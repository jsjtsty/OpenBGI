# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_bitmap_load_effect.py
# Execute native 90:10 cache-hit, synchronous and asynchronous control paths.
# Address resolution, image manager, allocator and asynchronous constructor are stubbed.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda x:struct.pack('<I',x&0xffffffff)
for label,cache,allow_sync,expected in (('cache hit',0,1,0),('sync miss',1,1,0),('async miss',1,0,2)):
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));name=k.alloc(b'test\0');fake=k.alloc(bytes(0x678))
 for off,val in ((0x10,16),(0x18,stack),(0x78,3)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(7)+pack(0x08000020)+pack(0x08000030))
 u.mem_write(0x49d0a0,b'\xb8'+pack(name)+b'\xc3')
 for addr,result in ((0x402200,cache),(0x4a4e50,0),(0x402640,allow_sync),(0x402300,0)):
  u.mem_write(addr,b'\xb8'+pack(result)+b'\xc3')
 # Four stack args to thiscall constructor, return the allocated object.
 u.mem_write(0x441f80,b'\x8b\xc1\xc2\x10\x00')
 def alloc(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EAX,fake);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 u.hook_add(UC_HOOK_CODE,alloc,None,0x4cf50d,0x4cf50d)
 k.call(0x488d20,0,0,thread)
 assert get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==expected
 assert bool(get(thread+0x5c))==bool(expected) and bool(get(thread+0x74)&1)==bool(expected)
 print(f'90:10 {label}: pop 3 push 0 return {expected} wait_installed={bool(expected)}')
print('PASS: native 90:10 control paths with resource and allocator stubs')
# 90:22: native handler validates two arguments, forwards seven and returns 2.
# Helper is stubbed, so this test does not prove wait-object installation.
k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
for off,val in ((0x10,16),(0x18,stack),(0x78,7)):u.mem_write(thread+off,pack(val))
u.mem_write(stack,pack(0)*7);u.mem_write(0x4a0e60,b'\x31\xc0\xc3');k.call(0x489bd0,0,0,thread)
assert get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==2
print('90:22 valid zeros: pop 7 push 0 return 2 (control helper stubbed)')
