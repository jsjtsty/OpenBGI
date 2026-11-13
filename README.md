# OpenBGI

C++23 的 BGI 兼容运行时。主要开发和验证使用《巧可甜恋 2》和《巧可甜恋 3》；其他游戏和引擎代际尚未完整验证。

## 构建与运行

需要 CMake 3.28+ 和支持 C++23 的编译器。完整运行时还会构建 SDL3、FreeType、FFmpeg 和着色器工具。

```powershell
cmake --preset windows -DOPENBGI_GAME_DIR="E:/Games/Amairo Chocolate 2"
cmake --build --preset windows-debug
ctest --preset windows-debug

build/windows/src/Debug/obgi_run.exe "E:/Games/Amairo Chocolate 2" --window
```

macOS 和 Linux 分别提供 `macos`、`linux` preset，使用 Ninja Multi-Config。例如在 macOS 上：

```sh
cmake --preset macos -DOPENBGI_GAME_DIR="/path/to/game"
cmake --build --preset macos-debug
ctest --preset macos-debug
build/macos/src/Debug/obgi_run "/path/to/game" --window
```

无窗口回归可指定帧数、独立数据目录和截图：

```powershell
build/windows/src/Debug/obgi_run.exe "E:/Games/Amairo Chocolate 2" --frames 3000 --render-headless --data-dir build/run-data --shot 2999 build/title.png
```

交互模式使用单调真实时间、后台资源解码和音频设备；无窗口模式默认使用虚拟毫秒时间、同步执行的解码任务和离线音频推进。`--render-headless` 启用离屏 GPU 渲染，`--async-resources` 在无窗口模式启用后台解码，`--pace-headless` 按真实时间推进虚拟帧，便于诊断异步加载。默认持久数据写入用户数据目录中按游戏路径隔离的 OpenBGI 子目录。测试可用 `--data-dir` 单独隔离。

VM 等待异步图片或音频资源时，由解码完成事件唤醒并继续执行，无需等待下一逻辑帧；普通帧更新仍按原有时钟推进。启用 `OBGI_PERF` 可输出性能诊断，启用 `OBGI_PERF_RESOURCE` 可查看资源加载和提交信息。

使用 `obgi_run --help` 查看选项。长时间无头调试可用 `--mouse 0:735:645,1000:640:400` 在指定帧移动鼠标，`--click-every 300 30` 从第 300 帧起每 30 帧点击一次，`--progress-every 1000` 定期报告进度，`--threads` 在到达帧数上限时打印存活线程，`--trace` 输出执行的子指令。帧编号从 0 开始；自动点击不能处理所有游戏分支。`--window --frames N` 也可限制窗口模式运行帧数。

代理是本机配置，需要时传入 `-DOPENBGI_PROXY=...`，公共 preset 不指定代理。

## 仅构建核心

VM、格式、归档与资源查找可以不依赖图形和音视频栈构建：

```powershell
cmake --preset windows-core
cmake --build --preset windows-core-debug
ctest --preset windows-core-debug
```

也可以在其他生成器下配置 `-DOPENBGI_BUILD_RUNTIME=OFF`。

测试分为 `unit`、`gpu`、`host`、`game`：`host` 需要系统字体，`gpu` 需要至少一个受支持的 GPU 后端，`game` 需要配置游戏目录。缺少游戏目录时，CTest 明确禁用 `game` 套件；已启用套件中缺少所需资源会失败。

普通脚本文字优先使用系统简体中文字体；脚本注册的内嵌字体和明确指定的繁体中文字符集仍按各自设置处理。可用环境变量 `OBGI_FONT` 指定字体文件或文件名，用 `OBGI_FONT_DIR` 添加字体搜索目录。

## 结构

- `vm/`：解释器、地址空间、调度、等待完成校验和资源事件唤醒。
- `runtime/`：`EngineSession`，统一生命周期、时钟、输入、服务装配、播放与呈现。
- `services/`：独立输入状态、逻辑场景、消息渐显和序列解码。
- `resources/`：共享归档映射与资源查找。
- `bindings/`：脚本适配层；部分历史窗口/特效逻辑仍在逐步迁移。
- `render/`、`audio/`、`video/`：设备后端及媒体算法。
- `formats/`：不依赖平台的格式处理。

详细改动和验证边界见 [重构实施记录](analysis/重构实施记录.md)。原版行为证据和仍未知的兼容语义在 `analysis/` 中。
