# Research prototype: py -3 analysis/scripts/vm_proto.py [--max N]
# Native _bp VM only; semantics from FUN_0044d160/0044d2a0/0044d2c0,
# FUN_00482780/004827d0, FUN_0049be00. OS-facing handlers are explicit stubs.
# Stops at first unsupported opcode; never treats unknown instructions as NOP.
import argparse,csv,pathlib,struct,sys,datetime
from bgi_dis import decode,PREFIX
ROOT=pathlib.Path(__file__).resolve().parents[1]
def s32(n):n&=0xffffffff;return n-0x100000000 if n&0x80000000 else n
def u32(n):return n&0xffffffff
def signed16(x):return struct.unpack('<h',struct.pack('<H',x))[0]
class VM:
 def __init__(self):
  self.program=bytearray();self.modules=[];self.locals=bytearray(0x400000);self.mem={};self.expr=[0]*4096;self.ep=0;self.sp=0;self.ip=0;self.calls=[];self.trace=[];self.symbols={};self.extensions={};self.nextalloc=0x50000000;self.gdb=bytearray(0x100000);self.resolution_table={}
  with (ROOT/'out/bp_corpus_v2/index.tsv').open(encoding='utf8',newline='') as f:
   for r in csv.DictReader(f,delimiter='\t'):self.symbols[(r['archive'].lower(),r['member'].lower())]=ROOT.parent/pathlib.Path(r['output'].replace('\\','/'))
  self.load('autoload.arc','ipl._bp')
 def load(self,arc,name):
  p=self.symbols.get((arc.lower(),name.lower()))
  if not p:raise ValueError(f'program not found: {arc}/{name}')
  b=p.read_bytes();off,n=struct.unpack_from('<II',b);start=len(self.program)
  if off+n>len(b):raise ValueError('truncated program block')
  if start+n>0x800000:raise ValueError('entry thread program capacity exceeded')
  self.program.extend(b[off:off+n]);self.modules.append((start,n,arc,name));self.trace.append(f'LOAD {arc}/{name} => 0x{start:X}, bytes={n}')
  return start
 def push(self,x):self.expr[self.ep]=u32(x);self.ep=(self.ep+1)%len(self.expr)
 def pop(self):self.ep=(self.ep-1)%len(self.expr);return self.expr[self.ep]
 def memory(self,addr):
  tag=addr>>26
  if tag==1:return self.program,addr&0x3ffffff
  if tag==2:return self.locals,addr&0x3ffffff
  return None,addr
 def read(self,addr,n,signed=False):
  buf,off=self.memory(addr)
  if buf is None:data=bytes(self.mem.get(off+i,0) for i in range(n))
  else:
   if off+n>len(buf):raise ValueError(f'read out of bounds: {addr:08X}+{n}')
   data=buf[off:off+n]
  return int.from_bytes(data,'little',signed=signed)
 def write(self,addr,val,n):
  data=(val&((1<<(n*8))-1)).to_bytes(n,'little');buf,off=self.memory(addr)
  if buf is None:
   for i,x in enumerate(data):self.mem[off+i]=x
  else:
   if off+n>len(buf):raise ValueError(f'write out of bounds {addr:08X}')
   buf[off:off+n]=data
 def string(self,addr):
  a=[]
  for i in range(4096):
   c=self.read(addr+i,1)
   if not c:return bytes(a).decode('cp932',errors='replace')
   a.append(c)
  raise ValueError('unterminated string')
 def modname(self,pc):
  for start,n,a,b in reversed(self.modules):
   if start<=pc<start+n:return f'{a}/{b}+0x{pc-start:X}'
  return f'0x{pc:X}'
 def subop(self,key):
  if key=='80:40':
   name=self.string(self.pop());arc=self.string(self.pop());self.push(self.load(arc,name));self.trace.append(f'  {key} load {arc}/{name}');return 0
  if key=='80:41':
   start,n,a,b=self.modules.pop();del self.program[start:];self.push(len(self.modules));self.trace.append(f'  {key} unload {a}/{b}');return 0
  if key=='80:34':
   member=self.string(self.pop());arcptr=self.pop();archive=self.string(arcptr) if arcptr else ''
   game=pathlib.Path(r'E:\Games\Amairo Chocolate 2');exists=(game/member).is_file() if not archive else (archive.lower(),member.lower()) in self.symbols
   self.push(exists);self.trace.append(f'  {key} resource {archive}/{member} exists={exists} (filesystem/corpus approximation)');return 0
  if key=='80:FD':self.push(0);self.trace.append('  80:FD STUB startup flag=0');return 0
  if key in ('80:58','80:68','80:52','80:00','90:02','91:00'):
   value=self.pop();self.trace.append(f'  {key} STUB arg={value:#x}');return 0
  if key=='81:60':
   a,b,index=[self.pop() for _ in range(3)]
   result=1 if index>=16 else 2 if not a or not b else 0
   if result==0:self.resolution_table[index]=(a,b)
   self.push(result);self.trace.append(f'  {key} resolution table index={index} values=({a},{b}) result={result} (native handler verified)');return 0
  if key=='80:70':
   a=self.pop();self.mem.clear();self.push(1);self.trace.append(f'  {key} STUB clears and initializes global region, exponent={a}, result=1');return 0
  if key=='91:09':
   a=self.pop();self.push(1);self.trace.append(f'  {key} STUB arg={a}, result=1');return 0
  if key=='80:0C':
   addr=self.pop();now=datetime.datetime(2020,1,1)
   for i,v in enumerate((now.year,now.month,3,now.day,0,0,0,0)):self.write(addr+i*2,v,2)
   self.trace.append(f'  {key} STUB deterministic SYSTEMTIME');return 0
  if key=='80:0D':self.push(0x40000000);self.push(0x20000000);self.trace.append('  80:0D STUB synthetic memory status');return 0
  if key=='81:0E':
   dst=self.pop();self.write(dst,1280,4);self.write(dst+4,720,4);self.trace.append('  81:0E STUB monitor 1280x720');return 0
  if key=='81:E9':
   size=self.pop();src=self.pop();dst=self.pop()
   if size>0x1000000:raise ValueError('hash too large')
   a=self.read(dst,4);s,x,t,y=[self.read(dst+i,1) for i in range(4,8)]
   for i in range(size):
    c=self.read(src+i,1);a=u32(a*233+c);low=a&255;s=(s+low)&255;x^=low;t=(t+c)&255;y^=c
   self.write(dst,a,4);self.write(dst+4,s|(x<<8)|(t<<16)|(y<<24),4);self.trace.append(f'  {key} rolling hash {size} bytes');return 0
  if key=='81:21':
   source=self.string(self.pop());dst=self.pop();encoded=source.encode('utf8')+b'\0'
   if not dst:raise NotImplementedError('81:21 null destination allocation')
   for i,c in enumerate(encoded):self.write(dst+i,c,1)
   self.trace.append(f'  {key} CP932 -> UTF-8 {len(encoded)} bytes');return 0
  if key=='80:21':
   addr=self.pop();self.push(1);self.trace.append(f'  {key} STUB release {addr:#x}');return 0
  if key=='80:20':
   size=self.pop()
   if size>0x1000000:raise ValueError('allocation too large')
   addr=self.nextalloc;self.nextalloc=(addr+size+15)&~15;self.push(addr);self.trace.append(f'  {key} allocate {size} => {addr:#x} (synthetic address)');return 0
  if key=='B0:C7':
   arg=[self.pop(),self.pop()];self.trace.append(f'  {key} STUB font alias args={arg}');return 0
  if key=='B0:C1':
   charset=self.pop();name=self.string(self.pop());self.push(0);self.trace.append(f'  {key} STUB font-id=0 for {name!r}, charset={charset}');return 0
  if key=='B0:C6':
   name=self.string(self.pop());dst=self.pop();self.write(dst,0x80,4);self.push(1);self.trace.append(f'  {key} STUB font charset for {name!r}');return 0
  if key=='91:0E':
   args=[self.pop() for _ in range(5)];self.trace.append(f'  {key} STUB create font args={args}');return 0
  if key=='B0:C5':
   charset=self.pop();addr=self.pop();name=b'Arial\0'
   if addr:
    for i,c in enumerate(name):self.write(addr+i,c,1)
    self.push(1)
   else:self.push(len(name))
   self.trace.append(f'  {key} STUB font enumeration charset={charset} buffer={addr:#x}');return 0
  if key=='80:2A':
   path=self.string(self.pop());exists=pathlib.Path(path).is_dir();self.push(exists);self.trace.append(f'  {key} directory {path!r} exists={exists}');return 0
  if key=='80:28':
   path=self.string(self.pop());self.push(1);self.trace.append(f'  {key} STUB assume directory creation succeeds: {path!r}');return 0
  if key=='80:80':
   import sdc_decode
   payload,expected,check,_=sdc_decode.sdc_decode(pathlib.Path(r'E:\Games\Amairo Chocolate 2\BGI.gdb').read_bytes())
   if not check or len(payload)!=expected or payload[:16]!=b'BURIKO GDB 3.00\0':raise ValueError('GDB decode failed')
   block=struct.unpack_from('<I',payload,0x1c)[0];globalbytes=payload[0x20:0x20+block]
   for i,c in enumerate(globalbytes):self.mem[i]=c
   ptr=0x20+block;n=struct.unpack_from('<I',payload,ptr)[0];self.gdb[:n]=payload[ptr+4:ptr+4+n]
   self.push(0);self.push(0);self.push(0);self.trace.append(f'  80:80 GDB restored {block} global and {n} bytes; status=0, window dimensions STUB=0');return 0
  if key in ('80:82','80:83'):
   size=self.pop()
   if key=='80:83':offset=self.pop();addr=self.pop()
   else:addr=self.pop();offset=self.pop()
   if not size or offset+size>len(self.gdb):raise ValueError('GDB copy range')
   for i in range(size):
    if key=='80:83':self.write(addr+i,self.gdb[offset+i],1)
    else:self.gdb[offset+i]=self.read(addr+i,1)
   self.trace.append(f'  {key} copy size={size} offset={offset:#x}');return 0
  if key=='80:3D':
   which=self.pop();addr=self.pop();path='E:\\Games\\Amairo Chocolate 2\\' if which==0 else ''
   for i,c in enumerate(path.encode('cp932')+b'\0'):self.write(addr+i,c,1)
   self.push(which==0);self.trace.append(f'  {key} STUB base path for selector {which}');return 0
  if key=='80:E8':
   addr=self.pop()
   for i,c in enumerate(b'AmairoChocolate2ML\0'):self.write(addr+i,c,1)
   self.trace.append(f'  {key} writes game ID string');return 0
  if key=='C0:1F':
   v=self.pop()
   if v>2:raise ValueError('C0:1F mode out of range')
   self.mem[0x58f350]=v;self.trace.append(f'  {key} global mode={v}');return 0
  if key=='90:10':
   member=self.string(self.pop());archive=self.string(self.pop());slot=self.pop()
   self.trace.append(f'  {key} STUB synchronous image load assumed: {archive}/{member}, slot={slot}; no pixels loaded')
   return 0
  if key=='91:1F':
   source=self.pop();destination=self.pop()
   self.trace.append(f'  {key} STUB image copy source={source} destination={destination}; no pixels copied')
   return 0
  if key=='90:11':
   fmt,height,width,slot=[self.pop() for _ in range(4)]
   self.trace.append(f'  {key} STUB image buffer slot={slot} size={width}x{height} format={fmt}; no pixels allocated')
   return 0
  if key=='90:12':
   slot=self.pop();self.push(1);self.trace.append(f'  {key} STUB release image slot={slot}, assume success');return 0
  if key=='90:13':
   color=self.pop();slot=self.pop();self.trace.append(f'  {key} STUB fill slot={slot}, color={color:#x}');return 0
  if key in ('90:1F','90:18'):
   args=[self.pop() for _ in range(6)];self.trace.append(f'  {key} STUB image operation args(pop order)={args}');return 0
  if key=='81:6F':
   mode=self.pop(); result=int(mode in (0,2,3) or (mode in (1,4) and getattr(self,'shader_available',False)))
   if result:self.shader_mode=mode
   self.push(result);self.trace.append(f'  {key} mode={mode} result={result}'+(' (SIMULATED shader unavailable)' if mode in (1,4) and not getattr(self,'shader_available',False) else ''));return 0
  startup={'80:D8':(0,None),'90:07':(1,None),'90:0C':(2,None),'90:4C':(2,None),'90:00':(1,None),'80:38':(2,1),'80:46':(0,1),'B0:03':(2,1),'B0:02':(0,1),'80:64':(1,None),'80:62':(2,None),'80:1B':(2,None),'A0:08':(2,None),'A0:09':(2,None),'81:64':(2,None),'80:60':(3,None),'81:63':(1,1),'90:9C':(1,None),'B0:05':(1,None),'90:97':(2,None),'90:94':(1,None),'91:98':(6,None),'92:97':(7,None),'90:96':(2,None),'90:95':(2,None),'90:03':(1,None),'90:08':(1,None),'81:00':(1,1),'91:0D':(1,None),'90:0D':(1,None),'91:9A':(2,None),'80:67':(1,None),'80:74':(1,None),'80:37':(1,None),'80:36':(1,None),'80:AF':(1,1),'90:9F':(1,None),'91:99':(1,1),'90:AF':(1,None),'81:62':(1,1),'90:06':(2,None),'81:18':(1,1)}
  if key in startup:
   count,result=startup[key];args=[self.pop() for _ in range(count)]
   if result is not None:self.push(result)
   if key=='80:37':self.trace.append(f'  {key} STUB path={self.string(args[0])!r}')
   else:self.trace.append(f'  {key} STUB args={args} result={result}')
   return 0
  if key=='80:5F':self.trace.append('  80:5F yield');return 1
  if key=='80:5A':
   if getattr(self,'resume',False):
    self.push(0);self.trace.append("  80:5A RESUME-MODE assume deadline reached, push 0");return 0
   self.trace.append('  80:5A timing scheduler return=2; remaining time and pushed boolean depend on host clock (not simulated)')
   return 2
  if key=='80:5C':
   args=[self.pop() for _ in range(3)];self.trace.append(f'  80:5C wait args(pop order)={args}');return 2
  if getattr(self,'auto',False):
   import autoeffect
   n_pop,n_push,branchy=autoeffect.effect(key)
   args=[self.pop() for _ in range(n_pop)]
   for _ in range(n_push):self.push(0)
   self.trace.append(f'  {key} AUTO-STUB (unverified) pops={n_pop} pushes={n_push} branchy={branchy} args={[hex(a) for a in args]}');return 0
  raise NotImplementedError(f'subopcode {key}: no trustworthy stub yet')
 def step(self):
  pc=self.ip;end,op,label,arg=decode(self.program,pc,True);self.ip=end
  raw=self.program[pc:end];v=lambda o,n:int.from_bytes(raw[o:o+n],'little');value=v(1,2) if len(raw)>=3 else 0
  if op in PREFIX:
   key=f'{op:02X}:{raw[1]:02X}';self.trace.append(f'{self.modname(pc)} {key} ep={self.ep} sp={self.sp}');return self.subop(key)
  if op==0:self.push(int.from_bytes(raw[1:2],'little',signed=True))
  elif op==1:self.push(int.from_bytes(raw[1:3],'little',signed=True))
  elif op==2:self.push(v(1,4))
  elif op==4:self.push((self.sp-value)|0x08000000)
  elif op==5:self.push((pc+signed16(value))|0x04000000)
  elif op==6:self.push(pc+signed16(value))
  elif op in (8,9,10):
   width={0:1,1:2,2:4}.get(raw[1],1)
   if op==8:self.push(self.read(self.pop(),width,signed=width<4))
   else:
    val=self.pop();addr=self.pop();self.write(addr,val,width)
    if op==9:self.push(val)
  elif op==0x0c:
   width={0:1,1:2,2:4}[raw[1]];values=[self.pop() for _ in range(raw[2])];addr=self.pop()
   for i,val in enumerate(reversed(values)):self.write(addr+i*width,val,width)
  elif op==0x0f:
   addr=(self.sp-(value&0x3fff))|0x08000000;width={0:1,1:2,2:4,3:1}[value>>14];self.write(addr,self.pop(),width)
  elif op==0x12:self.sp+=int(arg)
  elif op==0x14:self.ip=self.pop()
  elif op==0x15:
   mode=raw[1];dest=pc+signed16(v(2,2)) if mode&8 else self.pop();a=s32(self.pop());conds=[a!=0,a==0,a>0,a>=0,a<=0,a<0]
   if mode&7>=len(conds):raise NotImplementedError('unsupported branch mode')
   if conds[mode&7]:self.ip=dest
  elif op==0x16:
   dest=self.pop();self.write(0x08000000|self.sp,end,4);self.sp+=4;self.calls.append((pc,end));self.ip=dest;self.trace.append(f'CALL {self.modname(pc)} -> {self.modname(dest)} return=0x{end:X} sp={self.sp}')
  elif op==0x17:
   if not self.calls:self.trace.append('RETURN root');return 4
   self.sp-=4;dest=self.read(0x08000000|self.sp,4);origin,expected=self.calls.pop()
   if dest!=expected:raise ValueError(f'return mismatch {dest:X} vs {expected:X}')
   self.trace.append(f'RETURN {self.modname(pc)} -> {self.modname(dest)} sp={self.sp}');self.ip=dest
  elif op==0x18:
   encoded=v(1,4);width={0:1,1:2,2:4,3:1}[encoded>>30];self.push(self.read(encoded&0x3fffffff,width,signed=width<4))
  elif op==0x19:
   addr=(self.sp-(value&0x3fff))|0x08000000;width={0:1,1:2,2:4,3:1}[value>>14];self.push(self.read(addr,width,signed=width<4))
  elif op in (0x1a,0x1b,0x1c,0x1d,0x1e):
   addr=(self.sp-(value&0x3fff))|0x08000000;width={0:1,1:2,2:4,3:1}[value>>14];val=self.read(addr,width,signed=width<4)
   if op in (0x1a,0x1b):
    imm=int(arg.split()[1]);self.push(val+imm if op==0x1a else val*imm+self.pop())
   elif op==0x1c:
    shift=struct.unpack('<b',bytes([raw[3]]))[0];val=s32(val);self.push(val<<shift if shift>=0 else val>>(-shift))
   elif op==0x1d:self.push(val*self.pop())
   else:
    numerator=s32(self.pop())
    if val==0:self.push(-1)
    else:self.push(int(numerator/val))
  elif op==0x1f:
   imm=int(arg);addr=u32((imm>>2)+self.pop());width={0:1,1:2,2:4,3:1}[imm&3];self.push(self.read(addr,width,signed=width<4))
  elif op in (0x20,0x21,0x22,0x25,0x26,0x27,0x30,0x31,0x32,0x33,0x34,0x35):
   a=self.pop();b=self.pop();ops={0x20:lambda:b+a,0x21:lambda:b-a,0x22:lambda:b*a,0x25:lambda:b&a,0x26:lambda:b|a,0x27:lambda:b^a,0x30:lambda:b==a,0x31:lambda:b!=a,0x32:lambda:s32(b)<=s32(a),0x33:lambda:s32(b)>=s32(a),0x34:lambda:s32(b)<s32(a),0x35:lambda:s32(b)>s32(a)};self.push(ops[op]())
  elif op in (0x23,0x24):
   a=s32(self.pop());b=s32(self.pop())
   if not a:self.push(-1)
   else:
    q=abs(b)//abs(a)*(-1 if (a<0)!=(b<0) else 1)
    self.push(q if op==0x23 else b-q*a)
  elif op==0x28:self.push(~self.pop())
  elif op in (0x29,0x2a,0x2b):
   a=self.pop()&31;b=self.pop();self.push(b<<a if op==0x29 else u32(b)>>a if op==0x2a else s32(b)>>a)
  elif op==0x2c:self.push(self.pop()+int(arg))
  elif op==0x2d:self.push(self.pop()*int(arg))
  elif op==0x2e:
   a=self.pop();b=self.pop();self.push(a*int(arg)+b)
  elif op==0x2f:
   divisor=int(arg);n=s32(self.pop());self.push(-1 if not divisor else int(n/divisor))
  elif op in (0x38,0x39):
   a=self.pop();b=self.pop();self.push(bool(a and b) if op==0x38 else bool(a or b))
  elif op==0x3a:self.push(not self.pop())
  elif op==0x3b:
   mode=raw[1];dest=pc+signed16(v(2,2));a=self.pop();b=self.pop();conds=[b==a,b!=a,s32(b)<s32(a),s32(b)<=s32(a),s32(a)<=s32(b),s32(a)<s32(b),bool(a&b),bool(a or b),bool(a and b),bool(b or a)]
   if mode&15>=len(conds):raise NotImplementedError('unsupported compare')
   cond=conds[mode&15];cond=cond if mode&0x80 else not cond
   if cond:self.ip=dest
  elif op==0x3c:
   shift=struct.unpack('<b',bytes([raw[1]]))[0];val=s32(self.pop());self.push(val<<shift if shift>=0 else val>>(-shift))
  elif op==0xff:
   ext=raw[1]
   if ext==0xf0:
    member=self.string(self.pop());archive=self.string(self.pop());slot=self.pop()
    if slot>0xef:raise ValueError('extension id out of range')
    self.extensions[slot]=(archive,member);self.trace.append(f'EXT register {slot:#x} {archive}/{member} (resource load deferred)')
   elif ext==0xf1:
    slot=self.pop();self.extensions.pop(slot,None);self.trace.append(f'EXT unregister {slot:#x}')
   elif ext==0xf8:
    for _ in range(2):
     start,n,a,b=self.modules.pop();del self.program[start:];self.trace.append(f'EXT unload {a}/{b}')
    self.sp-=4;dest=self.read(0x08000000|self.sp,4);origin,expected=self.calls.pop()
    if dest!=expected:raise ValueError('extension return mismatch')
    self.ip=dest;self.trace.append(f'EXT return -> {self.modname(dest)}')
   elif ext<0xf0 and ext in self.extensions:
    archive,name=self.extensions[ext];start=len(self.program);med=bytes.fromhex('06100016fff8')+bytes(10);self.program.extend(med);self.modules.append((start,len(med),'[mediation]',f'{ext:02X}'))
    target=self.load(archive,name);self.write(0x08000000|self.sp,end,4);self.sp+=4;self.calls.append((pc,end));self.ip=start
    self.trace.append(f'EXT call {ext:02X} mediator={start:#x} target={target:#x} return={end:#x}')
   else:raise NotImplementedError(f'extension {ext:02X}')
  elif op==0x6f:
   import re
   fmt=self.string(self.pop());dst=self.pop()
   def subst(m):
    kind=m.group(1)
    if kind=='%':return '%'
    v=self.pop()
    if kind=='s':return m.group(0)%self.string(v)
    if kind=='d':return m.group(0)%s32(v)
    if kind in ('x','X'):return m.group(0)%v
    raise NotImplementedError('format '+kind)
   text=re.sub(r'%(?:[0-9 .-]*)([%sdxX])',subst,fmt)
   if '%' in text and '%' in fmt:raise NotImplementedError('unsupported remaining format')
   for i,c in enumerate(text.encode('cp932',errors='replace')+b'\0'):self.write(dst+i,c,1)
  elif op==0x60:
   n=self.pop();src=self.pop();dst=self.pop()
   if n>0x1000000:raise ValueError('copy too large')
   vals=[self.read(src+i,1) for i in range(n)]
   for i,c in enumerate(vals):self.write(dst+i,c,1)
  elif op==0x61:
   n=self.pop();dst=self.pop()
   if n>0x1000000:raise ValueError('clear too large')
   for i in range(n):self.write(dst+i,0,1)
  elif op==0x62:
   val=self.pop();n=self.pop();dst=self.pop()
   if n>0x1000000:raise ValueError('fill too large')
   for i in range(n):self.write(dst+i,val,1)
  elif op==0x63:
   n=self.pop();a=self.pop();b=self.pop()
   if n>0x1000000:raise ValueError('comparison too large')
   self.push(all(self.read(a+i,1)==self.read(b+i,1) for i in range(n)))
  elif op==0x67:
   replacement=self.string(self.pop());needle=self.string(self.pop());source=self.string(self.pop());dst=self.pop()
   if not needle:raise NotImplementedError('empty replacement needle')
   count=source.count(needle);text=source.replace(needle,replacement)
   for i,c in enumerate(text.encode('cp932',errors='replace')+b'\0'):self.write(dst+i,c,1)
   self.push(count)
  elif op==0x68:self.push(len(self.string(self.pop()).encode('cp932',errors='replace')))
  elif op==0x69:
   a=self.string(self.pop());b=self.string(self.pop());self.push(a==b)
  elif op==0x6a:
   src=self.pop();dst=self.pop();rawstr=self.string(src).encode('cp932',errors='replace')+b'\0'
   for i,c in enumerate(rawstr):self.write(dst+i,c,1)
  elif op==0x6b:
   tail=self.string(self.pop());head=self.string(self.pop());dst=self.pop();rawstr=(head+tail).encode('cp932',errors='replace')+b'\0'
   for i,c in enumerate(rawstr):self.write(dst+i,c,1)
  elif op==0x40:
   a=self.pop();b=self.pop();c=self.pop();self.push(b if c else a)
  else:raise NotImplementedError(f'main opcode {op:02X} at {self.modname(pc)}')
  return 0
 def run(self,limit):
  for n in range(limit):
   pc=self.ip
   try:rc=self.step()
   except Exception as e:self.trace.append(f'STOP {self.modname(pc)} {type(e).__name__}: {e}');return 'unsupported/error'
   if rc==2:self.trace.append('STOP scheduler return 2 (wait object installation depends on handler and runtime state)');return 'wait'
   if rc==4:self.trace.append('STOP root exit');return 'exit'
  self.trace.append(f'STOP instruction limit {limit}');return 'limit'
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--max',type=int,default=100000);ap.add_argument('--resume',action='store_true');ap.add_argument('--auto',action='store_true');ap.add_argument('--out',default='out/vm_trace_boot.txt');args=ap.parse_args();vm=VM();vm.resume=args.resume;vm.auto=args.auto;state=vm.run(args.max);out=ROOT/args.out;out.write_text('\n'.join(vm.trace)+'\n',encoding='utf8');print('state',state,'instructions limit',args.max,'trace',out,'last',vm.trace[-1])
