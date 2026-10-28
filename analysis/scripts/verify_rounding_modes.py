# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_rounding_modes.py
# Original opcodes 46/47 with exact 64.5 and -64.5 results under four MXCSR modes.
import struct
import emu
from unicorn.x86_const import UC_X86_REG_MXCSR
pack=lambda v:struct.pack('<I',v&0xffffffff)
cases=[('nearest-even',0,64,-64),('down',1,64,-65),('up',2,65,-64),('toward-zero',3,64,-64)]
for name,mode,positive,negative in cases:
 for op,va in ((0x46,0x483550),(0x47,0x483630)):
  for sign,expected in ((1,positive),(-1,negative)):
   k=emu.Kernel();u=k.u;u.reg_write(UC_X86_REG_MXCSR,0x1f80|(mode<<13))
   stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100));get=lambda x:struct.unpack('<I',k.read(x,4))[0]
   args=(0,sign,129,131072) if op==0x46 else (0,sign*129,32768)
   u.mem_write(stack,b''.join(map(pack,args)))
   for off,val in ((0x10,16),(0x18,stack),(0x78,len(args))):u.mem_write(thread+off,pack(val))
   k.call(va,0,0,thread);actual=get(stack);assert actual==(expected&0xffffffff),(name,op,args,actual,expected)
 print(name,'PASS: 46/47 ±64.5 ->',positive,negative)
