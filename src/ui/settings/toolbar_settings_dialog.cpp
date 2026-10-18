#include "../themed_combo_box.h"
#include "toolbar_settings_dialog.h"
#include "../toolbar/toolbar_icons.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>
#include <QScrollArea>
#include <QToolButton>
#include "../../core/theme_manager.h"

namespace {
constexpr int VisibilityRole = Qt::UserRole + 1;
}

ToolbarSettingsDialog::ToolbarSettingsDialog(QWidget* parent, bool embedded)
    : QDialog(parent), m_embedded(embedded)
{
    setWindowTitle("工具栏设置 - AccuShot");
    setWindowIcon(appIcon());
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    if (!embedded) { resize(740, 700); setMinimumSize(640, 520); }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(embedded ? 0 : 24, embedded ? 0 : 24, embedded ? 0 : 24, embedded ? 0 : 24);
    layout->setSpacing(16);
    auto* title = new QLabel("自定义截图工具栏", this);
    title->setObjectName(embedded ? "cardTitle" : "pageTitle");
    layout->addWidget(title);
    auto* hint = new QLabel("拖动或用上下按钮排序；隐藏工具仍可通过快捷键使用。", this);
    hint->setObjectName("secondaryText");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* previewScroll = new QScrollArea(this);
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setWidgetResizable(true);
    previewScroll->setFixedHeight(48);
    auto* preview = new QWidget(previewScroll);
    preview->setObjectName("toolbarPreview");
    m_preview = new QHBoxLayout(preview);
    m_preview->setContentsMargins(8, 4, 8, 4);
    m_preview->setSpacing(4);
    previewScroll->setWidget(preview);
    layout->addWidget(previewScroll);
    auto* orderBar = new QHBoxLayout();
    auto* up = new QPushButton("上移", this); up->setObjectName("moveUp");
    auto* down = new QPushButton("下移", this); down->setObjectName("moveDown");
    orderBar->addWidget(up); orderBar->addWidget(down); orderBar->addStretch();
    layout->addLayout(orderBar);
    connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    m_list = new QListWidget(this);
    m_list->setObjectName("toolbarToolList");
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDragDropOverwriteMode(false);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSpacing(4);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] { updatePreview(); });

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto* reset = new QPushButton("恢复默认", this);
    connect(reset, &QPushButton::clicked, this, [this]() {
        loadEntries(ToolbarConfig::defaults());
    });
    buttons->addWidget(reset);
    buttons->addStretch();
    auto* cancel = new QPushButton("取消", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* save = new QPushButton("保存", this);
    save->setObjectName("primaryAction");
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, &ToolbarSettingsDialog::accept);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    if (embedded) { cancel->hide(); save->hide(); }
    loadEntries(ToolbarConfig::instance().entries());
    updateTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ToolbarSettingsDialog::updateTheme);
    for (auto* button : findChildren<QPushButton*>()) button->setAutoDefault(false);
}

void ToolbarSettingsDialog::loadEntries(const QList<ToolbarEntry>& entries)
{
    m_list->clear();
    for (const auto& entry : entries) {
        const auto& definition = toolDefinition(entry.action);
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, static_cast<int>(entry.action));
        item->setData(VisibilityRole, static_cast<int>(entry.visibility));
        item->setSizeHint(QSize(0, 74));
        auto* row = new QWidget(m_list);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(20, 18, 20, 18);
        rowLayout->setSpacing(12);
        auto* grip = new QLabel(row);
        grip->setPixmap(toolbarGripIcon(ThemeManager::instance().palette().secondary).pixmap(16, 20));
        grip->setFixedSize(16, 20);
        grip->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(grip);
        auto* icon = new QLabel(row);
        icon->setPixmap(toolbarIcon(entry.action, ThemeManager::instance().palette().text).pixmap(20, 20));
        icon->setFixedSize(20, 20);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(icon);
        auto* name = new QLabel(definition.label, row);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(name, 1);
        auto* category = new QLabel(definition.annotation ? "标注" : "操作", row);
        category->setObjectName("category");
        category->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(category);
        auto* visibility = new ThemedComboBox(row);
        visibility->setFixedWidth(120);
        visibility->setFixedHeight(36);
        visibility->setAccessibleName(definition.label + "显示位置");
        visibility->addItem("常驻", static_cast<int>(ToolVisibility::Always));
        visibility->addItem("更多菜单", static_cast<int>(ToolVisibility::More));
        visibility->addItem("隐藏", static_cast<int>(ToolVisibility::Hidden));
        visibility->setCurrentIndex(visibility->findData(static_cast<int>(entry.visibility)));
        connect(visibility, &QComboBox::currentIndexChanged, this, [this, item, visibility](int) {
            item->setData(VisibilityRole, visibility->currentData());
            updatePreview();
        });
        rowLayout->addWidget(visibility);
        m_list->setItemWidget(item, row);
    }
    updatePreview();
}

QList<ToolbarEntry> ToolbarSettingsDialog::entries() const
{
    QList<ToolbarEntry> entries;
    for (int i = 0; i < m_list->count(); ++i) {
        const auto* item = m_list->item(i);
        entries.append({static_cast<ToolAction>(item->data(Qt::UserRole).toInt()),
            static_cast<ToolVisibility>(item->data(VisibilityRole).toInt())});
    }
    return entries;
}

void ToolbarSettingsDialog::accept()
{
    if (!m_embedded) ToolbarConfig::instance().setEntries(entries());
    QDialog::accept();
}

void ToolbarSettingsDialog::moveSelected(int offset)
{
    const int row = m_list->currentRow();
    const int target = row + offset;
    if (row < 0 || target < 0 || target >= m_list->count()) return;
    m_list->model()->moveRow(QModelIndex(), row, QModelIndex(), offset > 0 ? target + 1 : target);
    m_list->setCurrentRow(target);
}

void ToolbarSettingsDialog::updatePreview()
{
    while (auto* item = m_preview->takeAt(0)) { delete item->widget(); delete item; }
    const QColor ink = ThemeManager::instance().palette().text;
    QStringList more;
    for (const auto& entry : entries()) {
        if (entry.visibility == ToolVisibility::Hidden) continue;
        if (entry.visibility == ToolVisibility::More) { more << toolDefinition(entry.action).label; continue; }
        auto* icon = new QLabel(this);
        icon->setPixmap(toolbarIcon(entry.action, ink).pixmap(19, 19));
        icon->setFixedSize(36, 36);
        icon->setAlignment(Qt::AlignCenter);
        icon->setToolTip(toolDefinition(entry.action).label);
        m_preview->addWidget(icon);
    }
    auto* moreLabel = new QLabel(this);
    moreLabel->setPixmap(uiIcon("more", ink).pixmap(19,19));
    moreLabel->setAlignment(Qt::AlignCenter);
    moreLabel->setFixedSize(36,36);
    moreLabel->setToolTip(more.isEmpty() ? "更多菜单" : more.join("、"));
    m_preview->addWidget(moreLabel);
    m_preview->addStretch();
}

void ToolbarSettingsDialog::updateTheme()
{
    setStyleSheet(ThemeManager::instance().getSettingsDialogStyle());
    const auto current = entries();
    const int row = m_list->currentRow();
    loadEntries(current);
    m_list->setCurrentRow(row);
}
