"""Disassemble around offsets reported by the in-process stack probe.

Usage:  cap_at.py <dll> <hexrva> [<hexrva> ...]
"""
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PATH = sys.argv[1]
OFFSETS = [int(x, 16) for x in sys.argv[2:]]

pe = pefile.PE(PATH, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
print("{}  base=0x{:X}  size=0x{:X}".format(PATH, image_base, pe.OPTIONAL_HEADER.SizeOfImage))
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode(errors="replace")
    print("  {:8s} va=0x{:08X} vsize=0x{:08X} raw=0x{:08X}".format(
        name, s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData))

data = open(PATH, "rb").read()
md = Cs(CS_ARCH_X86, CS_MODE_64)


def rva_to_off(rva):
    for s in pe.sections:
        span = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if s.VirtualAddress <= rva < s.VirtualAddress + span:
            delta = rva - s.VirtualAddress
            if delta < s.SizeOfRawData:
                return s.PointerToRawData + delta
    return None


for rva in OFFSETS:
    off = rva_to_off(rva)
    print("\n===== RVA 0x{:X} -> file 0x{:X} =====".format(rva, off if off is not None else -1))
    if off is None:
        print("  (not in a raw section)")
        continue
    back = 80
    start = max(0, off - back)
    code = data[start:off + 128]
    base_rva = rva - (off - start)
    for insn in md.disasm(code, base_rva):
        mark = "   <== probe" if insn.address == rva else ""
        print("  0x{:08X}: {:10s} {}{}".format(insn.address, insn.mnemonic, insn.op_str, mark))
