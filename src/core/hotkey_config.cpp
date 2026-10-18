#include "hotkey_config.h"
#include <QSettings>
#include <QKeyCombination>

HotkeyConfig::HotkeyConfig(QObject* parent)
    : QObject(parent)
{
    load();
}

HotkeyConfig& HotkeyConfig::instance()
{
    static HotkeyConfig config;
    return config;
}

void HotkeyConfig::load()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");

    // 全局热键
    m_data.globalSnipping = settings.value("Hotkey/Snipping", "F1").toString();
    m_data.globalPin      = settings.value("Hotkey/Pin", "F3").toString();

    // 截屏操作
    m_data.snippingUndo    = settings.value("Hotkey/SnippingUndo", "Ctrl+Z").toString();
    m_data.snippingPin     = settings.value("Hotkey/SnippingPin", "F3").toString();
    m_data.snippingConfirm = settings.value("Hotkey/SnippingConfirm", "Return").toString();
    m_data.snippingSave    = settings.value("Hotkey/SnippingSave", "Ctrl+S").toString();
    m_data.snippingOcr     = settings.value("Hotkey/SnippingOcr", "Ctrl+O").toString();
    m_data.snippingCancel  = settings.value("Hotkey/SnippingCancel", "Esc").toString();
    m_data.snippingSmallerCandidate = settings.value("Hotkey/SnippingSmallerCandidate", "WheelDown").toString();
    m_data.snippingLargerCandidate = settings.value("Hotkey/SnippingLargerCandidate", "WheelUp").toString();
    m_data.snippingShrinkCandidate = settings.value("Hotkey/SnippingShrinkCandidate", "Ctrl+WheelDown").toString();
    m_data.snippingExpandCandidate = settings.value("Hotkey/SnippingExpandCandidate", "Ctrl+WheelUp").toString();
    // Keep existing area-navigation mappings when these newly introduced keys are absent.
    if (!settings.contains("Hotkey/SnippingShrinkCandidate") &&
        (m_data.snippingShrinkCandidate == m_data.snippingSmallerCandidate ||
         m_data.snippingShrinkCandidate == m_data.snippingLargerCandidate))
        m_data.snippingShrinkCandidate.clear();
    if (!settings.contains("Hotkey/SnippingExpandCandidate") &&
        (m_data.snippingExpandCandidate == m_data.snippingSmallerCandidate ||
         m_data.snippingExpandCandidate == m_data.snippingLargerCandidate))
        m_data.snippingExpandCandidate.clear();
    m_data.snippingCandidateResizeStep = qBound(1, settings.value("Hotkey/SnippingCandidateResizeStep", 1).toInt(), 50);

    // 标注工具
    m_data.toolRect   = settings.value("Hotkey/ToolRect", "R").toString();
    m_data.toolArrow  = settings.value("Hotkey/ToolArrow", "A").toString();
    m_data.toolPencil = settings.value("Hotkey/ToolPencil", "P").toString();
    m_data.toolText   = settings.value("Hotkey/ToolText", "T").toString();
    m_data.toolMosaic = settings.value("Hotkey/ToolMosaic", "M").toString();
    m_data.toolCharEdit = settings.value("Hotkey/ToolCharEdit", "E").toString();

    // 贴图窗口
    m_data.pinClose = settings.value("Hotkey/PinClose", "Esc").toString();
    m_data.pinOcr   = settings.value("Hotkey/PinOcr", "Ctrl+O").toString();
    m_data.pinCopy  = settings.value("Hotkey/PinCopy", "Ctrl+C").toString();
    m_data.pinSave  = settings.value("Hotkey/PinSave", "Ctrl+S").toString();
}

void HotkeyConfig::save()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");

    // 全局热键
    settings.setValue("Hotkey/Snipping", m_data.globalSnipping);
    settings.setValue("Hotkey/Pin", m_data.globalPin);

    // 截屏操作
    settings.setValue("Hotkey/SnippingUndo", m_data.snippingUndo);
    settings.setValue("Hotkey/SnippingPin", m_data.snippingPin);
    settings.setValue("Hotkey/SnippingConfirm", m_data.snippingConfirm);
    settings.setValue("Hotkey/SnippingSave", m_data.snippingSave);
    settings.setValue("Hotkey/SnippingOcr", m_data.snippingOcr);
    settings.setValue("Hotkey/SnippingCancel", m_data.snippingCancel);
    settings.setValue("Hotkey/SnippingSmallerCandidate", m_data.snippingSmallerCandidate);
    settings.setValue("Hotkey/SnippingLargerCandidate", m_data.snippingLargerCandidate);
    settings.setValue("Hotkey/SnippingShrinkCandidate", m_data.snippingShrinkCandidate);
    settings.setValue("Hotkey/SnippingExpandCandidate", m_data.snippingExpandCandidate);
    settings.setValue("Hotkey/SnippingCandidateResizeStep", m_data.snippingCandidateResizeStep);

    // 标注工具
    settings.setValue("Hotkey/ToolRect", m_data.toolRect);
    settings.setValue("Hotkey/ToolArrow", m_data.toolArrow);
    settings.setValue("Hotkey/ToolPencil", m_data.toolPencil);
    settings.setValue("Hotkey/ToolText", m_data.toolText);
    settings.setValue("Hotkey/ToolMosaic", m_data.toolMosaic);
    settings.setValue("Hotkey/ToolCharEdit", m_data.toolCharEdit);

    // 贴图窗口
    settings.setValue("Hotkey/PinClose", m_data.pinClose);
    settings.setValue("Hotkey/PinOcr", m_data.pinOcr);
    settings.setValue("Hotkey/PinCopy", m_data.pinCopy);
    settings.setValue("Hotkey/PinSave", m_data.pinSave);

    emit configChanged();
}

void HotkeyConfig::setData(const HotkeyConfigData& data)
{
    m_data = data;
    m_data.snippingCandidateResizeStep = qBound(1, m_data.snippingCandidateResizeStep, 50);
    save();
}

void HotkeyConfig::resetToDefaults()
{
    m_data = HotkeyConfigData();
    save();
}

bool HotkeyConfig::matches(QKeyEvent* event, const QString& keySeqStr)
{
    if (keySeqStr.contains("Wheel")) return false;
    if (keySeqStr.trimmed().isEmpty()) return false;
    QKeySequence seq(keySeqStr, QKeySequence::PortableText);
    if (seq.isEmpty()) return false;

    int key = event->key();
    // 忽略单纯的修饰键
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) {
        return false;
    }

    Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);

    // 回车键兼容：Key_Return 与 Key_Enter 均认可
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        auto targetCombo = seq[0];
        if (targetCombo.key() == Qt::Key_Return || targetCombo.key() == Qt::Key_Enter) {
            return (mods == targetCombo.keyboardModifiers());
        }
    }

    QKeyCombination eventCombo(mods, static_cast<Qt::Key>(key));
    return (seq[0] == eventCombo);
}

QString HotkeyConfig::wheelBinding(int delta, Qt::KeyboardModifiers modifiers)
{
    QString binding;
    if (modifiers.testFlag(Qt::ControlModifier)) binding += "Ctrl+";
    if (modifiers.testFlag(Qt::AltModifier)) binding += "Alt+";
    if (modifiers.testFlag(Qt::ShiftModifier)) binding += "Shift+";
    if (modifiers.testFlag(Qt::MetaModifier)) binding += "Meta+";
    return binding + (delta > 0 ? "WheelUp" : "WheelDown");
}

bool HotkeyConfig::matches(QWheelEvent* event, const QString& binding)
{
    return event->angleDelta().y() != 0 && binding == wheelBinding(event->angleDelta().y(), event->modifiers());
}
