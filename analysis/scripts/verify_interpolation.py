# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_interpolation.py
# Differential test 46/47 against original 00483550/00483630, MXCSR=0x1f80.
import random,struct
import numpy as np
import emu
from unicorn.x86_const import UC_X86_REG_EAX
pack=lambda n:struct.pack('<I',n&0xffffffff)
f=np.float32
def snap(v):
 if not np.isfinite(v):q=0x80000000
 else:
  n=int(np.rint(v));q=(n&0xffffffff) if -0x80000000<=n<=0x7fffffff else 0x80000000
 base=q&0xffff0000;fraction=(q-base)&0xffffffff
 if fraction<64:return base
 if fraction>0xffc0:return (base+0x10000)&0xffffffff
 return q
def ref46(a,b,n,d):
 t=f(0) if d==0 else f(f(n)/f(d));t=f(min(1,max(0,t)))
 return snap(f(f(f(f(b)-f(a))*t+f(a))*f(65536)))
def ref47(a,b,t):
 x=f(f(a)*f(1/65536));y=f(f(b)*f(1/65536));ratio=f(f(t)*f(1/65536));ratio=f(min(1,max(0,ratio)))
 return snap(f(f(f(y-x)*ratio+x)*f(65536)))
r=random.Random(0x414243);k=emu.Kernel();u=k.u;stack=k.alloc(bytes(128));thread=k.alloc(bytes(0x100));get=lambda p:struct.unpack('<I',k.read(p,4))[0]
for op,va,ref in ((0x46,0x483550,ref46),(0x47,0x483630,ref47)):
 for i in range(3000):
  if op==0x46:args=(r.randint(-40000,40000),r.randint(-40000,40000),r.randint(-100000,100000),r.choice([0,1,-1,r.randint(-100000,100000)]))
  else:args=(r.randint(-0x7fffffff,0x7fffffff),r.randint(-0x7fffffff,0x7fffffff),r.randint(-100000,100000))
  u.mem_write(stack,b''.join(pack(x) for x in args))
  for off,val in ((0x10,32),(0x18,stack),(0x78,len(args))):u.mem_write(thread+off,pack(val))
  k.call(va,0,0,thread);actual=get(stack);expected=ref(*args)
  if get(thread+0x78)!=1 or u.reg_read(UC_X86_REG_EAX)!=0 or actual!=expected:
   raise AssertionError((hex(op),args,hex(actual),hex(expected)))
 print(f'{op:02X}: 3000/3000 cases match native with MXCSR 0x1f80')
