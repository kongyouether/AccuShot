#include "snipping_overlay.h"
#include "../markdown/markdown_result_dialog.h"
#include "../../core/hotkey_config.h"
#include "../../core/theme_manager.h"
#include "../../core/ocr/ocr_manager.h"
#include "../../core/char_edit/char_segmentation.h"
#include "../../core/char_edit/char_inpainter.h"
#include "../../core/char_edit/char_font_analyzer.h"
#include "../char_edit/single_char_editor.h"
#include "../magnifier/magnifier_widget.h"
#include "../annotation/inplace_text_editor.h"
#include "../ocr/ocr_result_dialog.h"
#include "../translation/screen_translation_window.h"
#include "../formula/formula_result_dialog.h"
#include "../plugin/plugin_market_dialog.h"
#include "../capture/scroll_capture_widget.h"
#include "../capture/gif_recorder_widget.h"
#include "../../core/export/image_exporter.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QFileDialog>
#include <QDateTime>
#include <QTimer>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QProcess>
#include <QScreen>
#include <QGuiApplication>
#include <QMessageBox>
#include <QDebug>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <cmath>
#include <algorithm>
#include <numeric>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

SnippingOverlay::SnippingOverlay(QWidget* parent)
    : QWidget(parent)
{
    // 设置无边框、置顶、工具悬浮窗属性，不产生任务栏孤立图标
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setWindowTitle(QStringLiteral("AccuShot 截图"));
    // Global capture can start while Settings::exec() is active. Without its own
    // modality Windows disables this topmost window and rejects mouse clicks.
    // Hiding the overlay restores the previously active modal dialog.
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    m_candidateAnimation.setDuration(90);
    m_candidateAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_candidateAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_candidateDisplayRect = value.toRectF();
        update();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });

    // 挂载悬浮放大镜挂件
    m_magnifier = new MagnifierWidget(this);
    m_magnifier->hide();

    // 挂载原位所见即所得文字输入框 (参考 Snipaste / PixPin)
    m_textEditor = new InPlaceTextEditor(this);
    m_textEditor->hide();
    m_textEditor->installEventFilter(this);
    m_textEditor->viewport()->installEventFilter(this);
    connect(m_textEditor, &InPlaceTextEditor::editingCommitted, this, [this](const QPoint& pos, const QString& text) {
        m_annotationMgr.addItem(std::make_shared<TextAnnotation>(pos, text, m_textEditor->textColor(), m_textEditor->font()));
        update();
    });

    // 挂载原位单字符就地修改气泡编辑器
    m_singleCharEditor = new SingleCharEditor(this);
    m_singleCharEditor->hide();
    connect(m_singleCharEditor, &SingleCharEditor::editingCommitted, this, [this](const SingleCharUnit& unit, const QString& newText, const QColor& color, const QFont& font) {
        qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
        if (dpr <= 0.0) dpr = 1.0;
        CharInpaintResult inpaintRes = CharInpainter::inpaintChar(m_snapshot.fullSnapshot.toImage(), unit.physicalBox, 2);
        QPixmap patchPix;
        if (inpaintRes.success && !inpaintRes.patchImage.isNull()) {
            patchPix = QPixmap::fromImage(inpaintRes.patchImage);
            patchPix.setDevicePixelRatio(dpr);
        }
        auto annot = std::make_shared<SingleCharEditAnnotation>(
            unit.logicalBox,
            patchPix,
            newText,
            color,
            font
        );
        m_annotationMgr.addItem(annot);
        update();
    });

    connect(m_singleCharEditor, &SingleCharEditor::requestNavigateNext, this, [this](int currentIndex) {
        int nextIdx = currentIndex + 1;
        if (nextIdx < m_charUnits.size()) {
            qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
            auto attrs = CharFontAnalyzer::analyze(m_snapshot.fullSnapshot.toImage(), m_charUnits[nextIdx].physicalBox, m_charUnits[nextIdx].character, dpr);
            m_charUnits[nextIdx].estimatedFgColor = attrs.fgColor;
            m_charUnits[nextIdx].estimatedBgColor = attrs.bgColor;
            m_charUnits[nextIdx].estimatedFontSize = attrs.fontSizePt;
            m_charUnits[nextIdx].estimatedFontFamily = attrs.fontFamily;
            m_charUnits[nextIdx].isBold = attrs.isBold;
            m_hoveredCharIndex = nextIdx;
            m_singleCharEditor->startEdit(m_charUnits[nextIdx], rect());
            update();
        }
    });

    connect(m_singleCharEditor, &SingleCharEditor::requestNavigatePrev, this, [this](int currentIndex) {
        int prevIdx = currentIndex - 1;
        if (prevIdx >= 0) {
            qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
            auto attrs = CharFontAnalyzer::analyze(m_snapshot.fullSnapshot.toImage(), m_charUnits[prevIdx].physicalBox, m_charUnits[prevIdx].character, dpr);
            m_charUnits[prevIdx].estimatedFgColor = attrs.fgColor;
            m_charUnits[prevIdx].estimatedBgColor = attrs.bgColor;
            m_charUnits[prevIdx].estimatedFontSize = attrs.fontSizePt;
            m_charUnits[prevIdx].estimatedFontFamily = attrs.fontFamily;
            m_charUnits[prevIdx].isBold = attrs.isBold;
            m_hoveredCharIndex = prevIdx;
            m_singleCharEditor->startEdit(m_charUnits[prevIdx], rect());
            update();
        }
    });

    // 挂载自适应吸附工具栏
    m_toolbar = new FloatingToolbar(this);
    m_toolbar->hide();
    connect(m_annotationMgr.stack(), &QUndoStack::canUndoChanged, m_toolbar, &FloatingToolbar::setUndoAvailable);

    // 颜色选择信号 → 同步标注颜色 + 文字编辑器颜色
    connect(m_toolbar, &FloatingToolbar::colorSelected, this, [this](const QColor& color) {
        m_annotColor = color;
        if (m_textEditor) m_textEditor->setTextColor(color);
    });

    // 粗细/字号选择信号 → 同步标注线宽 + 文字编辑器字号
    connect(m_toolbar, &FloatingToolbar::thicknessSelected, this, [this](int strokeWidth, int fontSize) {
        m_annotStrokeWidth = strokeWidth;
        m_annotFont.setPointSize(fontSize);
        if (m_textEditor) m_textEditor->setFontSize(fontSize);
    });

    connect(m_toolbar, &FloatingToolbar::fontSelected, this, [this](const QFont& font) {
        m_annotFont = font;
        m_textEditor->setTextFont(font);
    });
    connect(m_toolbar, &FloatingToolbar::mosaicSizeSelected, this, [this](int size) {
        m_mosaicBlockSize = size;
    });

    connect(m_toolbar, &FloatingToolbar::actionTriggered, this, [this](ToolAction action) {
        if (action == ToolAction::Confirm) {
            triggerConfirmAction();
        } else if (action == ToolAction::Cancel) {
            triggerCancelAction();
        } else if (action == ToolAction::Pin) {
            triggerPinAction();
        } else if (action == ToolAction::Save) {
            triggerSaveAction();
        } else if (action == ToolAction::Undo) {
            triggerUndoAction();
        } else if (action == ToolAction::Ocr) {
            triggerOcrAction();
        } else if (action == ToolAction::Translate) {
            triggerTranslateAction();
        } else if (action == ToolAction::ScreenTranslate) {
            triggerScreenTranslateAction();
        } else if (action == ToolAction::CopyText) {
            triggerCopyTextAction();
        } else if (action == ToolAction::FormulaOcr) {
            triggerFormulaOcrAction();
        } else if (action == ToolAction::Markdown) {
            triggerMarkdownAction();
        } else if (action == ToolAction::ScrollCapture) {
            startScrollCapture();
        } else if (action == ToolAction::GifRecord) {
            triggerGifRecord();
        } else {
            selectTool(action);
        }
    });
}

SnippingOverlay& SnippingOverlay::instance()
{
    static SnippingOverlay overlay;
    return overlay;
}

QPixmap SnippingOverlay::renderSelectedArea()
{
    if (m_selectionRect.isNull() || !m_selectionRect.isValid()) {
        return QPixmap();
    }

    QRect norm = m_selectionRect.normalized();
    qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
    if (dpr <= 0.0) dpr = 1.0;

    // 将逻辑选区换算为物理像素坐标切图，保证 1:1 绝对清晰且无放大失真
    QRect physicalNorm(
        static_cast<int>(std::round(norm.x() * dpr)),
        static_cast<int>(std::round(norm.y() * dpr)),
        static_cast<int>(std::round(norm.width() * dpr)),
        static_cast<int>(std::round(norm.height() * dpr))
    );

    QPixmap cropped = m_snapshot.fullSnapshot.copy(physicalNorm);
    cropped.setDevicePixelRatio(dpr);
    QImage base = cropped.toImage();
    base.setDevicePixelRatio(dpr);

    // 渲染标注 (马赛克与矢量图元统一绘制)
    QPainter painter(&base);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(-norm.topLeft());
    m_annotationMgr.renderAnnotations(painter);
    painter.end();

    QPixmap result = QPixmap::fromImage(base);
    result.setDevicePixelRatio(dpr);
    return result;
}

void SnippingOverlay::startSnipping()
{
    // 防止重复唤醒叠加截图导致遮罩越来越黑
    if (isVisible()) {
        return;
    }
    setCursor(Qt::CrossCursor);
    resetCandidatePreview();
#ifdef Q_OS_WIN
    BOOL animationsEnabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0);
    m_candidateAnimationsEnabled = animationsEnabled;
#endif

    qInfo() << "[CaptureInput] start previousModal="
            << (QApplication::activeModalWidget() ? QApplication::activeModalWidget()->metaObject()->className() : "none");
    // 1. 获取全屏快照
    m_snapshot = ScreenCapturer::captureVirtualScreen();
    if (!m_snapshot.isValid) {
        qWarning() << "[SnippingOverlay] 快照截取失败，无法唤醒遮罩";
        return;
    }

    m_state = SnippingState::Idle;
    ++m_captureGeneration;
    m_textOcrRunning = false;
    m_toolbar->resetSession();
    m_annotColor = m_toolbar->currentColor();
    m_annotStrokeWidth = m_toolbar->currentStrokeWidth();
    m_annotFont = m_toolbar->currentTextFont();
    m_textEditor->setTextColor(m_annotColor);
    m_textEditor->setTextFont(m_annotFont);
    m_selectionRect = QRect();
    m_smartCandidates.clear();
    m_candidateIndex = 0;
    m_candidateWheelDelta = 0;
    resetCandidateAdjustment();
    m_candidatesManuallyChosen = false;
    m_candidateContext = {};
    m_currentTool = ToolAction::Cancel;
    m_annotationMgr.clear();
    m_isDrawingAnnotation = false;
    m_currentPencil.reset();
    if (m_textEditor) m_textEditor->hide();
    if (m_singleCharEditor) m_singleCharEditor->hide();
    m_charUnits.clear();
    m_hoveredCharIndex = -1;
    m_ocrRunningForSelection = false;
    updateOcrBusyState();

    // 从配置读取智能吸附开关
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    m_enableSmartSnapping = settings.value("Snapping/EnableSmartSnapping", true).toBool();

    // 2. 覆盖整个虚拟屏幕几何区域
    setGeometry(m_snapshot.virtualGeometry);
    show();
    activateWindow();
    raise();
    setFocus(Qt::ActiveWindowFocusReason);

    // 3. Analyse the immutable image off the UI thread. Native snapping stays query-driven.
    qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
    const auto snapMode=settings.value("Snapping/Mode","Hybrid").toString();
    m_snapper.configure(snapMode,m_snapshot.virtualGeometry,quintptr(winId()));
    m_snapper.clear();
    if(m_enableSmartSnapping && (snapMode=="Contour" || snapMode=="Hybrid")) {
        auto* watcher=new QFutureWatcher<SmartSnapper>(this);
        watcher->setObjectName("contourAnalysis");
        const auto generation=m_captureGeneration;
        connect(watcher,&QFutureWatcher<SmartSnapper>::finished,this,[this,watcher,generation] {
            watcher->deleteLater();
            acceptContourAnalysis(watcher->result(), generation);
        });
        QPromise<SmartSnapper> promise;
        watcher->setFuture(promise.future());
        QThreadPool::globalInstance()->start([promise=std::move(promise),snapper=m_snapper,
                                            image=m_snapshot.fullSnapshot.toImage(),dpr]() mutable {
            promise.start();
            snapper.processSnapshot(image,dpr);
            promise.addResult(std::move(snapper));
            promise.finish();
        });
    }
    if(!findChild<QTimer*>("controlSnapPoll")) {
        auto* poll=new QTimer(this);poll->setObjectName("controlSnapPoll");poll->setInterval(100);
        connect(poll,&QTimer::timeout,this,[this]{if(isVisible()&&m_state==SnippingState::Idle&&m_enableSmartSnapping&&updateSmartCandidates(mapFromGlobal(QCursor::pos())))update();});poll->start();
    }

    // 4. 唤醒放大镜挂件
    QPoint initialPos = mapFromGlobal(QCursor::pos());
    if (m_enableSmartSnapping) {
        updateSmartCandidates(initialPos);
    } else {
        m_smartCandidates.clear();
    }
    if (m_magnifier) {
        m_magnifier->updatePosition(initialPos, m_snapshot.fullSnapshot, rect());
        m_magnifier->show();
    }

    update();
}

QRect SnippingOverlay::selectionScreenBoundary() const
{
    QScreen* screen = QGuiApplication::screenAt(mapToGlobal(m_selectionRect.center()));
    if (!screen) return rect();
    const QRect available = screen->availableGeometry();
    return QRect(mapFromGlobal(available.topLeft()), available.size()).intersected(rect());
}

void SnippingOverlay::updateToolbarPosition()
{
    m_toolbar->updatePosition(m_selectionRect, selectionScreenBoundary());
}

void SnippingOverlay::updateOcrBusyState()
{
    m_toolbar->setOcrBusy(m_textOcrRunning || m_ocrRunningForSelection);
}

HandleType SnippingOverlay::getHandleAt(const QPoint& pt) const
{
    if (m_selectionRect.isNull() || !m_selectionRect.isValid()) {
        return HandleType::None;
    }

    QRect norm = m_selectionRect.normalized();
    const int half = HANDLE_SIZE / 2;

    auto makeHandleRect = [half](int x, int y) {
        return QRect(x - half, y - half, HANDLE_SIZE, HANDLE_SIZE);
    };

    if (makeHandleRect(norm.left(), norm.top()).contains(pt)) return HandleType::TopLeft;
    if (makeHandleRect(norm.center().x(), norm.top()).contains(pt)) return HandleType::Top;
    if (makeHandleRect(norm.right(), norm.top()).contains(pt)) return HandleType::TopRight;
    if (makeHandleRect(norm.right(), norm.center().y()).contains(pt)) return HandleType::Right;
    if (makeHandleRect(norm.right(), norm.bottom()).contains(pt)) return HandleType::BottomRight;
    if (makeHandleRect(norm.center().x(), norm.bottom()).contains(pt)) return HandleType::Bottom;
    if (makeHandleRect(norm.left(), norm.bottom()).contains(pt)) return HandleType::BottomLeft;
    if (makeHandleRect(norm.left(), norm.center().y()).contains(pt)) return HandleType::Left;

    if (norm.contains(pt)) return HandleType::Inside;

    return HandleType::None;
}

void SnippingOverlay::updateCursorForHandle(HandleType handle)
{
    switch (handle) {
    case HandleType::TopLeft:
    case HandleType::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case HandleType::TopRight:
    case HandleType::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case HandleType::Top:
    case HandleType::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case HandleType::Left:
    case HandleType::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case HandleType::Inside:
        setCursor(Qt::SizeAllCursor);
        break;
    default:
        setCursor(m_state == SnippingState::Selected ? Qt::ArrowCursor : Qt::CrossCursor);
        break;
    }
}

void SnippingOverlay::updateCursorAt(const QPoint& position)
{
    if (m_state != SnippingState::Selected) {
        setCursor(Qt::CrossCursor);
    } else if (m_currentTool == ToolAction::Cancel) {
        updateCursorForHandle(getHandleAt(position));
    } else if (!m_selectionRect.normalized().contains(position)) {
        setCursor(Qt::ArrowCursor);
    } else if (m_currentTool == ToolAction::CharEdit) {
        setCursor(m_ocrRunningForSelection ? Qt::WaitCursor : Qt::IBeamCursor);
    } else {
        setCursor(m_currentTool == ToolAction::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    }
}

void SnippingOverlay::resizeSelection(const QPoint& pt)
{
    QRect norm = m_selectionRect.normalized();

    switch (m_activeHandle) {
    case HandleType::TopLeft:
        norm.setTopLeft(pt);
        break;
    case HandleType::Top:
        norm.setTop(pt.y());
        break;
    case HandleType::TopRight:
        norm.setTopRight(pt);
        break;
    case HandleType::Right:
        norm.setRight(pt.x());
        break;
    case HandleType::BottomRight:
        norm.setBottomRight(pt);
        break;
    case HandleType::Bottom:
        norm.setBottom(pt.y());
        break;
    case HandleType::BottomLeft:
        norm.setBottomLeft(pt);
        break;
    case HandleType::Left:
        norm.setLeft(pt.x());
        break;
    default:
        break;
    }

    m_selectionRect = norm.normalized();
}

void SnippingOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (!m_snapshot.isValid) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1. 绘制底图快照
    painter.drawPixmap(0, 0, m_snapshot.fullSnapshot);

    // 2. 绘制半透明遮罩
    QColor maskColor("#10251F"); maskColor.setAlpha(72);
    if (m_selectionRect.isNull() || !m_selectionRect.isValid()) {
        // 空闲状态且有智能候选框时：使用 QPainterPath 单次镂空候选框，严禁二次叠加 drawPixmap 造成画面错位和放大
        if (m_state == SnippingState::Idle && !m_smartCandidates.empty() && m_candidateIndex < m_smartCandidates.size()) {
            const QRectF candidate = m_candidateDisplayRect.isValid()
                ? m_candidateDisplayRect : QRectF(currentCandidateRect());

            QPainterPath maskPath;
            maskPath.setFillRule(Qt::OddEvenFill);
            maskPath.addRect(rect());
            maskPath.addRect(candidate);
            painter.fillPath(maskPath, maskColor);

            // 绘制高亮虚线候选框
            painter.setRenderHint(QPainter::Antialiasing, false);
            QPen candidatePen(QColor("#006B6C"), 2, Qt::DashLine);
            painter.setPen(candidatePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(candidate);
            painter.setRenderHint(QPainter::Antialiasing, true);
        } else {
            // 全屏暗色遮罩
            painter.fillRect(rect(), maskColor);
        }
    } else {
        // 已有选区：使用 QPainterPath 单通道镂空，无拼接缝
        QRect norm = m_selectionRect.normalized();
        QPainterPath maskPath;
        maskPath.setFillRule(Qt::OddEvenFill);
        maskPath.addRect(rect());
        maskPath.addRect(norm);
        painter.fillPath(maskPath, maskColor);

        // 3. 绘制矢量标注图元
        painter.save();
        painter.setClipRect(norm);
        m_annotationMgr.renderAnnotations(painter);

        // 绘制正在拖拽绘制中的图元实时预览 (矩形/箭头/画笔/马赛克)
        if (m_isDrawingAnnotation) {
            if (m_currentTool == ToolAction::Rect) {
                painter.setPen(QPen(m_annotColor, m_annotStrokeWidth));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRect(m_annotStartPos, m_currentAnnotMousePos).normalized());
            } else if (m_currentTool == ToolAction::Arrow) {
                ArrowAnnotation tempArrow(m_annotStartPos, m_currentAnnotMousePos, m_annotColor, m_annotStrokeWidth);
                tempArrow.draw(painter);
            } else if (m_currentTool == ToolAction::Mosaic) {
                QRect previewRect = QRect(m_annotStartPos, m_currentAnnotMousePos).normalized();
                painter.setPen(QPen(QColor("#006B6C"), 1.5, Qt::DashLine));
                QColor previewFill = QColor("#006B6C");
                previewFill.setAlpha(50);
                painter.setBrush(previewFill);
                painter.drawRect(previewRect);
            } else if (m_currentTool == ToolAction::Pencil && m_currentPencil) {
                m_currentPencil->draw(painter);
            }
        }
        painter.restore();

        // 4. 绘制选区高亮边框 (关闭抗锯齿，确保边框精准对齐物理像素，不向外溢色)
        painter.setRenderHint(QPainter::Antialiasing, false);
        QPen borderPen(QColor("#006B6C"), 2, Qt::SolidLine);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(norm);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
        const QString dimensions = QString("%1 × %2 px")
            .arg(qRound(norm.width() * dpr)).arg(qRound(norm.height() * dpr));
        QFont labelFont = font();
        labelFont.setPixelSize(12);
        painter.setFont(labelFont);
        const QFontMetrics metrics(labelFont);
        const QRect boundary = selectionScreenBoundary();
        const int labelWidth = qMin(metrics.horizontalAdvance(dimensions) + 16, boundary.width());
        const int labelHeight = metrics.height() + 10;
        int labelY = norm.top() - labelHeight - 6;
        if (labelY < boundary.top()) labelY = norm.top() + 6;
        QRect label(qBound(boundary.left(), norm.left(), boundary.right() - labelWidth + 1),
                    qBound(boundary.top(), labelY, qMax(boundary.top(), boundary.bottom() - labelHeight + 1)),
                    labelWidth, labelHeight);
        painter.setPen(Qt::NoPen);
        QColor labelBackground("#006B6C");
        painter.setBrush(labelBackground);
        painter.drawRoundedRect(label, 4, 4);
        painter.setPen(QColor("#F3FAF5"));
        painter.drawText(label, Qt::AlignCenter, dimensions);

        // 绘制单字符就地修改模式下的高亮磁吸字符框
        if (m_currentTool == ToolAction::CharEdit && m_hoveredCharIndex >= 0 && m_hoveredCharIndex < m_charUnits.size()) {
            QRect hBox = m_charUnits[m_hoveredCharIndex].logicalBox;
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            QColor charOutline = QColor("#006B6C");
            charOutline.setAlpha(230);
            QColor charFill = QColor("#006B6C");
            charFill.setAlpha(50);
            painter.setPen(QPen(charOutline, 1.5, Qt::SolidLine));
            painter.setBrush(charFill);
            painter.drawRoundedRect(hBox.adjusted(-1, -1, 1, 1), 3, 3);
            painter.restore();
        }

        // 5. 绘制 8 个几何控制手柄 (选区固定状态下显示)
        if (m_state == SnippingState::Selected && m_currentTool == ToolAction::Cancel) {
            painter.setBrush(QColor("#F3FAF5"));
            painter.setPen(QPen(QColor("#006B6C"), 1));
            const int half = HANDLE_SIZE / 2;

            QPoint pts[8] = {
                norm.topLeft(),
                QPoint(norm.center().x(), norm.top()),
                norm.topRight(),
                QPoint(norm.right(), norm.center().y()),
                norm.bottomRight(),
                QPoint(norm.center().x(), norm.bottom()),
                norm.bottomLeft(),
                QPoint(norm.left(), norm.center().y())
            };

            for (const auto& p : pts) {
                painter.drawRect(p.x() - half, p.y() - half, HANDLE_SIZE, HANDLE_SIZE);
            }
        }
    }
}

void SnippingOverlay::mousePressEvent(QMouseEvent* event)
{
    qInfo() << "[CaptureInput] press state=" << int(m_state) << "button=" << event->button();
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        if (m_state == SnippingState::Selected) {
            // 如果处于标注工具模式，优先进入标注绘制
            if (m_currentTool != ToolAction::Cancel && m_selectionRect.normalized().contains(event->pos())) {
                if (m_currentTool == ToolAction::Text) {
                    if (m_textEditor) {
                        m_textEditor->setTextFont(m_annotFont);
                        m_textEditor->startEdit(event->pos());
                    }
                    return;
                }
                if (m_currentTool == ToolAction::CharEdit) {
                    int clickIndex = CharSegmentation::findCharUnitAt(m_charUnits, event->pos());
                    if (clickIndex == -1) {
                        clickIndex = m_hoveredCharIndex;
                    }
                    if (clickIndex >= 0 && clickIndex < m_charUnits.size()) {
                        m_hoveredCharIndex = clickIndex;
                        qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
                        if (dpr <= 0.0) dpr = 1.0;
                        auto attrs = CharFontAnalyzer::analyze(m_snapshot.fullSnapshot.toImage(), m_charUnits[clickIndex].physicalBox, m_charUnits[clickIndex].character, dpr);
                        m_charUnits[clickIndex].estimatedFgColor = attrs.fgColor;
                        m_charUnits[clickIndex].estimatedBgColor = attrs.bgColor;
                        m_charUnits[clickIndex].estimatedFontSize = attrs.fontSizePt;
                        m_charUnits[clickIndex].estimatedFontFamily = attrs.fontFamily;
                        m_charUnits[clickIndex].isBold = attrs.isBold;

                        if (m_singleCharEditor) {
                            m_singleCharEditor->startEdit(m_charUnits[clickIndex], rect());
                        }
                    }
                    return;
                }
                m_isDrawingAnnotation = true;
                m_annotStartPos = event->pos();
                m_currentAnnotMousePos = event->pos();
                if (m_currentTool == ToolAction::Pencil) {
                    m_currentPencil = std::make_shared<PencilAnnotation>(m_annotColor, m_annotStrokeWidth);
                    m_currentPencil->addPoint(event->pos());
                }
                update();
                return;
            }

            m_activeHandle = getHandleAt(event->pos());
            if (m_activeHandle == HandleType::Inside) {
                m_moveOffset = event->pos() - m_selectionRect.topLeft();
            } else if (m_activeHandle == HandleType::None) {
                m_dragStartPos = event->pos();
                m_selectionRect = QRect(m_dragStartPos, QSize(0, 0));
                m_state = SnippingState::Selecting;
                if (m_toolbar) m_toolbar->hide();
            }
            update();
            return;
        }

        if (m_state == SnippingState::Idle) {
            resetCandidatePreview();
            m_dragStartPos = event->pos();
            m_selectionRect = QRect(m_dragStartPos, QSize(0, 0));
            m_state = SnippingState::Selecting;
            if (m_magnifier) m_magnifier->hide();
            if (m_toolbar) m_toolbar->hide();
            update();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_state == SnippingState::Selected) {
            if (m_currentTool != ToolAction::Cancel) {
                selectTool(ToolAction::Cancel);
                update();
                return;
            }
            m_selectionRect = QRect();
            m_charUnits.clear();
            m_hoveredCharIndex = -1;
            m_state = SnippingState::Idle;
            if (m_toolbar) m_toolbar->hide();
            m_candidatesManuallyChosen = false;
            m_smartCandidates.clear();
            resetCandidateAdjustment();
            resetCandidatePreview();
            updateSmartCandidates(event->pos());
            if (m_magnifier) {
                m_magnifier->updatePosition(event->pos(), m_snapshot.fullSnapshot, rect());
                m_magnifier->show();
            }
            updateCursorForHandle(HandleType::None);
            update();
        } else {
            triggerCancelAction();
        }
    }
}

bool SnippingOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Text mode opens an empty editor on the first click of a double-click.
    // Forward its second click, while keeping word selection in nonempty editors.
    if ((watched == m_textEditor || watched == m_textEditor->viewport()) &&
        event->type() == QEvent::MouseButtonDblClick && m_textEditor->toPlainText().isEmpty()) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint position = static_cast<QWidget*>(watched)->mapTo(this, mouse->position().toPoint());
        QMouseEvent forwarded(event->type(), QPointF(position), mouse->globalPosition(),
                              mouse->button(), mouse->buttons(), mouse->modifiers());
        forwarded.setAccepted(false);
        mouseDoubleClickEvent(&forwarded);
        if (forwarded.isAccepted()) return true;
    }
    return QWidget::eventFilter(watched, event);
}

void SnippingOverlay::mouseDoubleClickEvent(QMouseEvent* event)
{
    qInfo() << "[CaptureInput] doubleClick state=" << int(m_state);
    if (event->button() == Qt::LeftButton && m_state == SnippingState::Selected &&
        m_selectionRect.normalized().contains(event->pos()) &&
        (!m_textEditor->isVisible() || m_textEditor->toPlainText().isEmpty()) && !m_singleCharEditor->isVisible()) {
        m_textEditor->hide();
        triggerConfirmAction();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SnippingOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_state == SnippingState::Selecting) {
        m_selectionRect = QRect(m_dragStartPos, event->pos()).normalized();
        update();
    } else if (m_state == SnippingState::Selected) {
        if (event->buttons() & Qt::LeftButton) {
            if (m_isDrawingAnnotation) {
                m_currentAnnotMousePos = event->pos();
                if (m_currentTool == ToolAction::Pencil && m_currentPencil) {
                    m_currentPencil->addPoint(event->pos());
                }
                update();
                return;
            }

            if (m_activeHandle == HandleType::Inside) {
                QPoint newTopLeft = event->pos() - m_moveOffset;
                m_selectionRect.moveTo(newTopLeft);
            } else if (m_activeHandle != HandleType::None) {
                resizeSelection(event->pos());
            }
            if (m_activeHandle != HandleType::None) {
                m_charUnits.clear();
                m_hoveredCharIndex = -1;
            }
            if (m_toolbar) {
                updateToolbarPosition();
            }
            update();
        } else {
            updateCursorAt(event->pos());
            if (m_currentTool == ToolAction::CharEdit) {
                int idx = CharSegmentation::findCharUnitAt(m_charUnits, event->pos());
                if (idx != m_hoveredCharIndex) {
                    m_hoveredCharIndex = idx;
                    update();
                }
            }
        }
    } else if (m_state == SnippingState::Idle) {
        updateSmartCandidates(event->pos());
        if (m_magnifier) {
            m_magnifier->updatePosition(event->pos(), m_snapshot.fullSnapshot, rect());
            if (!m_magnifier->isVisible()) {
                m_magnifier->show();
            }
        }
        update();
    }
}

bool SnippingOverlay::updateSmartCandidates(const QPoint& position)
{
    if (m_state != SnippingState::Idle) return false;
    auto candidates = m_enableSmartSnapping ? m_snapper.findCandidatesAt(position) : std::vector<QRect>{};
    return applySmartCandidates(position, std::move(candidates), m_snapper.candidateContext());
}

bool SnippingOverlay::applySmartCandidates(const QPoint& position, std::vector<QRect> candidates,
                                          const SmartSnapper::CandidateContext& context)
{
    if (m_state != SnippingState::Idle) return false;
    const bool ownerChanged = context.ownerWindow != m_candidateContext.ownerWindow ||
        context.windowRect != m_candidateContext.windowRect || context.generation != m_candidateContext.generation ||
        context.occludedRegion != m_candidateContext.occludedRegion;
    m_candidatePosition = position;
    if (m_candidatesManuallyChosen && !ownerChanged && m_candidateIndex < m_smartCandidates.size()) {
        const auto movement = position - m_candidateAnchor;
        const auto selected = m_smartCandidates[m_candidateIndex].united(currentCandidateRect()).adjusted(-4, -4, 4, 4);
        if (std::max(std::abs(movement.x()), std::abs(movement.y())) <= 4 && selected.contains(position))
            return false; // Keep the complete list while the user is choosing through it.
    }
    const bool releasedChoice = m_candidatesManuallyChosen;
    m_candidatesManuallyChosen = false;
    m_candidateContext = context;
    // A better ranked UIA/CV candidate may upgrade the initial window or visual fallback.
    // When the leading candidate merely shifts a few pixels, keep its old painted edges.
    if (!ownerChanged && !candidates.empty() && m_candidateIndex < m_smartCandidates.size()) {
        const auto previous = m_smartCandidates[m_candidateIndex];
        const auto next = candidates.front();
        if (previous.contains(position) && std::abs(previous.left() - next.left()) <= 4 &&
            std::abs(previous.top() - next.top()) <= 4 && std::abs(previous.right() - next.right()) <= 4 &&
            std::abs(previous.bottom() - next.bottom()) <= 4)
            candidates.front() = previous;
    }
    if (candidates == m_smartCandidates && !ownerChanged && !releasedChoice) return false;
    m_smartCandidates = std::move(candidates);
    m_candidateIndex = 0;
    m_candidateWheelDelta = 0;
    resetCandidateAdjustment();
    updateCandidatePreview(!ownerChanged);
    return true;
}

void SnippingOverlay::updateCandidatePreview(bool animate)
{
    if (m_smartCandidates.empty()) {
        resetCandidatePreview();
        return;
    }
    const QRectF target(currentCandidateRect());
    if (animate && m_candidateAnimationsEnabled && m_candidateAnimation.state() == QAbstractAnimation::Running &&
        m_candidateAnimation.endValue().toRectF() == target) return;
    const QRectF from = m_candidateDisplayRect;
    m_candidateAnimation.stop();
    if (!animate || !m_candidateAnimationsEnabled || !from.isValid() || from == target) {
        m_candidateDisplayRect = target;
        return;
    }
    // Retarget from the currently painted frame; never queue intermediate choices.
    m_candidateAnimation.setStartValue(from);
    m_candidateAnimation.setEndValue(target);
    m_candidateAnimation.start();
}

void SnippingOverlay::resetCandidatePreview()
{
    m_candidateAnimation.stop();
    m_candidateDisplayRect = {};
}

void SnippingOverlay::hideEvent(QHideEvent* event)
{
    resetCandidatePreview();
    resetCandidateAdjustment();
    m_candidateWheelDelta = 0;
    QWidget::hideEvent(event);
}

void SnippingOverlay::acceptContourAnalysis(SmartSnapper analyzed, quint64 generation)
{
    if (generation != m_captureGeneration || !isVisible()) return;
    m_snapper.adoptSnapshotRegions(std::move(analyzed));
    if (m_state == SnippingState::Idle && updateSmartCandidates(mapFromGlobal(QCursor::pos()))) update();
}

void SnippingOverlay::freezeSmartCandidates()
{
    m_candidateAnchor = m_candidatePosition;
    m_candidatesManuallyChosen = true;
}

void SnippingOverlay::wheelEvent(QWheelEvent* event)
{
    const auto& c = HotkeyConfig::instance().data();
    const bool smaller = HotkeyConfig::matches(event, c.snippingSmallerCandidate);
    const bool larger = HotkeyConfig::matches(event, c.snippingLargerCandidate);
    const bool shrink = HotkeyConfig::matches(event, c.snippingShrinkCandidate);
    const bool expand = HotkeyConfig::matches(event, c.snippingExpandCandidate);
    if (m_state != SnippingState::Idle || !m_enableSmartSnapping || (!smaller && !larger && !shrink && !expand)) {
        m_candidateWheelDelta = 0;
        m_candidateResizeWheelDelta = 0;
        QWidget::wheelEvent(event);
        return;
    }
    // Start navigation from the frame the user saw, even if UIA completed since
    // the last paint. applySmartCandidates releases this on a new point/owner.
    if (!m_smartCandidates.empty()) freezeSmartCandidates();
    updateSmartCandidates(event->position().toPoint());
    event->accept();
    update();
    if (shrink || expand) {
        m_candidateWheelDelta = 0;
        m_candidateResizeWheelDelta += (expand ? 1 : -1) * std::abs(event->angleDelta().y());
        const int steps = m_candidateResizeWheelDelta / 120;
        m_candidateResizeWheelDelta %= 120;
        if (steps) resizeCurrentCandidate(steps);
        return;
    }
    m_candidateResizeWheelDelta = 0;
    // Accumulate action steps so remapped directions and modifiers cannot mix partial detents.
    m_candidateWheelDelta += (larger ? 1 : -1) * std::abs(event->angleDelta().y());
    const int steps = m_candidateWheelDelta / 120;
    m_candidateWheelDelta %= 120;
    if (steps) selectCandidateByArea(steps);
}

void SnippingOverlay::selectCandidateByArea(int steps)
{
    if (m_smartCandidates.empty()) return;
    freezeSmartCandidates();

    // Keep the snapper's ranking for Tab; the wheel walks the same candidates by area.
    std::vector<size_t> order(m_smartCandidates.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        const auto& left = m_smartCandidates[a];
        const auto& right = m_smartCandidates[b];
        return qint64(left.width()) * left.height() < qint64(right.width()) * right.height();
    });
    auto rank = std::find(order.begin(), order.end(), m_candidateIndex) - order.begin();
    // Positive steps select larger candidates; stop at either endpoint.
    rank = std::clamp(rank + steps, decltype(rank){0}, decltype(rank)(order.size() - 1));
    m_candidateIndex = order[size_t(rank)];
    resetCandidateAdjustment();
    updateCandidatePreview();
    update();
}

QRect SnippingOverlay::currentCandidateRect() const
{
    if (m_candidateIndex >= m_smartCandidates.size()) return {};
    return m_adjustedCandidateRect.isValid() ? m_adjustedCandidateRect : m_smartCandidates[m_candidateIndex];
}

void SnippingOverlay::resizeCurrentCandidate(int steps)
{
    if (m_state != SnippingState::Idle || !m_enableSmartSnapping || m_smartCandidates.empty()) return;
    freezeSmartCandidates();
    const QRect current = currentCandidateRect().intersected(rect());
    const int distance = steps * HotkeyConfig::instance().data().snippingCandidateResizeStep;
    if (distance > 0) {
        m_adjustedCandidateRect = current.adjusted(-distance, -distance, distance, distance).intersected(rect());
    } else {
        // Clamp both edges to the same center pixel: shrinking never flips a narrow rectangle.
        const QPoint center = current.center();
        m_adjustedCandidateRect = QRect(QPoint(std::min(current.left() - distance, center.x()),
                                              std::min(current.top() - distance, center.y())),
                                        QPoint(std::max(current.right() + distance, center.x()),
                                               std::max(current.bottom() + distance, center.y())));
    }
    updateCandidatePreview();
    update();
}

void SnippingOverlay::resetCandidateAdjustment()
{
    m_adjustedCandidateRect = {};
    m_candidateResizeWheelDelta = 0;
}

void SnippingOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_state == SnippingState::Selecting) {
            int dist = (event->pos() - m_dragStartPos).manhattanLength();
            if (dist <= 4) {
                // 单击：吸附智能候选框 (仅在开启智能吸附且有候选时吸附)
                if (m_enableSmartSnapping && !m_smartCandidates.empty() && m_candidateIndex < m_smartCandidates.size()) {
                    m_selectionRect = currentCandidateRect();
                    m_state = SnippingState::Selected;
                    if (m_magnifier) m_magnifier->hide();
                    if (m_toolbar) {
                        updateToolbarPosition();
                        m_toolbar->show();
                    }
                    m_charUnits.clear();
                    m_hoveredCharIndex = -1;
                    if (m_currentTool == ToolAction::CharEdit) detectCharsInSelection();
                } else {
                    m_selectionRect = QRect();
                    m_state = SnippingState::Idle;
                    if (m_toolbar) m_toolbar->hide();
                    if (m_magnifier) {
                        m_magnifier->updatePosition(event->pos(), m_snapshot.fullSnapshot, rect());
                        m_magnifier->show();
                    }
                }
            } else {
                // 拖拽：建立手动选区
                m_selectionRect = QRect(m_dragStartPos, event->pos()).normalized();
                if (m_selectionRect.width() > 3 && m_selectionRect.height() > 3) {
                    m_state = SnippingState::Selected;
                    if (m_magnifier) m_magnifier->hide();
                    if (m_toolbar) {
                        updateToolbarPosition();
                        m_toolbar->show();
                    }
                    m_charUnits.clear();
                    m_hoveredCharIndex = -1;
                    if (m_currentTool == ToolAction::CharEdit) detectCharsInSelection();
                } else {
                    m_selectionRect = QRect();
                    m_state = SnippingState::Idle;
                    if (m_toolbar) m_toolbar->hide();
                    m_candidatesManuallyChosen = false;
                    m_smartCandidates.clear();
                    resetCandidateAdjustment();
                    updateSmartCandidates(event->pos());
                    if (m_magnifier) {
                        m_magnifier->updatePosition(event->pos(), m_snapshot.fullSnapshot, rect());
                        m_magnifier->show();
                    }
                }
            }
            update();
        } else if (m_state == SnippingState::Selected) {
            if (m_isDrawingAnnotation) {
                if (m_currentTool == ToolAction::Rect && m_annotStartPos != event->pos()) {
                    m_annotationMgr.addItem(std::make_shared<RectAnnotation>(
                        QRect(m_annotStartPos, event->pos()).normalized(), m_annotColor, m_annotStrokeWidth));
                } else if (m_currentTool == ToolAction::Arrow && m_annotStartPos != event->pos()) {
                    m_annotationMgr.addItem(std::make_shared<ArrowAnnotation>(
                        m_annotStartPos, event->pos(), m_annotColor, m_annotStrokeWidth));
                } else if (m_currentTool == ToolAction::Pencil) {
                    if (m_currentPencil && m_currentPencil->hasStroke()) {
                        m_annotationMgr.addItem(m_currentPencil);
                    }
                    m_currentPencil.reset();
                } else if (m_currentTool == ToolAction::Mosaic) {
                    QRect mosaicRect = QRect(m_annotStartPos, event->pos()).normalized();
                    if (mosaicRect.width() > 3 && mosaicRect.height() > 3) {
                        m_annotationMgr.addItem(std::make_shared<MosaicAnnotation>(
                            mosaicRect, m_snapshot.fullSnapshot, m_snapshot.fullSnapshot.devicePixelRatio(), m_mosaicBlockSize));
                    }
                }
                m_isDrawingAnnotation = false;
                update();
                return;
            }

            bool wasResizingOrMoving = (m_activeHandle != HandleType::None);
            m_activeHandle = HandleType::None;
            if (m_toolbar) {
                updateToolbarPosition();
                m_toolbar->show();
            }
            if (wasResizingOrMoving) {
                m_charUnits.clear();
                m_hoveredCharIndex = -1;
                if (m_currentTool == ToolAction::CharEdit) detectCharsInSelection();
            }
            update();
        }
    }
}

void SnippingOverlay::detectCharsInSelection()
{
    if (m_selectionRect.isNull() || !m_selectionRect.isValid() || m_ocrRunningForSelection) {
        return;
    }
    m_ocrRunningForSelection = true;
    updateOcrBusyState();
    const quint64 generation = m_captureGeneration;
    const QRect selection = m_selectionRect;

    QRect norm = m_selectionRect.normalized();
    qreal dpr = m_snapshot.fullSnapshot.devicePixelRatio();
    if (dpr <= 0.0) dpr = 1.0;

    QRect physicalNorm(
        static_cast<int>(std::round(norm.x() * dpr)),
        static_cast<int>(std::round(norm.y() * dpr)),
        static_cast<int>(std::round(norm.width() * dpr)),
        static_cast<int>(std::round(norm.height() * dpr))
    );

    QImage crop = m_snapshot.fullSnapshot.copy(physicalNorm).toImage();
    OcrManager::instance().recognizeAsync(crop, dpr, [this, physicalNorm, dpr, generation, selection](const OcrResult& result, const QString& /*engineName*/) {
        if (generation != m_captureGeneration) return;
        m_ocrRunningForSelection = false;
        updateOcrBusyState();
        if (selection != m_selectionRect) {
            m_charUnits.clear();
            if (isVisible() && m_state == SnippingState::Selected && m_currentTool == ToolAction::CharEdit) detectCharsInSelection();
            return;
        }
        if (!result.success || result.lines.isEmpty()) {
            if (m_currentTool == ToolAction::CharEdit) {
                updateCursorAt(mapFromGlobal(QCursor::pos()));
            }
            update();
            return;
        }

        // 坐标偏移转换回全屏全景物理坐标
        QList<OcrLine> globalLines;
        for (const auto& line : result.lines) {
            OcrLine gLine = line;
            gLine.boundingBox.translate(physicalNorm.topLeft());
            gLine.logicalBox = QRect(
                static_cast<int>(std::round(gLine.boundingBox.x() / dpr)),
                static_cast<int>(std::round(gLine.boundingBox.y() / dpr)),
                static_cast<int>(std::round(gLine.boundingBox.width() / dpr)),
                static_cast<int>(std::round(gLine.boundingBox.height() / dpr))
            );
            for (auto& w : gLine.words) {
                w.boundingBox.translate(physicalNorm.topLeft());
                w.logicalBox = QRect(
                    static_cast<int>(std::round(w.boundingBox.x() / dpr)),
                    static_cast<int>(std::round(w.boundingBox.y() / dpr)),
                    static_cast<int>(std::round(w.boundingBox.width() / dpr)),
                    static_cast<int>(std::round(w.boundingBox.height() / dpr))
                );
            }
            globalLines.append(gLine);
        }

        m_charUnits = CharSegmentation::segmentAllLines(m_snapshot.fullSnapshot.toImage(), globalLines, dpr);

        // 如果用户已处于单字修改模式，立即恢复光标并直接吸附当前鼠标指针下的字符
        if (m_currentTool == ToolAction::CharEdit) {
            updateCursorAt(mapFromGlobal(QCursor::pos()));
            QPoint mousePos = mapFromGlobal(QCursor::pos());
            m_hoveredCharIndex = CharSegmentation::findCharUnitAt(m_charUnits, mousePos);
        }
        update();
    });
}

void SnippingOverlay::selectTool(ToolAction tool)
{
    if (tool == ToolAction::CharEdit && m_textOcrRunning) return;
    if (tool != ToolAction::Cancel && tool == m_currentTool) tool = ToolAction::Cancel;
    m_currentTool = tool;
    m_isDrawingAnnotation = false;
    m_currentPencil.reset();
    if (m_toolbar) {
        m_toolbar->setActiveTool(tool);
    }
    if (tool == ToolAction::Text) {
        setCursor(Qt::IBeamCursor);
    } else if (tool == ToolAction::CharEdit) {
        if (m_ocrRunningForSelection) {
            // 后台 OCR 正在分析中，显示等待光标与即时反馈
            setCursor(Qt::WaitCursor);
        } else {
            setCursor(Qt::IBeamCursor);
            if (m_charUnits.isEmpty()) {
                detectCharsInSelection();
            } else {
                QPoint mousePos = mapFromGlobal(QCursor::pos());
                m_hoveredCharIndex = CharSegmentation::findCharUnitAt(m_charUnits, mousePos);
            }
        }
        updateCursorAt(mapFromGlobal(QCursor::pos()));
        update();
        return;
    } else {
        setCursor(Qt::CrossCursor);
    }
    updateCursorAt(mapFromGlobal(QCursor::pos()));
    update();
}

void SnippingOverlay::triggerUndoAction()
{
    if (m_textEditor && m_textEditor->isVisible()) {
        return;
    }
    m_annotationMgr.undo();
    update();
}

void SnippingOverlay::triggerPinAction()
{
    if (hasValidSelection()) {
        ++m_captureGeneration;
        QRect norm = m_selectionRect.normalized();
        QPixmap composite = renderSelectedArea();
        if (m_magnifier) m_magnifier->hide();
        if (m_toolbar) m_toolbar->hide();
        hide();
        emit pinRequested(composite, norm.translated(m_snapshot.virtualGeometry.topLeft()));
    }
}

void SnippingOverlay::triggerConfirmAction()
{
    if (hasValidSelection()) {
        ++m_captureGeneration;
        if (m_magnifier) m_magnifier->hide();
        if (m_toolbar) m_toolbar->hide();
        QRect norm = m_selectionRect.normalized();
        QPixmap composite = renderSelectedArea();
        QApplication::clipboard()->setPixmap(composite);
        hide();
        emit snippingFinished(composite, norm);
    }
}

void SnippingOverlay::triggerSaveAction()
{
    if (hasValidSelection()) {
        QRect norm = m_selectionRect.normalized();
        QPixmap composite = renderSelectedArea();
        QString defaultName = QString("AccuShot_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString path = QFileDialog::getSaveFileName(this, "保存截图", defaultName, ImageExporter::getSaveFileFilter());
        if (!path.isEmpty()) {
            ++m_captureGeneration;
            ImageExporter::saveImage(composite.toImage(), path);
            if (m_magnifier) m_magnifier->hide();
            if (m_toolbar) m_toolbar->hide();
            hide();
            emit snippingFinished(composite, norm);
        }
    }
}

void SnippingOverlay::triggerOcrAction()
{
    recognizeSelectedText(false, false);
}

void SnippingOverlay::triggerTranslateAction()
{
    recognizeSelectedText(true, false);
}

void SnippingOverlay::triggerScreenTranslateAction()
{
    if (!hasValidSelection() || m_textOcrRunning || m_ocrRunningForSelection) return;
    const QRect selection = m_selectionRect.normalized();
    const QRect screenRect(mapToGlobal(selection.topLeft()), selection.size());
    const QPixmap composite = renderSelectedArea();
    ++m_captureGeneration;
    m_magnifier->hide();
    m_toolbar->hide();
    hide();
    auto* window = new ScreenTranslationWindow(composite, screenRect);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->raise();
    window->activateWindow();
    window->startRecognition();
    emit snippingFinished(composite, selection);
}

void SnippingOverlay::triggerCopyTextAction()
{
    recognizeSelectedText(false, true);
}

void SnippingOverlay::recognizeSelectedText(bool translate, bool copyOnly)
{
    if (!hasValidSelection() || m_textOcrRunning || m_ocrRunningForSelection) return;
    const QRect selection = m_selectionRect.normalized();
    const QPixmap composite = renderSelectedArea();
    const quint64 generation = m_captureGeneration;
    qInfo() << "[CaptureOCR] begin generation=" << generation << "translate=" << translate
            << "copyOnly=" << copyOnly << "pixels=" << composite.size()
            << "engine=" << OcrManager::instance().activeEngineName();
    if (!copyOnly) {
        ++m_captureGeneration;
        m_magnifier->hide();
        m_toolbar->hide();
        hide();
        auto* dialog = new OcrResultDialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setTranslationRequested(translate);
        dialog->setImage(composite);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        dialog->startRecognition();
        emit snippingFinished(composite, selection);
        return;
    }
    m_textOcrRunning = true;
    updateOcrBusyState();
    OcrManager::instance().recognizeAsync(composite.toImage(), composite.devicePixelRatio(),
        [this, generation, selection, composite](const OcrResult& result, const QString& engineName) {
            qInfo() << "[CaptureOCR] result generation=" << generation << "current=" << m_captureGeneration
                    << "success=" << result.success << "chars=" << result.fullText.size() << "engine=" << engineName;
            if (generation != m_captureGeneration) return;
            m_textOcrRunning = false;
            updateOcrBusyState();
            if (selection != m_selectionRect.normalized()) return;
            QString text = result.fullText.trimmed();
            if (text.isEmpty()) {
                QStringList lines;
                for (const auto& line : result.lines) lines.append(line.text);
                text = lines.join('\n').trimmed();
            }
            if (!result.success || text.isEmpty()) {
                const QString message = result.success ? "选区内未识别到文字，请调整选区后重试。" :
                    (result.errorMessage.isEmpty() ? "文字识别失败，请重试。" : result.errorMessage);
                QMessageBox::information(this, "文字识别", message);
                return;
            }
            ++m_captureGeneration;
            m_magnifier->hide();
            m_toolbar->hide();
            hide();
            QApplication::clipboard()->setText(text);
            emit snippingFinished(composite, selection);
        });
}

void SnippingOverlay::triggerFormulaOcrAction()
{
    if (hasValidSelection() && !m_textOcrRunning && !m_ocrRunningForSelection) {
        ++m_captureGeneration;
        QRect norm = m_selectionRect.normalized();
        QPixmap composite = renderSelectedArea();

        // 立即关闭截屏全屏底图遮罩及所有工具挂件，恢复正常桌面
        if (m_magnifier) m_magnifier->hide();
        if (m_toolbar) m_toolbar->hide();
        hide();
        emit snippingFinished(composite, norm);

        auto* dialog = new FormulaResultDialog(nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        dialog->startRecognition(composite, composite.devicePixelRatio());
    }
}

void SnippingOverlay::triggerMarkdownAction()
{
    if(!hasValidSelection() || m_textOcrRunning || m_ocrRunningForSelection)return;
    ++m_captureGeneration;
    const auto composite=renderSelectedArea();
    const auto selection=m_selectionRect.normalized();
    if(m_magnifier)m_magnifier->hide();
    if(m_toolbar)m_toolbar->hide();
    hide();
    emit snippingFinished(composite,selection);
    auto* dialog=new MarkdownResultDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setImage(composite);
    dialog->show();dialog->raise();dialog->activateWindow();
}

void SnippingOverlay::triggerCancelAction()
{
    resetCandidatePreview();
    resetCandidateAdjustment();
    m_candidateWheelDelta = 0;
    ++m_captureGeneration;
    m_candidatesManuallyChosen = false;
    m_textOcrRunning = false;
    m_ocrRunningForSelection = false;
    updateOcrBusyState();
    if (m_magnifier) m_magnifier->hide();
    if (m_toolbar) m_toolbar->hide();
    hide();
    emit snippingCancelled();
}

void SnippingOverlay::keyPressEvent(QKeyEvent* event)
{
    const auto& c = HotkeyConfig::instance().data();

    QWidget* focused = QApplication::focusWidget();
    if (qobject_cast<QLineEdit*>(focused) || qobject_cast<QAbstractSpinBox*>(focused) || qobject_cast<QComboBox*>(focused) ||
        (m_textEditor->isVisible()) || (m_singleCharEditor->isVisible())) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (m_state == SnippingState::Idle && m_enableSmartSnapping) {
        const bool shrink = HotkeyConfig::matches(event, c.snippingShrinkCandidate);
        const bool expand = HotkeyConfig::matches(event, c.snippingExpandCandidate);
        if (shrink || expand) {
            m_candidateWheelDelta = 0;
            m_candidateResizeWheelDelta = 0;
            resizeCurrentCandidate(expand ? 1 : -1);
            event->accept();
            return;
        }
        const bool smaller = HotkeyConfig::matches(event, c.snippingSmallerCandidate);
        const bool larger = HotkeyConfig::matches(event, c.snippingLargerCandidate);
        if (smaller || larger) {
            m_candidateWheelDelta = 0;
            m_candidateResizeWheelDelta = 0;
            selectCandidateByArea(larger ? 1 : -1);
            event->accept();
            return;
        }
    }

    // 1. 取消截屏
    if (HotkeyConfig::matches(event, c.snippingCancel) || event->key() == Qt::Key_Escape) {
        if (m_currentTool != ToolAction::Cancel) {
            selectTool(ToolAction::Cancel);
            return;
        }
        triggerCancelAction();
        return;
    }

    // 方向键移动 1 px，Shift 调整右/下边，Ctrl 将步长改为 10 px（逻辑像素）。
    if (m_state == SnippingState::Selected && event->key() >= Qt::Key_Left && event->key() <= Qt::Key_Down) {
        const int step = event->modifiers().testFlag(Qt::ControlModifier) ? 10 : 1;
        QPoint delta;
        if (event->key() == Qt::Key_Left) delta.setX(-step);
        if (event->key() == Qt::Key_Right) delta.setX(step);
        if (event->key() == Qt::Key_Up) delta.setY(-step);
        if (event->key() == Qt::Key_Down) delta.setY(step);
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            m_selectionRect.setRight(qBound(m_selectionRect.left() + 3, m_selectionRect.right() + delta.x(), rect().right()));
            m_selectionRect.setBottom(qBound(m_selectionRect.top() + 3, m_selectionRect.bottom() + delta.y(), rect().bottom()));
        } else {
            m_selectionRect.moveTo(qBound(rect().left(), m_selectionRect.left() + delta.x(), rect().right() - m_selectionRect.width() + 1),
                                   qBound(rect().top(), m_selectionRect.top() + delta.y(), rect().bottom() - m_selectionRect.height() + 1));
        }
        m_charUnits.clear();
        m_hoveredCharIndex = -1;
        updateToolbarPosition();
        if (m_currentTool == ToolAction::CharEdit) detectCharsInSelection();
        update();
        event->accept();
        return;
    }

    // 2. 撤销上一步标注 (修复快捷键)
    if (HotkeyConfig::matches(event, c.snippingUndo) ||
        (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Z)) {
        triggerUndoAction();
        return;
    }

    // 3. 贴图置顶 (修复快捷键)
    if (HotkeyConfig::matches(event, c.snippingPin) || event->key() == Qt::Key_F3) {
        triggerPinAction();
        return;
    }

    // 4. 完成截屏并复制到剪贴板
    if (HotkeyConfig::matches(event, c.snippingConfirm) ||
        event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        triggerConfirmAction();
        return;
    }

    // 5. 保存到本地文件
    if (HotkeyConfig::matches(event, c.snippingSave) ||
        ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_S)) {
        triggerSaveAction();
        return;
    }

    // 6. 文字识别 OCR
    if (HotkeyConfig::matches(event, c.snippingOcr) ||
        ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_O) ||
        (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_O)) {
        triggerOcrAction();
        return;
    }

    // 7. 标注工具切换快捷键 (仅在选区建立后生效)
    if (m_state == SnippingState::Selected && (!m_textEditor || !m_textEditor->isVisible())) {
        if (HotkeyConfig::matches(event, c.toolRect)) {
            selectTool(ToolAction::Rect);
            return;
        }
        if (HotkeyConfig::matches(event, c.toolArrow)) {
            selectTool(ToolAction::Arrow);
            return;
        }
        if (HotkeyConfig::matches(event, c.toolPencil)) {
            selectTool(ToolAction::Pencil);
            return;
        }
        if (HotkeyConfig::matches(event, c.toolText)) {
            selectTool(ToolAction::Text);
            return;
        }
        if (HotkeyConfig::matches(event, c.toolMosaic)) {
            selectTool(ToolAction::Mosaic);
            return;
        }
        if (HotkeyConfig::matches(event, c.toolCharEdit)) {
            selectTool(ToolAction::CharEdit);
            return;
        }
    }

    // 8. 放大镜复制颜色
    if (event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier) {
        if (m_magnifier && m_magnifier->isVisible()) {
            QString hex = m_magnifier->hexColor();
            QApplication::clipboard()->setText(hex);
            qDebug() << "[SnippingOverlay] 颜色代码已复制到剪贴板:" << hex;
        }
        return;
    }

    // 9. 智能吸附候选框 Tab 切换 (仅在开启智能吸附时生效)
    if (event->key() == Qt::Key_Tab && m_enableSmartSnapping) {
        m_candidateWheelDelta = 0;
        resetCandidateAdjustment();
        if (m_state == SnippingState::Idle && !m_smartCandidates.empty()) {
            freezeSmartCandidates();
            m_candidateIndex = (m_candidateIndex + 1) % m_smartCandidates.size();
            updateCandidatePreview();
            update();
        }
        return;
    } else if (event->key() == Qt::Key_Backtab && m_enableSmartSnapping) {
        m_candidateWheelDelta = 0;
        resetCandidateAdjustment();
        if (m_state == SnippingState::Idle && !m_smartCandidates.empty()) {
            freezeSmartCandidates();
            if (m_candidateIndex == 0) {
                m_candidateIndex = m_smartCandidates.size() - 1;
            } else {
                --m_candidateIndex;
            }
            updateCandidatePreview();
            update();
        }
        return;
    }

    QWidget::keyPressEvent(event);
}

void SnippingOverlay::startScrollCapture()
{
    if (!hasValidSelection()) return;
    ++m_captureGeneration;

    QRect norm = m_selectionRect.normalized();
    QPixmap initialPix = renderSelectedArea();

    // 1. 关闭全屏暗色遮罩与所有工具条，彻底把桌面和底层窗口的真实交互与滚轮权归还给浏览器/文档
    if (m_magnifier) m_magnifier->hide();
    if (m_toolbar) m_toolbar->hide();
    hide();

    // 2. 启动长截图会话 (显示独立鼠标穿透边框与悬浮控制胶囊，启动20fps实时差分采样缝合)
    if (!m_scrollSession) {
        m_scrollSession = new ScrollCaptureSession(this);
        connect(m_scrollSession, &ScrollCaptureSession::finished, this, [this](const QPixmap& stitchedPix) {
            QApplication::clipboard()->setPixmap(stitchedPix);
            QRect n = m_selectionRect.normalized();
            emit snippingFinished(stitchedPix, n);
        });
        connect(m_scrollSession, &ScrollCaptureSession::cancelled, this, [this]() {
            QRect n = m_selectionRect.normalized();
            emit snippingFinished(QPixmap(), n);
        });
    }

    m_scrollSession->start(QRect(mapToGlobal(norm.topLeft()), norm.size()), initialPix.toImage());
}

void SnippingOverlay::triggerGifRecord()
{
    if (!hasValidSelection()) return;
    ++m_captureGeneration;

    QRect norm = m_selectionRect.normalized();

    // 关闭全屏暗色遮罩与工具栏，将画面控制权交给所见即所得 GIF 录屏会话
    if (m_magnifier) m_magnifier->hide();
    if (m_toolbar) m_toolbar->hide();
    hide();

    if (!m_gifSession) {
        m_gifSession = new GifRecordSession(this);
        connect(m_gifSession, &GifRecordSession::finished, this, [this](const QString&) {
            QRect n = m_selectionRect.normalized();
            emit snippingFinished(QPixmap(), n);
        });
        connect(m_gifSession, &GifRecordSession::cancelled, this, [this]() {
            QRect n = m_selectionRect.normalized();
            emit snippingFinished(QPixmap(), n);
        });
    }

    m_gifSession->start(norm);
}
