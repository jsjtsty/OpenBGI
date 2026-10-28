# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_wait_timing.py
# Emulates 80:5C handler and CProcWaitTimingEx::update with allocator and clock stubbed.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
k=emu.Kernel();u=k.u;pack=lambda v:struct.pack('<I',v);get=lambda a:struct.unpack('<I',k.read(a,4))[0]
# MSVC SEH uses fs:[0]; map the TEB page because the harness does not execute Windows.
u.mem_map(0,0x1000);u.mem_write(0,pack(0xffffffff))
clock=emu.RET+0x300;stub=emu.RET+0x200;u.mem_write(clock,pack(100));u.mem_write(stub,b'\xa1'+pack(clock)+b'\x31\xd2\xc3');u.mem_write(0x514fdc,pack(stub))
def alloc(uc,addr,size,user):
 sp=uc.reg_read(UC_X86_REG_ESP);n=get(sp+4);p=k.alloc(bytes(n));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
u.hook_add(UC_HOOK_CODE,alloc,None,0x4cf50d,0x4cf50d)
thread=k.alloc(bytes(0x100));stack=k.alloc(bytes(64));u.mem_write(thread+0x10,pack(16));u.mem_write(thread+0x18,pack(stack));u.mem_write(thread+0x78,pack(3))
for i,v in enumerate((5,0,0)):u.mem_write(stack+4*i,pack(v))
u.mem_write(0x52ebb4,pack(1))
k.call(0x497eb0,0,0,thread);obj=get(thread+0x5c)
assert obj and get(thread+0x74)&1 and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==2
assert get(obj)==0x514fd0 and get(obj+8)==105
for now,expected in ((104,0),(105,1)):
 u.mem_write(clock,pack(now));k.call(0x445620,obj,0)
 assert u.reg_read(UC_X86_REG_EAX)==expected
 assert get(thread+0x78)==expected
 if expected:assert get(stack)==0
 print('clock',now,'update',expected,'stack-index',get(thread+0x78))
# With input enabled, replace the input query by a known active logical bit.
u.mem_write(thread+0x78,pack(0));u.mem_write(clock,pack(104));u.mem_write(obj+0x20,pack(1));u.mem_write(obj+0x24,pack(0))
input_value=k.alloc(pack(1));u.mem_write(0x47c020,b'\xa1'+pack(input_value)+b'\xc3')
k.call(0x445620,obj,0)
assert u.reg_read(UC_X86_REG_EAX)==1 and get(thread+0x78)==1 and get(stack)==1
print('input bit 1 before deadline: update 1, stack result 1')
u.mem_write(0x52ebb0,pack(0))
for bits,expected in ((0,0),(1,1),(2,0),(0x80,1),(0x100,1),(0x80000000,1),(0x40000000,0)):
 u.mem_write(thread+0x78,pack(0));u.mem_write(input_value,pack(bits));u.mem_write(obj+0x28,pack(0))
 k.call(0x445620,obj,0)
 assert u.reg_read(UC_X86_REG_EAX)==expected and get(thread+0x78)==expected,(bits,expected)
 if expected:assert get(stack)==1
 print(f'input bits 0x{bits:08x}: update {expected}')
u.mem_write(0x52ebb0,pack(2));u.mem_write(input_value,pack(2));u.mem_write(thread+0x78,pack(0));k.call(0x445620,obj,0)
assert u.reg_read(UC_X86_REG_EAX)==1 and get(thread+0x78)==1 and get(stack)==1
print('additional global mask bit 2 enables input bit 2')
# Explicit control signal sets +0x28 and completes with result 0 before deadline.
u.mem_write(input_value,pack(0));u.mem_write(thread+0x78,pack(0));u.mem_write(obj+0x28,pack(1));k.call(0x445620,obj,0)
assert u.reg_read(UC_X86_REG_EAX)==1 and get(thread+0x78)==1 and get(stack)==0
print('control-completion flag before deadline: update 1, stack result 0')
print('PASS: 80:5C installs wait object; timer/input behavior verified with allocator, clock and input query stubbed')
# 80:5A always returns 2; it installs CProcWaitTiming only for a future deadline.
u.mem_write(0x4a9f60,b'\xa1'+pack(clock)+b'\x31\xd2\xc3')
for deadline,expected in ((100,0),(105,1)):
 thread2=k.alloc(bytes(0x100));stack2=k.alloc(bytes(64))
 for off,val in ((0x10,16),(0x18,stack2),(0x88,deadline)):u.mem_write(thread2+off,pack(val))
 u.mem_write(clock,pack(100));k.call(0x497e20,0,0,thread2)
 assert u.reg_read(UC_X86_REG_EAX)==2 and get(thread2+0x78)==1 and get(stack2)==expected
 assert bool(get(thread2+0x5c))==bool(expected) and bool(get(thread2+0x74)&1)==bool(expected)
 print('80:5A deadline',deadline,'pushed',expected,'object installed',bool(expected),'return 2')
print('PASS: 80:5A conditional wait with stubbed clock and allocator')
