#include "core/ocr/rapid_ocr_engine.h"
#include "core/ocr/windows_media_ocr.h"
#include "core/translation/offline_plugin_translator.h"
#include "core/formula/formula_ocr_manager.h"
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    if(argc!=4)return 2;
    QFile input(QString::fromLocal8Bit(argv[2]));if(!input.open(QIODevice::ReadOnly))return 3;
    const auto request=QJsonDocument::fromJson(input.readAll()).object();
    const QString backend=QString::fromLocal8Bit(argv[1]);QJsonObject output;QElapsedTimer timer;timer.start();
    if(backend=="RapidOCR"||backend=="WindowsMedia") {
        RapidOcrEngine rapid;WindowsMediaOcrEngine windows;IOcrEngine* engine=backend=="RapidOCR"?static_cast<IOcrEngine*>(&rapid):&windows;
        auto result=engine->recognize(QImage(request["image"].toString()),1);
        output={{"success",result.success},{"text",result.fullText},{"error",result.errorMessage}};
    } else if(backend=="translation-en-zh") {
        OfflinePluginTranslator translator;
        auto result=translator.translate(request["text"].toString(),request["source"].toString(),request["target"].toString());
        output={{"success",result.success},{"text",result.translatedText},{"error",result.errorMessage}};
    } else {
        QEventLoop loop;bool done=false;
        FormulaOcrManager::instance().recognizeAsync(QImage(request["image"].toString()),1,[&](const FormulaOcrResult& result){
            output={{"success",result.success},{"text",result.latex},{"error",result.errorMessage}};done=true;loop.quit();
        },backend);
        if(!done)loop.exec();
    }
    output["elapsed_ms"]=timer.elapsed();output["backend"]=backend;
    QFile file(QString::fromLocal8Bit(argv[3]));if(!file.open(QIODevice::WriteOnly))return 4;
    file.write(QJsonDocument(output).toJson());return output["success"].toBool()?0:1;
}
