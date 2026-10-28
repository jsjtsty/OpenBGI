# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/bs_native_map.py
# Static relation: compiled opcode -> initial _bp handler -> native sub-opcodes
# directly present in that handler's candidate function. No dynamic count claim.
import csv, pathlib, struct, collections
import bgi_dis, bs_dis
ROOT=pathlib.Path(__file__).resolve().parents[1]
def tsv(path):
 with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
func={(r['program'],int(r['start'],16)):int(r['end'],16) for r in tsv(ROOT/'out/bp_functions.tsv')}
handlers=tsv(ROOT/'out/bs_dispatch_candidates.tsv');mapping={};missing=[]
for row in handlers:
 key=(row['program'],int(row['handler'],16));end=func.get(key)
 if end is None:missing.append((row['opcode'],*key));continue
 raw=(ROOT/'out/bp_corpus_v2'/key[0]).read_bytes();base,size=struct.unpack_from('<II',raw);data=raw[base:base+size];pc=key[1];found=[]
 while pc<end:
  begin=pc;pc,op,name,arg=bgi_dis.decode(data,pc,True)
  if op in bgi_dis.PREFIX:found.append((f'{op:02X}:{data[begin+1]:02X}',begin))
 mapping[int(row['opcode'],16)]=[(x,key[0],p) for x,p in found]
counts=collections.Counter();modules=collections.defaultdict(set);bsops=collections.defaultdict(set);native=collections.defaultdict(set);symbols=collections.Counter();num=0
for row in tsv(ROOT/'out/bs_corpus/index.tsv'):
 path=ROOT.parent/pathlib.Path(row['output'].replace('\\','/'));base,deps,syms,insns,end,data=bs_dis.read(path);module=row['archive']+'/'+row['member'];num+=len(insns)
 for name,off in syms:symbols[name]+=1
 for pc,op,args in insns:
  seen=set()
  for sub,program,pos in mapping.get(op,[]):
   if sub not in seen:counts[sub]+=1;seen.add(sub)
   modules[sub].add(module);bsops[sub].add(f'{op:04X}');native[sub].add(f'{program}+0x{pos:x}')
out=ROOT/'out/bs_subop_usage.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('native_subop','distinct_compiled_sites_with_subop_in_handler','modules','compiled_opcodes','native_handler_sites','evidence_limit'))
 for key in sorted(counts,key=lambda k:(-counts[k],k)):
  w.writerow((key,counts[key],','.join(sorted(modules[key])),','.join(sorted(bsops[key])),','.join(sorted(native[key])),'distinct compiled sites, not executions; native handler branches and indirect calls unknown'))
print('compiled instruction sites',num,'handler mappings',len(mapping),'missing function starts',len(missing),'native subops',len(counts))
print('missing examples',missing[:5]);print('top',counts.most_common(15))
