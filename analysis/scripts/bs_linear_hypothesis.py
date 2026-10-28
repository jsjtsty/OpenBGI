# Python 3.10: py -3 analysis/scripts/bs_linear_hypothesis.py <BS-corpus/index.tsv> <dispatch.tsv> <reads.tsv> <output-prefix>
# Exploratory only. scrmain._bp 0x232..0x2c3 fetches a u32; scrctrl._bp 0x3e6 reads one u32 operand.
# Non-scrctrl handlers are tentatively zero-immediate; indirect helpers, data, variable paths not resolved.
import pathlib,sys,struct,csv,collections
index,dispatch,reads,prefix=map(pathlib.Path,sys.argv[1:5]); handlers={}; counts={}
with dispatch.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):handlers[int(r['opcode'],16)]=r
with reads.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  op=int(r['opcode'],16)
  if r['program'].endswith('/00010_scrctrl._bp'):
   v=r['direct_bs_read_counts'].split(',');counts[op]=int(v[0]) if len(v)==1 and v[0].isdigit() else None
  else:counts[op]=0
rows=[];freq=collections.defaultdict(collections.Counter)
with index.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  d=pathlib.Path(r['output']).read_bytes();base=28+struct.unpack_from('<I',d,28)[0];code=d[base:];pc=0;n=0;status='end';local=collections.Counter()
  while pc+4<=len(code):
   op=struct.unpack_from('<I',code,pc)[0]
   if op not in handlers:status=f'unknown {op:08X}';break
   operands=counts.get(op)
   if operands is None:status=f'unknown width {op:04X}';break
   nxt=pc+4*(1+operands)
   if nxt>len(code):status=f'truncated operands {op:04X}';break
   local[op]+=1;n+=1;pc=nxt
  if pc+4>len(code) and pc!=len(code):status='trailing bytes'
  key=r['archive']+'/'+r['member'];rows.append((key,len(code),n,pc,status))
  for op,c in local.items():freq[op][key]+=c
for suffix,heading,data in [('_modules.tsv',('module','payload_bytes','hypothesis_insns','stop_offset','stop_reason'),rows),('_opcodes.tsv',('opcode','sites','module_count','modules'),[(f'{op:04X}',sum(x.values()),len(x),','.join(x)) for op,x in sorted(freq.items(),key=lambda p:-sum(p[1].values()))])]:
 with pathlib.Path(str(prefix)+suffix).open('w',encoding='utf8',newline='') as f:w=csv.writer(f,delimiter='\t');w.writerow(heading);w.writerows(data)
print('modules',len(rows),'instructions',sum(r[2] for r in rows),'stop reasons',collections.Counter(r[-1].split()[0] for r in rows))
