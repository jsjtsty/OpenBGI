# Run the real ruby pass FUN_0043e750 after FUN_0043ba90 (see FUN_0043b030) with the faked GDI of verify_layout.py.
import sys,struct
import verify_layout as V
from verify_layout import *
V.cfg()                                   # boot-time 91:98 / 91:9A settings
def ruby_nodes(text,rect=(0,0,480,200),ruby=1,leading=30,color=0xffffff,effect=(0,0,0,0,0),fid=1,extra=None,run=True):
    k.heap=V.HB2
    M2=r32(r32(0x53605c)+4); head=r32(M2+0xf8); u.mem_write(head+0x78,bytes(4))   # drop fonts created by the previous run (their memory is reused)
    nl=k.alloc(bytes(0x64)); lines=k.alloc(bytes(4)); t=gstr(text.encode('utf-8')); rt=k.alloc(bytes(0x80))
    cur=k.alloc(struct.pack('<ii',0,0)); rc=k.alloc(struct.pack('<4i',*rect)); ef=k.alloc(struct.pack('<5i',*effect))
    k.call(0x43ba90,nl,lines,t,ruby,rt,cur,rc,leading,fid,0,0,color,ef)
    base=[]; n=r32(nl+0x60)
    while n:
        base.append(n); n=r32(n+0x60)
    before=len(base)
    if run: k.call(0x43e750,nl,fid,color,ef,rt)
    allnodes=[]; n=r32(nl+0x60)
    while n:
        allnodes.append(n); n=r32(n+0x60)
    return nl,allnodes,before,rt
def dump(n):
    return dict(addr=hex(n),off4=s32(n+4),off8=s32(n+8),x=s32(n+0x10),y=s32(n+0x14),x2=s32(n+0x18),y2=s32(n+0x1c),sz=s32(n+0x20),f0=s32(n+0),fc=s32(n+0xc),f54=r32(n+0x54),f58=s32(n+0x58),f5c=s32(n+0x5c),f54s=(cstr(r32(n+0x54)).decode('utf-8','replace') if r32(n+0x54) else ''))
if __name__=='__main__':
    for text in sys.argv[1:] or ['あ<ruby 漢字,かんじ>い','<r かん>漢</r>字']:
        try:
            nl,nodes,before,rt=ruby_nodes(text)
        except UcError as e:
            esp=u.reg_read(UC_X86_REG_ESP);print('FAULT',repr(text),e,'EIP',hex(u.reg_read(UC_X86_REG_EIP)),'regs',[hex(u.reg_read(r)) for r in (UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI)],'stack',[hex(r32(esp+4*i)) for i in range(10)],[hex(r32(esp+4*i)) for i in range(10,120) if 0x401000<r32(esp+4*i)<0x4f0000]); continue
        print(repr(text),'nodes before',before,'after',len(nodes))
        for i,n in enumerate(nodes): print(' ',i,dump(n))
