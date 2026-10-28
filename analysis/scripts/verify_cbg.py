# Run the exe's real CompressedBG v1 decoder (FUN_00477a60: ecx=out, edx=cbg) in Unicorn and compare with decode_cbg_v1.decode.
# usage: .venv\Scripts\python.exe scripts\verify_cbg.py [every_nth=25]
import sys, struct, glob, os, emu
from unicorn import *; from unicorn.x86_const import *
sys.path.insert(0,'scripts'); import decode_cbg_v1 as ref
GAME=r'E:\Games\Amairo Chocolate 2'
k=emu.Kernel(); u=k.u
STUB=emu.RET+0x100; u.mem_write(STUB,b'\xc2\x04\x00')
for iat in (0x4f72fc,0x4f72f8): u.mem_write(iat,struct.pack('<I',STUB))
def hook(uc,addr,size,_):
    esp=uc.reg_read(UC_X86_REG_ESP); ret=struct.unpack('<I',uc.mem_read(esp,4))[0]
    if addr==0x4cf54b:                         # malloc(size), cdecl
        n=struct.unpack('<I',uc.mem_read(esp+4,4))[0]; a=k.heap; k.heap=(a+n+63)&~15; uc.reg_write(UC_X86_REG_EAX,a)
    uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)   # free: just return
u.hook_add(UC_HOOK_CODE,hook,None,0x4cf54b,0x4cf54b); u.hook_add(UC_HOOK_CODE,hook,None,0x4e0964,0x4e0964)
step=int(sys.argv[1]) if len(sys.argv)>1 else 25
ONLY=set(sys.argv[2:])
ok=bad=skip=0; cnt=0
for arc in sorted(glob.glob(GAME+r'\*.arc')):
    d=open(arc,'rb').read(); n=struct.unpack_from('<I',d,12)[0]; base=16+n*0x80
    for i in range(n):
        e=d[16+i*0x80:16+(i+1)*0x80]; name=e[:0x60].split(b'\0')[0].decode('cp932','replace')
        off,size=struct.unpack_from('<II',e,0x60); m=d[base+off:base+off+size]
        if m[:16]!=b'CompressedBG___\0': continue
        if struct.unpack_from('<H',m,46)[0]!=1: skip+=1; continue
        cnt+=1
        if ONLY and name not in ONLY: continue
        if not ONLY and cnt%step: continue
        w,h,bpp=struct.unpack_from('<HHH',m,16); ch=bpp//8
        k.heap=emu.HEAP; src=k.alloc(m); out=k.alloc(b'\0'*(16+w*h*4+16))
        k.call(0x477a60,out,src)
        raw=k.read(out+16,w*h*ch)
        rw,rh,rb,rgba=ref.decode(m)
        mine=bytearray(w*h*4)
        for p in range(w*h):
            q=p*4;s=p*ch
            if ch==1: mine[q:q+3]=bytes([raw[s]])*3
            else: mine[q:q+3]=bytes((raw[s+2],raw[s+1],raw[s]))
            mine[q+3]=raw[s+3] if ch==4 else 255
        if bytes(mine)==bytes(rgba): ok+=1
        else: bad+=1; print('MISMATCH',os.path.basename(arc),name)
print('ok',ok,'bad',bad,'skipped(non-v1)',skip)
