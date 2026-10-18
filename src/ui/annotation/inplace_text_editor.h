#pragma once

#include "../../core/theme_manager.h"
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QSettings>
#include <QFontMetrics>
#include <algorithm>

class InPlaceTextEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit InPlaceTextEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QPlainTextEdit::NoWrap);

        m_textFont = font();
        m_textFont.setPointSize(16);
        m_textFont.setBold(true);

        updatePlaceholderByMode();
        updateEditorStyle();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { updateEditorStyle(); });

        connect(this, &QPlainTextEdit::textChanged, this, [this]() {
            adjustEditorSize();
        });
    }

    void setTextColor(const QColor& color)
    {
        m_currentColor = color;
        updateEditorStyle();
    }

    void setFontSize(int size)
    {
        m_textFont.setPointSize(size);
        updateEditorStyle();
    }

    void setTextFont(const QFont& font)
    {
        m_textFont = font;
        updateEditorStyle();
    }

    // 0: 模式 B (Enter 确定, Shift+Enter 换行 - 默认)
    // 1: 模式 A (Ctrl+Enter 确定, Enter 换行)
    int commitMode() const
    {
        QSettings settings("Evan", "Evan");
        return settings.value("Annotation/TextCommitMode", 0).toInt();
    }

    void updatePlaceholderByMode()
    {
        if (commitMode() == 1) {
            setPlaceholderText("输入文字 (Enter 换行, Ctrl+Enter 确定)");
        } else {
            setPlaceholderText("输入文字 (Enter 确定, Shift+Enter 换行)");
        }
    }

    void adjustEditorSize()
    {
        QString str = toPlainText();
        QFontMetrics fm(font());

        int maxLineW = 0;
        int lineCount = 1;

        if (str.isEmpty()) {
            maxLineW = fm.horizontalAdvance(placeholderText());
        } else {
            QStringList lines = str.split('\n');
            lineCount = (std::max)(1, static_cast<int>(lines.size()));
            for (const auto& line : lines) {
                maxLineW = (std::max)(maxLineW, fm.horizontalAdvance(line));
            }
        }

        int lineHeight = fm.lineSpacing();
        int neededW = maxLineW + 28;
        int neededH = lineCount * lineHeight + 14;

        resize(qMax(120, neededW), qMax(32, neededH));
    }

    void updateEditorStyle()
    {
        setFont(m_textFont);
        const auto colors = ThemeManager::instance().palette();
        auto editorPalette = palette();
        editorPalette.setColor(QPalette::PlaceholderText, colors.secondary);
        setPalette(editorPalette);

        setStyleSheet(QString(
            "QPlainTextEdit {"
            "  background-color: rgba(%2, %3, %4, 215);"
            "  color: %1;"
            "  border: 1.5px dashed %5;"
            "  selection-background-color: %5;"
            "  selection-color: %6;"
            "  border-radius: 4px;"
            "  padding: 4px 6px;"
            "}"
        ).arg(m_currentColor.name()).arg(colors.raised.red()).arg(colors.raised.green())
         .arg(colors.raised.blue()).arg(colors.action.name(), colors.onAction.name()));

        adjustEditorSize();
    }

    QColor textColor() const { return m_currentColor; }
    int fontSize() const { return m_textFont.pointSize(); }

    void startEdit(const QPoint& pos)
    {
        m_startPos = pos;
        clear();
        updatePlaceholderByMode();
        updateEditorStyle();
        move(pos);
        show();
        setFocus();
        activateWindow();
    }

    QPoint startPos() const { return m_startPos; }

signals:
    void editingCommitted(const QPoint& pos, const QString& text);
    void editingCancelled();

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        int mode = commitMode();

        if (event->key() == Qt::Key_Escape) {
            hide();
            emit editingCancelled();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            bool hasShift = (event->modifiers() & Qt::ShiftModifier);
            bool hasCtrl  = (event->modifiers() & Qt::ControlModifier);

            if (mode == 0) {
                // 模式 B: Shift+Enter 换行, Enter 提交
                if (hasShift) {
                    insertPlainText("\n");
                    event->accept();
                    return;
                } else {
                    commitAndHide();
                    event->accept();
                    return;
                }
            } else {
                // 模式 A: Enter 换行, Ctrl+Enter 提交
                if (hasCtrl) {
                    commitAndHide();
                    event->accept();
                    return;
                } else {
                    insertPlainText("\n");
                    event->accept();
                    return;
                }
            }
        }

        QPlainTextEdit::keyPressEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        QPlainTextEdit::focusOutEvent(event);
        commitAndHide();
    }

private:
    void commitAndHide()
    {
        if (!isVisible()) return;
        QString str = toPlainText().trimmed();
        if (!str.isEmpty()) {
            emit editingCommitted(m_startPos, str);
        } else {
            emit editingCancelled();
        }
        hide();
    }

    QPoint m_startPos;
    QColor m_currentColor = QColor(235, 30, 30);
    QFont m_textFont;
};
