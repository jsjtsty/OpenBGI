# usage: py read_vtable.py <vtable VA hex> [count]  -- reads vtable entries from ac2_ML.exe (static, RTTI vftable symbol addresses)
import sys,struct
p=r'E:\Games\Amairo Chocolate 2\ac2_ML.exe'
d=open(p,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(n):
    o=pe+24+opt+i*40
    vs,va,rs,ra=struct.unpack_from('<IIII',d,o+8); secs.append((va,vs,ra,rs))
def rd(va):
    r=va-0x400000
    for a,s,ra,rs in secs:
        if a<=r<a+max(s,rs): return struct.unpack_from('<I',d,ra+r-a)[0]
va=int(sys.argv[1],16); c=int(sys.argv[2]) if len(sys.argv)>2 else 40
for i in range(c):
    print('+%02x %08x'%(i*4,rd(va+i*4)))
