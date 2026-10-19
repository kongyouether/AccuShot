#include "plugin_manager.h"
#include "local_plugin.h"
#include "../ocr/rapid_ocr_engine.h"
#include "../translation/translation_plugin_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QMetaObject>
#include <QDebug>

#include <QNetworkProxyFactory>

PluginManager& PluginManager::instance()
{
    static PluginManager mgr;
    return mgr;
}

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_nam = std::make_unique<QNetworkAccessManager>(this);
    initCatalog();
    refreshAllStatus();
}

void PluginManager::initCatalog()
{
    m_plugins.clear();

    // 1. RapidOCR 高精度离线文字识别核心引擎
    PluginInfo ocrCore;
    ocrCore.id = "rapidocr-engine";
    ocrCore.name = "RapidOCR 高精度离线文字识别引擎";
    ocrCore.category = "文字识别";
    ocrCore.description = "基于 ONNX Runtime 与 PaddleOCR 的纯本地高精度离线文字识别引擎，支持中英文排版、旋转方向纠正与精准坐标提取。";
    ocrCore.sizeDisplay = "70 MB";
    ocrCore.sizeBytes = 73461693;
    // Use the engine project's original release package.
    ocrCore.downloadUrl = "https://github.com/hiroi-sora/RapidOCR-json/releases/download/v0.2.0/RapidOCR-json_v0.2.0.7z";
    ocrCore.installSubdir = "plugins/ocr";
    ocrCore.checkRelativeFile = "RapidOCR-json.exe";
    m_plugins.append(ocrCore);

    const auto add=[this](QString id,QString name,QString category,QString description) {
        PluginInfo info;info.id=id;info.name=name;info.category=category;info.description=description;
        info.installSubdir="plugins/"+id;info.checkRelativeFile="installed.json";
        info.sizeDisplay="按需下载运行环境及模型";m_plugins.append(info);
    };
    add("translation-en-zh","Argos 中英离线翻译","离线翻译","安装独立 Python 运行环境与英中、中英模型；仅支持中英双向，推理不调用在线服务。");
    add("latex-ocr-engine","RapidLaTeXOCR","公式识别","安装独立 ONNX 公式识别插件，输出 LaTeX。");
    add("pix2text","Pix2Text MFR","公式识别","安装独立 Pix2Text 公式模型与 CPU 运行环境，首次安装需要下载较大依赖。");
    add("paddleformula","PP-FormulaNet_plus-M","公式识别","安装独立 PaddlePaddle CPU 环境与公式模型，首次加载较慢。");
    add("pix2text-page","Pix2Text 文档转 Markdown（实验）","Markdown 离线","CPU 本地解析表格和独立公式。可能重复段落或误判代码，需校对；独立安装完整页面模型，不等同于公式插件。");
    add("markdown-vision","视觉模型转 Markdown","Markdown 在线","启用后在 AI 服务配置视觉模型及密钥。支持通义千问和已有兼容视觉服务；图片上传所选服务，按供应商计费。");
    add("markdown-mathpix","Mathpix 转 Markdown","Markdown 在线","启用后在 AI 服务配置 Mathpix。图片上传 Mathpix，按图或页收费，需 API 账号。");
    for(auto& info:m_plugins)if(LocalPlugin::isOnlineAdapter(info.id))info.sizeDisplay="无需下载模型";

}

QList<PluginInfo> PluginManager::plugins() const
{
    return m_plugins;
}

PluginInfo PluginManager::pluginInfo(const QString& id) const
{
    for (const auto& item : m_plugins) {
        if (item.id == id) return item;
    }
    return PluginInfo();
}

QString PluginManager::pluginRootDir() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir::toNativeSeparators(appDir + "/plugins");
}

void PluginManager::refreshAllStatus()
{
    for (auto& item : m_plugins) {
        // 如果当前正在下载中或解压中，不重置状态
        if (item.status != PluginStatus::Downloading && item.status != PluginStatus::Extracting) {
            updateSinglePluginStatus(item);
        }
    }
    emit pluginsRefreshed();
}

void PluginManager::updateSinglePluginStatus(PluginInfo& info)
{
    const bool isInstalled=info.id=="rapidocr-engine" ? RapidOcrEngine().isAvailable() : LocalPlugin::ready(info.id);
    if (isInstalled) {
        info.status = PluginStatus::Installed;
        info.statusMessage = "已安装并就绪";
        info.progressPercent = 100;
    } else {
        info.status = PluginStatus::NotInstalled;
        info.statusMessage = "未安装";
        info.progressPercent = 0;
    }
}

void PluginManager::installPlugin(const QString& id)
{
    int index = -1;
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].id == id) {
            index = i;
            break;
        }
    }
    if (index == -1) return;

    PluginInfo& info = m_plugins[index];
    if (info.status == PluginStatus::Downloading || info.status == PluginStatus::Extracting) {
        return;
    }

    if(LocalPlugin::isOnlineAdapter(id)) {
        QDir().mkpath(LocalPlugin::directory(id));
        QFile marker(LocalPlugin::directory(id)+"/installed.json");
        const auto data=QJsonDocument(QJsonObject{{"id",id},{"adapterVersion",1}}).toJson();
        const bool saved=marker.open(QIODevice::WriteOnly) && marker.write(data)==data.size();
        marker.close();
        updateSinglePluginStatus(info);
        if(!saved){info.status=PluginStatus::Failed;info.statusMessage="无法保存在线插件启用状态";}
        emit pluginStatusChanged(id,info.status,info.statusMessage);
        emit pluginsRefreshed();
        return;
    }

    if(info.id!="rapidocr-engine") {
        const auto destination=LocalPlugin::directory(id);
        const auto installer=pluginRootDir()+"/.installer";
        QDir().mkpath(installer);
        for(const auto& name:{"install.py","runner.py"}) {
            QFile::remove(installer+"/"+name);
            QFile::copy(QString(":/local_plugins/")+name,installer+"/"+name);
        }
        info.status=PluginStatus::Extracting;info.statusMessage="正在安装依赖和模型，请稍候（需要 Python 3.12）…";
        emit pluginStatusChanged(id,info.status,info.statusMessage);
        auto* process=new QProcess(this);
        auto* log=new QFile(destination+"-install.log",process);QDir().mkpath(pluginRootDir());log->open(QIODevice::WriteOnly);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process,&QProcess::readyReadStandardOutput,this,[process,log]{log->write(process->readAllStandardOutput());log->flush();});
        connect(process,&QProcess::errorOccurred,this,[this,id](QProcess::ProcessError error){
            if(error!=QProcess::FailedToStart)return;
            for(auto& p:m_plugins)if(p.id==id){p.status=PluginStatus::Failed;p.statusMessage="无法启动 Python 3.12，请安装 Python 后重试。";emit pluginStatusChanged(id,p.status,p.statusMessage);}
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,id,process](int code,QProcess::ExitStatus){
            for(auto& p:m_plugins)if(p.id==id){updateSinglePluginStatus(p);if(code!=0||p.status!=PluginStatus::Installed){p.status=PluginStatus::Failed;p.statusMessage="安装失败，查看 plugins/"+id+"-install.log";}emit pluginStatusChanged(id,p.status,p.statusMessage);}
            TranslationPluginManager::instance().refreshPlugins();emit pluginsRefreshed();process->deleteLater();
        });
        process->start("py",{"-3.12",installer+"/install.py","--id",id,"--destination",destination});
        return;
    }

    info.status = PluginStatus::Downloading;
    info.progressPercent = 0;
    info.downloadedBytes = 0;
    info.totalBytes = info.sizeBytes;
    info.statusMessage = "正在建立高速连接...";
    emit pluginStatusChanged(id, PluginStatus::Downloading, info.statusMessage);

    QString tempZip = QDir::tempPath() + QString("/accushot_plugin_%1.%2").arg(id, QFileInfo(QUrl(info.downloadUrl).path()).suffix());
    if (QFile::exists(tempZip)) {
        QFile::remove(tempZip);
    }

    auto* file = new QFile(tempZip, this);
    if (!file->open(QIODevice::WriteOnly)) {
        info.status = PluginStatus::Failed;
        info.statusMessage = "无法创建临时下载文件";
        emit pluginStatusChanged(id, PluginStatus::Failed, info.statusMessage);
        delete file;
        return;
    }

    m_activeFiles[id] = file;

    QNetworkRequest request(QUrl(info.downloadUrl));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "AccuShot-App/1.0.0 (Windows x64)");

    QNetworkReply* reply = m_nam->get(request);
    m_activeReplies[id] = reply;

    QElapsedTimer timer;
    timer.start();
    m_speedTimers[id] = timer;
    m_lastBytes[id] = 0;

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply, file]() {
        if (file && file->isOpen()) {
            file->write(reply->readAll());
        }
    });

    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 bytesReceived, qint64 bytesTotal) {
        int idx = -1;
        for (int i = 0; i < m_plugins.size(); ++i) {
            if (m_plugins[i].id == id) {
                idx = i;
                break;
            }
        }
        if (idx == -1) return;

        PluginInfo& p = m_plugins[idx];
        p.downloadedBytes = bytesReceived;
        if (bytesTotal > 0) {
            p.totalBytes = bytesTotal;
            p.progressPercent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        } else if (p.sizeBytes > 0) {
            p.progressPercent = qBound(0, static_cast<int>((bytesReceived * 100) / p.sizeBytes), 99);
        }

        double speedMBs = 0.0;
        if (m_speedTimers.contains(id)) {
            qint64 elapsedMs = m_speedTimers[id].elapsed();
            if (elapsedMs > 500) {
                qint64 delta = bytesReceived - m_lastBytes[id];
                speedMBs = (delta / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
                m_speedTimers[id].restart();
                m_lastBytes[id] = bytesReceived;
                p.speedBytesPerSec = speedMBs;
            }
        }

        emit pluginProgressChanged(id, p.progressPercent, bytesReceived, p.totalBytes, p.speedBytesPerSec);
    });

    connect(reply, &QNetworkReply::finished, this, [this, id, reply, file, tempZip]() {
        reply->deleteLater();
        m_activeReplies.remove(id);
        m_speedTimers.remove(id);
        m_lastBytes.remove(id);

        if (file) {
            file->flush();
            file->close();
            delete file;
            m_activeFiles.remove(id);
        }

        int idx = -1;
        for (int i = 0; i < m_plugins.size(); ++i) {
            if (m_plugins[i].id == id) {
                idx = i;
                break;
            }
        }
        if (idx == -1) return;
        PluginInfo& p = m_plugins[idx];

        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || (httpCode >= 400 && httpCode < 600)) {
            QFile::remove(tempZip);
            p.status = PluginStatus::Failed;
            if (httpCode == 404 || reply->error() == QNetworkReply::ContentNotFoundError) {
                p.statusMessage = "下载失败 (HTTP 404): 远端 Releases 尚未上传该扩展包，请先发布资产或在本地放入 plugins/ 目录";
            } else if (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::HostNotFoundError || reply->error() == QNetworkReply::ConnectionRefusedError) {
                p.statusMessage = QString("网络连接受限: %1 (国内访问 GitHub 资产易受阻，建议配置镜像或手动下载)").arg(reply->errorString());
            } else {
                p.statusMessage = QString("下载异常: HTTP %1 - %2").arg(httpCode).arg(reply->errorString());
            }
            emit pluginStatusChanged(id, PluginStatus::Failed, p.statusMessage);
            return;
        }

        // 下载完成，进入解压阶段
        p.status = PluginStatus::Extracting;
        p.statusMessage = "下载完成，正在自动解压部署中...";
        emit pluginStatusChanged(id, PluginStatus::Extracting, p.statusMessage);

        QString appDir = QCoreApplication::applicationDirPath();
        QString destDir = appDir + "/" + p.installSubdir;

        QThreadPool::globalInstance()->start([this, id, tempZip, destDir]() {
            bool ok = extractZipArchive(tempZip, destDir);
            QFile::remove(tempZip);

            QMetaObject::invokeMethod(this, [this, id, ok]() {
                int i = -1;
                for (int k = 0; k < m_plugins.size(); ++k) {
                    if (m_plugins[k].id == id) {
                        i = k;
                        break;
                    }
                }
                if (i == -1) return;

                PluginInfo& info = m_plugins[i];
                if (ok) {
                    updateSinglePluginStatus(info);
                    info.progressPercent = info.status == PluginStatus::Installed ? 100 : 0;
                    info.statusMessage = info.status == PluginStatus::Installed ? "已安装" : "插件包缺少引擎或模型";
                    emit pluginStatusChanged(id, info.status, info.statusMessage);
                    emit pluginsRefreshed();

                    // 自动热重载翻译插件管理器
                    TranslationPluginManager::instance().refreshPlugins();
                } else {
                    info.status = PluginStatus::Failed;
                    info.statusMessage = "自动解压失败，请检查目录权限或手动解压。";
                    emit pluginStatusChanged(id, PluginStatus::Failed, info.statusMessage);
                }
            }, Qt::QueuedConnection);
        });
    });
}

void PluginManager::cancelDownload(const QString& id)
{
    if (m_activeReplies.contains(id)) {
        QNetworkReply* reply = m_activeReplies.take(id);
        reply->abort();
        reply->deleteLater();
    }
    if (m_activeFiles.contains(id)) {
        QFile* file = m_activeFiles.take(id);
        file->close();
        file->remove();
        delete file;
    }
    m_speedTimers.remove(id);
    m_lastBytes.remove(id);

    for (auto& item : m_plugins) {
        if (item.id == id) {
            updateSinglePluginStatus(item);
            emit pluginStatusChanged(id, item.status, "已取消下载");
            break;
        }
    }
}

bool PluginManager::installFromLocalZip(const QString& zipPath, const QString& targetPluginId)
{
    if (!QFile::exists(zipPath)) return false;

    int idx = -1;
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].id == targetPluginId) {
            idx = i;
            break;
        }
    }
    if (idx == -1 && !m_plugins.isEmpty()) idx = 0;
    if (idx == -1) return false;

    PluginInfo& p = m_plugins[idx];
    p.status = PluginStatus::Extracting;
    p.statusMessage = "正在解压本地 ZIP 扩展包...";
    emit pluginStatusChanged(p.id, PluginStatus::Extracting, p.statusMessage);

    QString appDir = QCoreApplication::applicationDirPath();
    QString destDir = appDir + "/" + p.installSubdir;

    bool ok = extractZipArchive(zipPath, destDir);
    if (ok) {
        updateSinglePluginStatus(p);
        if(p.status != PluginStatus::Installed) {p.status=PluginStatus::Failed;p.statusMessage="插件包缺少引擎或模型";emit pluginStatusChanged(p.id,p.status,p.statusMessage);return false;}
        p.statusMessage = "已成功导入本地插件";
        p.progressPercent = 100;
        emit pluginStatusChanged(p.id, PluginStatus::Installed, p.statusMessage);
        TranslationPluginManager::instance().refreshPlugins();
        emit pluginsRefreshed();
        return true;
    } else {
        p.status = PluginStatus::Failed;
        p.statusMessage = "本地 ZIP 解压失败，请检查压缩包是否完整";
        emit pluginStatusChanged(p.id, PluginStatus::Failed, p.statusMessage);
        return false;
    }
}

bool PluginManager::uninstallPlugin(const QString& id)
{
    int index = -1;
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].id == id) {
            index = i;
            break;
        }
    }
    if (index == -1) return false;

    PluginInfo& info = m_plugins[index];
    QString appDir = QCoreApplication::applicationDirPath();
    QString targetDir = appDir + "/" + info.installSubdir;

    if(info.status==PluginStatus::Downloading||info.status==PluginStatus::Extracting)return false;
    // Catalog-owned paths only; remove the optional package including its private models.
    if(!QDir::cleanPath(targetDir).startsWith(QDir::cleanPath(appDir+"/plugins")+"/"))return false;
    const bool removed=!QDir(targetDir).exists() || QDir(targetDir).removeRecursively();
    if(!removed)return false;
    updateSinglePluginStatus(info);
    info.status = PluginStatus::NotInstalled;
    info.statusMessage = "已卸载";
    info.progressPercent = 0;
    emit pluginStatusChanged(id, PluginStatus::NotInstalled, info.statusMessage);

    // 热重载翻译插件管理器
    TranslationPluginManager::instance().refreshPlugins();

    emit pluginsRefreshed();
    return removed;
}

bool PluginManager::extractZipArchive(const QString& zipPath, const QString& destDir)
{
    QDir().mkpath(destDir);

    // 方案 1: Windows 10/11 内置的高性能 bsdtar (无任何外部依赖，极速秒解)
    QProcess process;
    QStringList args;
    args << "-xf" << QDir::toNativeSeparators(zipPath)
         << "-C" << QDir::toNativeSeparators(destDir);

    process.start("tar.exe", args);
    if (process.waitForFinished(60000) && process.exitCode() == 0) {
        return true;
    }

    // 方案 2: 若 tar 异常，优雅降级为 PowerShell Expand-Archive
    QString psCmd = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                    .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir));
    QProcess ps;
    ps.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", psCmd});
    if (ps.waitForFinished(90000) && ps.exitCode() == 0) {
        return true;
    }

    return false;
}
