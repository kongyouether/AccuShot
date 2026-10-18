#include "ui/translation/screen_translation_window.h"
#include "core/theme_manager.h"
#include "core/ocr/ocr_manager.h"
#include "core/translation/translation_plugin_manager.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QPixmap sample(qreal dpr = 1) {
    QPixmap image(qRound(640 * dpr), qRound(240 * dpr));
    image.setDevicePixelRatio(dpr);
    image.fill(QColor("#243146"));
    QPainter painter(&image);
    painter.setPen(Qt::white);
    QFont font("Arial"); font.setPixelSize(23); painter.setFont(font);
    painter.drawText(QRect(24, 36, 580, 36), "Hello world");
    painter.drawText(QRect(24, 112, 580, 36), "The total price is 25 dollars.");
    return image;
}
OcrResult recognized() {
    OcrResult result; result.success = true;
    OcrLine first; first.text = "Hello world"; first.logicalBox = QRect(24, 36, 220, 36);
    OcrLine second; second.text = "The total price is 25 dollars."; second.logicalBox = QRect(24, 112, 560, 36);
    result.lines = {first, second}; result.fullText = first.text + '\n' + second.text;
    return result;
}
ScreenTranslationWindow::Hooks immediateHooks(bool failed = false) {
    ScreenTranslationWindow::Hooks hooks;
    hooks.recognize = [](const QImage&, qreal, const QString&, auto callback) {
        callback(recognized()); return ScreenTranslationWindow::Cancel{};
    };
    hooks.translate = [failed](const QString&, const QString&, const QString&, const QString&, auto callback) {
        TranslationResult result;
        result.success = !failed;
        result.translatedText = failed ? "" : "这是完整译文。";
        result.errorMessage = failed ? "测试服务不可用" : "";
        callback(result); return ScreenTranslationWindow::Cancel{};
    };
    return hooks;
}
}

class ScreenTranslationTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf") >= 0);
        QApplication::setFont(QFont("Microsoft YaHei UI", 9));
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
        settings.setValue("Ocr/Engine", "WindowsMedia");
        settings.setValue("Translation/Engine", "Offline");
    }
    void renderLayoutAndDpr_data() {
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("100percent") << qreal(1);
        QTest::newRow("125percent") << qreal(1.25);
        QTest::newRow("200percent") << qreal(2);
    }
    void renderLayoutAndDpr() {
        QFETCH(qreal, dpr);
        const auto source = sample(dpr);
        QList<ScreenTranslationLine> lines = {
            {QRectF(24, 36, 220, 36), "Hello world", "你好，世界", {}, ScreenTranslationLine::Translated},
            {QRectF(24, 112, 120, 20), "Price", QString("长译文需要完整查看，不应无提示截断。").repeated(20), {}, ScreenTranslationLine::Translated}};
        const auto rendered = ScreenTranslationWindow::renderTranslatedImage(source, lines);
        QCOMPARE(rendered.image.size(), source.size());
        QCOMPARE(rendered.image.devicePixelRatio(), dpr);
        QCOMPARE(rendered.image.pixelColor(qRound(4 * dpr), qRound(4 * dpr)), QColor("#243146"));
        QCOMPARE(rendered.image.pixelColor(qRound(25 * dpr), qRound(37 * dpr)), QColor("#fffef9"));
        QVERIFY(rendered.overflowLines.contains(1));
        QVERIFY(!rendered.overflowLines.contains(0));
        QVERIFY(rendered.image.save(QString("screen-translation-long-%1.png").arg(dpr)));
    }
    void preservesPositionAndShowsFailures() {
        auto* window = new ScreenTranslationWindow(sample(), QRect(-200, 80, 640, 240), immediateHooks(true));
        QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
        window->show(); window->startRecognition();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(window->geometry(), QRect(-200, 80, 640, 240));
        QCOMPARE(window->lines().size(), 2);
        QCOMPARE(window->lines().first().state, ScreenTranslationLine::Failed);
        QVERIFY(window->lines().first().translation.isEmpty());
        QCOMPARE(window->lines().first().error, QString("测试服务不可用"));
        QVERIFY(window->statusText().contains("2 行翻译失败"));
        QVERIFY(!window->findChild<QPushButton*>("screenTranslationCopy")->isEnabled());
        QCOMPARE(finished.first().first().toBool(), false);
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void emptySuccessIsFailureAndMissingLayoutRemainsReadable() {
        auto hooks = immediateHooks();
        hooks.recognize = [](const QImage&, qreal, const QString&, auto callback) {
            OcrResult result; result.success = true; result.fullText = "Text without geometry";
            callback(result); return ScreenTranslationWindow::Cancel{};
        };
        hooks.translate = [](const QString&, const QString&, const QString&, const QString&, auto callback) {
            TranslationResult result; result.success = true;
            callback(result); return ScreenTranslationWindow::Cancel{};
        };
        auto* window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240), hooks);
        QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
        window->startRecognition(); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(window->lines().size(), 1);
        QCOMPARE(window->lines().first().source, QString("Text without geometry"));
        QCOMPARE(window->lines().first().state, ScreenTranslationLine::Failed);
        QVERIFY(window->lines().first().error.contains("有效译文"));
        QVERIFY(window->statusText().contains("位置缺失"));
        QVERIFY(!window->findChild<QPushButton*>("screenTranslationSave")->isEnabled());
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void missingOcrPluginDoesNotFallback() {
        if (OcrManager::instance().isRapidOcrReady()) QSKIP("Missing-plugin negative case needs a test executable without RapidOCR beside it.");
        OcrManager::instance().setPreferredEngineType("RapidOCR");
        auto* window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240));
        QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
        window->startRecognition(); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(window->statusText().contains("安装 RapidOCR"));
        QCOMPARE(window->lines().size(), 0);
        QCOMPARE(finished.first().first().toBool(), false);
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        OcrManager::instance().setPreferredEngineType("WindowsMedia");
    }
    void closesDuringRecognition() {
        auto hooks = immediateHooks();
        ScreenTranslationWindow::OcrCallback late;
        int cancelled = 0, requests = 0;
        hooks.recognize = [&](const QImage&, qreal, const QString&, auto callback) {
            late = callback;
            return ScreenTranslationWindow::Cancel([&] { ++cancelled; });
        };
        hooks.translate = [&](const QString&, const QString&, const QString&, const QString&, auto) {
            ++requests; return ScreenTranslationWindow::Cancel{};
        };
        QPointer<ScreenTranslationWindow> window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240), hooks);
        window->show(); window->startRecognition(); window->close();
        QCOMPARE(cancelled, 1);
        late(recognized());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(window.isNull());
        late(recognized()); QCoreApplication::processEvents();
        QCOMPARE(requests, 0);
    }
    void closesDuringTranslation() {
        auto hooks = immediateHooks();
        ScreenTranslationWindow::TranslateCallback late;
        int cancelled = 0, requests = 0;
        hooks.translate = [&](const QString&, const QString&, const QString&, const QString&, auto callback) {
            ++requests; late = callback;
            return ScreenTranslationWindow::Cancel([&] { ++cancelled; });
        };
        QPointer<ScreenTranslationWindow> window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240), hooks);
        window->show(); window->startRecognition(); QTRY_COMPARE(requests, 1);
        window->close(); QCOMPARE(cancelled, 1);
        TranslationResult result; result.success = true; result.translatedText = "迟到译文";
        late(result); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(window.isNull()); late(result); QCoreApplication::processEvents();
        QCOMPARE(requests, 1);
    }
    void closesFromControlsAndEscape() {
        for (const bool escape : {false, true}) {
            auto hooks = immediateHooks();
            ScreenTranslationWindow::OcrCallback late;
            int cancelled = 0;
            hooks.recognize = [&](const QImage&, qreal, const QString&, auto callback) {
                late = callback; return ScreenTranslationWindow::Cancel([&] { ++cancelled; });
            };
            QPointer<ScreenTranslationWindow> window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240), hooks);
            window->show(); window->startRecognition();
            auto* controls = window->findChild<QDialog*>("screenTranslationControls");
            if (escape) QTest::keyClick(controls, Qt::Key_Escape);
            else controls->close();
            QCOMPARE(cancelled, 1);
            late(recognized()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(window.isNull()); late(recognized()); QCoreApplication::processEvents();
        }
    }
    void retriesIgnoreOldReplyAndKeepSelectedService() {
        auto hooks = immediateHooks();
        QList<ScreenTranslationWindow::TranslateCallback> callbacks;
        QStringList services;
        hooks.translate = [&](const QString&, const QString&, const QString&, const QString& service, auto callback) {
            services.append(service); callbacks.append(callback); return ScreenTranslationWindow::Cancel{};
        };
        auto* window = new ScreenTranslationWindow(sample(), QRect(20, 20, 640, 240), hooks);
        QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
        window->startRecognition(); QTRY_COMPARE(callbacks.size(), 1);
        window->startRecognition(); QTRY_COMPARE(callbacks.size(), 2);
        TranslationResult old; old.success = true; old.translatedText = "旧结果";
        callbacks[0](old); QCoreApplication::processEvents();
        QCOMPARE(window->lines().first().state, ScreenTranslationLine::Pending);
        QCOMPARE(services, QStringList({"Offline", "Offline"}));
        TranslationResult current; current.success = true; current.translatedText = "新结果";
        callbacks[1](current); QTRY_COMPARE(callbacks.size(), 3);
        callbacks[2](current); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(window->lines().first().translation, QString("新结果"));
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void controlsAndOriginalThemes() {
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            auto* window = new ScreenTranslationWindow(sample(1.25), QRect(100, 220, 640, 240), immediateHooks());
            QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
            window->show(); window->startRecognition(); QTRY_COMPARE(finished.size(), 1);
            auto* controls = window->findChild<QDialog*>("screenTranslationControls");
            QVERIFY(controls->isVisible());
            const QString theme = mode == ThemeMode::Light ? "light" : "dark";
            QVERIFY(controls->grab().save("screen-translation-controls-" + theme + ".png"));
            QVERIFY(window->grab().save("screen-translation-result-" + theme + ".png"));
            QTest::keyPress(window, Qt::Key_Space);
            QVERIFY(!controls->isVisible());
            QVERIFY(window->grab().save("screen-translation-original-" + theme + ".png"));
            QTest::keyRelease(window, Qt::Key_Space); QVERIFY(controls->isVisible());
            window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void realLocalModels() {
        if (!qEnvironmentVariableIsSet("EVAN_SCREEN_TRANSLATION_REAL_LOCAL"))
            QSKIP("Set EVAN_SCREEN_TRANSLATION_REAL_LOCAL=1 and run beside installed plugins for synthetic local OCR/Argos validation.");
        qputenv("EVAN_PLUGIN_OFFLINE_TEST", "1");
        QVERIFY2(OcrManager::instance().isRapidOcrReady(), "Run this test executable beside installed RapidOCR, e.g. build-release.");
        QVERIFY(TranslationPluginManager::instance().isOfflinePluginReady());
        OcrManager::instance().setPreferredEngineType("RapidOCR");
        TranslationPluginManager::instance().setPreferredEnginePreference("Offline");
        auto* window = new ScreenTranslationWindow(sample(1.25), QRect(100, 220, 640, 240));
        QSignalSpy finished(window, &ScreenTranslationWindow::workFinished);
        window->show(); window->startRecognition();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 180000);
        QVERIFY2(finished.first().first().toBool(), qPrintable(window->statusText()));
        QVERIFY(window->lines().size() >= 2);
        QString source, translation;
        for (const auto& line : window->lines()) { source += line.source + '\n'; translation += line.translation + '\n'; }
        QVERIFY(source.contains("Hello", Qt::CaseInsensitive));
        QVERIFY(translation.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
        QVERIFY(window->translatedImage().save("screen-translation-real-local.png"));
        QFile report("screen-translation-real-local.json"); QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"source", source}, {"translation", translation}, {"status", window->statusText()}}).toJson());
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir preferences;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    ScreenTranslationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "screen_translation.moc"
