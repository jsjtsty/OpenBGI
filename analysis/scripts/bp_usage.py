# Python 3.10: py -3 analysis/scripts/bp_usage.py <bp-corpus> <bp-linear-report.tsv> <output.tsv>
# Count static native two-byte opcode sites to each provisional final RET, including NULL slots.
# NOT dynamic call counts: the code/data boundary and reachability remain hypotheses.
import csv,pathlib,struct,sys,collections
from bgi_dis import decode,PREFIX,SUB
root,report,out=map(pathlib.Path,sys.argv[1:]);freq=collections.defaultdict(collections.Counter);nulls=collections.Counter();total=0
with report.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  file=root/r['program'];b=file.read_bytes();base,size=struct.unpack_from('<II',b);d=b[base:base+size];end=int(r['last_ret']);p=0
  while p<end:
   start=p;p,op,_,_=decode(d,p,True);total+=1
   if p>end:raise ValueError(f'{r["program"]}: crossing code candidate')
   if op in PREFIX:
    sub=d[start+1];freq[(op,sub)][r['program']]+=1
    if (op,sub) not in SUB:nulls[(op,sub)]+=1
  if p!=end:raise ValueError(f'{r["program"]}: code candidate misalignment')
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('main_opcode','sub_opcode','static_sites','program_count','status','programs'))
 for (a,b),items in sorted(freq.items(),key=lambda t:-sum(t[1].values())):
  w.writerow((f'{a:02X}',f'{b:02X}',sum(items.values()),len(items),'INITIAL_NULL' if (a,b) not in SUB else 'INITIAL_PRESENT',','.join(items)))
print('static native instructions',total,'subop sites',sum(sum(x.values()) for x in freq.values()),'distinct',len(freq),'null',dict(nulls))
