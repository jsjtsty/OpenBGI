# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_gdb_buffer.py
# Original 80:82/83 handlers against a synthetic 1 MiB GDB buffer.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
gdb=k.alloc(bytes(0x100000));local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
u.mem_write(0x5482bc,pack(gdb))
for off,val in ((0x10,16),(0x18,stack),(0x4c,local)):u.mem_write(thread+off,pack(val))
for offset,data in ((0,b'ABCD'),(0xffffc,b'XYZ!')):
 u.mem_write(local+0x20,data);u.mem_write(stack,pack(offset)+pack(0x08000020)+pack(len(data)));u.mem_write(thread+0x78,pack(3))
 k.call(0x4984c0,0,0,thread)
 assert k.read(gdb+offset,len(data))==data and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 u.mem_write(local+0x40,bytes(len(data)));u.mem_write(stack,pack(0x08000040)+pack(offset)+pack(len(data)));u.mem_write(thread+0x78,pack(3))
 k.call(0x498580,0,0,thread)
 assert k.read(local+0x40,len(data))==data and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print(f'80:82/83 offset=0x{offset:X} length={len(data)} PASS: stack -3, EAX=0')
print('PASS: first and last 4 bytes, valid paths; error paths not executed')
# Original 80:80 maps GDB loader status and pushes its two output words.
# Reset Unicorn per case because translated code may otherwise remain cached.
for status,expected in ((0,0),(0x80000001,1),(0x80000002,2),(0x1234,0x1234)):
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,0)):u.mem_write(thread+off,pack(val))
 # Loader gets output pointer in ECX; return a deterministic status.
 stub=b'\xc7\x01'+pack(640)+b'\xc7\x41\x04'+pack(480)+b'\xb8'+pack(status)+b'\xc3'
 u.mem_write(0x479260,stub);k.call(0x498440,0,0,thread)
 assert get(thread+0x78)==3 and [get(stack+i*4) for i in range(3)]==[640,480,expected]
 assert u.reg_read(UC_X86_REG_EAX)==0
 print(f'80:80 loader status=0x{status:08X} -> stack [640,480,{expected}]')
print('PASS: 80:80 wrapper mapping with GDB loader stubbed; actual file loading not tested')
# Missing-file path through native loader; path construction and file lookup stubbed.
for fill in (0x55,0xaa):
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,0)):u.mem_write(thread+off,pack(val))
 u.mem_write(emu.STACK+0x7e000,bytes([fill])*0x2000)
 u.mem_write(0x478ff0,b'\xc3');u.mem_write(0x473d30,b'\x31\xc0\xc3')
 k.call(0x498440,0,0,thread);actual=[get(stack+i*4) for i in range(3)]
 assert actual==[int.from_bytes(bytes([fill])*4,'little')]*2+[1] and u.reg_read(UC_X86_REG_EAX)==0,actual
 print(f'80:80 missing file, stack prefill={fill:02X}: pushed {[hex(x) for x in actual]}')
print('PASS: missing-file path leaves first two loader output words uninitialized for these samples')
# Simulate a file whose SDC decoder fails length validation; native loader/handler.
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
for fill in (0x55,0xaa):
 k=emu.Kernel();u=k.u;get=lambda p:struct.unpack('<I',k.read(p,4))[0]
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 for off,val in ((0x10,16),(0x18,stack),(0x78,0)):u.mem_write(thread+off,pack(val))
 u.mem_write(emu.STACK+0x7e000,bytes([fill])*0x2000)
 u.mem_write(0x478ff0,b'\xc3');u.mem_write(0x473d30,b'\xb8'+pack(32)+b'\xc3')
 # *ECX points to source buffer; write the decompressed-size field.
 u.mem_write(0x473500,b'\x8b\x01\xc7\x40\x18'+pack(256)+b'\xb8'+pack(1)+b'\xc3')
 u.mem_write(0x4a2360,b'\x31\xc0\xc3');u.mem_write(0x4cf508,b'\xc3')
 def alloc(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);result=k.alloc(bytes(get(sp+4)));uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 u.hook_add(UC_HOOK_CODE,alloc,None,0x4cf54b,0x4cf54b)
 k.call(0x498440,0,0,thread);actual=[get(stack+i*4) for i in range(3)]
 assert actual==[int.from_bytes(bytes([fill])*4,'little')]*2+[2] and u.reg_read(UC_X86_REG_EAX)==0,actual
 print(f'80:80 failed SDC, stack prefill={fill:02X}: pushed {[hex(x) for x in actual]}')
print('PASS: one decoder-error path leaves first two output words uninitialized')
