# usage: py sdc_decode.py <in> <out> -- decode "SDC FORMAT 1.00" (BGI.gdb, .sud blocks) ; FUN_004a2220 / FUN_004a22b0 / FUN_004a2180 (encrypt/verify/LZ)
import sys,struct
def keystream(seed):
    st=seed&0xffffffff
    while True:
        p=(st*0x015a4e35)&0xffffffff
        yield (p>>16)&0x7fff
        st=(p+1)&0xffffffff
def sdc_decode(b, off=0):
    assert b[off:off+15]==b'SDC FORMAT 1.00'
    seed,csz,osz,s16,x16=struct.unpack_from('<IIIHH',b,off+0x10)
    ks=keystream(seed); pay=bytearray(b[off+0x20:off+0x20+csz])
    ssum=0;sx=0
    for i,c in enumerate(pay):
        ssum=(ssum+c)&0xffff; sx^=c
    ok=(ssum==s16 and sx==x16)
    pay=bytes((c-next(ks))&0xff for c in pay)
    out=bytearray(); i=0
    while i<len(pay):
        c=pay[i]; i+=1
        if c&0x80:
            n=((c>>3)&0xf)+2; d=pay[i]+2+((c&7)<<8); i+=1
            for _ in range(n): out.append(out[-d])
        else:
            n=c+1; out+=pay[i:i+n]; i+=n
    return bytes(out),osz,ok,32+csz
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read(); o,osz,ok,end=sdc_decode(b)
    print('size',len(o),'expected',osz,'checksum ok',ok,'end',end); open(sys.argv[2],'wb').write(o)
