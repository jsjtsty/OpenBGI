# Python 3.10: py -3 analysis/scripts/audit_bp_functions.py <bp-dir> <linear.tsv> <candidate-all.tsv> <output.tsv>
# Static proposed boundaries from executable prefix, direct calls, and table/store initializer candidates.
# BP functions can be hidden by indirect calls, so this cannot prove a complete function list.
import csv,pathlib,struct,sys,collections
from bgi_dis import decode,DecodeError
root,report,candidates,out=map(pathlib.Path,sys.argv[1:]);seeds=collections.defaultdict(set)
with candidates.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  seeds[r['program'].replace('\\','/')].update(int(x,16) for x in r['candidate_targets'].split(','))
rows=[]; totals=collections.Counter()
with report.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  key=r['program'];b=(root/key).read_bytes();base,n=struct.unpack_from('<II',b);d=b[base:base+n];end=int(r['last_ret']);pc=0;ins={};last=None;prevmap={}; direct=set()
  while pc<end:
   p=pc;pc,op,_,_=decode(d,pc,True);ins[p]=(op,pc);prevmap[p]=last;last=(p,op)
   if op==0x16 and prevmap[p] is not None:
    q,prior=prevmap[p]
    if prior==6 and q+3==p:
     t=q+struct.unpack_from('<h',d,q+1)[0]
     if 0<=t<len(d) and d[t]==0x12:direct.add(t)
  proposed={0}|seeds[key]|direct
  aligned={x for x in proposed if x in ins and ins[x][0]==0x12};unaligned=proposed-aligned
  totals['candidates']+=len(proposed);totals['unaligned']+=len(unaligned)
  for i,start in enumerate(sorted(aligned)):
   nxt=sorted(aligned)[i+1] if i+1<len(aligned) else end
   prior=prevmap.get(nxt) if nxt!=end else last
   ok=bool(prior and prior[1]==0x17)
   rows.append((key,f'{start:X}',f'{nxt:X}',f'{prior[0]:X}' if prior else '',f'{prior[1]:02X}' if prior else '', 'RET' if ok else 'UNRESOLVED'))
   totals['boundaries']+=1;totals['ret']+=ok
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','candidate_start','next_candidate','preceding_instruction','preceding_opcode','status'));w.writerows(rows)
print(dict(totals))
if totals['unaligned'] or totals['ret'] != totals['boundaries']:
 raise SystemExit(1)
