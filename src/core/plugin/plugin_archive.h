#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace PluginArchive {
inline bool extract(const QString& archive, const QString& destination, QString& error) {
    const auto tool = QCoreApplication::applicationDirPath() + "/tools/7zip/7za.exe";
    if (!QFileInfo::exists(tool)) { error = "缺少 tools/7zip/7za.exe，请使用完整的 AccuShot 程序包"; return false; }
    if (!QDir().mkpath(destination)) { error = "无法创建插件目录：" + destination; return false; }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, {"x", "-y", "-bsp0", "-sccUTF-8", "-o" + QDir::toNativeSeparators(destination), "--", QDir::toNativeSeparators(archive)});
    if (!process.waitForStarted(5000)) { error = "无法启动 7za：" + process.errorString(); return false; }
    if (!process.waitForFinished(90000)) {
        process.kill(); process.waitForFinished(3000); error = "7za 解压超时（90 秒）"; return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString("7za 解压失败（退出码 %1）：\n%2").arg(process.exitCode())
            .arg(QString::fromUtf8(process.readAll()).trimmed().right(1800));
        return false;
    }
    return true;
}
}
