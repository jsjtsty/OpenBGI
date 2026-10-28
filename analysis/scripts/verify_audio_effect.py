# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_audio_effect.py
# Native A0:08 and A0:09 wrappers; audio backend calls stubbed, validators native.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
for name,handler,backend in (('A0:08',0x495cb0,0x4a26e0),('A0:09',0x495cf0,0x4a2710)):
 for channel,value in ((0,0),(15,128)):
  k=emu.Kernel();u=k.u;stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));get=lambda p:struct.unpack('<I',k.read(p,4))[0]
  for off,val in ((0x10,16),(0x18,stack),(0x78,2)):u.mem_write(thread+off,pack(val))
  u.mem_write(stack,pack(channel)+pack(value));u.mem_write(backend,b'\xc3');k.call(handler,0,0,thread)
  assert get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
  print(name,'channel',channel,'value',value,'pop 2 push 0 return 0')
print('PASS: native audio wrappers and validators; backend stubbed')
