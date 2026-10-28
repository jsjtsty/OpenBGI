# Heuristic stack effect of a native sub-opcode handler: counts direct calls to the VM pop (0x44d2a0) and push (0x44d2c0)
# in the handler body, following direct calls one level deep. UNVERIFIED; for research tracing only.
import re,sys,struct,capstone
sys.path.insert(0,'.')
d=open(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0];n=struct.unpack_from('<H',d,pe+6)[0];opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
def off(va):
    r=va-0x400000
    for vs,a,rs,ra in secs:
        if a<=r<a+max(vs,rs):return ra+r-a
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
fsize={}
for l in open('out/ac2_ML.functions.tsv'):
    f=l.split('\t')
    try:fsize[int(f[0],16)]=int(f[1])
    except:pass
H=dict(re.findall(r'^\| `([0-9A-F]{2}:[0-9A-F]{2})` \| \d+ \| `([0-9A-F]{8})`',open('子opcode语义表.md',encoding='utf8').read(),re.M))
POP,PUSH=0x44d2a0,0x44d2c0
def scan(va,depth,seen):
    if va in seen or va not in fsize or off(va) is None:return 0,0,0
    seen=seen|{va};o=off(va);pop=push=br=0
    for i in md.disasm(d[o:o+fsize[va]],va):
        if i.mnemonic=='call' and i.op_str.startswith('0x'):
            t=int(i.op_str,16)
            if t==POP:pop+=1
            elif t==PUSH:push+=1
            elif depth>0 and t not in (POP,PUSH):
                a,b,c=scan(t,depth-1,seen);pop+=a;push+=b;br+=c
        elif i.mnemonic.startswith('j') and i.mnemonic!='jmp':br+=1
    return pop,push,br
def effect(key):
    va=int(H[key],16);p,q,b=scan(va,1,frozenset());return p,q,b
if __name__=='__main__':
    for k in sys.argv[1:]:print(k,effect(k))
