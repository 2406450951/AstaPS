"""Map an RVA to its function (from the PE exception directory) and disassemble it."""
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PATH = sys.argv[1]
TARGETS = [int(x, 16) for x in sys.argv[2:]]

pe = pefile.PE(PATH, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
funcs = [(f.struct.BeginAddress, f.struct.EndAddress) for f in pe.DIRECTORY_ENTRY_EXCEPTION]
funcs.sort()
print("{} functions in .pdata".format(len(funcs)))

data = open(PATH, "rb").read()


def rva_to_off(rva):
    for s in pe.sections:
        span = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if s.VirtualAddress <= rva < s.VirtualAddress + span:
            d = rva - s.VirtualAddress
            if d < s.SizeOfRawData:
                return s.PointerToRawData + d
    return None


md = Cs(CS_ARCH_X86, CS_MODE_64)

for rva in TARGETS:
    hit = None
    for b, e in funcs:
        if b <= rva < e:
            hit = (b, e)
            break
    print("\n===== RVA 0x{:X} -> function {}".format(
        rva, "0x{:X}..0x{:X} (size 0x{:X})".format(hit[0], hit[1], hit[1] - hit[0]) if hit else "NOT FOUND"))
    if not hit:
        continue
    off = rva_to_off(hit[0])
    if off is None:
        print("  no raw data")
        continue
    code = data[off:off + min(hit[1] - hit[0], 0x600)]
    for insn in md.disasm(code, hit[0]):
        mark = "   <== probe" if insn.address == rva else ""
        print("  0x{:08X}: {:10s} {}{}".format(insn.address, insn.mnemonic, insn.op_str, mark))
