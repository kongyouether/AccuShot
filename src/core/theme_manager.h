#pragma once

#include <QObject>
#include <QString>
#include <QColor>
class QWidget;

enum class ThemeMode {
    Auto,   // 跟随系统设置 (默认)
    Light,  // 马尔斯绿浅色
    Dark    // 马尔斯绿深色
};

struct UiPalette {
    QColor page, sidebar, surface, raised, text, secondary, brand, action, onAction;
    QColor border, controlBorder, hover, pressed, selected, disabled;
    QColor success, warning, danger;
    QColor well, selectedText, focus, warningBg;
};

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    // 当前主题模式与实际生效的暗色判定
    ThemeMode themeMode() const { return m_themeMode; }
    void setThemeMode(ThemeMode mode, bool persist = true);

    bool isDarkMode() const;
    UiPalette palette() const;
    int resultFontSize() const { return m_resultFontSize; }
    void setResultFontSize(int size, bool persist = true);

    // 样式表获取
    QString getResultDialogStyle() const;
    QString getControlStyle() const;
    QString getApplicationStyle() const;
    QString getTrayMenuStyle() const;
    QString getPinMenuStyle() const;
    QString getFloatingToolbarStyle() const;
    QString getSettingsDialogStyle() const;

signals:
    void themeChanged(bool isDark);
    void resultFontSizeChanged(int size);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void updateWindowFrame(QWidget* widget) const;
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    bool detectSystemIsDark() const;

    ThemeMode m_themeMode = ThemeMode::Auto;
    int m_resultFontSize = 15;
};
