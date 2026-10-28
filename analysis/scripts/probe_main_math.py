# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_main_math.py
# Sample native handlers 0x42/0x44 with default emu.py MXCSR/x87 configuration.
import csv,struct,pathlib
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda x:struct.pack('<I',x&0xffffffff)
root=pathlib.Path(__file__).resolve().parents[1];out=[]
for op,va,values in [('42',0x483250,[(3,7,2),(-3,7,2),(3,-7,2),(0x7fffffff,2,3),(1,1,0)]),('43',0x4832a0,[(0,0),(1,0),(0,1),(-1,0),(0,-1),(1,1),(-1,1),(-1,-1),(1,-1)]),('44',0x4832d0,[(3,4,12),(2,2,0),(0,0,0),(-3,4,0)]),('46',0x483550,[(0,100,5,10),(0,100,10,10),(-100,100,5,10),(10,20,1,0)]),('47',0x483630,[(0,65536,32768),(0,65536,65536),(-65536,65536,32768)]),('48',0x483700,[(0,),(90*65536,),(180*65536,),(270*65536,)]),('49',0x483750,[(0,),(90*65536,),(180*65536,),(270*65536,)])]:
 for args in values:
  k=emu.Kernel();u=k.u;stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));get=lambda p:struct.unpack('<I',k.read(p,4))[0]
  for off,val in ((0x10,16),(0x18,stack),(0x78,len(args))):u.mem_write(thread+off,pack(val))
  for i,value in enumerate(args):u.mem_write(stack+4*i,pack(value))
  try:
   k.call(va,0,0,thread);count=get(thread+0x78);result=get(stack) if count==1 else None;ret=u.reg_read(UC_X86_REG_EAX);status='returned'
  except Exception as exc:count=get(thread+0x78);result='';ret='';status=type(exc).__name__
  out.append((op,','.join(map(str,args)),status,count,result,ret))
  print(out[-1])
with (root/'out/main_math_vectors.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('opcode','args_push_order','status','stack_slots_after','result_u32','handler_eax'));w.writerows(out)
