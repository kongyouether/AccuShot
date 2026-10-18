#pragma once

#include <QDialog>
#include "../../core/toolbar_config.h"

class QListWidget;
class QHBoxLayout;

class ToolbarSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ToolbarSettingsDialog(QWidget* parent = nullptr, bool embedded = false);
    void accept() override;
    QList<ToolbarEntry> entries() const;
    void loadEntries(const QList<ToolbarEntry>& entries);
    void updateTheme();

private:
    void updatePreview();
    void moveSelected(int offset);
    bool m_embedded = false;
    QHBoxLayout* m_preview = nullptr;
    QListWidget* m_list = nullptr;
};
