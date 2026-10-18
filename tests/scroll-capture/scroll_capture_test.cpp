#include <QtTest>
#include <QPainter>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QDir>
#include <QSettings>
#include "core/capture/scroll_stitcher.h"
#include "ui/capture/scroll_capture_widget.h"

// 裁切同一张带文字、色块和细纹理的文档画面，最终逐像素比较完整拼图。
static QImage documentTexture(int width, int height, quint32 seed = 42)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator random(seed);
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            row[x] = qRgb(random.bounded(256), random.bounded(256), random.bounded(256));
        }
    }
    QPainter painter(&image);
    for (int y = 15; y < height; y += 45) {
        painter.setPen(QColor(40, 30, 80));
        painter.drawText(12, y, QString("Document row %1 - capture texture").arg(y));
        painter.setPen(QColor(200, 220, 240));
        painter.drawLine(0, y + 4, width - 1, y + 4);
    }
    return image;
}

class ScrollCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void horizontalPixels();
    void horizontalRollbackAndPrepend();
    void repeatedFrames();
    void unmatchedFrameDoesNotPoisonReference();
    void verticalRegression();
    void physicalPixelsAtHighDpi();
    void directionResetAndInvalidSize();
    void offsetPerformance();
    void nativeSessionSampling();
};

void ScrollCaptureTest::horizontalPixels()
{
    const QImage source = documentTexture(700, 160);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(110, 0, 220, 160), ScrollStitcher::Direction::Horizontal);
    int delta = 0;
    QVERIFY(stitcher.appendFrame(source.copy(180, 0, 220, 160), delta));
    QCOMPARE(delta, 70);
    QVERIFY(stitcher.appendFrame(source.copy(245, 0, 220, 160), delta));
    QCOMPARE(delta, 65);
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(110, 0, 355, 160));
    QCOMPARE(stitcher.frameCount(), 3);
}

void ScrollCaptureTest::horizontalRollbackAndPrepend()
{
    const QImage source = documentTexture(700, 140);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(200, 0, 220, 140), ScrollStitcher::Direction::Horizontal);
    int delta = 0;
    QVERIFY(stitcher.appendFrame(source.copy(270, 0, 220, 140), delta));
    QVERIFY(!stitcher.appendFrame(source.copy(230, 0, 220, 140), delta));
    QCOMPARE(delta, -40);
    QCOMPARE(stitcher.frameCount(), 2);
    QVERIFY(!stitcher.appendFrame(source.copy(260, 0, 220, 140), delta));
    QVERIFY(stitcher.appendFrame(source.copy(150, 0, 220, 140), delta));
    QCOMPARE(delta, -110);
    QVERIFY(stitcher.appendFrame(source.copy(65, 0, 220, 140), delta));
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(65, 0, 425, 140));
    // 已有画布内回到右端，再超过右端才追加。
    QVERIFY(!stitcher.appendFrame(source.copy(180, 0, 220, 140), delta));
    QVERIFY(!stitcher.appendFrame(source.copy(260, 0, 220, 140), delta));
    QVERIFY(stitcher.appendFrame(source.copy(320, 0, 220, 140), delta));
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(65, 0, 475, 140));
}

void ScrollCaptureTest::repeatedFrames()
{
    const QImage source = documentTexture(400, 120);
    ScrollStitcher stitcher;
    const QImage frame = source.copy(0, 0, 200, 120);
    stitcher.reset(frame, ScrollStitcher::Direction::Horizontal);
    int delta = 9;
    for (int i = 0; i < 4; ++i) {
        QVERIFY(!stitcher.appendFrame(frame, delta));
        QCOMPARE(delta, 0);
    }
    QCOMPARE(stitcher.frameCount(), 1);
    QCOMPARE(stitcher.currentStitchedImage(), frame);
}

void ScrollCaptureTest::unmatchedFrameDoesNotPoisonReference()
{
    const QImage source = documentTexture(450, 120);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(0, 0, 200, 120), ScrollStitcher::Direction::Horizontal);
    int delta = 0;
    QVERIFY(!stitcher.appendFrame(documentTexture(200, 120, 314), delta));
    QCOMPARE(delta, 0);
    QCOMPARE(stitcher.currentTotalWidth(), 200);
    QVERIFY(stitcher.appendFrame(source.copy(75, 0, 200, 120), delta));
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(0, 0, 275, 120));
}

void ScrollCaptureTest::verticalRegression()
{
    const QImage source = documentTexture(160, 600);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(0, 25, 160, 180));
    QCOMPARE(stitcher.direction(), ScrollStitcher::Direction::Vertical);
    int delta = 0;
    QVERIFY(stitcher.appendFrame(source.copy(0, 90, 160, 180), delta));
    QCOMPARE(delta, 65);
    QVERIFY(!stitcher.appendFrame(source.copy(0, 50, 160, 180), delta));
    QVERIFY(stitcher.appendFrame(source.copy(0, 160, 160, 180), delta));
    QCOMPARE(stitcher.currentTotalHeight(), 315);
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(0, 25, 160, 315));
}

void ScrollCaptureTest::physicalPixelsAtHighDpi()
{
    const QImage source = documentTexture(550, 160);
    for (qreal dpr : {1.25, 1.5, 2.0}) {
        ScrollStitcher stitcher;
        QImage first = source.copy(0, 0, 240, 160);
        QImage second = source.copy(83, 0, 240, 160);
        first.setDevicePixelRatio(dpr);
        second.setDevicePixelRatio(dpr);
        stitcher.reset(first, ScrollStitcher::Direction::Horizontal);
        int delta = 0;
        QVERIFY(stitcher.appendFrame(second, delta));
        QCOMPARE(delta, 83);
        QCOMPARE(stitcher.currentStitchedImage().devicePixelRatio(), 1.0);
        QCOMPARE(stitcher.currentStitchedImage(), source.copy(0, 0, 323, 160));
    }
}

void ScrollCaptureTest::directionResetAndInvalidSize()
{
    const QImage source = documentTexture(400, 400);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(0, 0, 180, 160), ScrollStitcher::Direction::Horizontal);
    int delta = 0;
    QVERIFY(!stitcher.appendFrame(source.copy(0, 0, 181, 160), delta));
    QVERIFY(!stitcher.appendFrame(QImage(), delta));
    QVERIFY(stitcher.appendFrame(source.copy(60, 0, 180, 160), delta));
    stitcher.reset(source.copy(0, 0, 180, 160));
    QCOMPARE(stitcher.frameCount(), 1);
    QCOMPARE(stitcher.currentTotalWidth(), 180);
    QVERIFY(stitcher.appendFrame(source.copy(0, 55, 180, 160), delta));
    QCOMPARE(stitcher.currentStitchedImage(), source.copy(0, 0, 180, 215));
}

void ScrollCaptureTest::offsetPerformance()
{
    const QImage source = documentTexture(2400, 1000);
    ScrollStitcher stitcher;
    stitcher.reset(source.copy(0, 0, 1600, 1000), ScrollStitcher::Direction::Horizontal);
    int delta = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(stitcher.appendFrame(source.copy(375, 0, 1600, 1000), delta));
    qInfo() << "1600x1000 horizontal match + canvas expansion:" << elapsed.elapsed() << "ms";
    QCOMPARE(delta, 375);
}

class NativeTextureWindow : public QWidget
{
public:
    QImage texture;
    int physicalOffset = 0;
    NativeTextureWindow() : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {}
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawImage(QPointF(-physicalOffset / texture.devicePixelRatio(), 0), texture);
    }
};

void ScrollCaptureTest::nativeSessionSampling()
{
    if (!qEnvironmentVariableIsSet("EVAN_NATIVE_SCROLL")) QSKIP("Set EVAN_NATIVE_SCROLL=1 with QT_QPA_PLATFORM=windows for screen sampling");
    QVERIFY(QGuiApplication::platformName() == "windows");
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const qreal dpr = screen->devicePixelRatio();
    NativeTextureWindow fixture;
    fixture.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(80, 80), QSize(480, 240)));
    fixture.texture = documentTexture(qRound(1200 * dpr), qRound(240 * dpr));
    fixture.texture.setDevicePixelRatio(dpr);
    fixture.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture));
    QTest::qWait(250);
    const QRect capture = fixture.geometry();
    const QPoint local = capture.topLeft() - screen->geometry().topLeft();
    const QRect pixels(qRound(local.x() * dpr), qRound(local.y() * dpr),
                       qRound(capture.width() * dpr), qRound(capture.height() * dpr));
    const QImage first = screen->grabWindow(0).toImage().copy(pixels);
    ScrollCaptureSession session;
    session.start(capture, first, ScrollStitcher::Direction::Horizontal);
    QWidget* controls = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->windowTitle() == "Evan 长截图") controls = widget;
    }
    QVERIFY(controls);
    auto* sizeLabel = controls->findChild<QLabel*>("secondaryText");
    auto* direction = controls->findChild<QComboBox*>("scrollDirection");
    auto* preview = controls->findChild<QLabel*>("scrollPreview");
    QVERIFY(sizeLabel && direction && preview);
    const auto expected = [&](int extra, int frames) {
        return QString("%1 × %2 px（%3帧）").arg(first.width() + extra).arg(first.height()).arg(frames);
    };
    QCOMPARE(sizeLabel->text(), expected(0, 1));
    fixture.physicalOffset = 80;
    fixture.repaint();
    QTRY_COMPARE_WITH_TIMEOUT(sizeLabel->text(), expected(80, 2), 3000);
    fixture.physicalOffset = 35;
    fixture.repaint();
    QTest::qWait(250);
    QCOMPARE(sizeLabel->text(), expected(80, 2));
    fixture.physicalOffset = 140;
    fixture.repaint();
    QTRY_COMPARE_WITH_TIMEOUT(sizeLabel->text(), expected(140, 3), 3000);
    QVERIFY(!preview->pixmap().isNull());
    const QString artifacts = qEnvironmentVariable("EVAN_SCROLL_ARTIFACT_DIR", QDir::currentPath() + "/screenshots");
    QVERIFY(QDir().mkpath(artifacts));
    QVERIFY(controls->grab().save(artifacts + "/horizontal-scroll-native.png"));
    direction->setCurrentIndex(0);
    QCOMPARE(sizeLabel->text(), expected(0, 1));
    session.cancel();
}

QTEST_MAIN(ScrollCaptureTest)
#include "scroll_capture_test.moc"
