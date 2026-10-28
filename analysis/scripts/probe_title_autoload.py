# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_title_autoload.py
# Instrument the research VM only; its AUTO-STUB results are not native evidence.
import os,pathlib
import vm_mt
from vm_proto import ROOT
os.chdir(ROOT)
class Probe(vm_mt.MT):
 def __init__(self):
  self.changes=[]
  super().__init__()
 def write(self,addr,val,n):
  if addr>>26==0 and any(addr<=x<addr+n for x in (0x15e,0x15f,0x19f,0x400,0x774)):
   self.changes.append((self.frame,self.cur['id'] if self.cur else '?',self.modname(self.ip),f'{addr:08X}',n,f'{val&0xffffffff:08X}'))
  return super().write(addr,val,n)
vm=Probe();state=vm.run(150);out=ROOT/'out/title_autoload_writes.tsv'
with out.open('w',encoding='utf8') as f:
 f.write('frame\tthread\tnext_ip\taddress\twidth\tvalue\n')
 for row in vm.changes:f.write('\t'.join(map(str,row))+'\n')
print('state',state,'writes',len(vm.changes),'saved',out,'(simulation with unverified stubs)')
for row in vm.changes:print(*row)
