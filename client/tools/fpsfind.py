"""Locate the frame-rate globals in GenshinImpact.exe without touching the running game.

    python fpsfind.py scan                      # static scan of the exe -> fps_candidates.txt
    python fpsfind.py dump  out.txt             # read every candidate from the live process
    python fpsfind.py diff  before.txt after.txt

Workflow to identify the real cap field: dump, change the in-game FPS setting (30 <-> 60), dump
again, diff. The RVA whose value follows the setting is the frame-rate field.
"""
import ctypes
import ctypes.wintypes as wt
import sys

EXE = r"D:\Program Files\HoYoPlay\games\Genshin Impact game\GenshinImpact.exe"
PROC = "GenshinImpact.exe"
CANDIDATES_TXT = r"D:\dsh_workspace\dsh_code\genshi\clientpatch\fps_candidates.txt"


def scan():
    import pefile

    pe = pefile.PE(EXE, fast_load=True)
    ib = pe.OPTIONAL_HEADER.ImageBase
    out = []
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("latin1")
        if name not in (".text", ".data", ".rdata"):
            continue
        data = sec.get_data()
        if not data:
            continue
        va = sec.VirtualAddress
        for i in range(0, len(data) - 16):
            b0, b1 = data[i], data[i + 1]
            if b0 == 0xC7 and b1 == 0x05:
                imm = int.from_bytes(data[i + 6:i + 10], "little", signed=True)
                if imm not in (30, 60, 120, 144):
                    continue
                disp = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
                tgt = va + i + 10 + disp
            elif b0 == 0x89 and (b1 & 0xC7) == 0x05:
                disp = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
                tgt = va + i + 6 + disp
            else:
                continue
            if tgt <= 0 or tgt >= pe.OPTIONAL_HEADER.SizeOfImage:
                continue
            out.append(tgt)

    uniq = sorted(set(out))
    with open(CANDIDATES_TXT, "w", encoding="utf-8") as f:
        for t in uniq:
            f.write(f"0x{t:X}\n")
    print(f"image base 0x{ib:X}")
    print(f"{len(uniq)} candidate global(s) -> {CANDIDATES_TXT}")


def _pid():
    import subprocess
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {PROC}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].lower() == PROC.lower():
            return int(parts[1])
    raise SystemExit(f"{PROC} not running")


def _base():
    TH32CS_SNAPMODULE = 0x08
    TH32CS_SNAPMODULE32 = 0x10

    class MODULEENTRY32(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD),
                    ("th32ProcessID", wt.DWORD), ("GlblcntUsage", wt.DWORD),
                    ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)),
                    ("modBaseSize", wt.DWORD), ("hModule", wt.HMODULE),
                    ("szModule", ctypes.c_char * 256), ("szExePath", ctypes.c_char * 260)]

    k = ctypes.windll.kernel32
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, _pid())
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(MODULEENTRY32)
    if k.Module32First(snap, ctypes.byref(me)):
        while True:
            if me.szModule.decode(errors="replace").lower() == PROC.lower():
                return ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
            if not k.Module32Next(snap, ctypes.byref(me)):
                break
    raise SystemExit("module base not found")


def _reader():
    hp = ctypes.windll.kernel32.OpenProcess(0x0410, False, _pid())  # QUERY_INFO | VM_READ
    if not hp:
        raise SystemExit(f"OpenProcess failed: {ctypes.GetLastError()}")
    return hp, _base()


def dump(path):
    rd = ctypes.windll.kernel32.ReadProcessMemory
    hp, base = _reader()
    rvas = [int(x, 16) for x in open(CANDIDATES_TXT, encoding="utf-8").read().split()]
    buf = ctypes.c_int32()
    got = ctypes.c_size_t()
    n = 0
    with open(path, "w", encoding="utf-8") as f:
        for rva in rvas:
            if rd(hp, ctypes.c_void_p(base + rva), ctypes.byref(buf), 4, ctypes.byref(got)):
                f.write(f"0x{rva:X} {buf.value}\n")
                n += 1
    print(f"dumped {n}/{len(rvas)} values -> {path}")


def diff(a, b):
    va = dict(l.split() for l in open(a, encoding="utf-8") if l.strip())
    vb = dict(l.split() for l in open(b, encoding="utf-8") if l.strip())
    cnt = 0
    for k in va:
        if k in vb and va[k] != vb[k]:
            print(f"  RVA {k}: {va[k]} -> {vb[k]}")
            cnt += 1
    print(f"{cnt} field(s) changed")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "scan"
    if mode == "scan":
        scan()
    elif mode == "dump":
        dump(sys.argv[2])
    elif mode == "diff":
        diff(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
