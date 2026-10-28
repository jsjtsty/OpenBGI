# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_bitmap_ops.py
# Native image opcode wrappers with image-manager functions stubbed.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
cases=[
 ('90:11',0x488eb0,0x402c90,[7,100,50,2],0,None),
 ('90:12',0x488f90,0x402cf0,[7],1,1),
 ('90:13',0x488fb0,0x402d00,[7,0xff],1,None),
 ('90:18',0x489180,0x402d20,[7,12,34,8,1,128],0,None),
 ('90:1F',0x4899d0,0x403960,[7,8,1,2,30,40],0,None),
]
for name,handler,callee,args,host_result,stack_result in cases:
 k=emu.Kernel();u=k.u;stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 for off,val in ((0x10,16),(0x18,stack),(0x78,len(args))):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,b''.join(map(pack,args)));u.mem_write(callee,b'\xb8'+pack(host_result)+b'\xc3')
 k.call(handler,0,0,thread);after=get(thread+0x78)
 assert after==(stack_result is not None) and (stack_result is None or get(stack)==stack_result) and u.reg_read(UC_X86_REG_EAX)==0
 print(name,'pop',len(args),'push',after,'EAX 0')
print('PASS: native wrappers and validators; image-manager stubbed, failures not exercised')
