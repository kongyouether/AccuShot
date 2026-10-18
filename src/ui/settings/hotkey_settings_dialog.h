#pragma once

#include <QDialog>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include "../../core/hotkey_config.h"
#include "../../core/theme_manager.h"

class ToolbarSettingsDialog;
class AiSettingsPage;
class FeatureSchemeWidget;
class CandidateShortcutEdit;
class QSpinBox;

class HotkeySettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HotkeySettingsDialog(QWidget* parent = nullptr);
    void openPlugins();
    void openAiServices();
    void openMarkdown();
    ~HotkeySettingsDialog() override;

signals:
    void hotkeysSaved();

private:
    void setupUi();
    void loadFromConfig();
    void loadHotkeys(const HotkeyConfigData& c);
    bool saveToConfig();
    void resetAllDefaults();
    void restoreAppearancePreview();

    QWidget* createGeneralTab();
    QWidget* createHotkeysTab();

    QWidget* createKeyRow(const QString& title, const QString& desc, QKeySequenceEdit*& editOut);
    QWidget* createShortcutRow(const QString& title, const QString& desc, QWidget* edit);

    // 全局热键
    QKeySequenceEdit* m_globalSnippingEdit = nullptr;
    QKeySequenceEdit* m_globalPinEdit = nullptr;

    // 截屏操作快捷键
    QKeySequenceEdit* m_snippingUndoEdit = nullptr;
    QKeySequenceEdit* m_snippingPinEdit = nullptr;
    QKeySequenceEdit* m_snippingConfirmEdit = nullptr;
    QKeySequenceEdit* m_snippingSaveEdit = nullptr;
    QKeySequenceEdit* m_snippingOcrEdit = nullptr;
    QKeySequenceEdit* m_snippingCancelEdit = nullptr;
    CandidateShortcutEdit* m_smallerCandidateEdit = nullptr;
    CandidateShortcutEdit* m_largerCandidateEdit = nullptr;
    CandidateShortcutEdit* m_shrinkCandidateEdit = nullptr;
    CandidateShortcutEdit* m_expandCandidateEdit = nullptr;
    QSpinBox* m_candidateResizeStep = nullptr;

    // 标注工具切换快捷键
    QKeySequenceEdit* m_toolRectEdit = nullptr;
    QKeySequenceEdit* m_toolArrowEdit = nullptr;
    QKeySequenceEdit* m_toolPencilEdit = nullptr;
    QKeySequenceEdit* m_toolTextEdit = nullptr;
    QKeySequenceEdit* m_toolMosaicEdit = nullptr;
    QKeySequenceEdit* m_toolCharEdit = nullptr;

    // 贴图窗口快捷键
    QKeySequenceEdit* m_pinCloseEdit = nullptr;
    QKeySequenceEdit* m_pinOcrEdit = nullptr;
    QKeySequenceEdit* m_pinCopyEdit = nullptr;
    QKeySequenceEdit* m_pinSaveEdit = nullptr;

    // 通用与外观设置
    AiSettingsPage* m_aiSettings = nullptr;
    QList<FeatureSchemeWidget*> m_featureChoices;
    QComboBox* m_markdownDefault = nullptr;
    QComboBox* m_ocrDefault = nullptr;
    QComboBox* m_translationDefault = nullptr;
    QComboBox* m_formulaDefault = nullptr;
    QComboBox* m_snapMode = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_resultFontSizeCombo = nullptr;
    QCheckBox* m_enableSmartSnappingCheck = nullptr;
    QCheckBox* m_autoStartCheck = nullptr;
    QComboBox* m_textCommitModeCombo = nullptr;
    QLineEdit* m_simpleTexTokenEdit = nullptr;

    QWidget* m_capturePage = nullptr;
    QWidget* m_recognitionPage = nullptr;
    ToolbarSettingsDialog* m_toolbarSettings = nullptr;
    bool m_initialAutoStart = false;
    ThemeMode m_initialTheme = ThemeMode::Auto;
    int m_initialFontSize = 15;
    bool m_applyingAppearancePreview = false;
    bool m_themePreviewActive = false;
    bool m_fontSizePreviewActive = false;
    QTabWidget* m_tabWidget = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};
