#include "pin_window.h"
#include "../../core/hotkey_config.h"
#include "../../core/theme_manager.h"
#include "../../core/ocr/ocr_manager.h"
#include "../../core/export/image_exporter.h"
#include <QDateTime>
#include "../ocr/ocr_result_dialog.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <algorithm>
#include <cmath>
#include <QPointer>
#include <QMessageBox>
#include "../toolbar/toolbar_icons.h"

PinWindow::PinWindow(const QPixmap& pixmap, const QRect& initialGeometry, QWidget* parent)
    : QWidget(parent), m_originalPixmap(pixmap)
{
    // 设置无边框、透明底色、置顶、独立工具窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setWindowTitle(QStringLiteral("AccuShot 贴图"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setCursor(Qt::SizeAllCursor);
    QFont uiFont("Segoe UI");
    uiFont.setPixelSize(13);
    setFont(uiFont);
    setToolTip("拖动移动 · 滚轮缩放 · Ctrl+滚轮调整透明度 · 右键更多操作");
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });

    // 将窗口大小向外扩展 SHADOW_MARGIN，使得内容依然精准停留在截图原位，但四周呈现立体悬浮阴影
    QRect winGeom = initialGeometry.adjusted(-SHADOW_MARGIN, -SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN);
    setGeometry(winGeom);
    m_scaleFactor = initialGeometry.width() / pixmap.deviceIndependentSize().width();

    show();
    activateWindow();
    raise();
}

QRect PinWindow::contentRect() const
{
    return rect().adjusted(SHADOW_MARGIN, SHADOW_MARGIN, -SHADOW_MARGIN, -SHADOW_MARGIN);
}

void PinWindow::paintEvent(QPaintEvent* /*event*/)
{
    if (m_originalPixmap.isNull()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QRect cRect = contentRect();
    if (cRect.isEmpty()) return;
    const auto palette = ThemeManager::instance().palette();
    const bool dark = ThemeManager::instance().isDarkMode();

    // 1. 绘制多层高柔和度立体落影 (模仿 macOS 浮动窗口与 Windows 11 Fluent 深度阴影)
    // 阴影自 16px 逐渐向外衰减，略带 y 轴下沉，即使原图与桌面背景 100% 相同也能一眼辨识贴图边界
    for (int r = SHADOW_MARGIN; r >= 1; --r) {
        QRect shadowRect = cRect.adjusted(-r, -r + 2, r, r + 2);
        double ratio = 1.0 - (static_cast<double>(r) / SHADOW_MARGIN);
        int alpha = static_cast<int>(38.0 * ratio * ratio); // 平滑非线性渐变
        if (alpha > 0) {
            painter.setPen(Qt::NoPen);
            QColor shadow = dark ? palette.sidebar : palette.text;
            shadow.setAlpha(alpha);
            painter.setBrush(shadow);
            painter.drawRoundedRect(shadowRect, 6, 6);
        }
    }

    // 2. 绘制贴图实体画面
    painter.setOpacity(m_opacity);
    painter.drawPixmap(cRect, m_originalPixmap);

    // 3. 贴图外框跟随主题；原始截图保持原色。
    painter.setOpacity(1.0);
    if (m_isHovered) {
        painter.setPen(QPen(palette.focus, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(cRect.adjusted(0, 0, -1, -1), 3, 3);
    }


}

void PinWindow::enterEvent(QEnterEvent* /*event*/)
{
    m_isHovered = true;
    update();
}

void PinWindow::leaveEvent(QEvent* /*event*/)
{
    m_isHovered = false;
    update();
}

void PinWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
    }
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    }
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) { event->ignore(); return; }
    if (event->modifiers() & Qt::ControlModifier) {
        m_opacity = std::clamp(m_opacity + steps * 0.05, 0.10, 1.0);
    } else {
        const QRect content = contentRect();
        const QPointF anchor = event->position() - content.topLeft();
        const double rx = anchor.x() / content.width();
        const double ry = anchor.y() / content.height();
        m_scaleFactor = std::clamp(m_scaleFactor * std::pow(1.1, steps), 0.20, 5.0);
        const QSize size = (m_originalPixmap.deviceIndependentSize() * m_scaleFactor).toSize().expandedTo(QSize(1, 1));
        const QPoint topLeft = event->globalPosition().toPoint() - QPoint(qRound(rx * size.width()), qRound(ry * size.height())) - QPoint(SHADOW_MARGIN, SHADOW_MARGIN);
        setGeometry(QRect(topLeft, size + QSize(2 * SHADOW_MARGIN, 2 * SHADOW_MARGIN)));
    }
    update();
    event->accept();
}

void PinWindow::triggerOcr()
{
    auto* dialog = new OcrResultDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setImage(m_originalPixmap);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    OcrManager::instance().recognizeAsync(m_originalPixmap.toImage(), m_originalPixmap.devicePixelRatio(), [dialog = QPointer<OcrResultDialog>(dialog)](const OcrResult& res, const QString& engineName) {
        if (dialog) dialog->setResult(res, engineName);
    });
}

void PinWindow::contextMenuEvent(QContextMenuEvent* event)
{
    const auto& c = HotkeyConfig::instance().data();
    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::instance().getPinMenuStyle());

    auto* ocrAct = menu.addAction(QString("识别文字\t%1").arg(c.pinOcr));
    menu.addSeparator();
    auto* copyAct = menu.addAction(QString("复制图片\t%1").arg(c.pinCopy));
    auto* saveAct = menu.addAction(QString("另存为…\t%1").arg(c.pinSave));
    menu.addSeparator();
    auto* closeAct = menu.addAction(QString("关闭贴图\t%1 / 双击").arg(c.pinClose));

    const QColor ink = ThemeManager::instance().palette().text;
    ocrAct->setIcon(toolbarIcon(ToolAction::Ocr, ink));
    copyAct->setIcon(toolbarIcon(ToolAction::Confirm, ink));
    saveAct->setIcon(toolbarIcon(ToolAction::Save, ink));
    closeAct->setIcon(toolbarIcon(ToolAction::Cancel, ink));
    QAction* selected = menu.exec(event->globalPos());
    if (selected == ocrAct) {
        triggerOcr();
    } else if (selected == copyAct) {
        QApplication::clipboard()->setPixmap(m_originalPixmap);
    } else if (selected == saveAct) {
        QString defaultName = QString("AccuShot_Pin_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString path = QFileDialog::getSaveFileName(this, "保存贴图", defaultName, ImageExporter::getSaveFileFilter());
        if (!path.isEmpty()) {
            if (!ImageExporter::saveImage(m_originalPixmap.toImage(), path))
                QMessageBox::warning(this, "保存失败", "图片未能保存，请检查路径和写入权限后重试。");
        }
    } else if (selected == closeAct) {
        close();
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        close();
    }
}

void PinWindow::keyPressEvent(QKeyEvent* event)
{
    const auto& c = HotkeyConfig::instance().data();
    if (HotkeyConfig::matches(event, c.pinClose)) {
        close();
        return;
    }
    if (HotkeyConfig::matches(event, c.pinOcr)) {
        triggerOcr();
        return;
    }
    if (HotkeyConfig::matches(event, c.pinCopy)) {
        QApplication::clipboard()->setPixmap(m_originalPixmap);
        return;
    }
    if (HotkeyConfig::matches(event, c.pinSave)) {
        QString defaultName = QString("AccuShot_Pin_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString path = QFileDialog::getSaveFileName(this, "保存贴图", defaultName, ImageExporter::getSaveFileFilter());
        if (!path.isEmpty()) {
            if (!ImageExporter::saveImage(m_originalPixmap.toImage(), path))
                QMessageBox::warning(this, "保存失败", "图片未能保存，请检查路径和写入权限后重试。");
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
