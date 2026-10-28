# Python 3.10: py -3 analysis/scripts/find_bp_string_refs.py <decoded._bp> <linear-end> [<string>...]
# Uses tentative 0x05/0x06 relative operands; verified against sysprg/script._bp.
import pathlib,struct,sys
from bgi_dis import decode,DecodeError
if len(sys.argv)<4:raise SystemExit('usage: file linear-end string...')
raw=pathlib.Path(sys.argv[1]).read_bytes();base,size=struct.unpack_from('<II',raw);d=raw[base:base+size];end=int(sys.argv[2],0);need=[s.encode('utf-8') for s in sys.argv[3:]];p=0
while p<min(end,len(d)):
 s=p
 try:p,op,_,_=decode(d,p)
 except DecodeError as e:print(f'{s:08x} ERROR {e}');break
 if op in (5,6):
  target=s+struct.unpack_from('<h',d,s+1)[0]
  if 0<=target<len(d):
   zero=d.find(b'\0',target);val=d[target:zero] if zero>=0 else b''
   if any(x in val for x in need):print(f'{s:08x}\t{target:08x}\t{val.decode("utf-8",errors="replace")}')
