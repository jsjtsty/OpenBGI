# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_message_effect.py
# Execute B0:82 (00488200); replace address resolution and message box only.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x)
for mode in (0,1):
 for flag in (0,1):
  for result in (1,6,7):
   k=emu.Kernel();u=k.u;get=lambda x:struct.unpack('<I',k.read(x,4))[0]
   message=k.alloc(b'test\0');u.mem_write(0x49d0a0,b'\xb8'+pack(message)+b'\xc3')
   u.mem_write(0x4692c0,b'\xb8'+pack(result)+b'\xc3')
   stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
   for off,val in ((0x10,16),(0x18,stack),(0x78,3)):u.mem_write(thread+off,pack(val))
   u.mem_write(stack,pack(0x08000000)+pack(mode)+pack(flag));k.call(0x488200,0,0,thread)
   expected=int(result==(1 if mode==1 else 6))
   assert get(thread+0x78)==1 and get(stack)==expected and u.reg_read(UC_X86_REG_EAX)==0
   print(f'mode={mode} flag={flag} native_dialog_result={result} -> stack={expected}, return 0')
print('PASS: B0:82 pops 3 pushes 1, both modes; address resolution and dialog stubbed')
