"""Look for the (2,0,4) error constant used as a dword in gmesdk.dll, and disassemble around hits."""
import struct
import sys

PATH = sys.argv[1]
PATTERNS = [
    ("0x00020004", struct.pack("<I", 0x00020004)),
    ("0x020004", struct.pack("<I", 0x020004)),
    ("0204 (u16)", struct.pack("<H", 0x0204)),
    ("0x00040002", struct.pack("<I", 0x00040002)),
    ("0x040002", struct.pack("<I", 0x040002)),
]

data = open(PATH, "rb").read()
print("size", len(data))

for name, pat in PATTERNS:
    hits = []
    start = 0
    while True:
        i = data.find(pat, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
        if len(hits) > 60:
            break
    print(f"{name:14s} hits={len(hits)}  first={[hex(h) for h in hits[:12]]}")
