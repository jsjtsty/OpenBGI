# usage: bw_extract.py <arc> <member> <out.ogg> -- strip the 0x40-byte BW header and write the Ogg Vorbis stream; prints header fields
import sys,struct
d=open(sys.argv[1],'rb').read();n=struct.unpack_from('<I',d,12)[0];base=16+n*0x80
for i in range(n):
    e=d[16+i*0x80:16+(i+1)*0x80]
    if e[:0x60].split(b'\0')[0].decode('cp932')==sys.argv[2]:
        off,size=struct.unpack_from('<II',e,0x60);m=d[base+off:base+off+size];h=struct.unpack_from('<16I',m,0)
        print('ogg bytes %d frames %d rate %d channels %d loop_flag %d loop_start %d'%(h[2],h[3],h[4],h[5],h[6],h[7]))
        open(sys.argv[3],'wb').write(m[0x40:]);break
else: sys.exit('member not found')
