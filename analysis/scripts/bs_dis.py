# Python 3.10: py -3 analysis/scripts/bs_dis.py <decoded-compiled-script> [--output <path>]
# scrmain._bp 0x232..0x2c3: fetch u32 and dispatch. scrctrl._bp 0x3e6: read u32 operand.
# Handler mapping and read summaries are research-derived; native helper indirect effects remain a risk.
import argparse,csv,pathlib,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[1];MAGIC=b'BurikoCompiledScriptVer1.00\0'
def tables():
 with (ROOT/'out'/'bs_dispatch_candidates.tsv').open(encoding='utf8',newline='') as f:handlers={int(r['opcode'],16):r for r in csv.DictReader(f,delimiter='\t')}
 counts={}
 with (ROOT/'out'/'bs_handler_reads.tsv').open(encoding='utf8',newline='') as f:
  for r in csv.DictReader(f,delimiter='\t'):
   op=int(r['opcode'],16);v=r['direct_bs_read_counts'].split(',')
   counts[op]=(int(v[0]) if len(v)==1 and v[0].isdigit() else None) if r['program'].endswith('/00010_scrctrl._bp') else 0
 return handlers,counts
HANDLERS,COUNTS=tables()
NAMES={0:'push_imm',1:'push_module_addr',3:'push_relative_addr',0x10:'dup?',0x11:'pop?',0x18:'jump_indirect',0x1b:'return',0xf4:'stop'}
def read(path):
 d=pathlib.Path(path).read_bytes()
 if not d.startswith(MAGIC):raise ValueError('not compiled script')
 base=28+struct.unpack_from('<I',d,28)[0]
 if base>len(d):raise ValueError('invalid section')
 p=32
 def u32():
  nonlocal p
  if p+4>base:raise ValueError('truncated metadata')
  v=struct.unpack_from('<I',d,p)[0];p+=4;return v
 def string():
  nonlocal p
  end=d.find(b'\0',p,base)
  if end<0:raise ValueError('unterminated name')
  result=d[p:end].decode('utf8',errors='replace');p=end+1;return result
 num=u32();deps=[string() for _ in range(num)];num=u32();symbols=[]
 for _ in range(num):symbols.append((string(),u32()))
 data=d[base:];pc=0;insns=[];refs=[]
 while pc+4<=len(data):
  op=struct.unpack_from('<I',data,pc)[0]
  if op not in HANDLERS or COUNTS.get(op) is None:break
  n=COUNTS[op];end=pc+4*(1+n)
  if end>len(data):raise ValueError(f'truncated operands at {pc:x}')
  args=struct.unpack_from('<'+'I'*n,data,pc+4) if n else ()
  insns.append((pc,op,args));
  if op==3:refs.append(args[0])
  pc=end
 starts={x[0] for x in insns};bad=[(n,v) for n,v in symbols if v not in starts];badrefs=[v for v in refs if v<pc or v>=len(data) or data.find(b'\0',v)<0]
 badtargets=[(pos,args[0]) for pos,op,args in insns if op==1 and args[0] not in starts]
 if bad:raise ValueError(f'export not at instruction boundary: {bad[:3]}')
 if badrefs:raise ValueError(f'03 target outside suffix: {badrefs[:3]}')
 if badtargets:raise ValueError(f'01 target not at instruction boundary: {badtargets[:3]}')
 if not insns or insns[-1][1] not in (0x1b,0xf4):raise ValueError('unvalidated code/data boundary')
 return base,deps,symbols,insns,pc,data

def main():
 ap=argparse.ArgumentParser();ap.add_argument('file');ap.add_argument('--output');a=ap.parse_args();base,deps,symbols,insns,end,data=read(a.file);label={};
 for name,off in symbols:label.setdefault(off,[]).append(name)
 lines=[f'; payload file offset=0x{base:x}; code end=0x{end:x}; data bytes={len(data)-end}',f'; modules={len(deps)} exports={len(symbols)}']
 for pc,op,args in insns:
  for name in label.get(pc,[]):lines.append(f'\n{name}:')
  arg=' '.join(f'{v:08X}' for v in args);r=HANDLERS[op];comment=f"{r['program']}+{r['handler']}"
  if op==3:
   stop=data.find(b'\0',args[0]);v=data[args[0]:stop].decode('cp932',errors='replace');comment+=' '+repr(v[:100])
  lines.append(f'{pc:08X}  {op:04X} {arg:<26} {NAMES.get(op,"op_%04X"%op):<20} ; {comment}')
 result='\n'.join(lines)+'\n'
 if a.output:pathlib.Path(a.output).write_text(result,encoding='utf8')
 else:print(result,end='')
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,struct.error) as e:print('error:',e,file=sys.stderr);sys.exit(1)
