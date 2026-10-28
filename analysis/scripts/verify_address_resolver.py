# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_address_resolver.py
# Test native FUN_0049d0a0 with synthetic thread, global memory and page table.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
k=emu.Kernel();u=k.u
thread=k.alloc(bytes(0x100));globalbase=k.alloc(bytes(0x1000));codebase=k.alloc(bytes(0x1000));localbase=k.alloc(bytes(0x1000));heapbase=k.alloc(bytes(0x1000));pool=k.alloc(bytes(32));page=k.alloc(bytes(0x1000));table=k.alloc(bytes(64))
u.mem_write(pool+4,pack(heapbase));u.mem_write(thread,pack(0x515060));u.mem_write(thread+0x2c,pack(codebase));u.mem_write(thread+0x4c,pack(localbase));u.mem_write(thread+0x50,pack(pool));u.mem_write(0x5485e0,pack(globalbase))
# Descriptor 0: high tags [4,5), page bits=12, offset mask=0xfff.
u.mem_write(0x504c20,pack(12));u.mem_write(0x504c40,pack(4));u.mem_write(0x504c44,pack(5));u.mem_write(0x504d20,pack(0xfff));u.mem_write(0x504d40,pack(table));u.mem_write(table,pack(page))
cases=((0,0),(0x123,globalbase+0x123),(0x04000123,codebase+0x123),(0x08000123,localbase+0x123),(0x0c000123,heapbase+0x123),(0x10000234,page+0x234))
for encoded,expected in cases:
 k.call(0x49d0a0,encoded,thread);actual=u.reg_read(UC_X86_REG_EAX)
 assert actual==expected,(hex(encoded),hex(actual),hex(expected))
 print(f'address {encoded:08X} -> {actual:08X} PASS')
print('PASS: null, tags 0..3 and one mapped tag 4; native CThread vtable, synthetic memory pool and page table')
