"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_docs.py
Apply task-5 evidence to ORIGINAL semantic-table rows. Idempotent corrections;
does not regenerate the old static tables or alter engine source.
"""
import re
from truth5_runtime import ROOT
from truth5_report import SEM

EXTRA={
'80:30':'已验证（原版执行）：依次弹成员名、归档名、目标地址；原版读取/DSC解压后复制至目标，压复制字节数，返回0。三个真实成员为80、4256、464字节；不是载入VM程序块或压布尔。证据：truth5_edges.py。',
'80:35':'已验证（原版执行）：依次弹成员名、归档名；压资源解码后字节数，返回0。autoload/ipl._bp为80；envinfoctrl为4256；fontinfo为464，与80:30一致。证据：truth5_edges.py。',
'80:88':'已验证（原版执行）：依次弹位数、表名字串地址；定义命名位表，压成功布尔，返回0。由真实DCFlag构造器初始化；16位表创建压1，后续89～8B有设置/读取证据；不是全局符号表查找。证据：truth5_edges.py。',
'90:98':'已验证（原版执行）：依次弹图像槽ID数组地址、数量；注册文字字符位图列表，-1表示空项；不压栈。3个-1正常返回；3个不存在槽16383触发原版错误。不是单张“屏幕兼容位图”设置。证据：truth5_edges.py。',
'90:37':'已验证（原版执行；指定样本）：依次弹两个值、对象ID，通过对象虚表+38设置；返回0，无压栈。ID0为根CDspObjBackN，存在且不报句柄错误；普通精灵也正常，FFFFFFFF报错。具体字段/像素作用尚未全部闭合。证据：truth5_verify.py --group T3。',
'91:3E':'已验证（原版执行）：依次弹y偏移、x偏移、从属句柄、主句柄；建立从属关系，保存主对象+13c链中节点+4/+8偏移。主位置变化立即把从属设为主位置+偏移；直接移动从属会反算更新偏移。证据：truth5_edges.py。',
'91:3F':'已验证（原版执行）：依次弹从属句柄、主句柄；解除已建立的从属关系。两者为同一对象或不在关系中的错误分支见三样本。证据：truth5_edges.py / truth5_verify.py --group T3。',
'90:85':'已验证（原版执行；参数2仍未知）：依次弹priority、参数2、alpha、blend、y、x、窗口句柄。区分值样本10/128/64/1/3/2写CDspObjWindow+1c=10、+b4=64、+b0=1、+38=3、+34=2；参数2=128未发现作用，不命名。证据：truth5_edges.py。',
'90:89':'已验证（原版执行）：先弹窗口句柄、再弹输出矩形地址；64×32新平面读出0,0,63,31，右下边包含，压1，返回0。无效句柄脚本异常。证据：truth5_edges.py。'}
EXTRA['91:99']='已验证（原版执行）：弹参数v；计算并写0052eb78：v=0时写0，其余写(v+FFFF)/v；压成功标志1，而不是压计算值，调度返回0。0、1、65536三样本均执行；证据：truth5_edges.py。'

def main():
    corrections=dict(EXTRA)
    for op,sem in SEM.items():
        if ':' in op and op not in corrections:
            corrections[op]='已验证（原版执行；仅列明样本，不外推全部分支）：依次弹 '+sem[0]+'；压入：'+sem[1]+'。'+sem[2]+'。错误/未覆盖：'+sem[3]+'。证据：truth5_verify.py、out/t_verify.tsv。'
    p=ROOT/'子opcode语义表.md';lines=p.read_text(encoding='utf8').splitlines();out=[]
    for line in lines:
        m=re.match(r'\| `([^`]+)` \|',line)
        if m and m[1] in corrections:
            cells=line.split('|');op=m[1]
            if len(cells)==7 and not cells[3].strip().startswith('`004'):
                cells[3]=' '+corrections[op]+' ';cells[5]=' 已验证（原版执行；指定范围），未覆盖部分保持未知 '
                line='|'.join(cells)
            elif len(cells)==5 and cells[2].strip().startswith('`004'):
                cells[3]=' '+corrections[op]+' ';line='|'.join(cells)
            elif len(cells)==7 and op in ('80:88','80:89','80:8A','80:8B','90:98'):
                cells[5]=' 已验证（原版执行）；参数与真实压栈见原条目及t_verify.tsv '
                cells[4]={'80:88':' 1 / 1 / 1 / 0 ','80:89':' 2 / 1 / 1 / 0 ','80:8A':' 3 / 1 / 1 / 0 ','80:8B':' 1 / 2 / 1 / 0 ','90:98':' 1 / 1 / 0 / 0 '}[op]
                line='|'.join(cells)
        out.append(line)
    marker='## 任务书5原版执行修订记录（2026-10-28）'
    text='\n'.join(out)
    if marker not in text:text+='\n\n'+marker+'\n\n已直接更正80:30/35、80:88～8B、90:98、90:37等原条目；位序为每字节高位在前。T1/T2的执行结论均限定到具体样本，T3/T4尚有未解释字段与像素效果。复现、OS桩、未通过的验收见《任务书5执行报告.md》，不得用本次条目升级来宣称完整启动已验证。\n'
    p.write_text(text+'\n',encoding='utf8')
    p=ROOT/'主opcode语义.md';text=p.read_text(encoding='utf8')
    text=text.replace('弹长度、源和目标地址，调用带长度的字符串处理函数；截断和终止符细节待查。','已验证（原版执行）：弹包裹字符、源和目标地址；在两侧写该字符并终止，非截断复制。字符22与ABCDE生成2241424344452200；见truth5_edges.py。')
    text=text.replace('弹字符串地址并调用 `FUN_00435f70`，压结果；查找对象和返回码待查。','已验证（原版执行）：弹字符串地址，写CF_UNICODETEXT剪贴板；虚拟OS成功压1。原版00435f70；见truth5_verify.py --group T2。')
    p.write_text(text,encoding='utf8')
    print('updated original semantic rows and main opcode corrections')
if __name__=='__main__':main()
