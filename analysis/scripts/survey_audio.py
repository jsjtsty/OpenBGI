# survey members whose first bytes look like 'bw  ' in all ARC20 archives
import glob,struct,collections,os
ex={}
for f in sorted(glob.glob(r'E:\Games\Amairo Chocolate 2\*.arc')):
    d=open(f,'rb').read(); n=struct.unpack_from('<I',d,12)[0]
    if d[:4]!=b'PackFile'[:4] or n>100000: pass
    base=16+n*0x80;k=collections.Counter()
    for i in range(n):
        e=d[16+i*0x80:16+(i+1)*0x80];name=e[:0x60].split(b'\0')[0].decode('cp932','replace')
        off,size=struct.unpack_from('<II',e,0x60);h=d[base+off:base+off+4];k[h]+=1
        if h[:2]==b'bw':ex.setdefault(os.path.basename(f),[]).append((name,size,d[base+off:base+off+0x30].hex()))
    print(os.path.basename(f),n,k.most_common(3))
for f,v in ex.items(): print(f,len(v),v[:2])
