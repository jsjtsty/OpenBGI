# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_system_queue.py
# Native 80:A0: empty/nonempty global event queue; locks and free stubbed.
import struct
import emu,winstub
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda x:struct.pack('<I',x&0xffffffff)
for present in (False,True):
 k=emu.Kernel();u=k.u;win=winstub.Win(k);get=lambda p:struct.unpack('<I',k.read(p,4))[0]
 win.api('EnterCriticalSection',1)(lambda a:0);win.api('LeaveCriticalSection',1)(lambda a:0)
 def free(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 u.hook_add(UC_HOOK_CODE,free,None,0x4cf53d,0x4cf53d)
 local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));u.mem_write(stack,pack(0x08000020))
 for off,val in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,1)):u.mem_write(thread+off,pack(val))
 if present:
  node=k.alloc(pack(0x11111111)+pack(0x22222222)+pack(0x33333333)+pack(0));u.mem_write(0x59262c,pack(node))
 k.call(0x498d20,0,0,thread)
 assert get(thread+0x78)==1 and get(stack)==int(present) and u.reg_read(UC_X86_REG_EAX)==0
 if present:
  assert k.read(local+0x20,12)==pack(0x11111111)+pack(0x22222222)+pack(0x33333333)
  assert get(0x59262c)==0
 print('80:A0 queue present=',present,'pushed=',get(stack),'returned 0')
print('PASS: native queue handler; locks and free stubbed')
