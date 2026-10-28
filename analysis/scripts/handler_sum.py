# usage: handler_sum.py addr... -- per handler: pop/push counts, called functions, referenced strings (cp932)
import sys,re,struct
sys.path.insert(0,'scripts')
d=open(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0];n=struct.unpack_from('<H',d,pe+6)[0];opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
def off(va):
    r=va-0x400000
    for vs,a,rs,ra in secs:
        if a<=r<a+max(vs,rs):return ra+r-a
def cstr(va):
    o=off(va)
    if o is None:return None
    e=d.index(b'\0',o);return d[o:e].decode('cp932','replace')[:70]
L=open('out/ac2_ML.c',encoding='utf8',errors='replace').read().split('\n')
idx={}
for i,l in enumerate(L):
    m=re.match(r'// ==== ([0-9a-f]{8}) ',l)
    if m:idx[m.group(1)]=i
keys=sorted(idx.values())
def body(a):
    s=idx[a];e=next((k for k in keys if k>s),len(L));return '\n'.join(L[s:e])
for a in sys.argv[1:]:
    a=a.lower();
    if a not in idx:print(a,'NOT DECOMPILED');continue
    b=body(a);pops=len(re.findall(r'FUN_0044d2a0|FUN_0049d1f0|FUN_0044d1e0',b));ptr=len(re.findall(r'FUN_0049d1f0',b));push=len(re.findall(r'FUN_0044d2c0',b))
    calls=[c for c in dict.fromkeys(re.findall(r'(FUN_[0-9a-f]{8})\(',b)) if c[4:] not in (a,'0044d2a0','0049d1f0','0044d2c0','00471e50','00402010')]
    strs=[]
    for m in dict.fromkeys(re.findall(r'DAT_(005[0-9a-f]{5})',b)):
        s=cstr(int(m,16))
        if s and len(s)>2 and sum(ord(c)>127 or c.isalpha() for c in s)>2:strs.append(s)
    print(f'{a}: pop={pops} (ptr={ptr}) push={push} calls={",".join(c[4:] for c in calls[:6])} str={strs[:4]}')
