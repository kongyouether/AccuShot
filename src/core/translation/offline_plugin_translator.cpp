#include "offline_plugin_translator.h"
#include "../plugin/local_plugin.h"
#include <QElapsedTimer>
#include <QDebug>
OfflinePluginTranslator::OfflinePluginTranslator(QObject* parent):QObject(parent) {}
QString OfflinePluginTranslator::name() const { return "Argos 中英离线翻译"; }
bool OfflinePluginTranslator::isAvailable() const { return LocalPlugin::ready("translation-en-zh"); }
QString OfflinePluginTranslator::componentPath() const { return LocalPlugin::directory("translation-en-zh"); }
QString OfflinePluginTranslator::modelsDirectory() const { return componentPath()+"/models"; }
QList<LanguagePair> OfflinePluginTranslator::supportedLanguagePairs() const {
    return {{"auto","zh","自动 → 中文"},{"auto","en","自动 → 英语"},{"en","zh","英语 → 中文"},{"zh","en","中文 → 英语"}};
}
TranslationResult OfflinePluginTranslator::translate(const QString& text,const QString& srcLang,const QString& targetLang) {
    TranslationResult result;result.originalText=text;result.sourceLang=srcLang;result.targetLang=targetLang;
    result.engineName=name();result.engineType=TranslationEngineType::OfflinePlugin;
    QElapsedTimer timer;timer.start();
    auto json=LocalPlugin::run("translation-en-zh",{{"text",text},{"source",srcLang},{"target",targetLang}});
    result.success=json.value("success").toBool();result.translatedText=json.value("text").toString();
    result.errorMessage=json.value("error").toString();result.elapsedMs=timer.elapsed();return result;
}
void OfflinePluginTranslator::translateAsync(const QString& text, const QString& srcLang, const QString& targetLang,
                                            std::function<void(const TranslationResult&)> callback)
{
    QString txt = text;
    QString src = srcLang;
    QString tgt = targetLang;

    QThreadPool::globalInstance()->start([this, txt, src, tgt, callback]() {
        QElapsedTimer elapsed;
        elapsed.start();
        qInfo() << "[Translation] offline worker begin chars=" << txt.size();
        TranslationResult res = translate(txt, src, tgt);
        qInfo() << "[Translation] offline worker end success=" << res.success << "ms=" << elapsed.elapsed();
        QMetaObject::invokeMethod(this, [callback, res]() {
            if (callback) {
                callback(res);
            }
        }, Qt::QueuedConnection);
    });
}
