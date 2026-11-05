"""Write the 7.2 protocol as one .proto per type under src/main/proto.

Inputs: the remapped 7.2 descriptor set (server names, 7.2 numbers), the 7.1 server
descriptor set, the kitkat 7.1.0 dump, the recovered 7.2 (NT) dump, PacketOpcodes.java
and a list of protocol classes the Java code references.

- Every server type with a readable name is kept, plus obfuscated types they or the Java
  code reference. Other obfuscated types are dropped.
- With --with-nt-additions, named NT messages the server does not have yet are added. Their
  references to types the server already has use the server's name; other referenced types
  are added too. Without it only the server's own types are written.
  NT enums (`message X { enum Value }`) become plain enums with prefixed value names.
"""
import os
import re
import sys

from google.protobuf import descriptor_pb2

sys.path.insert(0, os.path.dirname(__file__))
from remap_71_to_72 import compute_maps, obf  # noqa: E402

FDP = descriptor_pb2.FieldDescriptorProto
SCALAR_NAMES = {
    FDP.TYPE_DOUBLE: 'double', FDP.TYPE_FLOAT: 'float', FDP.TYPE_INT64: 'int64',
    FDP.TYPE_UINT64: 'uint64', FDP.TYPE_INT32: 'int32', FDP.TYPE_FIXED64: 'fixed64',
    FDP.TYPE_FIXED32: 'fixed32', FDP.TYPE_BOOL: 'bool', FDP.TYPE_STRING: 'string',
    FDP.TYPE_BYTES: 'bytes', FDP.TYPE_UINT32: 'uint32', FDP.TYPE_SFIXED32: 'sfixed32',
    FDP.TYPE_SFIXED64: 'sfixed64', FDP.TYPE_SINT32: 'sint32', FDP.TYPE_SINT64: 'sint64',
}
SCALARS = set(SCALAR_NAMES.values())
HEADER = 'syntax = "proto3";\n\noption java_package = "emu.grasscutter.net.proto";\n'


# ---------- descriptor -> .proto text ----------

def field_type(fd):
    if fd.type in (FDP.TYPE_MESSAGE, FDP.TYPE_ENUM):
        return fd.type_name.lstrip('.')
    return SCALAR_NAMES[fd.type]


def print_enum(enum, indent):
    pad = '  ' * indent
    out = [f'{pad}enum {enum.name} {{']
    if enum.options.allow_alias:
        out.append(f'{pad}  option allow_alias = true;')
    for v in enum.value:
        out.append(f'{pad}  {v.name} = {v.number};')
    out.append(f'{pad}}}')
    return out


def print_message(msg, indent):
    pad = '  ' * indent
    out = [f'{pad}message {msg.name} {{']
    maps = {n.name: n for n in msg.nested_type if n.options.map_entry}
    for n in msg.nested_type:
        if not n.options.map_entry:
            out += print_message(n, indent + 1)
    for e in msg.enum_type:
        out += print_enum(e, indent + 1)
    oneofs = {}
    for fd in msg.field:
        if fd.HasField('oneof_index') and not fd.proto3_optional:
            oneofs.setdefault(fd.oneof_index, []).append(fd)

    def field_line(fd, p):
        entry = maps.get(fd.type_name.split('.')[-1]) if fd.type == FDP.TYPE_MESSAGE else None
        if entry is not None and fd.label == FDP.LABEL_REPEATED:
            typ = f'map<{field_type(entry.field[0])}, {field_type(entry.field[1])}>'
            label = ''
        else:
            typ = field_type(fd)
            label = 'repeated ' if fd.label == FDP.LABEL_REPEATED else ''
        opts = ''
        if fd.options.HasField('packed'):
            opts = f' [packed = {"true" if fd.options.packed else "false"}]'
        return f'{p}{label}{typ} {fd.name} = {fd.number}{opts};'

    emitted_oneofs = set()
    for fd in msg.field:
        if fd.HasField('oneof_index') and not fd.proto3_optional:
            idx = fd.oneof_index
            if idx in emitted_oneofs:
                continue
            emitted_oneofs.add(idx)
            out.append(f'{pad}  oneof {msg.oneof_decl[idx].name} {{')
            for member in oneofs[idx]:
                out.append(field_line(member, pad + '    '))
            out.append(f'{pad}  }}')
        else:
            out.append(field_line(fd, pad + '  '))
    out.append(f'{pad}}}')
    return out


def print_file(f):
    lines = [HEADER]
    if f.options.java_outer_classname:
        lines[0] += f'option java_outer_classname = "{f.options.java_outer_classname}";\n'

    for dep in f.dependency:
        lines.append(f'import "{dep}";')
    if f.dependency:
        lines.append('')
    for e in f.enum_type:
        lines += print_enum(e, 0)
        lines.append('')
    for m in f.message_type:
        lines += print_message(m, 0)
        lines.append('')
    return '\n'.join(lines).rstrip() + '\n'


# ---------- NT (recovered 7.2) parser with oneofs and enum wrappers ----------

def parse_nt(path):
    """name -> {'fields': [(label, type, name, num, oneof)], 'enum': [(name, num)] or None}"""
    types = {}
    stack = []
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.strip()
        if not line or line.startswith('//'):
            continue
        m = re.match(r'(message|enum|oneof)\s+(\w+)\s*\{', line)
        if m:
            kind, name = m.groups()
            stack.append((kind, name))
            if kind == 'message' and len(stack) == 1:
                types[name] = {'fields': [], 'enum': None}
            elif kind == 'enum' and len(stack) == 2 and stack[0][0] == 'message':
                types[stack[0][1]]['enum'] = []
            continue
        if line.startswith('}'):
            if stack:
                stack.pop()
            continue
        if not stack:
            continue
        top = types.get(stack[0][1])
        if top is None:
            continue
        if stack[-1][0] == 'enum' and len(stack) == 2:
            e = re.match(r'(\w+)\s*=\s*(-?\d+)', line)
            if e:
                top['enum'].append((e.group(1), int(e.group(2))))
            continue
        oneof = stack[1][1] if len(stack) == 2 and stack[1][0] == 'oneof' else None
        if len(stack) == 1 or oneof:
            f = re.match(r'(repeated\s+)?(map<[^>]+>|[\w.]+)\s+(\w+)\s*=\s*(\d+)', line)
            if f:
                types[stack[0][1]]['fields'].append(
                    ('repeated' if f.group(1) else '', f.group(2), f.group(3), int(f.group(4)), oneof))
    return types


def upper_snake(name):
    s = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', name)
    return re.sub(r'[^A-Za-z0-9]', '_', s).upper()


def main(server72, server71, kitkat, r72, opcodes71, java_refs, out_dir, *flags):
    with_nt_additions = '--with-nt-additions' in flags
    fds72 = descriptor_pb2.FileDescriptorSet()
    fds72.ParseFromString(open(server72, 'rb').read())
    files = {f.name: f for f in fds72.file}
    type_file = {}
    s_messages, s_enums = set(), set()
    for f in fds72.file:
        for m in f.message_type:
            type_file[m.name] = f.name
            s_messages.add(m.name)
        for e in f.enum_type:
            type_file[e.name] = f.name
            s_enums.add(e.name)

    # 1. keep named server types, plus whatever they or the Java code reference
    used = {l.strip() for l in open(java_refs) if l.strip()}
    roots = [n for n, f in files.items()
             if any(not obf(t.name) for t in list(f.message_type) + list(f.enum_type))
             or any(t.name in used for t in list(f.message_type) + list(f.enum_type))]
    keep, todo = set(), list(roots)
    while todo:
        n = todo.pop()
        if n in keep:
            continue
        keep.add(n)
        todo += [d for d in files[n].dependency if d in files]

    # 2. named NT messages the server lacks
    _, S71, K, R, kcmd, rcmd, ops, s2k, k2r, alias = compute_maps(server71, kitkat, r72, opcodes71)
    r2s = {}
    for s, k in s2k.items():
        r = k2r.get(k)
        if r and r not in r2s:
            r2s[r] = s
    for name in R:
        if name in s_messages and name not in r2s:
            r2s[name] = name
    nt = parse_nt(r72)
    is_enum = {n for n, t in nt.items() if t['enum'] is not None}

    new_types = {}  # NT name -> emitted name
    queue = []

    # File and class names must also differ ignoring case, or they overwrite each other on
    # Windows and macOS checkouts.
    taken = {n.lower() for n in type_file} | {f.lower()[:-6] for f in files}

    def emitted_name(nt_name):
        if nt_name in new_types:
            return new_types[nt_name]
        name = nt_name
        if name.lower() in taken:  # an unrelated type whose name differs at most in case
            name = name + '_72'
        n = 2
        while name.lower() in taken:
            name = f'{nt_name}_72_{n}'
            n += 1
        taken.add(name.lower())
        new_types[nt_name] = name
        queue.append(nt_name)
        return name

    def resolve(t):
        """NT type reference -> (emitted type, file to import or None)."""
        if t in SCALARS:
            return t, None
        if t.endswith('.Value'):
            base = t[:-6]
            if base in s_enums and files[type_file[base]].name in keep:
                return base, type_file[base]
            n = emitted_name(base)
            return n, n + '.proto'
        base = t.split('.')[-1]
        if base in r2s and type_file.get(r2s[base]) in keep:
            return r2s[base], type_file[r2s[base]]
        if base in is_enum:
            n = emitted_name(base)
            return n, n + '.proto'
        if base not in nt:
            return None, None
        n = emitted_name(base)
        return n, n + '.proto'

    if with_nt_additions:
        for name in nt:
            if obf(name) or name in is_enum or name in r2s or name in s_messages:
                continue
            emitted_name(name)

    defs = {}  # emitted name -> (new type deps, server files, text)
    dropped_fields = 0
    while queue:
        nt_name = queue.pop()
        out_name = new_types[nt_name]
        t = nt[nt_name]
        if t['enum'] is not None:
            prefix = upper_snake(out_name)
            seen = set()
            lines = [f'enum {out_name} {{']
            if not any(num == 0 for _, num in t['enum']):
                lines.append(f'  {prefix}_UNSPECIFIED_0 = 0;')
            names = []
            for vname, num in t['enum']:
                vn = f'{prefix}_{upper_snake(vname)}'
                while vn in seen:
                    vn += '_'
                seen.add(vn)
                names.append((vn, num))
            for vn, num in names:
                # protoc adds an int constant <NAME>_VALUE for every value, which would clash
                # with a value that is itself called <NAME>_VALUE.
                if vn.endswith('_VALUE') and vn[:-6] in seen:
                    vn += '_'
                lines.append(f'  {vn} = {num};')
            lines.append('}')
            defs[out_name] = (set(), set(), '\n'.join(lines) + '\n')
            continue
        deps, server_files, body, oneofs = set(), set(), [], {}
        for label, typ, fname, num, oneof in t['fields']:
            target = typ[4:-1].split(',')[1].strip() if typ.startswith('map<') else typ
            rt, imp = resolve(target)
            if rt is None:
                dropped_fields += 1
                continue
            if imp:
                if imp.endswith('.proto') and imp[:-6] in set(new_types.values()) and imp not in type_file.values():
                    deps.add(imp[:-6])
                else:
                    server_files.add(imp)
            if typ.startswith('map<'):
                key = typ[4:-1].split(',')[0].strip()
                line = f'map<{key}, {rt}> {fname} = {num};'
            else:
                line = f'{"repeated " if label else ""}{rt} {fname} = {num};'
            if oneof:
                oneofs.setdefault(oneof, []).append(line)
            else:
                body.append('  ' + line)
        for oname, members in oneofs.items():
            body.append(f'  oneof {oname} {{')
            body += ['    ' + m for m in members]
            body.append('  }')
        deps.discard(out_name)
        cmd = rcmd.get(nt_name)
        comment = f'// 7.2 CmdId {cmd}\n' if cmd else ''
        text = comment + f'message {out_name} {{\n' + '\n'.join(body) + ('\n' if body else '') + '}\n'
        defs[out_name] = (deps, server_files, text)

    # Mutually recursive NT types cannot import each other, so each strongly connected
    # group shares one file named after its first member.
    index, low, on, stack, sccs, counter = {}, {}, set(), [], [], [0]
    sys.setrecursionlimit(100000)

    def strong(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in defs[v][0]:
            if w not in defs:
                continue
            if w not in index:
                strong(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            group = []
            while True:
                w = stack.pop()
                on.discard(w)
                group.append(w)
                if w == v:
                    break
            sccs.append(sorted(group))

    for v in sorted(defs):
        if v not in index:
            strong(v)
    file_of = {}
    for group in sccs:
        for v in group:
            file_of[v] = group[0] + '.proto'
    new_texts = {}
    for group in sccs:
        fname = group[0] + '.proto'
        imports = set()
        for v in group:
            imports |= defs[v][1]
            imports |= {file_of[d] for d in defs[v][0] if d in file_of}
        imports.discard(fname)
        head = HEADER + f'option java_outer_classname = "{group[0]}OuterClass";\n' + \
            ''.join(f'import "{i}";\n' for i in sorted(imports))
        new_texts[fname] = head + ('\n' if imports else '') + '\n'.join(defs[v][2] for v in group)
    merged = sum(1 for g in sccs if len(g) > 1)

    os.makedirs(out_dir, exist_ok=True)
    for name in sorted(keep):
        open(os.path.join(out_dir, name), 'w').write(print_file(files[name]))
    for name, text in new_texts.items():
        open(os.path.join(out_dir, name), 'w').write(text)
    print(f'server files {len(files)}  kept {len(keep)}  dropped {len(files) - len(keep)}')
    print(f'new NT types {len(defs)} in {len(new_texts)} files ({merged} shared by recursive types)'
          f'  dropped unresolved fields {dropped_fields}')


if __name__ == '__main__':
    main(*sys.argv[1:])
