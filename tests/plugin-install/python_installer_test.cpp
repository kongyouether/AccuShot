#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QStandardPaths>
#include "core/plugin/python_installer.h"

class PythonInstallerTest : public QObject {
    Q_OBJECT
private slots:
    void installerFlow_data() {
        QTest::addColumn<int>("http"); QTest::addColumn<bool>("validHash");
        QTest::addColumn<int>("exitCode"); QTest::addColumn<bool>("expected");
        QTest::addColumn<QString>("message");
        QTest::newRow("success")<<200<<true<<0<<true<<"检测";
        QTest::newRow("reboot-required")<<200<<true<<3010<<true<<"检测";
        QTest::newRow("cancelled")<<200<<true<<1602<<false<<"取消";
        QTest::newRow("install-failed")<<200<<true<<1603<<false<<"1603";
        QTest::newRow("http-failed")<<404<<true<<0<<false<<"下载失败";
        QTest::newRow("hash-mismatch")<<200<<false<<0<<false<<"校验失败";
    }
    void installerFlow() {
        QFETCH(int,http); QFETCH(bool,validHash); QFETCH(int,exitCode);
        QFETCH(bool,expected); QFETCH(QString,message);
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto receipt=temp.filePath("launched.json");
        qputenv("ACCUSHOT_INSTALLER_FIXTURE_RECEIPT",receipt.toUtf8());
        qputenv("ACCUSHOT_INSTALLER_FIXTURE_EXIT",QByteArray::number(exitCode));
        const auto restore=qScopeGuard([]{qunsetenv("ACCUSHOT_INSTALLER_FIXTURE_RECEIPT");qunsetenv("ACCUSHOT_INSTALLER_FIXTURE_EXIT");});
        QFile self(QCoreApplication::applicationFilePath()); QVERIFY(self.open(QIODevice::ReadOnly));
        const auto bytes=self.readAll();
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests=0;
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto* socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                if(socket->property("answered").toBool())return;
                socket->readAll();socket->setProperty("answered",true);++requests;
                socket->write("HTTP/1.1 "+QByteArray::number(http)+" Test\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n");
                socket->write(bytes);socket->disconnectFromHost();
            });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        QNetworkAccessManager network; PythonInstaller installer(&network);
        QSignalSpy completed(&installer,&PythonInstaller::finished);
        const QUrl url(QString("http://127.0.0.1:%1/setup.exe").arg(server.serverPort()));
        const auto hash=validHash?QCryptographicHash::hash(bytes,QCryptographicHash::Sha256):QByteArray(32,'x');
        installer.start(url,hash);QVERIFY(installer.busy());installer.start(url,hash);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,15000);
        QCOMPARE(requests,1);QCOMPARE(installer.busy(),false);
        QCOMPARE(completed[0][0].toBool(),expected);QVERIFY(installer.message().contains(message));
        QCOMPARE(QFile::exists(receipt),http==200&&validHash);
        if(QFile::exists(receipt)) {
            QFile receiptFile(receipt);QVERIFY(receiptFile.open(QIODevice::ReadOnly));
            const auto arguments=QJsonDocument::fromJson(receiptFile.readAll()).array();
            QVERIFY(arguments.contains("InstallAllUsers=0"));QVERIFY(arguments.contains("Include_pip=1"));
            QVERIFY(arguments.contains("PrependPath=0"));QVERIFY(arguments.contains("Include_launcher=0"));
            QVERIFY(arguments.contains("/norestart"));
        }
        // Retry is possible after every terminal state; no automatic retry loop.
        installer.start(url,hash);QTRY_COMPARE_WITH_TIMEOUT(completed.size(),2,15000);
    }
    void officialPackageHash() {
        const auto path=qEnvironmentVariable("ACCUSHOT_TEST_PYTHON_INSTALLER");
        QVERIFY2(!path.isEmpty(),"Set ACCUSHOT_TEST_PYTHON_INSTALLER to a downloaded official installer; this test never runs it");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256),PythonInstaller::officialSha256());
        QCOMPARE(PythonInstaller::officialUrl().host(),QString("www.python.org"));
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("/passive")) {
        QFile receipt(qEnvironmentVariable("ACCUSHOT_INSTALLER_FIXTURE_RECEIPT"));
        if(!receipt.open(QIODevice::WriteOnly))return 1;
        receipt.write(QJsonDocument(QJsonArray::fromStringList(app.arguments())).toJson());receipt.close();
        return qEnvironmentVariableIntValue("ACCUSHOT_INSTALLER_FIXTURE_EXIT");
    }
    QStandardPaths::setTestModeEnabled(true);
    PythonInstallerTest test;return QTest::qExec(&test,argc,argv);
}
#include "python_installer_test.moc"
