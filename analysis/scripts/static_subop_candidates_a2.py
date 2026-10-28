# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/static_subop_candidates_a2.py
# Conservative A2 fallback: handlers with straight-line machine code and no
# normal Unicorn return. Reports direct thread-stack helper calls as static
# effects; transitive behavior of callees remains outside this conclusion.
import csv,pathlib,struct
import capstone
ROOT=pathlib.Path(__file__).resolve().parents[1]
def rows(path):
 with path.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
exe=pathlib.Path(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe').read_bytes()
pe=struct.unpack_from('<I',exe,0x3c)[0];num=struct.unpack_from('<H',exe,pe+6)[0];opt=struct.unpack_from('<H',exe,pe+20)[0]
secs=[struct.unpack_from('<IIII',exe,pe+24+opt+i*40+8) for i in range(num)]
def fileoff(va):
 rva=va-0x400000
 for vs,va0,rs,ra in secs:
  if va0<=rva<va0+min(vs,rs):return ra+rva-va0
 return None
sizes={int(r['addr'],16):int(r['size']) for r in rows(ROOT/'out/ac2_ML.functions.tsv')}
tiers={r['subop']:r for r in rows(ROOT/'out/subop_tiers.tsv')}
evidence={r['opcode']:r for r in rows(ROOT/'out/subop_evidence.tsv')}
effects={r['subop']:r for r in rows(ROOT/'out/subop_effects.tsv')}
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
found=[]
for sub,row in tiers.items():
 if row['tier']!='A2' or not row['handler']:continue
 current=effects[sub]
 if current['pops']!='' and not current['置信度'].startswith('静态推断：A2 直线处理器'):continue
 va=int(row['handler'],16);off=fileoff(va);size=sizes.get(va)
 if off is None or not size:continue
 ins=list(md.disasm(exe[off:off+size],va))
 if not ins or any(i.mnemonic.startswith(('j','loop')) for i in ins):continue
 ev=evidence.get(sub)
 if not ev:continue
 pops=int(ev['direct_pop_refs'])+int(ev['addr_pop_refs'])
 pushes=int(ev['direct_push_refs'])
 # A handler with no direct calls still has a statically fixed 0/0 effect.
 normal='未知'
 found.append((sub,row['handler'],pops,pushes,normal,'0','静态推断：无条件跳转/循环；直接栈辅助函数调用数','被调函数及宿主错误路径未穷举'))
out=ROOT/'out/subop_static_candidates_a2.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','handler','pops','pushes','返回码集合','立即数','依据','限制'));w.writerows(found)
print('straight-line A2 candidates',len(found),'output',out)
