# usage: py blend_usage.py  -- scans out/bp_corpus/*/*._bp with bgi_dis.py, for every `90:56` (sprite setup) site
# whose last 3 args (blendMode, alpha, priority) are immediate pushes, tallies blendMode/alpha. Static only.
import subprocess,sys,re,glob,os,collections
root=os.path.dirname(os.path.abspath(__file__)); an=os.path.dirname(root)
imm=re.compile(r'^[0-9a-f]+: (?:[0-9a-f]{2} )+\s*push_i(?:8|16|32)\s+(-?\d+)')
modes=collections.Counter(); alphas=collections.Counter(); nonimm=0; total=0; ex={}
for f in sorted(glob.glob(os.path.join(an,'out','bp_corpus','*','*'))):
    out=subprocess.run([sys.executable,os.path.join(root,'bgi_dis.py'),f,'--count','1000000'],capture_output=True,text=True).stdout.splitlines()
    for i,l in enumerate(out):
        if ' 90 56 ' in l and 'op_90' in l and i>=3:
            total+=1
            m=[imm.match(out[i-k]) for k in (3,2,1)]
            if all(m):
                b,a,p=(int(x.group(1)) for x in m); modes[b]+=1; alphas[a]+=1; ex.setdefault(b,os.path.basename(f))
            else: nonimm+=1
print('sites',total,'non-immediate',nonimm)
print('blend',{hex(k) if k>=0 else k:v for k,v in sorted(modes.items())})
print('alpha',dict(sorted(alphas.items())))
print('example file per mode',{hex(k):v for k,v in ex.items()})
# --- 90:38 SetProperty(objId, propId, a, b): propId 1 = blend mode, 2 = alpha
p1=collections.Counter(); p2=collections.Counter(); props=collections.Counter(); n38=0
for f in sorted(glob.glob(os.path.join(an,'out','bp_corpus','*','*'))):
    out=subprocess.run([sys.executable,os.path.join(root,'bgi_dis.py'),f,'--count','1000000'],capture_output=True,text=True).stdout.splitlines()
    for i,l in enumerate(out):
        if ' 90 38 ' in l and 'op_90' in l and i>=4:
            n38+=1
            m=[imm.match(out[i-k]) for k in (3,2)]
            if m[0]:
                pid=int(m[0].group(1)); props[pid]+=1
                if m[1]:
                    if pid==1: p1[int(m[1].group(1))]+=1
                    if pid==2: p2[int(m[1].group(1))]+=1
print('90:38 sites',n38,'propId (imm)',dict(sorted(props.items())))
print('prop1 blend',{hex(k):v for k,v in sorted(p1.items())}); print('prop2 alpha',dict(sorted(p2.items())))
