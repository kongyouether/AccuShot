#include "online_fallback_translator.h"
#include <QThreadPool>
#include <QEventLoop>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QTimer>
#include <QMetaObject>
#include <QDebug>

OnlineFallbackTranslator::OnlineFallbackTranslator(QObject* parent)
    : QObject(parent)
{
}

QList<LanguagePair> OnlineFallbackTranslator::supportedLanguagePairs() const
{
    return {
        {"auto", "zh", "自动检测 ➔ 中文"},
        {"auto", "en", "自动检测 ➔ 英语"},
        {"zh", "en", "中文 ➔ 英语"},
        {"en", "zh", "英语 ➔ 中文"},
        {"zh", "ja", "中文 ➔ 日语"},
        {"ja", "zh", "日语 ➔ 中文"},
        {"zh", "ko", "中文 ➔ 韩语"},
        {"ko", "zh", "韩语 ➔ 中文"},
        {"zh", "ru", "中文 ➔ 俄语"},
        {"ru", "zh", "俄语 ➔ 中文"},
        {"zh", "fr", "中文 ➔ 法语"},
        {"fr", "zh", "法语 ➔ 中文"},
        {"zh", "de", "中文 ➔ 德语"},
        {"de", "zh", "德语 ➔ 中文"}
    };
}

QString OnlineFallbackTranslator::mapLangCode(const QString& lang) const
{
    if (lang.isEmpty() || lang == "auto") return "auto";
    if (lang.startsWith("zh")) return "zh-CN";
    if (lang.startsWith("en")) return "en";
    if (lang.startsWith("ja")) return "ja";
    if (lang.startsWith("ko")) return "ko";
    if (lang.startsWith("ru")) return "ru";
    if (lang.startsWith("fr")) return "fr";
    if (lang.startsWith("de")) return "de";
    return lang.toLower();
}

static QString getYoudaoType(const QString& src, const QString& tgt)
{
    if (src == "zh" && tgt == "en") return "ZH_CN2EN";
    if (src == "en" && tgt == "zh") return "EN2ZH_CN";
    if (src == "zh" && tgt == "ja") return "ZH_CN2JA";
    if (src == "ja" && tgt == "zh") return "JA2ZH_CN";
    if (src == "zh" && tgt == "ko") return "ZH_CN2KR";
    if (src == "ko" && tgt == "zh") return "KR2ZH_CN";
    if (src == "zh" && tgt == "ru") return "ZH_CN2RU";
    if (src == "ru" && tgt == "zh") return "RU2ZH_CN";
    if (src == "zh" && tgt == "fr") return "ZH_CN2FR";
    if (src == "fr" && tgt == "zh") return "FR2ZH_CN";
    if (src == "zh" && tgt == "de") return "ZH_CN2DE";
    if (src == "de" && tgt == "zh") return "DE2ZH_CN";
    if (src == "auto" && tgt == "en") return "AUTO";
    if (src == "auto" && tgt == "zh") return "AUTO";
    return "AUTO";
}

TranslationResult OnlineFallbackTranslator::translate(const QString& text, const QString& srcLang, const QString& targetLang)
{
    TranslationResult result;
    result.originalText = text;
    result.sourceLang = srcLang;
    result.targetLang = targetLang;
    result.engineType = TranslationEngineType::OnlineFallback;
    result.engineName = "在线直连 (国内高速通道)";

    if (text.trimmed().isEmpty()) {
        result.success = true;
        result.translatedText = "";
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    qInfo() << "[Translation] online begin chars=" << text.size() << "source=" << srcLang << "target=" << targetLang;

    // 优先调用国内免鉴权高可用有道高速翻译接口 (100% 国内直连，零梯子)
    QUrl url("https://m.youdao.com/translate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148");
    request.setRawHeader("Referer", "https://m.youdao.com/translate");
    request.setRawHeader("Origin", "https://m.youdao.com");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

    QByteArray postData = "inputtext=" + QUrl::toPercentEncoding(text) +
                          "&type=" + QUrl::toPercentEncoding(getYoudaoType(srcLang, targetLang));

    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.post(request, postData);

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });

    timeoutTimer.start(6000);
    loop.exec();
    // The fallback runs another event loop after reply is deleted. Its predecessor's
    // timeout must not remain armed with a pointer to that deleted reply.
    timeoutTimer.stop();
    const int primaryStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qInfo() << "[Translation] primary reply ms=" << timer.elapsed()
            << "http=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << "networkError=" << int(reply->error());

    result.elapsedMs = timer.elapsed();

    if (reply->error() == QNetworkReply::NoError) {
        QString html = QString::fromUtf8(reply->readAll());
        reply->deleteLater();

        // 提取 <ul id="translateResult">...<li>译文</li>...
        static QRegularExpression re("<ul\\s+id=\"translateResult\">([\\s\\S]*?)</ul>");
        auto match = re.match(html);
        if (match.hasMatch()) {
            QString ulContent = match.captured(1);
            static QRegularExpression liRe("<li>([\\s\\S]*?)</li>");
            auto liIterator = liRe.globalMatch(ulContent);
            QStringList lines;
            while (liIterator.hasNext()) {
                auto m = liIterator.next();
                QString line = m.captured(1).trimmed();
                // 剔除任何 a 标签等导航杂质
                static QRegularExpression tagRe("<[^>]*>");
                line.remove(tagRe);
                // 实体符号反转义
                line.replace("&quot;", "\"")
                    .replace("&amp;", "&")
                    .replace("&lt;", "<")
                    .replace("&gt;", ">")
                    .replace("&#39;", "'")
                    .replace("&nbsp;", " ");
                line = line.trimmed();
                if (!line.isEmpty()) {
                    lines.append(line);
                }
            }
            if (!lines.isEmpty()) {
                result.success = true;
                result.translatedText = lines.join('\n');
                qInfo() << "[Translation] primary success chars=" << result.translatedText.size();
                return result;
            }
        }
    } else {
        reply->deleteLater();
    }

    qInfo() << "[Translation] switching to fallback ms=" << timer.elapsed();
    // 次选在线翻译通道。
    QUrl gtxUrl("https://translate.googleapis.com/translate_a/single");
    QUrlQuery gtxQuery;
    gtxQuery.addQueryItem("client", "gtx");
    gtxQuery.addQueryItem("sl", mapLangCode(srcLang));
    gtxQuery.addQueryItem("tl", mapLangCode(targetLang));
    gtxQuery.addQueryItem("dt", "t");
    gtxQuery.addQueryItem("q", text);
    gtxUrl.setQuery(gtxQuery);

    QNetworkRequest gtxReq(gtxUrl);
    gtxReq.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    QNetworkReply* gtxReply = nam.get(gtxReq);
    QTimer gtxTimer;
    gtxTimer.setSingleShot(true);
    QEventLoop gtxLoop;
    connect(gtxReply, &QNetworkReply::finished, &gtxLoop, &QEventLoop::quit);
    connect(&gtxTimer, &QTimer::timeout, &gtxLoop, [&]() {
        if (gtxReply->isRunning()) gtxReply->abort();
        gtxLoop.quit();
    });
    gtxTimer.start(4000);
    gtxLoop.exec();
    gtxTimer.stop();
    const int fallbackStatus = gtxReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.elapsedMs = timer.elapsed();
    qInfo() << "[Translation] fallback reply ms=" << result.elapsedMs
            << "http=" << gtxReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << "networkError=" << int(gtxReply->error());

    if (gtxReply->error() == QNetworkReply::NoError) {
        QByteArray gtxData = gtxReply->readAll();
        gtxReply->deleteLater();
        QJsonDocument gtxDoc = QJsonDocument::fromJson(gtxData);
        if (gtxDoc.isArray() && !gtxDoc.array().isEmpty()) {
            QJsonArray sArr = gtxDoc.array().at(0).toArray();
            QString gtxTrans;
            for (int k = 0; k < sArr.size(); ++k) {
                QJsonArray item = sArr.at(k).toArray();
                if (!item.isEmpty() && item.at(0).isString()) {
                    gtxTrans += item.at(0).toString();
                }
            }
            if (!gtxTrans.isEmpty()) {
                result.success = true;
                result.translatedText = gtxTrans;
                result.engineName = "在线直连 (高速通道)";
                qInfo() << "[Translation] fallback success chars=" << result.translatedText.size();
                return result;
            }
        }
    } else {
        gtxReply->deleteLater();
    }

    // 若双通道均遇偶发网络阻断
    result.success = false;
    qWarning() << "[Translation] both services failed ms=" << timer.elapsed();
    if (primaryStatus == 413) {
        result.errorMessage = QStringLiteral("原文过长，主翻译服务拒绝请求（HTTP 413）；备用服务未成功（HTTP %1）。请缩小截图范围或分段翻译。").arg(fallbackStatus);
    } else if (primaryStatus == 429 || fallbackStatus == 429) {
        result.errorMessage = QStringLiteral("翻译服务请求过于频繁（HTTP 429），请稍后重试。");
    } else {
        result.errorMessage = QStringLiteral("在线翻译未成功（主服务 HTTP %1，备用服务 HTTP %2；0 表示没有收到 HTTP 响应）。请检查网络或稍后重试。").arg(primaryStatus).arg(fallbackStatus);
    }
    return result;
}

void OnlineFallbackTranslator::translateAsync(const QString& text, const QString& srcLang, const QString& targetLang,
                                             std::function<void(const TranslationResult&)> callback)
{
    QString txt = text;
    QString src = srcLang;
    QString tgt = targetLang;

    QThreadPool::globalInstance()->start([this, txt, src, tgt, callback]() {
        TranslationResult res = translate(txt, src, tgt);
        QMetaObject::invokeMethod(this, [callback, res]() {
            if (callback) {
                callback(res);
            }
        }, Qt::QueuedConnection);
    });
}
