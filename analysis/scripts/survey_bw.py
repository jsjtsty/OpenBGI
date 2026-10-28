# Verify BW header (0x40 bytes + Ogg Vorbis) against libsndfile for every audio member.
import struct,io,glob,os,collections
import soundfile as sf
bad=collections.Counter();tot=0;loops=collections.Counter();ex={}
for f in ('data03300','data04400','data04401','data04410','data04411','data04420','data05500'):
    d=open(r'E:\Games\Amairo Chocolate 2\%s.arc'%f,'rb').read();n=struct.unpack_from('<I',d,12)[0];base=16+n*0x80
    for i in range(n):
        e=d[16+i*0x80:16+(i+1)*0x80];name=e[:0x60].split(b'\0')[0].decode('cp932');off,size=struct.unpack_from('<II',e,0x60)
        m=d[base+off:base+off+size];tot+=1
        h=struct.unpack_from('<16I',m,0)
        if h[0]!=0x40 or m[4:8]!=b'bw  ' or h[2]!=size-0x40: bad['hdr']+=1;continue
        try: inf=sf.info(io.BytesIO(m[0x40:]))
        except Exception as ex_: bad['sf']+=1;continue
        if inf.frames!=h[3]: bad['frames']+=1
        if inf.samplerate!=h[4]: bad['rate']+=1
        if inf.channels!=h[5]: bad['ch']+=1
        loops[(h[6],h[7]>0,h[8]>0,h[9],h[14])]+=1
        if h[6] and f not in ex: ex[f]=(name,h)
print('total',tot,'bad',dict(bad));print(loops.most_common(10));print(ex)
