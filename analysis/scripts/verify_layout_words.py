# Latin-word wrap: exe layout vs Python model, compared by node coordinates. usage: .venvScriptspython.exe scriptserify_layout_words.py
import sys,random; sys.argv=['x']; sys.path.insert(0,'scripts')
import io, contextlib
with contextlib.redirect_stdout(io.StringIO()): import verify_layout as V
V.cfg(); rnd=random.Random(11)
def isw(c):
    o=ord(c); return 0x20<o<0x3c or 0x3c<o<0x7f
def ref(text,W,ks,size=24):
    wd=lambda c: size//2 if (ord(c)<0x80 or 0xff60<ord(c)<0xffa0) else size
    IND=wd(text[0]) if (ks and text[0] in '"「　（『') else 0; margin=size if ks else 0; L=W-margin+1; L0=W+1; x=0;y=0;n=len(text);out=[];pend=0
    for i,c in enumerate(text):
        wc=wd(c)
        if ks and c==' ' and x+wc>L:
            if x>IND: x=IND;y+=30
            continue
        if pend>0:
            pend-=1; wrap = x+wc>L and not (ks and c in V.SIMPLE and x+wc<=L0)
            if wrap and x>IND: x=IND;y+=30
            out.append((x,y)); x+=wc; continue
        if isw(c):
            j=i
            while j<n and isw(text[j]): j+=1
            w=text[i:j]
            while len(w)>1 and w[-1] in '!"),.?]}': w=w[:-1]
            wA=sum(map(wd,w)); wB=0; run=[]
            if ks:
                j2=i+len(w)
                while j2<n and text[j2] in V.NOLS: run.append(text[j2]); j2+=1
                last=-1
                for q,ch in enumerate(run):
                    if ch not in V.SIMPLE: last=q
                wA+=sum(map(wd,run[:last+1])); wB=sum(map(wd,run[last+1:]))
            if x+wA>L: wrap = not (ks and c in V.SIMPLE and len(w)==1 and x+wA<=L0)
            else: wrap = wB>0 and L0<wB+wA+x
            if wrap and x>IND: x=IND;y+=30
            pend = len(w)-1+len(run)
            out.append((x,y)); x+=wc; continue
        wrap = x+wc>L and not (ks and c in V.SIMPLE and x+wc<=L0)
        if wrap and x>IND: x=IND;y+=30
        out.append((x,y)); x+=wc
    return out
words=['world,','test.','"hi"','(a)','end!?','ok)','Hello','world','this','is','a','test','abc','defghijkl','x','The','quick','brown','fox']
bad=tot=0; byks={}
for it in range(1500):
    ks=rnd.choice((0,1)); W=rnd.randrange(150,400)
    t=' '.join(rnd.choice(words) for _ in range(rnd.randrange(3,14)))
    nd,n,c=V.layout(t,rect=(0,0,W,200),leading=30,kinsoku=ks); got=[(a['x'],a['y']) for a in nd]
    e=ref(t,W,ks); tot+=1
    if got!=e:
        bad+=1; byks[ks]=byks.get(ks,0)+1
        if bad<=4: k0=next((q for q,(a,b) in enumerate(zip(got,e)) if a!=b),min(len(got),len(e))); print('DIFF ks',ks,'W',W,repr(t),'node',k0,len(got),len(e),'exe',got[k0:k0+2],'ref',e[k0:k0+2])
print('cases',tot,'bad',bad,byks)
