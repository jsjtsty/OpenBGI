"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_edges.py
Targeted native fixtures for 80:88..8B and bitmap formats 0..6 / 90:16.
Also records 6C/6E character cases. No semantic helper is substituted.
"""
import json
from truth5_verify import setup,tables,P,ROOT
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *

def main():
    h,t,b=setup();addresses=tables();results=[]
    def call(op,args):
        values=[0x08001000+(x-b) if b<=x<b+0x10000 else x for x in args]
        s=h.get(t+0x18);h.put(t+0x78,len(values));h.u.mem_write(s,b''.join(P(x) for x in reversed(values)))
        item={'opcode':op,'pop_order':[f'{x:08X}' for x in values]};results.append(item)
        def err(u,a,size,_):
            item['error_address']=f'{a:08X}'
            try:item['error']=h.string(u.reg_read(UC_X86_REG_ECX))
            except Exception:item['error']='unreadable'
            raise RuntimeError('native script error')
        hook=h.u.hook_add(UC_HOOK_CODE,err,None,0x471e50,0x471e50)
        try:
            item['eax']=f'{h.call(addresses[op],0,0,t):08X}'
            item['stack']=[f'{h.get(s+i*4):08X}' for i in range(h.get(t+0x78))]
        except Exception as e:item['blocked']=str(e);item['eip']=f'{h.u.reg_read(UC_X86_REG_EIP):08X}'
        h.u.hook_del(hook);return item
    h.u.mem_write(b,b'task5bits\0');call('80:88',[16,b])
    for index in (0,7,8,15,16):
        r=call('80:89',[1,index,b]);node=h.get(h.get(0x5482c0)+4);r['packed_bits']=h.k.read(h.get(node+12),2).hex()
        r=call('80:8B',[index,b,b+512]);r['output']=h.k.read(b+512,4).hex()
    for length in (0,1,16,17):call('80:8A',[length,1,0,b])
    node=h.get(h.get(0x5482c0)+4)
    if node:results.append({'flag_node':h.k.read(node,20).hex(),'packed_bits':h.k.read(h.get(node+12),2).hex()})
    h.u.mem_write(b,b'unknown\0');call('80:89',[1,0,b]);call('80:8B',[0,b,b+512]);call('80:8A',[1,1,0,b])
    for value in (0,1,65536):
        r=call('91:99',[value]);r['stored_scale']=f'{h.get(0x52eb78):08X}'
        assert r['stack']==['00000001']
    for fmt in range(7):
        create=call('90:11',[fmt,3,5,fmt]);create['format']=fmt
        h.u.mem_write(b,b'\xcc'*24);r=call('90:16',[fmt,b]);r['format']=fmt;r['descriptor']=[f'{h.get(b+i*4):08X}' for i in range(6)]
    for arc,member in [('autoload.arc','ipl._bp'),('sysprg.arc','envinfoctrl._bp'),('sysprg.arc','fontinfo._bp')]:
        h.u.mem_write(b,member.encode()+b'\0');h.u.mem_write(b+256,arc.encode()+b'\0')
        call('80:35',[b,b+256]);call('80:30',[b,b+256,b+4096])
    for count in (3,0,3):
        h.u.mem_write(b,P(0xffffffff)*3);call('90:98',[b,count])
    h.u.mem_write(b,P(0x3fff)*3);call('90:98',[b,3])
    for ch in ['A','!',' ','あ','、','。','（','）','「','」','ｱ','é','𠀀']:
        h.u.mem_write(b,ch.encode('utf8')+b'\0');r=call('6C',[b]);r['utf8']=ch.encode('utf8').hex();r['text']=ch
    punctuation=[]
    for off in range(0,256,2):
        cp=int.from_bytes(h.k.read(0x5143d0+off,2),'little')
        if not cp:break
        punctuation.append(cp)
    assert all(h.call(0x4347a0,cp)==1 for cp in punctuation)
    assert all(h.call(0x4347a0,cp)==0 for cp in [0,0x41,0x300c,0x20000])
    widths={f'{cp:X}':h.call(0x4a4b40,cp) for cp in [0,0x7f,0x80,0xff60,0xff61,0xff9f,0xffa0,0x20000]}
    assert list(widths.values())==[0,0,1,1,0,0,1,1]
    results.append({'helper':'6C_width_and_punctuation','punctuation_codepoints':punctuation,'width_boundaries':widths})
    for ch in (0,0x22,0x300c,0x110000):
        h.u.mem_write(b,b'ABCDE\0');h.u.mem_write(b+512,b'\xcc'*32);r=call('6E',[ch,b,b+512]);r['bytes']=h.k.read(b+512,24).hex()
    manager=h.get(0x5485ec);root=h.get(manager+0x50);plane=h.get(manager+0x8f4)
    sprite=h.call(0x44b270,manager,0,h.sprite)
    r=call('90:85',[10,128,64,1,3,2,h.plane]);r['fields']={f'{off:03X}':f'{h.get(plane+off):08X}' for off in [0x34,0x38,0xb0,0xb4,0xb8,0x1c]}
    r=call('90:89',[h.plane,b]);r['rect']=[f'{h.get(b+j*4):08X}' for j in range(4)]
    def positions():return {'root':[h.get(root+0x34),h.get(root+0x38)],'plane':[h.get(plane+0x34),h.get(plane+0x38)]}
    for op,args in [('90:33',[7,5,h.plane]),('90:33',[20,10,0]),('90:3C',[h.plane,0]),('90:33',[40,30,0]),('90:33',[9,8,h.plane]),('90:33',[60,50,0])]:
        r=call(op,args);r['positions']=positions()
        node=h.get(root+0x13c)
        if node:r['child_node']=[f'{h.get(node+j*4):08X}' for j in range(4)]
    results.append({'plane_header_154':[f'{h.get(plane+0x154+i*4):08X}' for i in range(6)]})
    for op,args in [('91:3E',[7,5,h.plane,h.sprite]),('90:33',[20,10,h.sprite]),('90:33',[40,30,h.sprite]),('90:33',[9,8,h.plane]),('90:33',[60,50,h.sprite]),('91:3F',[h.plane,h.sprite])]:
        r=call(op,args);r['positions']={'master':[h.get(sprite+0x34),h.get(sprite+0x38)],'slave':[h.get(plane+0x34),h.get(plane+0x38)]}
        node=h.get(sprite+0x13c)
        if node:r['child_node']=[f'{h.get(node+j*4):08X}' for j in range(4)]
    format6=next(x for x in results if x.get('opcode')=='90:16' and x.get('format')==6)
    assert format6['descriptor']==['00000000','0000001E','00000005','00000003','00000006','00000006']
    resource35=[x['stack'] for x in results if x.get('opcode')=='80:35']
    resource30=[x['stack'] for x in results if x.get('opcode')=='80:30']
    assert resource35==resource30==[['00000050'],['000010A0'],['000001D0']]
    quoted=next(x for x in results if x.get('opcode')=='6E' and x['pop_order'][0]=='00000022')
    assert quoted['bytes'].startswith('2241424344452200')
    (ROOT/'out/task5_edge_trials.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(results,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
