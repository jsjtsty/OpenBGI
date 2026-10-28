# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_config_effects.py
# Native startup config handlers; clock is stubbed only for 80:58.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
cases=[('80:58',0x497dd0,[12],0x88,112,'thread'),('80:67',0x4980f0,[4],0x548138,4,'global'),('C0:1F',0x485450,[2],0x58f350,2,'global'),('90:95',0x48d3e0,[4,31],0x52ebb8,4,'global'),('90:96',0x48d410,[4,31],0x52ebe0,4,'global')]
for name,va,args,address,expected,kind in cases:
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0];stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,len(args))):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,b''.join(map(pack,args)))
 if name=='80:58':u.mem_write(0x4a9f60,b'\xb8'+pack(100)+b'\x31\xd2\xc3')
 k.call(va,0,0,thread);target=thread+address if kind=='thread' else address
 assert get(target)==expected and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 if name=='90:95':assert get(0x52ebbc)==31
 if name=='90:96':assert get(0x52ebd4)==31
 print(name,'pop',len(args),'push 0 return 0','verified field',hex(target))
print('PASS: valid config paths; 80:58 clock stubbed, invalid paths not executed')
