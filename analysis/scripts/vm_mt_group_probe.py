# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/vm_mt_group_probe.py
# Compare an archive-membership approximation to the corpus-only startup stub.
# Only BURIKO ARC20 indexes and 80:38 group names are modeled; loose-file
# overrides, archive priority, errors and other host behavior remain unverified.
import os,pathlib,struct,csv
import vm_mt
import bgi_dis
from vm_proto import ROOT
os.chdir(ROOT);GAME=pathlib.Path(r'E:\Games\Amairo Chocolate 2')
class GroupProbe(vm_mt.MT):
 def __init__(self):super().__init__();self.groups={};self.arcindex={};self.subop_events=[]
 def step(self):
  pc=self.ip;op=self.program[pc]
  if op not in bgi_dis.PREFIX:return super().step()
  key=f'{op:02X}:{self.program[pc+1]:02X}';module=self.modname(pc);thread=self.cur['id'];pos=len(self.trace)
  before=[self.expr[(self.ep-1-i)%len(self.expr)] for i in range(12)]
  try:rc=super().step();status='returned'
  except Exception as e:
   rc='';status=f'{type(e).__name__}: {e}'
   self.subop_events.append((self.frame,thread,module,key,','.join(f'{x:08X}' for x in before),self.ep,rc,status,'SIMULATED; not original execution'))
   raise
  detail=' | '.join(self.trace[pos+1:])[:500]
  self.subop_events.append((self.frame,thread,module,key,','.join(f'{x:08X}' for x in before),self.ep,rc,status,'SIMULATED; '+detail))
  return rc
 def subop(self,key):
  if key=='80:38':
   arr=self.pop();name=self.string(self.pop());members=[]
   for i in range(128):
    p=self.read(arr+4*i,4)
    if not p:break
    members.append(self.string(p))
   else:raise ValueError('unterminated archive group')
   self.groups[name.lower()]=members;self.push(1)
   self.log(f'  80:38 SIMULATED register {name}: {members}; success assumed');return 0
  if key=='80:34':
   member=self.string(self.pop());ptr=self.pop();archive=self.string(ptr) if ptr else ''
   paths=self.groups.get(archive.lower(),[archive] if archive else [])
   exists=(GAME/member).is_file()
   for name in paths:
    path=GAME/name
    if not path.is_file():continue
    if name.lower() not in self.arcindex:
     with path.open('rb') as f:
      h=f.read(16)
      if h[:12]!=b'BURIKO ARC20':raise ValueError(f'unsupported archive {name}')
      n=struct.unpack_from('<I',h,12)[0];table=f.read(n*128)
      if len(table)!=128*n:raise ValueError('truncated archive index')
      self.arcindex[name.lower()]={table[i*128:i*128+96].split(b'\0')[0].decode('cp932').lower() for i in range(n)}
    exists|=member.lower() in self.arcindex[name.lower()]
   self.push(exists)
   self.log(f'  80:34 SIMULATED archive index approximation {archive}/{member}: {int(exists)}; paths={paths}')
   return 0
  return super().subop(key)
vm=GroupProbe();vm.dialog_result=1;result=vm.run(300);out=ROOT/'out/vm_mt_group_probe.txt';out.write_text('\n'.join(vm.trace)+'\n',encoding='utf8')
print('state',result,'frame',vm.frame,'threads',[(t['id'],t['dead']) for t in vm.threads],'trace',out)
print('groups',vm.groups)
print('index hits',sum('80:34 SIMULATED' in x and ': 1;' in x for x in vm.trace),'misses',sum('80:34 SIMULATED' in x and ': 0;' in x for x in vm.trace))
eventfile=ROOT/'out/boot_subop_sequence_candidate.tsv'
with eventfile.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('frame','thread','program_ip','subop','top12_before','stack_index_after','scheduler_return','status','evidence'))
 w.writerows(vm.subop_events)
print('candidate events',len(vm.subop_events),'file',eventfile,'all events are simulation, not native execution')
