# Capture per-block-row coefficient buffers and output pixels from the original BM decoder (FUN_004af400).
# usage: .venv\Scripts\python.exe scripts\bm_capture.py <arc> <member> <frame> <out.pkl>
import sys,struct,pickle,pathlib
sys.path.insert(0,'scripts')
import emu,decode_bm as D
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
arc,name,frame,outp=sys.argv[1],sys.argv[2],int(sys.argv[3]),sys.argv[4]
data=D.member(arc,name)
version,w,h,bits,fmt,rate,frames=struct.unpack_from('<7I',data,16)
k=emu.Kernel();u=k.u;u.mem_map(0,0x1000);u.mem_write(0,b'\xff'*4)
get=lambda a:u32(k.read(a,4))
def alloc(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);n=get(sp+4);p=k.alloc(bytes(n));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
def free(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
for a in (0x4cf50d,0x4cf54b,0x4dfb3c):u.hook_add(UC_HOOK_CODE,alloc,None,a,a)
for a in (0x4cf508,0x4cf53d,0x4e0964,0x4dfb22):u.hook_add(UC_HOOK_CODE,free,None,a,a)
pool=k.alloc(bytes(0x100));u.mem_write(pool+4,struct.pack('<II',1,1))
obj=k.alloc(bytes(0x300));fileaddr=k.alloc(data+bytes(16));pixels=k.alloc(bytes(w*h*4));bitmap=k.alloc(struct.pack('<6I',pixels,w*4,w,h,fmt,4))
k.call(0x4acf80,obj,0,pool)
scratch=k.heap
caps=[]
caps=[]
def on_entry(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);p=get(sp+4)
 hdr=k.read(p,0x84);w8=u32(hdr,0x40);cb=u32(hdr,0x20)
 caps.append(dict(hdr=hdr,coef=k.read(cb,w8*0x30)))
u.hook_add(UC_HOOK_CODE,on_entry,None,0x4af400,0x4af400)
for fi in range(frame+1):
 if fi==frame:prev=k.read(pixels,w*h*4);caps.clear()
 k.heap=scratch;k.call(0x4aeb40,obj,0,bitmap,fileaddr,fi)
 res=u.reg_read(UC_X86_REG_EAX)
 if res:raise SystemExit('decode error %d at frame %d'%(res,fi))
print('result',res,'caps',len(caps))
pickle.dump(dict(prev=prev,w=w,h=h,bits=bits,fmt=fmt,frame=frame,caps=caps,pixels=k.read(pixels,w*h*4)),open(outp,'wb'))
