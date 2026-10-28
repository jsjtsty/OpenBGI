# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_subop_simple.py
# Calls native handlers in ac2_ML.exe under Unicorn, using a synthetic 8-slot thread.
# FUN_0044d2a0/0044d2c0 use thread +0x10/+0x18/+0x78.
import struct
from unicorn.x86_const import UC_X86_REG_EAX
import emu

cases=[
 ('80:13',0x496cb0,[],0x5486f0,0x12345678,[0x12345678],0),
 ('80:14',0x496cd0,[0x12345678],0x52f5bc,0x12345678,[],0),
 ('80:15',0x496cf0,[0x13579bdf],0x549f04,0x13579bdf,[],0),
 ('80:50',0x497d00,[0x76543210],0x52ebb4,0x76543210,[],1),
 ('90:01',0x488940,[0x13579bdf],0x52ec3c,0x13579bdf,[],0),
 ('90:07',0x488ac0,[0x13579bdf],0x534604,0x13579bdf,[],0),
]
k=emu.Kernel(); pack=lambda v:struct.pack('<I',v); get=lambda a:struct.unpack('<I',k.read(a,4))[0]
for name,va,args,glob,expected,out,ret in cases:
 stack=k.alloc(b'\0'*32); thread=k.alloc(b'\0'*0x100)
 k.u.mem_write(thread+0x10,pack(8));k.u.mem_write(thread+0x18,pack(stack));k.u.mem_write(thread+0x78,pack(len(args)))
 for i,v in enumerate(args):k.u.mem_write(stack+i*4,pack(v))
 k.u.mem_write(glob,pack(expected if name=='80:13' else 0))
 k.call(va,0,0,thread)
 got=[get(stack+i*4) for i in range(get(thread+0x78))]
 assert get(glob)==expected,(name,'global',hex(get(glob)))
 assert got==out,(name,'stack',got)
 assert k.u.reg_read(UC_X86_REG_EAX)==ret,(name,'EAX')
 if name=='90:07': assert get(0x53460c)==0
 print(name,'PASS', 'stack',got,'EAX',ret)

# Graphics-manager calls are stubbed to success: verify original handler stack
# consumption, calling convention and scheduler return, not rendering behavior.
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
for name,va,callee,args,expected in [
 ('90:32',0x48a580,0x46ee40,[73,256],(73,256,[])),
 ('90:33',0x48a5d0,0x46ee50,[73,123,234],(73,123,[234])),
 ('90:38',0x48a760,0x46ef10,[73,2,123,234],(73,2,[123,234])),
 ('90:56',0x48b600,0x46f290,[73,123,234,42,1,128,600],(73,123,[234,42,1,128,600])),
]:
 stack=k.alloc(b'\0'*64);thread=k.alloc(b'\0'*0x100)
 k.u.mem_write(thread+0x10,pack(16));k.u.mem_write(thread+0x18,pack(stack));k.u.mem_write(thread+0x78,pack(len(args)))
 for i,v in enumerate(args):k.u.mem_write(stack+i*4,pack(v))
 seen=[]
 def capture(u,address,size,user):
  sp=u.reg_read(UC_X86_REG_ESP);seen.append((u.reg_read(UC_X86_REG_ECX),u.reg_read(UC_X86_REG_EDX),[get(sp+4+i*4) for i in range(len(expected[2]))]))
 original=k.read(callee,6);k.u.mem_write(callee,b'\xb8'+pack(1 if name in ('90:32','90:33') else 0)+b'\xc3')
 h=k.u.hook_add(UC_HOOK_CODE,capture,begin=callee,end=callee)
 k.call(va,0,0,thread);k.u.hook_del(h);k.u.mem_write(callee,original)
 assert seen==[expected],(name,seen,expected)
 assert get(thread+0x78)==0 and k.u.reg_read(UC_X86_REG_EAX)==0
 print(name,'PASS (manager stubbed)',seen[0])

# Input aggregation: initialize the native state array and logical mapping.
k.u.mem_map(0x10000,0x1000)
k.u.mem_write(0x10000,pack(1)+pack(2)+pack(4)+pack(0))
k.u.mem_write(0x52f498,pack(1)+pack(2)+pack(0))
for key,val in ((1,3),(2,5),(4,7)):k.u.mem_write(0x54870c+key*0x18,pack(val))
for name,va,arg,expected in [('80:12',0x496c70,0x10000,15),('80:1C',0x496e80,1,8)]:
 stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 k.u.mem_write(thread+0x10,pack(16));k.u.mem_write(thread+0x18,pack(stack));k.u.mem_write(thread+0x78,pack(1));k.u.mem_write(stack,pack(arg))
 k.call(va,0,0,thread)
 assert get(thread+0x78)==1 and get(stack)==expected and k.u.reg_read(UC_X86_REG_EAX)==0
 print(name,'PASS input-state sum',expected)
