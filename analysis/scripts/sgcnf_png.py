# Usage: py -3 analysis/scripts/sgcnf_png.py <DSC-decoded-member> [output.png]
# Bitmap header/format handling FUN_00402110 / FUN_004028e0; source channel order inferred BGRA/BGR.
import binascii,pathlib,struct,sys,zlib
p=pathlib.Path(sys.argv[1]);b=p.read_bytes();w,h,bits,compression=struct.unpack_from('<4H',b)
if compression or bits not in (24,32) or len(b)!=16+w*h*(bits//8):raise ValueError('unsupported layout or length')
stride=bits//8;rows=bytearray()
for y in range(h):
 rows.append(0)
 for x in range(w):
  i=16+(y*w+x)*stride;blue,green,red=b[i:i+3];rows.extend((red,green,blue,b[i+3] if stride==4 else 255))
def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',binascii.crc32(t+d)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b'')
out=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else pathlib.Path('analysis/out')/(p.stem+'.png');out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(png);print(p.name,w,h,bits,'->',out)
