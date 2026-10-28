"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_report.py
Summarize saved native evidence without promoting failed probes to verification.
Regenerates out/t_verify.tsv, out/boot_diff.md and out/task5_coverage.json.
"""
import csv,json,re,difflib,collections
from truth5_runtime import ROOT
from truth5_targets import GROUPS

SEM={
'80:17':('无参数','受输入激活/跳过许可等状态控制的布尔值','读取 00549f04、0052f5bc 等；不是固定 0','无参数范围错误；仅覆盖无输入及指定状态'),
'81:0F':('无参数','IsIconic(HWND) 原样值','查询主窗口是否最小化；OS 返回 0/1/2 时原样压 0/1/2','没有脚本参数；窗口状态依赖 OS'),
'80:1C':('逻辑输入位掩码','映射输入状态的累加','按原映射表调用 0047bd70；本轮覆盖 0/1/FFFFFFFF 的无输入环境','不限制掩码只取有效映射位；有按键的全部组合未覆盖'),
'80:12':('指向零结尾 u32 输入编号数组的脚本地址','数组各编号状态之和','编号 1 状态 7 时压 7；不是布尔 OR','编号 10000000 导致原版越界读取；未见边界检查；无返回码'),
'80:14':('跳过输入许可值','无','原样写 0052f5bc；0/1/FFFFFFFF 均接受','无 0/1 范围限制'),
'80:18':('输入优先级','无','形成 (v<<16)|FFFF，注册两张输入优先级链并刷新','0/1/FFFFFFFF 正常；只保留低16位参与标记'),
'80:19':('输入优先级','无','形成 (v<<16)|FFFF，移除两张输入优先级链并刷新','0/1/FFFFFFFF 正常'),
'80:0F':('无参数','0054815c 原样值','窗口/应用激活状态；本轮设置 0/1/FFFFFFFF 后原样返回','无参数错误'),
'91:96':('文本替换定义字符串地址','指定样本 1','按反斜线分隔原文/替换文本、换行分隔条目，写 005381c0 内的替换表；不是读取排版参数','空串正常；A\\B 换行样本正常；FFFFFFFF 指针触发原版脚本错误；缺反斜线的非空串可不前进'),
'91:B8':('窗口平面句柄','注册号 1、2 等','原版构造 DCIPIconEx(窗口对象) 并登记；此号是控制对象号','无效窗口句柄在 00420910 空指针读取，不能假定压0'),
'91:BA':('描述结构地址；控制对象注册号','样本均为 1','原版查 DCIPIconEx 再解析描述；1=注册号不存在','本轮只覆盖不存在注册号；有效描述结构的成功分支未验证'),
'90:B9':('控制对象注册号','0=未找到；成功删除路径见静态函数 0047a3f0','查 0054866c 链表，找到则析构并删节点','本轮 0/1/FFFFFFFF 均未找到；成功删除未在该三样本中覆盖'),
'90:05':('优先级上界；优先级下界','无','写显示管理器范围；两参数分别经过范围检查','0、1 接受；65536 触发脚本异常；上限 FFFF'),
'90:20':('优先级；参数B；帧率；参数D；混合量(0..256)；对象句柄','无','安装 CProc 对象运动等待对象；正常调度码 2','帧率0 原版异常；正常样本帧率60；中间两参数完整含义仍待验证'),
'80:81':('无参数','成功1；写文件失败样本见 task5_trials_T2.json','原版序列化、压缩并写 BGI.gdb；写入只落内存虚拟文件','文件写失败由 OS 桩提供；压值来自原处理器'),
'B0:C5':('字符集 BYTE；目标地址(0表示只算容量)','目标0=所有字体名含NUL的总字节数；目标非0=枚举条数','本机真实 GDI 枚举；原版回调写 NUL 分隔字体名','字符集由 BYTE 截断，不能把所有字符集统一为四个日文字体'),
'B0:C6':('字体名字串地址；输出地址','找到1，缺名0','输出 LOGFONT.lfPitchAndFamily & 3；Arial 本机为2','Arial两次/不存在名一次；别的字体类值未穷举'),
'80:0D':('无参数','totalPhys、availPhys，分别 min(value,7FFFFFFF)','来自 GlobalMemoryStatus，不是两个固定上限','三个 OS 物理内存配置；无脚本参数'),
'81:30':('读取字节数；文件偏移；成员名；归档名；目标地址','原版状态：成功0；不满足读取范围时样本3（同步）','读取存储成员的部分字节，未自动 DSC 解压；不是返回读取长度','字节数大于04000000时压3；不存在文件/异步等分支未穷举'),
'81:0E':('输出地址','无','查询 D3D 显示模式后写宽、高；超宽屏减半宽，特殊竖屏减半高；以原始宽为分母','非法地址触发原版脚本异常'),
'90:16':('图像槽；输出结构地址','有槽1，无槽0','六u32顺序：像素指针(强制0)、pitch、w、h、format、bpp；格式0..6全部执行成功','失败也将输出第一字段清0；其余字段不可假定被清'),
'91:8D':('窗口平面句柄','成功标志、文本游标X、文本游标Y','读 CDspObjWindow +390/+394；新建平面为1,0,0','无效句柄抛脚本异常'),
'91:8E':('窗口平面句柄','当前游标是否到行末的布尔值','新建平面压1；不是固定0；00432220结合游标、区域与布局方向判断','无效句柄抛脚本异常'),
'80:89':('置位值；位索引；表名字串','状态0成功/1缺表/2索引越界','高位在前：位0写80，位7写01，掩码80>>(index&7)','索引>=位数为2'),
'80:8A':('区间长度；置位值；起始位；表名字串','0成功/1缺表/2起点越界/3区间无效','区间置位或清位；长度范围1..65536且不可超过表终点','长度0、超过剩余位数压3'),
'80:8B':('位索引；表名字串；输出地址','状态0成功/1缺表/2索引越界','把位值0/1写输出地址；压栈的是状态而不是位','缺表/越界不写输出，保留旧值'),
'6C':('字符串地址','字节数、首字符值、宽度类、标点类','支持UTF8；A=>1,41,0,0；あ=>3,3042,1,0；、=>3,3001,1,1；!标点1；半角ｱ宽度0','完整字符分类表仍需静态整理；CP932和UTF8分支不可混用'),
'6E':('包裹字符；源字符串地址；目标地址','无','当前模式用字符低8位包在整个源串两侧并写NUL；不是截断复制','字符0产生前导NUL；UTF8模式另有按码点编码路径（静态）'),
'73':('安装路径；产品名；注册表子键名称（对应反向传递需参见00480960）','本轮 OS虚拟注册成功时观测值见JSON','RegCreateKeyExW/RegSetValueExW 写 InstalledFolder；不是注册表查找','真实注册表不写；原版机器码只验证虚拟OS样本'),
'77':('对象类别编号','该类对象数量；未知类别FFFFFFFF','0044b140：类别0精灵、5窗口等；固定0会漏掉已创建对象','有效选择器0..7、10、11；其他返回FFFFFFFF'),
'78':('对话框模式；消息字符串地址','系统结果==IDYES(6) 时1，否则0','模式0选1124，其余1024；调用OS消息框；OS分别返回1/6/7，原处理器压0/1/0','系统回答来自OS桩，不等同于真实窗口交互'),
'7C':('两个字体选择对话框字符串地址','虚拟对话框取消时0','先枚举字符集80字体，再打开字体选择对话框','这里只验证取消；成功选择未覆盖'),
'7D':('混合标志；字体编号；参数3(1..1024)；文本地址；y；x；图像槽（其余名称待核对）','没有压栈返回值','绘制到图像槽；有效第三参数1/2正常返回，0触发原版错误；最终像素仍未对照','原版检查图像槽、字体与第三个弹出参数；错误路径尚未完整覆盖'),
'7E':('复制文本字符串地址','剪贴板成功1','原版 GlobalAlloc/Lock、OpenClipboard、SetClipboardData(CF_UNICODETEXT)','只写虚拟剪贴板，不能固定压0'),
'90:09':('优先级上界','无','设置显示范围并通知重绘','范围0..FFFF；FFFFFFFF原版异常'),
'90:DD':('新标志','旧标志','交换0052ec40；与输入窗口控件刷新相关，原版调用00470bb0','不做0/1限制，原样保留'),
'80:98':('参数B；参数A；输出注册号地址','状态0成功/2任一参数为0','参数A/B非0则分配链表项，00548648自增并写输出地址','本轮零参数与非零参数都有执行；后续消费此注册项的意义待查')}
SEM.update({
'90:80':('高度；宽度','窗口句柄B0000000起','原构造CDspObjWindow；小宽(<32)/高(<20)按32倍；实际尺寸宽1..8192、高1..32768','0尺寸及超过16个窗口走原版错误；本轮覆盖0尺寸'),
'90:85':('priority；参数2(作用未知)；alpha；blend；y；x；窗口句柄','无','区分值样本写Window+1c/+b4/+b0/+38/+34；参数2没有证实像素作用','priority<=FFFF、alpha类<=256、blend为有限集合；无效句柄报错'),
'90:89':('窗口句柄；矩形输出地址','有效查询1','64x32平面为0,0,63,31，包含右下边','无效句柄报错；见truth5_edges.py'),
'91:3E':('y偏移；x偏移；从属句柄；主句柄','无','主+13c节点存从属指针与偏移；移动主立即主位置+偏移；直接移动从属反算偏移','自从属/无效句柄报错；有效关系见truth5_edges.py'),
'91:3F':('从属句柄；主句柄','无','从主+13c链解除关系；有效解除见truth5_edges.py','不在关系中及无效句柄报错')})

def main():
    coverage={};allrows=[];fieldrows=[]
    for group,ops in GROUPS.items():
        data=json.loads((ROOT/f'out/task5_trials_{group}.json').read_text(encoding='utf8'))
        by={r['opcode']:r for r in data};counts=collections.Counter()
        for op in ops:
            r=by[op];trials=r['trials'];returned=[t for t in trials if t['status']=='returned'];errors=[t for t in trials if t['status']=='native_error']
            status='正常返回样本' if returned else ('仅执行错误分支' if errors else '无法完成执行')
            if not r['handler']:status='原分发表无入口'
            counts[status]+=1
            sem=SEM.get(op,('实测弹出值见JSON；各字段命名尚未完成','见三样本 pushed 列','原版内存写事件见 writes；像素影响未验证','；'.join(dict.fromkeys(t.get('native_error',{}).get('message',t.get('reason','')) for t in trials if t['status']!='returned')) or '未覆盖错误路径'))
            pops=sem[0]+'；实测 '+json.dumps([t['popped'] for t in trials],ensure_ascii=False)
            pushes=sem[1]+'；实测 '+json.dumps([t['pushed'] for t in trials],ensure_ascii=False)
            evidence=f'执行：{status}，{len(returned)}/3返回，{len(errors)}/3脚本异常；task5_trials_{group}.json；truth5_verify.py --group {group}；VA={r["handler"]}'
            offsets=collections.defaultdict(set)
            for trial in returned:
                for obj,changes in trial.get('object_deltas',{}).items():
                    for change in changes:
                        offsets[obj].add(change['offset']);fieldrows.append([op,trial['sample'],obj,change['offset'],change['before'],change['after'],trial.get('root_after','')])
            side=sem[2]
            if offsets:side+='；fixture对象字节变化：'+'；'.join(obj+':'+','.join(sorted(items)) for obj,items in offsets.items())+'（根指针替换时只表示旧对象变化；字段用途及像素效果不外推）'
            diff='见 t_diff.md；未列出不代表一致，未完成逐字段对比'
            allrows.append([op,pops,pushes,side,sem[3],evidence,diff])
        coverage[group]=dict(counts)
    with (ROOT/'out/t_verify.tsv').open('w',encoding='utf8',newline='') as f:
        unique={}
        for row in allrows:unique.setdefault(row[0],row)
        w=csv.writer(f,delimiter='\t');w.writerow(['子opcode','弹出顺序（每个参数的含义）','压入','副作用','错误条件','证据（执行/静态）','与 C++ 实现的差异（见下）']);w.writerows(unique.values())
    with (ROOT/'out/t_object_fields.tsv').open('w',encoding='utf8',newline='') as f:
        w=csv.writer(f,delimiter='\t');w.writerow(['opcode','sample','fixture_object','offset','before_le_bytes','after_le_bytes','root_pointer_after']);w.writerows(fieldrows)
    (ROOT/'out/task5_coverage.json').write_text(json.dumps(coverage,ensure_ascii=False,indent=2),encoding='utf8')
    native=list(csv.DictReader((ROOT/'out/boot_truth.tsv').open(encoding='utf8'),delimiter='\t'))
    raw=(ROOT/'out/obgi_truth5_stderr.txt').read_text(encoding='utf8',errors='replace')
    cpp=[]
    for line in raw.splitlines():
        m=re.match(r't(\d+) ip=([\dA-Fa-f]+) ([\dA-Fa-f]{2}:[\dA-Fa-f]{2}) ->(.*)',line)
        if m:cpp.append({'line':line,'op':m[3].upper(),'values':[int(v,16) for v in m[4].split()]})
    complete=[r for r in native if r['返回码']]
    diffs=[]
    matcher=difflib.SequenceMatcher(a=[r['子opcode'] for r in complete],b=[r['op'] for r in cpp],autojunk=False)
    for tag,a0,a1,b0,b1 in matcher.get_opcodes():
        if tag=='equal':
            for ai,bi in zip(range(a0,a1),range(b0,b1)):
                r=complete[ai];vals=[int(x[:8],16) for x in json.loads(r['压入的值（按压入顺序）'])]
                if vals==cpp[bi]['values']:continue
                op=r['子opcode']
                why={'80:0D':'OS内存状态不同：本实验可用物理内存40000000，C++固定7FFFFFFF。',
                     '91:99':'C++错误地压计算出的scale；原版始终压成功1。原版对0/1/65536的边界执行另见truth5_edges.py，计算值写0052eb78。',
                     '80:2A':'目录环境不同：C++会创建/重定向data-dir，原版OS桩查询游戏原路径；目录是否已存在不同，导致后续80:28创建目录分支不同。',
                     'B0:C5':'本机GDI枚举及容量/条数分支，与C++固定字体集合并统一压条数不同；会影响分配容量和后续字体选择。',
                     'B0:C6':'字体集合/字符集及lfPitchAndFamily不同；需要按字体枚举成功情况解释。',
                     '80:20':'两套内存分配器和逻辑句柄不同；地址不同本身不构成语义错误。',
                     '80:40':'原版装载偏移受此前分支与模块大小影响；C++与本机字体环境的路径已不同。',
                     '80:41':'已加载模块数受此前分支影响；不能把这个次生结果当作首因。',
                     '80:80':'窗口位置由虚拟显示模式/客户区计算，C++保存的坐标策略不同。'}.get(op,'此操作在相同opcode对齐下结果不同；当前两条路径已分叉，根因未证实，禁止视为已定位的实现错误。')
                diffs.append((ai+1,bi+1,r,cpp[bi],why))
        else:
            if a0<len(complete) and b0<len(cpp):
                why='目录存在性返回不同（原版0、C++1），原版额外执行80:28创建目录。' if complete[a0]['子opcode']=='80:28' else '此前返回结果/字体枚举已存在差异；具体区间根因尚未闭合。'
                diffs.append((a0+1,b0+1,complete[a0],cpp[b0],f'控制流区间{tag}：原版 {a1-a0} 条、C++ {b1-b0} 条；'+why+'仅opcode序列对齐不能证明这些位置是同一次调用。'))
    lines=['# 启动对照：部分序列，未通过任务S验收','',
      f'原版完整调用 {len(complete)} 条，最后另有一条未完成调用；C++捕获 {len(cpp)} 条。原版在帧0的80:60触发Direct3D设备创建失败，未到标题。',
      '原版 CThread 的空根节点消耗ID0，所以脚本ID1；C++脚本ID0。C++ ip记录的是指令结束位置，而本表是模块内指令开始位置；还需减两字节并扣除模块装载偏移。它们不是程序错误的直接证据。',
      '下表由opcode序列对齐生成，含返回值差异与首个不匹配区间。存在明确标注的未闭合原因，因此本文件不宣称满足“所有差异都有原因”的验收。',
      '', '| 原版行 | C++行 | 原版调用/压栈 | C++原始行 | 原因及证据限制 |','|---|---|---|---|---|']
    for ai,bi,r,c,why in diffs[:200]:
        label=r['程序（归档/成员）']+'+'+r['ip']+' '+r['子opcode']+' '+r['压入的值（按压入顺序）']
        lines.append(f'| {ai} | {bi} | {label.replace("|","/")} | `{c["line"]}` | {why} |')
    lines+=['','最早可能改变后续分支的差异：B0:C5 的缓冲区容量/条数语义与字体集合；其次是 B0:C6 查找成功与字体类别。80:0D早于它们出现，但本环境内存值差异的具体分支作用未闭合。线程编号和地址分配差异应先规范化。',
      '原版80:60的失败直接来自本实验 IDirect3D9::CreateDevice 返回 D3DERR_NOTAVAILABLE；不能归因于C++，也不能据此推断正常原版会停止。',
      '复现：truth5_boot.py；OBGI_LIVE=1 obgi_run.exe <game> --frames 3000 --data-dir <analysis/out/obgi_truth5_data>；truth5_report.py。C++日志stdout显示帧127停于未实现91:96。',
      '', '## 已验证','真实原版调用与C++日志保存在同目录，可复核行号和原始值。','', '## 静态推断','字体容量错误会影响缓冲区分配；完整首因链还需闭合。','', '## 未知与风险','未到标题；不能提供前200个全部已解释的首次分歧，也不能确认年龄提示的真实可达性。']
    (ROOT/'out/boot_diff.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print(json.dumps(coverage,ensure_ascii=False))
if __name__=='__main__':main()
