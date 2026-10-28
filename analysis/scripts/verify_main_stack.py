# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_main_stack.py
# Execute native main opcode handlers 0B/10/11; validation virtual method is stubbed.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
k=emu.Kernel();u=k.u;get=lambda x:struct.unpack('<I',k.read(x,4))[0]
thread=k.alloc(bytes(0x100));stack=k.alloc(bytes(64));local=k.alloc(bytes(256));code=k.alloc(b'\x03ABC'+bytes(32))
validator=k.alloc(b'\xb8\x01\x00\x00\x00\xc2\x04\x00');table=k.alloc(pack(0)+pack(validator))
for off,val in ((0,table),(0x10,16),(0x18,stack),(0x2c,code),(0x40,0),(0x44,256),(0x4c,local),(0x78,1),(0x80,0),(0x84,19)):
 u.mem_write(thread+off,pack(val))
# opcode 0B: address comes from stack; byte length and data come from code.
u.mem_write(stack,pack(0x08000020));k.call(0x4823e0,0,0,thread)
assert k.read(local+0x20,3)==b'ABC' and get(thread+0x78)==0 and get(thread+0x80)==4 and u.reg_read(UC_X86_REG_EAX)==0
print('0B PASS: copies inline bytes, advances IP, pops one, returns 0')
# opcode 10 pushes the raw byte offset, without an address tag.
u.mem_write(thread+0x78,pack(0));k.call(0x482540,0,0,thread)
assert get(stack)==19 and get(thread+0x78)==1 and u.reg_read(UC_X86_REG_EAX)==0
print('10 PASS: pushes raw SP=19')
# opcode 11 sets, rather than increments, SP; boundary value is accepted.
for value in (0,24,256):
 u.mem_write(thread+0x78,pack(1));u.mem_write(stack,pack(value));u.mem_write(thread+0x84,pack(19));k.call(0x482560,0,0,thread)
 assert get(thread+0x84)==value and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print('11 PASS: set SP',value)
print('invalid value > limit: assembly branches to script error; not executed with real GUI/error environment')
