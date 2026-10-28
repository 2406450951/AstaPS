import re, sys, os

PATH = sys.argv[1]
NEEDLES = [n.encode() for n in (sys.argv[2].split(',') if len(sys.argv) > 2 else ['dispatch'])]
size = os.path.getsize(PATH)
print(f"file={PATH} size={size}")

CH = 8 << 20
MAXHIT = int(os.environ.get('MAXHIT', '40'))
hits = {n: 0 for n in NEEDLES}
overlap = 4096
with open(PATH, 'rb') as f:
    base = 0
    tail = b''
    while base < size:
        buf = f.read(CH)
        if not buf:
            break
        data = tail + buf
        dbase = base - len(tail)
        for n in NEEDLES:
            if hits[n] >= MAXHIT:
                continue
            start = 0
            while True:
                i = data.find(n, start)
                if i < 0:
                    break
                off = dbase + i
                lo = max(0, i - 120)
                hi = min(len(data), i + 200)
                ctx = data[lo:hi]
                printable = ''.join(chr(c) if 32 <= c < 127 else '.' for c in ctx)
                print(f"  0x{off:08X} [{n.decode()}] {printable}")
                hits[n] += 1
                start = i + 1
                if hits[n] >= MAXHIT:
                    break
        tail = data[-overlap:]
        base += len(buf)
print("done", {k.decode(): v for k, v in hits.items()})
