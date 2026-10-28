# Python 3.10: py -3 analysis/scripts/scan_bp_entries.py <bp-directory> <output.tsv>
# Entry-point linear scan only; instruction semantics from bgi_dis.py. Does not discover functions.
import csv,pathlib,struct,sys
from bgi_dis import decode,DecodeError
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]); rows=[]
for f in sorted(root.rglob('*._bp')):
 d=f.read_bytes();base,size=struct.unpack_from('<II',d);code=d[base:base+size];p=0;n=0;status='end'
 try:
  while p<len(code) and n<100000:
   p,op,_,_=decode(code,p);n+=1
   if op==0x17:status='ret';break
  else:
   if n>=100000:status='limit'
 except DecodeError as e:status=str(e)
 rows.append((f.relative_to(root).as_posix(),len(code),n,p,status))
with out.open('w',encoding='utf-8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','segment_bytes','linear_instructions','stop_offset','status'));w.writerows(rows)
from collections import Counter
print('programs',len(rows),'status',dict(Counter(r[-1] for r in rows)))
