#pragma once
#include <QComboBox>
#include <QFontComboBox>
#include <QAbstractItemView>
#include <QFrame>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QListView>
#include <QKeyEvent>
#include <QApplication>
#include "../core/theme_manager.h"

// Keep the committed choice (check mark) distinct from the keyboard/mouse cursor.
class TonalChoiceDelegate : public QStyledItemDelegate {
public:
    explicit TonalChoiceDelegate(QComboBox* combo) : QStyledItemDelegate(combo), m_combo(combo) {}
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QSize base = QStyledItemDelegate::sizeHint(option,index);
        QFont font=option.font;font.setPixelSize(14);
        return QSize(base.width()+38,qMax(36,QFontMetrics(font).height()+12));
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const auto p=ThemeManager::instance().palette();
        const bool enabled=index.flags().testFlag(Qt::ItemIsEnabled);
        const bool chosen=index.row()==m_combo->currentIndex();
        const bool active=option.state.testFlag(QStyle::State_Selected)||option.state.testFlag(QStyle::State_MouseOver);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect r=option.rect.adjusted(2,2,-2,-2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(chosen?p.selected:(active&&enabled?p.hover:p.raised));
        painter->drawRoundedRect(r,6,6);
        QStyleOptionViewItem content(option); initStyleOption(&content,index);
        content.font.setPixelSize(14);content.font.setWeight(QFont::Normal);
        painter->setFont(content.font);
        const QColor ink=!enabled?p.disabled:(chosen?p.selectedText:p.text);
        painter->setPen(ink);
        const QRect textRect=r.adjusted(10,0,-30,0);
        painter->drawText(textRect,Qt::AlignVCenter|Qt::AlignLeft,
            QFontMetrics(content.font).elidedText(content.text,Qt::ElideRight,textRect.width()));
        if(chosen) {
            painter->setPen(QPen(ink,2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const QPointF c(r.right()-16,r.center().y());
            QPainterPath check;check.moveTo(c+QPointF(-5,0));check.lineTo(c+QPointF(-1,4));check.lineTo(c+QPointF(6,-5));
            painter->drawPath(check);
        }
        painter->restore();
    }
private:
    QComboBox* m_combo;
};

// Qt owns commit, Escape/Tab and accessibility. Only its popup painting is replaced.
template<class Base>
class ThemedCombo : public Base {
public:
    using Base::Base;
    void showPopup() override {
        if (!m_tonalView) {
            auto* list=new QListView(this);
            list->setMouseTracking(true);
            list->setSpacing(0);
            list->setUniformItemSizes(true);
            list->setItemDelegate(new TonalChoiceDelegate(this));
            this->setView(list);
            this->setMaxVisibleItems(7);
            QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { this->hidePopup(); });
            m_tonalView=true;
        }
        auto* popup = this->view()->window();
        auto& theme = ThemeManager::instance();
        const auto colors = theme.palette();
        const QString background = colors.raised.name();
        popup->setStyleSheet(theme.getControlStyle() + QString(
            "QWidget {background:%1;} QAbstractItemView {border:0;padding:6px;outline:0;}"
            "QComboBox QAbstractItemView::item {border:0;padding:0;min-height:0;}"
            "QAbstractItemView::item:disabled {color:%2;background:%1;}").arg(background, colors.disabled.name()));
        if (auto* frame = qobject_cast<QFrame*>(popup)) frame->setFrameShape(QFrame::NoFrame);
        Base::showPopup();
        QPainterPath shape;shape.addRoundedRect(QRectF(popup->rect()),9,9);
        popup->setMask(QRegion(shape.toFillPolygon().toPolygon()));
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (!this->isEditable() && !this->view()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Down: case Qt::Key_Up: case Qt::Key_Home: case Qt::Key_End:
            case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
                showPopup(); event->accept(); return;
            default: break;
            }
            if (event->text().size()==1 && !event->modifiers().testFlag(Qt::ControlModifier)
                && !event->modifiers().testFlag(Qt::AltModifier) && !event->modifiers().testFlag(Qt::MetaModifier)
                && event->text().front().isPrint()) {
                showPopup();
                QApplication::sendEvent(this->view(),event);
                event->accept(); return;
            }
        }
        Base::keyPressEvent(event);
    }
private:
    bool m_tonalView=false;
};

using ThemedComboBox = ThemedCombo<QComboBox>;
using ThemedFontComboBox = ThemedCombo<QFontComboBox>;

// Keep missing plugins visible but impossible to choose in the popup.
inline void setPluginChoiceEnabled(QComboBox* combo, const QString& value, bool enabled) {
    const int index=combo->findData(value);if(index<0)return;
    QString label = combo->itemText(index);
    if (!enabled && !label.contains("未安装")) label += "（未安装）";
    if (enabled && label.endsWith("（未安装）")) label.chop(5);
    combo->setItemText(index, label);
    if(auto* model=qobject_cast<QStandardItemModel*>(combo->model())) {
        if(auto* item=model->item(index)){item->setEnabled(enabled);item->setToolTip(enabled?QString():QString("请先在设置 → 插件与模型中安装"));}
    }
}
