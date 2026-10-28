# Usage: py -3 analysis/scripts/sud_thumb.py <save.sud> [output.png]
# sysprg/userdata._bp+0x519: 91:1C scales 1280x720 by 10394/10377 in 16.16;
# +0x557: 90:15 -> FUN_004092d0 copies tightly packed 24-bit rows.
import pathlib,struct,sys,zlib,binascii
WIDTH,HEIGHT=203,114;START=0x848;SIZE=WIDTH*HEIGHT*3
p=pathlib.Path(sys.argv[1]);b=p.read_bytes()
if len(b)<START+SIZE or b[START+SIZE:START+SIZE+15]!=b'SDC FORMAT 1.00':
 raise ValueError('save header length or first SDC block differs; layout not established for this file')
raw=b[START:START+SIZE];scan=bytearray()
for y in range(HEIGHT):
 scan.append(0)
 for x in range(WIDTH):
  i=(y*WIDTH+x)*3;blue,green,red=raw[i:i+3];scan.extend((red,green,blue))
def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',binascii.crc32(t+d)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',WIDTH,HEIGHT,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan,6))+chunk(b'IEND',b'')
out=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else pathlib.Path('analysis/out')/(p.stem+'_thumb.png');out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(png)
print(f'{p.name}: {WIDTH}x{HEIGHT}, {SIZE} B, BGR24 -> {out}')
