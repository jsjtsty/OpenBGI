# Python 3.10: py -3 analysis/scripts/scan_bp_flow.py <bp-dir> <per-program.tsv> <subop.tsv>
# Conservative research exploration based on bgi_dis.py. Indirect calls/jumps remain unresolved;
# direct targets are recognized only when a 0x06 instruction immediately precedes call/jump.
import csv,pathlib,struct,sys,collections
from bgi_dis import decode,DecodeError,PREFIX
root,out,subout=map(pathlib.Path,sys.argv[1:4]);rows=[];usage=collections.defaultdict(lambda:collections.Counter());seeds=collections.defaultdict(set)
if len(sys.argv)>4:
 with open(sys.argv[4],encoding='utf8',newline='') as f:
  for row in csv.DictReader(f,delimiter='\t'):
   key=row['program'].replace('\\','/')
   seeds[key].update(int(x,16) for x in row['candidate_targets'].split(','))
for file in sorted(root.rglob('*._bp')):
 data=file.read_bytes();base,length=struct.unpack_from('<II',data);code=data[base:base+length];key=file.relative_to(root).as_posix();pending=collections.deque([(0,None)]+[(p,None) for p in sorted(seeds[key])]);visited=set();byte_owner={};bad=[];indirect=0;overlap=0;counts=collections.Counter()
 while pending:
  pc,prev=pending.popleft()
  if pc in visited:continue
  if pc<0 or pc>=len(code):bad.append(f'target 0x{pc:x} outside');continue
  try:end,op,_,_=decode(code,pc)
  except DecodeError as e:bad.append(f'0x{pc:x}: {e}');continue
  if any(i in byte_owner for i in range(pc,end)):
   overlap+=1;continue
  visited.add(pc)
  for i in range(pc,end):byte_owner[i]=pc
  if op in PREFIX:counts[(op,code[pc+1])]+=1
  if op==0x17:continue
  if op in (0x15,0x3b):
   if op==0x3b or code[pc+1]&8:
    target=pc+struct.unpack_from('<h',code,pc+2)[0];pending.append((target,None))
   else:indirect+=1
   pending.append((end,None));continue
  if op in (0x14,0x16):
   if prev is not None and code[prev]==0x06 and prev+3==pc:
    target=prev+struct.unpack_from('<h',code,prev+1)[0];pending.append((target,None))
   else:indirect+=1
   if op==0x16:pending.append((end,None))
   continue
  pending.append((end,pc))
 key=file.relative_to(root).as_posix()
 for k,v in counts.items():usage[k][key]+=v
 rows.append((key,len(code),len(visited),len(byte_owner),indirect,overlap,len(bad),' | '.join(bad[:5])))
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','bytes','decoded_instructions','covered_bytes','unresolved_indirect','overlaps','errors','examples'));w.writerows(rows)
with subout.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('main','sub','static_sites','program_count','programs')); 
 for (op,sub),items in sorted(usage.items(),key=lambda x:-sum(x[1].values())):w.writerow((f'{op:02X}',f'{sub:02X}',sum(items.values()),len(items),','.join(items)))
print('programs',len(rows),'decoded',sum(r[2] for r in rows),'indirect',sum(r[4] for r in rows),'overlaps',sum(r[5] for r in rows),'errors',sum(r[6] for r in rows))
