# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_global_alloc.py
# Native 80:70 + FUN_004798b0; allocator and free stubbed, actual memset native.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda v:struct.pack('<I',v&0xffffffff)
k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0];stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));allocations=[];freed=[]
for off,val in ((0x10,16),(0x18,stack)):u.mem_write(thread+off,pack(val))
def hook(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);arg=get(sp+4)
 if addr==0x4cf54b:
  value=k.alloc(b'\xcc'*arg);allocations.append((value,arg));uc.reg_write(UC_X86_REG_EAX,value)
 else:freed.append(arg)
 uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
for va in (0x4cf54b,0x4cf508):u.hook_add(UC_HOOK_CODE,hook,None,va,va)
for exp in (0,1,5):
 u.mem_write(stack,pack(exp));u.mem_write(thread+0x78,pack(1));k.call(0x498260,0,0,thread)
 base,size=allocations[-1];assert size==0x1000<<exp and get(0x5485e0)==base and get(0x5485dc)==size
 assert k.read(base,size)==bytes(size) and get(thread+0x78)==1 and get(stack)==1 and u.reg_read(UC_X86_REG_EAX)==0
 print(f'80:70 exponent={exp} size={size} initialized to zero, push 1, return 0')
assert freed==[allocations[0][0],allocations[1][0]]
print('PASS: native allocation wrapper and reallocation; allocator/free stubbed')
