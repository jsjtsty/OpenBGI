# Codex 任务书 2：第二轮研究（仍然只研究，不写正式代码）

沿用 `Codex任务书.md` 的第 0、1、4 节（分工、硬性规则、交付要求），以下只写增量。**不要创建或修改 `src\`，不要建 CMake 工程。**

## 0. 自上轮以来的变化（先读）

Claude 接手后新增了这些文档，**已覆盖的内容不要重做**：`显示对象属性.md`、`文字绘制.md`、`音频与子opcode_A0.md`、`输入.md`、`视频.md`、`存档与全局数据.md`、`渲染管线.md`、`混合公式.md`。读任务前先扫一遍相关的那份。

新增的**动态验证手段**（强烈建议用，静态读反编译会漏细节，上轮已经抓出过 4 处读错）：

- Python 环境：`analysis\.venv`（uv 管理，已装 unicorn、capstone、numpy）。运行用 `analysis\.venv\Scripts\python.exe`。需要新包用 `uv pip install --python analysis\.venv\Scripts\python.exe <包>`，不要用系统 Python 装包。
- `analysis\scripts\emu.py`：把 `ac2_ML.exe` 映射进 Unicorn，可直接调用 exe 里的函数（`Kernel().call(va, ecx, edx, *栈参数)`）。用法示例见 `verify_blend.py`（混合内核）、`verify_dsc.py`（DSC 解码，已用它确认 800 个成员与原引擎逐字节一致）。遇到系统调用/导入函数需要自己打桩：把 IAT 项指向 `ret N` 的小桩，或对 `malloc/free` 之类用 `UC_HOOK_CODE` 钩子（参考 `verify_cbg.py`）。
- 反汇编单个函数：`analysis\.venv\Scripts\python.exe analysis\scripts\disasm_fn.py <VA十六进制> [字节数]`。
- 结论仍按「已验证 / 静态推断 / 未知」标注。**用 Unicorn 与原引擎对照通过的，可标「已验证（与原引擎对照）」。**

分工提示：CompressedBG 解码器的 Unicorn 对照（`verify_cbg.py`）Claude 正在做，**任务 H 之外不要碰 CBG**。

## 1. 任务（按优先级；彼此独立，可分给多个 Codex 会话并行，各自只改自己的文档）

### 任务 G：子 opcode 语义表续作（最大的一块，可再拆成 G1–G4）

现状：`子opcode语义表.md` 有 1047 行，但约 870 行含「未知/待查/未分析」。`out\subop_usage.tsv` 是脚本里静态调用频次。

做什么：

1. 先统计：按 `subop_usage.tsv` 从高到低，列出「已有语义」「语义不完整」「完全未分析」三类，写在文档开头的覆盖率表里（调用次数加权，不是条目数）。
2. 按下面的拆分依次补全，**每个子 opcode** 记录：弹出参数个数及含义、返回值、立即数宽度、涉及的类（RTTI 名）、副作用、是否会挂起线程。先做占调用次数 95% 的部分。
   - G1：主 opcode `0x80`、`0x81`（及 `0x7F`）
   - G2：`0x90`（显示对象，量最大；与 `显示对象属性.md` 对照，别重复，已有的属性号只要引用）
   - G3：`0x91`、`0x92`、`0xB0`
   - G4：`0xA0`（与 `音频与子opcode_A0.md` 对照）、`0xC0`、`0xD0`、`0xE0`，以及主 opcode `0x50–0x5F`、`0x77–0x7E`
3. 线程等待协议：`FUN_0044d3b0`、`FUN_0044d3e0` 的挂起条件和恢复条件，写成一个独立小节（上轮没有完成）。
4. 用脚本佐证：`framework_symbols.tsv` 的符号名、`bp_corpus` 里调用点的上下文（前后几条指令、参数是否常量）。常量参数能直接给出语义（例如某子 opcode 总是被传 0/1，多半是开关）。
5. **验证**：对纯计算或仅读写简单结构的子 opcode，尽量用 Unicorn 直接调用其处理函数，对照你写下的语义。做不到的保持「静态推断」。

产出：更新 `子opcode语义表.md`（G1–G4 若并行，各自只改自己负责的主 opcode 段落，并写进度日志，不要整体重排文件）。

### 任务 H：`.bm`（BF_Movie）格式

`视频.md` 里 `.bm` 是未解部分。游戏里有 `ef_love.bm`、`ef_shuchu.bm` 等；`moviemngr`、`moviemngrbm` 两个系统脚本使用它。

做什么：

1. 头部各字段、帧索引、每帧数据的编码（是图像序列、差分、还是特效参数？是否带 alpha？）。入口线索：`FUN_004b2290` 构造 `BF_Movie_______` 描述并调用 `FUN_004aeb90`；`DCMovieRenderer`、`CMovieManager` 相关类。
2. 写 `analysis\scripts\decode_bm.py`，把一个 `.bm` 解成 PNG 序列（输出到 `analysis\out\bm_frames\`，不入仓库产物）。
3. 能用 Unicorn 对照原引擎的帧解码函数就对照，否则写明哪些环节只是静态推断。
4. 同时确认 `data02280.arc` 的 `.mpg` 是标准 MPEG-PS 还是有特殊处理（用 ffprobe 或手读包头），并写明游戏里 `.mpg` 与 `.bm` 各自在什么场合播放。

产出：`analysis\BM格式.md`。

### 任务 I：故事脚本（`_bs`）实际怎么调用引擎

目前只对系统脚本（`_bp`）做过静态统计；故事脚本（`_bs`，在 `data*.arc` 里，`BurikoCompiledScriptVer1.00` 开头）才是游戏内容主体，却没统计过。

做什么：

1. 用 `scripts\bs_dis.py`（或 `parse_bs.py`）扫描全部 `_bs`，统计：各子 opcode 的实际调用频次；对 `90:56`（显示对象混合）、`90:38`、`A0` 类（音频）、`0xC0/0xD0/0xE0` 中带**立即数**的调用，列出出现过的参数取值分布（混合模式值、渐变时间、BGM/SE 编号范围、wipe 名等）。
2. 回答：故事脚本里是否出现 `混合公式.md` 里未静态见过的混合模式？是否有视频播放（`.mpg`/`.bm`）调用点，传什么资源名？
3. 列出故事脚本里**自定义函数名**（`_bs` 带符号表，如 `WaitBS`、`_DrawBG`）及其出现次数，推断每个函数封装了哪几条子 opcode 序列。这是后面做「高层调用优化」或排错的路标。
4. 汇总出一份「引擎最小必需子 opcode 集合」：覆盖全部 `_bp` 与 `_bs` 实际调用到的子 opcode 清单，按重要性分档（A：必须；B：偶尔；C：从未出现）。

产出：`analysis\故事脚本用法.md` 加 `out\bs_subop_usage.tsv`。

### 任务 J：VM 参考解释器原型（研究用，放 `scripts\`）

目的：验证 `VM分析.md` 对字节码语义的理解，也给 Claude 之后写 C++ VM 提供逐条可对照的参考行为。**这是实验脚本，不是引擎。**

做什么：

1. 在 `analysis\scripts\vm_proto.py` 写一个 Python 解释器：加载 `ipl._bp`（入口脚本），按 `VM分析.md` 的栈模型与 opcode 表执行；所有 796 个子 opcode 先用**记录型桩**实现（打印子 opcode、参数、预期返回值，必要时按任务 G 的语义返回合理值）。
2. 目标：从 `ipl._bp` 起，能跑过启动阶段、按顺序加载 `sysprg` 的各 `_bp`，一直到第一次出现需要等待输入/定时器的挂起。输出一份**子 opcode 调用轨迹**（`out\vm_trace_boot.txt`）。
3. 把轨迹与原引擎对照：用 Unicorn 无法整机跑引擎，所以只对照**可观察事实**——比如加载顺序是否与 `ipl._bp` 的程序列表一致、跳转/调用/返回、栈平衡。发现 `VM分析.md` 与实际执行不符之处，写进该文档的修订记录。
4. 如果解释器跑偏（栈不平衡、解码出非法 opcode），这是找出 VM 文档错误的最好线索——请把每个这样的案例逐一追查并记录。

产出：`vm_proto.py`、`out\vm_trace_boot.txt`、`VM分析.md` 的修订。

### 任务 K：收尾小项（顺手做）

1. `.sud` 存档里截图缩略图的编码（`存档与全局数据.md` 里标为未解）：定位 `UserData` 里的样本，找到写缩略图的函数，解出像素格式和压缩方式，写 `scripts\sud_thumb.py` 输出 PNG。
2. `BGI.hvl` 的内容与用途；`SGCnf*`、`_bsi` 配置文件的字段含义（`sysgrp.arc`）。
3. 修正 `VM分析.md` 里「`DCFS FORMAT 1.00` 是 `DSC` 笔误」的错误说法：DCFS 是真实格式（见 `存档与全局数据.md`），不要改回去。

## 2. 交付与协作

- 每个任务在 `analysis\进度.md` 追加一行（日期、任务、结论、遗留问题）。
- 并行时：G1–G4 只改各自负责的主 opcode 段落；H、I、J、K 各写各自的新文件，互不修改对方文档。
- 文档末尾仍须有「已验证」「静态推断」「未知与风险」三节。
- 优先顺序建议：**G（量最大、最关键）> I > J > H > K**。H 与 K 可并入任一会话顺手完成。
- 遇到需要 Claude 决定的设计问题（例如某个行为在 SDL3 下难以复现），写进 `进度.md` 的「待 Claude 决定」条目，不要自己拍板。
