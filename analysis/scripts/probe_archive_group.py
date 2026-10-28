# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_archive_group.py
# Record research VM 80:38 args; simulation paths and inputs remain unverified.
import os
import vm_mt
from vm_proto import ROOT
os.chdir(ROOT)
class Probe(vm_mt.MT):
 def subop(self,key):
  if key=='80:38':
   arr=self.expr[(self.ep-1)%len(self.expr)];nameptr=self.expr[(self.ep-2)%len(self.expr)]
   try:
    names=[]
    for i in range(128):
     p=self.read(arr+i*4,4)
     if not p:break
     names.append(self.string(p))
    else:raise ValueError('unterminated archive group')
    print('80:38 simulated args: group=',repr(self.string(nameptr)),'members=',names)
   except Exception as e:print('group decode error:',e)
   raise NotImplementedError('stopped after archive registration probe')
  return super().subop(key)
p=Probe();print('state',p.run(200),'frame',p.frame,'last',p.trace[-1])
