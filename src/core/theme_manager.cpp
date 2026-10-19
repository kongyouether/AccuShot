#include "theme_manager.h"
#include <QSettings>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QApplication>
#include <QWidget>
#include <QEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

ThemeManager::ThemeManager(QObject* parent) : QObject(parent)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    const QString stored = settings.value("Theme/Mode", "Auto").toString();
    m_themeMode = stored == "Light" ? ThemeMode::Light : stored == "Dark" ? ThemeMode::Dark : ThemeMode::Auto;
    const int fontSize = settings.value("Appearance/ResultFontSize", 15).toInt();
    if (QList<int>{14, 15, 16, 18, 20}.contains(fontSize)) m_resultFontSize = fontSize;
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_themeMode == ThemeMode::Auto) emit themeChanged(isDarkMode());
    });
    qApp->installEventFilter(this);
    connect(this,&ThemeManager::themeChanged,this,[this] {
        for(auto* widget:QApplication::topLevelWidgets()) updateWindowFrame(widget);
    });
}

bool ThemeManager::eventFilter(QObject* watched,QEvent* event)
{
    if(event->type()==QEvent::Show || event->type()==QEvent::WinIdChange)
        if(auto* widget=qobject_cast<QWidget*>(watched)) updateWindowFrame(widget);
    return QObject::eventFilter(watched,event);
}

void ThemeManager::updateWindowFrame(QWidget* widget) const
{
#ifdef Q_OS_WIN
    // Keep native caption buttons and dragging; theme only decorated top-level windows.
    if(!widget->isWindow() || !widget->internalWinId()
        || widget->windowFlags().testFlag(Qt::FramelessWindowHint)
        || !widget->windowFlags().testFlag(Qt::WindowTitleHint)) return;
    const HWND handle=reinterpret_cast<HWND>(widget->internalWinId());
    const auto colors=palette();
    const BOOL dark=isDarkMode();
    const COLORREF caption=RGB(colors.surface.red(),colors.surface.green(),colors.surface.blue());
    const COLORREF text=RGB(colors.text.red(),colors.text.green(),colors.text.blue());
    DwmSetWindowAttribute(handle,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DwmSetWindowAttribute(handle,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(handle,DWMWA_TEXT_COLOR,&text,sizeof(text));
#else
    Q_UNUSED(widget);
#endif
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

void ThemeManager::setThemeMode(ThemeMode mode, bool persist)
{
    const bool changed = m_themeMode != mode;
    m_themeMode = mode;
    if (persist) {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
        settings.setValue("Theme/Mode", mode == ThemeMode::Light ? "Light" : mode == ThemeMode::Dark ? "Dark" : "Auto");
    }
    if (changed) emit themeChanged(isDarkMode());
}

bool ThemeManager::isDarkMode() const
{
    if (m_themeMode == ThemeMode::Light) return false;
    if (m_themeMode == ThemeMode::Dark) return true;
    return detectSystemIsDark();
}

void ThemeManager::setResultFontSize(int size, bool persist)
{
    if (!QList<int>{14, 15, 16, 18, 20}.contains(size)) return;
    const bool changed = m_resultFontSize != size;
    m_resultFontSize = size;
    if (persist) {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
        settings.setValue("Appearance/ResultFontSize", size);
    }
    if (changed) emit resultFontSizeChanged(size);
}

bool ThemeManager::detectSystemIsDark() const
{
#ifdef Q_OS_WIN
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(value);
        const auto status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS) return value == 0;
    }
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

UiPalette ThemeManager::palette() const
{
    UiPalette p;
    if (isDarkMode()) {
        p.page = QColor("#101E1B"); p.sidebar = QColor("#142822");
        p.surface = QColor("#1C302A"); p.raised = QColor("#263E35"); p.well = QColor("#152720");
        p.text = QColor("#E4F1E8"); p.secondary = QColor("#ADC4B5");
        p.brand = p.action = QColor("#73C6AE"); p.onAction = QColor("#102D25");
        p.hover = QColor("#345346"); p.selected = QColor("#2C5143"); p.selectedText = QColor("#C4F2DE");
        p.focus = QColor("#8FD8C1"); p.warning = QColor("#F1D59B"); p.warningBg = QColor("#483B24");
        p.success = QColor("#91D4AB"); p.danger = QColor("#F2A1A1");
    } else {
        p.page = QColor("#E7EEE9"); p.sidebar = QColor("#DCE7DF");
        p.surface = QColor("#F5F8F3"); p.raised = QColor("#FCFDF9"); p.well = QColor("#E6EDE5");
        p.text = QColor("#1F3932"); p.secondary = QColor("#53685E");
        p.brand = p.action = QColor("#006B6C"); p.onAction = QColor("#F3FAF5");
        p.hover = QColor("#D5E4DC"); p.selected = QColor("#CDE6DB"); p.selectedText = QColor("#005455");
        p.focus = QColor("#006B6C"); p.warning = QColor("#684413"); p.warningBg = QColor("#EEE1C5");
        p.success = QColor("#287044"); p.danger = QColor("#AD3535");
    }
    // Compatibility roles for painted auxiliary widgets; panel separation uses fills.
    p.border = p.hover;
    p.controlBorder = p.secondary;
    p.pressed = p.hover.darker(106);
    p.disabled = QColor::fromRgbF(p.text.redF() * .45 + p.surface.redF() * .55,
                                 p.text.greenF() * .45 + p.surface.greenF() * .55,
                                 p.text.blueF() * .45 + p.surface.blueF() * .55);
    return p;
}

namespace {
QString styled(QString css, const UiPalette& p)
{
    const QPair<const char*, QColor> colors[] = {
        {"page",p.page},{"sidebar",p.sidebar},{"surface",p.surface},{"raised",p.raised},{"well",p.well},
        {"text",p.text},{"secondary",p.secondary},{"brand",p.brand},{"action",p.action},{"onAction",p.onAction},
        {"border",p.border},{"control",p.controlBorder},{"hover",p.hover},{"pressed",p.pressed},
        {"selected",p.selected},{"selectedText",p.selectedText},{"focus",p.focus},{"disabled",p.disabled},
        {"success",p.success},{"warning",p.warning},{"warningBg",p.warningBg},{"danger",p.danger},
        {"actionHover",p.action.lighter(107)},{"actionPressed",p.action.darker(106)}
    };
    for (const auto& entry : colors) css.replace(QString("@%1@").arg(entry.first),entry.second.name());
    return css;
}
}

QString ThemeManager::getTrayMenuStyle() const {
    return getPinMenuStyle() + styled(R"(
        QMenu {min-width:224px;padding:8px;border-radius:12px;}
        QMenu::item {padding:8px 12px;min-height:20px;border-radius:6px;}
        QMenu::icon {width:18px;height:18px;margin-left:4px;}
        QMenu::separator {height:8px;margin:0 12px;background:transparent;}
    )",palette());
}

QString ThemeManager::getPinMenuStyle() const
{
    return styled(R"(
        QMenu {font-family:'Segoe UI','Microsoft YaHei UI';font-size:14px;background:@raised@;color:@text@;border:0;border-radius:9px;padding:6px;}
        QMenu::item {padding:8px 26px 8px 12px;min-height:20px;border-radius:6px;}
        QMenu::item:selected:enabled {background:@selected@;color:@selectedText@;}
        QMenu::item:disabled {color:@disabled@;}
        QMenu::separator {height:8px;background:transparent;margin:0 8px;}
        QToolTip {font-family:'Segoe UI','Microsoft YaHei UI';font-size:12px;background:@raised@;color:@text@;border:0;border-radius:6px;padding:6px 8px;}
    )",palette());
}

QString ThemeManager::getControlStyle() const
{
    QString css = styled(R"(
        QWidget {font-family:'Segoe UI','Microsoft YaHei UI';font-size:14px;font-weight:400;color:@text@;}
        QComboBox,QSpinBox,QDoubleSpinBox {background:@surface@;color:@text@;border:2px solid transparent;border-radius:7px;padding:6px 26px 6px 10px;min-height:20px;selection-background-color:@selected@;selection-color:@selectedText@;}
        QComboBox:hover:enabled,QSpinBox:hover:enabled,QDoubleSpinBox:hover:enabled {background:@hover@;}
        QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus {border-color:@focus@;}
        QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled {color:@disabled@;background:@well@;}
        QComboBox::drop-down {subcontrol-origin:padding;subcontrol-position:top right;width:26px;border:0;background:transparent;}
        QComboBox::down-arrow {image:url(:/ui/down-@variant@.png);width:16px;height:16px;}
        QComboBox QFrame {background:@raised@;border:0;}
        QComboBox QAbstractItemView {background:@raised@;color:@text@;border:0;padding:6px;outline:0;selection-background-color:@selected@;selection-color:@selectedText@;}
        QComboBox QAbstractItemView::item {min-height:20px;padding:8px 10px;border-radius:6px;}
        QComboBox QAbstractItemView::item:hover:enabled {background:@hover@;}
        QComboBox QAbstractItemView::item:selected:enabled {background:@selected@;color:@selectedText@;}
        QSpinBox::up-button,QSpinBox::down-button {subcontrol-origin:border;width:24px;border:0;background:transparent;}
        QSpinBox::up-button {subcontrol-position:top right;} QSpinBox::down-button {subcontrol-position:bottom right;}
        QSpinBox::up-button:hover,QSpinBox::down-button:hover {background:@hover@;}
        QSpinBox::up-button:pressed,QSpinBox::down-button:pressed {background:@pressed@;}
        QSpinBox::up-arrow {image:url(:/ui/up-@variant@.png);width:12px;height:12px;}
        QSpinBox::down-arrow {image:url(:/ui/down-@variant@.png);width:12px;height:12px;}
        QCheckBox {color:@text@;spacing:8px;min-height:28px;}
        QCheckBox::indicator {width:16px;height:16px;border:2px solid transparent;border-radius:5px;background:@well@;}
        QCheckBox::indicator:checked {background:@action@;image:url(:/ui/check-@checkVariant@.png);}
        QCheckBox::indicator:hover {background:@hover@;}
        QCheckBox::indicator:checked:hover {background:@actionHover@;}
        QCheckBox:focus::indicator {border-color:@focus@;}
        QCheckBox:disabled {color:@disabled@;} QCheckBox::indicator:disabled {background:@page@;}
        QProgressBar {background:@well@;color:@text@;border:0;border-radius:4px;text-align:center;}
        QProgressBar::chunk {background:@action@;border-radius:3px;}
        QWidget#pluginCard,QWidget#pillRoot,QWidget#barContainer,QWidget#charStyleBar {background:@well@;border:0;border-radius:12px;}
        QPushButton#dangerAction {color:@danger@;}
        QPushButton#recBtn {background:@danger@;color:@page@;}
        QPushButton#saveBtn,QPushButton#stopBtn {background:@action@;color:@onAction@;font-weight:700;}
        QPushButton#saveBtn:hover:enabled,QPushButton#stopBtn:hover:enabled {background:@actionHover@;}
        QPushButton#saveBtn:pressed:enabled,QPushButton#stopBtn:pressed:enabled {background:@actionPressed@;}
        QPushButton#recBtn:hover:enabled {background:@pressed@;color:@danger@;}
        QPushButton#recBtn:pressed:enabled {background:@well@;color:@danger@;}
        QPushButton#saveBtn:disabled,QPushButton#stopBtn:disabled,QPushButton#recBtn:disabled {background:@well@;color:@disabled@;}
    )",palette());
    css.replace("@variant@",isDarkMode()?"dark":"light");
    css.replace("@checkVariant@",isDarkMode()?"light":"white");
    return css;
}

QString ThemeManager::getFloatingToolbarStyle() const
{
    return getPinMenuStyle()+styled(R"(
        QToolButton {background:transparent;color:@text@;border:2px solid transparent;border-radius:6px;padding:0;}
        QToolButton:hover:enabled {background:@hover@;}
        QToolButton:checked {background:@selected@;color:@selectedText@;}
        QToolButton:pressed:enabled {background:@pressed@;}
        QToolButton:focus {border-color:@focus@;}
        QToolButton:disabled {color:@disabled@;}
        QToolButton#tool_confirm {background:@action@;color:@onAction@;}
        QToolButton#tool_confirm:hover:enabled {background:@actionHover@;}
        QToolButton#toolbarGrip {color:@secondary@;}
    )",palette());
}

QString ThemeManager::getSettingsDialogStyle() const { return getResultDialogStyle(); }

QString ThemeManager::getApplicationStyle() const
{
    return getPinMenuStyle()+styled(R"(
        QMessageBox {background:@surface@;color:@text@;font-family:'Segoe UI','Microsoft YaHei UI';font-size:14px;}
        QMessageBox QLabel {color:@text@;background:transparent;}
        QMessageBox QPushButton {background:@well@;color:@text@;border:2px solid transparent;border-radius:7px;padding:6px 12px;min-height:20px;}
        QMessageBox QPushButton:hover:enabled {background:@hover@;}
        QMessageBox QPushButton:pressed:enabled {background:@pressed@;}
        QMessageBox QPushButton:focus {border-color:@focus@;}
        QMessageBox QPushButton:disabled {background:@well@;color:@disabled@;}
    )",palette());
}

// AccuShot approved v2: Mars-green tonal surfaces, fixed navigation weights, rounded controls.
QString ThemeManager::getResultDialogStyle() const
{
    QString css=getControlStyle()+getPinMenuStyle()+styled(R"(
        QDialog {background:@surface@;color:@text@;}
        QLabel {color:@text@;background:transparent;}
        QLabel#settingName {font-size:14px;}
        QLabel[warning="true"] {background:@warningBg@;color:@warning@;border:0;border-radius:8px;padding:10px 14px;}
        QLabel#pageTitle {font-size:24px;font-weight:700;}
        QLabel#settingsBrand {font-size:19px;font-weight:700;}
        QLabel#sectionTitle,QLabel#cardTitle {font-size:15px;font-weight:700;}
        QLabel#secondaryText,QLabel#rowDesc,QLabel#category,QLabel#settingsPageDescription {font-size:12px;color:@secondary@;}
        QWidget#settingsSidebar {background:@sidebar@;border:0;}
        QWidget#settingsContent {background:@surface@;border:0;}
        QWidget#settingsFooter {background:@well@;border:0;}
        QScrollArea {background:@surface@;border:0;border-radius:12px;}
        QScrollArea > QWidget > QWidget {background:@surface@;}
        QWidget#settingsContent QScrollArea {background:@surface@;border:0;border-radius:0;}
        QPlainTextEdit {background:@raised@;color:@text@;font-size:15px;border:2px solid transparent;border-radius:7px;padding:10px;selection-background-color:@action@;selection-color:@onAction@;}
        QPlainTextEdit:focus {border-color:@focus@;}
        QPlainTextEdit#latexSource {font-family:'Consolas','Microsoft YaHei UI';font-size:14px;}
        QPushButton {background:@well@;color:@text@;border:2px solid transparent;border-radius:7px;padding:6px 12px;min-height:20px;}
        QPushButton:hover:enabled {background:@hover@;}
        QPushButton:checked {color:@selectedText@;background:@selected@;}
        QPushButton:checked:hover:enabled {background:@hover@;}
        QPushButton:pressed:enabled {background:@pressed@;}
        QPushButton:focus {border-color:@focus@;}
        QPushButton#primaryAction,QPushButton#settingsSave {background:@action@;color:@onAction@;font-weight:700;}
        QPushButton#primaryAction:hover:enabled,QPushButton#settingsSave:hover:enabled {background:@actionHover@;}
        QPushButton#primaryAction:pressed:enabled,QPushButton#settingsSave:pressed:enabled {background:@actionPressed@;}
        QPushButton:disabled,QPushButton#primaryAction:disabled,QPushButton#settingsSave:disabled {color:@disabled@;background:@well@;}
        QPushButton#compactAction {padding:4px 8px;border-radius:6px;min-height:20px;}
        QSplitter::handle {background:@surface@;width:16px;height:12px;}
        QSplitter::handle:hover {background:@hover@;}
        QFrame#settingsCard,QWidget#settingsBlock {background:@well@;border:0;border-radius:10px;}
        QFrame#settingsAppearance {background:@surface@;border:0;}
        QLabel#resultFontPreview {background:@surface@;border:0;border-radius:8px;padding:18px 20px;}
        QFrame#settingsCard QPlainTextEdit {background:@surface@;}
        QWidget#resultPanel,QWidget#transSection {background:@well@;border:0;border-radius:12px;}
        QWidget#segmentedControl {background:@well@;border:0;border-radius:9px;}
        QPushButton[segmented="true"] {background:transparent;font-weight:700;}
        QPushButton[segmented="true"]:checked {background:@raised@;color:@text@;font-weight:700;}
        QPushButton[segmented="true"]:hover:enabled {background:@hover@;}
        QWidget#resultPanel[surfaceRole="well"] {background:@well@;}
        QWidget#resultPanel[surfaceRole="raised"] {background:@raised@;}
        QWidget#resultPanel QPlainTextEdit,QWidget#transSection QPlainTextEdit {background:transparent;}
        QTabWidget::pane {border:0;background:@surface@;}
        QTabBar::tab {color:@text@;background:@well@;font-weight:700;padding:8px 16px;border:0;border-radius:7px;}
        QTabBar::tab:selected {background:@raised@;font-weight:700;}
        QTabBar::tab:hover:enabled {background:@hover@;}
        QListWidget {background:@well@;color:@text@;border:0;border-radius:8px;padding:4px;outline:0;}
        QListWidget::item:selected {color:@selectedText@;background:@selected@;border:0;border-radius:7px;}
        QListWidget::item:hover:enabled {background:@hover@;}
        QListWidget#settingsNavigation {background:@sidebar@;border:0;padding:0;font-weight:700;}
        QListWidget#settingsNavigation::item {padding:0 12px;border:0;border-radius:7px;font-weight:700;}
        QListWidget#settingsNavigation::item:hover {background:@hover@;}
        QListWidget#settingsNavigation::item:selected {background:@surface@;color:@text@;font-weight:700;}
        QLineEdit,QKeySequenceEdit {background:@surface@;color:@text@;border:2px solid transparent;border-radius:7px;padding:6px 10px;min-height:20px;selection-background-color:@action@;selection-color:@onAction@;}
        QLineEdit:focus,QKeySequenceEdit:focus {border-color:@focus@;}
        QLineEdit:hover:enabled,QKeySequenceEdit:hover:enabled {background:@hover@;}
        QLineEdit:disabled,QKeySequenceEdit:disabled {background:@well@;color:@disabled@;}
        QPushButton#clearBtn {padding:0;min-height:0;}
        QScrollBar:vertical {background:@surface@;width:10px;margin:0;}
        QScrollBar::handle:vertical {background:@hover@;min-height:28px;border-radius:4px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}
        QScrollBar:horizontal {background:@surface@;height:10px;margin:0;border:0;}
        QScrollBar::handle:horizontal {background:@hover@;min-width:28px;border-radius:4px;}
        QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0;background:transparent;border:0;}
        QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal {background:transparent;}
        QScrollBar::handle:horizontal:hover,QScrollBar::handle:vertical:hover {background:@secondary@;}
        QScrollBar::handle:horizontal:pressed,QScrollBar::handle:vertical:pressed {background:@action@;}
        QAbstractScrollArea::corner {background:@surface@;border:0;}
    )",palette());
    css+=QString("QPlainTextEdit#sourceText,QPlainTextEdit#translationText,QLabel#resultFontPreview {font-size:%1px;}").arg(m_resultFontSize);
    return css;
}
