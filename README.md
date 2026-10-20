<div align="center">

<img src="resources/app.svg" width="88" alt="AccuShot 标志">

# AccuShot

**框准所需，读懂所截。**

面向 Windows 的截图与内容提取工具。<br>
从精细选框，到文字、翻译、公式与 Markdown，让屏幕里的内容继续可用。

![Windows](https://img.shields.io/badge/Windows-x64-006B6C?style=flat-square)
![C++20](https://img.shields.io/badge/C%2B%2B-20-006B6C?style=flat-square)
![Qt 6](https://img.shields.io/badge/Qt-6-006B6C?style=flat-square)
[![MIT](https://img.shields.io/badge/License-MIT-006B6C?style=flat-square)](LICENSE)

[下载便携版](#download) · [功能亮点](#features) · [快速上手](#quick-start) · [快捷键](#shortcuts) · [源码构建](#build) · [参与贡献](#contributing)

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme/workbench-dark.png">
  <img src="docs/images/readme/workbench-light.png" width="1000" alt="AccuShot 文字识别与翻译工作台：左侧原图，右侧原文和译文区域">
</picture>

<sub>原生 Qt 界面，支持浅色、深色及跟随系统主题。</sub>

</div>

<a id="download"></a>

## 下载与运行

**[下载 AccuShot v1.2.3 · Windows x64 便携版](https://github.com/kongyouether/AccuShot/releases/download/v1.2.3/AccuShot-v1.2.3-windows-x64-portable.zip)**

[查看 Release、插件安装说明与 SHA256 校验值](https://github.com/kongyouether/AccuShot/releases/tag/v1.2.3)

1. 下载 ZIP，**完整解压**到有写入权限的目录。
2. 双击 `AccuShot.exe`，在任务栏右下角托盘找到程序。
3. 按 `F1` 开始截图；右键托盘可打开设置或退出。

便携包已包含主程序、Qt 与 VC++ 运行库和解压组件，解压即可使用截图、标注和贴图。OCR、翻译、公式和 Markdown 插件可在设置中按需安装，也可从 [Release](https://github.com/kongyouether/AccuShot/releases/tag/v1.2.3) 下载对应插件包。

设置保存在 Windows 当前用户配置中。首次运行默认开启开机自启动，可在设置中调整。

<details>
<summary>核对下载文件的 SHA256</summary>

下载 ZIP 后，在 PowerShell 中执行：

```powershell
Get-FileHash .\AccuShot-v1.2.3-windows-x64-portable.zip -Algorithm SHA256
```

将输出与 Release 正文中“附件 SHA256 校验值”对应文件的摘要比较，大小写不影响比较。

</details>

<a id="features"></a>

## 功能亮点

### 让选框更接近你的意图

面对网页卡片、文档段落或层叠窗口，先让候选框找到范围，再用滚轮和微调确定边界。

- **四种吸附模式**：窗口、元素与控件、图像轮廓，以及结合三者的智能融合（默认）。
- **可见内容优先**：根据窗口层叠关系推荐当前可见区域，也支持手动选择其他候选。
- **滚轮切换大小，Tab 切换推荐**：从小块内容到完整窗口，再到全屏，保留不同层级的选择空间。
- **Ctrl + 滚轮精细调整**：逐步扩大或缩小候选框，默认每档四边各移动 1 个逻辑像素，步长与快捷键可配置。
- **随时自由框选**：直接拖拽确定选区，也可以在设置中关闭自动吸附。

### 原图、原文、译文，放在一起核对

识别后无需来回切换多个窗口。点击原图中的文字行、选择原文或选择译文，即可联动定位对应段落。

- 支持图片缩放、识别框显示、原文编辑、合并段落、清除空格与复制。
- 提供对照、仅原文、仅译文视图，按任务调整阅读空间。
- 编辑原文后可重新翻译，更新段落对应关系。
- 使用提供文字坐标的 OCR 引擎时，可联动原图定位；原文与译文按段落对应。
- **原位屏幕翻译**将当前截图的译文显示在原位置，按住 `Space` 临时查看原图。

### 从截图直接进入笔记与写作

| 想做什么 | AccuShot 提供什么 |
|---|---|
| 提取屏幕文字 | Windows 原生 OCR、可选 RapidOCR，以及配置的在线识别服务 |
| 阅读外语内容 | 识别后翻译、原图/原文/译文联动、原位屏幕翻译 |
| 摘录论文公式 | 公式识别、LaTeX 编辑，复制 LaTeX、块级 Markdown 或 MathML |
| 整理截图笔记 | 截图转 Markdown，编辑源码、预览并导出 |
| 截取长页面或宽表格 | 纵向、横向滚动截图与拼接预览 |
| 记录操作过程 | 区域 GIF 录制 |
| 对照资料或反馈问题 | 桌面置顶贴图、矩形/箭头/画笔/文字/马赛克标注 |

<p align="center">
  <img src="docs/images/readme/toolbar-light.png" width="866" alt="截图工具栏：标注、滚动截图、GIF、识别与翻译、贴图、保存及更多操作">
</p>

### 本地与在线，按功能各自选择

文字识别、翻译、公式识别和 Markdown 可以分别选择处理方案、模型与指令；在线账号共用服务配置，各功能保留自己的选择。

| 功能 | 本地方案 | 在线方案 |
|---|---|---|
| 文字识别 | Windows OCR；RapidOCR 插件 | 配置支持图片输入的识别服务 |
| 翻译 | Argos 中英双向翻译插件 | 配置的文本翻译服务 |
| 公式识别 | RapidLaTeXOCR、Pix2Text MFR、PP-FormulaNet 插件 | 配置的公式或视觉服务 |
| Markdown | RapidOCR 基础文本；Pix2Text 页面解析（实验） | 兼容视觉服务；Mathpix 适配插件 |

本地插件各自使用独立环境，安装后在本机完成识别与翻译。

<details>
<summary><strong>查看插件管理与深浅主题</strong></summary>

| 浅色 | 深色 |
|---|---|
| ![浅色插件管理](docs/images/readme/plugins-light.png) | ![深色插件管理](docs/images/readme/plugins-dark.png) |

</details>

<a id="quick-start"></a>

## 快速上手

1. **启动 `AccuShot.exe`**，程序常驻系统托盘；右键托盘图标进入设置。
2. **按 `F1` 开始截图**，移动鼠标查看候选框，用滚轮切换大小，或直接拖拽框选。
3. **在选区内双击或按 `Enter` 复制截图**（添加文字或图形后也可双击），按 `F3` 将选区贴到桌面，或从工具栏选择标注、识别、翻译等操作。
4. **配置识别能力**：Windows OCR 为系统内置方案；其他本地方案先在「设置 → 插件与模型」安装所需插件，再到文字识别、翻译、公式识别或 Markdown 页选择。
5. **需要在线处理时**，先在「设置 → 在线账号」配置服务地址和密钥，再在对应功能页选择模型。图片任务还需服务与模型支持图片输入。

默认使用「智能融合」，结合窗口、元素与图像轮廓生成候选框，可在「设置 → 截图与标注」中切换模式。

### 安装插件

在「设置 → 插件与模型」中选择需要的功能并点击安装。程序内置 ZIP / 7z 解压组件，自动完成下载与部署。

- **RapidOCR**：直接安装，或下载 Release 中的 RapidOCR 包，通过「导入 RapidOCR 包」安装。
- **Python 本地插件**：自动检测系统及 Conda 中的 CPython 3.12 x64，也可手动选择环境。没有可用环境时，点击「安装 Python 3.12」安装官方 Python 与 pip，完成后自动重新检测。
- **模型预置包**：将 ZIP 内的 `plugins` 目录合并到主程序目录，再在插件页点击对应插件「安装」。此步骤会联网安装 Python 依赖并验证模型。
- **在线适配器**：在插件页启用后，到「在线账号」配置服务。

Python 插件依赖所选的基础 Python / Conda 环境，请保留该环境。插件安装日志位于 `plugins/<插件ID>-install.log`，应用日志位于 `%LOCALAPPDATA%\AccuShot\AccuShot\logs`。

### 数据如何处理

- 基础截图、标注、贴图，以及已安装的本地识别/翻译方案在本机处理内容。
- 在线方案将文字或图片发送到所配置的服务，费用按供应商规则计算。
- 在线账号的 API 密钥通过 Windows 凭据管理器保存。

<a id="shortcuts"></a>

## 常用快捷键

以下为默认映射，截图相关快捷键和滚轮方向可在设置中调整。

| 场景 | 操作 | 默认按键 |
|---|---|---|
| 全局 | 开始截图 | `F1` |
| 全局 | 将剪贴板图片贴到桌面 | `F3` |
| 选区确定前 | 切换更大 / 更小候选 | 滚轮向上 / 向下 |
| 选区确定前 | 按推荐顺序切换候选 | `Tab` / `Shift + Tab` |
| 选区确定前 | 精细扩大 / 缩小候选框 | `Ctrl + 滚轮向上 / 向下` |
| 截图中 | 完成并复制截图 | 选区内双击左键（文字输入框内双击仍用于选词） |
| 截图中 | 识别文字 | `Ctrl + O` |
| 截图中 | 撤销标注 | `Ctrl + Z` |
| 截图中 | 贴图 / 保存 / 复制截图 | `F3` / `Ctrl + S` / `Enter` |
| 截图中 | 取消 | `Esc` |
| 贴图中 | 识别 / 复制 / 保存 | `Ctrl + O` / `Ctrl + C` / `Ctrl + S` |
| 贴图中 | 关闭 | `Esc` |

普通滚轮按面积切换候选，最大一级为整个截图桌面；多显示器环境中指虚拟桌面范围。`Ctrl + 滚轮` 调整当前框的边缘。

<a id="build"></a>

## 从源码构建

### 开发环境

| 依赖 | 要求 |
|---|---|
| 平台 | Windows x64 |
| 编译器 | Visual Studio 2022 / Build Tools，安装 C++ 桌面开发工具和 Windows SDK |
| 构建工具 | CMake 3.20+、Ninja |
| 界面库 | Qt 6 的 MSVC x64 套件，含 Core、Gui、Widgets、Svg、Network；发布构建使用 6.11.1 |
| 可选测试依赖 | Qt Test |
| 可选本地 AI 插件 | CPython 3.12 x64，支持系统 Python 与 Conda 环境 |

### 编译与运行

将仓库下载或克隆到本机，在仓库根目录打开 **VS 2022 Developer PowerShell（x64）**。确保 `cmake`、`ninja` 已加入 PATH，然后按实际安装位置修改 `$qtRoot`：

```powershell
$qtRoot = 'C:\Qt\6.11.1\msvc2022_64'
$env:PATH = "$qtRoot\bin;" + $env:PATH

cmake -S . -B ../AccuShot-build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DQt6_DIR=$qtRoot\lib\cmake\Qt6" `
  -DACCUSHOT_EXECUTABLE_NAME=AccuShot
if ($LASTEXITCODE) { throw 'CMake configuration failed' }

cmake --build ../AccuShot-build/release --parallel 6
if ($LASTEXITCODE) { throw 'Build failed' }

& "$qtRoot\bin\windeployqt.exe" --release --no-translations ..\AccuShot-build\release\AccuShot.exe
if ($LASTEXITCODE) { throw 'Qt dependency deployment failed' }

..\AccuShot-build\release\AccuShot.exe
```

也可使用 [Windows 构建脚本](scripts/build_windows.ps1)，将脚本中的 Qt、CMake 和 Ninja 路径改为本机安装位置即可。构建产物包含 [7-Zip 命令行组件](tools/7zip/README.md)；分发时保留 `tools/7zip/` 下的程序及许可文件。

### 代码入口

```text
src/
├─ core/             截图、候选融合、OCR、翻译、公式、Markdown 与插件管理
├─ ui/               截图覆盖层、工具栏、结果窗口、贴图与设置
└─ platform/         Windows 全局热键等平台能力
resources/           应用图标、SVG 功能图标与 Qt 资源
scripts/             Windows 构建、插件安装与辅助工具
tests/               核心回归、Qt 界面及原生窗口测试
docs/                项目标识说明与 README 展示图片
```

测试入口、开发方式与提交说明见 [贡献指南](CONTRIBUTING.md)。

<a id="contributing"></a>

## 参与贡献

欢迎通过当前仓库的 Issues 提交问题和建议，也欢迎提交改进选框、识别适配、界面与文档的 Pull Request。

报告问题时，请说明 Windows 版本、显示缩放、功能或引擎选择，以及最短复现步骤；截图和日志请先移除私人内容与密钥。开发前可阅读 [贡献指南](CONTRIBUTING.md)。

## 致谢与许可

AccuShot 在 [Evan](https://github.com/duyuanfu/evan-ocr) 工程基础上继续开发，保留原项目版权声明。

感谢 Qt 以及 RapidOCR、Argos Translate、RapidLaTeXOCR、Pix2Text、PaddleOCR 等项目提供的基础能力。

本仓库代码采用 [MIT License](LICENSE)。第三方依赖、插件和模型权重遵循各自的许可证。
