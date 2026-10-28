# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_write_watch.py
# Original watched main opcodes 6A/6F; watch callback and 6F formatter are stubbed.
import struct
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda x:struct.pack('<I',x&0xffffffff)
for op,va in (('6A',0x49d710),('6F',0x49d760)):
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));u.mem_write(local+0x20,b'old\0');u.mem_write(local+0x40,b'new\0')
 for off,val in ((0x10,16),(0x18,stack),(0x4c,local),(0x78,2)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(0x08000020)+pack(0x08000040));seen=[]
 def watch(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);seen.append(k.read(local+0x20,4));uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 def formatter(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);u.mem_write(uc.reg_read(UC_X86_REG_ECX),b'new\0');uc.reg_write(UC_X86_REG_EAX,0);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 u.hook_add(UC_HOOK_CODE,watch,None,0x49d310,0x49d310)
 if op=='6F':u.hook_add(UC_HOOK_CODE,formatter,None,0x4a5620,0x4a5620)
 k.call(va,0,0,thread)
 assert seen==[b'old\0' if op=='6A' else b'new\0'],(op,seen)
 assert k.read(local+0x20,4)==b'new\0' and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print(op,'watch saw',seen[0],'final',k.read(local+0x20,4),'PASS')
print('PASS: native watched wrappers; monitor and 6F formatter stubbed')
# Native main opcode 74 toggles the actual global dispatch table.
k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0];stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
for off,val in ((0x10,16),(0x18,stack)):u.mem_write(thread+off,pack(val))
ops=(0x09,0x0a,0x0c,0x60,0x61,0x62,0x6a,0x6f)
normal=(0x482330,0x482390,0x482410,0x483e50,0x483e90,0x483ec0,0x484200,0x484360)
watched=(0x49d470,0x49d4f0,0x49d550,0x49d650,0x49d690,0x49d6d0,0x49d710,0x49d760)
for toggle,expected in ((1,watched),(0,normal)):
 u.mem_write(stack,pack(toggle));u.mem_write(thread+0x78,pack(1));k.call(0x4844b0,0,0,thread)
 assert tuple(get(0x52f660+4*op) for op in ops)==expected and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print('74 toggle',toggle,'replaced 8 pointers PASS')
# Native 75 adds a watch record only for nonzero length.
for length in (0,16):
 k=emu.Kernel();u=k.u;get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 local=k.alloc(bytes(256));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));u.mem_write(local+0x40,b'watch\0')
 for off,val in ((8,42),(0x10,16),(0x18,stack),(0x4c,local),(0x78,3)):u.mem_write(thread+off,pack(val))
 u.mem_write(stack,pack(0x08000020)+pack(length)+pack(0x08000040))
 def allocate(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);result=k.alloc(bytes(get(sp+4)));uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 u.hook_add(UC_HOOK_CODE,allocate,None,0x4cf50d,0x4cf50d)
 k.call(0x4844d0,0,0,thread);head=get(0x54ae00)
 assert get(thread+0x78)==1 and get(stack)==int(length!=0) and bool(head)==bool(length) and u.reg_read(UC_X86_REG_EAX)==0
 if head:assert [get(head+i*4) for i in range(3)]==[42,0x08000020,16] and k.read(head+12,6)==b'watch\0'
 print('75 length',length,'registered',bool(head),'PASS')
