#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <memory>

// Download and launch an official per-user installer. Interpreter readiness is
// checked separately by PluginManager after the installer exits.
class PythonInstaller : public QObject {
    Q_OBJECT
public:
    explicit PythonInstaller(QNetworkAccessManager* network, QObject* parent = nullptr);
    static QUrl officialUrl();
    static QByteArray officialSha256();
    static QStringList arguments(const QString& log);
    bool busy() const { return m_busy; }
    bool succeeded() const { return m_success; }
    QString message() const { return m_message; }
    void start(const QUrl& url = officialUrl(), const QByteArray& sha256 = officialSha256());
signals:
    void changed();
    void finished(bool success);
private:
    void complete(bool success, const QString& message);
    void setMessage(const QString& message);
    QNetworkAccessManager* m_network;
    std::unique_ptr<QTemporaryDir> m_download;
    bool m_busy = false;
    bool m_success = false;
    QString m_message;
};
