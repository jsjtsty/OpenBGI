# usage: py disasm_fn.py <VA hex> [maxbytes] -- x86-32 disassembly of one function of ac2_ML.exe (capstone); size from out/ac2_ML.functions.tsv
import sys,struct,capstone
d=open(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
def off(va):
    r=va-0x400000
    for vs,a,rs,ra in secs:
        if a<=r<a+max(vs,rs): return ra+r-a
va=int(sys.argv[1],16); size=int(sys.argv[2]) if len(sys.argv)>2 else None
if size is None:
    for l in open(r'D:\Projects\C++\OpenBGI\analysis\out\ac2_ML.functions.tsv'):
        f=l.split('\t')
        if f[0].lower()=='%08x'%va: size=int(f[1]);break
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for i in md.disasm(d[off(va):off(va)+size],va):
    print('%08x  %-6s %s'%(i.address,i.mnemonic,i.op_str))
