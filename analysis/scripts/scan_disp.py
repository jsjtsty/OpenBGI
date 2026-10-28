# Scan the exe .text for memory operands with a given displacement in a VA range.  usage: scan_disp.py 0x68 0x4c6000 0x4d0000
import sys, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_MEM
EXE = r'E:\Games\Amairo Chocolate 2\ac2_ML.exe'
disp = int(sys.argv[1], 0); lo = int(sys.argv[2], 0); hi = int(sys.argv[3], 0)
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
data = pe.get_memory_mapped_image()
code = data[lo - base:hi - base]
for i in md.disasm(code, lo):
    for op in i.operands:
        if op.type == X86_OP_MEM and op.mem.disp == disp and op.mem.base != 0:
            print('%x  %s %s' % (i.address, i.mnemonic, i.op_str))
            break
