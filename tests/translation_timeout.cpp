#include "online_fallback_translator.h"
#include <QCoreApplication>
#include <QDebug>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qCritical() << "Cannot bind regression HTTP server:" << server.errorString();
        return 2;
    }
    qputenv("EVAN_TEST_HTTP_PORT", QByteArray::number(server.serverPort()));
    int primaryRequests = 0;
    int fallbackRequests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (socket->property("scheduled").toBool() || !request.contains("\r\n\r\n")) return;
                socket->setProperty("scheduled", true);
                bool primary = request.startsWith("POST /primary ");
                bool fallback = request.startsWith("GET /fallback?");
                if (primary) ++primaryRequests;
                if (fallback) ++fallbackRequests;
                if (!primary && !fallback) {
                    qCritical() << "Unexpected HTTP request:" << request.left(request.indexOf('\r'));
                    app.exit(3);
                    return;
                }
                // Primary completes at ~3.2 s; fallback completes at ~6.5 s.
                // An uncancelled 6 s primary timer fires after its reply is deleted.
                QTimer::singleShot(primary ? 3200 : 3300, socket, [socket, primary] {
                    QByteArray body = primary ? QByteArray("<html>unusable primary response</html>")
                        : QByteArray("[[[\"translated by fallback\",\"hello\",null,null,1]],null,\"en\"]");
                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n\r\n" + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            });
        }
    });
    OnlineFallbackTranslator translator;
    int callbackCount = 0;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] {
        qCritical() << "Translation callback did not complete";
        app.exit(4);
    });
    watchdog.start(12000);
    translator.translateAsync("hello", "en", "zh", [&](const TranslationResult& result) {
        ++callbackCount;
        watchdog.stop();
        bool passed = result.success && result.translatedText == "translated by fallback"
            && primaryRequests == 1 && fallbackRequests == 1 && callbackCount == 1;
        if (passed) qInfo() << "PASS: delayed primary reply deletion followed by successful fallback";
        else qCritical() << "FAIL:" << result.success << result.translatedText << result.errorMessage
                            << primaryRequests << fallbackRequests << callbackCount;
        app.exit(passed ? 0 : 1);
    });
    const int result = app.exec();
    QThreadPool::globalInstance()->waitForDone();
    return result;
}
