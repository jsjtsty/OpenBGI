# usage: py read_wstr.py <VA hex>... -- print UTF-16LE string at each VA of ac2_ML.exe
import sys,struct
d=open(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
def off(va):
    r=va-0x400000
    for vs,a,rs,ra in secs:
        if a<=r<a+max(vs,rs): return ra+r-a
for a in sys.argv[1:]:
    o=off(int(a,16)); s=b''
    while d[o:o+2]!=b'\0\0' : s+=d[o:o+2]; o+=2
    print(a, s.decode('utf-16le'))
