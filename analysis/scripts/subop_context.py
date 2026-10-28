# Python 3.10+: py -3 analysis/scripts/subop_context.py
# Samples candidate native _bp instruction contexts from task C function boundaries.
# Constants are nearby syntax, not guaranteed arguments (branches and calls can intervene).
import csv, pathlib, struct
from collections import defaultdict
from bgi_dis import decode, PREFIX
ROOT=pathlib.Path(__file__).resolve().parents[1]
with (ROOT/'out/bp_functions.tsv').open(encoding='utf8',newline='') as f: funcs=list(csv.DictReader(f,delimiter='\t'))
allrows=defaultdict(list);cache={}
for row in funcs:
 program=row['program'];path=ROOT/'out/bp_corpus_v2'/program
 if program not in cache:
  raw=path.read_bytes();base,size=struct.unpack_from('<II',raw);cache[program]=raw[base:base+size]
 data=cache[program];pc=int(row['start'],16);end=int(row['end'],16);hist=[]
 while pc<end:
  start=pc
  try:pc,op,label,args=decode(data,pc,True)
  except Exception:break
  if op in PREFIX:
   key=f'{op:02X}:{data[start+1]:02X}'
   ctx=hist[-10:]; score=sum(x[1] in (0,1,2) for x in ctx)
   allrows[key].append((score,program,start,ctx))
  hist.append((start,op,label,args))
out=ROOT/'out/subop_context.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('opcode','program','offset','nearby_constant_count','preceding_instructions'))
 for key in sorted(allrows):
  for score,program,pc,ctx in sorted(allrows[key],key=lambda x:(-x[0],x[1],x[2]))[:3]:
   w.writerow((key,program,f'{pc:08X}',score,'; '.join(f'{p:04X} {label} {args}' for p,op,label,args in ctx)))
print('opcodes',len(allrows),'sites',sum(map(len,allrows.values())),'output',out)
