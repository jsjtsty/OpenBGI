# A/B/C tiering of native sub-opcodes from static evidence + boot trace. usage: subop_tiers.py [trace]
import csv,re,sys
r=lambda p:list(csv.DictReader(open(p,encoding='utf8'),delimiter='\t'))
table=re.findall(r'^\| `([0-9A-F]{2}:[0-9A-F]{2})` \| \d+ \| `([0-9A-F]{8})`',open('子opcode语义表.md',encoding='utf8').read(),re.M)
handler=dict(table)
P={f"{x['main_opcode']}:{x['sub_opcode']}".upper():x for x in r('out/subop_usage.tsv')}
cg={x['subop']:x for x in r('out/bs_subop_callgraph.tsv')}
bu={x['native_subop']:x for x in r('out/bs_subop_usage.tsv')}
tr=set(re.findall(r'^\S+ ([0-9A-F]{2}:[0-9A-F]{2}) ep=',open(sys.argv[1] if len(sys.argv)>1 else 'out/vm_trace_resume.txt',encoding='utf8').read(),re.M))
rows=[];cnt={}
for k in sorted(set(handler)|set(P)):
    inb=k in tr;sr=k in cg;sd=k in bu
    t='A1' if inb else 'A2' if (sr or sd) else 'B' if k in P else 'C'
    cnt[t]=cnt.get(t,0)+1
    p=P.get(k,{})
    rows.append([k,t,handler.get(k,''),p.get('static_sites',0),p.get('program_count',0),int(inb),cg[k]['candidate_compiled_sites'] if sr else 0,bu[k]['distinct_compiled_sites_with_subop_in_handler'] if sd else 0])
w=csv.writer(open('out/subop_tiers.tsv','w',encoding='utf8',newline=''),delimiter='\t')
w.writerow('subop tier handler static_sites_in_bp programs_in_bp in_boot_trace story_reach_sites story_direct_sites'.split());w.writerows(rows)
print(len(rows),cnt)
