# VisionFlow
本人是华南理工软件大一在读生，荣获黑框框程序设计大赛一等奖，直接对标剪映的视频防抖功能，应广大同学们，在此开源我的项目，并用中文写下这个readme，答辩的过程中，评委提出我的缺陷：无法自动识别输入视频的帧数，让输出输入帧数保持一致。其实这个实现应该是比较简单的，暂时还没有加。

这是一个基于纯 C++ 的相机运动分析与视频稳像系统。

VisionFlow 面向视频帧序列进行稳像处理，核心算法由 C++17 实现，包括 BMP 读写、灰度转换、Sobel 边缘提取、块匹配、RANSAC 过滤、仿射运动估计、相机轨迹平滑、虚拟云台补偿和 HTML 报告生成。

项目支持三种模式：

- `auto`：自动分析相机运动，选择 `static` 或 `motion`。
- `static`：固定机位稳像，适合三脚架、监控、小幅手抖。
- `motion`：运镜稳像，适合平移、跟拍、走路拍摄等运动镜头。

项目支持三类输入输出：

- BMP 帧目录：核心处理路径，完全由 C++ 实现。
- MP4 视频：调用项目根目录的外部 `ffmpeg.exe` 进行格式转换。
- VFVID：VisionFlow 自定义的纯 C++ 简易视频容器。

---

## 项目结构

```text
VisionFlow/
├── VisionFlow.exe                 # Release 可执行文件
├── ffmpeg.exe                     # 可选 MP4 格式转换工具
├── CMakeLists.txt
├── build_msvc.bat
├── README.md
├── VisionFlow 核心算法讲解.html
├── case(test)/                    # 示例视频
└── src/
    ├── main.cpp                   # 程序入口和命令行分发
    ├── app/
    │   ├── AppTypes.*             # 公共模式、路径、字符串和摘要工具
    │   ├── PipelineRunner.*       # 主处理流程和单任务调度
    │   ├── TuiController.*        # TUI 菜单与实时状态面板
    │   ├── BatchRunner.*          # CSV 批处理和批处理总报告
    │   ├── VideoIOAdapter.*       # MP4 / VFVID 输入输出适配
    │   └── AutoModeSelector.*     # auto 模式判别逻辑
    ├── core/                      # Image / GrayImage / Timer
    ├── io/                        # BMP、路径、帧序列、VFVID
    ├── cv/                        # 灰度、Sobel、归一化
    ├── motion/                    # 运动分析
    ├── affine/                    # 仿射稳像
    └── report/                    # HTML 报告
```



| 模块 | 主要职责 |
|---|---|
| `main.cpp` | 只保留命令行入口和参数分发 |
| `PipelineRunner` | 串联读帧、预处理、运动分析、稳像、报告生成 |
| `TuiController` | 控制台菜单、错误停留、htop 风格实时状态面板 |
| `VideoIOAdapter` | 调用外部 `ffmpeg.exe` 做 MP4 转换，管理 VFVID 工具命令 |
| `AutoModeSelector` | 根据全局位移、方向一致性、RANSAC 内点率选择模式 |
| `BatchRunner` | 解析任务 CSV，执行队列，生成 HTML/CSV 总报告 |

---

## 快速使用

直接打开 TUI：

```bat
VisionFlow.exe
```

命令行默认使用 `auto`：

```bat
VisionFlow.exe input.mp4 output.mp4
VisionFlow.exe input_frames output_dir
VisionFlow.exe input.vfvid output.vfvid
```

指定模式：

```bat
VisionFlow.exe --mode auto input output
VisionFlow.exe --mode static input output
VisionFlow.exe --mode motion input output
```

兼容简化命令：

```bat
VisionFlow.exe auto input_frames output_dir
VisionFlow.exe static input_frames output_dir
VisionFlow.exe motion input_frames output_dir
```

---

## TUI 功能

直接运行 `VisionFlow.exe` 后进入终端界面：

```text
[1] Single Task
[2] Batch Queue
[3] VFVID Tools
[4] View Last Report
[5] Project Info
[0] Exit
```

- `Single Task`：处理一个 MP4、BMP 帧目录或 VFVID 文件。
- `Batch Queue`：读取任务 CSV，连续处理多个任务。
- `VFVID Tools`：打包、解包、查看 VFVID 信息。
- `View Last Report`：打开当前 TUI 会话中最近生成的报告。
- `Project Info`：查看项目核心说明。

处理任务时，TUI 会显示实时状态面板：

```text
+--------------------------- VisionFlow Console ---------------------------+
| Mode: AUTO -> MOTION                                                     |
| Input: case(test)\motion\demo.mp4                                        |
| Stage: Affine stabilization                                              |
| Frames: 128 / 300        Progress: [#####.......] 42%                    |
| Avg motion: 7.82 px      RANSAC inlier: 73.5%                            |
| Jitter RMS: 9.41 -> 4.88  Improvement: 48.1%                             |
+--------------------------------------------------------------------------+
```

该界面使用 Windows Console API，不使用 Qt、EasyX、SDL、GDI/GDI+。

---

## 输入输出

### BMP 帧目录

BMP 帧目录是最直接、最稳定的核心入口：

```text
input_frames/
├── frame_0001.bmp
├── frame_0002.bmp
└── frame_0003.bmp
```

程序会按文件名排序读取 BMP，因此建议使用连续编号。

### MP4

项目根目录已包含 `ffmpeg.exe`。当输入或输出为 MP4 时，程序会调用它进行格式转换：

```text
MP4 输入
  -> ffmpeg.exe 拆成临时 BMP 帧
  -> VisionFlow 核心稳像算法
  -> ffmpeg.exe 合成 MP4
```

说明：

- `ffmpeg.exe` 只负责格式转换。
- 图像处理、运动估计、RANSAC、稳像算法仍由 VisionFlow 自己实现。
- 输入端临时拆帧目录在系统临时目录中，处理后自动清理，不会留下 `_source_frames`。
- 如果比赛环境不允许使用 `ffmpeg.exe`，使用 BMP 帧目录即可展示核心算法。

### VFVID

VFVID 是 VisionFlow 自定义的原生帧序列容器，扩展名为 `.vfvid`。它使用 C++ 标准文件 I/O 直接读写，不依赖外部视频库。

格式特点：

- Header：`VFVID01`、宽度、高度、FPS、帧数、像素格式。
- Frame Data：连续保存 RGB24 原始帧。
- 不压缩，体积通常比 MP4 大。
- 适合展示纯 C++ 视频容器能力和批处理数据管理。

VFVID 命令：

```bat
VisionFlow.exe --pack input_frames output.vfvid 30
VisionFlow.exe --unpack input.vfvid output_frames
VisionFlow.exe --vfinfo input.vfvid
VisionFlow.exe --mode auto input.vfvid output.vfvid
```

---

## Batch Queue

Batch Queue 用于一次处理多个任务。TUI 中的 `Task CSV path` 要填写任务表路径，不是单个视频路径。

示例 `tasks.csv`：

```csv
mode,input,output
auto,case(test)\static\pen.mp4,output\pen.mp4
motion,case(test)\motion\运镜1.mp4,output\motion1.mp4
static,input_frames,output_static
auto,input.vfvid,output.vfvid
```

运行：

```bat
VisionFlow.exe --batch tasks.csv
```

指定批处理报告：

```bat
VisionFlow.exe --batch tasks.csv output\VisionFlow_Batch_Report.html
```

也可以只给报告文件夹：

```bat
VisionFlow.exe --batch tasks.csv output
```

此时会自动生成：

```text
output\VisionFlow_Batch_Report.html
output\VisionFlow_Batch_Report.csv
```

注意：

- CSV 列顺序固定为 `mode,input,output`。
- `mode` 可填 `auto`、`static`、`motion`。
- 当前 CSV 解析器保持简单，不建议路径中包含英文逗号。
- 单个任务失败不会中断整个批处理，失败原因会写入总报告。

---

## 输出结果

如果输出是目录，通常包含：

```text
output/
├── preview/                       # 首帧、中间帧、末帧预览
├── gray_frames/                   # 灰度帧
├── edge_frames/                   # Sobel 边缘帧
├── motion/                        # 运动曲线、热力图、CSV
├── motion_diff_frames/            # 帧间差分图
├── affine_stabilization/          # 仿射运动 CSV 和曲线
├── affine_stable_frames/          # static 输出帧
├── cinematic_frames/              # motion 输出帧
├── affine_compare/ 或 cinematic_compare/
├── frames_info.txt
├── report.html
└── AUTO_MODE_DECISION.txt         # auto 模式下生成
```

如果输出是 MP4 或 VFVID，会额外生成工作目录：

```text
result_visionflow/
├── report.html
├── frames_info.txt
└── ...
```

---

## 核心算法流程

```text
输入 BMP / MP4 / VFVID
  -> 统一为 BMP 帧序列
  -> 读取彩色图像
  -> 灰度化 + Sobel 边缘提取
  -> 块匹配估计局部位移
  -> RANSAC 过滤错误匹配
  -> 估计全局仿射运动
  -> auto 判断 static / motion
  -> 相机轨迹平滑或虚拟云台
  -> 仿射补偿 + 裁剪缩放
  -> 输出帧目录 / MP4 / VFVID
  -> 生成 HTML 报告
```

`auto` 主要参考平均位移、P90 位移、净位移、方向一致性、旋转量和 RANSAC 内点比例。运行后会生成 `AUTO_MODE_DECISION.txt` 记录判别依据。

---

## 模式选择

| 模式 | 适合场景 | 策略 |
|---|---|---|
| `static` | 固定机位、三脚架、监控、小幅手抖 | 尽量锁定初始视角，削弱随机抖动 |
| `motion` | 平移、推拉、跟拍、走路拍摄 | 保留原始运镜趋势，削弱高频抖动 |
| `auto` | 不确定视频类型 | 先估计相机运动，再自动选择模式 |

---

## 编译

推荐 Windows + Visual Studio / MSVC。

一键编译：

```bat
build_msvc.bat
```

手动编译：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

输出：

```text
build\Release\VisionFlow.exe
```

---

## 参数速查

### 命令行参数

| 命令 | 说明 |
|---|---|
| `VisionFlow.exe` | 打开 TUI 主菜单 |
| `VisionFlow.exe input output` | 默认 `auto` 模式处理单任务 |
| `VisionFlow.exe --mode auto input output` | 自动选择 `static` 或 `motion` |
| `VisionFlow.exe --mode static input output` | 固定机位稳像 |
| `VisionFlow.exe --mode motion input output` | 运镜稳像 |
| `VisionFlow.exe auto input output` | 兼容旧版 auto 命令 |
| `VisionFlow.exe static input output` | 兼容旧版 static 命令 |
| `VisionFlow.exe motion input output` | 兼容旧版 motion 命令 |
| `VisionFlow.exe --batch tasks.csv` | 执行批处理任务表 |
| `VisionFlow.exe --batch tasks.csv report.html` | 执行批处理并指定总报告 |
| `VisionFlow.exe --pack input_frames output.vfvid 30` | 打包 BMP 帧目录为 VFVID，FPS 可省略 |
| `VisionFlow.exe --unpack input.vfvid output_frames` | 解包 VFVID 为 BMP 帧目录 |
| `VisionFlow.exe --vfinfo input.vfvid` | 查看 VFVID 文件信息 |
| `VisionFlow.exe --help` | 查看命令行帮助 |

`input` 可以是 BMP 帧目录、MP4 视频或 VFVID 文件。

`output` 可以是输出目录、MP4 文件或 VFVID 文件。

### 批处理 CSV 参数

```csv
mode,input,output
auto,case(test)\static\pen.mp4,output\pen.mp4
motion,case(test)\motion\运镜1.mp4,output\motion1.mp4
```

| 字段 | 说明 |
|---|---|
| `mode` | `auto`、`static` 或 `motion` |
| `input` | 单个任务的输入路径 |
| `output` | 单个任务的输出路径 |

### 核心算法参数位置

主要调参位置在 `src/app/PipelineRunner.cpp` 和 `src/app/AutoModeSelector.cpp`。

| 函数 | 作用 |
|---|---|
| `makeBaseAffineConfig()` | 设置块匹配、RANSAC、平滑等基础参数 |
| `applyModeConfig()` | 设置 `static` 和 `motion` 的稳像策略 |
| `decideAutoMode()` | 设置 `auto` 模式判别阈值 |

常见参数含义：

| 参数 | 含义 |
|---|---|
| `blockSize` | 块匹配窗口大小 |
| `blockStep` | 匹配块采样间隔 |
| `searchRadius` | 块匹配搜索半径 |
| `textureThreshold` | 过滤低纹理区域的阈值 |
| `ransacIterations` | RANSAC 迭代次数 |
| `ransacThreshold` | RANSAC 内点误差阈值 |
| `smoothRadius` | 相机轨迹平滑半径 |
| `correctionStrength` | 稳像补偿强度 |
| `maxCropRatio` | 最大裁剪比例 |
| `lockToOriginStrength` | `static` 模式锁定初始视角的强度 |
| `virtualGimbalEnabled` | 是否启用 `motion` 虚拟云台 |

---

## 常见错误与解决方案

### 1. Batch Queue 按 Y 后提示 `Cannot open batch task file`

原因：`Task CSV path` 填错了，或者把视频路径当成 CSV 路径填了。

解决：

- 如果只处理一个视频，用 `Single Task`。
- 如果使用 Batch Queue，先创建 `tasks.csv`，再把它的路径填到 `Task CSV path`。

正确 CSV 示例：

```csv
mode,input,output
auto,case(test)\static\pen.mp4,output\pen.mp4
```

### 2. `Cannot create batch HTML report: ...\新建文件夹`

旧版本会把文件夹当成 HTML 文件写入。当前版本已修复：报告路径可以填文件夹，会自动生成 `VisionFlow_Batch_Report.html`。

如果仍遇到该问题，请确认正在运行的是项目根目录里的新版：

```text
C:\Users\13834\Desktop\VisionFlow\VisionFlow.exe
```

### 3. Batch 报告路径应该填什么？

三种写法都可以：

```text
直接回车
```

在任务 CSV 同目录生成默认报告。

```text
C:\Users\13834\Desktop\VisionFlow\新建文件夹
```

在该文件夹下生成 `VisionFlow_Batch_Report.html`。

```text
C:\Users\13834\Desktop\VisionFlow\新建文件夹\my_report.html
```

按指定文件名生成报告。

### 4. `Decoding video to BMP frame sequence failed`

常见原因：

- 输入 MP4 路径不存在。
- 路径写错或没有加引号。
- `ffmpeg.exe` 不在 `VisionFlow.exe` 同目录。
- 视频文件损坏或格式不被当前 `ffmpeg.exe` 支持。

解决：

- 检查输入路径。
- 确认项目根目录存在 `ffmpeg.exe`。
- 先用示例视频测试：`case(test)\static\pen.mp4`。

### 5. `No BMP frames found in folder`

原因：输入目录里没有 `.bmp` 帧，或者把输出目录误填成输入目录。

解决：

- 检查输入文件夹是否包含 BMP。
- 使用 MP4 输入时，直接填 `.mp4` 文件路径，不要手动填写临时拆帧目录。

### 6. 输出目录被覆盖

VisionFlow 会重建输出目录，避免混入旧结果。

解决：

- 不要把重要文件放在输出目录中。
- 不要把输入目录和输出目录设置成同一个目录。

### 7. 双击运行时窗口一闪而过

当前 TUI 已捕获常见错误并暂停显示。如果仍一闪而过，建议从 PowerShell 打开：

```bat
cd C:\Users\13834\Desktop\VisionFlow
.\VisionFlow.exe
```

这样可以看到完整错误信息。

---

## 合规说明

- 核心图像处理和稳像算法不依赖 OpenCV、Qt、EasyX、SDL、FFmpeg 库。
- `Windows.h` 仅用于控制台能力，不使用 GDI/GDI+ 图形绘制。
- `ffmpeg.exe` 是外部可执行工具，只用于 MP4 与 BMP 帧之间的格式转换。
- VFVID 读写完全由 C++ 标准库文件 I/O 实现。
- 如果比赛环境不允许使用 `ffmpeg.exe`，可使用 BMP 帧目录或 VFVID 展示核心功能。

---

## 一句话总结

VisionFlow 用纯 C++ 完成相机运动估计和视频稳像：BMP 是核心处理路径，MP4 提供用户友好入口，VFVID 展示原生视频容器能力，Batch Queue 和 TUI 让项目从算法演示变成完整工具。
