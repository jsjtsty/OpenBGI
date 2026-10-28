# Python 3.10: py -3 analysis/scripts/index_bs_dispatch.py <bp-corpus> <out-tsv>
# scrmain._bp offsets 0x232..0x2c3 fetch a little-endian u32 and call [0x64818 + opcode*4].
# Statically list PUSH32 table address; 06 relative address; STORE32 initializers.
import pathlib,struct,sys,csv
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);table=0x64818;limit=0x65c18;rows=[]
for f in sorted(root.rglob('*._bp')):
 raw=f.read_bytes();base,size=struct.unpack_from('<II',raw);d=raw[base:base+size]
 for p in range(len(d)-10):
  if d[p]!=2 or d[p+5]!=6 or d[p+8] not in (9,10) or d[p+9]!=2:continue
  addr=struct.unpack_from('<I',d,p+1)[0];target=p+5+struct.unpack_from('<h',d,p+6)[0]
  if table<=addr<limit and (addr-table)%4==0 and 0<=target<len(d) and d[target]==0x12:
   rows.append(((addr-table)//4,f.relative_to(root).as_posix(),p,target,addr))
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('opcode','program','initializer','handler','table_address'))
 for op,name,p,t,a in sorted(rows):w.writerow((f'{op:04X}',name,f'{p:06X}',f'{t:06X}',f'{a:08X}'))
print('initializers',len(rows),'unique opcodes',len({x[0] for x in rows}),'programs',len({x[1] for x in rows}))
