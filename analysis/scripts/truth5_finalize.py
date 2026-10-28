"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_finalize.py
Audit completed evidence, add progress and boot limitations, hash provenance.
Does not run the emulator or write anything outside analysis/.
"""
import csv,hashlib,json,pathlib,sys
from truth5_runtime import ROOT,GAME
from truth5_targets import GROUPS

def main():
    status=json.loads((ROOT/'out/boot_native_status.json').read_text(encoding='utf8'))
    boot=list(csv.DictReader((ROOT/'out/boot_truth.tsv').open(encoding='utf8'),delimiter='\t'))
    assert len(boot)==status['completed']+status['partial']
    assert all(r['是否桩']=='否' for r in boot)
    assert sum(bool(r['返回码']) for r in boot)==status['completed']
    audit={'python':sys.version,'native_boot_complete':status['completed'],'native_boot_partial':status['partial'],'native_boot_semantic_stubs':0,'title_reached':False,'groups':{},'sha256':{}}
    for group,ops in GROUPS.items():
        data=json.loads((ROOT/f'out/task5_trials_{group}.json').read_text(encoding='utf8'))
        assert set(r['opcode'] for r in data)==set(ops),(group,'incomplete inventory')
        assert all(len(r['trials'])==3 if r['handler'] else len(r['trials'])==0 for r in data),(group,'missing trials')
        audit['groups'][group]={'requested':len(data),'normal_return_items':sum(any(t['status']=='returned' for t in r['trials']) for r in data),'no_dispatch_entry':sum(not r['handler'] for r in data)}
    verification=list(csv.DictReader((ROOT/'out/t_verify.tsv').open(encoding='utf8'),delimiter='\t'))
    assert len({r['子opcode'] for r in verification})==len(verification)
    for p in [GAME/'ac2_ML.exe',GAME/'BGI.gdb',ROOT/'out/boot_truth.tsv',ROOT/'out/task5_edge_trials.json',*sorted((ROOT/'scripts').glob('truth5_*.py'))]:
        audit['sha256'][str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
    (ROOT/'out/task5_audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf8')
    p=ROOT/'启动路径.md';text=p.read_text(encoding='utf8');marker='## 任务书5：真实启动前缀及阻塞（2026-10-28）'
    section=f'''{marker}

复现：`analysis/.venv/Scripts/python.exe analysis/scripts/truth5_boot.py`。本轮通过原版ARC查找、文件读取、DSC解压、CThread构造/程序装载和0049be00调度器，执行到 **{status['completed']} 条完整子opcode调用**，最后另有一条80:60未完成。`out/boot_truth.tsv`含所需九列，语义桩为0，未完成行返回码留空，不能算完成的真实事件。

原版停止于`sysprg.arc/environ._bp+165`的80:60，最终进入00471c20，消息为“Direct3Dデバイスが作成できませんでした”。直接原因是OS COM桩IDirect3D9::CreateDevice返回8876086a；尚未提供虚拟D3D设备、资源/锁定、异步完成及外层合成更新。没有跳过该指令或填一个成功值。**尚未越过system完整初始化，未达到任务S最低前缀长度/标题验收。**

已经修复一个宿主初始化遗漏：必须执行原版CRT初始化函数004d07af，启用宽printf的legacy wide %s模式。漏掉它时目录被拼成EU；旧探测产物已重跑替换，不能作为原版目录行为证据。另运行了原版WinAPI thunk cache初始化004e54be，恢复emu.py默认跳过的security-cookie检查。

干净环境在此实现为隐藏所有.sud打开、BGI.gdb读取游戏原件、保存仅写BytesIO。本前缀没有观测到年龄提示，也没有到标题前所有消息分支，**无法确认“未满……岁”在正常原版是否出现**；不能把本前缀无提示解释成原版永远不会提示。需要先完成Direct3D宿主再运行后续分支。

| 已观测环境依赖（仅此前缀） | 本次取值及范围 |
|---|---|
| 80:0C / GetLocalTime | 2026-10-28 12:00:00.000，虚拟当地时间 |
| 80:0D / GlobalMemoryStatus | total80000000钳制7FFFFFFF，available40000000 |
| B0:C5字体枚举 | 本机真实GDI；启动字符集134为2172字节、136条；容量/条数分别由原版回调统计 |
| 80:80窗口位置 | 原版恢复BGI.gdb；虚拟1920×1080监视器/1280×720客户区计算为960,540，状态0 |
| 80:2A目录查询 | 真实游戏原路径；最终日志应以重跑后的字符串为准，C++则可能重定向data-dir |
| 81:6F shader创建 | 本宿主无设备，模式1返回0；模式0返回1；不能当真实GPU结果 |

帧时间每轮16ms，帧内不增长。当前所有记录在帧0，未证实真实时间依赖调度/异步线程顺序；记录中的线程1来自空根节点先占ID0。C++使用线程0且ip为指令结束的绝对偏移，逐行比较须先归一化。

对照：`out/boot_diff.md`、`out/obgi_truth5_stderr.txt`。原二进制C++运行器实际帧127停在未实现91:96；未重新构建。91:99在启动第33条已找到确定的返回值错误：应压成功1，C++压计算scale。字体容量B0:C5也是确定错误；各差异的完整首因链仍未闭合。

详细边界见《任务书5执行报告.md》。

'''
    if marker not in text:
        pos=text.rfind('\n## 已验证')
        text=text[:pos]+'\n'+section+text[pos:] if pos>=0 else text+'\n'+section
    p.write_text(text,encoding='utf8')
    p=ROOT/'进度.md';text=p.read_text(encoding='utf8')
    entries=[
      ('任务书5 S：原版启动前缀',f'原版真实调度器/解释器/处理器得到{status["completed"]}条完整调用，语义桩0；真实GDB恢复；C++日志捕获帧127的91:96缺失。已记录boot_truth.tsv、boot_diff.md和OS调用。','80:60缺D3D设备，未到标题、未完成system初始化；年龄提示及首因链未知，S未通过。'),
      ('任务书5 T1：原版构造现场','14/14项有原版返回样本，含输入、IsIconic、文本替换、DCIPIconEx、显示等待对象；非法指针/句柄单列异常或访问故障。','91:BA成功描述结构、90:B9有效删除等分支未全面覆盖；不能把所有返回样本视为完整语义。'),
      ('任务书5 T2：原版值与结构','23/23项有原版返回样本；纠正位序高位在前、6E包裹、字体容量、80:81成功1失败0、格式4～6、91:8E等；另找到91:99始终压1。','主7D最终像素、完整Unicode/编码歧义、异步文件读取及错误条件仍有缺口。'),
      ('任务书5 T3/T4：字段与范围',f'T3实际40入口中{audit["groups"]["T3"]["normal_return_items"]}项有正常返回，T4实际23入口中{audit["groups"]["T4"]["normal_return_items"]}项有正常返回；8个请求编号无处理入口。新增对象偏移变化表，验证从属位置反算、包含右下边矩形。','逐字段命名与最终像素未完成，90:85参数2未知；背景/多层模式不应当作普通对象属性。'),
      ('任务书5：CRT环境修正与交付','原版004d07af修复宽printf初始化，避免EU错误路径；恢复真实security-cookie检查并重跑。原语义表条目直接更正；报告、差异、脚本和audit均已落盘。','整份任务书未通过验收；S的D3D宿主及T的未闭合字段仍需继续。')]
    for label,result,pending in entries:
        if f'| {label} |' not in text:text+=f'\n| 2026-10-28 | {label} | {result} | {pending} |'
    p.write_text(text+'\n',encoding='utf8')
    print(json.dumps(audit['groups'],ensure_ascii=False));print('PASS inventories, trial counts, unique TSV, native prefix and provenance')
if __name__=='__main__':main()
