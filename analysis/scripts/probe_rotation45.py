# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_rotation45.py
# Execute main opcode 45, x86 handler 00483340, with native trigonometric helpers.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
cases=[((100,20,-30),(0,0,0)),((100,0,0),(0,0,90)),((0,100,0),(90,0,0)),((0,0,100),(0,90,0)),((-1,-2,-3),(0,0,45)),((1,1,1),(0,0,45))]
for vec,angles in cases:
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,5)):u.mem_write(thread+off,pack(val))
 u.mem_write(local+0x20,b''.join(map(pack,vec)))
 values=(0x08000040,0x08000020,*[x*65536 for x in angles])
 u.mem_write(stack,b''.join(map(pack,values)));k.call(0x483340,0,0,thread)
 result=struct.unpack('<3i',k.read(local+0x40,12))
 assert get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print('input',vec,'degrees',angles,'output',result,'return 0')
