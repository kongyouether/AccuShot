#include "../../core/ai/ai_service.h"
#include "../settings/hotkey_settings_dialog.h"
#include "../themed_combo_box.h"
#include "../toolbar/toolbar_icons.h"
#include "ocr_result_dialog.h"
#include <QPointer>
#include <QSignalBlocker>
#include "../../core/ocr/ocr_manager.h"
#include "../../core/translation/translation_plugin_manager.h"
#include "../plugin/plugin_market_dialog.h"
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QPainter>
#include <QRegularExpression>
#include <QMessageBox>
#include <QDebug>
#include <QKeyEvent>
#include "../../core/theme_manager.h"
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>
#include <QMouseEvent>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QButtonGroup>
#include <QSettings>
#include <QSplitterHandle>
#include <QStyle>
#include <algorithm>

namespace {
constexpr int OcrLineProperty = QTextFormat::UserProperty + 121;

bool intersectsTextRange(const QTextCursor& cursor, int start, int end)
{
    if (cursor.hasSelection()) return cursor.selectionStart() < end && cursor.selectionEnd() > start;
    return cursor.position() >= start && cursor.position() <= end;
}

void scrollToText(QPlainTextEdit* edit, int position)
{
    QTextCursor cursor(edit->document());
    cursor.setPosition(position);
    const auto rect = edit->cursorRect(cursor);
    if (rect.top() < 0 || rect.bottom() > edit->viewport()->height()) {
        // QPlainTextEdit scrolls by visual lines, not pixels. Let Qt locate the line,
        // then restore the user's caret without changing the resulting scroll position.
        const QSignalBlocker blocker(edit);
        const auto originalCursor = edit->textCursor();
        edit->setTextCursor(cursor);
        edit->ensureCursorVisible();
        const int vertical = edit->verticalScrollBar()->value();
        const int horizontal = edit->horizontalScrollBar()->value();
        edit->setTextCursor(originalCursor);
        edit->verticalScrollBar()->setValue(vertical);
        edit->horizontalScrollBar()->setValue(horizontal);
    }
}
}

// -------------------------------------------------------------
// OcrImagePreviewWidget: 左侧原图对比与识别框高亮挂件
// -------------------------------------------------------------
OcrImagePreviewWidget::OcrImagePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::IBeamCursor);
    setObjectName("ocrImagePreview");
}

void OcrImagePreviewWidget::setImage(const QPixmap& pixmap)
{
    m_pixmap = pixmap;
    if (!m_pixmap.isNull()) {
        qreal dpr = m_pixmap.devicePixelRatio();
        if (dpr <= 0.0) dpr = 1.0;
        int w = qRound(m_pixmap.width() / dpr * m_zoom);
        int h = qRound(m_pixmap.height() / dpr * m_zoom);
        setMinimumSize(w, h);
    }
    update();
}

void OcrImagePreviewWidget::setZoom(qreal zoom)
{
    m_zoom = zoom;
    setImage(m_pixmap);
    updateGeometry();
}

QSize OcrImagePreviewWidget::sizeHint() const
{
    if (!m_pixmap.isNull()) {
        qreal dpr = m_pixmap.devicePixelRatio();
        if (dpr <= 0.0) dpr = 1.0;
        return QSize(qRound(m_pixmap.width() / dpr * m_zoom), qRound(m_pixmap.height() / dpr * m_zoom));
    }
    return QSize(400, 300);
}

void OcrImagePreviewWidget::setResult(const OcrResult& result)
{
    m_result = result;
    m_selectedLines.clear();
    update();
}

QRectF OcrImagePreviewWidget::imageRect() const
{
    const QSizeF size = m_pixmap.deviceIndependentSize() * m_zoom;
    return QRectF(QPointF(qMax(0.0, (width() - size.width()) / 2),
                         qMax(0.0, (height() - size.height()) / 2)), size);
}

QRectF OcrImagePreviewWidget::lineRect(int index) const
{
    const auto& line = m_result.lines[index];
    QRectF box = line.logicalBox;
    if (!box.isValid()) {
        const qreal dpr = m_result.dpr > 0 ? m_result.dpr : m_pixmap.devicePixelRatio();
        box = QRectF(QPointF(line.boundingBox.x() / dpr, line.boundingBox.y() / dpr),
                     QSizeF(line.boundingBox.width() / dpr, line.boundingBox.height() / dpr));
    }
    return QRectF(box.topLeft() * m_zoom, box.size() * m_zoom).translated(imageRect().topLeft());
}

void OcrImagePreviewWidget::setSelectedLines(const QList<int>& lines)
{
    if (m_selectedLines == lines) return;
    m_selectedLines = lines;
    update();
}

void OcrImagePreviewWidget::selectImageLines(const QPointF& position, bool dragging)
{
    QList<int> selected;
    const QRectF selection = QRectF(m_dragStart, position).normalized();
    for (int i = 0; i < m_result.lines.size(); ++i) {
        const auto box = lineRect(i);
        if (box.isValid() && (dragging ? selection.intersects(box) : box.contains(position))) {
            selected.append(i);
            if (!dragging) break;
        }
    }
    setSelectedLines(selected);
    emit linesSelected(selected);
}

void OcrImagePreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_result.success) {
        m_selecting = true;
        m_dragStart = event->position();
        selectImageLines(event->position(), false);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void OcrImagePreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting) {
        selectImageLines(event->position(),
                         (event->position() - m_dragStart).manhattanLength() >= QApplication::startDragDistance());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void OcrImagePreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_selecting && event->button() == Qt::LeftButton) {
        selectImageLines(event->position(),
                         (event->position() - m_dragStart).manhattanLength() >= QApplication::startDragDistance());
        m_selecting = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void OcrImagePreviewWidget::setShowBoundingBoxes(bool show)
{
    if (m_showBoundingBoxes != show) {
        m_showBoundingBoxes = show;
        update();
    }
}

void OcrImagePreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_pixmap.isNull()) {
        painter.fillRect(rect(), palette().color(QPalette::Window));
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, "暂无图像");
        return;
    }

    painter.drawPixmap(imageRect(), m_pixmap, QRectF(m_pixmap.rect()));

    if (m_result.success) {
        for (int i = 0; i < m_result.lines.size(); ++i) {
            const bool selected = m_selectedLines.contains(i);
            if (!m_showBoundingBoxes && !selected) continue;
            const auto box = lineRect(i);
            if (!box.isValid()) continue;
            const QColor accent("#006B6C");
            QColor fill = accent;
            fill.setAlpha(selected ? 24 : 18);
            painter.setPen(QPen(accent, selected ? 2.0 : 1.0));
            painter.setBrush(fill);
            painter.drawRoundedRect(box, 3, 3);
        }
    }
}

// -------------------------------------------------------------
// OcrResultDialog: 共享深浅主题、可折叠原图与可调译文区域
// -------------------------------------------------------------
OcrResultDialog::OcrResultDialog(QWidget* parent, TranslationRequest translationRequest)
    : QDialog(parent), m_translationRequest(std::move(translationRequest))
{
    if (!m_translationRequest) {
        m_translationRequest = [](const QString& text, const QString& source, const QString& target,
                                  TranslationCallback callback, const QString& mode) {
            return TranslationPluginManager::instance().translateAsync(text, source, target, std::move(callback), mode);
        };
    }
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle("文字识别与翻译");
    setWindowIcon(appIcon());
    resize(1120, 720);
    setMinimumSize(640, 480);

    setupUi();
    auto applyTheme = [this]() {
        setStyleSheet(ThemeManager::instance().getResultDialogStyle());
        const auto colors = ThemeManager::instance().palette();
        m_showImageBtn->setIcon(uiIcon("capture", colors.text));
        m_toggleBoxesBtn->setIcon(uiIcon("ocr", colors.text));
        m_copyBtn->setIcon(uiIcon("copytext", colors.text));
        m_copyTransBtn->setIcon(uiIcon("copytext", colors.text));
        m_closeBtn->setIcon(uiIcon("close", colors.text));
        m_translateBtn->setIcon(uiIcon(m_isTranslating ? "close" : "translate", colors.onAction));
        m_imagePreview->update();
        for (auto* edit : {m_textEdit, m_translationEdit}) {
            auto selections = edit->extraSelections();
            for (auto& selection : selections)
                selection.format.setBackground(ThemeManager::instance().palette().selected);
            edit->setExtraSelections(selections);
        }
    };
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, applyTheme);
    connect(&ThemeManager::instance(), &ThemeManager::resultFontSizeChanged, this, applyTheme);
    connect(this, &QDialog::finished, this, [this](int result) {
        invalidateTranslation();
        qInfo() << "[ResultWindow] finished dialog=" << quintptr(this) << "result=" << result;
    });
    connect(this, &QObject::destroyed, [id = quintptr(this)] {
        qInfo() << "[ResultWindow] destroyed dialog=" << id;
    });
}

bool OcrResultDialog::event(QEvent* event)
{
    if (event->type() == QEvent::Resize && m_textViewGroup) updateResultLayout();
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
        event->type() == QEvent::Close || event->type() == QEvent::WindowStateChange) {
        qInfo() << "[ResultWindow] event=" << event->type() << "dialog=" << quintptr(this)
                << "visible=" << isVisible() << "state=" << windowState() << "geometry=" << geometry();
    }
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            qInfo() << "[ResultWindow] control key=" << key->key() << "dialog=" << quintptr(this);
    }
    return QDialog::event(event);
}

bool OcrResultDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_splitter->handle(1) && event->type() == QEvent::KeyPress &&
        m_splitter->orientation() == Qt::Horizontal) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
            setImageColumnPercent(m_imagePercent + (key->key() == Qt::Key_Right ? 1 : -1));
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease &&
        (watched == m_textEdit->viewport() || watched == m_translationEdit->viewport())) {
        const bool source = watched == m_textEdit->viewport();
        // Qt may leave the cursor position unchanged on a click; synchronize after it handles the release.
        QTimer::singleShot(0, this, [this, source] {
            if (source && m_textEdit->hasFocus()) syncImageSelection();
            else if (!source && m_translationEdit->hasFocus()) syncSourceFromTranslation();
        });
    }
    return QDialog::eventFilter(watched, event);
}

void OcrResultDialog::setTranslationRequested(bool requested)
{
    m_translationRequested = requested;
    m_textViewGroup->button(requested ? 0 : 1)->setChecked(true);
}

void OcrResultDialog::startRecognition()
{
    m_textEdit->setPlaceholderText(QStringLiteral("正在识别文字，请稍候…可直接关闭此窗口取消查看。"));
    m_translationEdit->setPlaceholderText(QStringLiteral("等待文字识别完成…"));
    m_ocrEngineCombo->setEnabled(false);
    m_translateBtn->setEnabled(false);
    m_langPairCombo->setEnabled(false);
    m_transModeCombo->setEnabled(false);
    m_copyBtn->setEnabled(false);
    m_recognitionTimer = new QTimer(this);
    connect(m_recognitionTimer, &QTimer::timeout, this, [this, seconds = 0]() mutable {
        m_statusLabel->setText(QStringLiteral("正在识别文字… %1 秒").arg(++seconds));
    });
    m_statusLabel->setText(QStringLiteral("正在识别文字…"));
    m_recognitionTimer->start(1000);
    QPointer<OcrResultDialog> dialog(this);
    OcrManager::instance().recognizeAsync(m_image.toImage(), m_image.devicePixelRatio(),
        [dialog](const OcrResult& result, const QString& engine) {
            qInfo() << "[ResultWindow] OCR callback alive=" << bool(dialog)
                    << "success=" << result.success << "engine=" << engine << "chars=" << result.fullText.size();
            if (!dialog) return;
            dialog->m_recognitionTimer->stop();
            dialog->m_ocrEngineCombo->setEnabled(true);
            dialog->m_translateBtn->setEnabled(true);
            dialog->m_langPairCombo->setEnabled(true);
            dialog->m_transModeCombo->setEnabled(true);
            dialog->m_copyBtn->setEnabled(result.success);
            dialog->setResult(result, engine);
        });
}

void OcrResultDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(12);

    auto* viewBar = new QHBoxLayout();
    auto* title = new QLabel("文字识别与翻译", this);
    title->setObjectName("pageTitle");
    viewBar->addWidget(title);
    viewBar->addStretch();
    mainLayout->addLayout(viewBar);
    auto* linkHint = new QLabel("点击原图、原文或译文，查看段落对应关系。编辑原文后需重新翻译。", this);
    linkHint->setObjectName("secondaryText");
    linkHint->setWordWrap(true);
    mainLayout->addWidget(linkHint);

    auto* viewControls = new QHBoxLayout();
    viewControls->setSpacing(8);
    auto* viewSegment = new QWidget(this);
    viewSegment->setObjectName("segmentedControl");
    auto* segmentLayout = new QHBoxLayout(viewSegment);
    segmentLayout->setContentsMargins(4, 4, 4, 4);
    segmentLayout->setSpacing(4);
    m_textViewGroup = new QButtonGroup(this);
    const QStringList viewNames = {"对照", "仅原文", "仅译文"};
    for (int i = 0; i < viewNames.size(); ++i) {
        auto* button = new QPushButton(viewNames[i], this);
        button->setCheckable(true);
        button->setObjectName(i == 0 ? "showTranslation" : "compactAction");
        button->setProperty("segmented", true);
        m_textViewGroup->addButton(button, i);
        segmentLayout->addWidget(button);
        connect(button, &QPushButton::toggled, this, [this](bool checked) {
            if (checked) updateResultLayout();
        });
    }
    viewControls->addWidget(viewSegment);
    m_showImageBtn = new QPushButton("原图", this);
    m_showImageBtn->setObjectName("showImage");
    m_showImageBtn->setCheckable(true);
    m_showImageBtn->setChecked(true);
    viewControls->addWidget(m_showImageBtn);
    viewControls->addStretch();
    auto* fontLabel = new QLabel("字号", this);
    auto* fontSize = new ThemedComboBox(this);
    fontSize->setObjectName("resultFontSize");
    fontSize->setAccessibleName("原文与译文字号");
    fontSize->setMinimumWidth(88);
    for (int size : {14, 15, 16, 18, 20}) fontSize->addItem(QString::number(size), size);
    fontSize->setCurrentIndex(fontSize->findData(ThemeManager::instance().resultFontSize()));
    fontLabel->setBuddy(fontSize);
    viewControls->addWidget(fontLabel);
    viewControls->addWidget(fontSize);
    connect(fontSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [fontSize](int) {
        ThemeManager::instance().setResultFontSize(fontSize->currentData().toInt());
    });
    connect(&ThemeManager::instance(), &ThemeManager::resultFontSizeChanged, this, [fontSize](int size) {
        const QSignalBlocker blocker(fontSize);
        fontSize->setCurrentIndex(fontSize->findData(size));
    });
    mainLayout->addLayout(viewControls);
    // 核心分割器 (QSplitter)
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("resultSplitter");
    m_splitter->setHandleWidth(16);
    m_splitter->setChildrenCollapsible(false);

    // ==========================================
    // 左侧面板：原图对照
    // ==========================================
    auto* leftContainer = new QWidget(this);
    m_imagePanel = leftContainer;
    leftContainer->setObjectName("resultPanel");
    leftContainer->setProperty("surfaceRole", "well");
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(16, 16, 16, 16);
    leftLayout->setSpacing(12);

    auto* leftHeaderLayout = new QHBoxLayout();
    auto* leftTitle = new QLabel("原图", leftContainer);
    leftTitle->setObjectName("sectionTitle");

    leftHeaderLayout->addWidget(leftTitle);
    leftHeaderLayout->addStretch();

    m_toggleBoxesBtn = new QPushButton("识别框", leftContainer);
    m_toggleBoxesBtn->setObjectName("compactAction");
    m_toggleBoxesBtn->setCheckable(true);
    m_toggleBoxesBtn->setChecked(true);
    m_toggleBoxesBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBoxesBtn->setToolTip("显示或隐藏原图上的文字定位框");
    connect(m_toggleBoxesBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (m_imagePreview) {
            m_imagePreview->setShowBoundingBoxes(checked);
        }
    });
    leftHeaderLayout->addWidget(m_toggleBoxesBtn);
    leftLayout->addLayout(leftHeaderLayout);

    auto* imageTools = new QHBoxLayout();
    auto* zoomLabel = new QLabel("缩放", leftContainer);
    auto* zoom = new ThemedComboBox(leftContainer);
    zoom->setObjectName("imageZoom");
    zoom->setAccessibleName("原图缩放比例");
    zoom->setMinimumWidth(96);
    for (int percent : {75, 100, 125, 150, 175, 200}) zoom->addItem(QString::number(percent) + "%", percent);
    zoom->setCurrentIndex(1);
    zoomLabel->setBuddy(zoom);
    imageTools->addWidget(zoomLabel);
    imageTools->addWidget(zoom);
    imageTools->addStretch();
    leftLayout->addLayout(imageTools);

    m_imageScrollArea = new QScrollArea(leftContainer);
    m_imageScrollArea->setObjectName("ocrImageScrollArea");
    m_imageScrollArea->setWidgetResizable(true);
    m_imageScrollArea->setAlignment(Qt::AlignCenter);
    m_imagePreview = new OcrImagePreviewWidget(m_imageScrollArea);
    m_imageScrollArea->setWidget(m_imagePreview);
    connect(zoom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, zoom](int) {
        m_imagePreview->setZoom(zoom->currentData().toInt() / 100.0);
    });
    leftLayout->addWidget(m_imageScrollArea, 1);

    m_splitter->addWidget(leftContainer);
    connect(m_showImageBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_imageVisibilityChosen = true;
        m_imagePanel->setVisible(checked);
    });
    leftContainer->setMinimumWidth(224);

    // ==========================================
    // 右侧面板：识别文字与翻译
    // ==========================================
    auto* rightContainer = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    auto* textSplitter = new QSplitter(Qt::Vertical, rightContainer);
    textSplitter->setObjectName("textSplitter");
    textSplitter->setHandleWidth(12);
    textSplitter->setChildrenCollapsible(false);
    rightLayout->addWidget(textSplitter, 1);
    m_sourcePanel = new QWidget(textSplitter);
    m_sourcePanel->setObjectName("resultPanel");
    m_sourcePanel->setProperty("surfaceRole", "raised");
    auto* sourceLayout = new QVBoxLayout(m_sourcePanel);
    sourceLayout->setContentsMargins(16, 16, 16, 16);
    sourceLayout->setSpacing(8);
    textSplitter->addWidget(m_sourcePanel);

    // 顶部 OCR 引擎控制与工具栏 (精简无冗余按钮)
    auto* rightHeaderLayout = new QHBoxLayout();
    rightHeaderLayout->setSpacing(8);

    auto* rightTitle = new QLabel("原文", m_sourcePanel);
    rightTitle->setObjectName("sectionTitle");

    rightHeaderLayout->addWidget(rightTitle);

    // OCR 引擎下拉框 (宽度拓宽至 180px，彻底避免右侧文字及箭头被遮挡)
    m_ocrEngineCombo = new ThemedComboBox(rightContainer);
    m_ocrEngineCombo->setFixedWidth(180);
    refreshEngineComboState();

    connect(&PluginManager::instance(), &PluginManager::pluginsRefreshed, this, &OcrResultDialog::refreshEngineComboState);
    connect(&PluginManager::instance(), &PluginManager::pluginStatusChanged, this, [this](const QString&, PluginStatus, const QString&) {
        refreshEngineComboState();
    });

    connect(m_ocrEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        QString selectedData = m_ocrEngineCombo->currentData().toString();
        if (m_image.isNull()) return;
        m_currentEngineType = selectedData;
        // Result-window selection does not change the saved default.

        // 如果当前截图此前已识别过该引擎，直接瞬间读取缓存（0毫秒秒切，消除重复识别！）
        if (m_engineResultCache.contains(selectedData)) {
            setResult(m_engineResultCache[selectedData], selectedData);
            return;
        }

        m_textEdit->setPlaceholderText("正在识别文字…");
        m_statusLabel->setText("正在识别文字…已有内容保留至识别完成");
        QPointer<OcrResultDialog> dialog(this);
        OcrManager::instance().recognizeWithEngine(selectedData, m_image.toImage(), m_image.devicePixelRatio(),
            [dialog, selectedData](const OcrResult& res, const QString& engineName) {
                if (dialog && dialog->m_currentEngineType == selectedData)
                    dialog->setResult(res, engineName.isEmpty() ? selectedData : engineName);
            });
    });

    rightHeaderLayout->addWidget(m_ocrEngineCombo);
    rightHeaderLayout->addStretch();
    m_copyBtn = new QPushButton("复制原文", m_sourcePanel);
    m_copyBtn->setObjectName("compactAction");
    m_copyBtn->setEnabled(false);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    rightHeaderLayout->addWidget(m_copyBtn);
    sourceLayout->addLayout(rightHeaderLayout);
    rightHeaderLayout = new QHBoxLayout();

    // 格式化按钮：段落合并与清除空格 (无冗余图标)
    m_mergeBtn = new QPushButton("合并段落", rightContainer);
    m_mergeBtn->setObjectName("compactAction");

    m_mergeBtn->setCursor(Qt::PointingHandCursor);
    m_mergeBtn->setToolTip("将断行拼接为完整自然段落");
    connect(m_mergeBtn, &QPushButton::clicked, this, [this]() {
        m_mergeParagraphs = true;
        m_removeExtraSpaces = false;
        updateFormattedText();
    });
    rightHeaderLayout->addWidget(m_mergeBtn);

    m_removeSpacesBtn = new QPushButton("清除空格", rightContainer);
    m_removeSpacesBtn->setObjectName("compactAction");

    m_removeSpacesBtn->setCursor(Qt::PointingHandCursor);
    m_removeSpacesBtn->setToolTip("消除汉字之间的多余空格");
    connect(m_removeSpacesBtn, &QPushButton::clicked, this, [this]() {
        m_mergeParagraphs = false;
        m_removeExtraSpaces = true;
        updateFormattedText();
    });
    rightHeaderLayout->addWidget(m_removeSpacesBtn);
    rightHeaderLayout->addStretch();
    sourceLayout->addLayout(rightHeaderLayout);

    // 提取文字输入框
    m_textEdit = new QPlainTextEdit(rightContainer);
    m_textEdit->setPlaceholderText("正在识别文字中，请稍候...");
    m_textEdit->setObjectName("sourceText");
    m_textEdit->setMinimumHeight(64);
    sourceLayout->addWidget(m_textEdit, 1);

    // 下部：嵌入式「译文对照」面板 (全下拉框设计 + 极简UI)
    setupTranslationPanel(rightLayout);
    rightLayout->removeWidget(m_translationSection);
    textSplitter->addWidget(m_translationSection);
    textSplitter->setSizes({260, 260});

    m_splitter->addWidget(rightContainer);

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    m_imagePercent = qBound(28, settings.value("ResultDialog/imagePercent", 40).toInt(), 52);
    m_splitter->setSizes({m_imagePercent * 10, (100 - m_imagePercent) * 10});
    m_splitter->setStretchFactor(0, 40);
    m_splitter->setStretchFactor(1, 60);
    m_splitter->handle(1)->setFocusPolicy(Qt::StrongFocus);
    m_splitter->handle(1)->setAccessibleName("原图分栏宽度");
    m_splitter->handle(1)->setToolTip("拖动调整原图宽度；左右方向键每次调整 1%");
    m_splitter->handle(1)->installEventFilter(this);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (!m_imagePanel->isVisible() || m_splitter->orientation() != Qt::Horizontal) return;
        const auto sizes = m_splitter->sizes();
        setImageColumnPercent(qRound(100.0 * sizes[0] / (sizes[0] + sizes[1])));
    });
    mainLayout->addWidget(m_splitter, 1);

    // ==========================================
    // 底部工具与状态栏
    // ==========================================
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);

    m_statusLabel = new QLabel("字数: 0  |  行数: 0", this);
    m_statusLabel->setObjectName("secondaryText");

    bottomLayout->addWidget(m_statusLabel);

    m_transElapsedLabel = new QLabel(this);
    m_transElapsedLabel->setObjectName("secondaryText");
    m_transElapsedLabel->setWordWrap(true);

    bottomLayout->addWidget(m_transElapsedLabel);

    bottomLayout->addStretch();

    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_textEdit->toPlainText();
        if (text.trimmed().isEmpty()) return;
        QApplication::clipboard()->setText(text);
        m_copyBtn->setText("已复制");
        QTimer::singleShot(1600, this, [this]() {
            if (m_copyBtn) m_copyBtn->setText("复制原文");
        });
    });
    m_closeBtn = new QPushButton("关闭", this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        qInfo() << "[ResultWindow] close button dialog=" << quintptr(this);
        accept();
    });
    bottomLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(bottomLayout);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this]() {
        const auto text = m_textEdit->toPlainText();
        m_copyBtn->setEnabled(!text.trimmed().isEmpty());
        m_statusLabel->setText(QString("%1 字 · %2 行").arg(text.size()).arg(text.isEmpty() ? 0 : m_textEdit->blockCount()));
        if (text != m_translationSourceText && (m_isTranslating || !m_translationSpans.isEmpty()))
            invalidateTranslation(QStringLiteral("原文已修改，请重新翻译"));
    });
    connect(m_textEdit->document(), &QTextDocument::contentsChanged,
            this, &OcrResultDialog::updateTextLineMap);
    connect(m_textEdit, &QPlainTextEdit::selectionChanged, this, &OcrResultDialog::syncImageSelection);
    connect(m_textEdit, &QPlainTextEdit::cursorPositionChanged, this, &OcrResultDialog::syncImageSelection);
    connect(m_imagePreview, &OcrImagePreviewWidget::linesSelected, this, &OcrResultDialog::selectTextLines);
    m_textEdit->viewport()->installEventFilter(this);
    m_translationEdit->viewport()->installEventFilter(this);
    for (auto* button : findChildren<QPushButton*>()) button->setAutoDefault(false);
    m_textViewGroup->button(0)->setChecked(true);
}

void OcrResultDialog::updateResultLayout()
{
    const bool narrow = width() < 800;
    const auto orientation = narrow ? Qt::Vertical : Qt::Horizontal;
    if (m_splitter->orientation() != orientation) {
        m_imagePanel->setMinimumWidth(narrow ? 0 : 224);
        m_splitter->setOrientation(orientation);
        m_splitter->setSizes(narrow ? QList<int>{160, 480}
                                    : QList<int>{m_imagePercent * 10, (100 - m_imagePercent) * 10});
    }
    if (!m_imageVisibilityChosen) {
        const QSignalBlocker blocker(m_showImageBtn);
        m_showImageBtn->setChecked(true);
        m_imagePanel->setVisible(true);
    }
    const int mode = m_textViewGroup->checkedId();
    m_sourcePanel->setVisible(mode != 2);
    m_translationSection->setVisible(mode != 1);
    m_textViewGroup->button(0)->setToolTip("同时显示原文与译文");
}

void OcrResultDialog::setImageColumnPercent(int percent)
{
    m_imagePercent = qBound(28, percent, 52);
    const auto sizes = m_splitter->sizes();
    const int total = sizes[0] + sizes[1];
    const int imageWidth = qRound(total * m_imagePercent / 100.0);
    m_splitter->setSizes({imageWidth, total - imageWidth});
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    settings.setValue("ResultDialog/imagePercent", m_imagePercent);
}

void OcrResultDialog::initializeTextLineMap()
{
    const auto text = m_textEdit->toPlainText();
    m_textLineIds.fill(-1, text.size());
    QStringList sourceLines;
    for (const auto& line : m_result.lines) sourceLines.append(line.text);
    const bool exactText = text == sourceLines.join('\n');
    const auto displayedLines = text.split('\n');
    int start = 0;
    for (int i = 0; i < m_result.lines.size(); ++i) {
        const auto& line = m_result.lines[i].text;
        // Never search ahead for a duplicate: a missing/mismatched row must not steal a later row's text.
        if (exactText || (displayedLines.size() == sourceLines.size() && displayedLines[i] == line))
            std::fill(m_textLineIds.begin() + start, m_textLineIds.begin() + start + line.size(), i);
        if (exactText) start += line.size() + 1;
        else if (displayedLines.size() == sourceLines.size()) start += displayedLines[i].size() + 1;
    }
    applyTextLineMap();
    m_textEdit->document()->clearUndoRedoStacks();
    syncImageSelection();
}

void OcrResultDialog::applyTextLineMap()
{
    m_updatingTextMap = true;
    QTextCursor cursor(m_textEdit->document());
    for (int start = 0; start < m_textLineIds.size();) {
        int end = start + 1;
        while (end < m_textLineIds.size() && m_textLineIds[end] == m_textLineIds[start]) ++end;
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setProperty(OcrLineProperty, m_textLineIds[start]);
        cursor.mergeCharFormat(format);
        start = end;
    }
    m_updatingTextMap = false;
}

void OcrResultDialog::updateTextLineMap()
{
    if (m_updatingTextMap) return;
    // Character properties follow Qt's edits and native undo/redo, including restored deleted lines.
    m_textLineIds.fill(-1, m_textEdit->toPlainText().size());
    for (auto block = m_textEdit->document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            if (!format.hasProperty(OcrLineProperty)) continue;
            std::fill(m_textLineIds.begin() + fragment.position(),
                      m_textLineIds.begin() + fragment.position() + fragment.length(),
                      format.intProperty(OcrLineProperty));
        }
    }
    syncImageSelection();
}

void OcrResultDialog::syncImageSelection()
{
    if (m_updatingTextMap || m_syncingSelection) return;
    const auto cursor = m_textEdit->textCursor();
    // setPlainText emits contentsChanged before QPlainTextEdit recreates its cursor (position -1).
    // The later cursorPositionChanged signal will synchronize the replacement document.
    if (cursor.isNull()) {
        m_imagePreview->setSelectedLines({});
        return;
    }
    QList<int> selected;
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (start == end) {
        if (start < m_textLineIds.size() && m_textLineIds[start] >= 0) end = start + 1;
        else if (start > 0) --start;
    }
    for (int i = start; i < end && i < m_textLineIds.size(); ++i) {
        const int id = m_textLineIds[i];
        if (id >= 0 && !selected.contains(id)) selected.append(id);
    }
    m_imagePreview->setSelectedLines(selected);
    if (!selected.isEmpty()) {
        const auto box = m_imagePreview->lineRect(selected.front());
        m_imageScrollArea->ensureVisible(qRound(box.center().x()), qRound(box.center().y()),
                                        qRound(box.width() / 2) + 12, qRound(box.height() / 2) + 12);
    }
    syncTranslationFromSource();
}

void OcrResultDialog::selectTextLines(const QList<int>& lines)
{
    int start = -1;
    int end = -1;
    for (int i = 0; i < m_textLineIds.size(); ++i) {
        if (m_textLineIds[i] >= 0 && lines.contains(m_textLineIds[i])) {
            if (start < 0) start = i;
            end = i + 1;
        }
    }
    auto cursor = m_textEdit->textCursor();
    m_syncingSelection = true;
    if (start >= 0) {
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        QList<int> mappedLines;
        for (int i = start; i < end; ++i) {
            const int id = m_textLineIds[i];
            if (id >= 0 && !mappedLines.contains(id)) mappedLines.append(id);
        }
        m_imagePreview->setSelectedLines(mappedLines);
    } else {
        cursor.clearSelection();
        m_imagePreview->setSelectedLines({});
    }
    m_textEdit->setTextCursor(cursor);
    m_textEdit->ensureCursorVisible();
    m_textEdit->setFocus(Qt::MouseFocusReason);
    m_syncingSelection = false;
    if (start >= 0) syncTranslationFromSource();
    else {
        m_textEdit->setExtraSelections({});
        m_translationEdit->setExtraSelections({});
    }
}

void OcrResultDialog::refreshEngineComboState()
{
    if (!m_ocrEngineCombo) return;
    QSignalBlocker blocker(m_ocrEngineCombo);
    m_ocrEngineCombo->clear();

    bool rapidOk = OcrManager::instance().isRapidOcrReady();
    if (rapidOk) {
        m_ocrEngineCombo->addItem("RapidOCR (离线)", "RapidOCR");
    } else {
        m_ocrEngineCombo->addItem("RapidOCR (未安装)", "RapidOCR");
        setPluginChoiceEnabled(m_ocrEngineCombo,"RapidOCR",false);
    }
    m_ocrEngineCombo->addItem("Windows 原生", "WindowsMedia");
    for(const auto& p:AiService::profiles())if(p.vision&&!AiService::isLocalProfile(p))m_ocrEngineCombo->addItem(p.name+" · 在线","ai:"+p.id);

    int curIdx = m_ocrEngineCombo->findData(m_currentEngineType);
    if (curIdx >= 0) {
        m_ocrEngineCombo->setCurrentIndex(curIdx);
    } else {
        m_ocrEngineCombo->setCurrentIndex(rapidOk ? 0 : 1);
    }
}

void OcrResultDialog::setupTranslationPanel(QVBoxLayout* rightLayout)
{
    m_translationSection = new QWidget(this);
    m_translationSection->setObjectName("transSection");
    m_translationSection->setProperty("surfaceRole", "well");


    auto* transLayout = new QVBoxLayout(m_translationSection);
    transLayout->setContentsMargins(16, 16, 16, 16);
    transLayout->setSpacing(8);

    // 翻译顶部控制栏
    auto* barLayout = new QHBoxLayout();
    barLayout->setSpacing(8);

    auto* transTitle = new QLabel("译文", m_translationSection);
    transTitle->setObjectName("sectionTitle");

    barLayout->addWidget(transTitle);

    // 翻译模式下拉框 (在线直连 vs 离线插件)
    m_transModeCombo = new ThemedComboBox(m_translationSection);
    m_transModeCombo->setMinimumWidth(160);
    m_transModeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_transModeCombo->addItem("在线", "Online");
    m_transModeCombo->addItem("Argos 中英离线", "Offline");
    setPluginChoiceEnabled(m_transModeCombo,"Offline",TranslationPluginManager::instance().isOfflinePluginReady());
    connect(&PluginManager::instance(),&PluginManager::pluginsRefreshed,this,[this]{setPluginChoiceEnabled(m_transModeCombo,"Offline",TranslationPluginManager::instance().isOfflinePluginReady());});

    for(const auto& p:AiService::profiles())if(p.protocol!="mathpix"&&!AiService::isLocalProfile(p))m_transModeCombo->addItem(p.name,"ai:"+p.id);
    m_transModeCombo->setCurrentIndex(qMax(0,m_transModeCombo->findData(TranslationPluginManager::instance().preferredEnginePreference())));
    // 状态胶囊标签
    m_transStatusBadge = new QLabel(m_translationSection);
    m_transStatusBadge->setObjectName("secondaryText");
    auto updateTransBadge = [this]() {
        bool offlineMode = (m_transModeCombo->currentData().toString() == "Offline");
        bool isReady = offlineMode && TranslationPluginManager::instance().isOfflinePluginReady();

        if (offlineMode) {
            if (isReady) {
                m_transStatusBadge->setText("本地就绪");

                if (m_translationEdit && m_translationEdit->toPlainText().isEmpty()) {
                    m_translationEdit->setPlaceholderText("点击上方【翻译】获取译文...");
                }
            } else {
                m_transStatusBadge->setText("未安装插件");

                if (m_translationEdit && m_translationEdit->toPlainText().isEmpty()) {
                    m_translationEdit->setPlaceholderText("未检测到离线神经翻译插件，请前往设置【插件与模型】安装。");
                }
            }
        } else {
            m_transStatusBadge->setText("在线服务");

            if (m_translationEdit && m_translationEdit->toPlainText().isEmpty()) {
                m_translationEdit->setPlaceholderText("点击上方【翻译】获取译文...");
            }
        }
    };
    updateTransBadge();

    connect(m_transModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, updateTransBadge](int) {
        updateTransBadge();
        triggerTranslation();
    });

    connect(&TranslationPluginManager::instance(), &TranslationPluginManager::engineChanged, this, [updateTransBadge](const QString&, bool) {
        updateTransBadge();
    });

    barLayout->addStretch();
    barLayout->addWidget(m_transStatusBadge);
    transLayout->addLayout(barLayout);
    barLayout = new QHBoxLayout();
    barLayout->setSpacing(8);
    barLayout->addWidget(m_transModeCombo, 1);
    auto* setTranslationDefault=new QPushButton("设为默认",m_translationSection);setTranslationDefault->setToolTip("将当前翻译服务用于后续结果窗口");
    connect(setTranslationDefault,&QPushButton::clicked,this,[this]{TranslationPluginManager::instance().setPreferredEnginePreference(m_transModeCombo->currentData().toString());});barLayout->addWidget(setTranslationDefault);
    transLayout->addLayout(barLayout);
    barLayout = new QHBoxLayout();
    barLayout->setSpacing(8);

    // 语言对选择下拉框 (支持双向互译，选择后自动触发翻译)
    m_langPairCombo = new ThemedComboBox(m_translationSection);
    m_langPairCombo->addItem("中文 → 英语", "zh:en");
    m_langPairCombo->addItem("英语 → 中文", "en:zh");
    m_langPairCombo->addItem("自动 → 中文", "auto:zh");
    m_langPairCombo->addItem("自动 → 英语", "auto:en");
    m_langPairCombo->addItem("中文 → 日语", "zh:ja");
    m_langPairCombo->addItem("日语 → 中文", "ja:zh");
    m_langPairCombo->addItem("中文 → 韩语", "zh:ko");
    m_langPairCombo->addItem("韩语 → 中文", "ko:zh");
    m_langPairCombo->addItem("中文 → 俄语", "zh:ru");
    m_langPairCombo->addItem("俄语 → 中文", "ru:zh");
    m_langPairCombo->addItem("中文 → 法语", "zh:fr");
    m_langPairCombo->addItem("法语 → 中文", "fr:zh");
    m_langPairCombo->addItem("中文 → 德语", "zh:de");
    m_langPairCombo->addItem("德语 → 中文", "de:zh");
    m_langPairCombo->setMinimumWidth(144);
    m_langPairCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(m_langPairCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        triggerTranslation();
    });

    barLayout->addWidget(m_langPairCombo, 1);

    // 翻译按钮
    m_translateBtn = new QPushButton("翻译", m_translationSection);
    m_translateBtn->setObjectName("primaryAction");
    m_translateBtn->setCursor(Qt::PointingHandCursor);

    connect(m_translateBtn, &QPushButton::clicked, this, &OcrResultDialog::triggerTranslation);
    barLayout->addWidget(m_translateBtn);

    // 复制译文按钮
    m_copyTransBtn = new QPushButton("复制译文", m_translationSection);
    m_copyTransBtn->setObjectName("compactAction");
    m_copyTransBtn->setEnabled(false);
    m_copyTransBtn->setCursor(Qt::PointingHandCursor);
    m_copyTransBtn->setToolTip("复制当前翻译结果");
    connect(m_copyTransBtn, &QPushButton::clicked, this, [this]() {
        if (!m_translationEdit) return;
        QString text = m_translationEdit->toPlainText().trimmed();
        if (text.isEmpty()) return;
        QApplication::clipboard()->setText(text);
        m_copyTransBtn->setText("已复制");
        QTimer::singleShot(1600, this, [this]() {
            if (m_copyTransBtn) m_copyTransBtn->setText("复制译文");
        });
    });
    barLayout->addWidget(m_copyTransBtn);

    transLayout->addLayout(barLayout);

    // 译文展示编辑框
    m_translationEdit = new QPlainTextEdit(m_translationSection);
    m_translationEdit->setPlaceholderText("点击上方【翻译】获取译文...");
    m_translationEdit->setMinimumHeight(64);
    m_translationEdit->setObjectName("translationText");
    m_translationEdit->setReadOnly(false);
    connect(m_translationEdit, &QPlainTextEdit::textChanged, this, [this] {
        m_copyTransBtn->setEnabled(!m_translationEdit->toPlainText().trimmed().isEmpty());
        if (!m_isTranslating && !m_translationSpans.isEmpty())
            invalidateTranslation(QStringLiteral("译文已修改，段落关联已失效，请重新翻译"));
    });
    connect(m_translationEdit, &QPlainTextEdit::selectionChanged, this, &OcrResultDialog::syncSourceFromTranslation);
    connect(m_translationEdit, &QPlainTextEdit::cursorPositionChanged, this, &OcrResultDialog::syncSourceFromTranslation);

    transLayout->addWidget(m_translationEdit, 1);

    rightLayout->addWidget(m_translationSection);
}

void OcrResultDialog::setImage(const QPixmap& pixmap)
{
    invalidateTranslation();
    m_image = pixmap;
    m_engineResultCache.clear(); // 截图换新时，清空旧图缓存
    if (m_imagePreview) {
        m_imagePreview->setImage(pixmap);
    }
}

void OcrResultDialog::setResult(const OcrResult& result, const QString& engineName)
{
    if (!result.success) {
        m_copyBtn->setEnabled(!m_textEdit->toPlainText().trimmed().isEmpty());
        m_textEdit->setPlaceholderText(QString("识别失败：%1。可切换引擎重试。").arg(result.errorMessage));
        m_statusLabel->setText(QString("识别失败：%1。已有内容已保留，可切换引擎重试。").arg(result.errorMessage));
        return;
    }
    invalidateTranslation();
    m_updatingTextMap = true;
    m_textLineIds.clear();
    QString curEngine = engineName.isEmpty() ? m_currentEngineType : engineName;
    if (curEngine == "Windows.Media.Ocr") curEngine = "WindowsMedia";
    else if (curEngine.startsWith("RapidOCR")) curEngine = "RapidOCR";
    else if(!curEngine.startsWith("ai:"))for(const auto& p:AiService::profiles())if(p.name==curEngine){curEngine="ai:"+p.id;break;}
    m_currentEngineType = curEngine;
    const int index = m_ocrEngineCombo->findData(curEngine);
    if (index >= 0) {
        QSignalBlocker blocker(m_ocrEngineCombo);
        m_ocrEngineCombo->setCurrentIndex(index);
    }
    m_result = result;
    m_translationEdit->clear();
    m_copyTransBtn->setEnabled(false);
    m_transElapsedLabel->clear();
    if (result.success) {
        m_engineResultCache[curEngine] = result;
    }
    if (m_imagePreview) {
        m_imagePreview->setResult(result);
    }

    m_textEdit->setPlainText(result.fullText);
    if (result.fullText.isEmpty()) {
        QStringList lines;
        for (const auto& line : result.lines) lines << line.text;
        m_textEdit->setPlainText(lines.join('\n'));
    }
    m_updatingTextMap = false;
    initializeTextLineMap();
    autoDetectSourceLanguage();
}

void OcrResultDialog::updateFormattedText()
{
    const auto oldText = m_textEdit->toPlainText();
    QString formatted = processText(m_mergeParagraphs, m_removeExtraSpaces);
    // Both formatting actions only change whitespace. Carry each surviving character's source line.
    QList<int> formattedIds(formatted.size(), -1);
    int source = 0;
    for (int i = 0; i < formatted.size(); ++i) {
        if (formatted[i].isSpace()) continue;
        while (source < oldText.size() && oldText[source].isSpace()) ++source;
        if (source < oldText.size()) formattedIds[i] = m_textLineIds[source++];
    }
    m_updatingTextMap = true;
    auto cursor = m_textEdit->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(formatted);
    m_textLineIds = formattedIds;
    applyTextLineMap();
    cursor.endEditBlock();
    m_updatingTextMap = false;
    syncImageSelection();

    int charCount = formatted.length();
    int lineCount = formatted.isEmpty() ? 0 : formatted.split('\n').size();
    m_statusLabel->setText(QString("字数: %1  |  行数: %2").arg(charCount).arg(lineCount));

}

QString OcrResultDialog::processText(bool mergeParagraphs, bool removeExtraSpaces)
{
    QStringList rawLines = m_textEdit->toPlainText().split('\n');

    if (mergeParagraphs) {
        QString merged;
        for (const auto& line : rawLines) {
            if (line.isEmpty()) continue;
            if (merged.isEmpty()) {
                merged = line;
            } else {
                QChar lastChar = merged.back();
                if (lastChar == '.' || lastChar == '?' || lastChar == '!' ||
                    lastChar == QChar(0x3002) /* 。 */ || lastChar == QChar(0xFF01) /* ！ */ || lastChar == QChar(0xFF1F) /* ？ */) {
                    merged += "\n" + line;
                } else {
                    bool isChinese = (lastChar.unicode() >= 0x4E00 && lastChar.unicode() <= 0x9FA5);
                    QChar firstChar = line.front();
                    bool nextIsChinese = (firstChar.unicode() >= 0x4E00 && firstChar.unicode() <= 0x9FA5);

                    if (isChinese && nextIsChinese) {
                        merged += line;
                    } else {
                        merged += " " + line;
                    }
                }
            }
        }
        rawLines = merged.split('\n');
    }

    QString resultText = rawLines.join('\n');

    if (removeExtraSpaces) {
        static QRegularExpression zhSpaceZh("([\\x{4e00}-\\x{9fa5}])[ \t]+(?=[\\x{4e00}-\\x{9fa5}])");
        resultText.replace(zhSpaceZh, "\\1");
        static QRegularExpression multiSpaces("[ \\t]{2,}");
        resultText.replace(multiSpaces, " ");
    }

    return resultText;
}

void OcrResultDialog::syncTranslationFromSource()
{
    if (m_syncingSelection || m_translationSpans.isEmpty()) return;
    const auto cursor = m_textEdit->textCursor();
    if (cursor.isNull()) return;
    QList<int> selected;
    for (int i = 0; i < m_translationSpans.size(); ++i) {
        const auto& span = m_translationSpans[i];
        if (span.targetStart >= 0 && intersectsTextRange(cursor, span.sourceStart, span.sourceEnd))
            selected.append(i);
    }
    highlightTranslationSpans(selected, false);
}

void OcrResultDialog::syncSourceFromTranslation()
{
    if (m_syncingSelection || m_translationSpans.isEmpty()) return;
    const auto cursor = m_translationEdit->textCursor();
    if (cursor.isNull()) return;
    QList<int> selected;
    for (int i = 0; i < m_translationSpans.size(); ++i) {
        const auto& span = m_translationSpans[i];
        if (span.targetStart >= 0 && intersectsTextRange(cursor, span.targetStart, span.targetEnd))
            selected.append(i);
    }
    highlightTranslationSpans(selected, true);
}

void OcrResultDialog::highlightTranslationSpans(const QList<int>& spans, bool fromTranslation)
{
    QList<QTextEdit::ExtraSelection> sourceSelections;
    QList<QTextEdit::ExtraSelection> targetSelections;
    QList<int> lines;
    for (int index : spans) {
        const auto& span = m_translationSpans[index];
        QTextEdit::ExtraSelection source;
        source.cursor = QTextCursor(m_textEdit->document());
        source.cursor.setPosition(span.sourceStart);
        source.cursor.setPosition(span.sourceEnd, QTextCursor::KeepAnchor);
        source.format.setBackground(ThemeManager::instance().palette().selected);
        sourceSelections.append(source);
        QTextEdit::ExtraSelection target;
        target.cursor = QTextCursor(m_translationEdit->document());
        target.cursor.setPosition(span.targetStart);
        target.cursor.setPosition(span.targetEnd, QTextCursor::KeepAnchor);
        target.format = source.format;
        targetSelections.append(target);
        for (int line : span.lineIds) if (!lines.contains(line)) lines.append(line);
    }
    m_textEdit->setExtraSelections(sourceSelections);
    m_translationEdit->setExtraSelections(targetSelections);
    if (fromTranslation || !spans.isEmpty()) {
        m_imagePreview->setSelectedLines(lines);
        if (fromTranslation && !lines.isEmpty()) {
            const auto box = m_imagePreview->lineRect(lines.front());
            m_imageScrollArea->ensureVisible(qRound(box.center().x()), qRound(box.center().y()),
                                            qRound(box.width() / 2) + 12, qRound(box.height() / 2) + 12);
        }
    }
    if (!spans.isEmpty()) {
        const auto& first = m_translationSpans[spans.front()];
        scrollToText(fromTranslation ? m_textEdit : m_translationEdit,
                     fromTranslation ? first.sourceStart : first.targetStart);
    }
}

void OcrResultDialog::setTranslating(bool translating)
{
    m_isTranslating = translating;
    m_langPairCombo->setEnabled(!translating);
    m_transModeCombo->setEnabled(!translating);
    m_textEdit->setReadOnly(translating);
    m_translationEdit->setReadOnly(translating);
    if (translating) {
        m_transElapsedLabel->setProperty("warning", false);
        m_transElapsedLabel->style()->unpolish(m_transElapsedLabel);
        m_transElapsedLabel->style()->polish(m_transElapsedLabel);
    }
    m_mergeBtn->setEnabled(!translating);
    m_removeSpacesBtn->setEnabled(!translating);
    m_ocrEngineCombo->setEnabled(!translating);
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(translating ? QStringLiteral("取消翻译") : QStringLiteral("翻译"));
    m_translateBtn->setIcon(uiIcon(translating ? "close" : "translate", ThemeManager::instance().palette().onAction));
}

void OcrResultDialog::invalidateTranslation(const QString& message)
{
    ++m_translationGeneration;
    const auto reply = m_aiReply;
    m_aiReply.clear();
    if (m_isTranslating) {
        setTranslating(false);
        const QSignalBlocker blocker(m_translationEdit);
        m_translationEdit->setPlainText(m_translationBeforeRequest);
        m_copyTransBtn->setEnabled(!m_translationBeforeRequest.trimmed().isEmpty());
    }
    m_translationSpans.clear();
    m_textEdit->setExtraSelections({});
    m_translationEdit->setExtraSelections({});
    m_imagePreview->setSelectedLines({});
    if (reply) reply->abort();
    if (!message.isEmpty()) {
        m_transElapsedLabel->setText(message);
        m_transElapsedLabel->setProperty("warning", true);
        m_transElapsedLabel->style()->unpolish(m_transElapsedLabel);
        m_transElapsedLabel->style()->polish(m_transElapsedLabel);
    }
}

void OcrResultDialog::triggerTranslation()
{
    if (m_isTranslating) {
        invalidateTranslation(QStringLiteral("翻译已取消，请重新翻译"));
        return;
    }
    invalidateTranslation();
    m_translationBeforeRequest = m_translationEdit->toPlainText();
    m_translationSourceText = m_textEdit->toPlainText();
    for (auto block = m_textEdit->document()->begin(); block.isValid(); block = block.next()) {
        if (block.text().trimmed().isEmpty()) continue;
        TranslationSpan span;
        span.sourceText = block.text();
        span.sourceStart = block.position();
        span.sourceEnd = block.position() + block.text().size();
        for (int i = span.sourceStart; i < span.sourceEnd; ++i) {
            const int line = m_textLineIds[i];
            if (line >= 0 && !span.lineIds.contains(line)) span.lineIds.append(line);
        }
        m_translationSpans.append(span);
    }
    if (m_translationSpans.isEmpty()) {
        m_translationEdit->setPlaceholderText(QStringLiteral("请输入或识别文字后再翻译"));
        return;
    }
    const auto languages = m_langPairCombo->currentData().toString().split(':');
    m_translationSourceLang = languages.value(0, "auto");
    m_translationTargetLang = languages.value(1, "zh");
    m_translationMode = m_transModeCombo->currentData().toString();
    m_nextTranslationParagraph = 0;
    m_translationElapsedMs = 0;
    m_transElapsedLabel->setText(QStringLiteral("正在翻译 1 / %1 段").arg(m_translationSpans.size()));
    m_translationEdit->setPlaceholderText(QStringLiteral("正在按原文段落翻译…"));
    setTranslating(true);
    qInfo() << "[TranslationUI] begin dialog=" << quintptr(this) << "mode=" << m_translationMode
            << "paragraphs=" << m_translationSpans.size() << "chars=" << m_translationSourceText.size();
    translateNextParagraph(m_translationGeneration);
}

void OcrResultDialog::translateNextParagraph(quint64 generation)
{
    if (generation != m_translationGeneration || !m_isTranslating) return;
    const int index = m_nextTranslationParagraph;
    QPointer<OcrResultDialog> dialog(this);
    auto callback = [dialog, generation, index](const TranslationResult& result) {
        if (!dialog || generation != dialog->m_translationGeneration || !dialog->m_isTranslating) return;
        auto* self = dialog.data();
        self->m_aiReply.clear();
        qInfo() << "[TranslationUI] paragraph result dialog=" << quintptr(self) << "index=" << index
                << "success=" << result.success << "chars=" << result.translatedText.size();
        if (!result.success || result.translatedText.trimmed().isEmpty()) {
            const QString reason = result.success ? QStringLiteral("服务返回空文本") : result.errorMessage;
            self->invalidateTranslation(QStringLiteral("翻译失败：%1。已有译文已保留，请重试或切换服务。").arg(reason));
            self->m_translationEdit->setPlaceholderText(QStringLiteral("翻译失败：%1。请重试或切换服务。")
                .arg(result.success ? QStringLiteral("服务返回空文本") : result.errorMessage));
            return;
        }
        {
            QScopedValueRollback<bool> syncing(self->m_syncingSelection, true);
            auto cursor = QTextCursor(self->m_translationEdit->document());
            if (index == 0) {
                cursor.select(QTextCursor::Document);
                cursor.removeSelectedText();
            }
            cursor.movePosition(QTextCursor::End);
            if (index > 0) cursor.insertText("\n\n");
            auto& span = self->m_translationSpans[index];
            span.targetStart = cursor.position();
            QString translated = result.translatedText;
            translated.replace("\r\n", "\n");
            translated.replace('\r', '\n');
            cursor.insertText(translated);
            span.targetEnd = cursor.position();
        }
        self->m_translationElapsedMs += result.elapsedMs;
        self->m_copyTransBtn->setEnabled(true);
        ++self->m_nextTranslationParagraph;
        self->m_transStatusBadge->setText(result.engineType == TranslationEngineType::OfflinePlugin
                                         ? QStringLiteral("本地就绪") : QStringLiteral("在线服务"));
        if (self->m_translationEdit->hasFocus()) self->syncSourceFromTranslation();
        else self->syncTranslationFromSource();
        if (self->m_nextTranslationParagraph == self->m_translationSpans.size()) {
            self->setTranslating(false);
            self->m_transElapsedLabel->setText(QStringLiteral("翻译用时: %1ms · %2 段")
                .arg(self->m_translationElapsedMs).arg(self->m_translationSpans.size()));
            return;
        }
        self->m_transElapsedLabel->setText(QStringLiteral("正在翻译 %1 / %2 段")
            .arg(self->m_nextTranslationParagraph + 1).arg(self->m_translationSpans.size()));
        QTimer::singleShot(0, self, [dialog, generation] {
            if (dialog) dialog->translateNextParagraph(generation);
        });
    };
    auto* reply = m_translationRequest(m_translationSpans[index].sourceText, m_translationSourceLang,
                                       m_translationTargetLang, std::move(callback), m_translationMode);
    // A local provider or injected provider may complete synchronously.
    if (generation == m_translationGeneration && m_isTranslating && m_nextTranslationParagraph == index)
        m_aiReply = reply;
    if (reply) connect(this, &QObject::destroyed, reply, &QNetworkReply::abort);
}

void OcrResultDialog::autoDetectSourceLanguage()
{
    if (!m_textEdit || !m_langPairCombo) return;
    QString sample = m_textEdit->toPlainText();
    QString detected = TranslationPluginManager::detectLanguageHeuristic(sample);

    int targetIdx = -1;
    if (detected == "zh") {
        targetIdx = m_langPairCombo->findData("zh:en");
    } else if (detected == "en") {
        targetIdx = m_langPairCombo->findData("en:zh");
    } else if (detected == "ja") {
        targetIdx = m_langPairCombo->findData("ja:zh");
    } else if (detected == "ko") {
        targetIdx = m_langPairCombo->findData("ko:zh");
    } else {
        targetIdx = m_langPairCombo->findData("auto:zh");
    }

    if (targetIdx >= 0 && m_langPairCombo->currentIndex() != targetIdx) {
        QSignalBlocker blocker(m_langPairCombo);
        m_langPairCombo->setCurrentIndex(targetIdx);
    }
    if (m_translationRequested) {
        triggerTranslation();
    }
}
