"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_verify.py [--group T1|T2|T3|T4]
Independent task-5 harness. Native CThread/CMemoryDX, display manager and root
object constructors; no handler, lookup or manager stubs. Only Host's OS API
substitutions. Results distinguish native errors, missing dependencies and ret.
Raw evidence is task5_trials_<group>.json; never equate a failing call with a
validated normal-path semantic. Addresses from 子opcode表.tsv / handler_reads.tsv.
"""
import argparse, csv, json, re, collections
from truth5_runtime import Host, ROOT, P
from truth5_targets import GROUPS
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import *

def tables():
    result={}
    with (ROOT/'子opcode表.tsv').open(encoding='utf-8-sig') as f:
        for r in csv.DictReader(f,delimiter='\t'):result[r['主opcode']+':'+r['子opcode']]=int(r['处理器VA'],16)
    with (ROOT/'out/handler_reads.tsv').open(encoding='utf8') as f:
        for r in csv.DictReader(f,delimiter='\t'):result[r['opcode'].upper()]=int(r['handler'],16)
    return result

SOURCE=(ROOT/'out/ac2_ML.c').read_text(encoding='utf8')
FUNCS={int(a,16):b for a,b in re.findall(r'// ==== ([0-9a-f]{8}) [^\n]*\n(.*?)(?=// ==== |\Z)',SOURCE,re.S)}

def setup():
    h=Host();h.call(0x47b190);flags=h.alloc(8);h.call(0x450270,flags);h.put(0x5482c0,flags)
    archive=h.alloc(0x33c);h.call(0x407650,archive);h.put(0x5482cc,archive)
    h.display();h.call(0x44acf0,0,0,32,32,1,0)
    t=h.thread();buf=h.get(t+0x4c)+0x1000;h.u.mem_write(buf,b'Arial\0'+bytes(10))
    s=h.get(t+0x18);h.put(s,64);h.put(s+4,32);h.put(t+0x78,2);h.call(0x48cdb0,0,0,t);h.plane=h.get(s)
    h.put(t+0x78,0);h.call(0x48b400,0,0,t);h.sprite=h.get(s)
    for slot in (0,1,2):
        h.u.mem_write(s,P(slot)+P(32)+P(32)+P(3 if slot==2 else 1));h.put(t+0x78,4);h.call(0x488eb0,0,0,t)
    return h,t,buf

def args_for(op,n,h,t,b,i):
    v=[0,1,0xffffffff][i]
    if op in ('80:17','80:0F','81:0F','80:0D','80:81'):return []
    if op=='80:12':
        h.u.mem_write(b,P([0,1,0x10000000][i])+P(0));h.put(0x54870c+24,7);return [b]
    if op=='91:B8':return [h.plane if i<2 else 0xffffffff]
    if op in ('80:14','80:18','80:19','80:1C','90:B9','90:09','90:DD'):return [v]
    if op=='91:96':
        h.u.mem_write(b,b'\0' if i==0 else b'A\\B\n\0');return [b if i<2 else 0xffffffff]
    if op=='91:BA':return [b,0x1234+i]
    if op=='90:05':return [0 if i<2 else 0x10000, v if i<2 else 0]
    if op=='90:20':return [i,0,(60 if i==0 else 30) if i<2 else 0,i,256,0]
    if op=='B0:C5':return [[128,128,255][i],0 if i==0 else b]
    if op=='B0:C6':
        h.u.mem_write(b,(b'Arial' if i<2 else b'__missing_font__')+b'\0');return [b,b+2048]
    if op=='81:0E':return [b if i<2 else 0xffffffff]
    if op=='81:30':
        h.u.mem_write(b,b'ipl._bp\0');h.u.mem_write(b+256,b'autoload.arc\0')
        return [[16,32,0x4000001][i],[0,4,0][i],b,b+256,b+512]
    if op=='90:16':return [[0,1,0xffffffff][i],b]
    if op in ('91:8D','91:8E'):return [h.plane if i<2 else 0xffffffff]
    if op=='80:98':return [0 if i==2 else b,b+32,b+64]
    if op in ('80:89','80:8A','80:8B'):
        # Native DCFlag::definition: shared fixture is created via 80:88 below.
        return {'80:89':[v,3,b], '80:8A':[v,3,0,b], '80:8B':[3 if i<2 else 999,b,b+512]}[op]
    if op in ('6C','6E','73','77','78','7C','7D','7E'):
        if op=='6C':h.u.mem_write(b,[b'A\0','あ'.encode('utf8')+b'\0','、'.encode('utf8')+b'\0'][i])
        if op=='6E':h.u.mem_write(b,b'ABCDE\0');h.u.mem_write(b+512,b'XXXXXXXXXXXX\0')
        return {'6C':[b], '6E':[[0,3,8][i],b,b+512], '73':[b if i<2 else 0xffffffff,b+256,b+512], '77':[v],
                '78':[i%2,b], '7C':[b,b+512], '7D':[0,256,[1,2,0][i],b,i,i,0], '7E':[b]}[op]
    if op=='90:80':return [[32,64,0][i],[64,32,0][i]]
    if op in ('90:40','90:41','90:42','90:45','90:46'):
        return [0 if i==0 else 1]*n if i<2 else [0]*(n-1)+[0xffffffff]
    if op=='90:44':
        h.u.mem_write(b,P(0)+P(1));return [256,b,2 if i<2 else 0]
    if op=='90:48':return [1+i,1+i,0,0,0 if i<2 else 0xffffffff]
    if op=='90:49':return [256+i*256,0,0 if i<2 else 0xffffffff]
    if op=='90:55':return [2,h.sprite if i<2 else 0xffffffff]
    if op in [f'91:{x:02X}' for x in range(0x41,0x4b)]:return [0 if i==0 else 1]*(n-1)+[0 if i<2 else 0xffffffff]
    if op.startswith('90:') or op.startswith('91:') or op.startswith('92:'):
        # Property operands are popped before the object ID in these handlers.
        is_t3=op in GROUPS['T3']
        object_id=(0 if i==0 else h.sprite) if is_t3 else h.plane
        values=[0 if i==0 else 1]*(max(n,1)-1)+[object_id if i<2 else 0xffffffff]
        sequence=re.findall(r'FUN_(0044d2a0|0049d1f0)\(',FUNCS.get(tables().get(op),''))
        for j,callee in enumerate(sequence):
            if callee=='0049d1f0':values[j]=b+j*128 if i<2 else 0xffffffff
        return values
    return [v]*n

def group_for_plane(op):return op.startswith('90:8')

def run_group(group):
    addresses=tables();out=[]
    for op in dict.fromkeys(GROUPS[group]):
        va=addresses.get(op);body=FUNCS.get(va,'')
        n=len(re.findall(r'FUN_0044d2a0\(|FUN_0049d1f0\(',body))
        result={'opcode':op,'handler':f'{va:08X}' if va else None,'trials':[]}
        if not va:result['blocked']='No original dispatch entry';out.append(result);continue
        try:h,t,b=setup()
        except Exception as e:
            result['blocked']='native constructors: '+str(e);out.append(result);continue
        if op in [f'91:{x:02X}' for x in range(0x41,0x4b)]:
            # These operate on background mode 0xC, not mode 5 created by
            # 90:44. The original factory constructs CDspObjBack's subclass.
            h.call(0x446370,h.get(0x5485ec),0,0xc)
        current=None;events=[]
        manager=h.get(0x5485ec)
        multilayer=op in [f'91:{x:02X}' for x in range(0x41,0x4b)]
        objects={('root_CDspObjBackML' if multilayer else 'root_CDspObjBackN'):(h.get(manager+0x50),0x410 if multilayer else 0x154),
                 'sprite_CDspObjSprite':(h.call(0x44b270,manager,0,h.sprite),0x1b4),
                 'plane_CDspObjWindow':(h.get(manager+0x8f4),0x3f8)}
        result['fixture_objects']={name:f'{a:08X}' for name,(a,n) in objects.items()}
        def code(u,a,size,_):
            nonlocal current
            if current is None:return
            sp=u.reg_read(UC_X86_REG_ESP);ecx=u.reg_read(UC_X86_REG_ECX)
            if a==0x44d2a0 and ecx==t:
                ep=h.get(t+0x78) or h.get(t+0x10);current['popped'].append(f'{h.get(h.get(t+0x18)+(ep-1)*4):08X}')
            elif a==0x44d2c0 and ecx==t:current['pushed'].append(f'{h.get(sp+4):08X}')
            elif a in (0x471e50,0x471c20,0x471c40):
                msg=''
                try:msg=h.string(ecx)
                except Exception:pass
                current['native_error']={'function':f'{a:08X}','message':msg,'ecx':f'{ecx:08X}'}
                raise RuntimeError('native script error')
        def write(u,access,a,size,value,_):
            if current is None or 0x20000000<=a<0x20100000:return
            if len(events)<3000:
                try:events.append((a,size,h.k.read(a,size).hex(),f'{value & ((1<<(size*8))-1):0{size*2}x}'))
                except Exception:pass
        for a in (0x44d2a0,0x44d2c0,0x471e50,0x471c20,0x471c40):h.u.hook_add(UC_HOOK_CODE,code,None,a,a)
        if op!='80:81':h.u.hook_add(UC_HOOK_MEM_WRITE,write)
        for i in range(3):
            values=args_for(op,n,h,t,b,i);h.put(t+0x78,len(values));stack=h.get(t+0x18)
            values=[0x08001000+(x-b) if b<=x<b+0x10000 else x for x in values]
            h.u.mem_write(stack,b''.join(P(x) for x in reversed(values)))
            if op=='81:0F':h.win.api('IsIconic',1)(lambda a,i=i:[0,1,2][i])
            if op=='80:0F':h.put(0x54815c,[0,1,0xffffffff][i])
            if op=='80:17':h.put(0x549f04,[0,1,1][i]);h.put(0x52f5bc,[0,1,0][i])
            if op=='78':h.win.api('MessageBoxW',4)(lambda a,i=i:[1,6,7][i])
            if op=='80:0D':
                total,available=[(1024,256),(0x80000000,0x40000000),(0xffffffff,0x80000000)][i]
                h.win.api('GlobalMemoryStatus',1)(lambda a,total=total,available=available:h.u.mem_write(a[0],P(32)+P(50)+P(total)+P(available)+bytes(16)))
            if op=='80:81' and i==2:h.win.api('WriteFile',5)(lambda a:0)
            if op=='81:0E':
                w,hei=[(1920,1080),(5760,1080),(1080,1200)][i]
                h.win.api('IDirect3D9::GetAdapterDisplayMode',3)(lambda a,w=w,hei=hei:h.u.mem_write(a[2],P(w)+P(hei)+P(60)+P(22)) or 0)
            current={'sample':i,'input_pop_order':[f'{x:08X}' for x in values],'popped':[],'pushed':[]}
            events.clear();before=h.k.read(b,4096)
            object_before={name:h.k.read(a,n) for name,(a,n) in objects.items()}
            try:current['return']=f'{h.call(va,0,0,t,count=30000000 if op=="80:81" else 2000000):08X}';current['status']='returned'
            except Exception as e:current['status']='native_error' if 'native_error' in current else 'blocked';current['reason']=str(e);current['eip']=f'{h.u.reg_read(UC_X86_REG_EIP):08X}'
            after=h.k.read(b,4096)
            current['object_deltas']={}
            for name,(a,n) in objects.items():
                previous=object_before[name];now=h.k.read(a,n)
                current['object_deltas'][name]=[{'offset':f'+{j:03X}','before':previous[j:j+4].hex(),'after':now[j:j+4].hex()} for j in range(0,n,4) if previous[j:j+4]!=now[j:j+4]]
            current['root_after']=f'{h.get(manager+0x50):08X}'
            current['buffer_changes']=[{'offset':x,'before':before[x:x+4].hex(),'after':after[x:x+4].hex()} for x in range(0,4096,4) if before[x:x+4]!=after[x:x+4]]
            current['writes']=[{'address':f'{a:08X}','size':s,'before':old,'value_le_numeric':new} for a,s,old,new in events]
            current['writes_truncated']=len(events)>=3000
            result['trials'].append(current);current=None
        out.append(result)
        print(group,op,','.join(x['status'] for x in result['trials']),flush=True)
        (ROOT/f'out/task5_trials_{group}.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
    return out

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--group',choices=list(GROUPS));args=ap.parse_args()
    for group in ([args.group] if args.group else GROUPS):run_group(group)
