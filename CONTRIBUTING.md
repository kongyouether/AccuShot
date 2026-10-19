# 开发与贡献

## 开始之前

安装和源码构建步骤见 [README](README.md#build)。主程序使用 C++20、Qt Widgets 和 Windows 原生接口；修改应尽量复用已有模块。

提交问题时请附上复现步骤、Windows 版本、显示缩放及相关功能/引擎设置。截图与日志应先移除私人内容、账号和密钥。

## 定向验证

在 VS 2022 Developer PowerShell（x64）中执行，确保 CMake、Ninja 在 PATH。将 Qt 路径改为本机安装位置，界面测试额外需要 Qt Test。

```powershell
$qtRoot = 'C:\Qt\6.11.1\msvc2022_64'
$env:PATH = "$qtRoot\bin;" + $env:PATH

cmake -S tests/contour -B ../AccuShot-build/contour -G Ninja `
  "-DQt6_DIR=$qtRoot\lib\cmake\Qt6" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Configure failed' }
cmake --build ../AccuShot-build/contour --target fusion_regression contour_regression --parallel 6
if ($LASTEXITCODE) { throw 'Build failed' }

$env:QT_QPA_PLATFORM = 'offscreen'
..\AccuShot-build\contour\fusion_regression.exe
if ($LASTEXITCODE) { throw 'Fusion regression failed' }
..\AccuShot-build\contour\contour_regression.exe
if ($LASTEXITCODE) { throw 'Contour regression failed' }
```

界面与原生窗口回归位于 `tests/result-ui`。先配置、构建 `result_ui` 和 `native_snap_probe`，再按变更范围选择测试方法。`native_snap_probe --occlusion` 和 `result_ui snapFullScreenNavigation` 需要 `QT_QPA_PLATFORM=windows`，会创建测试窗口。其他测试可能操作剪贴板、安装插件、联网或调用本地模型，运行前请阅读对应方法，不要默认执行整个套件。

历史性能报告脚本可能引用本机 `docs/evidence` 数据。公开源码包不包含私人桌面截图、OCR输出和历史运行记录；运行这些报告需自行提供样本。基础合成图片回归不依赖这些资料。

## 提交变更

- 说明解决的问题、行为变化和实际执行的验证。
- 保留正确的测试断言，区分模拟服务、真实模型、原生窗口和物理硬件输入的验证范围。
- 文档改动检查内容、链接及 `git diff --check`，无需重新编译产品。
- 不提交模型权重、虚拟环境、构建目录、日志、转储、私人截图或凭据；使用 `.env.example` 等明确命名的模板时只放占位内容。
- 插件代码和安装器可进入源码仓库；模型与运行依赖应按需安装，并保留各自的许可要求。

## 准备公开源码包

`scripts/prepare_public_source.py` 从当前工作区导出经过路径白名单和 `.gitignore` 双重过滤的源码；不会复制 `.git`、重写历史或改动索引。它包含尚未提交的源代码修改，因此导出前应先完成相应验证。

```powershell
py -3.12 scripts/prepare_public_source.py
```

输出位于 `../AccuShot-build/exports/public-source-时间戳/`，包括源码目录、ZIP和文件清单。清单保留在源码包外。脚本只检查文件类别与路径，不替代凭据扫描或图片人工核对。新增要公开的目录时，应先审查内容再修改白名单。

要以新的公开仓库发布，请将生成的ZIP解压到独立目录，检查后在该目录初始化新仓库。若原仓库历史含敏感材料，直接推送原分支会把旧提交一并上传；新增忽略规则或删除提交无法清除旧历史。
