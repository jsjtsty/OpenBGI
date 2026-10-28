# Usage: analysis/.venv/Scripts/python.exe analysis/scripts/build_subop_effects.py
# Collect probe observations, never promote finite samples into exhaustive signatures.
import collections,csv,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
def read(path):
 with path.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f,delimiter='\t'))
samples=collections.defaultdict(list)
for tier in ('A1','A2','B','C'):
 path=ROOT/f'out/subop_probe_{tier}.tsv'
 if path.exists():
  for r in read(path):samples[r['subop']].append(r)
candidate_path=ROOT/'out/subop_static_candidates.tsv'
candidates={r['subop']:r for r in read(candidate_path)} if candidate_path.exists() else {}
# Manually cross-checked signatures: immediate handler effect, not later wait completion.
# Each specifies evidence script; '*' in conditional field describes later behavior.
known={
 '80:00':(1,0,'0','设置随机种子；随机数库与锁打桩','verify_random_effects.py'),
 '80:01':(0,1,'0','压CRT随机数；随机数库与锁打桩','verify_random_effects.py'),
 '80:02':(1,1,'0','上限有符号且不大于0时压0；正数由随机数生成','verify_random_effects.py'),
 '80:12':(1,1,'0','无','verify_subop_simple.py'), '80:1C':(1,1,'0','无','verify_subop_simple.py'),
 '80:13':(0,1,'0','无','verify_subop_simple.py'), '80:14':(1,0,'0','无','verify_subop_simple.py'),
 '80:15':(1,0,'0','无','verify_subop_simple.py'), '80:50':(1,0,'1','无','verify_subop_simple.py'),
 '90:01':(1,0,'0','无','verify_subop_simple.py'), '90:07':(1,0,'0','无','verify_subop_simple.py'),
 '90:32':(2,0,'0','异常路径不返回','verify_subop_simple.py'), '90:33':(3,0,'0','异常路径不返回','verify_subop_simple.py'),
 '90:38':(4,0,'0','异常路径不返回','verify_subop_simple.py'), '90:56':(7,0,'0','异常路径不返回','verify_subop_simple.py'),
 '90:30':(2,0,'0','异常路径不返回','verify_display_90_54.py'), '90:34':(2,0,'0','异常路径不返回','verify_display_90_54.py'),
 '90:54':(2,0,'0','异常路径不返回','verify_display_90_54.py'),
 '80:5A':(0,1,'2','剩余时间>0时安装等待对象；否则不安装','verify_wait_timing.py'),
 '80:5C':(3,0,'2','后续等待完成时由对象另压1个结果','verify_wait_timing.py'),
 '80:5F':(0,0,'1','无','verify_dispatch_returns.py'),
 '80:6A':(0,0,'6','无','probe_subop_effects.py；00498190机器码'),
 '80:FD':(0,1,'0','无','probe_subop_effects.py；FUN_00499d60'),
 '80:36':(1,0,'0','无','probe_subop_effects.py；FUN_00497620'),
 '80:52':(1,0,'0','无','probe_subop_effects.py；FUN_00497d20'),
 '80:74':(1,0,'0','无','probe_subop_effects.py；FUN_004982f0'),
 'B0:82':(3,1,'0','两种对话框分支各压一次；用户选择决定结果值','verify_message_effect.py（地址解析、对话框打桩）'),
 '81:60':(3,1,'0','索引>=16压1；索引有效但宽或高为0压2；成功记录并压0','verify_resolution_table.py'),
 '81:6F':(1,1,'0','模式0/2/3成功；模式1/4取决于shader创建；其他模式失败，成功更新模式','verify_shader_thread_ids.py'),
 '80:35':(2,1,'0','先弹成员名再弹归档名；压资源读取结果长度或0，查找及读取依赖宿主','verify_resource_size.py'),
 '81:62':(1,1,'0','输入影响返回值，不影响正常路径弹压','probe_subop_effects.py；FUN_0049ae10'),
 '81:63':(1,1,'0','输入影响返回值，不影响正常路径弹压','probe_subop_effects.py；FUN_0049ae30'),
 '90:94':(1,0,'0','无','probe_subop_effects.py；FUN_0048d3c0'),
 '90:9C':(1,0,'0','无','probe_subop_effects.py；FUN_0048d570'),
 '90:9F':(1,0,'0','无','probe_subop_effects.py；FUN_0048d6d0'),
 '91:00':(1,0,'0','无','probe_subop_effects.py；FUN_0048f640'),
 '91:0D':(1,0,'0','无','probe_subop_effects.py；FUN_0048f840'),
 '91:99':(1,1,'0','结果取决于输入；正常路径弹压固定','probe_subop_effects.py；FUN_004935c0'),
 '80:0C':(1,0,'0','无效地址走错误路径','verify_system_effects.py（Win32 API 打桩）'),
 '80:0D':(0,2,'0','输入由宿主内存状态决定；两个值各上限0x7fffffff','verify_system_effects.py（Win32 API 打桩）'),
 '80:5D':(1,0,'0','非零设置线程过滤，零关闭','probe_subop_effects.py；FUN_00497f40'),
 '80:5E':(1,0,'3','切换目标 ID 不存在时本轮结束','probe_subop_effects.py；verify_scheduler.py'),
 '90:02':(1,0,'0','参数大于1000时抛脚本错误','probe_subop_effects.py；FUN_00488960'),
 '80:80':(0,3,'0','无文件或SDC解码长度不符时前两项未初始化，状态分别压1或2','verify_gdb_buffer.py（文件/SDC失败打桩）'),
 '80:82':(3,0,'0','长度0、偏移/长度超范围时脚本错误','verify_gdb_buffer.py（合成1MiB缓冲）'),
 '80:83':(3,0,'0','长度0、偏移/长度超范围时脚本错误','verify_gdb_buffer.py（合成1MiB缓冲）'),
 '80:A0':(1,1,'0','队列空返回0；非空复制12字节并返回1','verify_system_queue.py（锁和free打桩）'),
 '90:10':(3,0,'0,2','缓存命中或同步返回0；异步安装等待对象并返回2；异常不正常返回','verify_bitmap_load_effect.py（资源管理与分配器打桩）'),
 '90:22':(7,0,'2','有效路径返回2；后续控制对象可向栈压结果，异常不正常返回','verify_bitmap_load_effect.py（控制对象帮助函数打桩）'),
 '80:40':(2,1,'0','载入失败或无空间抛错，不正常返回','verify_program_effects.py（文件、分配器和载入打桩）'),
 '80:44':(5,1,'0','创建失败的脚本错误路径未覆盖','verify_program_effects.py（线程创建和地址解析打桩）'),
 '80:41':(0,1,'0','根线程卸载最后模块时抛脚本错误；无模块时压0x80000001','probe_subop_effects.py；FUN_004979c0/0044cee0'),
 '80:46':(0,1,'0','当前线程ID由线程对象读取','probe_subop_effects.py；FUN_00497a60'),
 '90:11':(4,0,'0','参数或资源错误抛异常','verify_bitmap_ops.py（图像管理器打桩）'),
 '90:12':(1,1,'0','返回值由释放是否成功决定','verify_bitmap_ops.py（图像管理器打桩）'),
 '90:13':(2,0,'0','无效槽走错误路径','verify_bitmap_ops.py（图像管理器打桩）'),
 '90:18':(6,0,'0','模式、透明度或槽无效时抛异常','verify_bitmap_ops.py（图像管理器打桩）'),
 '90:1F':(6,0,'0','来源、目标或尺寸错误时抛异常','verify_bitmap_ops.py（图像管理器打桩）'),
 'A0:08':(2,0,'0','通道或音量超界时脚本错误','verify_audio_effect.py（音频后端打桩）'),
 'A0:09':(2,0,'0','通道或音量超界时脚本错误','verify_audio_effect.py（音频后端打桩）'),
 '80:70':(1,1,'0','指数0..12时分配并清零；大于12走错误路径','verify_global_alloc.py（分配器与释放打桩）'),
 '80:3D':(2,1,'0','selector=0主路径；selector=1且备用路径非空时成功，其余返回0','verify_base_paths.py（路径数据合成）'),
 '80:58':(1,0,'0','线程截止时间=引擎时钟低32位+参数','verify_config_effects.py（时钟打桩）'),
 '80:67':(1,0,'0','参数大于4时脚本错误','verify_config_effects.py'),
 'C0:1F':(1,0,'0','参数大于2时脚本错误','verify_config_effects.py'),
 '90:95':(2,0,'0','第二个弹出值必须为1..256','verify_config_effects.py'),
 '90:96':(2,0,'0','第二个弹出值必须为1..256','verify_config_effects.py'),
}
static_a1={
 '80:1B':(2,0,'未知','两个栈弹出发生在正常/错误分支前；void 处理器的 EAX 调度码未确认'),
 '80:20':(1,1,'0','正常路径一弹一压；资源失败走脚本错误'),
 '80:21':(1,1,'未知','弹出后按输入调用分支，正常路径压布尔值；void 处理器的 EAX 未确认'),
 '80:25':(5,1,'0','五个参数先弹出；正常路径压一次结果'),
 '80:2A':(1,1,'0','路径地址弹出后压文件属性布尔值'),
 '80:34':(2,1,'0','两地址参数弹出后压比较结果'),
 '80:37':(1,0,'0','弹出地址并执行设置'),
 '80:38':(2,1,'0','弹出两个地址并压一次转换结果'),
 '80:60':(3,0,'0','三参数先弹出，再校验并设置输入状态'),
 '80:62':(2,0,'0','两参数弹出后执行输入查询'),
 '80:66':(1,0,'0','弹出标题文本地址并设置窗口文本'),
 '80:68':(1,0,'0','弹出一个整数并执行设置'),
 '80:D0':(2,1,'0','弹出一个值和一个地址，压一次结果'),
 '80:DB':(2,1,'0','弹出一个值和一个地址，压一次结果'),
 '80:E8':(1,0,'0','弹出地址并执行操作'),
 '81:0E':(1,0,'0','弹出数组地址并执行初始化'),
 '81:21':(2,0,'0','弹出两个地址并执行复制/转换'),
 '81:64':(2,0,'0','弹出两个参数并执行设置'),
 '81:D8':(2,1,'0','弹出地址与值，压布尔结果'),
 '81:DA':(3,1,'0','两条结果分支都压一次长度/零'),
 '81:E9':(3,0,'0','弹出值及两个地址并执行写入'),
 '90:03':(1,0,'未知','弹出参数并验证范围；void 处理器的 EAX 调度码未确认'),
 '90:06':(2,0,'0','弹出两个显示参数'),
 '90:08':(1,0,'0','弹出一个显示参数'),
 '90:0C':(2,0,'0','弹出两个显示参数'),
 '90:0D':(1,0,'未知','弹出并解析资源名；void 处理器的 EAX 调度码未确认'),
 '90:4C':(2,0,'0','弹出两个显示参数'),
 '91:0E':(5,0,'未知','五个参数先弹出再调用文本布局；void 处理器的 EAX 调度码未确认'),
 '91:1F':(2,0,'未知','两参数先弹出；void 处理器的 EAX 调度码未确认'),
 '91:98':(6,0,'未知','六参数先弹出再执行文本配置；void 处理器的 EAX 调度码未确认'),
 '91:9A':(2,0,'未知','两参数先弹出再执行字体设置；void 处理器的 EAX 调度码未确认'),
 'A0:20':(3,0,'2','弹三参数，安装音频等待对象并返回调度码 2'),
 'B0:05':(1,0,'0','弹一个参数并设置音频状态'),
 'B0:C1':(2,1,'0','弹两个参数并压一次查询结果'),
 'B0:C5':(2,1,'0','弹两个参数并压一次查询结果'),
 'B0:C6':(2,1,'0','弹两个参数并压一次查询结果'),
 'B0:C7':(2,0,'0','弹两个参数并执行设置'),
}
evidence_rows={r['opcode']:r for r in read(ROOT/'out/subop_evidence.tsv')}
static_a2_path=ROOT/'out/subop_static_candidates_a2.tsv'
static_a2={r['subop']:r for r in read(static_a2_path)} if static_a2_path.exists() else {}
static_paths_path=ROOT/'out/subop_static_stack_paths.tsv'
static_paths={r['subop']:r for r in read(static_paths_path)} if static_paths_path.exists() else {}
static_variants_path=ROOT/'out/subop_static_stack_variants.tsv'
static_variants={r['subop']:r for r in read(static_variants_path)} if static_variants_path.exists() else {}
out=ROOT/'out/subop_effects.tsv';stats=collections.Counter()
conflicts=[]
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','tier','handler','pops','pushes','返回码集合','立即数','是否条件性','依据','置信度','正常样本数','正常样本弹压','正常样本EAX','失败或未完成样本','立即数读取样本'))
 for r in read(ROOT/'out/subop_tiers.tsv'):
  sub=r['subop'];probes=samples[sub];good=[x for x in probes if x['status']=='returned'];bad=[x for x in probes if x['status']!='returned']
  pairs=sorted({(int(x['pops_observed']),int(x['pushes_observed'])) for x in good});returns=sorted({x['eax_if_returned'] for x in good});fetch=sorted({int(x['immediate_reads_observed']) for x in probes});good_fetch=sorted({int(x['immediate_reads_observed']) for x in good})
  if sub in known and pairs:
   expected=known[sub][:2]
   if any(pair!=expected for pair in pairs):
    conflicts.append((sub,f'{expected[0]}/{expected[1]}',';'.join(f'{p}/{q}' for p,q in pairs),'正常样本动态弹压与手工记录不一致；需核对分支、线程初始化与嵌套调用'))
  if sub in known:
   pop,push,ret,condition,evidence=known[sub]
   confidence='已验证：指定路径；异常路径及未覆盖状态仍需核对';stats['known']+=1
  elif len(good)==4 and len(pairs)==1 and len(good_fetch)==1 and good_fetch[0]>=0:
   pop,push=pairs[0];ret=','.join(str(int(x,16)) for x in returns)
   condition='四种合成栈模式下正常返回的路径弹压一致；未观察到的状态分支不覆盖'
   evidence='Unicorn：四种参数模式；真实处理器；弹压钩子与线程 IP 差分'
   confidence='已验证：四组指定路径；全路径及副作用未穷举';stats['measured']+=1
  elif sub in static_a1:
   pop,push,ret,condition=static_a1[sub]
   evidence='ac2_ML.c 处理器逐项核对；线程栈操作在正常路径上的静态顺序'
   confidence='静态推断：处理器正常路径；异常路径及 EAX 未确认';stats['static_a1']+=1
  elif sub in static_a2:
   c=static_a2[sub];pop,push,ret=c['pops'],c['pushes'],c['返回码集合']
   condition=c['限制'];evidence=c['依据']
   confidence='静态推断：A2 直线处理器的直接栈调用';stats['static_a2']+=1
  elif sub in static_paths:
   c=static_paths[sub];pop,push,ret=c['pops'],c['pushes'],c['返回码集合']
   condition=c['限制'];evidence=c['依据']
   confidence='静态推断：处理器 CFG 正常返回路径';stats['static_paths']+=1
  elif sub in static_variants and r['tier']=='C':
   c=static_variants[sub];pairs=[tuple(map(int,pair.split('/'))) for pair in c['弹压路径集合'].split(';')]
   pop='|'.join(map(str,sorted({p for p,_ in pairs})));push='|'.join(map(str,sorted({q for _,q in pairs})));ret=''
   condition='CFG 正常返回路径弹压为 '+c['弹压路径集合']
   evidence=c['依据'];confidence='静态推断：C 档条件性弹压路径';stats['static_variants']+=1
  elif len(good)>0 and len(pairs)==1 and len(good_fetch)==1 and good_fetch[0]>=0:
   pop,push=pairs[0];ret=','.join(str(int(x,16)) for x in returns)
   condition=f'{len(good)}/4 组正常样本弹压一致；错误/未完成分支和宿主状态未覆盖'
   evidence='Unicorn：真实处理器；弹压钩子与线程 IP 差分'
   confidence='已验证：观察到的正常路径；条件分支未穷举';stats['measured_partial']+=1
  elif sub in candidates:
   c=candidates[sub];pop=c['candidate_pops'];push=c['candidate_pushes'];ret=c.get('static_normal_return','')
   condition='未知：被调函数和异常路径未穷举'
   evidence='直线处理器静态计数与四次Unicorn正常样本一致；subop_static_candidates.tsv'
   confidence='静态推断：不可视为全路径已验证';stats['candidate']+=1
  elif not r['handler']:
   pop=push=ret='';condition='初始表空项，执行报错；运行时修改未知';evidence='初始分发表';confidence='已验证：初始表空项';stats['null']+=1
  else:
   pop=push=ret='';condition='未知：样本不足以穷举分支';evidence='Unicorn 样本' if probes else '未测';confidence='未知：观察值不等于完整语义';stats['unknown']+=1
  successful_fetch=good_fetch
  if successful_fetch and all(x>=0 for x in successful_fetch): immediate=','.join(map(str,successful_fetch))
  elif sub in static_paths: immediate=static_paths[sub]['立即数']
  elif evidence_rows.get(sub,{}).get('immediate_reader_refs')=='0': immediate='0（静态：未见直接读取器调用）'
  else: immediate='未知'
  w.writerow((sub,r['tier'],r['handler'],pop,push,ret,immediate,condition,evidence,confidence,len(good),','.join(f'{x}/{y}' for x,y in pairs),','.join(returns),len(bad),','.join(map(str,fetch))))
print('rows',sum(stats.values()),dict(stats),'output',out)
with (ROOT/'out/subop_effect_conflicts.tsv').open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('subop','记录值_pops/pushes','Unicorn观察_pops/pushes','原因待核'));w.writerows(conflicts)
print('conflicts',len(conflicts),'output',ROOT/'out/subop_effect_conflicts.tsv')
