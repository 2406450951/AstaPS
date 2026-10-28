import os
import sys

ROOT = sys.argv[1]
NEEDLES_W = ["Error Code:(", "ErrorCode:(", "客户端文件", "客户端已损坏", "损坏，请重新安装", "请重新安装客户端"]
NEEDLES_A = ["Error Code:(", "ErrorCode(", "client is damaged"]
MAXSIZE = 600 * 1024 * 1024

def found_in(path):
    hits = []
    try:
        size = os.path.getsize(path)
        if size == 0 or size > MAXSIZE:
            return hits
        with open(path, "rb") as f:
            data = f.read()
    except Exception:
        return hits

    for n in NEEDLES_W:
        b = n.encode("utf-16-le")
        i = data.find(b)
        if i >= 0:
            hits.append((n, "utf16", i))
    for n in NEEDLES_A:
        b = n.encode("ascii")
        i = data.find(b)
        if i >= 0:
            hits.append((n, "ascii", i))
    return hits

count = 0
for dirpath, dirnames, filenames in os.walk(ROOT):
    for fn in filenames:
        if not fn.lower().endswith((".dll", ".exe", ".sys", ".node", ".bin", ".dat", ".json", ".js", ".bytes", ".txt")):
            continue
        p = os.path.join(dirpath, fn)
        h = found_in(p)
        if h:
            count += 1
            print(os.path.relpath(p, ROOT), os.path.getsize(p))
            for n, enc, off in h:
                print("    ", repr(n), enc, hex(off))
print("files with hits:", count)
