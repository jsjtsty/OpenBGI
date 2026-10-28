# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/verify_gdb_restore.py --native-sdc
# Run original 80:80 -> FUN_00479260 with real BGI.gdb bytes. File IO, SDC
# decode (unless --native-sdc), window queries and ancillary lists are stubbed;
# only global/GDB restore is verified. Python SDC output is compared to copies.
import pathlib,struct,sys
import emu,sdc_decode
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
native_sdc='--native-sdc' in sys.argv
source=pathlib.Path(r'E:\Games\Amairo Chocolate 2\BGI.gdb').read_bytes();payload,size,checksum,_=sdc_decode.sdc_decode(source)
assert checksum and size==len(payload) and payload[:16]==b'BURIKO GDB 3.00\0'
pack=lambda x:struct.pack('<I',x&0xffffffff);unpack=lambda b:struct.unpack('<I',b)[0]
globlen=unpack(payload[0x1c:0x20]);gdbpos=0x20+globlen;gdbsize=unpack(payload[gdbpos:gdbpos+4])
for valid_window in (0,1):
 k=emu.Kernel();u=k.u;get=lambda a:unpack(k.read(a,4));globalbuf=k.alloc(bytes(0x10000));gdbbuf=k.alloc(bytes(0x100000));stack=k.alloc(bytes(64));thread=k.alloc(bytes(0x100))
 if native_sdc:
  import winstub
  win=winstub.Win(k)
  win.api('EnterCriticalSection',1)(lambda a:0)
  win.api('LeaveCriticalSection',1)(lambda a:0)
 u.mem_write(0x5485e0,pack(globalbuf));u.mem_write(0x5482bc,pack(gdbbuf))
 for off,val in ((0x10,16),(0x18,stack)):u.mem_write(thread+off,pack(val))
 # Helpers are fastcall or cdecl; caller cleans ordinary stack arguments.
 def hook(uc,addr,ins_size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);ret=get(sp);ecx=uc.reg_read(UC_X86_REG_ECX)
  result=0
  if addr==0x4cf54b:result=k.alloc(bytes(get(sp+4)))
  elif addr==0x473d30:result=len(source)
  elif addr==0x473500:u.mem_write(get(ecx),source);result=1
  elif addr==0x4a2360:u.mem_write(ecx,payload);result=len(payload)
  elif addr==0x47daa0:result=valid_window
  elif addr==0x46e400:u.mem_write(ecx,pack(640)+pack(480))
  uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,ret)
 hooks=[0x478ff0,0x473d30,0x473500,0x4cf54b,0x4a4df0,0x47daa0,0x46e400,0x4a5df0,0x4a5f00,0x450320,0x4506f0,0x4e0964]
 if not native_sdc:hooks.append(0x4a2360)
 for va in hooks:
  u.hook_add(UC_HOOK_CODE,hook,None,va,va)
 try:k.call(0x498440,0,0,thread)
 except Exception:
  print(f'original restore stopped at 0x{u.reg_read(UC_X86_REG_EIP):08X}; SDC native={native_sdc}')
  raise
 assert get(thread+0x78)==3
 actual=[get(stack+i*4) for i in range(3)];expected=[0,0,0] if valid_window else [640,480,0]
 assert actual==expected,(valid_window,actual)
 assert k.read(globalbuf,globlen)==payload[0x20:0x20+globlen]
 assert k.read(gdbbuf,gdbsize)==payload[gdbpos+4:gdbpos+4+gdbsize]
 assert u.reg_read(UC_X86_REG_EAX)==0
 print(f'80:80 window-valid={valid_window}: stack={actual}; global={globlen}, gdb={gdbsize} bytes match')
print('PASS: native GDB restore core; IO, window and ancillary list hooks stubbed; SDC', 'native' if native_sdc else 'stubbed')
