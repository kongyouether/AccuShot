#include "../themed_combo_box.h"
#include "floating_toolbar.h"
#include "toolbar_icons.h"
#include "../../core/toolbar_config.h"
#include "../../core/hotkey_config.h"
#include "../../core/theme_manager.h"
#include "../settings/toolbar_settings_dialog.h"
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QFontComboBox>
#include <QLabel>
#include <QColorDialog>
#include <QMenu>
#include <QWidgetAction>
#include <QGridLayout>
#include <QKeyEvent>
#include <QTimer>
#include <QSettings>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
namespace {
constexpr int Margin = 8;
constexpr int ButtonSize = 36;
bool isDrawing(ToolAction a) { return toolDefinition(a).annotation; }
int toolGroup(ToolAction action) {
    if (isDrawing(action) || action == ToolAction::Undo) return 0;
    if (action == ToolAction::Ocr || action == ToolAction::FormulaOcr || action == ToolAction::Translate ||
        action == ToolAction::ScreenTranslate || action == ToolAction::CopyText || action == ToolAction::Markdown ||
        action == ToolAction::ScrollCapture || action == ToolAction::GifRecord) return 1;
    return 2;
}
bool hasDropdown(ToolAction a) {
    return a == ToolAction::Rect || a == ToolAction::Arrow || a == ToolAction::Pencil || a == ToolAction::Mosaic;
}
}
FloatingToolbar::FloatingToolbar(QWidget* parent) : QWidget(parent) {
    setObjectName("captureToolbar");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(8);
    m_header = new QWidget(this);
    m_header->setFixedHeight(ButtonSize + 4);
    m_toolsLayout = new QHBoxLayout(m_header);
    m_toolsLayout->setContentsMargins(0, 0, 0, 0);
    m_toolsLayout->setSpacing(3);
    column->addWidget(m_header);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    for (const auto& def : toolbarDefinitions()) {
        if (!def.annotation) continue;
        Properties p;
        settings.beginGroup("ToolbarProperties/" + def.id);
        const QColor color = settings.value("color", p.color).value<QColor>();
        if (color.isValid()) p.color = color;
        p.width = qBound(1, settings.value("width", 3).toInt(), 30);
        p.mosaic = qBound(4, settings.value("mosaic", 12).toInt(), 40);
        const auto fontString = settings.value("font").toString();
        if (!fontString.isEmpty()) p.font.fromString(fontString);
        p.font.setPointSize(qBound(8, p.font.pointSize(), 96));
        settings.endGroup();
        m_propertiesByTool.insert(def.action, p);
    }
    setupProperties();
    column->addWidget(m_properties);
    m_properties->hide();
    rebuildButtons();
    connect(&ToolbarConfig::instance(), &ToolbarConfig::changed, this, &FloatingToolbar::rebuildButtons);
    connect(&HotkeyConfig::instance(), &HotkeyConfig::configChanged, this, &FloatingToolbar::refreshTooltips);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &FloatingToolbar::updateThemeStyle);
}
void FloatingToolbar::rebuildButtons() {
    while (auto* item = m_toolsLayout->takeAt(0)) { delete item->widget(); delete item; }
    m_buttons.clear(); m_cells.clear();
    m_grip = new QToolButton(m_header);
    m_grip->setObjectName("toolbarGrip");
    m_grip->setFixedSize(18, ButtonSize);
    m_grip->setCursor(Qt::SizeAllCursor);
    m_grip->setFocusPolicy(Qt::NoFocus);
    m_grip->setToolTip("拖动工具栏");
    m_grip->setAccessibleName("拖动工具栏");
    m_grip->setIconSize(QSize(18, 18));
    m_grip->installEventFilter(this);
    m_toolsLayout->addWidget(m_grip);
    for (const auto& entry : ToolbarConfig::instance().entries()) {
        auto* cell = new QWidget(m_header);
        auto* row = new QHBoxLayout(cell);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
        auto* button = new QToolButton(cell);
        button->setObjectName("tool_" + toolDefinition(entry.action).id);
        button->setAccessibleName(toolDefinition(entry.action).label);
        button->setFixedSize(ButtonSize, ButtonSize);
        button->setIconSize(QSize(19, 19)); button->setFocusPolicy(Qt::NoFocus);
        button->setCheckable(isDrawing(entry.action));
        button->setChecked(isDrawing(entry.action) && entry.action == m_activeTool);
        connect(button, &QToolButton::clicked, this, [this, a = entry.action]() {
            if (isDrawing(a)) setActiveTool(a);
            emit actionTriggered(a);
        });
        row->addWidget(button);
        if (hasDropdown(entry.action)) {
            auto* arrow = new QToolButton(cell);
            arrow->setObjectName("toolDropdown");
            arrow->setFixedSize(14, ButtonSize); arrow->setFocusPolicy(Qt::NoFocus);
            arrow->setIconSize(QSize(16, 16));
            arrow->setToolTip(toolDefinition(entry.action).label + "设置");
            arrow->setAccessibleName(arrow->toolTip());
            connect(arrow, &QToolButton::clicked, this, [this, a = entry.action]() {
                if (m_activeTool == a) {
                    m_panelExpanded = !m_panelExpanded; syncProperties(); placeToolbar();
                } else { setActiveTool(a); emit actionTriggered(a); }
            });
            row->addWidget(arrow);
        }
        cell->setFixedWidth(hasDropdown(entry.action) ? 50 : ButtonSize);
        m_buttons.insert(entry.action, button); m_cells.insert(entry.action, cell);
        m_toolsLayout->addWidget(cell);
    }
    m_more = new QToolButton(m_header);
    m_more->setObjectName("toolbarMore");
    m_more->setFixedSize(ButtonSize, ButtonSize); m_more->setIconSize(QSize(19, 19));
    m_more->setFocusPolicy(Qt::NoFocus); m_more->setToolTip("更多功能 / 工具栏配置");
    m_more->setAccessibleName("更多功能 / 工具栏配置");
    connect(m_more, &QToolButton::clicked, this, &FloatingToolbar::showMoreMenu);
    m_toolsLayout->addWidget(m_more);
    refreshTooltips(); updateThemeStyle(); rebuildOverflow(); placeToolbar();
}
void FloatingToolbar::setupProperties() {
    m_properties = new QWidget(this); m_properties->setObjectName("toolProperties");
    auto* panel = new QVBoxLayout(m_properties);
    panel->setContentsMargins(8, 8, 8, 8); panel->setSpacing(8);
    m_widthControls = new QWidget(m_properties);
    auto* widthRow = new QHBoxLayout(m_widthControls); widthRow->setContentsMargins(0, 0, 0, 0);
    m_widthLabel = new QLabel("线宽", m_widthControls);
    m_widthSpin = new QSpinBox(m_widthControls); m_widthSpin->setObjectName("strokeWidth");
    m_widthSpin->setRange(1, 30); m_widthSpin->setSuffix(" px"); m_widthSpin->setFixedWidth(82);
    widthRow->addWidget(m_widthLabel); widthRow->addWidget(m_widthSpin); widthRow->addStretch();
    panel->addWidget(m_widthControls);
    connect(m_widthSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_syncing) return;
        if (m_activeTool == ToolAction::Mosaic) m_propertiesByTool[m_activeTool].mosaic = value;
        else m_propertiesByTool[m_activeTool].width = value;
        saveProperties(); publishProperties();
    });
    m_fontControls = new QWidget(m_properties);
    m_fontControls->setFixedHeight(36);
    auto* fontRow = new QHBoxLayout(m_fontControls); fontRow->setContentsMargins(0, 0, 0, 0); fontRow->setSpacing(8);
    m_fontCombo = new ThemedFontComboBox(m_fontControls); m_fontCombo->setFixedWidth(200);
    m_fontCombo->setFixedHeight(32);
    m_fontCombo->setAccessibleName("文字字体");
    m_fontSizeSpin = new QSpinBox(m_fontControls); m_fontSizeSpin->setObjectName("fontSize");
    m_fontSizeSpin->setRange(8, 96); m_fontSizeSpin->setSuffix(" pt"); m_fontSizeSpin->setFixedWidth(108);
    m_bold = new QToolButton(m_fontControls); m_bold->setText("B");
    auto boldFont = font(); boldFont.setBold(true); m_bold->setFont(boldFont);
    m_bold->setCheckable(true); m_bold->setFocusPolicy(Qt::NoFocus);
    m_bold->setToolTip("粗体"); m_bold->setAccessibleName("粗体"); m_bold->setFixedSize(32, 32);
    fontRow->addWidget(m_fontCombo); fontRow->addWidget(m_fontSizeSpin); fontRow->addWidget(m_bold); fontRow->addStretch();
    panel->addWidget(m_fontControls);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        if (m_syncing) return;
        m_propertiesByTool[m_activeTool].font.setFamily(font.family()); saveProperties(); publishProperties();
    });
    connect(m_fontSizeSpin, &QSpinBox::valueChanged, this, [this](int size) {
        if (m_syncing) return;
        m_propertiesByTool[m_activeTool].font.setPointSize(size); saveProperties(); publishProperties();
    });
    connect(m_bold, &QToolButton::toggled, this, [this](bool bold) {
        if (m_syncing) return;
        m_propertiesByTool[m_activeTool].font.setBold(bold); saveProperties(); publishProperties();
    });
    m_colors = new QWidget(m_properties);
    auto* colors = new QHBoxLayout(m_colors); colors->setContentsMargins(0, 0, 0, 0); colors->setSpacing(8);
    for (const auto* hex : {"#ef4444", "#f59e0b", "#facc15", "#22c55e", "#3b82f6", "#a855f7", "#ffffff", "#18181b"}) {
        auto* swatch = new QToolButton(m_colors); swatch->setFixedSize(20, 20); swatch->setFocusPolicy(Qt::NoFocus);
        swatch->setProperty("color", QColor(hex)); swatch->setToolTip(hex);
        swatch->setAccessibleName(QString("标注颜色 %1").arg(hex));
        connect(swatch, &QToolButton::clicked, this, [this, swatch]() {
            m_propertiesByTool[m_activeTool].color = swatch->property("color").value<QColor>();
            saveProperties(); syncProperties(); publishProperties();
        });
        colors->addWidget(swatch); m_swatches.append(swatch);
    }
    auto* custom = new QToolButton(m_colors); custom->setText("+"); custom->setFixedSize(24, 24);
    custom->setFocusPolicy(Qt::NoFocus); custom->setToolTip("自定义颜色");
    custom->setAccessibleName("自定义颜色");
    connect(custom, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(currentColor(), this, "标注颜色");
        if (!color.isValid()) return;
        m_propertiesByTool[m_activeTool].color = color; saveProperties(); syncProperties(); publishProperties();
    });
    colors->addWidget(custom); colors->addStretch(); panel->addWidget(m_colors);
}
void FloatingToolbar::syncProperties() {
    m_syncing = true;
    const bool editable = isDrawing(m_activeTool) && m_activeTool != ToolAction::CharEdit;
    m_properties->setVisible(editable && m_panelExpanded);
    if (editable) {
        const auto p = m_propertiesByTool.value(m_activeTool);
        const bool text = m_activeTool == ToolAction::Text, mosaic = m_activeTool == ToolAction::Mosaic;
        m_fontControls->setVisible(text); m_widthControls->setVisible(!text); m_colors->setVisible(!mosaic);
        m_widthLabel->setText(mosaic ? "马赛克块大小" : "线宽");
        m_widthSpin->setRange(mosaic ? 4 : 1, mosaic ? 40 : 30); m_widthSpin->setValue(mosaic ? p.mosaic : p.width);
        m_fontCombo->setCurrentFont(p.font); m_fontSizeSpin->setValue(p.font.pointSize()); m_bold->setChecked(p.font.bold());
        for (auto* swatch : m_swatches) {
            const auto color = swatch->property("color").value<QColor>();
            const auto palette = ThemeManager::instance().palette();
            swatch->setStyleSheet(ThemeManager::instance().getControlStyle() + QString(
                "QToolButton {background:%1;border:2px solid %2;border-radius:10px;}"
                "QToolButton:hover {background:%1;border:3px solid %3;}"
                "QToolButton:pressed {background:%1;border:3px solid %4;}")
                .arg(color.name(), (color == p.color ? palette.action : palette.controlBorder).name(),
                     palette.action.name(), palette.text.name()));
        }
    }
    m_syncing = false;
}
void FloatingToolbar::saveProperties() {
    const auto p = m_propertiesByTool.value(m_activeTool);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan"); settings.beginGroup("ToolbarProperties/" + toolDefinition(m_activeTool).id);
    settings.setValue("color", p.color); settings.setValue("width", p.width);
    settings.setValue("font", p.font.toString()); settings.setValue("mosaic", p.mosaic);
}
void FloatingToolbar::publishProperties() {
    const auto p = m_propertiesByTool.value(m_activeTool);
    emit colorSelected(p.color); emit thicknessSelected(p.width, p.font.pointSize());
    emit fontSelected(p.font); emit mosaicSizeSelected(p.mosaic);
}
QColor FloatingToolbar::currentColor() const { return m_propertiesByTool.value(m_activeTool).color; }
int FloatingToolbar::currentStrokeWidth() const { return m_propertiesByTool.value(m_activeTool).width; }
int FloatingToolbar::currentFontSize() const { return currentTextFont().pointSize(); }
QFont FloatingToolbar::currentTextFont() const { return m_propertiesByTool.value(m_activeTool).font; }
void FloatingToolbar::setActiveTool(ToolAction action) {
    m_activeTool = action; m_panelExpanded = true;
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) it.value()->setChecked(isDrawing(it.key()) && it.key() == action);
    syncProperties(); if (isDrawing(action)) publishProperties(); placeToolbar();
}
QString FloatingToolbar::tooltip(ToolAction action) const {
    const auto& c = HotkeyConfig::instance().data(); QString key;
    switch (action) {
    case ToolAction::Rect: key = c.toolRect; break;
    case ToolAction::Pencil: key = c.toolPencil; break;
    case ToolAction::Arrow: key = c.toolArrow; break;
    case ToolAction::Text: key = c.toolText; break;
    case ToolAction::Mosaic: key = c.toolMosaic; break;
    case ToolAction::CharEdit: key = c.toolCharEdit; break;
    case ToolAction::Ocr: key = c.snippingOcr; break;
    case ToolAction::Undo: key = c.snippingUndo; break;
    case ToolAction::Pin: key = c.snippingPin; break;
    case ToolAction::Save: key = c.snippingSave; break;
    case ToolAction::Cancel: key = c.snippingCancel; break;
    case ToolAction::Confirm: key = c.snippingConfirm; break;
    default: break;
    }
    QString tip = toolDefinition(action).label;
    if (!key.isEmpty()) tip += "  (" + key + ")";
    if (action == ToolAction::Undo && !m_undoAvailable) tip += "\n暂无可撤销的标注";
    if (m_ocrBusy && (action == ToolAction::Ocr || action == ToolAction::CopyText || action == ToolAction::Translate || action == ToolAction::ScreenTranslate || action == ToolAction::FormulaOcr || action == ToolAction::Markdown || action == ToolAction::CharEdit)) tip += "\n正在识别，请稍候…";
    return tip;
}
bool FloatingToolbar::actionEnabled(ToolAction action) const {
    if (action == ToolAction::Undo) return m_undoAvailable;
    if (action == ToolAction::Ocr || action == ToolAction::CopyText || action == ToolAction::Translate || action == ToolAction::ScreenTranslate || action == ToolAction::FormulaOcr || action == ToolAction::Markdown || action == ToolAction::CharEdit) return !m_ocrBusy;
    return true;
}
void FloatingToolbar::refreshTooltips() {
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        it.value()->setToolTip(tooltip(it.key())); it.value()->setEnabled(actionEnabled(it.key()));
    }
}
void FloatingToolbar::setUndoAvailable(bool value) { m_undoAvailable = value; refreshTooltips(); }
void FloatingToolbar::setOcrBusy(bool value) { m_ocrBusy = value; refreshTooltips(); }
void FloatingToolbar::showMoreMenu() {
    QMenu menu(this);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, &menu, &QMenu::close);
    menu.setObjectName("toolbarMoreMenu");
    menu.setStyleSheet(ThemeManager::instance().getPinMenuStyle() +
        "QMenu#toolbarMoreMenu {padding:0;border-radius:10px;}"
        "QWidget#toolbarMenuGrid QToolButton {padding:6px 8px;text-align:left;}");
    auto* content = new QWidget(&menu);
    content->setObjectName("toolbarMenuGrid");
    auto* grid = new QGridLayout(content);
    grid->setContentsMargins(8, 8, 8, 8); grid->setSpacing(4);
    auto* widgetAction = new QWidgetAction(&menu);
    widgetAction->setDefaultWidget(content); menu.addAction(widgetAction);
    bool chosen = false, configure = false;
    ToolAction selected = ToolAction::Cancel;
    int index = 0;
    for (auto action : m_overflow) {
        auto* item = new QToolButton(content);
        item->setObjectName("more_" + toolDefinition(action).id);
        item->setProperty("toolbarMenuItem", true);
        item->setIcon(toolbarIcon(action, ThemeManager::instance().palette().text));
        item->setIconSize(QSize(19, 19));
        item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        QString label = toolDefinition(action).label;
        if (action == ToolAction::ScrollCapture) label = "滚动截图";
        if (action == ToolAction::CopyText) label = "复制文字";
        if (action == ToolAction::ScreenTranslate) label = "原位翻译";
        if (action == ToolAction::Markdown) label = "转 Markdown";
        item->setText(label); item->setAccessibleName(label); item->setToolTip(tooltip(action));
        item->setMinimumSize(132, 36); item->setFocusPolicy(Qt::StrongFocus);
        item->setEnabled(actionEnabled(action)); item->setCheckable(isDrawing(action)); item->setChecked(isDrawing(action) && m_activeTool == action);
        item->installEventFilter(this);
        grid->addWidget(item, index / 2, index % 2); ++index;
        connect(item, &QToolButton::clicked, &menu, [&menu, &chosen, &selected, action]() {
            chosen = true; selected = action; menu.close();
        });
    }
    auto* settings = new QToolButton(content);
    settings->setObjectName("more_configure");
    settings->setProperty("toolbarMenuItem", true);
    settings->setText("工具栏配置…"); settings->setAccessibleName(settings->text());
    settings->setIcon(uiIcon("settings", ThemeManager::instance().palette().text));
    settings->setIconSize(QSize(19, 19)); settings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    settings->setMinimumHeight(36); settings->setFocusPolicy(Qt::StrongFocus); settings->installEventFilter(this);
    grid->addWidget(settings, (index + 1) / 2, 0, 1, 2);
    connect(settings, &QToolButton::clicked, &menu, [&menu, &configure]() { configure = true; menu.close(); });
    content->setMinimumWidth(290);
    QTimer::singleShot(0, &menu, [content]() {
        for (auto* button : content->findChildren<QToolButton*>())
            if (button->isEnabled()) { button->setFocus(Qt::PopupFocusReason); break; }
    });
    menu.exec(m_more->mapToGlobal(QPoint(0, m_more->height() + 6)));
    if (chosen) {
        if (isDrawing(selected)) setActiveTool(selected);
        emit actionTriggered(selected);
    } else if (configure) {
        ToolbarSettingsDialog dialog(this); dialog.setWindowFlag(Qt::WindowStaysOnTopHint); dialog.exec();
    }
}
void FloatingToolbar::rebuildOverflow() {
    m_overflow.clear(); QList<ToolAction> visible;
    for (const auto& entry : ToolbarConfig::instance().entries()) {
        bool show = entry.visibility == ToolVisibility::Always; m_cells[entry.action]->setVisible(show);
        if (show) visible.append(entry.action);
    }
    const auto measure = [this]() {
        int needed = 12 + 18 + ButtonSize + 3, previous = -1;
        for (const auto& entry : ToolbarConfig::instance().entries()) {
            auto* cell = m_cells[entry.action];
            if (cell->isHidden()) continue;
            const int group = toolGroup(entry.action);
            const int gap = previous >= 0 && group != previous ? 11 : 0;
            cell->layout()->setContentsMargins(gap, 0, 0, 0);
            cell->setFixedWidth((hasDropdown(entry.action) ? 50 : ButtonSize) + gap);
            needed += cell->width() + 3;
            previous = group;
        }
        return needed;
    };
    int needed = measure();
    const int available = m_boundary.isValid() ? m_boundary.width() - 2 * Margin : 1000;
    const int fontSpace = available - 32 - m_fontSizeSpin->width() - m_bold->width()
        - 3 * m_fontControls->layout()->spacing();
    m_fontCombo->setFixedWidth(qBound(120, fontSpace, 200));
    for (int i = visible.size() - 1; needed > available && i >= 0; --i) {
        auto action = visible[i];
        if (action == ToolAction::Cancel || action == ToolAction::Confirm) continue;
        m_cells[action]->hide(); needed = measure();
    }
    for (const auto& entry : ToolbarConfig::instance().entries())
        if (entry.visibility != ToolVisibility::Hidden && m_cells[entry.action]->isHidden()) m_overflow.append(entry.action);
    const int propertyWidth = m_properties->isHidden() ? 0 : m_properties->layout()->minimumSize().width() + 16;
    setFixedWidth(qMin(available, qMax(needed, propertyWidth)));
    update();
}
void FloatingToolbar::updatePosition(const QRect& selection, const QRect& boundary) { m_selection = selection; m_boundary = boundary; placeToolbar(); }
void FloatingToolbar::placeToolbar() {
    if (!m_boundary.isValid()) return;
    rebuildOverflow();
    m_properties->layout()->activate();
    const int panelHeight = m_properties->isHidden() ? 0 : m_properties->layout()->sizeHint().height() + 8;
    setFixedHeight(ButtonSize + 16 + panelHeight);
    layout()->activate();
    const auto area = m_boundary.adjusted(Margin, Margin, -Margin, -Margin); QPoint target = pos();
    if (!m_userPosition) {
        target = QPoint(m_selection.right() - width() + 1, m_selection.bottom() + Margin);
        if (target.y() + height() > area.bottom()) target.setY(m_selection.top() - height() - Margin);
        if (target.y() < area.top()) target.setY(m_selection.bottom() - height() - Margin);
    }
    target.setX(qBound(area.left(), target.x(), qMax(area.left(), area.right() - width() + 1)));
    target.setY(qBound(area.top(), target.y(), qMax(area.top(), area.bottom() - height() + 1))); move(target);
}
void FloatingToolbar::resetSession() {
    m_userPosition = false; m_dragging = false; m_ocrBusy = false; m_undoAvailable = false;
    setActiveTool(ToolAction::Cancel); refreshTooltips();
    emit mosaicSizeSelected(m_propertiesByTool.value(ToolAction::Mosaic).mosaic);
}
bool FloatingToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (watched->property("toolbarMenuItem").toBool() && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        int dx = 0, dy = 0;
        if (key->key() == Qt::Key_Left) dx = -1;
        else if (key->key() == Qt::Key_Right) dx = 1;
        else if (key->key() == Qt::Key_Up) dy = -1;
        else if (key->key() == Qt::Key_Down) dy = 1;
        else if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            qobject_cast<QToolButton*>(watched)->click(); return true;
        }
        if (dx || dy) {
            auto* button = qobject_cast<QToolButton*>(watched);
            auto* grid = qobject_cast<QGridLayout*>(button->parentWidget()->layout());
            int row, column, rowSpan, columnSpan;
            grid->getItemPosition(grid->indexOf(button), &row, &column, &rowSpan, &columnSpan);
            // Keep the entering column while the footer spans both columns.
            const int preferredColumn = columnSpan > 1 && button->property("menuColumn").isValid()
                ? button->property("menuColumn").toInt() : column;
            const auto focusAt = [grid, button](int targetRow, int targetColumn) {
                auto* item = grid->itemAtPosition(targetRow, targetColumn);
                auto* target = item ? qobject_cast<QToolButton*>(item->widget()) : nullptr;
                if (!target || target == button || !target->isEnabled()) return false;
                target->setProperty("menuColumn", targetColumn);
                target->setFocus(Qt::OtherFocusReason);
                return true;
            };
            if (dy) {
                for (int nextRow = row + dy; nextRow >= 0 && nextRow < grid->rowCount(); nextRow += dy) {
                    if (focusAt(nextRow, preferredColumn) || focusAt(nextRow, 1 - preferredColumn)) break;
                }
            } else {
                const int firstColumn = dx > 0 ? column + columnSpan : column - 1;
                for (int nextColumn = firstColumn; nextColumn >= 0 && nextColumn < grid->columnCount(); nextColumn += dx)
                    if (focusAt(row, nextColumn)) break;
            }
            return true;
        }
    }
    if (watched == m_grip) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = true; m_userPosition = true; m_dragOffset = mouse->globalPosition().toPoint() - mapToGlobal(QPoint()); return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint target = mouse->globalPosition().toPoint() - m_dragOffset;
            move(parentWidget() ? parentWidget()->mapFromGlobal(target) : target); placeToolbar(); return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) { m_dragging = false; return true; }
    }
    return QWidget::eventFilter(watched, event);
}
void FloatingToolbar::updateThemeStyle() {
    auto& theme = ThemeManager::instance();
    const auto p = theme.palette();
    auto colors = palette();
    colors.setColor(QPalette::ButtonText, p.text);
    colors.setColor(QPalette::Text, p.text);
    setPalette(colors);
    setStyleSheet(theme.getControlStyle() + theme.getFloatingToolbarStyle() +
        QString("QWidget#captureToolbar {background:%1;border:0;border-radius:12px;}"
                "QWidget#toolProperties {background:%2;border:0;border-radius:7px;}")
            .arg(p.raised.name(), p.surface.name()));
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it)
        it.value()->setIcon(toolbarIcon(it.key(), it.key() == ToolAction::Confirm ? p.onAction : p.text));
    m_grip->setIcon(toolbarGripIcon(p.secondary)); m_more->setIcon(toolbarMoreIcon(p.text));
    for (auto* arrow : m_header->findChildren<QToolButton*>("toolDropdown")) arrow->setIcon(toolbarChevronIcon(p.secondary));
    syncProperties(); update();
}
void FloatingToolbar::paintEvent(QPaintEvent*) {
    QPainter painter(this); QStyleOption option; option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeManager::instance().palette().well);
    painter.setRenderHint(QPainter::Antialiasing);
    QRect contentGroup;
    const auto flushGroup = [&]() {
        if (!contentGroup.isNull()) painter.drawRoundedRect(contentGroup.adjusted(-2, 0, 2, 0), 7, 7);
        contentGroup = QRect();
    };
    for (const auto& entry : ToolbarConfig::instance().entries()) {
        auto* cell = m_cells.value(entry.action); if (cell->isHidden()) continue;
        const int group = toolGroup(entry.action);
        if (group == 1) {
            const int inset = cell->layout()->contentsMargins().left();
            const QRect bounds(cell->mapTo(this, QPoint(inset, 0)), QSize(cell->width() - inset, m_header->height()));
            contentGroup = contentGroup.isNull() ? bounds : contentGroup.united(bounds);
        } else flushGroup();
    }
    flushGroup();
}
