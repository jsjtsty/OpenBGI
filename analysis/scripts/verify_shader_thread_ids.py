# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_shader_thread_ids.py
# Native thread constructors and 81:6F; shader creation is stubbed in modes 1/4.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX

pack = lambda n: struct.pack('<I', n & 0xffffffff)
get = lambda k, a: struct.unpack('<I', k.read(a, 4))[0]

k = emu.Kernel()
ids = []
for _ in range(3):
    obj = k.alloc(bytes(0x200))
    k.call(0x44c880, obj, 0, 0, 0, 0, 0, 0, 0)
    ids.append(get(k, obj + 8))
assert ids == [0, 1, 2], ids
print('constructor IDs:', ids)

for mode, backend, expected in [(0,None,1),(2,None,1),(3,None,1),
                                (1,0,1),(1,15,0),(4,0,1),(4,15,0),(5,None,0)]:
    k = emu.Kernel(); u = k.u
    if backend is not None:
        u.mem_write(0x46d150, b'\xb8' + pack(backend) + b'\xc3')
    u.mem_write(0x546100, pack(99))
    stack = k.alloc(pack(mode) + bytes(60)); thread = k.alloc(bytes(0x100))
    for offset, value in ((0x10,16),(0x18,stack),(0x78,1)):
        u.mem_write(thread + offset, pack(value))
    k.call(0x49af60, 0, 0, thread)
    assert get(k, thread + 0x78) == 1 and get(k, stack) == expected
    assert get(k, 0x546100) == (mode if expected else 99)
    assert u.reg_read(UC_X86_REG_EAX) == 0
    print('81:6F', mode, 'backend', backend, '=>', expected)
print('PASS')
