# Run the exe's real DSC decoder (FUN_004712b0: ecx=out, edx=dsc) in Unicorn and compare with out/dsc_manifest.tsv sha256.
# usage: .venv\Scripts\python.exe scripts\verify_dsc.py [max_members]
import sys, struct, glob, hashlib, os, emu
GAME=r'E:\Games\Amairo Chocolate 2'
k=emu.Kernel(); u=k.u
STUB=emu.RET+0x100
u.mem_write(STUB,b'\xc2\x04\x00')                   # ret 4 (Enter/LeaveCriticalSection)
for iat in (0x4f72fc,0x4f72f8): u.mem_write(iat,struct.pack('<I',STUB))
man={}
for l in open('out/dsc_manifest.tsv',encoding='utf8').read().splitlines()[1:]:
    p=l.split('\t'); man[(p[0],p[1])]=(int(p[3]),p[6],p[5])
lim=int(sys.argv[1]) if len(sys.argv)>1 else 10**9
ok=bad=0
for arc in sorted(glob.glob(GAME+r'\*.arc')):
    d=open(arc,'rb').read()
    if d[:4]!=b'PackFile'[:4] and d[:8]!=b'BURIKO A': pass
    n=struct.unpack_from('<I',d,12)[0]; base=16+n*0x80
    for i in range(n):
        e=d[16+i*0x80:16+(i+1)*0x80]; name=e[:0x60].split(b'\0')[0].decode('cp932','replace')
        off,size=struct.unpack_from('<II',e,0x60); m=d[base+off:base+off+size]
        key=(os.path.basename(arc),name)
        if m[:16]!=b'DSC FORMAT 1.00\0' or key not in man: continue
        osz=struct.unpack_from('<I',m,0x14)[0]
        k.heap=emu.HEAP
        src=k.alloc(m); out=k.alloc(b'\0'*(osz+16))
        k.call(0x4712b0,out,src)
        r=k.read(out,osz); h=hashlib.sha256(r).hexdigest()
        if h==man[key][1] and osz==man[key][0]: ok+=1
        else: bad+=1; print('MISMATCH',key)
        if ok+bad>=lim: break
    if ok+bad>=lim: break
print('ok',ok,'bad',bad)
