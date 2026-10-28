# Python 3.10: py -3 analysis/scripts/validate_bs_hypothesis.py <index.tsv> <dispatch.tsv> <reads.tsv> <output.tsv>
# Independent checks of inferred sequential BS boundaries, symbols, and opcode 03 references.
# Based on scrmain 0x232..0x2c3; scrctrl 0x3e6 and 0x62d. Not runtime trace.
import csv,pathlib,struct,sys
index,dispatch,reads,out=map(pathlib.Path,sys.argv[1:]);ops=set();counts={}
with dispatch.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):ops.add(int(r['opcode'],16))
with reads.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  op=int(r['opcode'],16);v=r['direct_bs_read_counts'].split(',')
  counts[op]=(int(v[0]) if len(v)==1 and v[0].isdigit() else None) if r['program'].endswith('/00010_scrctrl._bp') else 0
rows=[]
with index.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  d=pathlib.Path(r['output']).read_bytes();base=28+struct.unpack_from('<I',d,28)[0];p=32;n=struct.unpack_from('<I',d,p)[0];p+=4
  def string():
   global p
   z=d.index(0,p,base);v=d[p:z];p=z+1;return v
  for i in range(n):string()
  n=struct.unpack_from('<I',d,p)[0];p+=4;symbols=[]
  for i in range(n):
   s=string();v=struct.unpack_from('<I',d,p)[0];p+=4;symbols.append((s,v))
  data=d[base:];pc=0;starts=set();refs=[]
  while pc+4<=len(data):
   op=struct.unpack_from('<I',data,pc)[0]
   if op not in ops or counts.get(op) is None:break
   size=4*(1+counts[op]);
   if pc+size>len(data):break
   starts.add(pc)
   if op==3:refs.append(struct.unpack_from('<I',data,pc+4)[0])
   pc+=size
  good=sum(v in starts for _,v in symbols);refvalid=sum(pc<=v<len(data) and data.find(b'\0',v)>=0 for v in refs);first=data[pc:pc+16].hex();rows.append((r['archive'],r['member'],len(starts),pc,len(symbols),good,len(refs),refvalid,first))
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('archive','member','instructions','code_end_candidate','symbol_count','symbols_at_starts','op03_count','op03_target_in_suffix','suffix_first16_hex'));w.writerows(rows)
print('modules',len(rows),'symbols at candidate starts',sum(r[5] for r in rows),'/',sum(r[4] for r in rows),'op03 refs in suffix',sum(r[7] for r in rows),'/',sum(r[6] for r in rows))
for r in rows:
 if r[4]!=r[5] or r[6]!=r[7]:print('mismatch',r[0],r[1],r[4:8])
