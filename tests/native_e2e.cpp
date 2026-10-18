#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QThreadPool>
#include <memory>
#include "core/screen_capturer.h"
#include "core/hotkey_config.h"
#include "core/export/image_exporter.h"
#include "core/ocr/ocr_manager.h"
#include "core/translation/online_fallback_translator.h"
#include "ui/capture/scroll_capture_widget.h"
#include "ui/capture/gif_recorder_widget.h"
#include "ui/annotation/inplace_text_editor.h"
#include "ui/overlay/snipping_overlay.h"
#include "ui/ocr/ocr_result_dialog.h"
#include "ui/pin/pin_manager.h"

class CaptureFixture : public QWidget {
public:
    bool scrollContent = false;
    int scrollOffset = 0;
    QColor animationColor;
    QString heading = "Evan OCR 12345";
    bool denseText = false;
    static QColor rowColor(int row) { return QColor((row * 71 + 37) % 220, (row * 43 + 67) % 220, (row * 97 + 23) % 220); }
    CaptureFixture() { setWindowTitle("Evan native screenshot test"); setWindowFlag(Qt::WindowStaysOnTopHint); resize(760, 400); }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        if (denseText) {
            painter.setPen(Qt::black);
            painter.setFont(QFont("Arial", 14));
            for (int row = 0; row < 22; ++row)
                painter.drawText(30, 35 + row * 23, QString("Line %1: Good morning. Translation should keep this window open and responsive.").arg(row + 1));
            return;
        }
        painter.fillRect(QRect(710, 20, 24, 24), QColor(17, 83, 149));
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 32));
        painter.drawText(35, 85, heading);
        painter.setFont(QFont("Microsoft YaHei", 30));
        painter.drawText(35, 155, QString::fromUtf8("截图识别测试"));
        if (scrollContent) {
            painter.save();
            painter.setClipRect(QRect(20, 200, 400, 200));
            painter.setFont(QFont("Arial", 10));
            for (int row = 0; row < 24; ++row) {
                const int y = 200 + row * 20 - scrollOffset;
                painter.fillRect(QRect(20, y, 400, 20), rowColor(row));
                painter.setPen(Qt::white);
                painter.drawText(90, y + 15, QString("ROW %1").arg(row, 2, 10, QChar('0')));
            }
            painter.restore();
        }
        if (animationColor.isValid()) painter.fillRect(QRect(40, 220, 120, 80), animationColor);
    }
};

class NativeE2e : public QObject {
    Q_OBJECT
    CaptureFixture fixture;
    QString output;
    static void drag(QWidget* target, QPoint from, QPoint to) {
        QTest::mousePress(target, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(target, to, 30);
        QTest::mouseRelease(target, Qt::LeftButton, Qt::NoModifier, to);
    }
    static void shortcut(QWidget* target, const QString& keys) {
        QTest::keySequence(target, QKeySequence(keys));
    }
    static void acceptExportDialogs(QTimer& timer, const QString& path) {
        QObject::connect(&timer, &QTimer::timeout, &timer, [path] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
                    dialog->setDirectory(QFileInfo(path).absolutePath());
                    dialog->findChild<QLineEdit*>("fileNameEdit")->setText(QFileInfo(path).fileName());
                    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                } else if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
                    message->accept();
                }
            }
        });
        timer.start(50);
    }
    void selectFixture(SnippingOverlay& overlay) {
        overlay.startSnipping();
        QTest::qWait(200);
        const QPoint from = overlay.mapFromGlobal(fixture.mapToGlobal(QPoint(20, 20)));
        drag(&overlay, from, from + QPoint(680, 310));
    }
private slots:
    void initTestCase() {
        QVERIFY2(QGuiApplication::platformName() == "windows", "This test requires native Windows, never offscreen.");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        output = QCoreApplication::applicationDirPath() + "/results";
        QVERIFY(QDir().mkpath(output + "/settings"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output + "/settings");
        // Only this test's INI file is written; legacy native settings remain untouched.
        QSettings isolated(QSettings::IniFormat, QSettings::UserScope, "Evan", "Evan");
        isolated.setValue("Theme/Mode", "Dark");
        isolated.setValue("Hotkey/Snipping", "F1");
        fixture.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(100, 100));
        fixture.show();
        fixture.raise();
        fixture.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(300);
    }
    void screenshotAnnotationsClipboardAndExport() {
        const auto desktop = ScreenCapturer::captureVirtualScreen();
        QVERIFY(desktop.isValid);
        const QPoint marker = fixture.mapToGlobal(QPoint(720, 30)) - desktop.virtualGeometry.topLeft();
        const qreal screenDpr = desktop.fullSnapshot.devicePixelRatio();
        QCOMPARE(desktop.fullSnapshot.toImage().pixelColor(qRound(marker.x() * screenDpr), qRound(marker.y() * screenDpr)), QColor(17, 83, 149));
        QVERIFY(desktop.fullSnapshot.save(output + "/desktop.png"));
        auto& overlay = SnippingOverlay::instance();
        selectFixture(overlay);
        QVERIFY(overlay.isVisible());
        QVERIFY(overlay.hasValidSelection());
        QCOMPARE(overlay.selectedRect().size(), QSize(681, 311));
        auto* toolbar = overlay.findChild<FloatingToolbar*>();
        QVERIFY(toolbar && toolbar->isVisible());
        QVERIFY(overlay.grab().save(output + "/overlay-selected.png"));
        const QRect selected = overlay.selectedRect();
        const qreal dpr = overlay.currentSnapshot().fullSnapshot.devicePixelRatio();
        const QPoint base = selected.topLeft();
        const auto& keys = HotkeyConfig::instance().data();
        shortcut(&overlay, keys.toolRect);
        const QImage noRect = overlay.grab(selected).toImage();
        drag(&overlay, base + QPoint(420, 170), base + QPoint(610, 250));
        QVERIFY(overlay.grab(selected).toImage() != noRect);
        shortcut(&overlay, keys.toolArrow);
        const QImage noArrow = overlay.grab(selected).toImage();
        drag(&overlay, base + QPoint(320, 260), base + QPoint(430, 210));
        QVERIFY(overlay.grab(selected).toImage() != noArrow);
        shortcut(&overlay, keys.toolText);
        const QImage beforeText = overlay.grab(selected).toImage();
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, base + QPoint(30, 210));
        auto* editor = overlay.findChild<InPlaceTextEditor*>();
        QVERIFY(editor && editor->isVisible());
        QTest::keyClicks(editor, "Native annotation");
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->isVisible());
        QVERIFY(overlay.grab(selected).toImage() != beforeText);
        overlay.setFocus();
        QTest::keyClick(&overlay, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(overlay.grab(selected).toImage(), beforeText);
        QVERIFY(overlay.grab().save(output + "/overlay-annotated.png"));
        QSignalSpy finished(&overlay, &SnippingOverlay::snippingFinished);
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!overlay.isVisible());
        const QImage copied = QApplication::clipboard()->image();
        QCOMPARE(copied.size(), QSize(qRound(selected.width() * dpr), qRound(selected.height() * dpr)));
        // The selected lower-left background is white in the actual desktop fixture.
        QCOMPARE(copied.pixelColor(qRound(10 * dpr), qRound(280 * dpr)).rgb(), QColor(Qt::white).rgb());
        for (const QString& extension : {QString("png"), QString("gif")}) {
            const QString path = output + "/capture." + extension;
            QVERIFY2(ImageExporter::saveImage(copied, path), qPrintable(path));
            QImage reloaded(path);
            QCOMPARE(reloaded.size(), copied.size());
            // File pixels are preserved; Qt's in-memory DPR is not PNG metadata.
            reloaded.setDevicePixelRatio(copied.devicePixelRatio());
            if (extension == "png") QCOMPARE(reloaded.convertToFormat(QImage::Format_ARGB32), copied.convertToFormat(QImage::Format_ARGB32));
        }
    }
    void doubleClickSelectionCopiesClipboard() {
        auto& overlay = SnippingOverlay::instance();
        selectFixture(overlay);
        QVERIFY(overlay.hasValidSelection());
        const QRect selected = overlay.selectedRect();
        const qreal dpr = overlay.currentSnapshot().fullSnapshot.devicePixelRatio();
        QSignalSpy finished(&overlay, &SnippingOverlay::snippingFinished);
        overlay.selectTool(ToolAction::Rect);
        QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, selected.center());
        QCOMPARE(finished.count(), 0);
        QVERIFY(overlay.isVisible());
        overlay.selectTool(ToolAction::Text);
        QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, selected.center());
        auto* editor = overlay.findChild<InPlaceTextEditor*>();
        QVERIFY(editor && editor->isVisible());
        QCOMPARE(finished.count(), 0);
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        overlay.selectTool(ToolAction::Cancel);
        QApplication::clipboard()->clear();
        QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, selected.center());
        QCOMPARE(finished.count(), 1);
        QVERIFY(!overlay.isVisible());
        QCOMPARE(finished.at(0).at(1).toRect(), selected);
        const QImage copied = QApplication::clipboard()->image();
        QCOMPARE(copied.size(), QSize(qRound(selected.width() * dpr), qRound(selected.height() * dpr)));
        QCOMPARE(copied.pixelColor(qRound(10 * dpr), qRound(280 * dpr)).rgb(), QColor(Qt::white).rgb());
    }
    void pinCopyAndScale() {
        auto& overlay = SnippingOverlay::instance();
        selectFixture(overlay);
        QPointer<PinWindow> pin;
        QPixmap pinned;
        const QRect selected = overlay.selectedRect();
        const auto connection = connect(&overlay, &SnippingOverlay::pinRequested, this,
            [&](const QPixmap& pixmap, const QRect& region) {
                pinned = pixmap;
                pin = PinManager::instance().createPin(pixmap, region);
            });
        QTest::keyClick(&overlay, Qt::Key_F3);
        disconnect(connection);
        QVERIFY(pin && pin->isVisible());
        QCOMPARE(pin->contentRect().size(), selected.size());
        QCOMPARE(pin->mapToGlobal(pin->contentRect().topLeft()), selected.topLeft() + overlay.currentSnapshot().virtualGeometry.topLeft());
        QTest::keyClick(pin, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->image().size(), pinned.size());
        const QSize original = pin->size();
        const QPoint anchor = pin->rect().center();
        QWheelEvent wheel(anchor, pin->mapToGlobal(anchor), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(pin, &wheel);
        QVERIFY(pin->width() > original.width());
        QVERIFY(pin->height() > original.height());
        QVERIFY(pin->grab().save(output + "/pin-scaled.png"));
        QTest::keyClick(pin, Qt::Key_Escape);
        QTRY_VERIFY(pin.isNull());
    }
    void copyRecognizedTextFromToolbar() {
        auto& overlay = SnippingOverlay::instance();
        selectFixture(overlay);
        const QRect initial = overlay.selectedRect();
        QTest::keyClick(&overlay, Qt::Key_Right);
        QCOMPARE(overlay.selectedRect(), initial.translated(1, 0));
        QTest::keyClick(&overlay, Qt::Key_Left);
        QTest::keyClick(&overlay, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(overlay.selectedRect().width(), initial.width() + 1);
        auto* toolbar = overlay.findChild<FloatingToolbar*>();
        QVERIFY(toolbar);
        toolbar->actionTriggered(ToolAction::CopyText);
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 20000);
        const QString copied = QApplication::clipboard()->text();
        QVERIFY2(copied.contains("Evan", Qt::CaseInsensitive), qPrintable(copied));
        QVERIFY(copied.contains("12345"));
    }
    void windowsOcrOnCapturedText() {
        fixture.raise();
        fixture.activateWindow();
        QTest::qWait(200);
        const auto shot = ScreenCapturer::captureVirtualScreen();
        QVERIFY(shot.isValid);
        const qreal dpr = shot.fullSnapshot.devicePixelRatio();
        const QPoint pos = fixture.mapToGlobal(QPoint()) - shot.virtualGeometry.topLeft();
        const QImage input = shot.fullSnapshot.copy(QRect(qRound(pos.x() * dpr), qRound(pos.y() * dpr), qRound(fixture.width() * dpr), qRound(190 * dpr))).toImage();
        QVERIFY(input.save(output + "/ocr-input.png"));
        struct Outcome { bool called = false; OcrResult result; QString engine; };
        auto outcome = std::make_shared<Outcome>();
        QEventLoop loop;
        QPointer<QEventLoop> activeLoop(&loop);
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        OcrManager::instance().recognizeWithEngine("WindowsMedia", input, dpr,
            [outcome, activeLoop](const OcrResult& result, const QString& engine) {
                outcome->called = true; outcome->result = result; outcome->engine = engine;
                if (activeLoop) activeLoop->quit();
            });
        timer.start(20000);
        if (!outcome->called) loop.exec();
        QVERIFY2(outcome->called, "Windows OCR did not return within 20 seconds");
        qInfo().noquote() << "OCR engine:" << outcome->engine << "text:" << outcome->result.fullText;
        QVERIFY2(outcome->result.success, qPrintable(outcome->result.errorMessage));
        QVERIFY(outcome->result.fullText.contains("Evan", Qt::CaseInsensitive));
        QString compact = outcome->result.fullText;
        compact.remove(' ');
        QVERIFY(compact.contains("12345"));
        QVERIFY(!outcome->result.lines.isEmpty());
    }
    void longScreenshotCapturesRealScrolledViewport() {
        fixture.scrollContent = true;
        fixture.scrollOffset = 0;
        fixture.raise();
        fixture.repaint();
        QTest::qWait(250);
        const QRect region(fixture.mapToGlobal(QPoint(40, 220)), QSize(220, 160));
        const QPixmap initial = fixture.screen()->grabWindow(0, region.x(), region.y(), region.width(), region.height());
        QVERIFY(!initial.isNull());
        const qreal dpr = initial.devicePixelRatio();
        auto& overlay = SnippingOverlay::instance();
        overlay.startSnipping();
        QTest::qWait(200);
        drag(&overlay, overlay.mapFromGlobal(region.topLeft()), overlay.mapFromGlobal(region.bottomRight()));
        overlay.findChild<FloatingToolbar*>()->actionTriggered(ToolAction::ScrollCapture);
        auto* active = overlay.findChild<ScrollCaptureSession*>();
        QVERIFY(active && active->isActive());
        auto& session = *active;
        QSignalSpy finished(&session, &ScrollCaptureSession::finished);
        for (int offset : {40, 80, 120}) {
            fixture.scrollOffset = offset;
            fixture.repaint();
            QTest::qWait(450);
        }
        // Leave the viewport still for several samples: duplicate frames must not add rows.
        QTest::qWait(250);
        const QString path = output + "/long-real-scroll.png";
        QFile::remove(path);
        QTimer dialogs;
        acceptExportDialogs(dialogs, path);
        session.save();
        dialogs.stop();
        QCOMPARE(finished.count(), 1);
        QVERIFY(!session.isActive());
        const QImage stitched(path);
        QVERIFY(!stitched.isNull());
        QCOMPARE(stitched.width(), initial.width());
        QVERIFY(stitched.height() > initial.height());
        QCOMPARE(stitched.height(), qRound(280 * dpr));
        // Capture begins at logical document y=20; verify all 14 unique rows, including seams.
        for (int row = 1; row <= 14; ++row) {
            const int y = qRound(((row - 1) * 20 + 10) * dpr);
            QCOMPARE(stitched.pixelColor(qRound(10 * dpr), y), CaptureFixture::rowColor(row));
        }
        QCOMPARE(QApplication::clipboard()->image().size(), stitched.size());
        qInfo() << "Real scroll PNG:" << path << stitched.size();
    }
    void gifRecordingCapturesChangingScreenFrames() {
        fixture.animationColor = Qt::red;
        fixture.raise();
        fixture.repaint();
        QTest::qWait(200);
        const QRect region(fixture.mapToGlobal(QPoint(45, 225)), QSize(100, 60));
        const qreal dpr = fixture.screen()->devicePixelRatio();
        auto& overlay = SnippingOverlay::instance();
        overlay.startSnipping();
        QTest::qWait(200);
        drag(&overlay, overlay.mapFromGlobal(region.topLeft()), overlay.mapFromGlobal(region.bottomRight()));
        overlay.findChild<FloatingToolbar*>()->actionTriggered(ToolAction::GifRecord);
        auto* active = overlay.findChild<GifRecordSession*>();
        QVERIFY(active && active->isActive());
        auto& session = *active;
        QSignalSpy finished(&session, &GifRecordSession::finished);
        session.startRecording();
        QVERIFY(session.isRecording());
        for (const QColor& color : {QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue)}) {
            fixture.animationColor = color;
            fixture.repaint();
            QTest::qWait(350);
        }
        const QString path = output + "/record-real-animation.gif";
        QFile::remove(path);
        QTimer dialogs;
        acceptExportDialogs(dialogs, path);
        session.stopAndSave();
        dialogs.stop();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), path);
        QVERIFY(!session.isRecording());
        QImageReader reader(path);
        QVERIFY(reader.supportsAnimation());
        QVERIFY(reader.imageCount() > 2);
        QList<QColor> colors;
        int count = 0;
        while (reader.canRead()) {
            const QImage frame = reader.read();
            QVERIFY2(!frame.isNull(), qPrintable(reader.errorString()));
            QCOMPARE(frame.size(), QSize(qRound(100 * dpr), qRound(60 * dpr)));
            const QColor center = frame.pixelColor(frame.width() / 2, frame.height() / 2);
            if (!colors.contains(center)) colors.append(center);
            ++count;
        }
        QCOMPARE(count, reader.imageCount());
        QVERIFY(colors.contains(QColor(Qt::red)));
        QVERIFY(colors.contains(QColor(Qt::green)));
        QVERIFY(colors.contains(QColor(Qt::blue)));
        qInfo() << "Real recording GIF:" << path << "decoded frames:" << count;
    }
    void onlineTranslationReturnsChineseFromProductionEngine() {
        struct Outcome { bool called = false; TranslationResult result; };
        const auto outcome = std::make_shared<Outcome>();
        auto* translator = new OnlineFallbackTranslator(this);
        translator->translateAsync("Hello world", "en", "zh", [outcome, translator](const TranslationResult& result) {
            outcome->called = true;
            outcome->result = result;
            translator->deleteLater();
        });
        QTRY_VERIFY_WITH_TIMEOUT(outcome->called, 45000);
        const TranslationResult& result = outcome->result;
        qInfo().noquote() << "Real online translation:" << result.engineName << result.translatedText << result.errorMessage;
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.originalText, QString("Hello world"));
        QVERIFY(result.translatedText != result.originalText);
        QVERIFY2(result.translatedText.contains(QString::fromUtf8("世界")), qPrintable(result.translatedText));
        QVERIFY(result.translatedText.contains(QString::fromUtf8("你")) || result.translatedText.contains(QString::fromUtf8("好")));
    }
    void translationFromScreenshotToolbar() {
        qInfo() << "Testing installed OCR engine:" << OcrManager::instance().activeEngineName();
        fixture.heading = "Good morning";
        fixture.raise();
        fixture.repaint();
        QTest::qWait(200);
        auto& overlay = SnippingOverlay::instance();
        overlay.startSnipping();
        QTest::qWait(200);
        const QPoint from = overlay.mapFromGlobal(fixture.mapToGlobal(QPoint(20, 25)));
        drag(&overlay, from, from + QPoint(480, 80));
        overlay.findChild<FloatingToolbar*>()->actionTriggered(ToolAction::Translate);
        QPointer<OcrResultDialog> dialog;
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* result = qobject_cast<OcrResultDialog*>(widget); result && result->isVisible()) dialog = result;
        QVERIFY2(dialog, "The result window must be visible before OCR finishes");
        QVERIFY(!overlay.isVisible());
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(50);
        const auto translated = [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* result = qobject_cast<OcrResultDialog*>(widget); result && result->isVisible()) {
                    dialog = result;
                    for (auto* edit : result->findChildren<QPlainTextEdit*>())
                        if (edit->toPlainText().contains(QStringLiteral("早上")) ||
                            edit->toPlainText().contains(QStringLiteral("早晨")) ||
                            edit->toPlainText().contains(QStringLiteral("早安"))) return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(translated(), 30000);
        QVERIFY(ticks > 0);
        QTest::qWait(1000);
        QVERIFY2(dialog && dialog->isVisible(), "Translation must not close its result window");
        QVERIFY(dialog->grab().save(output + "/translation-result.png"));
        dialog->close();
    }
    void denseRapidOcrKeepsWindowResponsive() {
        if (!OcrManager::instance().isRapidOcrReady()) QSKIP("Run from dist/Evan to use the installed RapidOCR plugin");
        fixture.denseText = true;
        fixture.resize(1180, 600);
        fixture.raise();
        fixture.repaint();
        QTest::qWait(200);
        auto& overlay = SnippingOverlay::instance();
        overlay.startSnipping();
        const QPoint start = overlay.mapFromGlobal(fixture.mapToGlobal(QPoint(20, 10)));
        drag(&overlay, start, start + QPoint(1108, 532));
        overlay.findChild<FloatingToolbar*>()->actionTriggered(ToolAction::Translate);
        QPointer<OcrResultDialog> dialog;
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* result = qobject_cast<OcrResultDialog*>(widget); result && result->isVisible()) dialog = result;
        QVERIFY(dialog);
        bool disappeared = false;
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; disappeared |= !dialog || !dialog->isVisible(); });
        heartbeat.start(50);
        const auto translationFinished = [&] {
            if (!dialog) return false;
            for (auto* edit : dialog->findChildren<QPlainTextEdit*>())
                if (edit->toPlainText().contains(QStringLiteral("早上")) ||
                    edit->toPlainText().startsWith(QStringLiteral("翻译失败:"))) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(translationFinished(), 20000);
        QTest::qWait(1000);
        QVERIFY(ticks > 2);
        QVERIFY(!disappeared);
        QVERIFY(dialog->grab().save(output + "/dense-rapid-translation.png"));
        heartbeat.stop();
        dialog->close();
    }
    void closeWhileRecognitionIsPending() {
        auto* result = new OcrResultDialog;
        result->setAttribute(Qt::WA_DeleteOnClose);
        result->setImage(fixture.grab());
        result->show();
        result->startRecognition();
        QPointer<OcrResultDialog> guarded(result);
        result->close();
        QTRY_VERIFY(guarded.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(QThreadPool::globalInstance()->activeThreadCount(), 0, 20000);
        QCoreApplication::processEvents();
        for (QWidget* widget : QApplication::topLevelWidgets())
            QVERIFY(!qobject_cast<OcrResultDialog*>(widget));
    }
    void cleanup() {
        fixture.denseText = false;
        fixture.resize(760, 400);
        fixture.heading = "Evan OCR 12345";
        fixture.scrollContent = false;
        fixture.scrollOffset = 0;
        fixture.animationColor = QColor();
        fixture.repaint();
    }
    void cleanupTestCase() {
        SnippingOverlay::instance().triggerCancelAction();
        PinManager::instance().closeAll();
        fixture.close();
    }
};

QTEST_MAIN(NativeE2e)
#include "native_e2e.moc"
