# Compare native decoding of the same BM frame onto two prefilled buffers.
# Different results prove destination-state dependence for this frame/sample.
import struct,sys,pathlib
import numpy as np
import decode_bm,emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
arc,name=sys.argv[1:3];indexes=[int(x) for x in sys.argv[3:]] or [0,1,2,3]
data=decode_bm.member(arc,name);version,w,h,bits,fmt,rate,n=struct.unpack_from('<7I',data,16)
if any(i<0 or i>=n for i in indexes):raise ValueError('frame index out of range')
k=emu.Kernel();u=k.u;u.mem_map(0,0x1000);u.mem_write(0,b'\xff'*4)
get=lambda a:struct.unpack('<I',k.read(a,4))[0]
def alloc(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);n=get(sp+4);p=k.alloc(bytes(n));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
def free(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
for a in (0x4cf50d,0x4cf54b,0x4dfb3c):u.hook_add(UC_HOOK_CODE,alloc,None,a,a)
for a in (0x4cf508,0x4cf53d,0x4e0964,0x4dfb22):u.hook_add(UC_HOOK_CODE,free,None,a,a)
pool=k.alloc(bytes(0x100));u.mem_write(pool+4,struct.pack('<II',1,1));obj=k.alloc(bytes(0x300));fileaddr=k.alloc(data+bytes(16));pixels=k.alloc(bytes(w*h*4));bitmap=k.alloc(struct.pack('<6I',pixels,w*4,w,h,fmt,4))
scratch=k.heap
for index in indexes:
 results=[]
 for value in (0,255):
  k.heap=scratch;u.mem_write(obj,bytes(0x300));k.call(0x4acf80,obj,0,pool)
  u.mem_write(pixels,bytes([value])*(w*h*4));k.call(0x4aeb40,obj,0,bitmap,fileaddr,index)
  if u.reg_read(UC_X86_REG_EAX):raise ValueError('decoder returned error')
  results.append(np.frombuffer(k.read(pixels,w*h*4),dtype=np.uint8).reshape(-1,4))
 a,b=results;different=np.any(a!=b,axis=1);unchanged=np.all(a==0,axis=1)&np.all(b==255,axis=1)
 unchanged_rgb=np.all(a[:,:3]==0,axis=1)&np.all(b[:,:3]==255,axis=1)
 channels=np.count_nonzero(a!=b,axis=0)
 print(f'{name} frame {index}: destination-dependent pixels={np.count_nonzero(different)}/{w*h}; preserved BGRA={np.count_nonzero(unchanged)}, BGR={np.count_nonzero(unchanged_rgb)}; differing B,G,R,A={channels.tolist()}',flush=True)
