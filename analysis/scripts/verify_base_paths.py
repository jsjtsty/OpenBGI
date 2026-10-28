# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_base_paths.py
# Native 80:3D path selection with synthetic base directories.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
for alt in (b'',b'D:\\ALT\\'):
 for selector in (0,1,2):
  k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
  local=k.alloc(bytes(512));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
  for off,val in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,2)):u.mem_write(thread+off,pack(val))
  u.mem_write(0x547db8,b'C:\\GAME\\\0');u.mem_write(0x5470a0,alt+b'\0')
  u.mem_write(stack,pack(0x08000040)+pack(selector));k.call(0x4977e0,0,0,thread)
  expected=b'C:\\GAME\\' if selector==0 else alt if selector==1 else b'';success=int(selector==0 or selector==1 and bool(alt))
  assert get(thread+0x78)==1 and get(stack)==success and u.reg_read(UC_X86_REG_EAX)==0
  if success:assert k.read(local+0x40,len(expected)+1)==expected+b'\0'
  print('80:3D selector',selector,'alt',bool(alt),'success',success,'return 0')
print('PASS: native path selection; base paths synthetic')
