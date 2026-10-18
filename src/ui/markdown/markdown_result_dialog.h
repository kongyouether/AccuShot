#pragma once
#include <QDialog>
#include <QPointer>
#include "../../core/markdown/markdown_converter.h"
class QComboBox;
class QPlainTextEdit;
class QTextBrowser;
class QLabel;
class QPushButton;
class MarkdownResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit MarkdownResultDialog(QWidget* parent=nullptr);
    ~MarkdownResultDialog() override;
    void setImage(const QPixmap& image);
private:
    void refreshProviders();
    void recognize();
    void updatePreview();
    QImage m_image;
    MarkdownResult m_result;
    QComboBox* m_providers;
    QPlainTextEdit* m_editor;
    QTextBrowser* m_preview;
    QLabel* m_original;
    QLabel* m_status;
    QLabel* m_description;
    QPushButton* m_run;
    QPushButton* m_copy;
    QPushButton* m_save;
    QPointer<QNetworkReply> m_reply;
    bool m_busy=false;
};
