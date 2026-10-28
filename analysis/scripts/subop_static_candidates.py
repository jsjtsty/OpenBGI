# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/subop_static_candidates.py
# Conservative candidate finder, not a proof: no branches in handler; direct
# textual pop/push counts match four normal Unicorn runs. Helper paths remain unknown.
import csv,pathlib,struct,collections
import capstone
ROOT=pathlib.Path(__file__).resolve().parents[1]
def rows(p):
 with p.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
d=pathlib.Path(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe').read_bytes();pe=struct.unpack_from('<I',d,0x3c)[0];num=struct.unpack_from('<H',d,pe+6)[0];opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(num)]
def offset(va):
 r=va-0x400000
 for vs,v,rs,ra in secs:
  if v<=r<v+min(vs,rs):return ra+r-v
 return None
sizes={int(r['addr'],16):int(r['size']) for r in rows(ROOT/'out/ac2_ML.functions.tsv')};meta={r['opcode']:r for r in rows(ROOT/'out/subop_evidence.tsv')};sample=collections.defaultdict(list)
for tier in ('A1','A2','B','C'):
 for r in rows(ROOT/f'out/subop_probe_{tier}.tsv'):sample[r['subop']].append(r)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);found=[]
for row in rows(ROOT/'out/subop_tiers.tsv'):
 sub=row['subop'];probe=sample[sub]
 if len(probe)!=4 or any(x['status']!='returned' for x in probe):continue
 signatures={(int(x['pops_observed']),int(x['pushes_observed']),x['eax_if_returned']) for x in probe}
 if row['tier']=='C':
  if len({(p,q) for p,q,_ in signatures})!=1:continue
 else:
  if len(signatures)!=1:continue
 va=int(row['handler'],16);size=sizes.get(va);off=offset(va)
 if size is None or off is None:continue
 ins=list(md.disasm(d[off:off+size],va))
 if not ins or any(i.mnemonic.startswith('j') or i.mnemonic.startswith('loop') for i in ins):continue
 x=meta.get(sub)
 if not x:continue
 pops=int(x['direct_pop_refs'])+int(x['addr_pop_refs']);pushes=int(x['direct_push_refs']);obs=next(iter(signatures))
 if (pops,pushes)!=(obs[0],obs[1]):continue
 # A literal EAX immediately before a simple register/stack epilogue proves
 # the normal return register, not exception or nonlocal-control-flow paths.
 constant='';ret_at=next((i for i in range(len(ins)-1,-1,-1) if ins[i].mnemonic.startswith('ret')),None)
 if ret_at is not None:
  for x in reversed(ins[:ret_at]):
   if x.mnemonic=='xor' and x.op_str=='eax, eax':constant='0';break
   if x.mnemonic=='mov' and x.op_str.startswith('eax, 0x'):
    try:constant=str(int(x.op_str.split(', ')[1],16))
    except ValueError:pass
    break
   if (x.mnemonic=='pop' and x.op_str!='eax') or x.mnemonic in ('leave','nop') or (x.mnemonic=='mov' and x.op_str=='esp, ebp') or (x.mnemonic=='add' and x.op_str.startswith('esp, ')):continue
   break
  if constant and int(constant)!=int(obs[2],16):raise AssertionError((sub,constant,obs[2]))
 found.append((sub,row['tier'],row['handler'],pops,pushes,obs[2],constant,','.join(i.op_str for i in ins if i.mnemonic=='call'),'直线处理器与4次样本一致；被调函数的条件路径未穷举'))
out=ROOT/'out/subop_static_candidates.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','tier','handler','candidate_pops','candidate_pushes','observed_eax','static_normal_return','calls','limitation'));w.writerows(found)
print('candidates',len(found),'tiers',dict(collections.Counter(x[1] for x in found)),'output',out)
