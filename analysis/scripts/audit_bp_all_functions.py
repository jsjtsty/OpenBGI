# Python 3.10: py -3 analysis/scripts/audit_bp_all_functions.py <bp-dir> <linear.tsv> <output.tsv>
# Evidence-based BP segmentation: a linear sweep to provisional final RET, split at RET;
# require each next function to start with opcode 12 and all direct intra-function branches align.
# NULL child entries are decoded structurally but remain runtime errors when executed.
import csv,pathlib,struct,sys,collections
from bgi_dis import decode,PREFIX,SUB
root,report,out=map(pathlib.Path,sys.argv[1:]);rows=[];fail=[];total=collections.Counter()
with report.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  key=r['program'];raw=(root/key).read_bytes();base,n=struct.unpack_from('<II',raw);d=raw[base:base+n];end=int(r['last_ret']);pc=0;start=0;fn=[];branches=[];prev=None;ins=0;null=0
  while pc<end:
   at=pc;pc,op,_,_=decode(d,pc,True);fn.append(at);ins+=1
   if at==start and op!=0x12:fail.append((key,at,'no entry prologue'))
   if op in PREFIX and (op,d[at+1]) not in SUB:null+=1
   if op in (0x15,0x3b) and (op==0x3b or d[at+1]&8):branches.append((at,at+struct.unpack_from('<h',d,at+2)[0]))
   if op==0x14 and prev is not None and d[prev]==6 and prev+3==at:branches.append((at,prev+struct.unpack_from('<h',d,prev+1)[0]))
   if op==0x17:
    targets=set(fn);bad=[(src,t) for src,t in branches if t not in targets];status='PASS' if not bad else 'FAIL'
    if bad:fail.append((key,start,'invalid branches '+repr(bad[:3])))
    rows.append((key,f'{start:X}',f'{pc:X}',ins,len(branches),null,status));total['functions']+=1;total['branches']+=len(branches);total['null_slots']+=null
    start=pc;fn=[];branches=[];ins=0;null=0
   prev=at
  if start!=end or pc!=end:fail.append((key,pc,'trailing partial function'))
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','start','end','instructions','direct_branches','initial_null_subops','status'));w.writerows(rows)
print('programs',len({r[0] for r in rows}),dict(total),'failures',len(fail))
for x in fail[:30]:print('FAIL',*x)
if fail:sys.exit(1)
