"""Diff the two stack dumps: find threads whose call chain changed after the verdict."""
import re
import sys

PATH = sys.argv[1]

lines = open(PATH, "r", encoding="utf-8", errors="replace").read().splitlines()

dumps = []
cur = None
for ln in lines:
    if "stack dump:" in ln and "===" in ln:
        cur = {}
        dumps.append(cur)
        continue
    if "stack dump end" in ln:
        cur = None
        continue
    m = re.match(r"^\[\d\d:\d\d:\d\d\]\s+T(\d+) rip=([0-9a-fA-F]+):(.*)$", ln)
    if m and cur is not None:
        cur[m.group(1)] = (m.group(2), m.group(3).strip())

print(f"dumps: {[len(d) for d in dumps]}")
if len(dumps) < 2:
    sys.exit(0)

d1, d2 = dumps[0], dumps[1]
changed = []
for tid in sorted(set(d1) & set(d2), key=lambda x: int(x)):
    a = d1[tid][1]
    b = d2[tid][1]
    if a != b:
        changed.append((tid, a, b))

print(f"threads whose stack changed: {len(changed)}")
for tid, a, b in changed:
    mark = ""
    if "MHYPBase" in b or "YuanShen" in b:
        mark = "  <== game code"
    print(f"\n--- T{tid}{mark}")
    print("  before:", a[:300])
    print("  after :", b[:300])

print("\n=== threads present only in dump2 ===")
for tid in sorted(set(d2) - set(d1), key=lambda x: int(x)):
    print(f"  T{tid}: {d2[tid][1][:260]}")

print("\n=== MHYPBase chains in dump2 (unique) ===")
seen = set()
for tid, (rip, chain) in d2.items():
    if "MHYPBase" not in chain:
        continue
    key = chain[:200]
    if key in seen:
        continue
    seen.add(key)
    print(f"  T{tid}: {chain[:240]}")
