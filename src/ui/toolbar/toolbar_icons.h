#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include "../../core/tool_action.h"

QIcon uiIcon(const QString& name, const QColor& color);
QIcon appIcon();
QIcon toolbarIcon(ToolAction action, const QColor& color = QColor("#e8e8eb"));
QIcon toolbarMoreIcon(const QColor& color = QColor("#e8e8eb"));
QIcon toolbarGripIcon(const QColor& color = QColor("#e8e8eb"));
QIcon toolbarChevronIcon(const QColor& color = QColor("#e8e8eb"));
