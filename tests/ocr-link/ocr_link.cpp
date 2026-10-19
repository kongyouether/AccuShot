#include <QtTest>
#include <QPainter>
#include <QFontDatabase>
#include <QSettings>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextBlock>
#include "ui/ocr/ocr_result_dialog.h"
#include "core/theme_manager.h"
#include "core/ocr/ocr_manager.h"
#include "core/translation/translation_plugin_manager.h"

class ControlledReply : public QNetworkReply {
public:
    explicit ControlledReply(QObject* parent) : QNetworkReply(parent) {}
    bool aborted = false;
    std::function<void()> whenAborted;
    void abort() override { aborted = true; if (whenAborted) whenAborted(); }
protected:
    qint64 readData(char*, qint64) override { return -1; }
};

class OcrLinkTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;

    struct ControlledTranslation {
        struct Request { QString text; OcrResultDialog::TranslationCallback callback; };
        QList<Request> requests;
        OcrResultDialog::TranslationRequest provider() {
            return [this](const QString& text, const QString&, const QString&,
                          OcrResultDialog::TranslationCallback callback, const QString&) -> QNetworkReply* {
                requests.append({text, std::move(callback)});
                return nullptr;
            };
        }
        void complete(int index, const QString& translated, bool success = true) {
            TranslationResult result;
            result.success = success;
            result.translatedText = translated;
            result.errorMessage = success ? QString() : QStringLiteral("Controlled failure");
            result.elapsedMs = 3;
            const auto callback = requests[index].callback;
            callback(result);
        }
    };

    OcrResult sample(qreal dpr = 1, bool logicalBoxes = true) {
        OcrResult result;
        result.success = true;
        result.dpr = dpr;
        QStringList texts{"Alpha", "Beta", "Gamma"};
        for (int i = 0; i < texts.size(); ++i) {
            OcrLine line;
            line.text = texts[i];
            const QRect box(30, i == 2 ? 1100 : 40 + i * 70, 150, 30);
            line.boundingBox = QRect(qRound(box.x() * dpr), qRound(box.y() * dpr),
                                     qRound(box.width() * dpr), qRound(box.height() * dpr));
            if (logicalBoxes) line.logicalBox = box;
            result.lines.append(line);
        }
        result.fullText = texts.join('\n');
        return result;
    }

    void populate(OcrResultDialog& dialog, const OcrResult& result) {
        QPixmap image(qRound(600 * result.dpr), qRound(1600 * result.dpr));
        image.setDevicePixelRatio(result.dpr);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Microsoft YaHei UI", 14));
        for (const auto& line : result.lines) {
            QRectF box = line.logicalBox;
            if (!box.isValid()) box = QRectF(line.boundingBox.x() / result.dpr,
                                            line.boundingBox.y() / result.dpr,
                                            line.boundingBox.width() / result.dpr,
                                            line.boundingBox.height() / result.dpr);
            painter.drawText(box, Qt::AlignVCenter, line.text);
        }
        painter.end();
        dialog.setImage(image);
        dialog.setResult(result, "WindowsMedia");
        dialog.show();
        QApplication::processEvents();
    }

    void clickLine(OcrResultDialog& dialog, int index) {
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, preview->lineRect(index).center().toPoint());
    }

    void selectText(QPlainTextEdit* edit, int start, int end) {
        auto cursor = edit->textCursor();
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
    }

    QPushButton* button(OcrResultDialog& dialog, const QString& text) {
        for (auto* button : dialog.findChildren<QPushButton*>())
            if (button->text() == text) return button;
        return nullptr;
    }

    void translate(OcrResultDialog& dialog) {
        dialog.setTranslationRequested(true);
        button(dialog, "翻译")->click();
    }

    void clickText(QPlainTextEdit* edit, int position) {
        QTextCursor cursor(edit->document());
        cursor.setPosition(position);
        {
            const QSignalBlocker blocker(edit);
            const auto originalCursor = edit->textCursor();
            edit->setTextCursor(cursor);
            edit->ensureCursorVisible();
            const int vertical = edit->verticalScrollBar()->value();
            const int horizontal = edit->horizontalScrollBar()->value();
            edit->setTextCursor(originalCursor);
            edit->verticalScrollBar()->setValue(vertical);
            edit->horizontalScrollBar()->setValue(horizontal);
        }
        QVERIFY(edit->viewport()->rect().contains(edit->cursorRect(cursor).center()));
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, edit->cursorRect(cursor).center());
        QApplication::processEvents();
    }

private slots:
    void initTestCase() {
        QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf") >= 0);
        QVERIFY(settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QApplication::setFont(QFont("Microsoft YaHei UI", 9));
    }

    void bidirectionalSelection_data() {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<bool>("logicalBoxes");
        for (qreal dpr : {1.0, 1.25, 2.0}) {
            QTest::newRow(qPrintable(QString("logical-%1").arg(dpr))) << dpr << true;
            QTest::newRow(qPrintable(QString("physical-%1").arg(dpr))) << dpr << false;
        }
    }

    void bidirectionalSelection() {
        QFETCH(qreal, dpr);
        QFETCH(bool, logicalBoxes);
        OcrResultDialog dialog;
        populate(dialog, sample(dpr, logicalBoxes));
        auto* edit = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        QVERIFY(qAbs(preview->lineRect(0).width() - 150) < 0.5);
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("Beta"));
        QCOMPARE(preview->selectedLines(), QList<int>{1});

        const QPoint first = preview->lineRect(0).topLeft().toPoint();
        const QPoint last = preview->lineRect(1).bottomRight().toPoint();
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, first);
        QTest::mouseMove(preview, last);
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, last);
        QCOMPARE(edit->textCursor().selectedText(), QString("Alpha\u2029Beta"));
        QCOMPARE(preview->selectedLines(), (QList<int>{0, 1}));

        selectText(edit, 11, 16);
        QCOMPARE(preview->selectedLines(), QList<int>{2});
        auto* scroll = dialog.findChild<QScrollArea*>("ocrImageScrollArea");
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
        const auto visibleCenter = preview->mapTo(scroll->viewport(), preview->lineRect(2).center().toPoint());
        QVERIFY(scroll->viewport()->rect().contains(visibleCenter));
        selectText(edit, 7, 7);
        QCOMPARE(preview->selectedLines(), QList<int>{1});
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, QPoint(250, 250));
        QVERIFY(!edit->textCursor().hasSelection());
        QVERIFY(preview->selectedLines().isEmpty());
    }

    void editedAndDeletedLines() {
        OcrResultDialog dialog;
        populate(dialog, sample());
        auto* edit = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        selectText(edit, 1, 1);
        edit->insertPlainText("X");
        clickLine(dialog, 0);
        QCOMPARE(edit->textCursor().selectedText(), QString("AXlpha"));
        selectText(edit, 0, 7); // Delete the complete first line, including its newline.
        edit->textCursor().removeSelectedText();
        QCOMPARE(edit->toPlainText(), QString("Beta\nGamma"));
        clickLine(dialog, 0);
        QVERIFY(!edit->textCursor().hasSelection());
        QVERIFY(preview->selectedLines().isEmpty());
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("Beta"));
        edit->undo();
        QCOMPARE(edit->toPlainText(), QString("AXlpha\nBeta\nGamma"));
        clickLine(dialog, 0);
        QCOMPARE(edit->textCursor().selectedText(), QString("AXlpha"));
        edit->redo();
        QCOMPARE(edit->toPlainText(), QString("Beta\nGamma"));
        clickLine(dialog, 0);
        QVERIFY(!edit->textCursor().hasSelection());
        clickLine(dialog, 1);
        edit->insertPlainText("BETA revised");
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("BETA revised"));
        selectText(edit, 13, 13);
        QCOMPARE(preview->selectedLines(), QList<int>{2});
    }

    void formattedLines() {
        OcrResultDialog dialog;
        populate(dialog, sample());
        auto* edit = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* merge = button(dialog, "合并段落");
        QVERIFY(merge);
        merge->click();
        QCOMPARE(edit->toPlainText(), QString("Alpha Beta Gamma"));
        for (int i = 0; i < 3; ++i) {
            clickLine(dialog, i);
            QCOMPARE(edit->textCursor().selectedText(), sample().lines[i].text);
        }
        edit->undo();
        QCOMPARE(edit->toPlainText(), QString("Alpha\nBeta\nGamma"));
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("Beta"));
        edit->redo();
        QCOMPARE(edit->toPlainText(), QString("Alpha Beta Gamma"));
        clickLine(dialog, 2);
        QCOMPARE(edit->textCursor().selectedText(), QString("Gamma"));
        auto result = sample();
        result.lines[0].text = "中 文";
        result.lines[1].text = "测 试";
        result.fullText = "中 文\n测 试\nGamma";
        dialog.setResult(result, "WindowsMedia");
        auto* spaces = button(dialog, "清除空格");
        QVERIFY(spaces);
        spaces->click();
        QCOMPARE(edit->toPlainText(), QString("中文\n测试\nGamma"));
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("测试"));
    }

    void inconsistentOcrTextDoesNotMapToOtherLine() {
        OcrResultDialog dialog;
        auto result = sample();
        result.fullText = "Alpha\nB e t a\nGamma";
        populate(dialog, result);
        auto* edit = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        clickLine(dialog, 1);
        QVERIFY(!edit->textCursor().hasSelection());
        QVERIFY(preview->selectedLines().isEmpty());
        clickLine(dialog, 2);
        QCOMPARE(edit->textCursor().selectedText(), QString("Gamma"));
        result.lines[0].text = "Beta";
        result.fullText = "B e t a\nBeta\nGamma";
        dialog.setResult(result, "WindowsMedia");
        clickLine(dialog, 0);
        QVERIFY(!edit->textCursor().hasSelection());
        clickLine(dialog, 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("Beta"));
    }

    void threeWaySelection_data() {
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("100-percent") << 1.0;
        QTest::newRow("125-percent") << 1.25;
        QTest::newRow("200-percent") << 2.0;
    }

    void threeWaySelection() {
        QFETCH(qreal, dpr);
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        populate(dialog, sample(dpr, false));
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        translate(dialog);
        QCOMPARE(service.requests.size(), 1);
        QCOMPARE(service.requests[0].text, QString("Alpha"));
        QVERIFY(source->isReadOnly());
        service.complete(0, "阿尔法\n续行");
        QTRY_COMPARE(service.requests.size(), 2);
        QCOMPARE(service.requests[1].text, QString("Beta"));
        service.complete(1, "贝塔");
        QTRY_COMPARE(service.requests.size(), 3);
        service.complete(2, "伽马\n内部\n三行");
        QVERIFY(!source->isReadOnly());
        QCOMPARE(target->toPlainText(), QString("阿尔法\n续行\n\n贝塔\n\n伽马\n内部\n三行"));

        clickLine(dialog, 1);
        QCOMPARE(source->textCursor().selectedText(), QString("Beta"));
        QCOMPARE(target->extraSelections().size(), 1);
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("贝塔"));
        QCOMPARE(preview->selectedLines(), QList<int>{1});
        selectText(source, 1, 3);
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("阿尔法\u2029续行"));

        selectText(source, 11, 16);
        const auto originalSelection = source->textCursor().selectedText();
        auto linkedStart = target->extraSelections()[0].cursor;
        linkedStart.setPosition(linkedStart.selectionStart());
        QVERIFY(target->viewport()->rect().contains(target->cursorRect(linkedStart).center()));
        const int beta = target->toPlainText().indexOf("贝塔");
        clickText(target, beta);
        QCOMPARE(source->textCursor().selectedText(), originalSelection);
        QCOMPARE(source->extraSelections().size(), 1);
        QCOMPARE(source->extraSelections()[0].cursor.selectedText(), QString("Beta"));
        QCOMPARE(preview->selectedLines(), QList<int>{1});
        QVERIFY(target->hasFocus());
        selectText(target, 0, beta + 1);
        QCOMPARE(source->extraSelections().size(), 2);
        QCOMPARE(preview->selectedLines(), (QList<int>{0, 1}));

        selectText(source, 2, 2);
        clickText(target, beta);
        QCOMPARE(preview->selectedLines(), QList<int>{1});
        clickText(source, 2); // Same collapsed source position; no cursor-position signal is required.
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("阿尔法\u2029续行"));
        clickText(target, beta); // Same target caret as before; switch correspondence back.
        QCOMPARE(source->extraSelections()[0].cursor.selectedText(), QString("Beta"));

        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, QPoint(250, 250));
        QVERIFY(source->extraSelections().isEmpty());
        QVERIFY(target->extraSelections().isEmpty());
        QVERIFY(preview->selectedLines().isEmpty());
    }

    void mergedParagraphAndEditedMapping() {
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        populate(dialog, sample());
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        button(dialog, "合并段落")->click();
        translate(dialog);
        QCOMPARE(service.requests.size(), 1);
        QCOMPARE(service.requests[0].text, QString("Alpha Beta Gamma"));
        service.complete(0, "完整段落\n译文仍可换行");
        clickLine(dialog, 1);
        QCOMPARE(source->textCursor().selectedText(), QString("Beta"));
        QCOMPARE(source->extraSelections()[0].cursor.selectedText(), QString("Alpha Beta Gamma"));
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("完整段落\u2029译文仍可换行"));
        QCOMPARE(preview->selectedLines(), (QList<int>{0, 1, 2}));
        clickText(target, 2);
        QCOMPARE(preview->selectedLines(), (QList<int>{0, 1, 2}));
        selectText(source, 1, 1);
        source->insertPlainText("X");
        QVERIFY(source->extraSelections().isEmpty());
        QVERIFY(target->extraSelections().isEmpty());
        QCOMPARE(target->toPlainText(), QString("完整段落\n译文仍可换行"));
        bool staleNotice = false;
        for (auto* label : dialog.findChildren<QLabel*>()) staleNotice |= label->text().contains("原文已修改");
        QVERIFY(staleNotice);
        clickText(target, 2);
        QVERIFY(source->extraSelections().isEmpty());
    }

    void textOnlyParagraphsAndAttributeChanges() {
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        OcrResult result;
        result.success = true;
        result.fullText = "First paragraph\n\nSecond paragraph";
        dialog.setResult(result, "WindowsMedia");
        dialog.show();
        translate(dialog);
        QCOMPARE(service.requests[0].text, QString("First paragraph"));
        service.complete(0, "第一段\n分为两行");
        QTRY_COMPARE(service.requests.size(), 2);
        QCOMPARE(service.requests[1].text, QString("Second paragraph"));
        service.complete(1, "第二段");
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        selectText(source, 18, 20);
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("第二段"));
        QTextCursor formatting(source->document());
        formatting.setPosition(0);
        formatting.setPosition(3, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setProperty(QTextFormat::UserProperty + 200, 42);
        formatting.mergeCharFormat(format); // The plain text snapshot is unchanged.
        QCOMPARE(target->extraSelections().size(), 1);
        clickText(target, 1);
        QCOMPARE(source->extraSelections()[0].cursor.selectedText(), QString("First paragraph"));
        QVERIFY(dialog.findChild<OcrImagePreviewWidget*>()->selectedLines().isEmpty());
    }

    void cancellationFailureRetryAndLateCallbacks() {
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        populate(dialog, sample());
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        translate(dialog);
        button(dialog, "取消翻译")->click();
        QVERIFY(!source->isReadOnly());
        service.complete(0, "迟到结果");
        QApplication::processEvents();
        QCOMPARE(service.requests.size(), 1);
        QVERIFY(target->toPlainText().isEmpty());
        translate(dialog);
        service.complete(1, "第一段");
        QTRY_COMPARE(service.requests.size(), 3);
        service.complete(2, "", false);
        QVERIFY(!source->isReadOnly());
        QVERIFY(target->toPlainText().isEmpty());
        QVERIFY(target->extraSelections().isEmpty());
        translate(dialog);
        service.complete(3, "成功重试一");
        QTRY_COMPARE(service.requests.size(), 5);
        service.complete(4, "成功重试二");
        QTRY_COMPARE(service.requests.size(), 6);
        service.complete(5, "成功重试三");
        QVERIFY(!source->isReadOnly());
        clickLine(dialog, 1);
        QCOMPARE(target->extraSelections()[0].cursor.selectedText(), QString("成功重试二"));

        translate(dialog);
        QCOMPARE(service.requests.size(), 7);
        auto replacement = sample();
        replacement.fullText = "Replacement";
        replacement.lines.clear();
        dialog.setResult(replacement, "WindowsMedia");
        service.complete(6, "旧识别迟到翻译");
        QCOMPARE(source->toPlainText(), QString("Replacement"));
        QVERIFY(target->toPlainText().isEmpty());
        QCOMPARE(service.requests.size(), 8);
        dialog.reject(); // The dialog remains alive after closing.
        service.complete(7, "关闭后迟到翻译");
        QVERIFY(!source->isReadOnly());
        QVERIFY(target->toPlainText().isEmpty());
        QApplication::processEvents();
        QCOMPARE(service.requests.size(), 8);

        auto* destroyed = new OcrResultDialog(nullptr, service.provider());
        destroyed->setResult(replacement, "WindowsMedia");
        translate(*destroyed);
        QCOMPARE(service.requests.size(), 9);
        delete destroyed;
        service.complete(8, "销毁后迟到翻译");
    }

    void renderThreeWayThemes() {
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            ControlledTranslation service;
            OcrResultDialog dialog(nullptr, service.provider());
            populate(dialog, sample(1.25));
            translate(dialog);
            service.complete(0, "第一段译文\n内部换行仍对应原段");
            QTRY_COMPARE(service.requests.size(), 2);
            service.complete(1, "第二段对应 Beta");
            QTRY_COMPARE(service.requests.size(), 3);
            service.complete(2, "第三段对应 Gamma");
            clickText(dialog.findChild<QPlainTextEdit*>("translationText"), 17);
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(mode == ThemeMode::Dark ? "tri-link-dark.png" : "tri-link-light.png"));
        }
    }

    void retranslationRestoresExistingTextAfterCancelOrFailure() {
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        populate(dialog, sample());
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        translate(dialog);
        service.complete(0, "已有第一段");
        QTRY_COMPARE(service.requests.size(), 2);
        service.complete(1, "已有第二段");
        QTRY_COMPARE(service.requests.size(), 3);
        service.complete(2, "已有第三段");
        const auto original = target->toPlainText();

        translate(dialog);
        QCOMPARE(target->toPlainText(), original);
        service.complete(3, "重译的第一段");
        QTRY_COMPARE(service.requests.size(), 5);
        button(dialog, "取消翻译")->click();
        QCOMPARE(target->toPlainText(), original);
        QVERIFY(!source->isReadOnly());
        QVERIFY(!target->isReadOnly());
        service.complete(4, "取消后的迟到结果");
        QApplication::processEvents();
        QCOMPARE(target->toPlainText(), original);
        QCOMPARE(service.requests.size(), 5);
        QVERIFY(target->extraSelections().isEmpty());

        translate(dialog);
        QCOMPARE(target->toPlainText(), original);
        service.complete(5, "再次重译的第一段");
        QTRY_COMPARE(service.requests.size(), 7);
        service.complete(6, "", false);
        QCOMPARE(target->toPlainText(), original);
        QVERIFY(!source->isReadOnly());
        QVERIFY(!target->isReadOnly());
        QVERIFY(target->extraSelections().isEmpty());
        QVERIFY(dialog.findChild<OcrImagePreviewWidget*>()->selectedLines().isEmpty());
        bool reasonVisible = false;
        for (auto* label : dialog.findChildren<QLabel*>())
            reasonVisible |= label->text().contains("Controlled failure") && label->isVisible();
        QVERIFY(reasonVisible);
    }

    void editingTranslationInvalidatesAllAssociations() {
        ControlledTranslation service;
        OcrResultDialog dialog(nullptr, service.provider());
        populate(dialog, sample());
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        translate(dialog);
        service.complete(0, "第一段");
        QTRY_COMPARE(service.requests.size(), 2);
        service.complete(1, "第二段");
        QTRY_COMPARE(service.requests.size(), 3);
        service.complete(2, "第三段");
        clickLine(dialog, 1);
        QVERIFY(!source->extraSelections().isEmpty());
        QVERIFY(!target->extraSelections().isEmpty());
        QCOMPARE(preview->selectedLines(), QList<int>{1});
        QVERIFY(!target->isReadOnly());
        target->moveCursor(QTextCursor::End);
        target->insertPlainText("（修订）");
        QVERIFY(target->toPlainText().endsWith("（修订）"));
        QVERIFY(source->extraSelections().isEmpty());
        QVERIFY(target->extraSelections().isEmpty());
        QVERIFY(preview->selectedLines().isEmpty());
        clickText(target, 1);
        QVERIFY(source->extraSelections().isEmpty());
        bool noticeVisible = false;
        for (auto* label : dialog.findChildren<QLabel*>())
            noticeVisible |= label->text().contains("译文已修改") && label->isVisible();
        QVERIFY(noticeVisible);
    }

    void imageZoomKeepsTextMapping_data() {
        QTest::addColumn<int>("percent");
        QTest::addColumn<QSizeF>("expectedSize");
        QTest::addColumn<qreal>("expectedLineOffset");
        QTest::newRow("75-percent") << 75 << QSizeF(112.8, 22.8) << qreal(52.8);
        QTest::newRow("200-percent") << 200 << QSizeF(300.8, 60.8) << qreal(140.8);
    }

    void imageZoomKeepsTextMapping() {
        QFETCH(int, percent);
        QFETCH(QSizeF, expectedSize);
        QFETCH(qreal, expectedLineOffset);
        OcrResultDialog dialog;
        const auto result = sample(1.25, false);
        // At DPR 1.25 this fixture rounds physical pixel coordinates before storing them:
        // Beta is (38,138,188,38), not an exact 150x30 logical-pixel rectangle.
        // Its logical size is 150.4x30.4 and its offset from Alpha is 70.4 pixels.
        QCOMPARE(result.lines[1].boundingBox, QRect(38, 138, 188, 38));
        populate(dialog, result);
        auto* zoom = dialog.findChild<QComboBox*>("imageZoom");
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        auto* scroll = dialog.findChild<QScrollArea*>("ocrImageScrollArea");
        zoom->setCurrentIndex(zoom->findData(percent));
        QApplication::processEvents();
        QCOMPARE(preview->lineRect(1).size(), expectedSize);
        QCOMPARE(preview->lineRect(1).topLeft() - preview->lineRect(0).topLeft(), QPointF(0, expectedLineOffset));
        const auto box = preview->lineRect(1);
        scroll->ensureVisible(qRound(box.center().x()), qRound(box.center().y()), 20, 20);
        clickLine(dialog, 1);
        QCOMPARE(source->textCursor().selectedText(), QString("Beta"));
        selectText(source, 0, 5);
        QCOMPARE(preview->selectedLines(), QList<int>{0});
    }

    void narrowComparisonKeepsAllThreePanels() {
        OcrResultDialog dialog;
        populate(dialog, sample());
        dialog.setTranslationRequested(true);
        dialog.resize(760, 800);
        QApplication::processEvents();
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        auto* image = dialog.findChild<QScrollArea*>("ocrImageScrollArea");
        auto* splitter = dialog.findChild<QSplitter*>("resultSplitter");
        QCOMPARE(splitter->orientation(), Qt::Vertical);
        QVERIFY(source->isVisible());
        QVERIFY(target->isVisible());
        QVERIFY(image->isVisible());
        QVERIFY(source->viewport()->height() > 0);
        QVERIFY(target->viewport()->height() > 0);
        QVERIFY(image->viewport()->height() > 0);
        button(dialog, "仅原文")->click();
        QVERIFY(source->isVisible());
        QVERIFY(!target->isVisible());
        button(dialog, "对照")->click();
        QVERIFY(source->isVisible());
        QVERIFY(target->isVisible());
    }

    void networkReplyCancellationAndSynchronousProvider() {
        auto* reply = new ControlledReply(this);
        OcrResultDialog dialog(nullptr, [reply](const QString&, const QString&, const QString&,
                                             OcrResultDialog::TranslationCallback callback, const QString&) {
            reply->whenAborted = [callback] {
                TranslationResult late;
                late.success = true;
                late.translatedText = "Cancelled reply callback";
                callback(late);
            };
            return reply;
        });
        populate(dialog, sample());
        translate(dialog);
        button(dialog, "取消翻译")->click();
        QVERIFY(reply->aborted);
        QVERIFY(dialog.findChild<QPlainTextEdit*>("translationText")->toPlainText().isEmpty());
        delete reply;

        int calls = 0;
        OcrResultDialog synchronous(nullptr, [&calls](const QString& text, const QString&, const QString&,
                                                     OcrResultDialog::TranslationCallback callback, const QString&) -> QNetworkReply* {
            ++calls;
            TranslationResult result;
            result.success = true;
            result.translatedText = "translated " + text;
            callback(result);
            return nullptr;
        });
        populate(synchronous, sample());
        translate(synchronous);
        QTRY_COMPARE(calls, 3);
        QVERIFY(!synchronous.findChild<QPlainTextEdit*>("sourceText")->isReadOnly());
        clickLine(synchronous, 2);
        QCOMPARE(synchronous.findChild<QPlainTextEdit*>("translationText")->extraSelections()[0].cursor.selectedText(),
                 QString("translated Gamma"));
    }

    void realLocalThreeWay() {
        if (!qEnvironmentVariableIsSet("ACCUSHOT_THREE_WAY_REAL_LOCAL"))
            QSKIP("Opt-in actual RapidOCR + installed Argos only; run beside build-release/plugins");
        QVERIFY(qEnvironmentVariable("ACCUSHOT_PLUGIN_OFFLINE_TEST") == "1");
        QVERIFY(OcrManager::instance().isRapidOcrReady());
        QVERIFY(TranslationPluginManager::instance().isOfflinePluginReady());
        OcrManager::instance().setPreferredEngineType("RapidOCR");
        TranslationPluginManager::instance().setPreferredEnginePreference("Offline");
        QPixmap image(1250, 500);
        image.setDevicePixelRatio(1.25);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Segoe UI", 20));
        painter.drawText(QPoint(35, 85), "First paragraph about green trees.");
        painter.drawText(QPoint(35, 235), "Second paragraph about blue rivers.");
        painter.end();
        OcrResultDialog dialog;
        dialog.setImage(image);
        dialog.setTranslationRequested(true);
        dialog.show();
        dialog.startRecognition();
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        auto* target = dialog.findChild<QPlainTextEdit*>("translationText");
        auto* preview = dialog.findChild<OcrImagePreviewWidget*>();
        QTRY_VERIFY_WITH_TIMEOUT(!target->toPlainText().isEmpty() && !source->isReadOnly() && button(dialog, "翻译"), 120000);
        QVERIFY(source->toPlainText().contains("trees", Qt::CaseInsensitive));
        QVERIFY(source->toPlainText().contains("rivers", Qt::CaseInsensitive));
        clickLine(dialog, 0);
        QVERIFY(!target->extraSelections().isEmpty());
        const QString imageLinked = target->extraSelections()[0].cursor.selectedText();
        const auto first = source->document()->firstBlock();
        selectText(source, first.position(), first.position() + first.text().size());
        QVERIFY(!target->extraSelections().isEmpty());
        const QString sourceLinked = target->extraSelections()[0].cursor.selectedText();
        const int linkedPosition = target->extraSelections()[0].cursor.selectionStart();
        clickText(target, linkedPosition);
        QVERIFY(!source->extraSelections().isEmpty());
        QVERIFY(!preview->selectedLines().isEmpty());
        QJsonArray rows;
        for (const auto& selection : source->extraSelections()) rows.append(selection.cursor.selectedText());
        QJsonObject evidence{{"ocr", source->toPlainText()}, {"translation", target->toPlainText()},
                             {"imageLinkedTranslation", imageLinked}, {"sourceLinkedTranslation", sourceLinked},
                             {"targetLinkedSource", rows}, {"dpr", image.devicePixelRatio()},
                             {"scope", "Actual local RapidOCR and Argos with artificial English paragraphs; paragraph correspondence"}};
        QFile file("tri-link-real-local.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(evidence).toJson(QJsonDocument::Indented)) > 0);
        file.close();
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(mode == ThemeMode::Dark ? "tri-link-real-dark.png" : "tri-link-real-light.png"));
        }
    }

    void renderSelectionThemes() {
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            OcrResultDialog dialog;
            populate(dialog, sample(1.25));
            clickLine(dialog, 1);
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(mode == ThemeMode::Dark ? "ocr-link-dark.png" : "ocr-link-light.png"));
        }
    }

    void textOnlyResultFormattingUndoAndFailure() {
        OcrResultDialog dialog;
        OcrResult result;
        result.success = true;
        result.fullText = "Original text";
        dialog.setResult(result, "WindowsMedia");
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        source->setPlainText(QStringLiteral("修 正后的文本\n第二行"));
        button(dialog, "清除空格")->click();
        QCOMPARE(source->toPlainText(), QStringLiteral("修正后的文本\n第二行"));
        source->undo();
        QCOMPARE(source->toPlainText(), QStringLiteral("修 正后的文本\n第二行"));
        result.success = false;
        result.errorMessage = "Test failure";
        dialog.setResult(result);
        QVERIFY(source->toPlainText().isEmpty());
        QVERIFY(dialog.findChild<OcrImagePreviewWidget*>()->selectedLines().isEmpty());
    }
};

QTEST_MAIN(OcrLinkTest)
#include "ocr_link.moc"
