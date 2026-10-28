# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_subop_effects.py --tier A1 [--limit N]
# Runs native handlers in ac2_ML.exe under Unicorn; hooks 0044d2a0/2c0 and
# 0044d180/1a0/1c0/1e0. Failed paths are recorded, never counted as returns.
# Synthetic memory, allocator and absent host APIs do not establish all paths.
import argparse,csv,pathlib,struct
from collections import Counter
import emu
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_EIP
ROOT=pathlib.Path(__file__).resolve().parents[1]
pack=lambda x:struct.pack('<I',x&0xffffffff)
def rows(p):
 with p.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
def trial(va,pattern):
 k=emu.Kernel();u=k.u;thread=k.alloc(bytes(0x100));stack=k.alloc(bytes(0x400));local=k.alloc(bytes(0x10000));program=k.alloc(bytes(0x10000))
 # The expression stack has a 256-slot ring buffer. Start at a safe depth so
 # both underflowing and pushing handlers remain inside the synthetic buffer.
 for off,v in ((0x10,256),(0x18,stack),(0x1c,0x10000),(0x24,0x10000),(0x2c,program),(0x3c,0x10000),(0x44,0x10000),(0x4c,local),(0x78,128)):
  u.mem_write(thread+off,pack(v))
 patterns=(
  [0]*256,
  [i for i in range(256)],
  [v for i in range(64) for v in (0,1,2,3)],
  [v for i in range(42) for v in (0xffffffff,0,1,0x100,0x7fffffff,0x08000000)]+[0]*4,
 )
 values=patterns[pattern]
 u.mem_write(stack,b''.join(pack(v) for v in values))
 # Isolate stack effects from process-wide locks and the CRT RNG/TLS state.
 # Their behavior is already covered by verify_random_effects.py.
 for addr in (0x472080,0x4720a0,0x4dcb8f):u.mem_write(addr,b'\xc3')
 u.mem_write(0x4dcb6e,b'\xb8'+pack(0x1234)+b'\xc3')
 counters=Counter();error=[];ip_start=0
 def hook(uc,addr,size,user):
  if addr==0x44d2a0:counters['pop']+=1
  elif addr==0x44d2c0:counters['push']+=1
  elif addr in (0x44d180,0x44d1a0,0x44d1c0,0x44d1e0):counters['fetch']+=1
  elif addr==0x471e50:error.append('script_error');uc.emu_stop()
 for addr in (0x44d2a0,0x44d2c0,0x44d180,0x44d1a0,0x44d1c0,0x44d1e0,0x471e50):u.hook_add(UC_HOOK_CODE,hook,None,addr,addr)
 sp=emu.STACK+0x80000-8;u.mem_write(sp,pack(emu.RET)+pack(thread))
 ip_start=struct.unpack('<I',u.mem_read(thread+0x80,4))[0]
 for reg,val in ((UC_X86_REG_ESP,sp),(UC_X86_REG_EBP,0),(UC_X86_REG_ECX,0),(UC_X86_REG_EDX,0)):u.reg_write(reg,val)
 try:
  u.emu_start(va,emu.RET,timeout=500000,count=200000)
  if u.reg_read(UC_X86_REG_EIP)==emu.RET:status='returned';ret=f'{u.reg_read(UC_X86_REG_EAX):08X}'
  elif error:status=error[0];ret=''
  else:status='limit';ret=''
 except UcError as e:status='unmapped_or_exception';ret='';error.append(str(e))
 try:immediate_bytes=(struct.unpack('<I',u.mem_read(thread+0x80,4))[0]-ip_start)&0xffffffff
 except UcError:immediate_bytes=-1
 return status,counters['pop'],counters['push'],immediate_bytes,ret,hex(u.reg_read(UC_X86_REG_EIP)),';'.join(error)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--tier',choices=['A1','A2','B','C','ALL'],default='A1');ap.add_argument('--limit',type=int,default=0);a=ap.parse_args()
 allrows=rows(ROOT/'out/subop_tiers.tsv');selected=[r for r in allrows if (a.tier=='ALL' or r['tier']==a.tier) and r['handler']]
 if a.limit:selected=selected[:a.limit]
 out=ROOT/f'out/subop_probe_{a.tier}.tsv'
 with out.open('w',encoding='utf8',newline='') as f:
  w=csv.writer(f,delimiter='\t');w.writerow(('subop','tier','handler','pattern','status','pops_observed','pushes_observed','immediate_reads_observed','eax_if_returned','stop_eip','error'))
  for i,r in enumerate(selected):
   for pattern,label in enumerate(('zero','ascending','0_1_2_3','boundary_mix')):
    try:result=trial(int(r['handler'],16),pattern)
    except Exception as e:result=('harness_error',0,0,0,'','',str(e))
    w.writerow((r['subop'],r['tier'],r['handler'],label,*result));f.flush()
   print(f'{i+1}/{len(selected)} {r["subop"]}',flush=True)
 print('wrote',out)
if __name__=='__main__':main()
