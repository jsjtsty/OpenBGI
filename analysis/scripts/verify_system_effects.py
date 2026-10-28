# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_system_effects.py
# Native 80:0C/0D with deterministic Win32 time and memory-status IAT stubs.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
for total,avail in ((1024,256),(0x90000000,0x80000000)):
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
 local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,0)):u.mem_write(thread+off,pack(val))
 # stdcall GlobalMemoryStatus: write dwTotalPhys and dwAvailPhys, then ret 4.
 status=k.alloc(b'\x8b\x44\x24\x04\xc7\x40\x08'+pack(total)+b'\xc7\x40\x0c'+pack(avail)+b'\xc2\x04\x00')
 u.mem_write(0x4f71a4,pack(status));k.call(0x496b60,0,0,thread)
 assert get(thread+0x78)==2 and get(stack)==min(total,0x7fffffff) and get(stack+4)==min(avail,0x7fffffff) and u.reg_read(UC_X86_REG_EAX)==0
 print('80:0D push total/available',get(stack),get(stack+4),'return 0')
 # GetLocalTime stub writes a sentinel to SYSTEMTIME.
 time=k.alloc(b'\x8b\x44\x24\x04\xc7\x00'+pack(0x11223344)+b'\xc2\x04\x00')
 u.mem_write(0x4f7154,pack(time));u.mem_write(thread+0x78,pack(1));u.mem_write(stack,pack(0x08000020))
 k.call(0x496b40,0,0,thread)
 assert get(thread+0x78)==0 and get(local+0x20)==0x11223344 and u.reg_read(UC_X86_REG_EAX)==0
 print('80:0C pop destination, write through OS stub, return 0')
print('PASS: 80:0C/0D native handlers with Win32 IAT stubs')
