# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/analyze_subop_stack_paths.py
# Enumerates normal return paths in each still-unknown handler's native CFG.
# Only direct thread stack helper calls are counted; transitive callee effects
# are deliberately excluded and every result remains static inference.
import csv,pathlib,struct
import capstone
ROOT=pathlib.Path(__file__).resolve().parents[1]
def rows(path):
 with path.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
exe=pathlib.Path(r'E:\Games\Amairo Chocolate 2\ac2_ML.exe').read_bytes()
pe=struct.unpack_from('<I',exe,0x3c)[0];num=struct.unpack_from('<H',exe,pe+6)[0];opt=struct.unpack_from('<H',exe,pe+20)[0]
secs=[struct.unpack_from('<IIII',exe,pe+24+opt+i*40+8) for i in range(num)]
def fileoff(va):
 rva=va-0x400000
 for vs,va0,rs,ra in secs:
  if va0<=rva<va0+min(vs,rs):return ra+rva-va0
 return None
sizes={int(r['addr'],16):int(r['size']) for r in rows(ROOT/'out/ac2_ML.functions.tsv')}
tiers={r['subop']:r for r in rows(ROOT/'out/subop_tiers.tsv')}
effects={r['subop']:r for r in rows(ROOT/'out/subop_effects.tsv')}
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
md.detail=False
pop_targets={0x44d2a0,0x49d1f0};push_targets={0x44d2c0}
imm_width={0x44d180:1,0x44d1a0:2,0x44d1c0:4}
found=[];variants=[]
for sub,row in tiers.items():
 if not row['handler']:continue
 if effects[sub]['pops']!='' and not effects[sub]['置信度'].startswith('静态推断：处理器 CFG'):continue
 va=int(row['handler'],16);off=fileoff(va);size=sizes.get(va)
 if off is None or not size:continue
 ins=list(md.disasm(exe[off:off+size],va))
 if not ins:continue
 at={i.address:n for n,i in enumerate(ins)};states={(0,0,0,0)};work=[(0,0,0,0)];returns=set();bad=False;steps=0
 while work:
  idx,pops,pushes,imm=work.pop();steps+=1
  if steps>20000 or len(states)>20000:bad=True;break
  if idx<0 or idx>=len(ins):bad=True;break
  i=ins[idx];mn=i.mnemonic
  if mn.startswith('loop'):
   bad=True;break
  if mn.startswith('ret'):
   returns.add((pops,pushes,imm));continue
  if mn=='jmp':
   try:target=int(i.op_str.split()[0],16)
   except ValueError:bad=True;break
   nxt=at.get(target)
   if nxt is None or nxt<=idx:bad=True;break
   s=(nxt,pops,pushes,imm)
   if s not in states:states.add(s);work.append(s)
   continue
  if mn.startswith('j'):
   try:target=int(i.op_str.split()[0],16)
   except ValueError:bad=True;break
   nxt=at.get(target)
   if nxt is None or nxt<=idx or idx+1>=len(ins):bad=True;break
   for ni in (nxt,idx+1):
    s=(ni,pops,pushes,imm)
    if s not in states:states.add(s);work.append(s)
   continue
  if mn=='call':
   try:target=int(i.op_str.split()[0],16)
   except ValueError:target=-1
   if target in pop_targets:pops+=1
   elif target in push_targets:pushes+=1
   elif target in imm_width:imm+=imm_width[target]
   elif target==0x44d1e0:imm=-1
  if idx+1>=len(ins):bad=True;break
  s=(idx+1,pops,pushes,imm)
  if s not in states:states.add(s);work.append(s)
 if bad or not returns:continue
 stack_pairs=sorted({(p,q) for p,q,_ in returns})
 imm_values=sorted({v for _,_,v in returns})
 if len(stack_pairs)==1:
  pops,pushes=stack_pairs[0]
  imm=imm_values[0] if len(imm_values)==1 else -1
  found.append((sub,row['tier'],row['handler'],pops,pushes,'未知',('未知' if imm<0 else imm),'静态推断：所有正常返回路径的直接栈辅助调用计数相同','不包含被调函数的间接栈效果；错误、挂起与异常路径未覆盖'))
 elif row['tier']=='C':
  variants.append((sub,row['handler'],';'.join(f'{p}/{q}' for p,q in stack_pairs),'静态推断：正常返回 CFG 路径弹压分支不同','C 档只报告栈效果；需逐函数确认分支条件与被调函数影响'))
out=ROOT/'out/subop_static_stack_paths.tsv'
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','tier','handler','pops','pushes','返回码集合','立即数','依据','限制'));w.writerows(found)
print('static normal-path effects',len(found),'output',out)
vout=ROOT/'out/subop_static_stack_variants.tsv'
with vout.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','handler','弹压路径集合','依据','限制'));w.writerows(variants)
print('conditional C effects',len(variants),'output',vout)
