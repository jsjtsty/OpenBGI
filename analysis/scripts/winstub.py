# Win32 import stubs for emu.Kernel: maps every IAT slot of ac2_ML.exe to a stub address; a code hook dispatches stdcall calls to Python.
# usage: w=winstub.Win(k); w.api('Name',nargs)(lambda a: ...)  (a = list of stdcall args) ; unknown imports raise with the name.
import struct
from unicorn import *; from unicorn.x86_const import *
import emu
STUBS=0x41000000
def imports(exe=emu.EXE):
    d=open(exe,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]
    n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
    secs=[struct.unpack_from('<IIII',d,pe+24+opt+i*40+8) for i in range(n)]
    def off(rva):
        for vs,va,rs,ra in secs:
            if va<=rva<va+max(vs,rs): return rva-va+ra
        raise ValueError(hex(rva))
    idir=struct.unpack_from('<I',d,pe+24+0x60+8)[0]; out={}
    o=off(idir)
    while True:
        ol,_,_,nm,ft=struct.unpack_from('<IIIII',d,o)
        if nm==0: break
        dll=d[off(nm):].split(b'\0')[0].decode().lower()
        t=off(ol or ft); i=0
        while True:
            v=struct.unpack_from('<I',d,t+i*4)[0]
            if v==0: break
            if not v&0x80000000: out[d[off(v)+2:].split(b'\0')[0].decode()]=emu.BASE+ft+i*4
            i+=1
        o+=20
    return out
class Win:
    def __init__(self,k):
        self.k=k; u=k.u; self.iat=imports(); self.impl={}; self.names={}
        u.mem_map(STUBS,0x10000)
        for i,(name,slot) in enumerate(sorted(self.iat.items())):
            a=STUBS+i*16; u.mem_write(a,b'\xf4'); u.mem_write(slot,struct.pack('<I',a)); self.names[a]=name
        u.hook_add(UC_HOOK_CODE,self._hook,None,STUBS,STUBS+0xffff)
        self.log=[]
    def api(self,name,nargs):
        def deco(fn): self.impl[name]=(nargs,fn); return fn
        return deco
    def _hook(self,u,addr,size,_):
        name=self.names.get(addr)
        if name not in self.impl: raise RuntimeError('unimplemented import '+str(name))
        nargs,fn=self.impl[name]; esp=u.reg_read(UC_X86_REG_ESP)
        ret=struct.unpack('<I',u.mem_read(esp,4))[0]; a=list(struct.unpack('<%dI'%nargs,u.mem_read(esp+4,4*nargs))) if nargs else []
        self.log.append((name,a)); r=fn(a) or 0
        u.reg_write(UC_X86_REG_EAX,r&0xffffffff); u.reg_write(UC_X86_REG_ESP,esp+4+4*nargs); u.reg_write(UC_X86_REG_EIP,ret)
