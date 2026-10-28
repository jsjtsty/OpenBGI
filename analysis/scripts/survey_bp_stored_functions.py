# Python 3.10: py -3 analysis/scripts/survey_bp_stored_functions.py <bp-corpus> <tsv>
# Experimental pattern: PUSH32 address; PUSH_IP_REL; STORE32 (FUN_00482230/004822b0/00482330).
# An in-bounds 0x12 target is only a candidate, not proof of a function.
import pathlib,struct,sys,csv
root=pathlib.Path(sys.argv[1]);rows=[]
for f in sorted(root.rglob('*._bp')):
 b=f.read_bytes();base,size=struct.unpack_from('<II',b);d=b[base:base+size]
 for p in range(len(d)-10):
  if d[p]!=2 or d[p+5]!=6 or d[p+8] not in (9,10) or d[p+9]!=2:continue
  target=p+5+struct.unpack_from('<h',d,p+6)[0]
  if 0<=target<len(d) and d[target]==0x12:
   rows.append((f.relative_to(root).as_posix(),f'{p:x}',1,f'{target:x}'))
with pathlib.Path(sys.argv[2]).open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','table_start','entries','candidate_targets'));w.writerows(rows)
print('candidate stores',len(rows),'programs',len({r[0] for r in rows}))
