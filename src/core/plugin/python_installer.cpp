#include "python_installer.h"
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QProcess>
#include <QFile>
#include <QStandardPaths>

PythonInstaller::PythonInstaller(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), m_network(network) {}

QUrl PythonInstaller::officialUrl() {
    return QUrl("https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe");
}
QByteArray PythonInstaller::officialSha256() {
    // SHA256 from python.org's matching .exe.sigstore messageDigest; pinned with URL.
    return QByteArray::fromHex("67b5635e80ea51072b87941312d00ec8927c4db9ba18938f7ad2d27b328b95fb");
}
QStringList PythonInstaller::arguments(const QString& log) {
    return {"/passive", "/norestart", "InstallAllUsers=0", "Include_launcher=0",
        "Include_pip=1", "Include_test=0", "Include_doc=0", "Include_tcltk=0",
        "PrependPath=0", "AppendPath=0", "AssociateFiles=0", "Shortcuts=0", "/log", log};
}
void PythonInstaller::setMessage(const QString& message) {
    m_message = message; emit changed();
}
void PythonInstaller::complete(bool success, const QString& message) {
    m_busy = false;
    m_success = success;
    m_download.reset();
    setMessage(message); emit finished(success);
}
void PythonInstaller::start(const QUrl& url, const QByteArray& sha256) {
    if (m_busy) return;
    m_busy = true;
    m_success = false;
    setMessage("正在下载 Python 3.12（64 位）…");
    m_download = std::make_unique<QTemporaryDir>(QDir::tempPath()+"/accushot-python-XXXXXX");
    const auto executable = m_download->filePath("python-3.12.10-amd64.exe");
    auto file = std::make_shared<QFile>(executable);
    if (!m_download->isValid() || !file->open(QIODevice::WriteOnly)) {
        complete(false, "无法创建 Python 安装包临时文件：" + file->errorString()); return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto writeError = std::make_shared<QString>();
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = m_network->get(request);
    const auto drain = [reply, file, hash, writeError] {
        const auto bytes = reply->readAll();
        if (file->write(bytes) != bytes.size()) { *writeError = file->errorString(); reply->abort(); return; }
        hash->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (total > 0) setMessage(QString("正在下载 Python 3.12：%1%").arg(done * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, writeError, drain, sha256, executable] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) drain();
        const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (!writeError->isEmpty()) error = "无法写入 Python 安装包：" + *writeError;
        else if (reply->error() != QNetworkReply::NoError || http != 200)
            error = QString("Python 下载失败（HTTP %1）：%2").arg(http).arg(reply->errorString());
        else if (hash->result() != sha256) error = "Python 安装包校验失败，请重新下载安装";
        else if (!file->flush()) error = "无法保存 Python 安装包：" + file->errorString();
        file->close();
        if (!error.isEmpty()) { complete(false, error); return; }
        const auto logs = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs";
        if (!QDir().mkpath(logs)) { complete(false, "无法创建 Python 安装日志目录："+logs); return; }
        const auto log = logs+"/python-setup.log";
        setMessage("正在为当前用户安装 Python 3.12，请等待安装窗口完成…");
        auto* process = new QProcess(this);
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                const auto error = process->errorString(); process->deleteLater();
                complete(false, "无法启动 Python 安装程序："+error);
            }
        });
        connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, log](int code, QProcess::ExitStatus status) {
            process->deleteLater();
            const bool ok = status == QProcess::NormalExit && (code == 0 || code == 3010);
            complete(ok, ok ? "Python 安装程序已完成，正在检测环境…"
                : code == 1602 ? "Python 安装已取消，可点击重试"
                : QString("Python 安装失败（退出码 %1），日志：%2").arg(code).arg(log));
        });
        process->start(executable, arguments(log));
    });
}
