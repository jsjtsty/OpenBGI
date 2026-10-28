# Experimental original-engine BM frame decoder, not an independent format implementation.
# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/decode_bm.py <arc> <member> [count]
#    or: analysis/.venv/Scripts/python.exe analysis/scripts/decode_bm.py <file.bm> [count]
# FUN_004acf80 (YCBFMovie2 ctor), FUN_004aeb40 (frame) / FUN_004aebc0.
# Runs native x86 under Unicorn; allocator/free and pool environment are emulated.
import pathlib,struct,sys,zlib,binascii,time
import emu
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
def member(arc,name):
 with open(arc,'rb') as f:
  head=f.read(16);assert head[:12]==b'BURIKO ARC20';n=u32(head,12);table=f.read(n*128);base=16+n*128
  for i in range(n):
   r=table[i*128:(i+1)*128]
   if r[:96].split(b'\0')[0].decode('cp932').lower()==name.lower():
    off,size=struct.unpack_from('<II',r,96);f.seek(base+off);return f.read(size)
 raise ValueError('member not found')
def png(path,w,h,bgra,source_bits):
 def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',binascii.crc32(t+d)&0xffffffff)
 lines=bytearray()
 for y in range(h):
  lines.append(0)
  for x in range(w):
   i=(y*w+x)*4;b,g,r,a=bgra[i:i+4];lines.extend((r,g,b,a if source_bits==32 else 255))
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(lines,6))+chunk(b'IEND',b''))
def main():
 source=pathlib.Path(sys.argv[1])
 if source.suffix.lower()=='.bm':
  name=source.name;data=source.read_bytes();limit=int(sys.argv[2]) if len(sys.argv)>2 else 0
 else:
  name=sys.argv[2];data=member(source,name);limit=int(sys.argv[3]) if len(sys.argv)>3 else 0
 assert data[:16]==b'BF_Movie_______\0';version,w,h,bits,fmt,rate,frames=struct.unpack_from('<7I',data,16)
 if w<=0 or h<=0 or w*h>4000000 or bits not in (24,32):raise ValueError('unsupported header')
 k=emu.Kernel();u=k.u;u.mem_map(0,0x1000);u.mem_write(0,b'\xff'*4)
 get=lambda a:u32(k.read(a,4))
 def alloc(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);n=get(sp+4);p=k.alloc(bytes(n));uc.reg_write(UC_X86_REG_EAX,p);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
 def free(uc,addr,size,user):
  sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,get(sp))
 for a in (0x4cf50d,0x4cf54b,0x4dfb3c):u.hook_add(UC_HOOK_CODE,alloc,None,a,a)
 for a in (0x4cf508,0x4cf53d,0x4e0964,0x4dfb22):u.hook_add(UC_HOOK_CODE,free,None,a,a)
 pool=k.alloc(bytes(0x100));u.mem_write(pool+4,struct.pack('<II',1,1))
 obj=k.alloc(bytes(0x300));fileaddr=k.alloc(data+bytes(16));pixels=k.alloc(bytes(w*h*4));bitmap=k.alloc(struct.pack('<6I',pixels,w*4,w,h,fmt,4))
 k.call(0x4acf80,obj,0,pool)
 scratch=k.heap  # native decoder frees all per-frame temporaries before return
 out=pathlib.Path('analysis/out/bm_frames')/pathlib.Path(name).stem;out.mkdir(parents=True,exist_ok=True)
 for i in range(min(frames,limit) if limit else frames):
  k.heap=scratch;t=time.monotonic();k.call(0x4aeb40,obj,0,bitmap,fileaddr,i);result=u.reg_read(UC_X86_REG_EAX)
  if result:raise RuntimeError(f'frame {i}: decoder returned {result:#x}')
  png(out/f'{i:05}.png',w,h,k.read(pixels,w*h*4),bits);print('frame',i,'seconds',round(time.monotonic()-t,2),flush=True)
 print('original-engine Unicorn decode only:',name,'frames',frames,'dimensions',w,h,'source bits',bits)
if __name__=='__main__':main()
