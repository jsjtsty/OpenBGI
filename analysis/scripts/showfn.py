# usage: showfn.py FUN_xxxxxxxx|hexaddr ... -- print decompiled bodies from out/ac2_ML.c (compact: blank lines dropped)
import sys,re
L=open('out/ac2_ML.c',encoding='utf8',errors='replace').read().split('\n')
idx={}
for i,l in enumerate(L):
    m=re.match(r'// ==== ([0-9a-f]{8}) ',l)
    if m:idx[m.group(1)]=i
keys=sorted(idx.values())
for a in sys.argv[1:]:
    a=a.lower().replace('fun_','')
    if a not in idx:print('//',a,'not found');continue
    s=idx[a];e=next((k for k in keys if k>s),len(L))
    print('\n'.join(x for x in L[s:e] if x.strip() and not re.match(r'\s*(undefined\d?|int|uint|void \*|char) +\w+;\s*$',x)))
