#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThreadPool>
#include <functional>

// Optional model packages live exclusively under this executable's plugins/.
namespace LocalPlugin {
inline QString directory(const QString& id) {
    return QCoreApplication::applicationDirPath()+"/plugins/"+id;
}
inline bool isOnlineAdapter(const QString& id) {
    return id=="markdown-vision" || id=="markdown-mathpix";
}
inline bool ready(const QString& id) {
    const auto root=directory(id);
    QFile file(root+"/installed.json");
    if(!file.open(QIODevice::ReadOnly))return false;
    const auto manifest=QJsonDocument::fromJson(file.readAll()).object();
    if(isOnlineAdapter(id))return manifest.value("id").toString()==id && manifest.value("adapterVersion").toInt()==1;
    const auto weights=manifest.value("weights").toArray();
    if(weights.isEmpty())return false;
    for(const auto& weight:weights) {
        const QString path=QDir::cleanPath(root+"/"+weight.toString());
        if(!path.startsWith(root+"/")||QFileInfo(path).size()<1024*1024)return false;
    }
    return manifest.value("id").toString()==id
        && QFile::exists(root+"/venv/Scripts/python.exe") && QFile::exists(root+"/runner.py");
}
inline QJsonObject run(const QString& id, QJsonObject request) {
    if(!ready(id))return {{"success",false},{"error","请先在设置 → 插件与模型中安装该功能插件。"}};
    QTemporaryDir temp;
    QFile input(temp.filePath("input.json"));
    if(!input.open(QIODevice::WriteOnly))return {{"success",false},{"error","无法写入插件请求"}};
    input.write(QJsonDocument(request).toJson());input.close();
    const auto root=directory(id);
    QProcess process;
    process.setWorkingDirectory(root);
    process.start(root+"/venv/Scripts/python.exe",{root+"/runner.py","--backend",id,"--input",input.fileName(),"--output",temp.filePath("output.json")});
    if(!process.waitForStarted(5000))return {{"success",false},{"error",process.errorString()}};
    if(!process.waitForFinished(180000)) {
        process.kill();process.waitForFinished();
        return {{"success",false},{"error","本地模型处理超时（180 秒）"}};
    }
    QFile output(temp.filePath("output.json"));
    if(output.open(QIODevice::ReadOnly)) {
        auto result=QJsonDocument::fromJson(output.readAll()).object();
        if(result.contains("success"))return result;
    }
    return {{"success",false},{"error",QString::fromUtf8(process.readAllStandardError()).right(1600)}};
}
}
