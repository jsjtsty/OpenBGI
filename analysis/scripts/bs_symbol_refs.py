# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/bs_symbol_refs.py
# BS metadata and opcode 0003 references, not native execution or dynamic calls.
import collections,csv,pathlib
from bs_dis import read
ROOT=pathlib.Path(__file__).resolve().parents[1]
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
exports=collections.defaultdict(list);refs=collections.Counter();movie=[]
for r in rows(ROOT/'out/bs_corpus/index.tsv'):
 module=r['archive']+'/'+r['member'];base,deps,symbols,insns,end,data=read(ROOT.parent/pathlib.Path(r['output'].replace('\\','/')));bysym=collections.defaultdict(list)
 for name,off in symbols:exports[name].append((module,off));bysym[off].append(name)
 for i,(pc,op,args) in enumerate(insns):
  if op==1:
   for name in bysym.get(args[0],[]):refs[(module,name)]+=1
  if op==3:
   pos=args[0];endstr=data.find(b'\0',pos);val=data[pos:endstr].decode('cp932',errors='replace')
   if '.mpg' in val.lower() or '.bm' in val.lower():
    around=insns[max(0,i-3):i+6];context='; '.join(f'{p:08X}:{o:04X} '+','.join(f'{a:08X}' for a in ar) for p,o,ar in around)
    movie.append((module,f'{pc:08X}',val,context))
with (ROOT/'out/bs_symbol_refs.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('symbol','export_count','local_address_references','definitions'))
 for name,items in sorted(exports.items()):w.writerow((name,len(items),sum(refs[(m,name)] for m,o in items),','.join(f'{m}+0x{o:x}' for m,o in items)))
with (ROOT/'out/bs_movie_refs.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('module','offset','string','compiled_instruction_context'));w.writerows(movie)
print('export names',len(exports),'exports',sum(map(len,exports.values())),'movie string reference sites',len(movie))
