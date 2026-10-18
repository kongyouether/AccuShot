#pragma once
#include <QWidget>
#include <QColor>
#include <QFont>
#include <QMap>
#include "../../core/tool_action.h"
class QToolButton;
class QHBoxLayout;
class QSpinBox;
class QFontComboBox;
class QLabel;
class FloatingToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FloatingToolbar(QWidget* parent = nullptr);
    void updatePosition(const QRect&, const QRect&);
    void refreshTooltips();
    void updateThemeStyle();
    void setActiveTool(ToolAction);
    void resetSession();
    void setUndoAvailable(bool);
    void setOcrBusy(bool);
    QColor currentColor() const;
    int currentStrokeWidth() const;
    int currentFontSize() const;
    QFont currentTextFont() const;
signals:
    void actionTriggered(ToolAction);
    void colorSelected(const QColor&);
    void thicknessSelected(int, int);
    void fontSelected(const QFont&);
    void mosaicSizeSelected(int);
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    struct Properties {
        QColor color = QColor("#ef4444");
        int width = 3;
        QFont font = QFont("Microsoft YaHei UI", 18);
        int mosaic = 12;
    };
    void rebuildButtons();
    void rebuildOverflow();
    void setupProperties();
    void syncProperties();
    void saveProperties();
    void publishProperties();
    void showMoreMenu();
    void placeToolbar();
    QString tooltip(ToolAction) const;
    bool actionEnabled(ToolAction) const;
    QHBoxLayout* m_toolsLayout = nullptr;
    QWidget *m_header = nullptr, *m_properties = nullptr, *m_colors = nullptr;
    QWidget *m_fontControls = nullptr, *m_widthControls = nullptr;
    QToolButton *m_grip = nullptr, *m_more = nullptr, *m_bold = nullptr;
    QSpinBox *m_widthSpin = nullptr, *m_fontSizeSpin = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QLabel* m_widthLabel = nullptr;
    QMap<ToolAction, QToolButton*> m_buttons;
    QMap<ToolAction, QWidget*> m_cells;
    QMap<ToolAction, Properties> m_propertiesByTool;
    QList<ToolAction> m_overflow;
    QList<QToolButton*> m_swatches;
    ToolAction m_activeTool = ToolAction::Cancel;
    QRect m_selection, m_boundary;
    QPoint m_dragOffset;
    bool m_userPosition = false, m_dragging = false;
    bool m_undoAvailable = false, m_ocrBusy = false;
    bool m_syncing = false, m_panelExpanded = true;
};
