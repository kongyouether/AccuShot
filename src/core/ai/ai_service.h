#pragma once
#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QImage>
#include <QNetworkRequest>
#include <functional>
class QNetworkReply;
struct AiProfile {
    QString id, name, protocol = "openai", endpoint, model, key;
    bool vision = false;
    QJsonObject headers;
    QString translationPrompt = "Translate faithfully. Preserve paragraphs, code and formulas. Return only the translation.";
    QString formulaPrompt = "Transcribe the formula in the image to LaTeX. Return only LaTeX, without explanation or code fences. Do not solve it.";
};
struct AiResult { bool success = false; QString text, error; bool truncated = false; };
class AiService {
public:
    static QList<AiProfile> profiles();
    static QList<AiProfile> presets();
    static bool saveProfiles(const QList<AiProfile>& profiles, QString* error = nullptr);
    static QString secret(const QString& id);
    static bool saveSecret(const QString& id, const QString& key);
    static AiProfile profile(const QString& id);
    static AiProfile forTask(const QString& id, const QString& task);
    static QString taskPrompt(const QString& id, const QString& task, const QString& fallback);
    static bool isLocalProfile(const AiProfile& profile);
    static QJsonObject payload(const AiProfile&, const QString& prompt, const QImage& image = {});
    static QNetworkRequest request(const AiProfile&);
    static AiResult parse(const QString& protocol, const QByteArray& bytes);
    static QNetworkReply* run(const AiProfile&, const QString& prompt, const QImage&, QObject* context,
                             std::function<void(AiResult)> callback);
};
