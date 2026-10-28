import sys, os

def tobytes(pat):
    if pat.startswith('0x') or (all(c in '0123456789abcdefABCDEF' for c in pat) and len(pat) % 2 == 0 and len(pat) > 8):
        return bytes.fromhex(pat)
    return pat.encode('ascii')

def wide(s):
    return s.encode('utf-16-le')

PATH = sys.argv[1]
SPECS = sys.argv[2:]
NEEDLES = []
for s in SPECS:
    if s.startswith('w:'):
        NEEDLES.append((s[2:], wide(s[2:])))
    else:
        NEEDLES.append((s, tobytes(s)))

size = os.path.getsize(PATH)
print(f"file={PATH} size={size} needles={[n for n,_ in NEEDLES]}")
CH = 8 << 20
MAXHIT = int(os.environ.get('MAXHIT', '20'))
hits = {n: 0 for n, _ in NEEDLES}
overlap = 8192
with open(PATH, 'rb') as f:
    base = 0
    tail = b''
    while base < size:
        buf = f.read(CH)
        if not buf:
            break
        data = tail + buf
        dbase = base - len(tail)
        for name, n in NEEDLES:
            if hits[name] >= MAXHIT:
                continue
            start = 0
            while True:
                i = data.find(n, start)
                if i < 0:
                    break
                off = dbase + i
                lo = max(0, i - 64)
                hi = min(len(data), i + 160)
                ctx = data[lo:hi]
                printable = ''.join(chr(c) if 32 <= c < 127 else '.' for c in ctx)
                print(f"  0x{off:08X} [{name}] {printable}")
                hits[name] += 1
                start = i + 1
                if hits[name] >= MAXHIT:
                    break
        tail = data[-overlap:]
        base += len(buf)
print("done", hits)
