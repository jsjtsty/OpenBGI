# usage: py blend_rounding.py -- quantifies SIMD-vs-scalar rounding differences of the blend kernels (static models of the disassembly)
# (1) mode 0/1 BGRA->BGRA (40c480/40cda0): integer div (scalar) vs float rcpps (SIMD); rcpps modelled as exact 1/x with relative error +-1.5*2^-12
# (2) mode 7 BGRA->8bit (418770): scalar d*(0xff00-sa*P)>>16 vs SIMD d*(256-(sa*P>>8))>>8
import math
E_=1.5*2**-12
mw=0; mc=0; wcount=0; tot=0
for sa in range(2,256):
    for da in range(0,256,1):
        den=256*sa+da*(256-sa)
        ws=(sa<<16)//den; wd=((da*(256-sa))<<8)//den
        ao=den/256.0
        for sgn in (-1,1):
            r=(1/ao)*(1+sgn*E_)
            ws2=int(sa*r*256); wd2=int((den/256.0-sa)*r*256)
            d1=max(abs(ws-ws2),abs(wd-wd2)); tot+=1
            if d1: wcount+=1
            mw=max(mw,d1)
# worst colour error for those weights is <= max weight diff * 255 >> 8 ; report both
print('mode0/1 2->2: max |dw|=',mw,' => max colour diff <=',math.ceil(mw*255/256),' (fraction of (sa,da) cases with any weight diff: %.1f%%)'%(100*wcount/tot))
mx=0;cnt=0;n=0
for P in range(0,256):
    for sa in range(1,256):
        for d in range(256):
            a=(d*(0xff00-sa*P))>>16; b=(d*(256-((sa*P)>>8)))>>8
            n+=1
            if a!=b: cnt+=1
            mx=max(mx,abs(a-b))
print('mode7 8bit scalar vs SIMD: max diff',mx,'differing fraction %.1f%%'%(100*cnt/n))
