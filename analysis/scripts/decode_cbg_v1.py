# Python 3.10: py -3 analysis/scripts/decode_cbg_v1.py <archive> <member> <png>
# Research decoder based on FUN_00476820/004770c0/004773f0/00477700/004778a0/00476ff0/00476cf0.
import struct, sys, zlib
from pathlib import Path

def u16(b,p): return struct.unpack_from('<H',b,p)[0]
def u32(b,p): return struct.unpack_from('<I',b,p)[0]
def read_member(path, name):
    with open(path,'rb') as f:
        h=f.read(16)
        if h[:12]!=b'BURIKO ARC20': raise ValueError('unsupported archive')
        count=u32(h,12); base=16+count*128; directory=f.read(count*128)
        for i in range(count):
            e=directory[i*128:(i+1)*128]
            if e[:96].split(b'\0')[0].decode('utf-8').lower()==name.lower():
                f.seek(base+u32(e,96)); d=f.read(u32(e,100))
                if len(d)!=u32(e,100): raise ValueError('truncated archive')
                return d
    raise ValueError('member not found')

def decode(d):
    if len(d)<48 or d[:16]!=b'CompressedBG___\0' or u16(d,46)!=1: raise ValueError('not version 1')
    w,h,bpp=u16(d,16),u16(d,18),u16(d,20); channels=bpp//8
    size,seed,tsize=u32(d,32),u32(d,36),u32(d,40)
    if channels not in (1,3,4) or not w or not h or w*h*channels>100000000 or 48+tsize>len(d): raise ValueError('invalid dimensions/header')
    table=bytearray(d[48:48+tsize]); state=seed
    for i in range(tsize):
        product=(state*0x015a4e35)&0xffffffff; table[i]=(table[i]-((product>>16)&0x7fff))&255; state=(product+1)&0xffffffff
    x=0
    for v in table: x^=v
    if sum(table)&255!=d[44] or x!=d[45]: raise ValueError('checksum mismatch')
    pos=0
    def read_var(data):
        nonlocal pos
        n=shift=0
        while True:
            if pos>=len(data) or shift>28: raise ValueError('invalid varint')
            b=data[pos]; pos+=1; n|=(b&127)<<shift; shift+=7
            if b<128:return n
    nodes=[]
    for i in range(256):
        f=read_var(table); nodes.append([f, bool(f), i, i])
    total=sum(node[0] for node in nodes)
    if not total: raise ValueError('empty Huffman tree')
    while True:
        choices=sorted((node[0],i) for i,node in enumerate(nodes) if node[1])
        if not choices: raise ValueError('invalid tree')
        a=choices[0][1]; nodes[a][1]=False
        b=choices[1][1] if len(choices)>1 else -1
        if b>=0: nodes[b][1]=False
        n=nodes[a][0]+(nodes[b][0] if b>=0 else 0); nodes.append([n,True,a,b]); root=len(nodes)-1
        if n==total:break
    bits=(48+tsize)*8; stream=bytearray(size)
    for i in range(size):
        j=root
        while j>=256:
            if bits>=len(d)*8: raise ValueError('truncated bitstream')
            bit=(d[bits>>3]>>(7-(bits&7)))&1; bits+=1; j=nodes[j][2+bit]
            if j<0:raise ValueError('invalid Huffman traversal')
        stream[i]=j
    raw=bytearray(w*h*channels); pos=0; output=0; literal=True
    while pos<len(stream):
        length=read_var(stream)
        if output+length>len(raw): raise ValueError('RLE overflow')
        if literal:
            if pos+length>len(stream): raise ValueError('RLE input truncated')
            raw[output:output+length]=stream[pos:pos+length]; pos+=length
        output+=length; literal=not literal
    if output!=len(raw): raise ValueError('RLE size mismatch')
    # Reconstruct in place, using the preceding pixel and preceding row.
    row=w*channels
    for y in range(h):
        for x in range(w):
            i=y*row+x*channels
            for c in range(channels):
                j=i+c; left=raw[j-channels] if x else -1; above=raw[j-row] if y else -1
                predictor=(0 if above<0 else above) if left<0 else (left if above<0 else (left+above)//2)
                raw[j]=(raw[j]+predictor)&255
    rgba=bytearray(w*h*4)
    for i in range(w*h):
        p=i*channels; q=i*4
        if channels==1: rgba[q:q+3]=bytes([raw[p]])*3
        else: rgba[q:q+3]=bytes((raw[p+2],raw[p+1],raw[p]))
        rgba[q+3]=raw[p+3] if channels==4 else 255
    return w,h,bpp,rgba

def png(w,h,rgba):
    def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d))
    hdr=struct.pack('>IIBBBBB',w,h,8,6,0,0,0); stride=w*4
    scan=b''.join(b'\0'+rgba[y*stride:(y+1)*stride] for y in range(h))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',hdr)+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
if __name__=='__main__':
    if len(sys.argv)!=4: raise SystemExit('usage: decode_cbg_v1.py <archive> <member> <png>')
    w,h,bpp,rgba=decode(read_member(sys.argv[1],sys.argv[2])); Path(sys.argv[3]).write_bytes(png(w,h,rgba)); print(f'{sys.argv[2]}: {w}x{h} {bpp} bpp -> {sys.argv[3]}')
