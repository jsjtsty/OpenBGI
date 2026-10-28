# Run the exe's real text layout engine FUN_0043ba90 (plus the real CFontDx constructor and glyph source) in Unicorn with faked GDI.
# usage: .venv\Scripts\python.exe scripts\verify_layout.py [-d]    (-d: keep a ring buffer of executed addresses to localise crashes)
import sys, struct, math, collections, emu, winstub
from unicorn import *; from unicorn.x86_const import *
k=emu.Kernel(); u=k.u; W=winstub.Win(k)
DBG='-d' in sys.argv
def rd(a,n): return bytes(u.mem_read(a,n))
def r32(a): return struct.unpack('<I',rd(a,4))[0]
def s32(a): return struct.unpack('<i',rd(a,4))[0]
def hook(uc,addr,size,_):                                  # cdecl malloc/new -> bump allocator, free/delete -> no-op
    esp=uc.reg_read(UC_X86_REG_ESP); ret=r32(esp)
    if addr in (0x4cf54b,0x4cf50d):
        n=r32(esp+4); a=k.heap; k.heap=(a+max(n,1)+63)&~15; uc.mem_write(a,bytes(max(n,1))); uc.reg_write(UC_X86_REG_EAX,a)
    uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)
for a in (0x4cf54b,0x4cf50d,0x4cf53d,0x4e0964): u.hook_add(UC_HOOK_CODE,hook,None,a,a)
ring=collections.deque(maxlen=40)
if DBG: u.hook_add(UC_HOOK_CODE,lambda uc,a,s,_:ring.append(a),None,emu.BASE,emu.BASE+emu.IMG)

# ---- fake GDI: one DC, glyph shape depends only on (code, font px) so the result is deterministic
GDI=dict(px=0,dibw=0,dibh=0,dib=0)
def cstr(a):
    b=b''
    while True:
        c=rd(a,1)
        if c==b'\0': return b
        b+=c; a+=1
def wstr(a):
    s=b''
    while True:
        c=rd(a,2)
        if c==b'\0\0': return s.decode('utf-16le')
        s+=c; a+=2
@W.api('MultiByteToWideChar',6)
def _(a):
    cp,fl,src,cb,dst,cch=a; raw=cstr(src)+(b'\0' if cb==0xffffffff else b'') if cb==0xffffffff else rd(src,cb)
    s=raw.decode('utf-8' if cp==65001 else 'cp932',errors='replace'); w=(s if cb!=0xffffffff else s).encode('utf-16le')
    if cb==0xffffffff and not w.endswith(b'\0\0'): w+=b'\0\0'
    n=len(w)//2
    if cch==0: return n
    if n>cch: return 0
    u.mem_write(dst,w); return n
@W.api('WideCharToMultiByte',8)
def _(a):
    cp,fl,src,cch,dst,cb,d1,d2=a
    s=wstr(src) if cch==0xffffffff else rd(src,cch*2).decode('utf-16le')
    b=s.encode('utf-8' if cp==65001 else 'cp932',errors='replace')+(b'\0' if cch==0xffffffff else b'')
    if cb==0: return len(b)
    if len(b)>cb: return 0
    u.mem_write(dst,b); return len(b)
W.api('CreateFontW',14)(lambda a: (GDI.update(px=abs(struct.unpack('<i',struct.pack('<I',a[0]))[0])),0x1001)[1])
W.api('CreateCompatibleDC',1)(lambda a: 0x2001)
@W.api('CreateDIBSection',6)
def _(a):
    hdc,bmi,usage,ppv,hs,off=a; w=s32(bmi+4); h=-s32(bmi+8); GDI.update(dibw=w,dibh=h)
    buf=k.alloc(bytes(w*h)); GDI['dib']=buf; u.mem_write(ppv,struct.pack('<I',buf)); return 0x3001
W.api('SelectObject',2)(lambda a: 0x4001)
W.api('DeleteObject',1)(lambda a: 1); W.api('DeleteDC',1)(lambda a: 1)
W.api('GetDC',1)(lambda a: 0x2002); W.api('ReleaseDC',2)(lambda a: 1)
W.api('EnumFontFamiliesExW',5)(lambda a: 0)                # callback never invoked: charset lookup falls back to the first candidate
@W.api('GetObjectA',3)
def _(a):
    h,cb,out=a
    if h==0x3001: u.mem_write(out,struct.pack('<iiiiHH',0,GDI['dibw'],GDI['dibh'],GDI['dibw'],1,8)[:cb])   # BITMAP: bmWidthBytes at +0xc
    else: u.mem_write(out,bytes(cb))
    return cb
@W.api('GetOutlineTextMetricsW',3)
def _(a):
    hdc,cb,p=a; size=0x200
    if p==0: return size
    px=GDI['px']; u.mem_write(p,bytes(size))
    tm=struct.pack('<iiiiiii',px,0,px*4//5,px//5,0,px//2,px)   # tmHeight, Ascent, Descent... (TEXTMETRICW: Height,Ascent,Descent,IntLead,ExtLead,AveCharWidth,MaxCharWidth)
    u.mem_write(p+4,struct.pack('<i',px)+struct.pack('<i',px*4//5)+struct.pack('<i',px//5)+struct.pack('<i',0)+struct.pack('<i',0)+struct.pack('<i',px//2)+struct.pack('<i',px))
    u.mem_write(p+0x40,b'\0'); return 1
@W.api('GetTextMetricsA',2)
def _(a):
    px=GDI['px']; u.mem_write(a[1],struct.pack('<7i',px,px*4//5,px//5,0,0,px//2,px)); return 1
SHAPELOG=[]
def shape(code,px):
    SHAPELOG.append((code,px))                                        # ink box (x0,y0,x1,y1) in a px*? cell; full width for CJK, half for ASCII
    wide=code>=0x80
    cw=px if wide else px//2
    if code in (0x20,0x3000): return cw,None
    return cw,(1,1,max(2,cw-2),max(2,px-2))
@W.api('GetGlyphOutlineW',7)
def _(a):
    hdc,code,fmt,gm,cb,buf,mat=a; px=GDI['px']; cw,box=shape(code,px)
    if box is None: u.mem_write(gm,struct.pack('<IIiiHH',0,0,0,0,cw,0)); return 0
    x0,y0,x1,y1=box; bw=x1-x0+1; bh=y1-y0+1; pitch=(bw+3)&~3
    u.mem_write(gm,struct.pack('<IIiiHHH',bw,bh,x0,px*4//5-y0,cw,0,0))
    if cb==0: return pitch*bh
    lv={4:4,5:16,6:64}[fmt]; g=bytearray(pitch*bh)
    for y in range(bh):
        for x in range(bw): g[y*pitch+x]=(x*7+y*13+code)%(lv+1)
    u.mem_write(buf,bytes(g)); return pitch*bh
@W.api('GetCharABCWidthsFloatW',4)
def _(a):
    cw,_b=shape(a[1],GDI['px']); u.mem_write(a[3],struct.pack('<fff',0.0,float(cw),0.0)); return 1
@W.api('GetTextExtentPoint32W',4)
def _(a):
    cw,_b=shape(r32(a[1])&0xffff,GDI['px']); u.mem_write(a[3],struct.pack('<ii',cw,GDI['px'])); return 1

# ---- engine state
k.call(0x408890,0,0); HB=k.heap
def gstr(s): return k.alloc(s+b'\0')
fontobj=k.alloc(bytes(0x128)); k.call(0x432cc0,fontobj,0)
name=gstr('MS Gothic'.encode()); FS=24
try:
    k.call(0x4333c0,fontobj,0, name,FS,100,0,0,0,0x100,1)
    rc=u.reg_read(UC_X86_REG_EAX)
except UcError as e:
    print('ctor fault',e,'EIP',hex(u.reg_read(UC_X86_REG_EIP)),[hex(x) for x in list(ring)[-12:]],[n for n,_ in W.log[-5:]]); raise
print('CFontDx ctor rc',hex(rc),'API calls',[n for n,_ in W.log])

# ---- TextOutW (supersampled path, the exe's default: DAT_0052eb88=1): draw the glyph ink box into the DIB as nonzero samples
@W.api('TextOutW',5)
def _(a):
    hdc,x,y,s,n=a; code=r32(s)&0xffff; cw,box=shape(code,GDI['px']); g=bytearray(GDI['dibw']*GDI['dibh'])
    if box:
        x0,y0,x1,y1=box
        for yy in range(y0,min(y1+1,GDI['dibh'])):
            for xx in range(x0,min(x1+1,GDI['dibw'])): g[yy*GDI['dibw']+xx]=1
    u.mem_write(GDI['dib'],bytes(g)); return 1
def gdi_px(): return GDI['px']

# ---- font manager as the engine sees it: DAT_0053605c -> M1; M1+4 -> M2; M2+0xf8 -> entry list (id, 29-dword descriptor, next)
def mkfont(fid,fs,pct=100,bold=0,italic=0):
    obj=k.alloc(bytes(0x128)); k.call(0x432cc0,obj,0)
    nm=gstr(b'MS Gothic'); k.call(0x4333c0,obj,0,nm,fs,pct,bold,italic,0,0x100,1)
    assert u.reg_read(UC_X86_REG_EAX)==0, 'font ctor failed'
    desc=bytearray(0x74); desc[:9]=b'MS Gothic'; struct.pack_into('<IIIII',desc,0x60,fs,pct,bold,italic,obj)
    return obj,desc
def install_fonts(fonts):
    ents=[]; nxt=0
    for fid,(obj,desc) in reversed(list(fonts.items())):
        e=k.alloc(struct.pack('<I',fid)+bytes(desc)+struct.pack('<I',nxt)); nxt=e
    M2=k.alloc(bytes(0x400)); u.mem_write(M2+0xf8,struct.pack("<I",nxt)); u.mem_write(M2+0x78,struct.pack("<I",0x100))
    M1=k.alloc(struct.pack('<II',0,M2)); u.mem_write(0x53605c,struct.pack('<I',M1))
sent=k.alloc(bytes(0x40)); u.mem_write(sent,struct.pack('<II',sent,sent)); u.mem_write(0x597288,struct.pack('<I',sent))   # DAT_00597288: std::list sentinel (CRT static init is not run)
fonts={1:mkfont(1,24)}; install_fonts(fonts)
fo=fonts[1][0]
print('font obj: size %d pct %d  cell %dx%d pitch %d  dib %dx%d  asc-off %d  spacing %d,%d'%(s32(fo+8),s32(fo+0xc),s32(fo+0x50),s32(fo+0x54),s32(fo+0x5c),s32(fo+0x64),s32(fo+0x68),s32(fo+0x24),s32(fo+0x40),s32(fo+0x44)))

# ---- glyph fetch through the real FUN_004345c0 / FUN_00433ba0
def glyph(code):
    out=k.alloc(bytes(0x30)); k.call(0x4345c0,fo,0,out,code)
    f=struct.unpack('<IIIiiii3fI',rd(out,0x2c)); return f
for c in (0x61,0x3042,0x20,0x3000):
    f=glyph(c); print('glyph %04x: wide=%d bbox(l,t,r,b)=%s A,B,C=%s flag=%d'%(c,f[1],f[3:7],tuple(round(x,3) for x in f[7:10]),f[10]))

# ---- 4x4 supersample -> alpha: count*255>>4 (DAT_0052eb90=4, DAT_00535f00=0)
def check_alpha(code):
    out=k.alloc(bytes(0x30)); k.call(0x4345c0,fo,0,out,code); bm=r32(out+8); pitch=s32(fo+0x5c); W_,H_=s32(fo+0x50),s32(fo+0x54)
    g=rd(bm,pitch*H_); px=GDI['px']; cw,box=shape(code,px); bad=0
    for y in range(H_):
        for x in range(W_):
            n=0
            if box:
                x0,y0,x1,y1=box
                for yy in range(y*4,y*4+4):
                    for xx in range(x*4,x*4+4):
                        if x0<=xx<=x1 and y0<=yy<=y1: n+=1
            if g[y*pitch+x]!=(n*255>>4): bad+=1
    return bad
for c in (0x61,0x3042,0x20): print('alpha map vs ref, code %04x: mismatches %d'%(c,check_alpha(c)))

# ---- layout: FUN_0043ba90 (fastcall ecx=nodelist(100B), edx=&lines; stack: text, ruby?, rubytbl, cursor*, rect*, extra_leading, fontid, vertical, kinsoku, color, effect*)
def layout(text,rect=(0,0,480,200),cursor=(0,0),ruby=0,leading=0,vertical=0,kinsoku=0,color=0xffffff,effect=(0,0,0,0,0),fid=1):
    k.heap=HB2
    nl=k.alloc(bytes(0x64)); lines=k.alloc(bytes(4)); t=gstr(text.encode('utf-8')); rt=k.alloc(bytes(0x80))
    cur=k.alloc(struct.pack('<ii',*cursor)); rc=k.alloc(struct.pack('<4i',*rect)); ef=k.alloc(struct.pack('<5i',*effect))
    k.call(0x43ba90,nl,lines,t,ruby,rt,cur,rc,leading,fid,vertical,kinsoku,color,ef)
    nodes=[]; n=r32(nl+0x60)
    while n:
        nodes.append(dict(x=s32(n+0x10),y=s32(n+0x14),x2=s32(n+0x18),y2=s32(n+0x1c),sz=s32(n+0x20),off=s32(n+4)))
        n=r32(n+0x60)
    return nodes,s32(lines),struct.unpack('<ii',rd(cur,8))
HB2=k.heap
def show(text,**kw):
    try: nodes,nl_,cur=layout(text,**kw)
    except UcError as e:
        print('FAULT',repr(text),e,'EIP',hex(u.reg_read(UC_X86_REG_EIP)),[hex(x) for x in list(ring)[-8:]],[n for n,_ in W.log[-3:]]); return None
    rows={}
    for n in nodes: rows.setdefault(n['y'],[]).append(n['x'])
    print('%-28r lines=%d cursor=%s nodes=%d'%(text,nl_,cur,len(nodes)),{y:xs for y,xs in rows.items()}, 'sz',sorted({n['sz'] for n in nodes}))
    return nodes

def lines_of(text,**kw):                                   # visible chars per output line (tags/prefix controls stripped by the caller)
    nodes,nl_,cur=layout(text,**kw); vis=[c for c in text]; out={}
    for n,c in zip(nodes,[c for c in text if ord(c)>=0x20 and ord(c)!=0x0a]): out.setdefault(n['y'],[]).append((c,n['x']))
    return out,nl_,cur
def cfg(hang=1,pct=33,key_flags=((0x80000002,1),(0x80000000,1))):    # what sysprg/environ + ipl set at boot (91:98, 91:9A)
    k.call(0x43a4a0,25,0,0,pct,16,hang)
    for kk,v in key_flags: k.call(0x43a500,kk,v)
NOLS="\"',.:;?!ﾞﾟ･，．、。：；？！”゛゜‐]})）〕］｝〉≫》」』】ヽヾゝゞ々・‥…━―─ー～♪ぁぃぅぇぉっゃゅょァィゥェォッャュョをヲ　"
SIMPLE="\"',.?!ﾞﾟ，．、。？！”゛゜]})）〕］｝〉≫》」』】ヽヾゝゞ々・‥…━―─ー～♪ぁぃぅぇぉっゃゅょァィゥェォッャュョ　"
OPEN="[{(（〔［｛〈≪《「『【“"
def ref_layout(text,W,size=24,kinsoku=0,leading=30,hang=1):
    """reference model of the horizontal greedy wrap (plain text): returns {y: string}.
    Each char is a 'base'; with kinsoku the base looks ahead at the run of no-line-start chars (NOLS) behind it. A = base + run up to the last
    non-SIMPLE char (must stay on the line), B = trailing SIMPLE chars (may hang into the right margin). If the group is accepted the run
    chars are then placed one by one (pending>0); if the base had to wrap, the group is dropped when A fits a fresh line."""
    wd=lambda c: size//2 if (ord(c)<0x80 or 0xff60<ord(c)<0xffa0) else size      # FUN_004a4b40: ASCII and half-width kana are narrow
    margin=size if (kinsoku and hang) else 0
    L=W-margin+1; L0=W+1; x=0; y=0; n=len(text); out={}; pending=0
    for i,c in enumerate(text):
        wc=wd(c)
        if pending>0 or not kinsoku:
            pending=max(pending-1,0); wA=wc; wB=0; run=[]
            wrap = x+wA>L and not (kinsoku and c in SIMPLE and x+wA<=L0)
        else:
            j=i+1; run=[]
            while j<n and text[j] in NOLS: run.append(text[j]); j+=1
            last=-1
            for q,ch in enumerate(run):
                if ch not in SIMPLE: last=q
            A=[c]+run[:last+1]; B=run[last+1:]; wA=sum(map(wd,A)); wB=sum(map(wd,B))
            if c in OPEN and not run and i+1<n: wA+=wd(text[i+1])
            if x+wA>L: wrap = not (c in SIMPLE and x+wA<=L0)
            else: wrap = wB>0 and L0<wB+wA+x
            if wrap:
                x=0; y+=leading
                pending = len(run) if x+wA>L else 0
                x=0
            else: pending=len(run)
            if c in OPEN and not run and i+1<n: wA-=wd(text[i+1])
            out.setdefault(y,[]).append(c); x+=wc; continue
        if wrap and x>0: x=0; y+=leading
        out.setdefault(y,[]).append(c); x+=wc
    return {yy:''.join(v) for yy,v in out.items()}
def check_ggo(mode):
    """DAT_0052eb88=0 path (GetGlyphOutlineW, set by the start-up self-test fallback FUN_00433200): alpha = v*255>>shift, shift 2/4/6 for eb8c 0/1/2"""
    u.mem_write(0x52eb8c,struct.pack('<I',mode)); k.call(0x433200,0,0)
    obj,_d=mkfont(9,24); fmt,shift={0:(4,2),1:(5,4),2:(6,6)}[mode]; bad=0; seen=set()
    for code in (0x61,0x3042,0x4e9c):
        out=k.alloc(bytes(0x30)); k.call(0x4345c0,obj,0,out,code); bm=r32(out+8); pitch=s32(obj+0x5c); H_=s32(obj+0x54); g=rd(bm,pitch*H_)
        cw,box=shape(code,24); x0,y0,x1,y1=box; bw=x1-x0+1; bh=y1-y0+1; sp=(bw+3)&~3; lv={4:4,5:16,6:64}[fmt]
        for y in range(H_):
            for x in range(pitch):
                inside = 0<=x<bw and y0<=y<y0+bh      # glyph pasted at (col 0, row = tmAscent-origin.y = y0)
                exp = ((((x*7+(y-y0)*13+code)%(lv+1))*255)>>shift) if inside else 0
                seen.add(g[y*pitch+x]); bad+= g[y*pitch+x]!=exp
    return bad,sorted(seen)[:8]
if __name__=='__main__':
    if '-g' in sys.argv:
        for m in (0,1,2): print('GGO path, eb8c',m,'mismatches/levels',check_ggo(m))
    if '-r' in sys.argv:
        import random; rnd=random.Random(5); cfg(); N=[c for c in 'あいうえおかきくけこ漢字語文']; P=[c for c in SIMPLE.replace('　','')+'を‐' if ord(c)>=0x80]; O=[c for c in OPEN if ord(c)>=0x80]
        bad=0; tot=0; WMIN=int(sys.argv[sys.argv.index('-r')+2]) if len(sys.argv)>sys.argv.index('-r')+2 else 80
        for it in range(int(sys.argv[sys.argv.index('-r')+1]) if len(sys.argv)>sys.argv.index('-r')+1 else 200):
            t=rnd.choice(N)+''.join(rnd.choice(N+N+P+O+N) for _ in range(rnd.randrange(4,30)))
            ks=rnd.choice((0,1)); W=rnd.randrange(WMIN,WMIN+250)
            if ks and any(a in OPEN and b in NOLS for a,b in zip(t,t[1:])): continue
            o,n_,c=lines_of(t,rect=(0,0,W,200),leading=30,kinsoku=ks); got={y:''.join(ch for ch,_ in v) for y,v in o.items()}
            exp=ref_layout(t,W,kinsoku=ks); tot+=1
            if got!=exp:
                bad+=1
                if bad<=8: print('DIFF ks',ks,'W',W,repr(t),'\n  exe',got,'\n  ref',exp)
        print('random cases',tot,'mismatch',bad)
    if '-p' in sys.argv:
        cfg()
        for W in (95,96,97,98,119,120):
            for ks in (0,1):
                o,n,c=lines_of('あいうえおかきくけこ',rect=(0,0,W,200),leading=30,kinsoku=ks); print('W',W,'kinsoku',ks,{y:''.join(ch for ch,_ in v) for y,v in o.items()})
        for ks in (0,1):
            for t in ('あいう。え','あいうえ。お','あい、う','あいう」え','あい「うえ','あいう！！え','abc defghijkl mno'):
                o,n,c=lines_of(t,rect=(0,0,96,200),leading=30,kinsoku=ks); print('ks',ks,repr(t),{y:''.join(ch for ch,_ in v) for y,v in o.items()},'cur',c)
