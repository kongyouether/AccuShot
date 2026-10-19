# AccuShot 独立项目标识

程序只读写 AccuShot 配置，不再包含旧品牌识别、回退或自动迁移。

| 项目 | 当前标识 |
|---|---|
| 应用与组织名 | `AccuShot` |
| 用户设置 | `HKEY_CURRENT_USER\Software\AccuShot\AccuShot` |
| Windows通用凭据 | `AccuShot/AI/<账号ID>`，私有请求头使用 `/headers` 后缀 |
| 自启动项 | 当前用户Run项中的 `AccuShot` |
| 日志目录 | `%LOCALAPPDATA%/AccuShot/AccuShot/logs/` |
| 单实例互斥量 | `AccuShot_SingleInstance_Mutex_Unique` |
| 构建目标/输出参数 | `accushot` / `ACCUSHOT_EXECUTABLE_NAME` |
| 测试环境变量 | `ACCUSHOT_*` |

## 工程布局

源码、资源和测试保留在项目目录。默认构建产物位于相邻的 `../AccuShot-build/release/`；部署及导出产物也位于 `../AccuShot-build/`。模型、依赖、日志和发布产物不纳入源码仓库。

## 插件来源

RapidOCR 直接使用[引擎项目官方 v0.2.0 发布包](https://github.com/hiroi-sora/RapidOCR-json/releases/tag/v0.2.0)，支持官方压缩包的顶层目录和已安装的平铺目录。

## 升级说明

v1.2.1 只使用 AccuShot 设置、凭据、自启动和日志标识，不包含旧品牌的自动迁移、读取回退或兼容分支。升级前请记录需要保留的设置，在新版中重新配置账号；已使用 AccuShot 标识的数据继续保留。

默认自动吸附为智能融合（窗口、元素与图像轮廓）；已保存的模式优先于默认值。

LICENSE、README 上游致谢与原版权声明保持原样。
