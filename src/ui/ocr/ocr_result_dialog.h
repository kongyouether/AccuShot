#include <QPointer>
#include <QNetworkReply>
#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QScrollArea>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPaintEvent>
#include "../../core/ocr/ocr_types.h"
#include "../../core/translation/translation_types.h"
#include <functional>
class QTimer;
class QButtonGroup;

// 具备文字包围框绘制的高清原图预览挂件
class OcrImagePreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OcrImagePreviewWidget(QWidget* parent = nullptr);

    void setImage(const QPixmap& pixmap);
    void setZoom(qreal zoom);
    void setResult(const OcrResult& result);
    void setShowBoundingBoxes(bool show);
    void setSelectedLines(const QList<int>& lines);
    QList<int> selectedLines() const { return m_selectedLines; }
    QRectF lineRect(int index) const;

    QSize sizeHint() const override;

signals:
    void linesSelected(const QList<int>& lines);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF imageRect() const;
    void selectImageLines(const QPointF& position, bool dragging);
    QPixmap m_pixmap;
    qreal m_zoom = 1.0;
    OcrResult m_result;
    bool m_showBoundingBoxes = true;
    QList<int> m_selectedLines;
    QPointF m_dragStart;
    bool m_selecting = false;
};

// 左右对比形式的 OCR 结果对话框
class OcrResultDialog : public QDialog
{
    Q_OBJECT
public:
    using TranslationCallback = std::function<void(const TranslationResult&)>;
    using TranslationRequest = std::function<QNetworkReply*(const QString&, const QString&, const QString&,
                                                           TranslationCallback, const QString&)>;
    explicit OcrResultDialog(QWidget* parent = nullptr, TranslationRequest translationRequest = {});
    ~OcrResultDialog() override = default;

    // 设置截图原图与 OCR 识别结果
    void setImage(const QPixmap& pixmap);
    void setResult(const OcrResult& result, const QString& engineName = "");
    void setTranslationRequested(bool requested);
    void startRecognition();

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void updateResultLayout();
    void setImageColumnPercent(int percent);
    void updateFormattedText();
    QString processText(bool mergeParagraphs, bool removeExtraSpaces);
    void setupTranslationPanel(QVBoxLayout* rightLayout);
    void triggerTranslation();
    void autoDetectSourceLanguage();
    void refreshEngineComboState();
    void initializeTextLineMap();
    void applyTextLineMap();
    void updateTextLineMap();
    void syncImageSelection();
    void selectTextLines(const QList<int>& lines);
    void syncTranslationFromSource();
    void syncSourceFromTranslation();
    void highlightTranslationSpans(const QList<int>& spans, bool fromTranslation);
    void invalidateTranslation(const QString& message = {});
    void setTranslating(bool translating);
    void translateNextParagraph(quint64 generation);

    struct TranslationSpan {
        QString sourceText;
        int sourceStart = 0;
        int sourceEnd = 0;
        int targetStart = -1;
        int targetEnd = -1;
        QList<int> lineIds;
    };

    QPixmap m_image;
    OcrResult m_result;
    bool m_mergeParagraphs = false;
    bool m_removeExtraSpaces = false;
    bool m_showBoundingBoxes = true;
    QList<int> m_textLineIds; // UTF-16 character -> original OCR line, -1 for unmapped text.
    bool m_updatingTextMap = false;
    bool m_syncingSelection = false;

    // 左右分栏 Splitter
    QSplitter* m_splitter = nullptr;
    QWidget* m_imagePanel = nullptr;
    QWidget* m_sourcePanel = nullptr;
    QPushButton* m_showImageBtn = nullptr;
    QButtonGroup* m_textViewGroup = nullptr;
    bool m_imageVisibilityChosen = false;
    int m_imagePercent = 40;

    // 左侧：原图预览与文字框展示
    OcrImagePreviewWidget* m_imagePreview = nullptr;
    QScrollArea* m_imageScrollArea = nullptr;
    QPushButton* m_toggleBoxesBtn = nullptr;

    // 右侧：提取文字框与操作
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel* m_statusLabel = nullptr;

    // OCR 引擎下拉框
    QComboBox* m_ocrEngineCombo = nullptr;

    QPushButton* m_mergeBtn = nullptr;
    QPushButton* m_removeSpacesBtn = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;


    QString m_currentEngineType = "RapidOCR";
    QMap<QString, OcrResult> m_engineResultCache; // 引擎识别结果缓存，防止切换时重复识别

    // 翻译相关挂件与状态
    QWidget* m_translationSection = nullptr;
    QPlainTextEdit* m_translationEdit = nullptr;
    QComboBox* m_transModeCombo = nullptr;
    QComboBox* m_langPairCombo = nullptr;
    QLabel* m_transStatusBadge = nullptr;
    QPushButton* m_translateBtn = nullptr;
    QPushButton* m_copyTransBtn = nullptr;
    QLabel* m_transElapsedLabel = nullptr;
    QPointer<QNetworkReply> m_aiReply;
    bool m_isTranslating = false;
    bool m_translationRequested = false;
    QTimer* m_recognitionTimer = nullptr;
    TranslationRequest m_translationRequest;
    QList<TranslationSpan> m_translationSpans;
    QString m_translationSourceText;
    QString m_translationBeforeRequest;
    QString m_translationSourceLang;
    QString m_translationTargetLang;
    QString m_translationMode;
    quint64 m_translationGeneration = 0;
    int m_nextTranslationParagraph = 0;
    qint64 m_translationElapsedMs = 0;
};
