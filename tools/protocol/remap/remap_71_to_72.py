"""Remap the merged 7.1 server descriptor set to 7.2 wire numbers.

S = our merged 7.1 descriptors (names/types the Java code uses, numbers from 7.1)
K = kitkat 7.1.0 Deobfuscated.proto (7.1 numbers with the best known names)
R = Recovered72.nt.proto (7.2 numbers)

Messages: S->K by 7.1 CmdId, name, or parent-field propagation by number;
K->R by name or parent-field propagation by field name.
Fields: S number -> K field at that number -> its name -> R field number.
"""
import json
import re
import sys

from google.protobuf import descriptor_pb2

OBF = re.compile(r'^[A-Z]{11}$')
SCALARS = {'double', 'float', 'int32', 'int64', 'uint32', 'uint64', 'sint32', 'sint64',
           'fixed32', 'fixed64', 'sfixed32', 'sfixed64', 'bool', 'string', 'bytes'}


def norm(name):
    return name.lower().replace('_', '')


def obf(name):
    return bool(OBF.match(name.lstrip('_')))


def parse_proto(path):
    """Top-level messages -> {field name: (number, type, repeated)}, plus CmdIds."""
    msgs, cmds = {}, {}
    pending = None
    stack = []  # (kind, name)
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.strip()
        m = re.match(r'//\s*CmdId:\s*(\d+)', line)
        if m:
            pending = int(m.group(1))
            continue
        if line.startswith('//') or not line:
            continue
        m = re.match(r'(message|enum|oneof)\s+(\w+)\s*\{', line)
        if m:
            kind, name = m.groups()
            stack.append((kind, name))
            if kind == 'message' and len(stack) == 1:
                msgs[name] = {}
                if pending:
                    cmds[name] = pending
            pending = None
            continue
        if line.startswith('}'):
            if stack:
                stack.pop()
            continue
        in_top = (len(stack) == 1 and stack[0][0] == 'message') or (
            len(stack) == 2 and stack[0][0] == 'message' and stack[1][0] == 'oneof')
        if not in_top:
            continue
        f = re.match(r'(repeated\s+|optional\s+)?(map<[^>]+>|[\w.]+)\s+(\w+)\s*=\s*(\d+)', line)
        if f:
            rep, typ, name, num = f.groups()
            msgs[stack[0][1]][name] = (int(num), typ, bool(rep))
    return msgs, cmds


def base_type(t):
    """Message type name used in a field (map value, or plain), or None for scalars."""
    if t.startswith('map<'):
        t = t[4:-1].split(',')[1].strip()
    if t in SCALARS:
        return None
    if t.endswith('.Value'):
        return None  # enum wrapper in the 7.2 recovery
    return t.split('.')[-1]


def compute_maps(server_desc, kitkat, r72, opcodes_file):
    """Returns (fds, S, K, R, kcmd, rcmd, ops, s2k, k2r, alias_sk)."""
    K, kcmd = parse_proto(kitkat)
    R, rcmd = parse_proto(r72)
    k_by_cmd = {v: k for k, v in kcmd.items()}
    k_by_num = {m: {f[0]: (n, f[1]) for n, f in fs.items()} for m, fs in K.items()}

    fds = descriptor_pb2.FileDescriptorSet()
    fds.ParseFromString(open(server_desc, 'rb').read())
    # S messages: top-level only, full name without leading dot
    S = {}
    for f in fds.file:
        for m in f.message_type:
            S[m.name] = m
    enums = {e.name for f in fds.file for e in f.enum_type}

    ops = {}
    for line in open(opcodes_file):
        m = re.match(r'\s*public static final int (\w+) = (\d+);(?:.*7\.1: (\d+))?', line)
        if m:
            value = int(m.group(3) or m.group(2))
            if value > 0:
                ops[m.group(1)] = value

    # Explicit renames the dumps cannot reach by themselves.
    alias_sk = {'WindSeedType1Notify': '_PlayerNormalLuaShellNotify'}

    s2k = {}
    for s in S:
        if s in alias_sk and alias_sk[s] in K:
            s2k[s] = alias_sk[s]
        elif s in ops and ops[s] in k_by_cmd:
            s2k[s] = k_by_cmd[ops[s]]
        elif s in K:
            s2k[s] = s

    def s_field_type(fd):
        if fd.type_name:
            return fd.type_name.split('.')[-1]
        return None

    changed = True
    while changed:  # propagate S->K through message-typed fields, matched by 7.1 number
        changed = False
        for s, k in list(s2k.items()):
            for fd in S[s].field:
                st = s_field_type(fd)
                if not st or st in s2k or st not in S:
                    continue
                kf = k_by_num.get(k, {}).get(fd.number)
                if kf:
                    kt = base_type(kf[1])
                    if kt and kt in K:
                        s2k[st] = kt
                        changed = True
            # map entry value types live in nested map entry messages
            for nested in S[s].nested_type:
                if not nested.options.map_entry:
                    continue
                for fd in S[s].field:
                    if fd.type_name.endswith('.' + nested.name):
                        vt = s_field_type(nested.field[1])
                        kf = k_by_num.get(k, {}).get(fd.number)
                        if vt and vt in S and vt not in s2k and kf:
                            kt = base_type(kf[1])
                            if kt and kt in K:
                                s2k[vt] = kt
                                changed = True

    k2r = {k: k for k in K if k in R}
    changed = True
    while changed:  # propagate K->R through named fields
        changed = False
        for k, r in list(k2r.items()):
            for name, (num, typ, rep) in K[k].items():
                kt = base_type(typ)
                if not kt or kt in k2r or kt not in K or obf(name):
                    continue
                rf = R.get(r, {}).get(name)
                if rf:
                    rt = base_type(rf[1])
                    if rt and rt in R:
                        k2r[kt] = rt
                        changed = True

    # A 7.1 dump message reached by CmdId may carry an obfuscated name that the 7.2 dump does
    # not share, while the server's own name for it is in the 7.2 dump.
    for s, k in s2k.items():
        if k not in k2r and s in R:
            k2r[k] = s

    return fds, S, K, R, kcmd, rcmd, ops, s2k, k2r, alias_sk


def main(server_desc, kitkat, r72, opcodes_file, out_desc, report_file):
    fds, S, K, R, kcmd, rcmd, ops, s2k, k2r, alias_sk = compute_maps(
        server_desc, kitkat, r72, opcodes_file)
    k_by_cmd = {v: k for k, v in kcmd.items()}
    k_by_num = {m: {f[0]: (n, f[1]) for n, f in fs.items()} for m, fs in K.items()}
    report = {'messages_mapped': 0, 'messages_unmapped': [], 'fields_lost': {},
              'fields_total': 0, 'fields_mapped': 0, 'cmd': {}}
    PARK = 20000

    def remap_message(msg, sname):
        k = s2k.get(sname)
        r = k2r.get(k) if k else (sname if sname in R else None)
        if r is None:
            report['messages_unmapped'].append(sname)
            return
        report['messages_mapped'] += 1
        rfields = {norm(n): v for n, v in R[r].items()}
        used = set()
        plan = {}
        # A message whose named fields all kept their numbers is treated as stable, so its
        # obfuscated fields may keep their numbers too when 7.2 has an obfuscated field there.
        kfields = K.get(k, {}) if k else {}
        named = [(n, v[0]) for n, v in kfields.items() if not obf(n) and norm(n) in rfields]
        stable = bool(named) and all(rfields[norm(n)][0] == num for n, num in named)
        r_obf_nums = {v[0]: v for n, v in R[r].items() if obf(n)}
        for fd in msg.field:
            report['fields_total'] += 1
            new = None
            if k:
                kf = k_by_num.get(k, {}).get(fd.number)
                if kf and not obf(kf[0]) and norm(kf[0]) in rfields:
                    new = rfields[norm(kf[0])][0]
            if new is None and norm(fd.name) in rfields and not obf(fd.name):
                new = rfields[norm(fd.name)][0]
            if new is None and stable and fd.number in r_obf_nums:
                new = fd.number
                report.setdefault('stable_kept', []).append(f'{sname}.{fd.name}={fd.number}')
            if new is not None and new not in used:
                plan[fd.name] = new
                used.add(new)
                report['fields_mapped'] += 1
        park = PARK
        for fd in msg.field:
            if fd.name in plan:
                fd.number = plan[fd.name]
            else:
                while park in used:
                    park += 1
                report['fields_lost'].setdefault(sname, []).append(f'{fd.name}={fd.number}')
                fd.number = park
                used.add(park)
                park += 1

    for f in fds.file:
        for m in f.message_type:
            remap_message(m, m.name)

    # 7.2 CmdIds for every opcode name
    newops = {}
    for name, old in ops.items():
        k = alias_sk.get(name) if name in alias_sk else (k_by_cmd.get(old) or (name if name in K else None))
        r = k2r.get(k) if k else None
        if r not in rcmd and name in rcmd:
            r = name  # the 7.2 dump knows the opcode by the server's name
        newops[name] = rcmd.get(r, 0) if r else 0
    report['cmd'] = newops
    report['s2k'] = len(s2k)
    report['k2r'] = len(k2r)

    open(out_desc, 'wb').write(fds.SerializeToString())
    json.dump(report, open(report_file, 'w'), indent=1, ensure_ascii=False)
    lost = sum(len(v) for v in report['fields_lost'].values())
    print(f"S messages {len(S)}  S->K {len(s2k)}  K->R {len(k2r)}  mapped {report['messages_mapped']}"
          f"  unmapped {len(report['messages_unmapped'])}")
    print(f"fields {report['fields_total']}  mapped {report['fields_mapped']}  lost {lost}")
    print(f"opcodes {len(ops)}  with 7.2 CmdId {sum(1 for v in newops.values() if v)}")


if __name__ == '__main__':
    main(*sys.argv[1:])
