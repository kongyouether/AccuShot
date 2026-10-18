#pragma once
#include "../ai/ai_service.h"
#include <QTemporaryDir>
#include <memory>

struct MarkdownProvider {
    QString id, name, description;
    bool available = false;
};
struct MarkdownResult {
    bool success = false;
    QString text, error;
    std::shared_ptr<QTemporaryDir> files;
};
namespace MarkdownConverter {
QList<MarkdownProvider> providers();
QString prompt();
QString plainText(const QString& text);
QNetworkReply* convert(const QString& provider, const QImage& image, QObject* context,
                       std::function<void(MarkdownResult)> callback);
bool save(const QString& path, const QString& text, const MarkdownResult& result, QString* error);
}
