# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_program_effects.py
# Original 80:40/44 wrappers with address, file, allocation and creation stubs.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda n:struct.pack('<I',n&0xffffffff)
for op in ('80:40','80:44'):
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0];stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));name=k.alloc(b'test\0');buf=k.alloc(bytes(0x20000))
 values=[0x08000010,0x08000020] if op=='80:40' else [0x08000010,0x08000020,512,8192,16384]
 for off,val in ((0x10,16),(0x18,stack),(0x78,len(values))):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,b''.join(map(pack,values)));u.mem_write(0x49d0a0,b'\xb8'+pack(name)+b'\xc3')
 seen=[]
 def hook(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP)
  if addr==0x49c120:
   seen.append((uc.reg_read(UC_X86_REG_ECX),uc.reg_read(UC_X86_REG_EDX),get(sp+4),get(sp+8),get(sp+12),get(sp+16)));value=42
  elif addr==0x4cf54b:value=buf
  elif addr==0x473500:value=1
  elif addr==0x44ce40:value=0x1234
  else:value=0
  uc.reg_write(UC_X86_REG_EAX,value);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 for addr in (0x49c120,0x4cf54b,0x473500,0x44ce40,0x4cf508):u.hook_add(UC_HOOK_CODE,hook,None,addr,addr)
 k.call(0x4978f0 if op=='80:40' else 0x497a00,0,0,thread)
 assert get(thread+0x78)==1 and get(stack)==(0x1234 if op=='80:40' else 42) and u.reg_read(UC_X86_REG_EAX)==0
 if op=='80:44':assert seen==[(name,name,512,8192,16384,thread)],seen
 print(op,'pop',len(values),'push 1 return 0; creation/file IO stubbed', 'forwarded='+str(seen) if seen else '')
print('PASS: native wrappers, synthetic host')
