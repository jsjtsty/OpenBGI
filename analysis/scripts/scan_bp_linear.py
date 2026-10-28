# Python 3.10: py -3 analysis/scripts/scan_bp_linear.py <bp-dir> <report.tsv>
# Hypothetical linear sweep with null child slots accepted as two-byte evidence.
# Unknown, padding and embedded data terminate a sweep; does not establish reachability.
import csv,pathlib,struct,sys
from bgi_dis import decode,DecodeError,PREFIX,SUB
root,out=map(pathlib.Path,sys.argv[1:]);rows=[]
for f in sorted(root.rglob('*._bp')):
 d=f.read_bytes();base,size=struct.unpack_from('<II',d);code=d[base:base+size];p=0;n=0;null=0;last_ret=None;status='end'
 try:
  while p<len(code):
   start=p;p,op,_,_=decode(code,p,True);n+=1
   if op in PREFIX and (op,code[start+1]) not in SUB:null+=1
   if op==0x17:last_ret=p
 except DecodeError as e:status=str(e)
 rows.append((f.relative_to(root).as_posix(),len(code),n,p,last_ret if last_ret is not None else '',null,status))
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','segment_bytes','insns','stop','last_ret','null_sub','status'));w.writerows(rows)
print('programs',len(rows),'instructions',sum(r[2] for r in rows),'null_sub',sum(r[5] for r in rows),'lastret=stop',sum(r[3]==r[4] for r in rows))
