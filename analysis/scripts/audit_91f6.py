# Python 3.10: py -3 analysis/scripts/audit_91f6.py <exe> <bp-corpus>
# Static table FUN_00493fd0 at VA 0x493fd0; 3B handler FUN_004830a0.
import pathlib,struct,sys
exe=pathlib.Path(sys.argv[1]).read_bytes(); corpus=pathlib.Path(sys.argv[2]); table=0x503818-0x401400
print('static 91 table F0..FB:')
for op in range(0xf0,0xfc):print(f'{op:02X}\t{struct.unpack_from("<I",exe,table+op*4)[0]:08X}')
for file in corpus.rglob('*._bp'):
 raw=file.read_bytes();base,size=struct.unpack_from('<II',raw);data=raw[base:base+size];pos=0
 while True:
  p=data.find(b'\x91\xf6',pos)
  if p<0:break
  if p>=6 and data[p-6:p]==bytes.fromhex('1a 04 80 0c 08 02'):
   print(f'{file.relative_to(corpus)}\t0x{p:x}\tcontext={data[max(0,p-32):p+2].hex(" ")}')
  pos=p+2
