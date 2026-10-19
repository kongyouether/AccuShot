#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QScopeGuard>
#include "core/plugin/python_runtime.h"
#include "core/plugin/plugin_archive.h"

class PluginInstallTest : public QObject {
    Q_OBJECT
private slots:
    void archiveFormats_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << "7z";
        QTest::newRow("zip") << "zip";
    }
    void archiveFormats() {
        QFETCH(QString, format);
        QTemporaryDir temp(QDir::tempPath() + "/AccuShot 中文 ' 空格-XXXXXX"); QVERIFY(temp.isValid());
        QFile sample(temp.filePath("原文.txt")); QVERIFY(sample.open(QIODevice::WriteOnly));
        const QByteArray content = "archive UTF-8 sample"; sample.write(content); sample.close();
        const auto archive = temp.filePath("插件." + format);
        QProcess pack;
        pack.start(QCoreApplication::applicationDirPath() + "/tools/7zip/7za.exe",
                   {"a", "-t" + format, archive, sample.fileName()});
        QString error; QVERIFY2(PythonDiscovery::finish(pack, 10000, error), qPrintable(error));
        // No tar / PowerShell / system 7-Zip available to the production extractor.
        const auto oldPath = qgetenv("PATH"); const auto restore = qScopeGuard([&]{qputenv("PATH", oldPath);});
        qputenv("PATH", "");
        QVERIFY2(PluginArchive::extract(archive, temp.filePath("解压目录"), error), qPrintable(error));
        QFile extracted(temp.filePath("解压目录/原文.txt")); QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), content);
    }
    void archiveErrors() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QFile bad(temp.filePath("bad.7z")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an archive"); bad.close();
        QString error;
        QVERIFY(!PluginArchive::extract(bad.fileName(), temp.filePath("out"), error));
        QVERIFY(error.contains("7za")); QVERIFY(error.contains("退出码"));
        QVERIFY(!PluginArchive::extract(bad.fileName(), bad.fileName()+"/blocked", error));
        QVERIFY(error.contains("无法创建"));
        const auto tool = QCoreApplication::applicationDirPath()+"/tools/7zip/7za.exe";
        QVERIFY(QFile::rename(tool, tool+".test-disabled"));
        const auto restore = qScopeGuard([&]{QFile::rename(tool+".test-disabled",tool);});
        QVERIFY(!PluginArchive::extract(bad.fileName(), temp.filePath("out"), error));
        QVERIFY(error.contains("缺少 tools/7zip/7za.exe"));
    }
    void rejectsWrongVersionAndArchitecture_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("python311") << QByteArray(R"({"version":[3,11,9],"bits":64,"implementation":"cpython"})");
        QTest::newRow("32bit") << QByteArray(R"({"version":[3,12,3],"bits":32,"implementation":"cpython"})");
        QTest::newRow("pypy") << QByteArray(R"({"version":[3,12,3],"bits":64,"implementation":"pypy"})");
        QTest::newRow("malformed") << QByteArray("not json");
    }
    void rejectsWrongVersionAndArchitecture() {
        QFETCH(QByteArray, response);
        qputenv("ACCUSHOT_TEST_PROBE_RESPONSE", response);
        const auto clear = qScopeGuard([]{qunsetenv("ACCUSHOT_TEST_PROBE_RESPONSE");});
        QString error;
        QVERIFY(!PythonDiscovery::probe(PythonDiscovery::candidate(QCoreApplication::applicationFilePath()), error).valid());
        QVERIFY(!error.isEmpty());
    }
    void missingInterpreterAndTimeout() {
        QString error;
        QVERIFY(!PythonDiscovery::probe(PythonDiscovery::candidate("Z:/absent-python.exe"), error).valid());
        QVERIFY(!error.isEmpty());
        QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--sleep-fixture"});
        QVERIFY(!PythonDiscovery::finish(child, 50, error)); QCOMPARE(error, QString("检测超时"));
        QCOMPARE(child.state(), QProcess::NotRunning);
    }
    void realSystemWithoutLauncher() {
        const auto oldPath=qgetenv("PATH"); const auto restore=qScopeGuard([&]{qputenv("PATH",oldPath);});
        qputenv("PATH", qgetenv("SystemRoot") + "/System32");
        QVERIFY(QStandardPaths::findExecutable("py.exe").isEmpty());
        const auto result = PythonDiscovery::discover();
        QVERIFY2(!result.runtimes.isEmpty(), qPrintable(result.errors.join('\n')));
        bool system=false;
        for(const auto& runtime:result.runtimes) if(runtime.conda.isEmpty()) system=true;
        QVERIFY(system);
    }
    void realPrivateEnvironment_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("system") << qEnvironmentVariable("ACCUSHOT_TEST_SYSTEM_PYTHON");
        QTest::newRow("conda") << qEnvironmentVariable("ACCUSHOT_TEST_CONDA_ROOT");
    }
    void realPrivateEnvironment() {
        QFETCH(QString,path);
        QVERIFY2(!path.isEmpty(), "Set ACCUSHOT_TEST_SYSTEM_PYTHON and ACCUSHOT_TEST_CONDA_ROOT to test real installations");
        const auto result = PythonDiscovery::discover({},path);
        QVERIFY2(!result.runtimes.isEmpty(), qPrintable(result.errors.join('\n')));
        const auto runtime=result.runtimes.front();
        QCOMPARE(!runtime.conda.isEmpty(), QFileInfo(path).isDir());
        QTemporaryDir temp(QDir::tempPath()+"/AccuShot Python 中文 空格-XXXXXX"); QVERIFY(temp.isValid());
        const QString script="import runpy,sys; from pathlib import Path; "
            "runpy.run_path(sys.argv[1])['prepare_environment'](Path(sys.argv[2]))";
        QProcess create;
        runtime.start(create,runtime.executable,{"-I","-c",script,INSTALLER_PATH,temp.path()});
        QString error; QVERIFY2(PythonDiscovery::finish(create,60000,error),qPrintable(error));
        QVERIFY(create.readAllStandardOutput().contains("pip"));
        QProcess verify;
        runtime.start(verify,temp.filePath("venv/Scripts/python.exe"),{"-I","-c",
            "import sys,ssl,json,pip; print(json.dumps({'isolated':sys.prefix!=sys.base_prefix,'pip':pip.__file__,'version':list(sys.version_info[:2])}))"});
        QVERIFY2(PythonDiscovery::finish(verify,15000,error),qPrintable(error));
        const auto data=QJsonDocument::fromJson(verify.readAllStandardOutput()).object();
        QVERIFY(data["isolated"].toBool());
        QVERIFY(QDir::fromNativeSeparators(data["pip"].toString()).startsWith(temp.path()));
        QCOMPARE(data["version"].toArray(), QJsonArray({3,12}));
        qInfo().noquote()<<runtime.label()<<runtime.executable;
    }
};

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--sleep-fixture")){QThread::sleep(5);return 0;}
    if(qEnvironmentVariableIsSet("ACCUSHOT_TEST_PROBE_RESPONSE") && app.arguments().contains("-c")) {
        QTextStream(stdout)<<"ACCUSHOT_PYTHON="<<qEnvironmentVariable("ACCUSHOT_TEST_PROBE_RESPONSE")<<Qt::endl;
        return 0;
    }
    PluginInstallTest test; return QTest::qExec(&test,argc,argv);
}
#include "plugin_install_test.moc"
