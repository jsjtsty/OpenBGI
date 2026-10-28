# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/main_opcode_usage.py
# Count candidate native _bp instructions in already-recovered function ranges.
# Does not count compiled _bs VM numeric opcodes as native instructions.
import collections,csv,pathlib,struct
import bgi_dis
ROOT=pathlib.Path(__file__).resolve().parents[1]
with (ROOT/'out/bp_functions.tsv').open(encoding='utf8',newline='') as f:funcs=list(csv.DictReader(f,delimiter='\t'))
cache={};sites=collections.defaultdict(set);programs=collections.defaultdict(set);fail=[]
for r in funcs:
 program=r['program'];start=int(r['start'],16);end=int(r['end'],16)
 if program not in cache:
  b=(ROOT/'out/bp_corpus_v2'/program).read_bytes();off,n=struct.unpack_from('<II',b);cache[program]=b[off:off+n]
 data=cache[program];pc=start
 try:
  while pc<end:
   old=pc;pc,op,_,_=bgi_dis.decode(data,pc,True)
   if pc>end:raise ValueError('instruction extends beyond function')
   sites[op].add((program,old));programs[op].add(program)
 except (ValueError,bgi_dis.DecodeError) as e:fail.append((program,start,pc,str(e)))
out=ROOT/'out/main_opcode_usage.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('opcode','candidate_sites','programs','examples','limits'))
 for op in range(256):
  s=sites[op];examples=','.join(f'{p}+0x{pos:X}' for p,pos in sorted(s)[:3])
  w.writerow((f'{op:02X}',len(s),len(programs[op]),examples,'candidate native function ranges; not runtime executions; compiled _bs is separate'))
print('functions',len(funcs),'programs',len(cache),'decode failures',len(fail),'site count',sum(map(len,sites.values())))
print('requested:',', '.join(f'{op:02X}={len(sites[op])}' for op in (0xb,0x10,0x11,*range(0x42,0x4a),*range(0x50,0x60),*range(0x64,0x67),*range(0x6c,0x6f),*range(0x73,0x76),*range(0x77,0x7f))))
if fail:print('first failures:',fail[:5])
