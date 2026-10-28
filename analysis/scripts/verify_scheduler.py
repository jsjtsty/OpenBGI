# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_scheduler.py
# Run original FUN_0049be00 with synthetic threads, test handlers and wait objects.
# Programs, wait result, target and callbacks are synthetic; the dispatcher is native.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX
pack=lambda x:struct.pack('<I',x&0xffffffff)
def run(first,second,wait=None,target=202):
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 dtor=k.alloc(b'\xc2\x04\x00');vtable=k.alloc(pack(dtor)+bytes(28));root=k.alloc(bytes(0x100));a=k.alloc(bytes(0x100));b=k.alloc(bytes(0x100))
 for obj,ident,prog,nextobj in ((root,0,[],a),(a,101,first,b),(b,202,second,0)):
  buf=k.alloc(bytes(prog)+bytes(16));u.mem_write(obj,pack(vtable));u.mem_write(obj+8,pack(ident));u.mem_write(obj+0xc,pack(nextobj));u.mem_write(obj+0x2c,pack(buf))
 for op,ret in ((0xe0,1),(0xe1,2),(0xe2,3),(0xe3,6),(0xe4,5),(0xe5,4)):
  stub=k.alloc(b'\xb8'+pack(ret)+b'\xc3');u.mem_write(0x52f660+op*4,pack(stub))
 u.mem_write(0x54adb0,pack(target));obs=[]
 def capture(uc,addr,size,user):
  obj=uc.reg_read(UC_X86_REG_ECX);ip=get(obj+0x80);ptr=get(obj+0x2c);obs.append((get(obj+8),k.read(ptr+ip,1)[0]))
 u.hook_add(UC_HOOK_CODE,capture,None,0x44d160,0x44d160)
 if wait is not None:
  dtor=k.alloc(b'\xc2\x04\x00');update=k.alloc(b'\xb8'+pack(wait)+b'\xc3');wt=k.alloc(pack(dtor)+pack(update));wo=k.alloc(pack(wt)+bytes(16))
  u.mem_write(a+0x5c,pack(wo));u.mem_write(a+0x74,pack(1))
 k.call(0x49be00,root,0)
 return obs,get(a+0x5c),get(a+0x74)
tests=[
 ('return2 continues current',[0xe1,0xe0],[0xe0],None,[(101,0xe1),(101,0xe0),(202,0xe0)]),
 ('return6 suppresses following',[0xe3],[0xe0],None,[(101,0xe3)]),
 ('return3 switches target',[0xe2],[0xe0],None,[(101,0xe2),(202,0xe0)]),
 ('return5 goes to next',[0xe4],[0xe0],None,[(101,0xe4),(202,0xe0)]),
 ('return4 terminates thread',[0xe5],[0xe0],None,[(101,0xe5),(202,0xe0)]),
 ('wait0 skips thread',[0xe0],[0xe0],0,[(202,0xe0)]),
 ('wait1 resumes thread',[0xe0],[0xe0],1,[(101,0xe0),(202,0xe0)]),
 ('wait-1 suppresses following',[0xe0],[0xe0],-1,[]),
]
for name,first,second,wait,expected in tests:
 obs,ptr,flags=run(first,second,wait);assert obs==expected,(name,obs,expected)
 if wait==0:assert ptr!=0 and flags&1
 if wait in (1,-1):assert ptr==0 and not flags&1
 print('PASS',name,obs)
obs,_,_=run([0xe2],[0xe0],target=999);assert obs==[(101,0xe2)]
print('PASS return3 to absent thread ends this dispatcher pass',obs)
