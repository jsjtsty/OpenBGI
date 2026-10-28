# Run the real blend dispatcher FUN_0040b9d0(ecx=dst, edx=src, mode, P, flag) in Unicorn: return codes and mode aliases.
# usage: .venv\Scripts\python.exe scripts\verify_dispatch.py
import random, itertools, struct, emu
random.seed(1); k=emu.Kernel(); u=k.u
from unicorn.x86_const import *
def run(mode,P,sf,df,w=8,h=3,flag=0,seed=5):
    r=random.Random(seed); bp=emu.BPP
    sp=bytes(r.randrange(256) for _ in range(w*h*bp[sf])); dp=bytes(r.randrange(256) for _ in range(w*h*bp[df]))
    k.heap=emu.HEAP; s,sa=k.bitmap(sp,w,h,sf); d,da=k.bitmap(dp,w,h,df)
    k.call(0x40b9d0,d,s,mode,P,flag); return u.reg_read(UC_X86_REG_EAX), k.read(da,w*h*bp[df])
fmts=[0,1,2,3]; res={}
for sf,df in itertools.product(fmts,fmts):
    for mode in list(range(0,10))+[0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x40,0x41,0x80,0xC0,0xC1,0xF0,0xFF,0x30,0x100]:
        for P in (0,1,128,255,256,257):
            res[(sf,df,mode,P)]=run(mode,P,sf,df)
# return-code table
codes={}
for (sf,df,m,P),(rc,_) in res.items(): codes.setdefault(rc,set()).add((m if m<0x100 else 'big'))
print('return codes ->modes seen:',{c:sorted(map(str,v))[:12] for c,v in codes.items()})
print('P=257 rc:',{res[(2,2,m,257)][0] for m in (0,1,5,0x80)})
print('mode 0x100/0x30 rc:',{res[(2,2,m,128)][0] for m in (0x100,0x30)})
print('format-incompatible pairs (rc==1):',sorted({(sf,df) for (sf,df,m,P),(rc,_) in res.items() if rc==1}))
def same(a,b): return all(res[(sf,df,a,P)]==res[(sf,df,b,P)] for sf,df in itertools.product(fmts,fmts) for P in (0,1,128,255,256) if res[(sf,df,a,P)][0]==0)
print('0x20==1:',same(0x20,1),' 0xC0==5:',same(0xC0,5))
for a,b in ((0x21,2),(0x22,3),(0x23,4),(0x24,6),(0x25,7),(0x26,8),(0x27,9)):
    ok=all(res[(sf,df,a,P)]==res[(sf,df,b,256-P)] for sf,df in itertools.product(fmts,fmts) for P in (0,1,128,255,256) if res[(sf,df,a,P)][0]==0)
    print(hex(a),'== mode',b,'with P\'=256-P:',ok)
