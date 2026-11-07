# 原版执行与当前C++实现差异

以下均仅阅读源码，没有修改或重建。复现脚本相对项目根目录，Python前缀为 `analysis/.venv/Scripts/python.exe analysis/scripts/`。完整数据见 `t_verify.tsv`、`task5_trials_T*.json`、`task5_edge_trials.json`。

| 优先级 | 操作 / C++入口 | 实际错误与应有行为 | 复现 |
|---|---|---|---|
| 最高 | 80:89～8B / table_subops.cpp | 当前mask为`1<<(index&7)`；原版为`80>>(index&7)`。位0/7/8/15实际存储序列8000/8100/8180/8181，影响BGI.gdb兼容 | truth5_edges.py |
| 最高 | B0:C5 / font_subops.cpp | 目标为0时压所需缓冲字节数，非0写NUL分隔名并压条数；当前两分支都压条数且忽略字符集。实测字符集128为1673字节/94条，不能用94分配容量 | truth5_verify.py --group T2 |
| 最高 | 91:96 / 绑定缺失 | 二进制实际帧127在此停止。原版弹文本替换定义字符串、写替换表，空串与A反斜线B换行都压1；不能用“读排版参数”或仅压0占位 | truth5_verify.py --group T1；obgi_truth5_stdout.txt |
| 高 | 91:99 / system_subops.cpp | 当前压计算得到的scale；原版0/1/65536输入均压成功1，scale分别0/65536/1写全局0052eb78。启动第33条原版1、C++0 | truth5_edges.py；boot_truth.tsv |
| 高 | 主6E / vm/interp.cpp | 当前按长度截断；原版第三操作数是包裹字符。22和ABCDE应输出引号+ABCDE+引号+NUL；0也实际写前导NUL及后面的字符串 | truth5_edges.py |
| 高 | 主6C / vm/interp.cpp | 当前只辨CP932双字节、标点恒0；原版还支持UTF8和真实标点类：あ是3,3042,1,0；、是3,3001,1,1；!标点1，半角ｱ宽0 | truth5_edges.py |
| 高 | 90:11/16 / graphics_subops.cpp、显示槽 | 当前拒绝format>3；原版0..6都可创建。4/5每像素4字节、6每像素6字节。90:16六字段顺序本来正确，错误在支持范围 | truth5_edges.py |
| 高 | 80:81 / system_subops.cpp | 当前保存后固定压0且注释称寄存器无意义；原版虚拟WriteFile成功压1、失败压0 | truth5_verify.py --group T2 |
| 高 | 91:8E / plane_subops.cpp | 当前恒0；原版新建平面压1，并根据文本游标与区域/布局方向动态计算。不能把原版查询值当固定否 | truth5_verify.py --group T2 |
| 中 | 主77 / vm/interp.cpp | 当前固定0；原版参数为对象类别计数选择器，fixture已创建一只精灵时类别0返回1；FFFFFFFF选择器返回FFFFFFFF | truth5_verify.py --group T2 |
| 中 | 主73 / vm/interp.cpp | 当前当注册表查询失败；原版创建键并写InstalledFolder，虚拟OS成功时压1。具体真实注册表策略应由宿主层决定 | truth5_verify.py --group T2 |
| 中 | 主7E / vm/interp.cpp | 当前恒0；原版设置CF_UNICODETEXT剪贴板成功压1。无UI宿主可选失败，但不能当成指令语义 | truth5_verify.py --group T2 |
| 中 | 90:09 / window_subops.cpp | 当前只记录且注释上限未知；原版接受0..FFFF，对FFFFFFFF调用脚本异常，非正常返回0 | truth5_verify.py --group T2 |
| 中 | 80:0D / system_subops.cpp | 当前两值恒7FFFFFFF；原版分别取total/available并独立钳制。1024/256应原样压1024/256；本机实验环境不是两者都达到上限 | truth5_verify.py --group T2 |
| 中 | 81:30 / system_subops.cpp | 正常同步压0这一点无需改成“字节数”。但原版读归档存储的原始片段，C++经res->read可能取解压后数据；原版范围失败样本压3，而C++超过04000000直接使VM失败 | truth5_verify.py --group T2；检查buffer_changes是否为DSC头 |
| 待实现 | T1其他未绑定项 | 80:17、81:0F、80:1C、80:12、80:14、80:18、80:19、80:0F、91:B8、91:BA、90:B9等应依原版语义补齐。81:0F为IsIconic；91:B8为DCIPIconEx，不是普通显示对象创建 | truth5_verify.py --group T1 |

未列入确定错误的项目：B0:C6在本机Arial输出2，与当前值碰巧相同；完整字体类别范围尚未动态穷举。91:8D的1,0,0仅是新平面的默认游标，不代表当前常量实现正确，但尚未录入真实文字绘制后游标非零的对照。81:0E常规1920×1080样本一致；特殊显示模式的原始宽度/变换后宽度使用差异需以JSON及公式进一步核对。

T3/T4很多C++操作目前只保存参数，原版会改对象并通知合成器；本轮并未逐项验证最终像素，故没有把每一个“只记录”条目都宣布为已闭合差异。

## 已验证

以上表中返回值、字节内容、格式范围及错误入口来自原版机器码，OS边界行为详见任务书5执行报告.md。C++代码行为来自当前源码；没有新构建来反推二进制与源码必然一致。

## 静态推断

位序错误对存档兼容、字体容量错误对启动分支的影响是结合实际返回值与C++调用约定的推断；完整游戏后果未运行到标题验证。

## 未知与风险

本文件不是全部请求项的完整差异清单。T3/T4逐字段及像素层仍未完成，S也未到标题；不得把未列出的项理解为一致或已验证。
