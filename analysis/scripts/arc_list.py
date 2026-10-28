# usage: py arc_list.py <arc> [substr] -- list ARC20 members (name, offset, size)
import sys,struct
d=open(sys.argv[1],'rb').read(); n=struct.unpack_from('<I',d,12)[0]; base=16+n*0x80
for i in range(n):
    e=d[16+i*0x80:16+(i+1)*0x80]; name=e[:0x60].split(b'\0')[0].decode('cp932','replace')
    off,size=struct.unpack_from('<II',e,0x60)
    if len(sys.argv)<3 or sys.argv[2] in name: print(i,name,base+off,size,d[base+off:base+off+16].hex())
