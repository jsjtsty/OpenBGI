# Candidate same-program calls only: immediately adjacent ip_relative + call.
# Paths ignore branches; dynamically calculated and cross-program calls are absent.
import collections,csv,pathlib,struct
import bgi_dis,bs_dis
ROOT=pathlib.Path(__file__).resolve().parents[1]
def rows(path):
 with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
func={(r['program'],int(r['start'],16)):int(r['end'],16) for r in rows(ROOT/'out/bp_functions.tsv')}
data={};direct={};edges={};unresolved=[]
for (program,start),end in func.items():
 if program not in data:
  raw=(ROOT/'out/bp_corpus_v2'/program).read_bytes();base,size=struct.unpack_from('<II',raw);data[program]=raw[base:base+size]
 code=data[program];pc=start;ins=[];subs=set()
 while pc<end:
  pos=pc;pc,op,_,_=bgi_dis.decode(code,pc,True);ins.append((pos,op,pc))
  if op in bgi_dis.PREFIX:subs.add(f'{op:02X}:{code[pos+1]:02X}')
 targets=set()
 for (pos,op,nextpc),(callpos,callop,_) in zip(ins,ins[1:]):
  if op==6 and callop==0x16 and nextpc==callpos:
   target=pos+struct.unpack_from('<h',code,pos+1)[0];key=(program,target)
   if key in func:targets.add(key)
   else:unresolved.append((program,start,pos,target))
 direct[(program,start)]=subs;edges[(program,start)]=targets
handlers=rows(ROOT/'out/bs_dispatch_candidates.tsv');opentry={int(r['opcode'],16):(r['program'],int(r['handler'],16)) for r in handlers}
closure={};dist={};witness=[]
for op,entry in opentry.items():
 q=collections.deque([(entry,0,(entry,))]);seen=set();subdist={}
 while q:
  key,depth,path=q.popleft()
  if key in seen:continue
  seen.add(key)
  for sub in direct.get(key,()):
   if sub not in subdist:
    subdist[sub]=depth
    if sub in ('90:56','90:38','A0:25','A0:26'):
     witness.append((f'{op:04X}',sub,depth,' -> '.join(f'{a}+0x{b:X}' for a,b in path)))
  for nxt in sorted(edges.get(key,())):q.append((nxt,depth+1,path+(nxt,)))
 closure[op]=subdist;dist[op]=len(seen)
counts=collections.Counter();byop=collections.defaultdict(set);depths=collections.defaultdict(set);n=0
for row in rows(ROOT/'out/bs_corpus/index.tsv'):
 path=ROOT.parent/pathlib.Path(row['output'].replace('\\','/'));*_,ins,end,data0=bs_dis.read(path)
 for pc,op,args in ins:
  n+=1
  for sub,depth in closure.get(op,{}).items():counts[sub]+=1;byop[sub].add(op);depths[sub].add(depth)
out=ROOT/'out/bs_subop_callgraph.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','candidate_compiled_sites','compiled_opcodes','minimum_call_depths','limits'))
 for sub in sorted(counts,key=lambda x:(-counts[x],x)):
  w.writerow((sub,counts[sub],','.join(f'{op:04X}' for op in sorted(byop[sub])),','.join(map(str,sorted(depths[sub]))),'same-module adjacent direct calls only; path-insensitive; not runtime frequency'))
with (ROOT/'out/bp_callgraph_edges.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('program','caller','callee'))
 for (program,start),targets in sorted(edges.items()):
  for _,target in sorted(targets):w.writerow((program,f'{start:X}',f'{target:X}'))
with (ROOT/'out/bs_callgraph_witness.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('compiled_opcode','subop','depth','candidate_function_path'));w.writerows(witness)
print('candidate functions',len(func),'edges',sum(map(len,edges.values())),'unresolved calls',len(unresolved),'compiled sites',n,'reachable subops',len(counts))
print('sample',counts.most_common(12))
for sub in ('90:56','90:38','A0:25','A0:26'):
 print(sub,'candidate sites',counts[sub],'compiled opcodes',','.join(f'{x:04X}' for x in sorted(byop[sub])))
