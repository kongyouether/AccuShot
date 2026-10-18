#pragma once

#include "../../core/ocr/ocr_types.h"
#include "../../core/translation/translation_types.h"
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <functional>

class QComboBox;
class QDialog;
class QLabel;
class QPushButton;

struct ScreenTranslationLine {
    enum State { Pending, Translated, Failed };
    QRectF box; // Screenshot-local logical coordinates, independent of display DPR.
    QString source, translation, error;
    State state = Pending;
};

struct ScreenTranslationRender {
    QImage image;
    QList<int> overflowLines; // Full text is available in the window's text viewer.
};

class ScreenTranslationWindow : public QWidget {
    Q_OBJECT
public:
    // Callbacks are delivered on the GUI thread. Optional functions make layout and
    // close-during-request tests independent of models, accounts and the network.
    using Cancel = std::function<void()>;
    using OcrCallback = std::function<void(OcrResult)>;
    using TranslateCallback = std::function<void(TranslationResult)>;
    struct Hooks {
        std::function<Cancel(const QImage&, qreal, const QString&, OcrCallback)> recognize;
        std::function<Cancel(const QString&, const QString&, const QString&, const QString&, TranslateCallback)> translate;
    };
    explicit ScreenTranslationWindow(QPixmap image, QRect screenRect, QWidget* parent = nullptr);
    ScreenTranslationWindow(QPixmap image, QRect screenRect, Hooks hooks, QWidget* parent = nullptr);
    ~ScreenTranslationWindow() override;

    void startRecognition();
    static ScreenTranslationRender renderTranslatedImage(const QPixmap&, const QList<ScreenTranslationLine>&);
    QImage translatedImage() const { return m_render.image; }
    const QList<ScreenTranslationLine>& lines() const { return m_lines; }
    QString statusText() const;

signals:
    void workFinished(bool allSucceeded);

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void setupControls();
    void positionControls();
    void stopCurrentRequest();
    void acceptRecognition(const OcrResult&);
    void startTranslation();
    void translateNext(int generation);
    void rebuildImage();
    void showFullText();
    void saveImage();
    void setOriginalHeld(bool held);

    QPixmap m_original;
    QRect m_screenRect;
    Hooks m_hooks;
    Cancel m_cancel;
    QList<ScreenTranslationLine> m_lines;
    ScreenTranslationRender m_render;
    QDialog* m_controls = nullptr;
    QComboBox* m_ocr = nullptr;
    QComboBox* m_service = nullptr;
    QComboBox* m_target = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_retry = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_originalButton = nullptr;
    int m_generation = 0;
    int m_nextLine = 0;
    bool m_busy = false;
    bool m_closed = false;
    bool m_originalHeld = false;
    bool m_showOriginal = false;
    QString m_requestedService, m_requestedTarget, m_sourceLanguage;
};
