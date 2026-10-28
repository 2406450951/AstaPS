"""Compare the two stack dumps, but only care about frames inside MHYPBase.dll (the login SDK)."""
import re
import sys
from collections import Counter

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
        cur[m.group(1)] = m.group(3).strip()

print("dumps:", [len(d) for d in dumps])
if len(dumps) < 2:
    sys.exit(0)
d1, d2 = dumps[0], dumps[1]


def mhyp_frames(chain):
    return [tok for tok in chain.split() if tok.lower().startswith("mhypbase")]


count1 = Counter()
count2 = Counter()
for tid, chain in d1.items():
    for f in mhyp_frames(chain):
        count1[f] += 1
for tid, chain in d2.items():
    for f in mhyp_frames(chain):
        count2[f] += 1

new_frames = {f: c for f, c in count2.items() if f not in count1}
print("\n== MHYPBase frames that only appear in dump #2 ==")
for f, c in sorted(new_frames.items(), key=lambda kv: -kv[1]):
    print(f"   {f}   x{c}")

print("\n== threads whose MHYPBase chain changed ==")
for tid in sorted(set(d1) & set(d2), key=lambda x: int(x)):
    a = mhyp_frames(d2[tid]) if False else mhyp_frames(d1[tid])
    b = mhyp_frames(d2[tid])
    if a != b and (a or b):
        print(f"\n--- T{tid}")
        print("  before:", " ".join(a) if a else "(none)")
        print("  after :", " ".join(b) if b else "(none)")

print("\n== dump2-only threads with MHYPBase frames ==")
for tid in sorted(set(d2) - set(d1), key=lambda x: int(x)):
    fr = mhyp_frames(d2[tid])
    if fr:
        print(f"  T{tid}: {' '.join(fr)}")
