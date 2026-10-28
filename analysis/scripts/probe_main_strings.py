# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_main_strings.py
# Original 66/6C/6D on synthetic VM-local strings; code-page internals are native.
import struct
import emu,winstub
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
pack=lambda x:struct.pack('<I',x&0xffffffff)
def setup(data):
 k=emu.Kernel();u=k.u;local=k.alloc(bytes(512));stack=k.alloc(bytes(128));thread=k.alloc(bytes(0x100));get=lambda a:struct.unpack('<I',k.read(a,4))[0]
 u.mem_write(local,data)
 for off,val in ((0x10,32),(0x18,stack),(0x4c,local)):u.mem_write(thread+off,pack(val))
 def alloc(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);p=k.alloc(bytes(get(sp+4)));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 def free(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EIP,get(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 for addr in (0x4cf50d,0x4cf54b):u.hook_add(UC_HOOK_CODE,alloc,None,addr,addr)
 for addr in (0x4cf508,0x4cf53d,0x4e0964):u.hook_add(UC_HOOK_CODE,free,None,addr,addr)
 return k,u,local,stack,thread,get
for data in (b'A\x82\xa0Z\0','AあZ'.encode('utf8')+b'\0'):
 k,u,local,stack,thread,get=setup(data);u.mem_write(stack,pack(0x08000000));u.mem_write(thread+0x78,pack(1));k.call(0x484300,0,0,thread)
 assert k.read(local,len(data))==data.replace(b'A',b'a').replace(b'Z',b'z') and get(thread+0x78)==0 and u.reg_read(UC_X86_REG_EAX)==0
 print('6D lower:',data.hex(),'->',k.read(local,len(data)).hex())
for data in (b'\x82\xa0A\0','あA'.encode('utf8')+b'\0'):
 k,u,local,stack,thread,get=setup(data);u.mem_write(stack,pack(0x08000000));u.mem_write(thread+0x78,pack(1))
 win=winstub.Win(k)
 def convert(a):
  cp,flags,src,n,dst,capacity=a
  if n==0xffffffff:
   n=0
   while n<1024 and k.read(src+n,1)!=b'\0':n+=1
   n+=1
  text=k.read(src,n).decode('cp932');raw=text.encode('utf-16le')
  if len(raw)//2>capacity:return 0
  if dst:u.mem_write(dst,raw)
  return len(raw)//2
 win.api('MultiByteToWideChar',6)(convert)
 try:
  k.call(0x484280,0,0,thread);result=[get(stack+i*4) for i in range(get(thread+0x78))]
  assert result==([2,0x82a0,1,0] if data[0]==0x82 else [3,0x3042,1,0]) and u.reg_read(UC_X86_REG_EAX)==0
  print('6C input',data.hex(),'stack',result,'return',u.reg_read(UC_X86_REG_EAX))
 except UcError as e:print('6C unavailable with current host stubs:',type(e).__name__,str(e),'ip',hex(u.reg_read(UC_X86_REG_EIP)),'last import',win.log[-1:] )
for haystack in (b'A\x82\xa0B\0','AあB'.encode('utf8')+b'\0'):
 k,u,local,stack,thread,get=setup(haystack);u.mem_write(local+0x80,b'B\0');u.mem_write(stack,pack(0x08000000)+pack(0x08000080));u.mem_write(thread+0x78,pack(2))
 try:
  k.call(0x4840a0,0,0,thread);assert get(stack)==(3 if haystack[1]==0x82 else 4) and get(thread+0x78)==1
  print('66 input',haystack.hex(),'byte offset',get(stack),'stack size',get(thread+0x78))
 except UcError as e:print('66 unavailable with current host stubs:',type(e).__name__)
