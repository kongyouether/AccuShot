> 历史插件包说明，当前安装与调用方式以 [主 README](../../README.md) 为准；下文旧版自动回退、性能和示例包名不作为当前行为承诺。

# RapidLaTeXOCR 离线数学公式识别扩展包

> 历史包结构参考。公开源码只包含插件代码和元信息，不附带下列模型或可执行文件。当前 AccuShot 请通过「设置 → 插件与模型」安装公式方案；模型及其运行依赖按需下载。下文旧版自动降级描述不代表当前行为。

本目录为 **AccuShot** 的纯本地离线数学公式识别插件包（RapidLaTeXOCR）。

## 目录结构
```text
plugins/latex_ocr/
├── config.json              # 插件元信息描述文件
├── README.md                # 规范与说明文件
├── rapid_latex_ocr.exe      # 离线推理主程序
├── runner.py                # Python 推理执行器
└── models/                  # ONNX 神经网络模型文件
    ├── image_resizer.onnx   # 图像预处理与尺寸自适应模型 (~37MB)
    ├── encoder.onnx         # 视觉特征编码模型 (~85MB)
    ├── decoder.onnx         # LaTeX 序列自回归解码模型 (~49MB)
    └── tokenizer.json       # 词表映射文件
```

## 功能特点
- 100% 本地运算，0 联网请求，隐私绝对安全；
- 支持识别各种二维复杂数学公式、矩阵、积分、上下标并输出标准 LaTeX 与 MathML 代码；
- AccuShot 主程序在无网或未配置云端 Token 时会自动无缝降级至本插件。
