# Python 3.10: py -3 analysis/scripts/parse_bs.py <BS-corpus/index.tsv> <output-dir>
# Experimental metadata parsing; script._bp program offsets 0x16f..0x34d, native handlers
# FUN_00497430, FUN_004991d0 and FUN_00499240. Addresses remain static inferences.
import csv,pathlib,struct,sys
index,out=map(pathlib.Path,sys.argv[1:3]);out.mkdir(parents=True,exist_ok=True)
modules=[];names=[];symbols=[]
with index.open(encoding='utf8',newline='') as f:
 for r in csv.DictReader(f,delimiter='\t'):
  path=pathlib.Path(r['output']);d=path.read_bytes();end=28+struct.unpack_from('<I',d,28)[0];p=32
  def u32():
   global p
   if p+4>end:raise ValueError(f'{path}: truncated')
   v=struct.unpack_from('<I',d,p)[0];p+=4;return v
  def string():
   global p
   z=d.find(b'\0',p,end)
   if z<0:raise ValueError(f'{path}: unterminated name')
   val=d[p:z].decode('utf8',errors='replace');p=z+1;return val
  n=u32()
  for j in range(n):names.append((r['archive'],r['member'],j,string()))
  count=u32()
  for j in range(count):
   sym=string();off=u32();
   if off>=len(d)-end:raise ValueError(f'{path}: symbol out of bounds')
   symbols.append((r['archive'],r['member'],j,sym,f'{off:08X}',f'{end+off:08X}'))
  modules.append((r['archive'],r['member'],end,len(d)-end,n,count,end-p))
def write(name,heading,data):
 with (out/name).open('w',encoding='utf8',newline='') as f:
  w=csv.writer(f,delimiter='\t');w.writerow(heading);w.writerows(data)
write('bs_modules.tsv',('archive','member','section_offset','section_size','name_count','symbol_count','padding'),modules)
write('bs_dependencies.tsv',('archive','member','index','name'),names)
write('bs_symbols.tsv',('archive','member','index','name','relative_offset','file_offset'),symbols)
print('modules',len(modules),'names',len(names),'symbols',len(symbols))
