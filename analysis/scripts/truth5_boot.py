"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_boot.py
Native scheduler + all original opcode handlers, no semantic stubs.
Start with original archive read/decompression, CThread construction/loading.
Outputs boot_truth.tsv and boot_native_status.json; stops at first unsupported
dependency. A partial row has an empty return code and is NOT a completed call.
"""
import csv, json, traceback, hashlib, argparse
from truth5_runtime import Host, ROOT
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import *

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--dx-window-fallback',action='store_true');args=ap.parse_args()
    prefix='boot_dx' if args.dx_window_fallback else 'boot'
    h=Host(); rows=[]; pending=[]; fault=[]; stage='initialization'; frame=0; hooked=set()
    h.recycling=args.dx_window_fallback
    corpus={};module_cache={}
    with (ROOT/'out/bp_corpus_v2/index.tsv').open(encoding='utf8') as f:
        for r in csv.DictReader(f,delimiter='\t'):
            data=(ROOT.parent/r['output']).read_bytes();offset=int.from_bytes(data[:4],'little');length=int.from_bytes(data[4:8],'little')
            corpus[hashlib.sha256(data[offset:offset+length]).digest()]=r['archive']+'/'+r['member']
    subs={}
    with (ROOT/'子opcode表.tsv').open(encoding='utf-8-sig') as f:
        for r in csv.DictReader(f,delimiter='\t'):
            subs.setdefault(int(r['处理器VA'],16),[]).append(r['主opcode']+':'+r['子opcode'])
    def val(v,t,decode=False):
        s=f'{v:08X}'
        if not decode or not v:return s
        try:
            tag=v>>26
            if tag not in (0,1,2):return s
            p=(h.get(t+0x2c)+(v&0x3ffffff)) if tag==1 else ((h.get(t+0x4c)+(v&0x3ffffff)) if tag==2 else h.get(0x5485e0)+v)
            text=h.string(p)
            if text and len(text)<=1024 and all(c.isprintable() for c in text):s+='='+json.dumps(text,ensure_ascii=False)
        except Exception:pass
        return s
    def module(t,ip):
        node=h.get(t+0x30)
        while node:
            start=h.get(node+8);size=h.get(node+4)
            if start<=ip<start+size:
                key=(t,node,start,size)
                if key not in module_cache:
                    digest=hashlib.sha256(h.k.read(h.get(t+0x2c)+start,size)).digest()
                    module_cache[key]=corpus.get(digest,'unresolved/'+h.string(h.get(node)))
                return module_cache[key],ip-start
            node=h.get(node+12)
        return '?',ip
    def hook(u,a,size,_):
        sp=u.reg_read(UC_X86_REG_ESP)
        if pending and a==pending[-1]['ret'] and sp==pending[-1]['sp']+4:
            p=pending.pop();p['row'][7]=f'{u.reg_read(UC_X86_REG_EAX):08X}'
            if p['row'][0]%1000==0:
                count=p['row'][0];stub_count=sum(r[8].startswith('是') for r in rows[:count])
                checkpoint=f'任务书5 S {prefix} 前{count}条'
                progress=ROOT/'进度.md';body=progress.read_text(encoding='utf8')
                if checkpoint not in body:
                    with progress.open('a',encoding='utf8') as f:f.write(f'\n| 2026-10-28 | {checkpoint} | 原版返回调用序列已到{count}条，包含{stub_count}条明确标记的显示初始化替代；当前{p["row"][2]}。 | 尚未据此认证标题可交互；继续追踪。 |\n')
                print(f'{prefix}: {count} returned calls, {stub_count} explicit display stubs',flush=True)
        if a in subs:
            t=h.get(sp+4);ip=h.get(t+0x80)-2;name,relative=module(t,ip)
            code=h.k.read(h.get(t+0x2c)+ip,2).hex(':').upper()
            row=[len(rows)+1,h.get(t+8),name,f'{relative:08X}',code,[],[],'','否']
            rows.append(row);pending.append({'row':row,'t':t,'ret':h.get(sp),'sp':sp})
            if code in ('80:60','81:64') and args.dx_window_fallback:
                # Explicit task-S exception: only window/device creation is
                # replaced. Never silently promote this row to original truth.
                ep=h.get(t+0x78);capacity=h.get(t+0x10);stack=h.get(t+0x18)
                for _ in range(3 if code=='80:60' else 2):
                    ep=(ep-1)%capacity;row[5].append(f'{h.get(stack+ep*4):08X}')
                if code=='81:64' and row[5]!=['000002D0','00000500']:raise RuntimeError('DX fallback refuses an unmodelled resize')
                h.put(t+0x78,ep);row[7]='00000000';row[8]='是：'+code+' Direct3D窗口/尺寸替代，framebuffer由原版0044acf0初始化'
                pending.pop();u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_ESP,sp+4);u.reg_write(UC_X86_REG_EIP,h.get(sp));return
            if h.get(sp) not in hooked:
                hooked.add(h.get(sp));u.hook_add(UC_HOOK_CODE,hook,None,h.get(sp),h.get(sp))
        if pending:
            p=pending[-1];t=p['t']
            if a==0x49d0a0 and u.reg_read(UC_X86_REG_EDX)==t and p['row'][5]:
                v=u.reg_read(UC_X86_REG_ECX)
                if p['row'][5][-1][:8]==f'{v:08X}':p['row'][5][-1]=val(v,t,True)
            if a==0x44d2a0 and u.reg_read(UC_X86_REG_ECX)==t:
                n=h.get(t+0x78) or h.get(t+0x10);v=h.get(h.get(t+0x18)+(n-1)*4)
                p['row'][5].append(val(v,t))
            if a==0x44d2c0 and u.reg_read(UC_X86_REG_ECX)==t:p['row'][6].append(val(h.get(sp+4),t))
        if a in (0x471e50,0x471c20,0x471c40):
            try:msg=h.string(u.reg_read(UC_X86_REG_ECX))
            except Exception:msg='unreadable error string'
            raise RuntimeError(f'native error function {a:08X}: '+msg)
    def invalid(u,access,address,size,value,_):
        sp=u.reg_read(UC_X86_REG_ESP)
        fault.append({'access':access,'address':f'{address:08X}','size':size,
                      'stack':h.k.read(sp,96).hex(),'ecx':f'{u.reg_read(UC_X86_REG_ECX):08X}'});return False
    # Do not single-step the DSC decoder (00470f00..00471300) in Python.
    # Native code there still executes, but isn't an opcode/stack event.
    h.u.hook_add(UC_HOOK_CODE,hook,None,0x480000,0x49ffff)
    for a in {a for a in subs if a<0x480000}|{0x44d2a0,0x44d2c0,0x471e50,0x471c20,0x471c40}:
        hooked.add(a);h.u.hook_add(UC_HOOK_CODE,hook,None,a,a)
    h.u.hook_add(UC_HOOK_MEM_INVALID,invalid)
    status={}
    try:
        archive=h.alloc(0x33c);h.call(0x407650,archive);h.put(0x5482cc,archive)
        h.call(0x47b190)
        flags=h.alloc(8);h.call(0x450270,flags);h.put(0x5482c0,flags)
        stage='native display constructors';h.display()
        if args.dx_window_fallback:
            # Dimensions seen before the strict native 80:60 failed at D3D.
            h.call(0x46b4d0,1280,720);h.call(0x44acf0,0,0,1280,720,1,0)
        root=h.thread(True)
        arc=h.k.alloc(b'autoload.arc\0');name=h.k.alloc(b'ipl._bp\0')
        stage='native resource read + create autoload thread'
        h.call(0x49c120,arc,name,4096,0x800000,0x400000,root)
        for frame in range(3000):
            h.now=frame*16;stage='native scheduler'
            h.call(0x49be00,root,count=300000000)
        status['outcome']='frame limit; interactive title not certified'
    except Exception as e:
        status['outcome']='BLOCKED';status['error']=str(e);status['traceback']=traceback.format_exc()
    finally:
        status.update(stage=stage,frame=frame,clock_ms=h.now,eip=f'{h.u.reg_read(UC_X86_REG_EIP):08X}',
            completed=sum(bool(r[7]) for r in rows),partial=sum(not bool(r[7]) for r in rows),
            fault=fault,imports=h.win.log[-30:],messages=h.messages,semantic_stubs=sorted({r[4] for r in rows if r[8].startswith('是')}))
        with (ROOT/f'out/{prefix}_truth.tsv').open('w',encoding='utf8',newline='') as f:
            w=csv.writer(f,delimiter='\t');w.writerow(['序号','线程','程序（归档/成员）','ip','子opcode','弹出的参数（按弹出顺序）','压入的值（按压入顺序）','返回码','是否桩'])
            for r in rows:w.writerow(r[:5]+[json.dumps(r[5],ensure_ascii=False),json.dumps(r[6],ensure_ascii=False)]+r[7:])
        (ROOT/f'out/{prefix}_native_status.json').write_text(json.dumps(status,ensure_ascii=False,indent=2),encoding='utf8')
        (ROOT/f'out/{prefix}_os_calls.json').write_text(json.dumps(h.win.log,ensure_ascii=False,indent=1),encoding='utf8')
        print(json.dumps(status,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
