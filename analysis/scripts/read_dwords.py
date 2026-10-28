# usage: py read_dwords.py <VA hex>... -- print zero-terminated dword list at each VA of ac2_ML.exe (static initial data)
import sys,struct
d=open(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
def off(va):
    r=va-0x400000
    for vs,a,rs,ra in secs:
        if a<=r<a+max(vs,rs): return ra+r-a
for a in sys.argv[1:]:
    o=off(int(a,16)); v=[]
    while len(v)<40:
        x=struct.unpack_from('<I',d,o)[0]
        if x==0: break
        v.append(x); o+=4
    print(a,' '.join('%02X'%x if x<0x100 else '%X'%x for x in v))
