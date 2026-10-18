#pragma once

#include "../../core/theme_manager.h"
#include <QCheckBox>
#include <QPainter>

// Keep QCheckBox's keyboard, accessibility and toggled contract for boolean preferences.
class SettingsSwitch : public QCheckBox
{
public:
    explicit SettingsSwitch(const QString& label, QWidget* parent=nullptr)
        : QCheckBox(label,parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(36);
        QSizePolicy policy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize sizeHint() const override
    {
        const int preferredWidth=fontMetrics().horizontalAdvance(text())+60;
        return {preferredWidth,heightForWidth(preferredWidth)};
    }
    QSize minimumSizeHint() const override { return {150,heightForWidth(150)}; }
    int heightForWidth(int availableWidth) const override
    {
        const auto textBounds=fontMetrics().boundingRect(QRect(0,0,qMax(1,availableWidth-60),10000),
            Qt::AlignLeft|Qt::TextWordWrap,text());
        return qMax(36,textBounds.height()+8);
    }
protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent*) override
    {
        const auto colors=ThemeManager::instance().palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if(!isEnabled())painter.setOpacity(.45);
        painter.setPen(colors.text);
        painter.drawText(QRect(0,0,width()-60,height()),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextWordWrap,text());
        const QRectF track(width()-40,(height()-22)/2.0,36,22);
        painter.setPen(Qt::NoPen);
        QColor fill=isChecked()?colors.action:colors.secondary;
        if(isDown())fill=fill.darker(106);
        painter.setBrush(fill);
        painter.drawRoundedRect(track,11,11);
        painter.setBrush(isChecked()?colors.onAction:colors.surface);
        painter.drawEllipse(QRectF(track.x()+3+(isChecked()?14:0),track.y()+3,16,16));
        if(hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colors.focus,2));
            painter.drawRoundedRect(track.adjusted(-3,-3,3,3),14,14);
        }
    }
};
