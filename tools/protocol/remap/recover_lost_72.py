"""Recover 7.2 numbers for server fields the remap left parked (20000 and up).

The dumps name a field the same way in every message: kitkat 7.1 and the NT 7.2 dump each use
one name, readable or obfuscated, for a given field wherever it appears. When a 7.1 name A and
a 7.2 name B are both left unmatched in the same mapped message pairs, every time either of
them is, and their types agree, they are the same field. That needs at least two such pairs.

A second, weaker rule handles fields seen in one message only: when a mapped pair has exactly
one unmatched 7.1 field and one unmatched 7.2 field of a given type, they are paired.

Usage: recover_lost_72.py <7.1 server.desc> <kitkat 7.1.0> <NT 7.2> <PacketOpcodes.java>
       <protoc> <src/main/proto> [--single]
Rewrites the parked numbers in src/main/proto in place and prints what it changed.
"""
import collections
import os
import re
import subprocess
import sys
import tempfile

from google.protobuf import descriptor_pb2

sys.path.insert(0, os.path.dirname(__file__))
from remap_71_to_72 import compute_maps, norm, obf  # noqa: E402

PARK = 20000
SCALARS = {'double', 'float', 'int32', 'int64', 'uint32', 'uint64', 'sint32', 'sint64',
           'fixed32', 'fixed64', 'sfixed32', 'sfixed64', 'bool', 'string', 'bytes'}


def kind(typ, k2r=None, side='k'):
    """Comparable shape of a field type: scalar name, 'enum', 'map:<value kind>' or msg:<R name>."""
    if typ.startswith('map<'):
        key, val = [x.strip() for x in typ[4:-1].split(',')]
        return f'map:{key}:{kind(val, k2r, side)}'
    if typ in SCALARS:
        return typ
    if typ.endswith('.Value'):
        return 'enum'
    base = typ.split('.')[-1]
    if side == 'k':
        if k2r is not None and base in k2r:
            return 'msg:' + k2r[base]
        return 'enum-or-msg'
    return 'msg:' + base


def compatible(a, b):
    if a == b:
        return True
    if a == 'enum-or-msg':
        return b == 'enum' or b.startswith('msg:')
    if a.startswith('map:') and b.startswith('map:'):
        ka, va = a[4:].split(':', 1)
        kb, vb = b[4:].split(':', 1)
        return ka == kb and compatible(va, vb)
    return False


def load_proto_dir(protoc, proto_dir):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'all.desc')
        files = sorted(f for f in os.listdir(proto_dir) if f.endswith('.proto'))
        subprocess.run([protoc, f'-I{proto_dir}', f'--descriptor_set_out={out}'] + files, check=True)
        fds = descriptor_pb2.FileDescriptorSet()
        fds.ParseFromString(open(out, 'rb').read())
    return {(f.name, m.name): m for f in fds.file for m in f.message_type}


def main(server71, kitkat, r72, opcodes, protoc, proto_dir, *flags):
    single = '--single' in flags
    _, S71, K, R, kcmd, rcmd, ops, s2k, k2r, alias = compute_maps(server71, kitkat, r72, opcodes)
    k_by_num = {m: {f[0]: (n, f[1]) for n, f in fs.items()} for m, fs in K.items()}

    # Unmatched fields of every mapped K/R message pair.
    pairs = []
    occ_k, occ_r, co = collections.Counter(), collections.Counter(), collections.Counter()
    for k, r in k2r.items():
        if k not in K or r not in R:
            continue
        rnames = {norm(n) for n in R[r]}
        knames = {norm(n) for n in K[k]}
        uk = {n: kind(t, k2r, 'k') for n, (num, t, rep) in K[k].items() if norm(n) not in rnames}
        ur = {n: kind(t, None, 'r') for n, (num, t, rep) in R[r].items() if norm(n) not in knames}
        pairs.append((k, r, uk, ur))
        for a in uk:
            occ_k[a] += 1
        for b in ur:
            occ_r[b] += 1
        for a, ta in uk.items():
            for b, tb in ur.items():
                if compatible(ta, tb):
                    co[(a, b)] += 1

    same = {}
    by_a = collections.defaultdict(list)
    for (a, b), n in co.items():
        if n >= 2 and n == occ_k[a] == occ_r[b]:
            by_a[a].append(b)
    for a, bs in by_a.items():
        if len(bs) == 1:
            same[a] = bs[0]
    taken_b = collections.Counter(same.values())
    same = {a: b for a, b in same.items() if taken_b[b] == 1}

    # Current server protocol and its parked fields.
    msgs = load_proto_dir(protoc, proto_dir)
    s71 = {m: {fd.name: fd.number for fd in S71[m].field} for m in S71}
    changes = collections.defaultdict(dict)
    rule_counts = collections.Counter()
    for (fname, mname), m in msgs.items():
        k = s2k.get(mname)
        r = k2r.get(k) if k else None
        if not k or not r or r not in R:
            continue
        used = {fd.number for fd in m.field if fd.number < PARK}
        parked = [fd for fd in m.field if fd.number >= PARK]
        if not parked:
            continue
        pair = next((p for p in pairs if p[0] == k and p[1] == r), None)
        for fd in parked:
            n71 = s71.get(mname, {}).get(fd.name)
            kf = k_by_num.get(k, {}).get(n71) if n71 else None
            a = kf[0] if kf else None
            b = same.get(a) if a else None
            rule = 'cross-message'
            if b is None and single and pair and a in pair[2]:
                ta = pair[2][a]
                cand_a = [x for x, t in pair[2].items() if t == ta]
                cand_b = [y for y, t in pair[3].items() if compatible(ta, t)]
                if len(cand_a) == 1 and len(cand_b) == 1:
                    b, rule = cand_b[0], 'single-message'
            if b is None or b not in R[r]:
                continue
            num = R[r][b][0]
            if num in used:
                continue
            used.add(num)
            changes[fname][(mname, fd.name)] = (fd.number, num, a, b, rule)
            rule_counts[rule] += 1

    for fname, edits in changes.items():
        path = os.path.join(proto_dir, fname)
        text = open(path).read()
        for (mname, field), (old, new, a, b, rule) in edits.items():
            text, n = re.subn(r'(\b' + re.escape(field) + r' = )' + str(old) + r';',
                              rf'\g<1>{new};', text)
            if n != 1:
                raise SystemExit(f'{fname}: could not rewrite {mname}.{field}')
        open(path, 'w').write(text)
        for (mname, field), (old, new, a, b, rule) in sorted(edits.items()):
            print(f'{mname}.{field}: {old} -> {new}  (7.1 {a} = 7.2 {b}, {rule})')
    print(f'equivalences {len(same)}  fields recovered {dict(rule_counts)}')


if __name__ == '__main__':
    main(*sys.argv[1:])
