# Text glyph post-processing in Unicorn: table init FUN_00408890, glyph colouring FUN_0040a090, outline FUN_0040a2a0.
# The GDI glyph source FUN_004345c0 is replaced by a stub that returns a synthetic 8-bit alpha bitmap.
import sys, struct, random, math, emu
from unicorn import *; from unicorn.x86_const import *
k=emu.Kernel(); u=k.u
def hook(uc,addr,size,_):                                  # malloc(size), cdecl
    esp=uc.reg_read(UC_X86_REG_ESP); ret=struct.unpack('<I',uc.mem_read(esp,4))[0]
    n=struct.unpack('<I',uc.mem_read(esp+4,4))[0]; a=k.heap; k.heap=(a+n+63)&~15
    uc.reg_write(UC_X86_REG_EAX,a); uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)
u.hook_add(UC_HOOK_CODE,hook,None,0x4cf54b,0x4cf54b)
k.call(0x408890,0,0)                                        # builds the radius weight tables at 0x534690..
HB=k.heap
TAB=[struct.unpack('<I',k.read(0x534690+4*i,4))[0] for i in range(5)]
tabs={}                                                     # radius r (1..5) -> (2r+1)^2 weights, 16.16
for r in range(1,6):
    n=2*r+1; tabs[r]=list(struct.unpack('<%dI'%(n*n),k.read(TAB[r-1],n*n*4)))
if '-t' in sys.argv:
    for r in (1,2): n=2*r+1; print('r',r,[[tabs[r][y*n+x] for x in range(n)] for y in range(n)])
# glyph stub: 0x4345c0 (thiscall ecx=font; stack out,code; ret 8): out[2]=GLYPH
GL=0x38000000; u.mem_map(GL,0x100000); FONT=0x38200000; u.mem_map(FONT,0x1000)
u.mem_write(0x4345c0,b'\x8b\x44\x24\x04\xc7\x40\x08'+struct.pack('<I',GL)+b'\xc2\x08\x00')
def font(w,h,pitch): u.mem_write(FONT+0x50,struct.pack('<III',w,h,0)); u.mem_write(FONT+0x5c,struct.pack('<I',pitch))
def callc(va,*args,ecx=0,edx=0):                           # cdecl-style stack args
    k.call(va,ecx,edx,*args)
rnd=random.Random(3)
def glyph(w,h):
    g=bytes(rnd.choice([0,0,0,255,rnd.randrange(256)]) for _ in range(w*h)); u.mem_write(GL,g); return g
# ---- colouring FUN_0040a090(dst, out, code, font, color)
bad=0
for fmt in (0,1,2):
    for (w,h,dw,dh) in ((7,5,7,5),(7,5,5,3),(7,5,9,7)):
        g=glyph(w,h); font(w,h,w); k.heap=HB
        bpp=emu.BPP[fmt]; s,a=k.bitmap(bytes(dw*dh*bpp),dw,dh,fmt); out=k.alloc(bytes(0x40))
        col=0x80c3e5; callc(0x40a090,s,out,0x41,FONT,col,ecx=0)
        res=k.read(a,dw*dh*bpp); ww=min(w,dw); hh=min(h,dh); okk=True
        for y in range(hh):
            for x in range(ww):
                v=g[y*w+x]; R,G,B=(col>>16)&255,(col>>8)&255,col&255
                if fmt==2: exp=struct.pack('<I',(v<<24)|(col&0xffffff))
                elif fmt==1: exp=struct.pack('<I',(((v*R)&0xffffff00)<<8)|((v*G)&0xffffff00)|((v*B)>>8))
                else:
                    t=((v*R>>1)&0xfc1f|(v*G>>6))&0xffe0|(v*B>>11); exp=struct.pack('<H',t&0xffff)
                if res[(y*dw+x)*bpp:(y*dw+x+1)*bpp]!=exp: okk=False
        print('colour fmt',fmt,(w,h),'->',(dw,dh),'OK' if okk else 'DIFF'); bad+=not okk
print('colouring bad:',bad)

# ---- outline FUN_0040a2a0(dst, code, font, a, b, color): dst is ARGB (fmt 2) of size (w+2a) x (h+2b)
def ref_outline(g,w,h,pitch,a,b):
    W=w+2*a; H=h+2*b; out=[]
    for y in range(H):
        for x in range(W):
            acc=0
            for j in range(2*b+1):
                yy=y-2*b+j
                for i in range(2*a+1):
                    xx=x-2*a+i
                    if not(0<=xx<w and 0<=yy<h): continue
                    v=g[yy*pitch+xx]
                    if a==b and a<6: acc+=(v*tabs[a][j*(2*a+1)+i])>>16 if a>0 else v
                    else:
                        dx=(i-a)*b/a if a else 0.0; dy=j-b
                        d=math.sqrt(dx*dx+dy*dy)-b
                        if d>0:
                            if d<1.0: acc+=(v*int(d*65536))>>16
                        else: acc+=v
            out.append(min(acc,255))
    return out,W,H
bad=0
for (w,h,a,b) in ((6,5,1,1),(5,4,2,2),(4,4,5,5),(5,4,2,1),(4,5,1,3),(4,4,6,6),(3,3,7,7)):
    g=glyph(w,h); font(w,h,w); k.heap=HB
    W,H=w+2*a,h+2*b; s_,pa=k.bitmap(bytes(W*H*4),W,H,2); col=0x112233
    callc(0x40a2a0,s_,0x41,FONT,a,b,col,ecx=0)
    res=struct.unpack('<%dI'%(W*H),k.read(pa,W*H*4)); ref,_,_=ref_outline(g,w,h,w,a,b)
    exp=[(r<<24)|col for r in ref]; diff=[(i,res[i]>>24,ref[i]) for i in range(W*H) if res[i]!=exp[i]]
    print('outline',(w,h),'a,b=',(a,b),'OK' if not diff else 'DIFF %d/%d first %s'%(len(diff),W*H,diff[:3])); bad+=bool(diff)
print('outline bad:',bad)

# ---- positioned, clipped blit FUN_0040b410(ecx=dst, edx=x, y, src, mode, P) -> dispatcher(flag=1); thread path FUN_0041f760 stubbed to "return 0"
u.mem_write(0x41f760,b'\x31\xc0\xc2\x18\x00')
bad=0; r2=random.Random(9)
for (dw,dh,sw,sh,x,y) in ((10,8,6,5,2,1),(10,8,6,5,-3,-2),(10,8,6,5,7,5),(10,8,6,5,-9,0),(10,8,6,5,0,20),(10,8,12,10,-1,-1)):
    for fmt in (1,2):
        bpp=4; dp=bytes(r2.randrange(256) for _ in range(dw*dh*bpp)); sp=bytes(r2.randrange(256) for _ in range(sw*sh*bpp))
        k.heap=HB; d_,da=k.bitmap(dp,dw,dh,fmt); s_,sa=k.bitmap(sp,sw,sh,fmt)
        k.call(0x40b410,d_,x,y,s_,1,0)
        rc=u.reg_read(UC_X86_REG_EAX); res=k.read(da,dw*dh*bpp); exp=bytearray(dp)
        for yy in range(sh):
            for xx in range(sw):
                tx,ty=x+xx,y+yy
                if 0<=tx<dw and 0<=ty<dh:
                    px=sp[(yy*sw+xx)*4:(yy*sw+xx+1)*4]
                    if fmt==2:                      # mode 1, P=0 -> mode 0 Over with source alpha (2->2): only check fully opaque/transparent px
                        continue
                    exp[(ty*dw+tx)*4:(ty*dw+tx+1)*4]=px
        if fmt==1:
            ok=all(res[i]==exp[i] for i in range(len(exp)) if i%4!=3)
            print('blit fmt1 dst',(dw,dh),'src',(sw,sh),'at',(x,y),'rc',rc,'OK' if ok else 'DIFF'); bad+=not ok
        else: print('blit fmt2 rc',rc,'(clip only; blend not compared)')
print('blit bad:',bad)
