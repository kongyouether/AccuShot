#pragma once

#include "tool_action.h"
#include <QList>
#include <QObject>
#include <QString>

struct ToolDefinition {
    ToolAction action;
    QString id;
    QString label;
    bool annotation;
};

const QList<ToolDefinition>& toolbarDefinitions();
const ToolDefinition& toolDefinition(ToolAction action);

enum class ToolVisibility { Always, More, Hidden };

struct ToolbarEntry {
    ToolAction action;
    ToolVisibility visibility;
};

class ToolbarConfig : public QObject
{
    Q_OBJECT
public:
    static ToolbarConfig& instance();
    const QList<ToolbarEntry>& entries() const { return m_entries; }
    static QList<ToolbarEntry> defaults();
    void setEntries(QList<ToolbarEntry> entries);

signals:
    void changed();

private:
    ToolbarConfig();
    QList<ToolbarEntry> m_entries;
};
