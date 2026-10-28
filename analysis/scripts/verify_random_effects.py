# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_random_effects.py
# Original handlers; locks and CRT random functions stubbed.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
get=lambda k,a:struct.unpack('<I',k.read(a,4))[0]
for addr, values, expected in ((0x496910,[42],[]),(0x496940,[],[0x1234]),
                                (0x496970,[0],[0]),(0x496970,[0xffffffff],[0]),
                                (0x496970,[1],[0]),(0x496970,[100],[36])):
 k=emu.Kernel();u=k.u;stack=k.alloc(bytes(64));thread=k.alloc(bytes(256))
 for off,val in ((0x10,16),(0x18,stack),(0x78,len(values))):u.mem_write(thread+off,pack(val))
 for i,v in enumerate(values):u.mem_write(stack+4*i,pack(v))
 for target in (0x472080,0x4720a0,0x4dcb8f):u.mem_write(target,b'\xc3')
 u.mem_write(0x4dcb6e,b'\xb8'+pack(0x1234)+b'\xc3')
 k.call(addr,0,0,thread)
 actual=[get(k,stack+4*i) for i in range(get(k,thread+0x78))]
 assert actual==expected,(hex(addr),values,actual,expected)
 assert u.reg_read(UC_X86_REG_EAX)==0
 print(hex(addr),values,'->',actual)
print('PASS: lock and CRT random calls stubbed')
