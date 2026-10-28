# Independent BF_Movie (.bm) entropy decoder (no original code). Verified against bm_capture.py output.
import struct
ZZ=[0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63]
u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
def varint(b,pos):
    v=0;s=0
    while True:
        c=b[pos];pos+=1;v|=(c&0x7f)<<(s&31);s+=7
        if c<0x80:return v,pos
def build_tree(freq):
    """FUN_004b15a0, including its tie-breaking quirk. nodes: [active,freq,label,parent,c0,c1]; returns (nodes,root,nleaf)"""
    n=len(freq);N=2*n-1
    nd=[[0,0,0,-1,-1,-1] for _ in range(N)]
    tot=0
    for i,f in enumerate(freq):
        nd[i][1]=f
        if f:nd[i][0]=1;tot+=f
    cnt=n
    while cnt<N:
        pick=[-1,-1];s=0
        for t in range(2):
            c=-1
            for i in range(n):          # first active among the first n (leaf-count) slots? see below
                pass
            # original scans indices < cnt (current node count)
            for i in range(cnt):
                if nd[i][0]:c=i;break
            if c>=0:
                for j in range(t+1,cnt):
                    if nd[j][0] and nd[j][1]<nd[c][1]:c=j
            pick[t]=c
            if c<0:break
            nd[c][0]=0;nd[c][3]=cnt;nd[c][2]=t;s+=nd[c][1]
        nd[cnt]=[1,s,0xff,-1,pick[0],pick[1]]
        if tot<=s:return nd,cnt,n
        cnt+=1
    return nd,cnt,n
class Bits:
    def __init__(s,b,pos):s.b=b;s.p=pos;s.i=0
    def bit(s):
        v=(s.b[s.p]>>(7-s.i))&1;s.i+=1
        if s.i==8:s.i=0;s.p+=1
        return v
    def bits(s,n):
        v=0
        for _ in range(n):v=(v<<1)|s.bit()
        return v
    def sym(s,tree):
        nd,root,nl=tree;x=root
        while x>=nl:
            x=nd[x][5] if s.bit() else nd[x][4]
        return x
    def end(s):return s.p+(1 if s.i else 0)
def sext(v,n):
    if n and not (v>>(n-1))&1:v=(v|(-1<<n))+1
    return v
def decode_row(b,pos,count,dct,act):
    """decode `count` coefficients (count%64==0) starting at pos; returns (list of int16, end_pos_of_dc, end_pos)"""
    co=[0]*count;r=Bits(b,pos);prev=0
    for blk in range(0,count,64):
        cat=r.sym(dct);v=sext(r.bits(cat),cat) if cat else 0
        prev=(prev+v)&0xffff;co[blk]=prev-0x10000 if prev&0x8000 else prev
    acpos=r.end();r=Bits(b,acpos)
    for blk in range(0,count,64):
        k=1
        while k<64:
            s=r.sym(act)
            if s==0:break
            if s==0xf:k+=16;continue
            k+=s&15;size=(s>>4)&15
            v=sext(r.bits(size),size) if size else 0
            if k<64:co[blk+ZZ[k]]=v
            k+=1
    return co,acpos,r.end()
def parse_frame(d,fo):
    """frame at file offset fo; returns dict with trees and row table."""
    h=d[:0xc0] if fo is None else d
    return None

# ---------------- full-frame decoder ----------------
import numpy as np
f32=np.float32
_S=None
def aan_scale():
    s=[1.0,1.387039845,1.306562965,1.175875602,1.0,0.785694958,0.541196100,0.275899379]
    return np.array([[a*b for b in s] for a in s],f32)
def _p1(v):
    t0,t1,t2,t3,t4,t5,t6,t7=v
    tmp10=t0+t4;tmp11=t0-t4;tmp13=t2+t6;tmp12=(t2-t6)*f32(1.4142135381698608)-tmp13
    a0=tmp10+tmp13;a3=tmp10-tmp13;a1=tmp11+tmp12;a2=tmp11-tmp12
    z13=t5+t3;z10=t5-t3;z11=t1+t7;z12=t1-t7
    b7=z11+z13;tm11=(z11-z13)*f32(1.4142135381698608)
    z5=(z10+z12)*f32(1.8477590084075928);tm10=z12*f32(1.0823922157287598)-z5;tm12=z10*f32(-2.613126039505005)+z5
    b6=tm12-b7;b5=tm11-b6;b4=tm10+b5
    return [a0+b7,a1+b6,a2+b5,a3-b4,a3+b4,a2-b5,a1-b6,a0-b7]
def _idct(blocks,q):
    x=blocks*q;y=np.stack(_p1([x[:,i,:] for i in range(8)]),1);return np.stack(_p1([y[:,:,j] for j in range(8)]),2)
def _plane(co,q):
    I=np.trunc(_idct(co,q)).astype(np.int32)>>3;return np.clip(I+128,0,255).astype(f32)
def _rgb(Y,Cb,Cr):
    R=np.floor(Y+f32(1.402)*(Cr-128)+f32(.5));G=np.floor(Y-f32(.34414)*(Cb-128)-f32(.71414)*(Cr-128)+f32(.5));B=np.floor(Y+f32(1.772)*(Cb-128)+f32(.5))
    return np.clip(np.stack([B,G,R],-1),0,255).astype(np.uint8)
class BM:
    def __init__(s,d):
        assert d[:16]==b'BF_Movie_______\0'
        s.d=d;s.ver,s.w,s.h,s.bits,s.fmt,s.rate,s.n=struct.unpack_from('<7I',d,0x10)
        sc=aan_scale()
        s.q1=np.array(list(d[0x40:0x80]),f32).reshape(8,8)*sc;s.q2=np.array(list(d[0x80:0xc0]),f32).reshape(8,8)*sc
        s.w8=(s.w+7)//8;s.h8=(s.h+7)//8;s.mask=(s.w8+7)//8
        s.img=np.zeros((s.h,s.w,4),np.uint8)       # BGRA, persists between frames
        s.alpha_ok=(s.bits==32)
    def frame(s,i):
        d=s.d;fo=B_u32(d,0xc0+4*i);fr=d[fo:];pos=0;f1=[];f2=[]
        for _ in range(16):v,pos=varint(fr,pos);f1.append(v)
        for _ in range(176):v,pos=varint(fr,pos);f2.append(v)
        dct=build_tree(f1);act=build_tree(f2)
        offs=[B_u32(fr,pos+4*k) for k in range(s.h8+1)]
        for r in range(s.h8):
            p=offs[r];m=fr[p:p+s.mask];cnt,q=varint(fr,p+s.mask)
            if not cnt:continue
            present=[b for b in range(s.w8) if m[b>>3]>>(b&7)&1];n=len(present)
            co,_,_=decode_row(fr,q,cnt,dct,act);co=np.array(co,f32).reshape(-1,8,8)
            px=_rgb(_plane(co[:n],s.q1),_plane(co[n:2*n],s.q2),_plane(co[2*n:3*n],s.q2))
            for j,b in enumerate(present):
                x0=b*8;wd=min(8,s.w-x0);hh=min(8,s.h-r*8)
                s.img[r*8:r*8+hh,x0:x0+wd,:3]=px[j][:hh,:wd]
        if s.bits==32:s._alpha(fr,offs[s.h8])
        return s.img
    def _alpha(s,fr,ao):
        mode=B_u32(fr,ao);ao+=4
        if s.ver==0x10000:mode=1;ao-=4
        a=s.img.reshape(-1)         # byte view, alpha at +3
        if mode==1:
            dst=3;end=s.w*s.h*4;stride=s.w*4;flags=ao;j=ao+1;bit=0
            while dst<end:
                if not (fr[flags]>>bit)&1:
                    a[dst]=fr[j];j+=1;dst+=4
                else:
                    t=fr[j]|fr[j+1]<<8;j+=2
                    dx=t&0x3f;dx-=64 if dx>31 else 0
                    dy=(t>>6)&7;dy-=8 if dy else 0
                    ln=(t>>9)+3
                    for _ in range(ln):
                        a[dst]=a[dst+dy*stride+dx*4];dst+=4
                bit+=1
                if bit>7:bit=0;flags=j;j+=1
        elif mode==2:
            size=B_u32(fr,ao);fq=[];pos=ao+4
            for _ in range(256):v,pos=varint(fr,pos);fq.append(v)
            tree=build_tree(fq);rd=Bits(fr,pos);buf=bytearray(size)
            for k in range(size):buf[k]=rd.sym(tree)
            nb=s.w8*s.h8;off=(nb+7)//8;bi=0
            for by in range(s.h8):
                for bx in range(s.w8):
                    if buf[bi>>3]>>(bi&7)&1:
                        wd=min(8,s.w-bx*8);hh=min(8,s.h-by*8)
                        for yy in range(hh):
                            for xx in range(wd):
                                s.img[by*8+yy,bx*8+xx,3]=buf[off];off+=1
                    bi+=1
        else:raise ValueError('alpha mode %d'%mode)
def B_u32(b,o):return struct.unpack_from('<I',b,o)[0]
