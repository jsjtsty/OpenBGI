# usage: py dcfs_decode.py <sud> -- decode all SDC blocks in a .sud and, for DCFS payloads, the delta-chained frames
# FUN_00450ab0 (encode) / FUN_00450c70 (decode); varint = FUN_00450a40/00450a70 (7-bit groups, LSB first, bit7 = continue)
import sys,struct
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sdc_decode import sdc_decode
def varint(b,i):
    v=0;s=0
    while True:
        c=b[i];i+=1;v|=(c&0x7f)<<s;s+=7
        if c<0x80: return v,i
def dcfs_decode(b):
    assert b[:16]==b'DCFS FORMAT 1.00'
    S,K=struct.unpack_from('<II',b,0x10)
    frames=[b[0x18:0x18+S]]; i=0x18+S
    for _ in range(K-1):
        prev=frames[-1]; cur=bytearray(); same=True
        while len(cur)<S:
            n,i=varint(b,i)
            if same: cur+=prev[len(cur):len(cur)+n]
            else: cur+=b[i:i+n]; i+=n
            same=not same
        assert len(cur)==S
        frames.append(bytes(cur))
    return frames,i
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read(); pos=[i for i in range(len(b)-15) if b[i:i+15]==b'SDC FORMAT 1.00']
    for p in pos:
        o,osz,ok,end=sdc_decode(b,p)
        if o[:4]==b'DCFS':
            fr,used=dcfs_decode(o); print(p,'DCFS frames',len(fr),'size',len(fr[0]),'consumed',used,'of',len(o))
        else: print(p,'other',len(o),o[:30])
