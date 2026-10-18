#include "../../core/plugin/local_plugin.h"
#include "../../core/plugin/plugin_manager.h"
#include "../themed_combo_box.h"
#include "hotkey_settings_dialog.h"
#include "toolbar_settings_dialog.h"
#include "../../core/theme_manager.h"
#include "../../core/autostart_helper.h"
#include "../../core/formula/formula_ocr_manager.h"
#include <QSettings>
#include <QMessageBox>
#include <QScrollArea>
#include <QListWidget>
#include <QTabBar>
#include <QSpinBox>
#include "ai_settings_page.h"
#include "feature_scheme_widget.h"
#include "settings_switch.h"
#include "../../core/ocr/ocr_manager.h"
#include "../plugin/plugin_market_dialog.h"
#include "../toolbar/toolbar_icons.h"
#include <QPainter>
#include <QPainterPath>

namespace {
// Theme samples deliberately retain both palettes when the surrounding theme changes.
class ThemePreviewButton : public QPushButton
{
public:
    ThemePreviewButton(int mode, const QString& title, QWidget* parent)
        : QPushButton(title, parent), m_mode(mode)
    {
        setCheckable(true);
        setAccessibleName(title);
        setObjectName(QStringLiteral("themeChoice%1").arg(mode));
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(0);
        setFixedHeight(150);
        // Global QPushButton rules supply a normal control height during style polish.
        // Keep this painted preview's full geometry through theme changes as well.
        setStyleSheet("QPushButton {min-height:150px;max-height:150px;padding:0;border:0;}");
    }
    QSize sizeHint() const override { return {180,150}; }
    QSize minimumSizeHint() const override { return {0,150}; }
protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto colors = ThemeManager::instance().palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(2, 2, -2, -2);
        QColor background = isChecked() ? colors.selected : colors.well;
        if (isDown()) background = background.darker(106);
        else if (underMouse()) background = colors.hover;
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(card, 10, 10);

        const QRectF sample = card.adjusted(10, 10, -10, 0);
        const QRectF preview(sample.x(), sample.y(), sample.width(), 80);
        QPainterPath clip;
        clip.addRoundedRect(preview, 5, 5);
        painter.save();
        painter.setClipPath(clip);
        auto drawSample = [&](bool dark, const QRectF& clipRect) {
            painter.save();
            painter.setClipRect(clipRect, Qt::IntersectClip);
            painter.fillRect(preview, QColor(dark ? "#1C302A" : "#F5F8F3"));
            painter.fillRect(QRectF(preview.x(), preview.y(), preview.width() * .25, 80),
                             QColor(dark ? "#142822" : "#DCE7DF"));
            painter.setBrush(QColor(dark ? "#345346" : "#E6EDE5"));
            painter.drawRoundedRect(QRectF(preview.x() + preview.width() * .33, preview.y() + 13,
                                           preview.width() * .58, 18), 2, 2);
            painter.setBrush(QColor(dark ? "#73C6AE" : "#006B6C"));
            painter.drawRoundedRect(QRectF(preview.x() + preview.width() * .33, preview.y() + 40,
                                           preview.width() * .25, 24), 2, 2);
            painter.restore();
        };
        if (m_mode == 0) {
            drawSample(false, QRectF(preview.x(), preview.y(), preview.width() / 2, 80));
            drawSample(true, QRectF(preview.center().x(), preview.y(), preview.width() / 2, 80));
        } else drawSample(m_mode == 2, preview);
        painter.restore();

        QFont titleFont = font();
        titleFont.setPixelSize(12);
        titleFont.setWeight(QFont::Bold);
        painter.setFont(titleFont);
        const QColor ink = isChecked() ? colors.selectedText : colors.text;
        painter.setPen(ink);
        painter.drawText(QRectF(card.x() + 12, card.y() + 99, card.width() - 42, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
        if (isChecked()) {
            painter.setPen(QPen(ink,2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const QPointF origin(card.right()-28,card.y()+100);
            QPainterPath check;check.moveTo(origin+QPointF(2,8));check.lineTo(origin+QPointF(6,12));check.lineTo(origin+QPointF(14,3));
            painter.drawPath(check);
        }
        titleFont.setWeight(QFont::Normal);
        painter.setFont(titleFont);
        painter.setPen(colors.secondary);
        painter.drawText(QRectF(card.x() + 12, card.y() + 122, card.width() - 24, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, m_mode == 0 ? "随系统外观切换" : "马尔斯绿");
        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colors.focus, 2));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
        }
    }
private:
    int m_mode;
};
}

class CandidateShortcutEdit : public QLineEdit
{
public:
    explicit CandidateShortcutEdit(QWidget* parent) : QLineEdit(parent)
    {
        setReadOnly(true);
        setPlaceholderText("点击后按键或滚轮");
        setToolTip("点击输入框后按键或滚动鼠标，可组合 Ctrl / Alt / Shift；使用右侧按钮清空。");
    }
    QString binding() const { return m_binding; }
    void setBinding(const QString& binding)
    {
        m_binding = binding;
        setText(QString(binding).replace("WheelUp", "向上滚").replace("WheelDown", "向下滚"));
        m_wheelDelta = 0;
    }
protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) m_wheelDelta = 0;
        return QLineEdit::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        const int key = event->key();
        if (key != Qt::Key_Control && key != Qt::Key_Alt && key != Qt::Key_Shift && key != Qt::Key_Meta) {
            const auto mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
            setBinding(QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText));
        }
        event->accept();
    }
    void wheelEvent(QWheelEvent* event) override
    {
        if (!hasFocus() || event->angleDelta().y() == 0) { event->ignore(); return; }
        m_wheelDelta += event->angleDelta().y();
        if (std::abs(m_wheelDelta) >= 120)
            setBinding(HotkeyConfig::wheelBinding(m_wheelDelta, event->modifiers()));
        event->accept();
    }
private:
    QString m_binding;
    int m_wheelDelta = 0;
};

HotkeySettingsDialog::HotkeySettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
    setWindowTitle("设置 - AccuShot");
    setWindowIcon(appIcon());
    resize(1120, 800);
    setMinimumSize(1120, 640);

    m_initialTheme = ThemeManager::instance().themeMode();
    m_initialFontSize = ThemeManager::instance().resultFontSize();
    setupUi();
    loadFromConfig();
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_themePreviewActive = true;
        m_applyingAppearancePreview = true;
        ThemeManager::instance().setThemeMode(index == 1 ? ThemeMode::Light :
            index == 2 ? ThemeMode::Dark : ThemeMode::Auto, false);
        m_applyingAppearancePreview = false;
    });
    connect(m_resultFontSizeCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_fontSizePreviewActive = true;
        m_applyingAppearancePreview = true;
        ThemeManager::instance().setResultFontSize(m_resultFontSizeCombo->currentData().toInt(), false);
        m_applyingAppearancePreview = false;
    });
    connect(this, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted) restoreAppearancePreview();
    });
}

HotkeySettingsDialog::~HotkeySettingsDialog()
{
    restoreAppearancePreview();
}

void HotkeySettingsDialog::restoreAppearancePreview()
{
    if (m_themePreviewActive) {
        m_themePreviewActive = false;
        ThemeManager::instance().setThemeMode(m_initialTheme, false);
    }
    if (m_fontSizePreviewActive) {
        m_fontSizePreviewActive = false;
        ThemeManager::instance().setResultFontSize(m_initialFontSize, false);
    }
}

void HotkeySettingsDialog::setupUi()
{
    setStyleSheet(ThemeManager::instance().getSettingsDialogStyle());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        setStyleSheet(ThemeManager::instance().getSettingsDialogStyle());
        if (!m_applyingAppearancePreview) {
            m_initialTheme = ThemeManager::instance().themeMode();
            m_themePreviewActive = false;
        }
    });
    connect(&ThemeManager::instance(), &ThemeManager::resultFontSizeChanged, this, [this](int size) {
        setStyleSheet(ThemeManager::instance().getSettingsDialogStyle());
        if (!m_applyingAppearancePreview) {
            m_initialFontSize = size;
            m_fontSizePreviewActive = false;
        }
    });

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(216);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 26, 14, 20);
    sidebarLayout->setSpacing(26);
    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(14, 0, 0, 0);
    brandRow->setSpacing(10);
    auto* brandIcon = new QLabel(sidebar);
    brandIcon->setPixmap(appIcon().pixmap(32, 32));
    brandIcon->setFixedSize(32, 32);
    brandRow->addWidget(brandIcon);
    auto* brand = new QLabel("AccuShot", sidebar);
    brand->setObjectName("settingsBrand");
    brandRow->addWidget(brand);
    brandRow->addStretch();
    sidebarLayout->addLayout(brandRow);
    mainLayout->addWidget(sidebar);

    auto* content = new QWidget(this);
    content->setObjectName("settingsContent");
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(40, 32, 40, 28);
    contentLayout->setSpacing(24);
    auto* heading = new QVBoxLayout;
    heading->setSpacing(4);
    auto* pageTitle = new QLabel(content);
    pageTitle->setObjectName("pageTitle");
    heading->addWidget(pageTitle);
    contentLayout->addLayout(heading);
    mainLayout->addWidget(content, 1);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("settingsPages");
    m_tabWidget->addTab(createGeneralTab(), "常规与外观");
    m_tabWidget->addTab(m_capturePage, "截图与标注");
    m_tabWidget->addTab(m_recognitionPage, "公式识别");
    m_tabWidget->addTab(createHotkeysTab(), "快捷键");
    m_toolbarSettings = new ToolbarSettingsDialog(this, true);
    m_tabWidget->addTab(m_toolbarSettings, "工具栏");
    m_aiSettings = new AiSettingsPage(this);
    auto* servicesScroll=new QScrollArea(this);servicesScroll->setWidgetResizable(true);servicesScroll->setFrameShape(QFrame::NoFrame);servicesScroll->setWidget(m_aiSettings);
    m_tabWidget->addTab(servicesScroll,"在线账号");
    m_tabWidget->addTab(new PluginMarketDialog(this,true),"插件与模型");
    auto accounts=[this]{return m_aiSettings->draftProfiles();};
    auto configure=[this](const QString& id){if(id=="__plugins__")openPlugins();else {m_aiSettings->selectAccount(id);openAiServices();}};
    auto makeChoice=[this,accounts,configure](QString task,QString selected){auto* page=new FeatureSchemeWidget(task,selected,accounts,configure,this);m_featureChoices.append(page);return page;};
    auto choiceScroll=[this](FeatureSchemeWidget* page){auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(page);return scroll;};
    auto* translation=makeChoice("translation",TranslationPluginManager::instance().preferredEnginePreference());
    m_translationDefault=translation->choice;m_tabWidget->addTab(choiceScroll(translation),"翻译");
    auto* formula=makeChoice("formula",FormulaOcrManager::instance().preferredMode());m_formulaDefault=formula->choice;
    auto* formulaColumn=qobject_cast<QVBoxLayout*>(qobject_cast<QScrollArea*>(m_recognitionPage)->widget()->layout());
    formulaColumn->insertWidget(0,formula);
    auto* ocr=makeChoice("ocr",OcrManager::instance().preferredEngineType());m_ocrDefault=ocr->choice;m_tabWidget->addTab(choiceScroll(ocr),"文字识别");
    QSettings taskSettings(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");
    auto* markdown=makeChoice("markdown",taskSettings.value("Markdown/Provider","rapidocr").toString());
    m_markdownDefault=markdown->choice;m_tabWidget->addTab(choiceScroll(markdown),"Markdown");
    auto refreshEngines=[this]{for(auto* page:m_featureChoices)page->refresh();};
    connect(&PluginManager::instance(),&PluginManager::pluginsRefreshed,this,refreshEngines);
    m_tabWidget->tabBar()->hide();auto* nav=new QListWidget(sidebar);nav->setObjectName("settingsNavigation");
    nav->setFrameShape(QFrame::NoFrame);nav->setSpacing(5);nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    nav->setIconSize(QSize(18,18));
    QFont navigationFont=nav->font();navigationFont.setPixelSize(14);navigationFont.setWeight(QFont::Bold);nav->setFont(navigationFont);
    const QStringList navIcons{"theme","capture","formula","hotkey","settings","layers","plugin","translate","ocr","markdown"};
    const QList<int> order{0,1,4,8,7,2,9,5,6,3};
    for(int index:order){auto* item=new QListWidgetItem(m_tabWidget->tabText(index),nav);item->setData(Qt::UserRole,index);item->setFont(navigationFont);item->setSizeHint(QSize(178,40));}
    auto refreshIcons=[nav,navIcons]{const auto ink=ThemeManager::instance().palette().text;for(int row=0;row<nav->count();++row){auto* item=nav->item(row);item->setIcon(uiIcon(navIcons.at(item->data(Qt::UserRole).toInt()),ink));}};
    refreshIcons();connect(&ThemeManager::instance(),&ThemeManager::themeChanged,this,refreshIcons);
    connect(nav,&QListWidget::currentRowChanged,this,[this,nav,refreshEngines](int row){if(row<0)return;refreshEngines();m_tabWidget->setCurrentIndex(nav->item(row)->data(Qt::UserRole).toInt());});
    connect(m_tabWidget,&QTabWidget::currentChanged,this,[nav](int index){for(int i=0;i<nav->count();++i)if(nav->item(i)->data(Qt::UserRole).toInt()==index){QSignalBlocker block(nav);nav->setCurrentRow(i);}});
    auto updateHeading = [this, pageTitle](int index) {
        pageTitle->setText(m_tabWidget->tabText(index));
    };
    connect(m_tabWidget, &QTabWidget::currentChanged, this, updateHeading);
    nav->setCurrentRow(0);updateHeading(0);
    sidebarLayout->addWidget(nav, 1);
    auto* sidebarHint = new QLabel("修改后点击保存\n取消将恢复主题预览", sidebar);
    sidebarHint->setObjectName("secondaryText");
    sidebarHint->setContentsMargins(14, 0, 0, 0);
    sidebarLayout->addWidget(sidebarHint);
    contentLayout->addWidget(m_tabWidget, 1);

    auto* footer = new QWidget(content);
    footer->setObjectName("settingsFooter");
    auto* btnLayout = new QHBoxLayout(footer);
    btnLayout->setContentsMargins(20, 18, 20, 18);
    btnLayout->setSpacing(8);

    m_resetBtn = new QPushButton("恢复快捷键默认", this);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &HotkeySettingsDialog::resetAllDefaults);
    btnLayout->addWidget(m_resetBtn);
    m_resetBtn->hide();
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) { m_resetBtn->setVisible(index == 3); });

    btnLayout->addStretch();

    m_saveBtn = new QPushButton("保存", this);
    m_saveBtn->setObjectName("settingsSave");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        if (saveToConfig()) {
            accept();
        }
    });


    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_saveBtn);

    contentLayout->addWidget(footer);
    for (auto* button : findChildren<QPushButton*>()) button->setAutoDefault(false);
}

QWidget* HotkeySettingsDialog::createKeyRow(const QString& title, const QString& desc, QKeySequenceEdit*& editOut)
{
    editOut = new QKeySequenceEdit(this);
    editOut->setMaximumSequenceLength(1);
    return createShortcutRow(title, desc, editOut);
}

QWidget* HotkeySettingsDialog::createShortcutRow(const QString& title, const QString& desc, QWidget* edit)
{
    auto* rowWidget = new QWidget(this);
    rowWidget->setMinimumHeight(74);
    rowWidget->setObjectName("settingsRow");
    rowWidget->setProperty("shortcutSearch", title + " " + desc);
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(20, 18, 20, 18);
    rowLayout->setSpacing(24);

    auto* labelLayout = new QVBoxLayout();
    labelLayout->setSpacing(4);
    auto* titleLbl = new QLabel(title, rowWidget);
    titleLbl->setObjectName("settingName");
    labelLayout->addWidget(titleLbl);

    if (!desc.isEmpty()) {
        auto* descLbl = new QLabel(desc, rowWidget);
        descLbl->setObjectName("rowDesc");
        descLbl->setWordWrap(true);
        labelLayout->addWidget(descLbl);
    }
    rowLayout->addLayout(labelLayout, 1);

    edit->setParent(rowWidget);
    edit->setFixedWidth(150);
    edit->setAccessibleName(title);
    rowLayout->addWidget(edit);

    auto* clearBtn = new QPushButton("清空", rowWidget);
    clearBtn->setObjectName("clearBtn");
    clearBtn->setToolTip("清空此快捷键");
    clearBtn->setFixedSize(72, 36);
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, edit, [edit] {
        if (auto* keyEdit = qobject_cast<QKeySequenceEdit*>(edit)) keyEdit->clear();
        else static_cast<CandidateShortcutEdit*>(edit)->setBinding({});
    });
    rowLayout->addWidget(clearBtn);

    return rowWidget;
}

QWidget* HotkeySettingsDialog::createGeneralTab()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget();
    auto* layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    auto makeCard = [](QWidget* parent) -> QFrame* {
        auto* card = new QFrame(parent);
        card->setObjectName("settingsCard");
        card->setAttribute(Qt::WA_StyledBackground);
        return card;
    };

    // ==========================================
    // 1. 主题外观设置卡片
    // ==========================================
    auto* themeCard = makeCard(contentWidget);
    themeCard->setObjectName("settingsAppearance");
    auto* themeLayout = new QVBoxLayout(themeCard);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(14);

    auto* themeTitle = new QLabel("外观", themeCard);
    themeTitle->setObjectName("cardTitle");
    themeLayout->addWidget(themeTitle);

    m_themeCombo = new ThemedComboBox(themeCard);
    m_themeCombo->addItem("跟随系统", "Auto");
    m_themeCombo->addItem("浅色", "Light");
    m_themeCombo->addItem("深色", "Dark");
    m_themeCombo->hide();
    auto* themeChoices = new QHBoxLayout;
    themeChoices->setSpacing(12);
    for (int mode=0; mode<3; ++mode) {
        auto* choice = new ThemePreviewButton(mode, m_themeCombo->itemText(mode), themeCard);
        choice->setChecked(mode == m_themeCombo->currentIndex());
        themeChoices->addWidget(choice, 1);
        connect(choice, &QPushButton::clicked, this, [this, mode, choice] {
            m_themeCombo->setCurrentIndex(mode);
            choice->setChecked(true);
        });
        connect(m_themeCombo, &QComboBox::currentIndexChanged, choice, [choice, mode](int selected) {
            choice->setChecked(selected == mode);
        });
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, choice, [choice] { choice->update(); });
    }
    themeLayout->addLayout(themeChoices);

    auto* themeDesc = new QLabel("切换后即时预览；取消将恢复原外观。", themeCard);
    themeDesc->setObjectName("rowDesc");
    themeDesc->setWordWrap(true);
    themeLayout->addWidget(themeDesc);

    auto* typographyTitle = new QLabel("文字", themeCard);
    typographyTitle->setObjectName("cardTitle");
    themeLayout->addSpacing(10);
    themeLayout->addWidget(typographyTitle);
    auto* typographyBlock = makeCard(themeCard);
    auto* typographyLayout = new QVBoxLayout(typographyBlock);
    typographyLayout->setContentsMargins(20,18,20,18);
    typographyLayout->setSpacing(12);
    auto* fontRow = new QHBoxLayout;
    fontRow->setSpacing(24);
    fontRow->addWidget(new QLabel("结果正文字号", typographyBlock), 1);
    m_resultFontSizeCombo = new ThemedComboBox(typographyBlock);
    m_resultFontSizeCombo->setObjectName("resultFontSize");
    m_resultFontSizeCombo->setAccessibleName("结果正文字号");
    m_resultFontSizeCombo->setFixedWidth(150);
    for (int size : {14, 15, 16, 18, 20}) m_resultFontSizeCombo->addItem(QString::number(size) + " px", size);
    fontRow->addWidget(m_resultFontSizeCombo);
    typographyLayout->addLayout(fontRow);

    auto* bodyPreview = new QLabel("原文与译文使用相同的正文字号。\nSource and translation use the same text size.", typographyBlock);
    bodyPreview->setObjectName("resultFontPreview");
    bodyPreview->setWordWrap(true);
    bodyPreview->setContentsMargins(20,18,20,18);
    typographyLayout->addWidget(bodyPreview);
    auto* fontDesc = new QLabel("中文 Microsoft YaHei UI · 英文 Segoe UI\n辅助说明 12 px · 控件与导航 14 px", typographyBlock);
    fontDesc->setObjectName("rowDesc");
    fontDesc->setWordWrap(true);
    typographyLayout->addWidget(fontDesc);
    themeLayout->addWidget(typographyBlock);

    layout->addWidget(themeCard);

    // ==========================================
    // 2. 截屏辅助与智能吸附卡片
    // ==========================================
    auto* snapCard = makeCard(contentWidget);
    auto* snapLayout = new QVBoxLayout(snapCard);
    snapLayout->setContentsMargins(20, 18, 20, 18);
    snapLayout->setSpacing(18);

    auto* snapTitle = new QLabel("选区", snapCard);
    snapTitle->setObjectName("cardTitle");
    snapLayout->addWidget(snapTitle);

    m_enableSmartSnappingCheck = new SettingsSwitch("启用自动吸附", snapCard);
    m_enableSmartSnappingCheck->setToolTip("开启后鼠标在屏幕上移动时会自动识别窗口与控件轮廓，可按 Tab 切换候选框；关闭后完全自由拖拽框选。");
    snapLayout->addWidget(m_enableSmartSnappingCheck);
    m_snapMode=new ThemedComboBox(snapCard);m_snapMode->setObjectName("snapMode");m_snapMode->addItem("窗口（默认）","Window");m_snapMode->addItem("元素与控件","Element");m_snapMode->addItem("图像轮廓","Contour");m_snapMode->addItem("智能融合（窗口+元素+轮廓）","Hybrid");snapLayout->addWidget(m_snapMode);
    connect(m_enableSmartSnappingCheck,&QCheckBox::toggled,m_snapMode,&QWidget::setEnabled);

    auto* resizeRow = new QHBoxLayout;
    resizeRow->addWidget(new QLabel("候选框缩放步长", snapCard), 1);
    m_candidateResizeStep = new QSpinBox(snapCard);
    m_candidateResizeStep->setObjectName("candidateResizeStep");
    m_candidateResizeStep->setAccessibleName("候选框缩放步长");
    m_candidateResizeStep->setRange(1, 50);
    m_candidateResizeStep->setSuffix(" 逻辑像素");
    resizeRow->addWidget(m_candidateResizeStep);
    resizeRow->setSpacing(24);
    m_candidateResizeStep->setFixedWidth(150);
    snapLayout->addLayout(resizeRow);
    auto* resizeDesc = new QLabel("每档四条边各移动此步长；默认 Ctrl + 向下滚缩小，Ctrl + 向上滚放大，可在快捷键中修改。", snapCard);
    resizeDesc->setObjectName("rowDesc");
    resizeDesc->setWordWrap(true);
    snapLayout->addWidget(resizeDesc);

    auto* snapDesc = new QLabel("提示：若在复杂密集网页或表格中拖拽框选时感觉吸附过于灵敏，可在此取消勾选关闭吸附。", snapCard);
    snapDesc->setObjectName("rowDesc");
    snapDesc->setWordWrap(true);
    snapLayout->addWidget(snapDesc);

    layout->addWidget(snapCard);

    // ==========================================
    // 3. 系统自启动偏好卡片
    // ==========================================
    auto* bootCard = makeCard(contentWidget);
    auto* bootLayout = new QVBoxLayout(bootCard);
    bootLayout->setContentsMargins(20, 18, 20, 18);
    bootLayout->setSpacing(8);

    auto* bootTitle = new QLabel("启动", bootCard);
    bootTitle->setObjectName("cardTitle");
    bootLayout->addWidget(bootTitle);

    m_autoStartCheck = new SettingsSwitch("登录 Windows 后启动 AccuShot", bootCard);
    m_autoStartCheck->setToolTip("在系统开机登录后自动启动 AccuShot 并常驻系统托盘");
    bootLayout->addWidget(m_autoStartCheck);

    auto* bootDesc = new QLabel("启动后常驻托盘，可使用快捷键截图。", bootCard);
    bootDesc->setObjectName("rowDesc");
    bootDesc->setWordWrap(true);
    bootLayout->addWidget(bootDesc);

    layout->addWidget(bootCard);

    // ==========================================
    // 4. 文字标注换行与回车确认卡片
    // ==========================================
    auto* annotCard = makeCard(contentWidget);
    auto* annotLayout = new QVBoxLayout(annotCard);
    annotLayout->setContentsMargins(20, 18, 20, 18);
    annotLayout->setSpacing(18);

    auto* annotTitle = new QLabel("文字标注", annotCard);
    annotTitle->setObjectName("cardTitle");
    annotLayout->addWidget(annotTitle);

    auto* commitLayout = new QHBoxLayout();
    commitLayout->setSpacing(24);
    auto* commitLbl = new QLabel("回车确认与换行方式：", annotCard);
    commitLayout->addWidget(commitLbl);

    m_textCommitModeCombo = new ThemedComboBox(annotCard);
    m_textCommitModeCombo->addItem("Enter 提交，Shift+Enter 换行 (默认)", 0);
    m_textCommitModeCombo->addItem("Ctrl+Enter 提交，Enter 换行", 1);
    commitLayout->addWidget(m_textCommitModeCombo, 1);
    annotLayout->addLayout(commitLayout);

    auto* commitDesc = new QLabel("文字标注支持多行动态排版，可在此根据个人输入习惯自选换行与提交规则。", annotCard);
    commitDesc->setObjectName("rowDesc");
    commitDesc->setWordWrap(true);
    annotLayout->addWidget(commitDesc);

    layout->addWidget(annotCard);

    // ==========================================
    // 5. 数学公式识别云端授权卡片
    // ==========================================
    auto* formulaCard = makeCard(contentWidget);
    auto* formulaLayout = new QVBoxLayout(formulaCard);
    formulaLayout->setContentsMargins(20, 18, 20, 18);
    formulaLayout->setSpacing(18);

    auto* formulaTitle = new QLabel("公式识别", formulaCard);
    formulaTitle->setObjectName("cardTitle");
    formulaLayout->addWidget(formulaTitle);

    auto* tokenLayout = new QHBoxLayout();
    tokenLayout->setSpacing(24);
    auto* tokenLbl = new QLabel("SimpleTeX Token：", formulaCard);
    tokenLayout->addWidget(tokenLbl);

    m_simpleTexTokenEdit = new QLineEdit(formulaCard);
    m_simpleTexTokenEdit->setPlaceholderText("输入 SimpleTeX Token");
    m_simpleTexTokenEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    tokenLayout->addWidget(m_simpleTexTokenEdit, 1);
    formulaLayout->addLayout(tokenLayout);

    auto* tokenDesc = new QLabel("Token 用于在线公式识别；本地识别需要安装离线插件。", formulaCard);
    tokenDesc->setObjectName("rowDesc");
    tokenDesc->setWordWrap(true);
    formulaLayout->addWidget(tokenDesc);

    layout->addWidget(formulaCard);

    auto makePage = [this](const QList<QWidget*>& controls) {
        auto* page = new QScrollArea(this);
        page->setWidgetResizable(true);
        page->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget(page);
        auto* column = new QVBoxLayout(content);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(24);
        for (auto* control : controls) column->addWidget(control);
        column->addStretch(); page->setWidget(content); return page;
    };
    layout->removeWidget(snapCard); layout->removeWidget(annotCard); layout->removeWidget(formulaCard);
    m_capturePage = makePage({snapCard, annotCard});
    m_recognitionPage = makePage({formulaCard});
    auto* engineHint = new QLabel("文字识别和翻译服务可在结果窗口切换；离线模型在插件中心安装。", formulaCard);
    engineHint->setWordWrap(true); engineHint->setObjectName("rowDesc");
    formulaLayout->addWidget(engineHint);
    m_themeCombo->setObjectName("themeMode");
    m_enableSmartSnappingCheck->setObjectName("smartSnapping");
    m_textCommitModeCombo->setObjectName("textCommitMode");
    m_simpleTexTokenEdit->setObjectName("formulaToken");
    m_autoStartCheck->setObjectName("autoStart");
    layout->addStretch();
    scroll->setWidget(contentWidget);
    return scroll;
}

QWidget* HotkeySettingsDialog::createHotkeysTab()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget();
    auto* layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* search = new QLineEdit(contentWidget);
    search->setObjectName("shortcutSearch");
    search->setPlaceholderText("搜索快捷键操作");
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, [contentWidget](const QString& query) {
        for (auto* row : contentWidget->findChildren<QWidget*>()) {
            const auto text = row->property("shortcutSearch");
            if (text.isValid()) row->setVisible(text.toString().contains(query.trimmed(), Qt::CaseInsensitive));
        }
    });

    auto makeSection = [contentWidget,layout](const QString& title) {
        auto* lbl = new QLabel(title, contentWidget);
        lbl->setObjectName("cardTitle");
        layout->addWidget(lbl);
        auto* block = new QFrame(contentWidget);
        block->setObjectName("settingsCard");
        block->setAttribute(Qt::WA_StyledBackground);
        auto* rows = new QVBoxLayout(block);
        rows->setContentsMargins(0,0,0,0);
        rows->setSpacing(0);
        layout->addWidget(block);
        return rows;
    };

    // 1. 全局热键
    auto* section = makeSection("全局快捷键");
    section->addWidget(createKeyRow("开始截屏", "唤醒全景截屏覆盖层", m_globalSnippingEdit));
    section->addWidget(createKeyRow("桌面贴图", "剪贴板图像或选区置顶贴图", m_globalPinEdit));

    layout->addSpacing(4);

    // 2. 截图操作
    section = makeSection("截图操作");
    section->addWidget(createKeyRow("撤销标注", "撤销上一步绘制的标注图元", m_snippingUndoEdit));
    section->addWidget(createKeyRow("选区贴图", "将当前选区转为置顶贴图", m_snippingPinEdit));
    section->addWidget(createKeyRow("完成复制", "复制截图内容到剪贴板并退出", m_snippingConfirmEdit));
    section->addWidget(createKeyRow("保存截图", "另存为图片文件 (支持 WebP/PNG/JPG)", m_snippingSaveEdit));
    section->addWidget(createKeyRow("文字识别", "提取选区中的文字内容", m_snippingOcrEdit));
    section->addWidget(createKeyRow("取消截屏", "放弃当前截屏并退出", m_snippingCancelEdit));
    m_smallerCandidateEdit = new CandidateShortcutEdit(this);
    m_smallerCandidateEdit->setObjectName("smallerCandidateShortcut");
    m_largerCandidateEdit = new CandidateShortcutEdit(this);
    m_largerCandidateEdit->setObjectName("largerCandidateShortcut");
    section->addWidget(createShortcutRow("选择面积更小的候选", "确定选区前切换；默认向下滚，可录制按键或滚轮", m_smallerCandidateEdit));
    section->addWidget(createShortcutRow("选择面积更大的候选", "确定选区前切换；默认向上滚，可录制按键或滚轮", m_largerCandidateEdit));
    m_shrinkCandidateEdit = new CandidateShortcutEdit(this);
    m_shrinkCandidateEdit->setObjectName("shrinkCandidateShortcut");
    m_expandCandidateEdit = new CandidateShortcutEdit(this);
    m_expandCandidateEdit->setObjectName("expandCandidateShortcut");
    section->addWidget(createShortcutRow("缩小当前候选框", "四边向内微调；默认 Ctrl + 向下滚，步长在截图与标注中设置", m_shrinkCandidateEdit));
    section->addWidget(createShortcutRow("放大当前候选框", "四边向外微调；默认 Ctrl + 向上滚，步长在截图与标注中设置", m_expandCandidateEdit));

    layout->addSpacing(4);

    // 3. 标注工具
    section = makeSection("标注工具");
    section->addWidget(createKeyRow("矩形框", "绘制空心/实心矩形框", m_toolRectEdit));
    section->addWidget(createKeyRow("箭头", "绘制指示箭头矢量", m_toolArrowEdit));
    section->addWidget(createKeyRow("画笔", "自由涂鸦笔迹", m_toolPencilEdit));
    section->addWidget(createKeyRow("文字", "富文本原地输入标注", m_toolTextEdit));
    section->addWidget(createKeyRow("马赛克", "局部打码脱敏遮罩", m_toolMosaicEdit));
    section->addWidget(createKeyRow("单字修改", "字符就地改字与P图", m_toolCharEdit));

    layout->addSpacing(4);

    // 4. 贴图窗口
    section = makeSection("贴图窗口");
    section->addWidget(createKeyRow("文字识别", "识别当前贴图窗口中的文字", m_pinOcrEdit));
    section->addWidget(createKeyRow("复制图片", "复制贴图到系统剪贴板", m_pinCopyEdit));
    section->addWidget(createKeyRow("保存图片", "保存贴图为独立图像文件", m_pinSaveEdit));
    section->addWidget(createKeyRow("关闭贴图", "关闭销毁当前置顶贴图窗口", m_pinCloseEdit));

    layout->addStretch();
    scroll->setWidget(contentWidget);
    return scroll;
}

void HotkeySettingsDialog::loadFromConfig()
{

    // 通用与外观设置
    ThemeMode curTheme = ThemeManager::instance().themeMode();
    if (curTheme == ThemeMode::Light) m_themeCombo->setCurrentIndex(1);
    else if (curTheme == ThemeMode::Dark) m_themeCombo->setCurrentIndex(2);
    else m_themeCombo->setCurrentIndex(0);
    m_resultFontSizeCombo->setCurrentIndex(m_resultFontSizeCombo->findData(ThemeManager::instance().resultFontSize()));

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    bool enableSnap = settings.value("Snapping/EnableSmartSnapping", true).toBool();
    m_enableSmartSnappingCheck->setChecked(enableSnap);
    m_snapMode->setCurrentIndex(qMax(0,m_snapMode->findData(settings.value("Snapping/Mode","Window"))));
    m_snapMode->setEnabled(enableSnap);

    if (m_autoStartCheck) {
        m_initialAutoStart = AutoStartHelper::isAutoStartEnabled();
        m_autoStartCheck->setChecked(m_initialAutoStart);
    }

    if (m_textCommitModeCombo) {
        int textMode = settings.value("Annotation/TextCommitMode", 0).toInt();
        int idx = m_textCommitModeCombo->findData(textMode);
        if (idx >= 0) m_textCommitModeCombo->setCurrentIndex(idx);
    }

    if (m_simpleTexTokenEdit) {
        m_simpleTexTokenEdit->setText(FormulaOcrManager::instance().simpleTexToken());
    }

    loadHotkeys(HotkeyConfig::instance().data());
}

void HotkeySettingsDialog::loadHotkeys(const HotkeyConfigData& c)
{
    // 全局热键
    m_globalSnippingEdit->setKeySequence(QKeySequence(c.globalSnipping));
    m_globalPinEdit->setKeySequence(QKeySequence(c.globalPin));

    // 截屏操作
    m_snippingUndoEdit->setKeySequence(QKeySequence(c.snippingUndo));
    m_snippingPinEdit->setKeySequence(QKeySequence(c.snippingPin));
    m_snippingConfirmEdit->setKeySequence(QKeySequence(c.snippingConfirm));
    m_snippingSaveEdit->setKeySequence(QKeySequence(c.snippingSave));
    m_snippingOcrEdit->setKeySequence(QKeySequence(c.snippingOcr));
    m_snippingCancelEdit->setKeySequence(QKeySequence(c.snippingCancel));
    m_smallerCandidateEdit->setBinding(c.snippingSmallerCandidate);
    m_largerCandidateEdit->setBinding(c.snippingLargerCandidate);
    m_shrinkCandidateEdit->setBinding(c.snippingShrinkCandidate);
    m_expandCandidateEdit->setBinding(c.snippingExpandCandidate);
    m_candidateResizeStep->setValue(c.snippingCandidateResizeStep);

    // 标注工具
    m_toolRectEdit->setKeySequence(QKeySequence(c.toolRect));
    m_toolArrowEdit->setKeySequence(QKeySequence(c.toolArrow));
    m_toolPencilEdit->setKeySequence(QKeySequence(c.toolPencil));
    m_toolTextEdit->setKeySequence(QKeySequence(c.toolText));
    m_toolMosaicEdit->setKeySequence(QKeySequence(c.toolMosaic));
    m_toolCharEdit->setKeySequence(QKeySequence(c.toolCharEdit));

    // 贴图窗口
    m_pinCloseEdit->setKeySequence(QKeySequence(c.pinClose));
    m_pinOcrEdit->setKeySequence(QKeySequence(c.pinOcr));
    m_pinCopyEdit->setKeySequence(QKeySequence(c.pinCopy));
    m_pinSaveEdit->setKeySequence(QKeySequence(c.pinSave));
}

bool HotkeySettingsDialog::saveToConfig()
{
    QString globalSnip = m_globalSnippingEdit->keySequence().toString();
    QString globalPin  = m_globalPinEdit->keySequence().toString();

    // 全局截屏快捷键不能为空
    if (globalSnip.trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "全局截屏快捷键不能为空，请录制有效的快捷键。");
        m_tabWidget->setCurrentIndex(3);
        m_globalSnippingEdit->setFocus();
        return false;
    }

    // 全局快捷键冲突校验
    if (!globalPin.trimmed().isEmpty() && globalPin.compare(globalSnip, Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, "快捷键冲突", "全局截屏与全局贴图快捷键不能相同，请调整后重新保存。");
        m_tabWidget->setCurrentIndex(3);
        m_globalPinEdit->setFocus();
        return false;
    }

    // Only keys in the same interaction scope conflict; screenshot tools share the screenshot scope.
    const QList<QList<QWidget*>> scopes = {
        {m_globalSnippingEdit, m_globalPinEdit},
        {m_snippingUndoEdit, m_snippingPinEdit, m_snippingConfirmEdit, m_snippingSaveEdit,
         m_snippingOcrEdit, m_snippingCancelEdit, m_toolRectEdit, m_toolArrowEdit,
         m_toolPencilEdit, m_toolTextEdit, m_toolMosaicEdit, m_toolCharEdit,
         m_smallerCandidateEdit, m_largerCandidateEdit, m_shrinkCandidateEdit, m_expandCandidateEdit},
        {m_pinCloseEdit, m_pinOcrEdit, m_pinCopyEdit, m_pinSaveEdit}
    };
    for (const auto& scope : scopes) {
        QMap<QString, QString> used;
        for (auto* edit : scope) {
            auto* keyEdit = qobject_cast<QKeySequenceEdit*>(edit);
            auto key = keyEdit ? keyEdit->keySequence().toString(QKeySequence::PortableText)
                               : static_cast<CandidateShortcutEdit*>(edit)->binding();
            // Return and numpad Enter trigger the same screenshot action.
            if (key == "Enter" || key.endsWith("+Enter")) key.replace("Enter", "Return");
            if (key.isEmpty()) continue;
            if (used.contains(key)) {
                m_tabWidget->setCurrentIndex(3);
                QMessageBox::warning(this, "快捷键冲突", QString("%1 与 %2 使用了同一快捷键：%3")
                    .arg(used.value(key), edit->accessibleName(), key));
                edit->setFocus(); return false;
            }
            used.insert(key, edit->accessibleName());
        }
    }

    QString serviceError;
    if(!m_aiSettings->save(&serviceError)){QMessageBox::warning(this,"服务配置",serviceError);return false;}
    for(auto* page:m_featureChoices)page->save();
    QSettings(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan").setValue("Markdown/Provider",m_markdownDefault->currentData());
    OcrManager::instance().setPreferredEngineType(m_ocrDefault->currentData().toString());
    TranslationPluginManager::instance().setPreferredEnginePreference(m_translationDefault->currentData().toString());
    FormulaOcrManager::instance().setPreferredMode(m_formulaDefault->currentData().toString());

    // 保存外观模式
    int themeIdx = m_themeCombo->currentIndex();
    ThemeMode selectedMode = (themeIdx == 1) ? ThemeMode::Light :
                             (themeIdx == 2) ? ThemeMode::Dark : ThemeMode::Auto;
    ThemeManager::instance().setThemeMode(selectedMode);
    ThemeManager::instance().setResultFontSize(m_resultFontSizeCombo->currentData().toInt());

    // 保存智能吸附开关
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    settings.setValue("Snapping/EnableSmartSnapping", m_enableSmartSnappingCheck->isChecked());
    settings.setValue("Snapping/Mode",m_snapMode->currentData());

    // 保存开机自启动设置
    if (m_autoStartCheck->isChecked() != m_initialAutoStart) {
        AutoStartHelper::setAutoStartEnabled(m_autoStartCheck->isChecked());
    }

    // 保存文字标注换行提交偏好
    if (m_textCommitModeCombo) {
        settings.setValue("Annotation/TextCommitMode", m_textCommitModeCombo->currentData().toInt());
    }

    // 保存 SimpleTeX 公式识别授权 Token
    if (m_simpleTexTokenEdit) {
        FormulaOcrManager::instance().setSimpleTexToken(m_simpleTexTokenEdit->text());
    }

    HotkeyConfigData d;
    d.globalSnipping = globalSnip;
    d.globalPin      = globalPin;

    d.snippingUndo    = m_snippingUndoEdit->keySequence().toString();
    d.snippingPin     = m_snippingPinEdit->keySequence().toString();
    d.snippingConfirm = m_snippingConfirmEdit->keySequence().toString();
    d.snippingSave    = m_snippingSaveEdit->keySequence().toString();
    d.snippingOcr     = m_snippingOcrEdit->keySequence().toString();
    d.snippingCancel  = m_snippingCancelEdit->keySequence().toString();
    d.snippingSmallerCandidate = m_smallerCandidateEdit->binding();
    d.snippingLargerCandidate = m_largerCandidateEdit->binding();
    d.snippingShrinkCandidate = m_shrinkCandidateEdit->binding();
    d.snippingExpandCandidate = m_expandCandidateEdit->binding();
    d.snippingCandidateResizeStep = m_candidateResizeStep->value();

    d.toolRect   = m_toolRectEdit->keySequence().toString();
    d.toolArrow  = m_toolArrowEdit->keySequence().toString();
    d.toolPencil = m_toolPencilEdit->keySequence().toString();
    d.toolText   = m_toolTextEdit->keySequence().toString();
    d.toolMosaic = m_toolMosaicEdit->keySequence().toString();
    d.toolCharEdit = m_toolCharEdit->keySequence().toString();

    d.pinClose = m_pinCloseEdit->keySequence().toString();
    d.pinOcr   = m_pinOcrEdit->keySequence().toString();
    d.pinCopy  = m_pinCopyEdit->keySequence().toString();
    d.pinSave  = m_pinSaveEdit->keySequence().toString();

    ToolbarConfig::instance().setEntries(m_toolbarSettings->entries());
    HotkeyConfig::instance().setData(d);
    m_themePreviewActive = false;
    m_fontSizePreviewActive = false;
    emit hotkeysSaved();
    return true;
}

void HotkeySettingsDialog::resetAllDefaults()
{
    loadHotkeys(HotkeyConfigData{});
}

void HotkeySettingsDialog::openPlugins() { m_tabWidget->setCurrentIndex(6); }
void HotkeySettingsDialog::openAiServices() { m_tabWidget->setCurrentIndex(5); }

void HotkeySettingsDialog::openMarkdown() { m_tabWidget->setCurrentIndex(9); }
