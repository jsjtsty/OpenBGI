# Original 90:54 handler and base CDspObj virtual methods under Unicorn.
# Lookup and redraw notification are replaced by deterministic stubs.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX

k=emu.Kernel();u=k.u
pack=lambda x:struct.pack('<I',x)
get=lambda a:struct.unpack('<I',k.read(a,4))[0]
obj=k.alloc(bytes(0x150));child=k.alloc(bytes(0x150));node=k.alloc(bytes(16))
table=k.alloc(bytes(16));counter=k.alloc(bytes(4));notify=k.alloc(b'\xff\x05'+pack(counter)+b'\xc3')
u.mem_write(table,pack(0)+pack(0x420a10)+pack(0x420a40)+pack(notify))
for address in (obj,child):
 u.mem_write(address,pack(table));u.mem_write(address+4,pack(1));u.mem_write(address+0xbc,pack(1))
u.mem_write(node,pack(child));u.mem_write(obj+0x13c,pack(node))
# __thiscall lookup consumes the ID; always find our constructed object.
u.mem_write(0x446780,b'\xb8'+pack(obj)+b'\xc2\x04\x00')
for old,new,notifications in ((0,1,1),(1,1,0),(1,0,1),(0,0,0)):
 u.mem_write(obj+0x14,pack(old));u.mem_write(child+0x14,pack(99));u.mem_write(counter,pack(0))
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,2)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(73)+pack(new));k.call(0x48b530,0,0,thread)
 assert get(obj+0x14)==new and get(child+0x14)==new
 assert get(counter)==notifications and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print(f'90:54 {old}->{new}: child updated, notify={notifications}, stack empty, return 0')
print('PASS: native handler/base object; lookup and notification stubbed')
# 90:30 uses the same virtual operations via the general display-object table.
u.mem_write(0x44b270,b'\xb8'+pack(obj)+b'\xc2\x04\x00')
for old,new,notifications in ((0,1,1),(1,1,0),(1,0,1),(0,0,0)):
 u.mem_write(obj+0x14,pack(old));u.mem_write(child+0x14,pack(99));u.mem_write(counter,pack(0))
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,2)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(73)+pack(new));k.call(0x48a500,0,0,thread)
 assert get(obj+0x14)==new and get(child+0x14)==new
 assert get(counter)==notifications and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print(f'90:30 {old}->{new}: child updated, notify={notifications}, return 0')
# 90:34 updates +0xB8 only when bit 0 of +0x114 is set, separately for each child.
for parent_flag,child_flag in ((0,1),(1,0),(1,1),(0,0)):
 for address,flag in ((obj,parent_flag),(child,child_flag)):
  u.mem_write(address+0x14,pack(1));u.mem_write(address+0xb8,pack(0));u.mem_write(address+0x114,pack(flag))
 u.mem_write(counter,pack(0));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,2)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(73)+pack(256));k.call(0x48a620,0,0,thread)
 assert get(obj+0xb8)==256*parent_flag and get(child+0xb8)==256*child_flag
 assert get(counter)==1 and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print(f'90:34 parent flag={parent_flag} child flag={child_flag}: independently gated, notify=1')
print('PASS: 90:30/34 native handler/base object; lookup and notification stubbed')
