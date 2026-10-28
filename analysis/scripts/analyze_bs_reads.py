# Python 3.10: py -3 analysis/scripts/analyze_bs_reads.py <bp-corpus> <dispatch.tsv> <output.tsv>
# Static CFG path analysis: scrctrl._bp 0x3e6 reads one u32 from BS IP and increments it.
# Direct calls to other helpers are reported separately; indirect calls/loops are not assumed.
import csv,pathlib,struct,sys,collections
from bgi_dis import decode,DecodeError
root=pathlib.Path(sys.argv[1]);dispatch=pathlib.Path(sys.argv[2]);output=pathlib.Path(sys.argv[3]);byfile={};rows=[]
with dispatch.open(encoding='utf8',newline='') as f:entries=list(csv.DictReader(f,delimiter='\t'))
for r in entries:
 name=r['program'];op=int(r['opcode'],16);start=int(r['handler'],16)
 if name not in byfile:
  b=(root/name).read_bytes();base,size=struct.unpack_from('<II',b);byfile[name]=b[base:base+size]
 d=byfile[name];todo=[(start,None,0,frozenset())];ends=set();unknown=[];calls=set();visited=set();steps=0
 while todo and steps<30000:
  pc,prev,count,path=todo.pop();steps+=1;state=(pc,prev,count)
  if state in visited:continue
  visited.add(state)
  if pc in path:unknown.append('loop');continue
  if pc<0 or pc>=len(d):unknown.append('out_of_range');continue
  try:end,code,_,_=decode(d,pc)
  except DecodeError as e:unknown.append('invalid');continue
  path=path|{pc}
  if code==0x17:ends.add(count);continue
  if code in (0x15,0x3b):
   if code==0x3b or d[pc+1]&8:todo.append((pc+struct.unpack_from('<h',d,pc+2)[0],None,count,path))
   else:unknown.append('indirect_branch')
   todo.append((end,None,count,path));continue
  if code in (0x14,0x16):
   if prev is not None and d[prev]==6 and prev+3==pc:
    target=prev+struct.unpack_from('<h',d,prev+1)[0]
    if code==0x14:todo.append((target,None,count,path));continue
    if name.endswith('/00010_scrctrl._bp') and target==0x3e6:count+=1
    else:calls.add(target)
   elif code==0x14:unknown.append('indirect_jump');continue
   else:unknown.append('indirect_call')
   if code==0x16:todo.append((end,None,count,path))
   continue
  todo.append((end,pc,count,path))
 if todo:unknown.append('limit')
 rows.append((f'{op:04X}',name,f'{start:06X}',','.join(map(str,sorted(ends))),','.join(f'{x:x}' for x in sorted(calls)),','.join(sorted(set(unknown)))))
with output.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('opcode','program','handler','direct_bs_read_counts','other_direct_calls','unresolved'));w.writerows(rows)
print('handlers',len(rows),'only zero unknown',sum(not r[5] for r in rows),'with variable read counts',sum(',' in r[3] for r in rows))
