# Unicorn harness: run the real blend kernels of ac2_ML.exe on synthetic bitmaps.  usage: import emu; k=emu.Kernel(); k.call(va, ecx, edx, *stack_args)
# env: analysis/.venv (uv venv; unicorn, numpy).  Bitmap struct (from the asm): +0 data, +4 pitch, +8 w, +0xc h, +0x10 fmt, +0x14 bytes/pixel.
import struct, os
from unicorn import *
from unicorn.x86_const import *
EXE=r'E:\Games\Amairo Chocolate 2\ac2_ML.exe'
BASE=0x400000; IMG=0x5c8000
STACK=0x20000000; HEAP=0x30000000; RET=0x40000000; TIB=0x42000000
BPP={0:2,1:4,2:4,3:1}
class Kernel:
    def __init__(self):
        d=open(EXE,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]
        n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
        u=Uc(UC_ARCH_X86,UC_MODE_32); self.u=u
        u.ctl_set_cpu_model(UC_CPU_X86_HASWELL)
        u.mem_map(BASE,IMG); u.mem_map(STACK,0x100000); u.mem_map(HEAP,0x4000000); u.mem_map(RET,0x1000)
        u.mem_write(BASE,d[:0x1000])
        for i in range(n):
            o=pe+24+opt+i*40; vs,va,rs,ra=struct.unpack_from('<IIII',d,o+8)
            u.mem_write(BASE+va,d[ra:ra+min(rs,vs)])
        u.mem_write(0x4cf4f7,b'\xc3')                      # security-cookie check -> ret
        for i in range(257): u.mem_write(0x5346b0+i*16,struct.pack('<8H',*[i]*8))   # init tables (see ac2_ML.c ~6280)
        for i in range(129): u.mem_write(0x5356d0+i*16,struct.pack('<8H',*[i]*8))
        # enable SSE: CR0.EM=0, CR4.OSFXSR
        u.reg_write(UC_X86_REG_CR0,u.reg_read(UC_X86_REG_CR0)&~0x4|0x2); u.reg_write(UC_X86_REG_CR4,u.reg_read(UC_X86_REG_CR4)|0x600)
        u.reg_write(UC_X86_REG_MXCSR,0x1f80)                 # mask SSE exceptions (CRT sqrt/ftol rely on it)
        u.reg_write(UC_X86_REG_FPCW,0x27f)                   # x87 control word: exceptions masked, 53-bit precision
        u.mem_write(RET,b'\xf4'); self.heap=HEAP
        u.mem_map(TIB,0x1000); u.msr_write(0xC0000100,TIB)     # FS base -> zeroed TIB so SEH prologues (fs:[0]) work
    def alloc(self,data):
        a=self.heap; self.u.mem_write(a,bytes(data)); self.heap=(a+len(data)+63)&~15; return a
    def bitmap(self,pix,w,h,fmt):
        bpp=BPP[fmt]; pitch=w*bpp; a=self.alloc(pix); s=self.alloc(struct.pack('<6I',a,pitch,w,h,fmt,bpp)); return s,a
    def read(self,a,n): return bytes(self.u.mem_read(a,n))
    def call(self,va,ecx,edx,*args):
        u=self.u; sp=STACK+0x80000
        for a in reversed(args): sp-=4; u.mem_write(sp,struct.pack('<I',a&0xffffffff))
        sp-=4; u.mem_write(sp,struct.pack('<I',RET))
        u.reg_write(UC_X86_REG_ESP,sp); u.reg_write(UC_X86_REG_EBP,0); u.reg_write(UC_X86_REG_ECX,ecx); u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(va,RET,count=0)
        assert u.reg_read(UC_X86_REG_EIP)==RET, 'did not reach return sentinel'
