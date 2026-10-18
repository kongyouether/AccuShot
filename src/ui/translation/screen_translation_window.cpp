#include "screen_translation_window.h"
#include "../themed_combo_box.h"
#include "../toolbar/toolbar_icons.h"
#include "../../core/ai/ai_service.h"
#include "../../core/ocr/ocr_manager.h"
#include "../../core/plugin/plugin_manager.h"
#include "../../core/translation/translation_plugin_manager.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QList<QTextLine> layoutLines(QTextLayout& layout, qreal width) {
    QList<QTextLine> lines;
    layout.beginLayout();
    qreal y = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(qMax(1.0, width));
        line.setPosition(QPointF(0, y));
        y += line.height();
        lines.append(line);
    }
    layout.endLayout();
    return lines;
}

ScreenTranslationWindow::Hooks productionHooks() {
    ScreenTranslationWindow::Hooks hooks;
    hooks.recognize = [](const QImage& image, qreal dpr, const QString& engine, auto callback) {
        OcrManager::instance().recognizeWithEngine(engine, image, dpr,
            [callback](const OcrResult& result, const QString&) { callback(result); });
        // Existing OCR workers have no interrupt API; closing invalidates their
        // callback and prevents the subsequent translation request.
        return ScreenTranslationWindow::Cancel{};
    };
    hooks.translate = [](const QString& text, const QString& source, const QString& target,
                         const QString& service, auto callback) {
        QPointer<QNetworkReply> reply = TranslationPluginManager::instance().translateAsync(
            text, source, target, callback, service);
        return ScreenTranslationWindow::Cancel([reply] { if (reply) reply->abort(); });
    };
    return hooks;
}
}

ScreenTranslationRender ScreenTranslationWindow::renderTranslatedImage(
    const QPixmap& source, const QList<ScreenTranslationLine>& lines) {
    ScreenTranslationRender result;
    result.image = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (result.image.isNull()) return result;
    QPainter painter(&result.image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF canvas(QPointF(0, 0), source.deviceIndependentSize());
    for (int index = 0; index < lines.size(); ++index) {
        const auto& item = lines[index];
        if (item.state == ScreenTranslationLine::Pending) continue;
        const QRectF box = item.box.intersected(canvas);
        if (box.width() < 3 || box.height() < 3) { result.overflowLines.append(index); continue; }
        painter.save();
        painter.setClipRect(box);
        const bool failed = item.state == ScreenTranslationLine::Failed;
        painter.fillRect(box, failed ? QColor("#fff0ed") : QColor("#fffef9"));
        painter.setPen(failed ? QColor("#b42318") : QColor("#17212c"));
        QString text = failed ? QStringLiteral("翻译失败 · 查看全文") : item.translation;
        text.replace('\n', QChar::LineSeparator);
        const QRectF content = box.adjusted(2, 0, -2, 0);
        QFont font("Microsoft YaHei UI");
        int pixels = qBound(11, qRound(box.height() * 0.72), 28);
        bool fits = false;
        for (; pixels >= 11; --pixels) {
            font.setPixelSize(pixels);
            QTextLayout layout(text, font);
            QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(option);
            const auto wrapped = layoutLines(layout, content.width());
            const qreal height = wrapped.isEmpty() ? 0 : wrapped.last().y() + wrapped.last().height();
            if (height <= content.height()) { fits = true; break; }
        }
        font.setPixelSize(qMax(11, pixels));
        painter.setFont(font);
        QTextLayout layout(text, font);
        QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        const auto wrapped = layoutLines(layout, content.width());
        if (fits) {
            const qreal height = wrapped.isEmpty() ? 0 : wrapped.last().y() + wrapped.last().height();
            const QPointF origin = content.topLeft() + QPointF(0, (content.height() - height) / 2);
            for (const auto& line : wrapped) line.draw(&painter, origin);
        } else {
            result.overflowLines.append(index);
            int count = 0;
            for (const auto& line : wrapped) {
                if (line.y() + line.height() > content.height() && count > 0) break;
                ++count;
            }
            for (int i = 0; i < count; ++i) {
                const auto& line = wrapped[i];
                if (i == count - 1) {
                    const QString tail = text.mid(line.textStart());
                    painter.drawText(content.topLeft() + QPointF(0, line.y() + line.ascent()),
                        QFontMetricsF(font).elidedText(tail, Qt::ElideRight, content.width()));
                } else line.draw(&painter, content.topLeft());
            }
            // A visible edge marks the excerpt even when an extremely small box
            // cannot hold the ellipsis. Full text is never discarded.
            painter.fillRect(QRectF(box.right() - 3, box.top(), 3, box.height()), QColor("#d38b18"));
        }
        painter.restore();
    }
    return result;
}

ScreenTranslationWindow::ScreenTranslationWindow(QPixmap image, QRect screenRect, QWidget* parent)
    : ScreenTranslationWindow(std::move(image), screenRect, productionHooks(), parent) {}

ScreenTranslationWindow::ScreenTranslationWindow(QPixmap image, QRect screenRect, Hooks hooks, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_original(std::move(image)), m_screenRect(screenRect), m_hooks(std::move(hooks)) {
    setObjectName("screenTranslationWindow");
    setWindowTitle("原位屏幕翻译");
    setWindowIcon(appIcon());
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(screenRect);
    setFixedSize(screenRect.size());
    setupControls();
    qApp->installEventFilter(this);
    rebuildImage();
}

ScreenTranslationWindow::~ScreenTranslationWindow() {
    stopCurrentRequest();
    qApp->removeEventFilter(this);
}

void ScreenTranslationWindow::setupControls() {
    m_controls = new QDialog(this, Qt::Tool | Qt::WindowStaysOnTopHint);
    m_controls->setObjectName("screenTranslationControls");
    m_controls->setWindowTitle("屏幕翻译 · 控制窗口");
    m_controls->setWindowIcon(appIcon());
    auto* layout = new QVBoxLayout(m_controls);
    layout->setContentsMargins(24,24,24,24);
    layout->setSpacing(12);
    auto* choices = new QHBoxLayout;
    m_ocr = new ThemedComboBox(m_controls);
    m_ocr->setObjectName("screenTranslationOcr");
    m_ocr->addItem("Windows OCR", "WindowsMedia");
    m_ocr->addItem("RapidOCR", "RapidOCR");
    m_ocr->setCurrentIndex(qMax(0, m_ocr->findData(OcrManager::instance().preferredEngineType())));
    m_service = new ThemedComboBox(m_controls);
    m_service->setObjectName("screenTranslationService");
    m_service->addItem("在线直连", "Online");
    m_service->addItem("Argos 中英离线", "Offline");
    for (const auto& p : AiService::profiles())
        if (p.protocol != "mathpix" && !AiService::isLocalProfile(p)) m_service->addItem(p.name, "ai:" + p.id);
    const QString preferred = TranslationPluginManager::instance().preferredEnginePreference();
    int serviceIndex = m_service->findData(preferred);
    if (serviceIndex < 0) {
        m_service->addItem("所选服务不可用", preferred);
        serviceIndex = m_service->count() - 1;
        setPluginChoiceEnabled(m_service, preferred, false);
    }
    m_service->setCurrentIndex(serviceIndex);
    m_target = new ThemedComboBox(m_controls);
    m_target->setObjectName("screenTranslationTarget");
    for (const auto& pair : QList<QPair<QString, QString>>{{"中文", "zh"}, {"英语", "en"},
        {"日语", "ja"}, {"韩语", "ko"}, {"法语", "fr"}, {"德语", "de"}, {"俄语", "ru"}})
        m_target->addItem("译为" + pair.first, pair.second);
    choices->addWidget(m_ocr); choices->addWidget(m_service); choices->addWidget(m_target);
    layout->addLayout(choices);
    auto refresh = [this] {
        setPluginChoiceEnabled(m_ocr, "RapidOCR", OcrManager::instance().isRapidOcrReady());
        setPluginChoiceEnabled(m_service, "Offline", TranslationPluginManager::instance().isOfflinePluginReady());
    };
    refresh();
    connect(&PluginManager::instance(), &PluginManager::pluginsRefreshed, this, refresh);
    auto* actions = new QHBoxLayout;
    m_retry = new QPushButton("重新识别", m_controls);
    m_retry->setObjectName("screenTranslationRetry");
    m_originalButton = new QPushButton("原图", m_controls);
    m_originalButton->setCheckable(true);
    m_originalButton->setToolTip("按住 Space 临时查看原图；Esc 关闭");
    auto* full = new QPushButton("全文", m_controls);
    full->setObjectName("screenTranslationFullText");
    m_copy = new QPushButton("复制译图", m_controls);
    m_copy->setObjectName("screenTranslationCopy");
    m_save = new QPushButton("保存译图", m_controls);
    m_save->setObjectName("screenTranslationSave");
    auto* close = new QPushButton("关闭", m_controls);
    for (auto* button : {m_retry, m_originalButton, full, m_copy, m_save, close}) actions->addWidget(button);
    layout->addLayout(actions);
    m_status = new QLabel("准备识别 · 按住 Space 看原图 · 双击图片查看全文", m_controls);
    m_status->setObjectName("screenTranslationStatus");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    auto theme = [this,full,close] {
        m_controls->setStyleSheet(ThemeManager::instance().getResultDialogStyle());
        const auto color = ThemeManager::instance().palette().text;
        m_retry->setIcon(uiIcon("ocr",color));
        m_originalButton->setIcon(uiIcon("capture",color));
        full->setIcon(uiIcon("text",color));
        m_copy->setIcon(uiIcon("copy",color));
        m_save->setIcon(uiIcon("save",color));
        close->setIcon(uiIcon("close",color));
    };
    theme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, theme);
    connect(m_retry, &QPushButton::clicked, this, &ScreenTranslationWindow::startRecognition);
    connect(m_originalButton, &QPushButton::toggled, this, [this](bool checked) { m_showOriginal = checked; update(); });
    connect(full, &QPushButton::clicked, this, &ScreenTranslationWindow::showFullText);
    connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setImage(m_render.image); });
    connect(m_save, &QPushButton::clicked, this, &ScreenTranslationWindow::saveImage);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(m_controls, &QDialog::finished, this, &QWidget::close);
    auto changed = [this] {
        if (m_lines.isEmpty()) return;
        startTranslation();
    };
    connect(m_service, &QComboBox::currentIndexChanged, this, changed);
    connect(m_target, &QComboBox::currentIndexChanged, this, changed);
    connect(m_ocr, &QComboBox::currentIndexChanged, this, [this] { startRecognition(); });
}

void ScreenTranslationWindow::positionControls() {
    m_controls->adjustSize();
    QScreen* screen = QGuiApplication::screenAt(m_screenRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();
    const QSize size = m_controls->frameGeometry().size();
    const QList<QPoint> candidates = {
        {m_screenRect.left(), m_screenRect.top() - size.height() - 8},
        {m_screenRect.left(), m_screenRect.bottom() + 9},
        {m_screenRect.right() + 9, m_screenRect.top()},
        {m_screenRect.left() - size.width() - 8, m_screenRect.top()}};
    for (QPoint point : candidates) {
        point.setX(qBound(available.left(), point.x(), qMax(available.left(), available.right() - size.width() + 1)));
        const QRect candidate(point, size);
        if (available.contains(candidate) && !candidate.intersects(m_screenRect)) { m_controls->move(point); return; }
    }
    // A full-screen capture leaves no outside strip. Keep a separate draggable
    // control window usable on-screen; Space hides it for unobstructed inspection.
    m_controls->move(available.topLeft() + QPoint(8, 8));
}

void ScreenTranslationWindow::stopCurrentRequest() {
    ++m_generation;
    auto cancel = std::move(m_cancel);
    m_cancel = {};
    if (cancel) cancel();
    m_busy = false;
}

void ScreenTranslationWindow::startRecognition() {
    if (m_closed) return;
    stopCurrentRequest();
    m_lines.clear();
    rebuildImage();
    if (m_original.isNull() || !m_screenRect.isValid()) {
        m_status->setText("截图为空或区域无效，无法翻译。"); emit workFinished(false); return;
    }
    m_busy = true;
    m_status->setText("正在识别截图文字…");
    const int generation = m_generation;
    QPointer<ScreenTranslationWindow> window(this);
    m_cancel = m_hooks.recognize(m_original.toImage(), m_original.devicePixelRatio(),
        m_ocr->currentData().toString(), [window, generation](OcrResult result) {
        if (!window) return;
        QTimer::singleShot(0, window, [window, generation, result] {
            if (!window || window->m_closed || window->m_generation != generation) return;
            window->m_cancel = {};
            window->acceptRecognition(result);
        });
    });
}

void ScreenTranslationWindow::acceptRecognition(const OcrResult& result) {
    m_busy = false;
    if (!result.success) {
        m_status->setText("文字识别失败：" + result.errorMessage); emit workFinished(false); return;
    }
    const QRectF canvas(QPointF(), m_original.deviceIndependentSize());
    for (const auto& line : result.lines) {
        if (line.text.trimmed().isEmpty()) continue;
        QRectF box = line.logicalBox;
        if (!box.isValid() && line.boundingBox.isValid()) {
            const qreal dpr = m_original.devicePixelRatio();
            box = QRectF(line.boundingBox.x() / dpr, line.boundingBox.y() / dpr,
                         line.boundingBox.width() / dpr, line.boundingBox.height() / dpr);
        }
        box = box.intersected(canvas);
        m_lines.append({box, line.text, {}, {}, ScreenTranslationLine::Pending});
    }
    if (m_lines.isEmpty() && !result.fullText.trimmed().isEmpty())
        m_lines.append({{}, result.fullText, {}, {}, ScreenTranslationLine::Pending});
    if (m_lines.isEmpty()) {
        m_status->setText("未识别到文字。");
        emit workFinished(false); return;
    }
    startTranslation();
}

void ScreenTranslationWindow::startTranslation() {
    stopCurrentRequest();
    m_requestedService = m_service->currentData().toString();
    m_requestedTarget = m_target->currentData().toString();
    QString source;
    for (auto& line : m_lines) {
        source += line.source + '\n';
        line.state = ScreenTranslationLine::Pending; line.translation.clear(); line.error.clear();
    }
    m_sourceLanguage = TranslationPluginManager::detectLanguageHeuristic(source);
    m_nextLine = 0;
    m_busy = true;
    rebuildImage();
    translateNext(m_generation);
}

void ScreenTranslationWindow::translateNext(int generation) {
    if (m_closed || generation != m_generation) return;
    if (m_nextLine >= m_lines.size()) {
        m_busy = false;
        rebuildImage();
        int failed = 0;
        for (const auto& line : m_lines) if (line.state == ScreenTranslationLine::Failed) ++failed;
        m_status->setText(failed ? QString("%1 行翻译失败，%2 行成功 · 全文查看错误并重试").arg(failed).arg(m_lines.size() - failed)
            : QString("已翻译 %1 行 · 按住 Space 看原图 · 全文查看完整译文").arg(m_lines.size()));
        if (!m_render.overflowLines.isEmpty()) m_status->setText(m_status->text() + QString(" · %1 行空间不足或位置缺失，仅节选显示，点击全文；琥珀边线标记节选").arg(m_render.overflowLines.size()));
        emit workFinished(failed == 0); return;
    }
    m_status->setText(QString("正在翻译 %1 / %2 行…").arg(m_nextLine + 1).arg(m_lines.size()));
    const int index = m_nextLine;
    QPointer<ScreenTranslationWindow> window(this);
    m_cancel = m_hooks.translate(m_lines[index].source, m_sourceLanguage, m_requestedTarget,
        m_requestedService, [window, generation, index](TranslationResult result) {
        if (!window) return;
        QTimer::singleShot(0, window, [window, generation, index, result] {
            if (!window || window->m_closed || window->m_generation != generation) return;
            window->m_cancel = {};
            auto& line = window->m_lines[index];
            const bool success = result.success && !result.translatedText.trimmed().isEmpty();
            line.state = success ? ScreenTranslationLine::Translated : ScreenTranslationLine::Failed;
            line.translation = success ? result.translatedText : QString();
            line.error = success ? QString() : result.errorMessage.isEmpty() ? QStringLiteral("服务未返回有效译文") : result.errorMessage;
            ++window->m_nextLine;
            window->rebuildImage();
            window->translateNext(generation);
        });
    });
}

void ScreenTranslationWindow::rebuildImage() {
    m_render = renderTranslatedImage(m_original, m_lines);
    const bool hasTranslation = std::any_of(m_lines.cbegin(), m_lines.cend(), [](const auto& line) {
        return line.state == ScreenTranslationLine::Translated && line.box.width() >= 3 && line.box.height() >= 3;
    });
    m_copy->setEnabled(hasTranslation && !m_busy); m_save->setEnabled(hasTranslation && !m_busy);
    update();
}

QString ScreenTranslationWindow::statusText() const { return m_status->text(); }

void ScreenTranslationWindow::showFullText() {
    auto* dialog = new QDialog(this, Qt::Window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("屏幕翻译 · 完整原文与译文");
    dialog->setWindowIcon(appIcon());
    dialog->resize(680, 520);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24,24,24,24);
    auto* text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    QString content;
    for (int i = 0; i < m_lines.size(); ++i) {
        const auto& line = m_lines[i];
        content += QString("[%1] 原文\n%2\n%3\n\n").arg(i + 1).arg(line.source,
            line.state == ScreenTranslationLine::Translated ? "译文\n" + line.translation
            : line.state == ScreenTranslationLine::Failed ? "翻译失败：" + line.error : "等待翻译…");
    }
    text->setPlainText(content);
    layout->addWidget(text);
    dialog->setStyleSheet(ThemeManager::instance().getResultDialogStyle());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, dialog, [dialog] {
        dialog->setStyleSheet(ThemeManager::instance().getResultDialogStyle());
    });
    dialog->show();
}

void ScreenTranslationWindow::saveImage() {
    const QString path = QFileDialog::getSaveFileName(m_controls, "保存译图", "屏幕译图.png", "PNG 图片 (*.png)");
    if (!path.isEmpty() && !m_render.image.save(path, "PNG")) QMessageBox::warning(m_controls, "保存失败", "无法写入图片，请检查路径与权限。");
}

void ScreenTranslationWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (m_showOriginal || m_originalHeld) painter.drawPixmap(rect(), m_original);
    else painter.drawImage(rect(), m_render.image);
}

void ScreenTranslationWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    m_controls->show(); positionControls();
    activateWindow(); setFocus();
}

void ScreenTranslationWindow::closeEvent(QCloseEvent* event) {
    m_closed = true; stopCurrentRequest(); m_controls->hide();
    QWidget::closeEvent(event);
}

void ScreenTranslationWindow::setOriginalHeld(bool held) {
    m_originalHeld = held;
    m_controls->setVisible(!held && !m_closed);
    update();
}

bool ScreenTranslationWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool ours = widget && (widget == this || isAncestorOf(widget));
    if (ours && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Space && !key->isAutoRepeat()) {
            setOriginalHeld(event->type() == QEvent::KeyPress); return true;
        }
        if (key->key() == Qt::Key_Escape && event->type() == QEvent::KeyPress && widget->window() != m_controls) {
            if (widget->window() == this) { close(); return true; }
        }
    }
    if (event->type() == QEvent::ApplicationDeactivate && m_originalHeld) setOriginalHeld(false);
    return QWidget::eventFilter(watched, event);
}

void ScreenTranslationWindow::mouseDoubleClickEvent(QMouseEvent*) { showFullText(); }

void ScreenTranslationWindow::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::instance().getPinMenuStyle());
    menu.addAction("查看全文", this, &ScreenTranslationWindow::showFullText);
    menu.addAction("显示控制窗口", this, [this] { m_controls->show(); m_controls->raise(); });
    menu.addAction("关闭", this, &QWidget::close);
    menu.exec(event->globalPos());
}
