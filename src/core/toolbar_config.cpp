#include "toolbar_config.h"
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <utility>

const QList<ToolDefinition>& toolbarDefinitions()
{
    // Keep this table in ToolAction enum order; ids remain stable across releases.
    static const QList<ToolDefinition> definitions = {
        {ToolAction::Rect, "rect", "矩形", true},
        {ToolAction::Arrow, "arrow", "箭头", true},
        {ToolAction::Pencil, "pencil", "画笔", true},
        {ToolAction::Text, "text", "文字", true},
        {ToolAction::Mosaic, "mosaic", "马赛克", true},
        {ToolAction::CharEdit, "char_edit", "单字修改", true},
        {ToolAction::Ocr, "ocr", "文字识别", false},
        {ToolAction::FormulaOcr, "formula_ocr", "公式识别", false},
        {ToolAction::ScrollCapture, "scroll_capture", "滚动截图（纵向 / 横向）", false},
        {ToolAction::GifRecord, "gif_record", "录制 GIF", false},
        {ToolAction::Undo, "undo", "撤销", false},
        {ToolAction::Pin, "pin", "贴图", false},
        {ToolAction::Save, "save", "保存", false},
        {ToolAction::Cancel, "cancel", "关闭", false},
        {ToolAction::Confirm, "confirm", "复制并关闭", false},
        {ToolAction::Translate, "translate", "翻译", false},
        {ToolAction::CopyText, "copy_text", "复制识别到的文字", false},
        {ToolAction::ScreenTranslate, "screen_translate", "原位屏幕翻译", false},
        {ToolAction::Markdown, "markdown", "转换为 Markdown", false},
    };
    return definitions;
}

const ToolDefinition& toolDefinition(ToolAction action)
{
    return toolbarDefinitions().at(static_cast<int>(action));
}

ToolbarConfig& ToolbarConfig::instance()
{
    static ToolbarConfig config;
    return config;
}

QList<ToolbarEntry> ToolbarConfig::defaults()
{
    return {
        {ToolAction::Rect, ToolVisibility::Always},
        {ToolAction::Pencil, ToolVisibility::Always},
        {ToolAction::Arrow, ToolVisibility::Always},
        {ToolAction::Text, ToolVisibility::Always},
        {ToolAction::Mosaic, ToolVisibility::Always},
        {ToolAction::Undo, ToolVisibility::Always},
        {ToolAction::ScrollCapture, ToolVisibility::Always},
        {ToolAction::GifRecord, ToolVisibility::Always},
        {ToolAction::CopyText, ToolVisibility::Always},
        {ToolAction::Translate, ToolVisibility::Always},
        {ToolAction::Pin, ToolVisibility::Always},
        {ToolAction::Save, ToolVisibility::Always},
        {ToolAction::Cancel, ToolVisibility::Always},
        {ToolAction::Confirm, ToolVisibility::Always},
        {ToolAction::Ocr, ToolVisibility::More},
        {ToolAction::ScreenTranslate, ToolVisibility::More},
        {ToolAction::FormulaOcr, ToolVisibility::More},
        {ToolAction::Markdown, ToolVisibility::More},
        {ToolAction::CharEdit, ToolVisibility::More},
    };
}

ToolbarConfig::ToolbarConfig()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    settings.beginGroup("Toolbar");
    const int count = settings.beginReadArray("entries");
    const auto& definitions = toolbarDefinitions();
    QSet<QString> seen;
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString id = settings.value("id").toString();
        const auto found = std::find_if(definitions.cbegin(), definitions.cend(),
            [&id](const ToolDefinition& definition) { return definition.id == id; });
        if (found == definitions.cend() || seen.contains(id))
            continue;

        const QString visibility = settings.value("visibility").toString();
        ToolVisibility state;
        if (visibility == "always") state = ToolVisibility::Always;
        else if (visibility == "more") state = ToolVisibility::More;
        else if (visibility == "hidden") state = ToolVisibility::Hidden;
        else continue;

        m_entries.append({found->action, state});
        seen.insert(id);
    }
    settings.endArray();
    settings.endGroup();

    // Missing tools (including tools added in a new version) retain their defaults.
    for (const auto& entry : defaults()) {
        if (!seen.contains(toolDefinition(entry.action).id))
            m_entries.append(entry);
    }
}

void ToolbarConfig::setEntries(QList<ToolbarEntry> entries)
{
    m_entries = std::move(entries);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
    settings.beginGroup("Toolbar");
    settings.beginWriteArray("entries", static_cast<int>(m_entries.size()));
    for (int i = 0; i < m_entries.size(); ++i) {
        settings.setArrayIndex(i);
        const auto& entry = m_entries.at(i);
        settings.setValue("id", toolDefinition(entry.action).id);
        const QString visibility = entry.visibility == ToolVisibility::Always ? "always"
            : entry.visibility == ToolVisibility::More ? "more" : "hidden";
        settings.setValue("visibility", visibility);
    }
    settings.endArray();
    settings.endGroup();
    emit changed();
}
