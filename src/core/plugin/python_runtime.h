#pragma once

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>

// A base interpreter only. Packages are always installed into a plugin's own venv.
struct PythonRuntime {
    QString executable;
    QString conda;
    QString prefix;
    QString version;

    bool valid() const { return !executable.isEmpty() && !version.isEmpty(); }
    QString label() const {
        return QString("Python %1 · 64 位 · %2").arg(version, conda.isEmpty() ? "系统安装" : "Conda");
    }
    QJsonObject toJson() const {
        return {{"executable", executable}, {"conda", conda}, {"prefix", prefix}, {"version", version}};
    }
    static PythonRuntime fromJson(const QJsonObject& o) {
        return {o["executable"].toString(), o["conda"].toString(), o["prefix"].toString(), o["version"].toString()};
    }
    void start(QProcess& process, const QString& python, const QStringList& args) const {
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("PYTHONHOME"); env.remove("PYTHONPATH");
        env.insert("PYTHONUTF8", "1"); env.insert("PYTHONIOENCODING", "utf-8");
        process.setProcessEnvironment(env);
        const auto pythonArgs = QStringList{"-X", "utf8"} + args;
        if (conda.isEmpty()) process.start(python, pythonArgs);
        else process.start(conda, QStringList{"run", "--no-capture-output", "-p", prefix, python} + pythonArgs);
    }
};

namespace PythonDiscovery {
inline QString normalized(const QString& path) {
    const QFileInfo file(path);
    return QDir::fromNativeSeparators(file.exists() ? file.canonicalFilePath() : file.absoluteFilePath());
}
inline QString condaAt(const QString& root) {
    for (const auto& relative : {"Scripts/conda.exe", "_conda.exe"}) {
        const auto path = root + "/" + relative;
        if (QFileInfo::exists(path)) return normalized(path);
    }
    return {};
}
inline PythonRuntime candidate(const QString& path, const QString& conda = {}) {
    PythonRuntime result;
    result.executable = normalized(path);
    const auto dir = QFileInfo(result.executable).absolutePath();
    if (QDir(dir + "/conda-meta").exists()) {
        result.prefix = dir;
        result.conda = condaAt(dir);
        if (result.conda.isEmpty()) result.conda = condaAt(QDir::cleanPath(dir + "/../.."));
        if (result.conda.isEmpty()) result.conda = conda;
    }
    return result;
}
inline bool finish(QProcess& process, int timeout, QString& error) {
    if (!process.waitForStarted(3000)) { error = process.errorString(); return false; }
    if (!process.waitForFinished(timeout)) {
        process.kill(); process.waitForFinished(3000);
        error = "检测超时"; return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed().right(1200);
        if (error.isEmpty()) error = QString("退出码 %1").arg(process.exitCode());
        return false;
    }
    return true;
}
inline PythonRuntime probe(PythonRuntime runtime, QString& error) {
    if (!runtime.prefix.isEmpty() && runtime.conda.isEmpty()) {
        error = "检测到 Conda Python，但未找到 conda.exe，请选择 Conda 安装目录"; return {};
    }
    // Probe across the process boundary; names and registry versions are not proof.
    const QString script = "import sys,struct,json,ssl,venv,ensurepip; "
        "print('ACCUSHOT_PYTHON='+json.dumps({'executable':sys.executable,'version':list(sys.version_info[:3]),"
        "'bits':struct.calcsize('P')*8,'implementation':sys.implementation.name}))";
    QProcess process;
    runtime.start(process, runtime.executable, {"-I", "-c", script});
    if (!finish(process, 12000, error)) return {};
    for (const auto& line : process.readAllStandardOutput().split('\n')) {
        if (!line.startsWith("ACCUSHOT_PYTHON=")) continue;
        const auto o = QJsonDocument::fromJson(line.mid(16).trimmed()).object();
        const auto version = o["version"].toArray();
        if (version.size() != 3 || version[0].toInt() != 3 || version[1].toInt() != 12
            || o["bits"].toInt() != 64 || o["implementation"].toString() != "cpython") {
            error = "需要 CPython 3.12（64 位）"; return {};
        }
        runtime.executable = normalized(o["executable"].toString());
        runtime.version = QString("%1.%2.%3").arg(version[0].toInt()).arg(version[1].toInt()).arg(version[2].toInt());
        return runtime;
    }
    error = "解释器未返回有效的检测结果";
    return {};
}
struct Result { QList<PythonRuntime> runtimes; QStringList errors; };

inline Result discover(const PythonRuntime& preferred = {}, const QString& manual = {}) {
    Result result;
    QStringList pythonPaths, condaPaths;
    auto append = [](QStringList& list, const QString& path) {
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            const auto clean = normalized(path);
            if (!list.contains(clean, Qt::CaseInsensitive)) list.append(clean);
        }
    };
    if (!preferred.executable.isEmpty()) append(pythonPaths, preferred.executable);
    if (!manual.isEmpty()) {
        if (QFileInfo(manual).isDir()) append(condaPaths, condaAt(manual));
        else append(pythonPaths, manual);
        if (pythonPaths.isEmpty() && condaPaths.isEmpty()) result.errors << "指定位置不存在或不是 Conda 安装目录";
    } else {
        // PEP 514 also finds installations not registered in PATH or the launcher.
        for (const auto& root : {"HKEY_CURRENT_USER\\Software\\Python", "HKEY_LOCAL_MACHINE\\Software\\Python"}) {
            QSettings registry(root, QSettings::NativeFormat);
            for (const auto& company : registry.childGroups()) {
                registry.beginGroup(company);
                for (const auto& tag : registry.childGroups()) {
                    registry.beginGroup(tag + "/InstallPath");
                    auto exe = registry.value("ExecutablePath").toString();
                    if (exe.isEmpty()) exe = registry.value(".").toString() + "/python.exe";
                    append(pythonPaths, exe); registry.endGroup();
                }
                registry.endGroup();
            }
        }
        for (const auto& dir : qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts)) {
            // WindowsApps python.exe can open the Store instead of executing a probe.
            if (!dir.contains("Microsoft/WindowsApps", Qt::CaseInsensitive)
                && !dir.contains("Microsoft\\WindowsApps", Qt::CaseInsensitive))
                append(pythonPaths, dir + "/python.exe");
            append(condaPaths, dir + "/conda.exe");
        }
        const auto launcher = QStandardPaths::findExecutable("py.exe");
        if (!launcher.isEmpty()) {
            QProcess p; p.start(launcher, {"-3.12", "-I", "-c", "import sys; print(sys.executable)"});
            QString error;
            if (finish(p, 5000, error)) append(pythonPaths, QString::fromUtf8(p.readAllStandardOutput()).trimmed());
        }
    }
    // An explicitly selected custom-prefix Conda environment may be outside root/envs.
    if (manual.isEmpty() || QFileInfo(manual).isFile()) {
        append(condaPaths, qEnvironmentVariable("CONDA_EXE"));
        append(condaPaths, QStandardPaths::findExecutable("conda.exe"));
        for (const auto& parent : {QDir::homePath(), qEnvironmentVariable("LOCALAPPDATA"),
                                  qEnvironmentVariable("ProgramData"), QString("C:/")})
            for (const auto& name : {"anaconda3", "miniconda3", "miniforge3", "mambaforge"})
                append(condaPaths, condaAt(parent + "/" + name));
    }
    append(condaPaths, preferred.conda);
    QElapsedTimer budget; budget.start();
    QStringList seen;
    auto check = [&](const QString& path, const QString& conda = QString()) {
        const auto key = normalized(path);
        if (seen.contains(key, Qt::CaseInsensitive) || budget.elapsed() > 45000) return;
        auto c = candidate(path, conda);
        // Retry with a discovered Conda executable when an earlier PATH candidate lacked it.
        if (c.prefix.isEmpty() || !c.conda.isEmpty()) seen << key;
        QString error;
        const auto runtime = probe(c, error);
        if (runtime.valid()) result.runtimes << runtime;
        else result.errors << key + ": " + error;
    };
    for (const auto& path : pythonPaths) check(path, condaPaths.isEmpty() ? QString() : condaPaths.front());
    if (!manual.isEmpty() && QFileInfo(manual).isFile()) return result;
    for (const auto& conda : condaPaths) {
        if (budget.elapsed() > 45000) break;
        QProcess p; p.start(conda, {"env", "list", "--json"});
        QString error;
        if (!finish(p, 10000, error)) { result.errors << conda + ": " + error; continue; }
        const auto object = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
        if (!object["envs"].isArray()) { result.errors << conda + ": 无有效环境列表"; continue; }
        for (const auto& env : object["envs"].toArray()) check(env.toString() + "/python.exe", conda);
    }
    if (budget.elapsed() > 45000) result.errors << "检测达到时间上限，可手动指定解释器";
    return result;
}
}
