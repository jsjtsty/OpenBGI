# Compare independent bm_dec.BM against the original decoder under Unicorn. usage: verify_bm.py <arc> <member> <nframes>
import sys,struct,time
sys.path.insert(0,'scripts')
import numpy as np,emu,decode_bm as D,bm_dec as B
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
arc,name,nf=sys.argv[1],sys.argv[2],int(sys.argv[3])
data=D.member(arc,name);u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
ver,w,h,bits,fmt,rate,frames=struct.unpack_from('<7I',data,16)
k=emu.Kernel();u=k.u;u.mem_map(0,0x1000);u.mem_write(0,b'\xff'*4)
get=lambda a:u32(k.read(a,4))
def alloc(uc,a,s,x):
 sp=uc.reg_read(UC_X86_REG_ESP);n=get(sp+4);p=k.alloc(bytes(n));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
def free(uc,a,s,x):
 sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
for a in (0x4cf50d,0x4cf54b,0x4dfb3c):u.hook_add(UC_HOOK_CODE,alloc,None,a,a)
for a in (0x4cf508,0x4cf53d,0x4e0964,0x4dfb22):u.hook_add(UC_HOOK_CODE,free,None,a,a)
pool=k.alloc(bytes(0x100));u.mem_write(pool+4,struct.pack('<II',1,1))
obj=k.alloc(bytes(0x300));fa=k.alloc(data+bytes(16));pix=k.alloc(bytes(w*h*4));bmp=k.alloc(struct.pack('<6I',pix,w*4,w,h,fmt,4))
k.call(0x4acf80,obj,0,pool);scratch=k.heap
m=B.BM(data);bad=0
for i in range(nf):
    k.heap=scratch;k.call(0x4aeb40,obj,0,bmp,fa,i);assert u.reg_read(UC_X86_REG_EAX)==0
    ref=np.frombuffer(k.read(pix,w*h*4),np.uint8).reshape(h,w,4)
    t=time.time();mine=m.frame(i)
    cmp_ch=4 if bits==32 else 3
    diff=(ref[:,:,:cmp_ch]!=mine[:,:,:cmp_ch]);nd=int(diff.sum())
    print('frame',i,'diff',nd,'t=%.1fs'%(time.time()-t),flush=True);bad+=nd
print('TOTAL diff',bad)
