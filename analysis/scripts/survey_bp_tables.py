# Python 3.10: py -3 analysis/scripts/survey_bp_tables.py <bp-corpus> <tsv>
# Candidate local dispatch tables only, not proven function boundaries. Native opcodes
# 06 (FUN_004822b0), 0C (FUN_00482410); validate bounds and candidate prologue 12.
import pathlib,struct,sys,csv
root=pathlib.Path(sys.argv[1]); rows=[]
for f in sorted(root.rglob('*._bp')):
 b=f.read_bytes();base,size=struct.unpack_from('<II',b);d=b[base:base+size]
 for p in range(len(d)-5):
  if d[p]!=0x0c or d[p+1]!=2 or not 1<=d[p+2]<=128: continue
  count=d[p+2];start=p-3*count
  if start<0:continue
  entries=[]
  for q in range(start,p,3):
   if d[q]!=6:break
   target=q+struct.unpack_from('<h',d,q+1)[0]
   if not 0<=target<len(d) or d[target]!=0x12:break
   entries.append(target)
  if len(entries)==count:rows.append((str(f.relative_to(root)),f'{start:x}',count,','.join(f'{t:x}' for t in entries)))
with pathlib.Path(sys.argv[2]).open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(['program','table_start','entries','candidate_targets']);w.writerows(rows)
print('candidate tables',len(rows),'entries',sum(r[2] for r in rows),'programs',len(set(r[0] for r in rows)))
