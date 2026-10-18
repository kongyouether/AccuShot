#include "../../core/theme_manager.h"
#include "scroll_capture_widget.h"
#include "../../core/export/image_exporter.h"
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDateTime>
#include <QDebug>
#include <QMessageBox>
#include <QSignalBlocker>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// -------------------------------------------------------------
// ScrollOutlineFrame: 视口边框 (设置鼠标穿透，绝对不阻挡滚轮)
// -------------------------------------------------------------
ScrollOutlineFrame::ScrollOutlineFrame(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true); // 鼠标完全穿透至底层真实窗口
    setAttribute(Qt::WA_DeleteOnClose, false);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void ScrollOutlineFrame::setOutlineRect(const QRect& rect)
{
    setGeometry(rect.adjusted(-2, -2, 2, 2));
    update();
}

void ScrollOutlineFrame::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto colors = ThemeManager::instance().palette();
    QColor outline = colors.action;
    outline.setAlpha(230);
    painter.setPen(QPen(outline, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -1, -1));

    QColor innerOutline = colors.raised;
    innerOutline.setAlpha(140);
    painter.setPen(QPen(innerOutline, 1.0));
    painter.drawRect(rect().adjusted(2, 2, -2, -2));
}

// -------------------------------------------------------------
// ScrollCaptureSession: 辅助滚动长截图控制器 (极简整齐界面，仅保存/取消)
// -------------------------------------------------------------
ScrollCaptureSession::ScrollCaptureSession(QObject* parent)
    : QObject(parent)
{
    m_outlineFrame = new ScrollOutlineFrame(nullptr);

    // 独立的悬浮控制胶囊挂件 (极简整洁，无图标累赘)
    m_pillWidget = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    m_pillWidget->setObjectName("pillRoot");
    m_pillWidget->setWindowTitle(QStringLiteral("AccuShot 长截图"));
    m_pillWidget->setAttribute(Qt::WA_StyledBackground, true);
#ifdef Q_OS_WIN
    // 捕获窗口之外的像素；即使大选区令控制面板无法完全移出，也不把面板/边框拼入长图。
    constexpr DWORD excludeFromCapture = 0x00000011; // WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+)
    const bool outlineExcluded = SetWindowDisplayAffinity(reinterpret_cast<HWND>(m_outlineFrame->winId()), excludeFromCapture);
    const bool controlsExcluded = SetWindowDisplayAffinity(reinterpret_cast<HWND>(m_pillWidget->winId()), excludeFromCapture);
    if (!outlineExcluded || !controlsExcluded) {
        qWarning() << "Scroll capture could not exclude its controls from screen capture:" << GetLastError();
    }
#endif
    m_pillWidget->setStyleSheet(ThemeManager::instance().getResultDialogStyle());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, m_pillWidget, [panel = m_pillWidget] {
        panel->setStyleSheet(ThemeManager::instance().getResultDialogStyle());
    });

    auto* pillContainer = new QWidget(m_pillWidget);

    auto* layout = new QHBoxLayout(pillContainer);
    layout->setContentsMargins(16, 6, 16, 6);
    layout->setSpacing(12);

    m_directionCombo = new QComboBox(pillContainer);
    m_directionCombo->setObjectName("scrollDirection");
    m_directionCombo->addItem("竖向滚动", static_cast<int>(ScrollStitcher::Direction::Vertical));
    m_directionCombo->addItem("横向滚动", static_cast<int>(ScrollStitcher::Direction::Horizontal));
    m_directionCombo->setToolTip("切换方向会清空已拼接内容，以当前画面重新开始");
    layout->addWidget(m_directionCombo);
    connect(m_directionCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_isActive) return;
        m_stitcher.reset(m_latestFrame, static_cast<ScrollStitcher::Direction>(m_directionCombo->currentData().toInt()));
        updatePreview();
    });

    m_heightLabel = new QLabel("高度: 0px", pillContainer);
    m_heightLabel->setObjectName("secondaryText");
    layout->addWidget(m_heightLabel);

    // 仅保留保存按钮 (无图标，纯文本极简)
    m_saveBtn = new QPushButton("保存", pillContainer);
    m_saveBtn->setObjectName("saveBtn");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &ScrollCaptureSession::save);
    layout->addWidget(m_saveBtn);

    // 仅保留取消按钮 (无图标，纯文本极简)
    m_cancelBtn = new QPushButton("取消", pillContainer);
    m_cancelBtn->setObjectName("cancelBtn");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ScrollCaptureSession::cancel);
    layout->addWidget(m_cancelBtn);

    auto* outerLayout = new QVBoxLayout(m_pillWidget);
    outerLayout->setContentsMargins(0, 0, 0, 8);
    outerLayout->addWidget(pillContainer);
    m_tipLabel = new QLabel(m_pillWidget);
    m_tipLabel->setAlignment(Qt::AlignCenter);
    outerLayout->addWidget(m_tipLabel);
    m_previewLabel = new QLabel(m_pillWidget);
    m_previewLabel->setObjectName("scrollPreview");
    m_previewLabel->setFixedHeight(80);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    outerLayout->addWidget(m_previewLabel);

    // 20fps 高速采样定时器 (每 50ms 采样一次屏幕视口画面)
    m_sampleTimer = new QTimer(this);
    m_sampleTimer->setInterval(50);
    connect(m_sampleTimer, &QTimer::timeout, this, &ScrollCaptureSession::onSampleTick);
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    if (m_outlineFrame) {
        m_outlineFrame->close();
        delete m_outlineFrame;
    }
    if (m_pillWidget) {
        m_pillWidget->close();
        delete m_pillWidget;
    }
}

void ScrollCaptureSession::start(const QRect& captureRect, const QImage& initialFrame,
                                 ScrollStitcher::Direction direction)
{
    m_captureRect = captureRect.normalized();
    m_latestFrame = initialFrame;
    m_stitcher.reset(initialFrame, direction);
    {
        const QSignalBlocker blocker(m_directionCombo);
        m_directionCombo->setCurrentIndex(direction == ScrollStitcher::Direction::Horizontal ? 1 : 0);
    }
    m_isActive = true;

    // 显示穿透边框，指示捕获视口
    m_outlineFrame->setOutlineRect(m_captureRect);
    m_outlineFrame->show();
    m_outlineFrame->raise();

    // 更新悬浮控制胶囊文字与位置
    updatePreview();
    positionControls();
    m_pillWidget->show();
    m_pillWidget->raise();

    // 启动实时抓帧采样
    m_sampleTimer->start();
}

void ScrollCaptureSession::updatePreview()
{
    const QImage image = m_stitcher.currentStitchedImage();
    m_heightLabel->setText(QString("%1 × %2 px（%3帧）")
        .arg(image.width()).arg(image.height()).arg(m_stitcher.frameCount()));
    m_tipLabel->setText(m_stitcher.direction() == ScrollStitcher::Direction::Horizontal
        ? "左右滚动页面（可用 Shift + 滚轮）；切换方向会重新开始"
        : "上下滚动页面；切换方向会重新开始");
    m_previewLabel->setPixmap(QPixmap::fromImage(image.scaled(360, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void ScrollCaptureSession::positionControls()
{
    m_pillWidget->adjustSize();

    int pillX = m_captureRect.center().x() - m_pillWidget->width() / 2;
    int pillY = m_captureRect.bottom() + 12;

    QScreen* screen = QGuiApplication::screenAt(m_captureRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect screenGeom = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    if (pillY + m_pillWidget->height() > screenGeom.bottom() - 10) {
        pillY = m_captureRect.top() - m_pillWidget->height() - 12;
    }
    pillX = qBound(screenGeom.left() + 10, pillX, qMax(screenGeom.left() + 10, screenGeom.right() - m_pillWidget->width() - 10));
    pillY = qBound(screenGeom.top() + 10, pillY, qMax(screenGeom.top() + 10, screenGeom.bottom() - m_pillWidget->height() - 10));

    m_pillWidget->move(pillX, pillY);
}

void ScrollCaptureSession::onSampleTick()
{
    if (!m_isActive || m_captureRect.isEmpty()) return;

    QScreen* screen = QGuiApplication::screenAt(m_captureRect.center());
    if (!screen) return;

    // 先抓指定屏幕，再按屏幕内逻辑坐标乘 DPR 裁切，避免副屏原点及混合 DPI 约定歧义。
    const QPixmap screenPixmap = screen->grabWindow(0);
    if (screenPixmap.isNull()) return;
    const QPoint local = m_captureRect.topLeft() - screen->geometry().topLeft();
    const qreal dpr = screenPixmap.devicePixelRatio();
    const QRect pixels(qRound(local.x() * dpr), qRound(local.y() * dpr),
                       qRound(m_captureRect.width() * dpr), qRound(m_captureRect.height() * dpr));
    m_latestFrame = screenPixmap.toImage().copy(pixels);
    int delta = 0;
    if (m_stitcher.appendFrame(m_latestFrame, delta)) updatePreview();
}

void ScrollCaptureSession::save()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_sampleTimer->stop();

    if (m_outlineFrame) m_outlineFrame->hide();
    if (m_pillWidget) m_pillWidget->hide();

    QImage stitched = m_stitcher.currentStitchedImage();
    if (!stitched.isNull()) {
        QPixmap res = QPixmap::fromImage(stitched);
        QString defaultName = QString("AccuShot_Long_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString path = QFileDialog::getSaveFileName(nullptr, "保存长截图", defaultName, ImageExporter::getSaveFileFilter());
        if (path.isEmpty() || !ImageExporter::saveImage(stitched, path)) {
            if (!path.isEmpty()) QMessageBox::warning(nullptr, "保存失败", "图片仍保留，请选择其他位置重试。");
            m_isActive = true;
            m_pillWidget->show();
            m_heightLabel->setText("截图已保留，可重新保存");
            return;
        }
        // 同步存入系统剪贴板备用
        QApplication::clipboard()->setPixmap(res);
        emit finished(res);
    } else {
        emit cancelled();
    }
}

void ScrollCaptureSession::cancel()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_sampleTimer->stop();

    if (m_outlineFrame) m_outlineFrame->hide();
    if (m_pillWidget) m_pillWidget->hide();

    emit cancelled();
}
