# Python 3.10: py -3 analysis/scripts/bgi_dis.py <unpacked._bp> [--start N] [--count N]
# Research-only disassembler for the native bytecode VM: FUN_0044d160/180/1a0/1c0/1e0,
# FUN_0049be00, and individual handlers in analysis/out/ac2_ML.c. Not a BS parser.
# Output is a linear hypothesis: code and data share a segment, branch/call targets require analysis.
import argparse, csv, pathlib, struct, sys, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def tables():
    with (ROOT/'out'/'handler_reads.tsv').open(encoding='utf-8-sig',newline='') as f:
        main={int(r['opcode'],16):r for r in csv.DictReader(f,delimiter='\t')}
    with (ROOT/'子opcode表.tsv').open(encoding='utf-8-sig',newline='') as f:
        sub={(int(r['主opcode'],16),int(r['子opcode'],16)):r['处理器VA'] for r in csv.DictReader(f,delimiter='\t')}
    return main,sub
MAIN,SUB=tables(); PREFIX={0x7f,0x80,0x81,0x90,0x91,0x92,0xa0,0xb0,0xc0,0xd0,0xe0}
NAMES={0:'push_i8',1:'push_i16',2:'push_i32',4:'addr_stack',5:'addr_code',6:'ip_relative',8:'load',9:'store_keep',10:'store',11:'copy_inline',12:'store_many',15:'store_stack',16:'push_sp',17:'set_sp',18:'adjust_sp',20:'jump',21:'branch',22:'call',23:'ret',24:'load_imm_addr',25:'load_stack',26:'load_stack_add',27:'load_stack_muladd',28:'load_stack_shift',29:'load_stack_mul',30:'div_stack',31:'load_offset',32:'add',33:'sub',34:'mul',35:'div',36:'mod',37:'and',38:'or',39:'xor',40:'not',41:'shl',42:'shr',43:'sar',44:'add_imm',45:'mul_imm',46:'muladd_imm',47:'div_imm',48:'eq',49:'ne',50:'le',51:'ge',52:'lt',53:'gt',56:'logical_and',57:'logical_or',58:'logical_not',59:'branch_compare',60:'shift_imm',255:'extension'}
class DecodeError(Exception):pass
class Reader:
    def __init__(self,data,p):self.data,self.p=data,p
    def take(self,n):
        if n<0 or self.p+n>len(self.data):raise DecodeError('truncated operand')
        x=self.data[self.p:self.p+n];self.p+=n;return x
    def u8(self):return self.take(1)[0]
    def u16(self):return struct.unpack('<H',self.take(2))[0]
    def u32(self):return struct.unpack('<I',self.take(4))[0]
    def sleb(self):
        v=0;shift=0
        for _ in range(5):
            b=self.u8();v|=(b&127)<<shift;shift+=7
            if b<128:return v-(1<<shift) if b&64 else v
        raise DecodeError('SLEB length exceeds five bytes')
def decode(d,pc,allow_null_sub=False):
    r=Reader(d,pc);op=r.u8()
    if op not in MAIN:raise DecodeError(f'unknown opcode 0x{op:02x}')
    label=NAMES.get(op,'op_%02x'%op); args=[]
    if op in PREFIX:
        sub=r.u8();addr=SUB.get((op,sub));args=[f'{sub:02X}',f'handler={addr or "UNLISTED"}']
        if not addr:
            if not allow_null_sub:raise DecodeError(f'unlisted subopcode {op:02x}:{sub:02x}')
            args=[f'{sub:02X}','handler=NULL (runtime error if executed)']
    elif op==255:args=[f'{r.u8():02X}']
    elif op==0:args=[str(struct.unpack('<b',r.take(1))[0])]
    elif op==1:args=[str(struct.unpack('<h',r.take(2))[0])]
    elif op in (2,24):args=[f'0x{r.u32():08X}']
    elif op in (4,5,6,15,25,29,30):args=[f'0x{r.u16():04X}']
    elif op in (26,27):args=[f'0x{r.u16():04X}',str(r.sleb())]
    elif op==28:args=[f'0x{r.u16():04X}',str(struct.unpack('<b',r.take(1))[0])]
    elif op in (18,31,44,45,46,47):args=[str(r.sleb())]
    elif op==11:
        n=r.u8();args=[str(n),r.take(n).hex()]
    elif op==12:args=[str(r.u8()),str(r.u8())]
    elif op==21:
        mode=r.u8();args=[f'mode={mode:02X}'];
        if mode&8:args.append(f'delta={struct.unpack("<h",r.take(2))[0]}')
    elif op==59:args=[f'mode={r.u8():02X}',f'delta={struct.unpack("<h",r.take(2))[0]}']
    elif op in (8,9,10,60):args=[str(r.u8())]
    return r.p,op,label,' '.join(args)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('file');ap.add_argument('--start',type=lambda x:int(x,0),default=0);ap.add_argument('--count',type=int,default=100);ap.add_argument('--stop-ret',action='store_true');ap.add_argument('--allow-null-sub',action='store_true',help='decode empty native subtable slots as evidence; executing them raises an error');a=ap.parse_args();raw=pathlib.Path(a.file).read_bytes()
    if raw.startswith(b'BurikoCompiledScriptVer1.00\0'):
        if a.start or a.count!=100 or a.stop_ret:
            raise DecodeError('BS input: --start, --count and --stop-ret apply only to BP')
        return subprocess.call([sys.executable,str(ROOT/'scripts'/'bs_dis.py'),a.file])
    if len(raw)<16:raise DecodeError('short bp header')
    base,size=struct.unpack_from('<II',raw); 
    if base+size>len(raw):raise DecodeError('bad bp range')
    data=raw[base:base+size];pc=a.start
    print(f'; program offset=0x{base:x} size=0x{size:x}; linear hypothesis')
    for _ in range(a.count):
        if pc>=len(data):break
        start=pc
        try:pc,op,label,args=decode(data,pc,a.allow_null_sub)
        except DecodeError as e:print(f'{pc:08x}: ERROR {e}');return 1
        print(f'{start:08x}: {data[start:pc].hex(" "):<24} {label:<20} {args}')
        if a.stop_ret and op==0x17:break
    return 0
if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,DecodeError) as e:print('error:',e,file=sys.stderr);sys.exit(1)
