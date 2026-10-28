"""Verify the game install against the client's own pkg_version manifest."""
import hashlib
import json
import os
import sys

ROOT = sys.argv[1]
CHECK_MD5 = "--md5" in sys.argv
MAX_MD5_BYTES = 64 * 1024 * 1024

manifest = os.path.join(ROOT, "pkg_version")
missing = []
wrong = []
ok = 0
total = 0
small_total = 0

with open(manifest, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except Exception:
            continue
        name = (entry.get("remoteName") or "").replace("\\", "/")
        if not name:
            continue
        total += 1
        path = os.path.join(ROOT, name.replace("/", os.sep))
        if not os.path.isfile(path):
            missing.append(name)
            continue
        size = os.path.getsize(path)
        if not CHECK_MD5 or size > MAX_MD5_BYTES:
            ok += 1
            continue
        h = hashlib.md5()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        small_total += 1
        if h.hexdigest().lower() != (entry.get("md5") or "").lower():
            wrong.append((name, size))
        else:
            ok += 1

print("manifest entries :", total)
print("present          :", ok)
print("missing          :", len(missing))
for m in missing[:40]:
    print("   MISSING", m)
if len(missing) > 40:
    print("   ... and", len(missing) - 40, "more")
if CHECK_MD5:
    print("md5 checked      :", small_total)
    print("md5 mismatched   :", len(wrong))
    for n, s in wrong[:30]:
        print("   BAD", n, s)
