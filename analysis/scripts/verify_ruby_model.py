# Check the ruby placement model (FUN_0043e910) against the real function run in Unicorn (fake 7px ruby glyph: float width 7, advance 8).
import verify_ruby as R
from verify_ruby import *
V.cfg()
def rnd(v): return int(math.floor(v+0.5))
def model(x,baseW,baseCnt,n,adv=8,gw=7,t0=0,tick=25):
    totalW=adv*n; L=(baseCnt*tick)//n
    if baseW<totalW:
        s=x+((baseW-totalW)>>1); xs=[s+adv*i for i in range(n)]
    else:
        step=(baseW+1)/n; xs=[rnd(x+(step-gw)/2+i*step) for i in range(n)]
    return [(t0+(L>>1)+L*i,xs[i]) for i in range(n)]
import math
bad=tot=0
for baseCnt in range(1,8):
    for n in range(1,9):
        text='<r '+'か'*n+'>'+'漢'*baseCnt+'</r>あ'
        nl,nodes,before,rt=ruby_nodes(text)
        got=[(s32(n_+4),s32(n_+0x10)) for n_ in nodes if r32(n_+0x5c)==2]
        exp=model(16,24*baseCnt,baseCnt,n); tot+=1
        if got!=exp: bad+=1; print('DIFF',baseCnt,n,'\n got',got,'\n exp',exp)
print('cases',tot,'mismatch',bad)
