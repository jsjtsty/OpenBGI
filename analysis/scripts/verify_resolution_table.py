# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_resolution_table.py
# Native 81:60 three result paths and startup arguments.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
for index,second,third,expected in ((13,1280,960,0),(16,1280,960,1),(13,0,960,2),(13,1280,0,2)):
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0];stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,3)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(index)+pack(second)+pack(third));k.call(0x49adb0,0,0,thread)
 assert get(thread+0x78)==1 and get(stack)==expected and u.reg_read(UC_X86_REG_EAX)==0
 if expected==0:assert get(0x52f288+4*index)==third and get(0x52f2c8+4*index)==second
 print('81:60',index,second,third,'->',expected)
print('PASS: native handler and table effects, no host stubs')
