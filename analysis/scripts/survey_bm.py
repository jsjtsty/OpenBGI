# Usage: py -3 analysis/scripts/survey_bm.py <game_directory>
# ARC20 directory and BF_Movie header / frame offset validation, no asset copies.
import csv,pathlib,struct,sys
u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
out=pathlib.Path('analysis/out/bm_manifest.tsv')
with out.open('w',encoding='utf8',newline='') as target:
 w=csv.writer(target,delimiter='\t');w.writerow(('archive','member','size','version','width','height','bit_depth','format','rate_field','frames','field_2c','first_frame','offsets_valid'))
 for p in sorted(pathlib.Path(sys.argv[1]).glob('*.arc')):
  with p.open('rb') as f:
   head=f.read(16)
   if not head.startswith(b'BURIKO ARC20'):continue
   n=u32(head,12);table=f.read(n*128);base=16+128*n
   for i in range(n):
    r=table[i*128:(i+1)*128];name=r[:96].split(b'\0')[0].decode('cp932','replace')
    if not name.lower().endswith('.bm'):continue
    offset,size=struct.unpack_from('<II',r,96);f.seek(base+offset);hdr=f.read(0xc0)
    if hdr[:16]!=b'BF_Movie_______\0':raise ValueError((p,name,'signature'))
    v,width,height,bits,fmt,rate,frames,other=struct.unpack_from('<8I',hdr,16)
    if frames>100000 or 0xc0+4*frames>size:raise ValueError((p,name,'frame count'))
    offsets=struct.unpack('<'+'I'*frames,f.read(4*frames));valid=all(0xc0+4*frames<=x<size for x in offsets) and all(a<=b for a,b in zip(offsets,offsets[1:]))
    w.writerow((p.name,name,size,f'{v:08x}',width,height,bits,fmt,rate,frames,other,offsets[0] if offsets else '',valid));print(p.name,name,frames,valid)
