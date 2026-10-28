# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/probe_title_resources.py
# Identify title 80:35 resource operands in the explicitly simulated startup path.
import os
import vm_mt
from vm_proto import ROOT
os.chdir(ROOT)
class Probe(vm_mt.MT):
 def subop(self,key):
  if key=='80:35':
   top=self.expr[(self.ep-1)%len(self.expr)];second=self.expr[(self.ep-2)%len(self.expr)]
   def string(addr):
    if addr==0:return None
    try:return self.string(addr)
    except Exception as e:return f'<unreadable {addr:#x}: {e}>'
   print('80:35 at',self.modname(self.ip-2),'member=',repr(string(top)),'archive=',repr(string(second)))
  return super().subop(key)
p=Probe();p.dialog_result=1;print('state',p.run(150),'frame',p.frame)
