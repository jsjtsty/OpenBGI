# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_resource_size.py
# Native 80:35 and lookup wrapper; loose/archive readers are controlled stubs.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX

pack = lambda n: struct.pack('<I', n & 0xffffffff)
get = lambda k, a: struct.unpack('<I', k.read(a, 4))[0]
for archive, loose, arcret, expected in ((0,27,0,27),(0,0,0,0),
                                          (0x08000010,27,42,27),
                                          (0x08000010,0,42,42),
                                          (0x08000010,0,0x80000011,0)):
    k = emu.Kernel(); u = k.u
    local = k.alloc(bytes(256)); stack = k.alloc(bytes(64)); thread = k.alloc(bytes(0x100))
    u.mem_write(local + 0x10, b'data06xxx.arc\0')
    u.mem_write(local + 0x40, b'sc._bsi\0')
    for offset, value in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,2)):
        u.mem_write(thread + offset, pack(value))
    u.mem_write(stack,pack(archive)+pack(0x08000040))
    for addr, result in ((0x473410,loose),(0x4732e0,arcret)):
        u.mem_write(addr,b'\xb8'+pack(result)+b'\xc3')
    u.mem_write(0x4a5030,b'\xc3');u.mem_write(0x4cf508,b'\xc3')
    k.call(0x4975f0,0,0,thread)
    assert get(k,thread+0x78)==1 and get(k,stack)==expected
    assert u.reg_read(UC_X86_REG_EAX)==0
    print('archive=',hex(archive),'loose=',loose,'archive result=',hex(arcret),'->',expected)
print('PASS: native handler/lookup wrapper; file and archive operations stubbed')
