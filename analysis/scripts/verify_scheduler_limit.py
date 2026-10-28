# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_scheduler_limit.py
# Original FUN_0049be00 executes 0x100000 zero-return instructions in one thread.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX
pack=lambda x:struct.pack('<I',x&0xffffffff)
k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
root=k.alloc(bytes(0x100));a=k.alloc(bytes(0x100));b=k.alloc(bytes(0x100))
code_a=k.alloc(b'\xe6'*0x100000);code_b=k.alloc(b'\xe0')
for obj,ident,prog,nxt in ((root,0,0,a),(a,101,code_a,b),(b,202,code_b,0)):
 u.mem_write(obj+8,pack(ident));u.mem_write(obj+0xc,pack(nxt));u.mem_write(obj+0x2c,pack(prog))
for op,ret in ((0xe6,0),(0xe0,1)):
 stub=k.alloc(b'\xb8'+pack(ret)+b'\xc3');u.mem_write(0x52f660+op*4,pack(stub))
counts={101:0,202:0}
def capture(uc,addr,size,user):
 thread=uc.reg_read(UC_X86_REG_ECX);ident=get(thread+8);counts[ident]+=1
u.hook_add(UC_HOOK_CODE,capture,None,0x44d160,0x44d160)
k.call(0x49be00,root,0)
assert counts=={101:0x100000,202:1},counts
assert get(a+0x80)==0x100000 and get(a+0x74)==0 and get(root+0xc)==a
print('PASS: thread 101 executes 0x100000 ops then thread 202 executes; thread 101 remains active')
