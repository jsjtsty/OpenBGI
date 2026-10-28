# Find raw dword references (vtable entries, pointers) to given VAs in the exe image.  usage: find_refs.py 0x4cc7b0 ...
import sys, struct
import pefile
pe = pefile.PE(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe')
base = pe.OPTIONAL_HEADER.ImageBase
d = bytes(pe.get_memory_mapped_image())
for a in sys.argv[1:]:
    va = int(a, 0)
    pat = struct.pack('<I', va); o = 0; r = []
    while True:
        i = d.find(pat, o)
        if i < 0: break
        r.append(base + i); o = i + 1
    print(hex(va), [hex(x) for x in r])
