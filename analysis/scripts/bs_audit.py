# Python 3.10: py -3 analysis/scripts/bs_audit.py <bs-index.tsv> <output-dir>
# Scan all compiled scripts with bs_dis and verify exported boundaries and terminator patterns.
import csv,pathlib,sys,collections
from bs_dis import read
index,out=map(pathlib.Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True);modules=[];functions=[];freq=collections.defaultdict(collections.Counter);errors=[]
with index.open(encoding='utf8',newline='') as f:
 for row in csv.DictReader(f,delimiter='\t'):
  key=row['archive']+'/'+row['member']
  try:
   base,deps,symbols,insns,end,data=read(row['output']);pos={pc:i for i,(pc,op,args) in enumerate(insns)};symbols=sorted(symbols,key=lambda s:s[1]);
   for i,(name,start) in enumerate(symbols):
    stop=symbols[i+1][1] if i+1<len(symbols) else end
    if stop<=start or stop not in pos and stop!=end:raise ValueError(f'bad symbol boundary {name}')
    last=insns[pos[stop]-1] if stop!=end else insns[-1]
    if last[1]!=0x1b:raise ValueError(f'{name} does not end in 001B')
    functions.append((key,name,f'{start:X}',f'{stop:X}',pos[stop]-pos[start] if stop!=end else len(insns)-pos[start],f'{last[1]:04X}'))
   for _,op,_ in insns:freq[op][key]+=1
   modules.append((key,f'{base:X}',f'{end:X}',len(insns),len(symbols),f'{insns[-1][1]:04X}',len(data)-end,'PASS'))
  except Exception as e:
   errors.append((key,str(e)));modules.append((key,'','','','','','','FAIL '+str(e)))
def write(name,heading,rows):
 with (out/name).open('w',encoding='utf8',newline='') as f:
  w=csv.writer(f,delimiter='\t');w.writerow(heading);w.writerows(rows)
write('bs_module_audit.tsv',('module','payload_offset','code_end','instructions','exports','last_opcode','data_bytes','result'),modules)
write('bs_function_audit.tsv',('module','symbol','start','end','instructions','last_opcode'),functions)
write('bs_opcode_usage.tsv',('opcode','static_sites','module_count','modules'),[(f'{op:04X}',sum(c.values()),len(c),','.join(c)) for op,c in sorted(freq.items(),key=lambda x:-sum(x[1].values()))])
print('modules',len(modules),'failed',len(errors),'exported functions',len(functions),'instructions',sum(v for c in freq.values() for v in c.values()),'opcodes',len(freq))
for e in errors:print('FAIL',*e)
if errors:sys.exit(1)
