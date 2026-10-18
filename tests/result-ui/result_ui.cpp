#include "core/plugin/local_plugin.h"
#include "core/ocr/ocr_manager.h"
#include "core/plugin/plugin_manager.h"
#include <QStandardItemModel>
#include "ui/overlay/snipping_overlay.h"
#include <QProcess>
#include <QScopeGuard>
#include <windows.h>
#include <dwmapi.h>
#include <QScreen>
#include "core/translation/translation_plugin_manager.h"
#include "core/ai/ai_service.h"
#include "core/smart_snapper.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QtTest>
#include <QStyleOptionButton>
#include "ui/magnifier/magnifier_widget.h"
#include <QApplication>
#include <QClipboard>
#include <QTemporaryDir>
#include <QSettings>
#include <QTimer>
#include <QFutureWatcher>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPainter>
#include <QFileDialog>
#include <QListWidget>
#include <QToolButton>
#include <QWheelEvent>
#include <QSpinBox>
#include <QMenu>
#include <algorithm>
#include <cmath>
#include "ui/plugin/plugin_market_dialog.h"
#include "ui/char_edit/single_char_editor.h"
#include "ui/capture/gif_recorder_widget.h"
#include <QContextMenuEvent>
#include "ui/pin/pin_window.h"
#include "ui/toolbar/floating_toolbar.h"
#include <QMessageBox>
#include "ui/settings/hotkey_settings_dialog.h"
#include "ui/settings/toolbar_settings_dialog.h"
#include "core/autostart_helper.h"
#include "ui/ocr/ocr_result_dialog.h"
#include "ui/translation/screen_translation_window.h"
#include "ui/formula/formula_result_dialog.h"
#include "ui/markdown/markdown_result_dialog.h"
#include "core/markdown/markdown_converter.h"
#include <QTextBrowser>
#include <QDirIterator>
#include "ui/capture/scroll_capture_widget.h"
#include "core/theme_manager.h"
#include "ui/themed_combo_box.h"
#include "ui/toolbar/toolbar_icons.h"

class ResultUiTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    QVariantMap nativeBefore;
    QPushButton* button(QWidget& widget, const QString& text) {
        for (auto* b : widget.findChildren<QPushButton*>()) if(b->text() == text) return b;
        return nullptr;
    }
private slots:
    void approvedV2VisualsAndChoiceInteraction() {
        const QString evidence=QStringLiteral("docs/evidence/ui-v2-2026-10-18");
        QVERIFY(QDir().mkpath(evidence));
        auto& theme=ThemeManager::instance();
        const auto oldTheme=theme.themeMode();
        const auto restore=qScopeGuard([&]{theme.setThemeMode(oldTheme,false);});
        const QStringList names={"arrow","capture","char","close","confirm","copy","copytext","ellipse","formula","gif","hotkey","layers","markdown","micro","more","mosaic","ocr","pencil","pin","plugin","rect","redo","save","screen","scrollh","scrollv","settings","snap","text","theme","translate","undo"};
        for(auto mode:{ThemeMode::Light,ThemeMode::Dark}) {
            theme.setThemeMode(mode,false);
            const auto palette=theme.palette();
            const QString suffix=mode==ThemeMode::Light?"light":"dark";
            QImage sheet(640,250,QImage::Format_ARGB32_Premultiplied);sheet.fill(palette.surface);
            QPainter painter(&sheet);
            for(int i=0;i<names.size();++i) {
                const auto icon=uiIcon(names[i],palette.text);
                QVERIFY2(!icon.isNull(),qPrintable(names[i]));
                QImage glyph=icon.pixmap(QSize(24,24)).toImage();int visible=0;
                for(int y=0;y<glyph.height();++y)for(int x=0;x<glyph.width();++x)if(qAlpha(glyph.pixel(x,y))>0)++visible;
                QVERIFY2(visible>10,qPrintable(names[i]));
                const int x=(i%8)*80,y=(i/8)*60;
                icon.paint(&painter,QRect(x+27,y+6,24,24));
                painter.setPen(palette.text);painter.drawText(QRect(x,y+34,80,22),Qt::AlignCenter,names[i]);
            }
            painter.end();QVERIFY(sheet.save(evidence+"/icons-"+suffix+".png"));
            QVERIFY(appIcon().pixmap(32,32).toImage()!=QImage());
            QVERIFY(uiIcon("pin",palette.text).pixmap(24,24).toImage()!=uiIcon("screen",palette.text).pixmap(24,24).toImage());
            HotkeySettingsDialog dialog;dialog.resize(1120,780);dialog.show();QTest::qWait(40);
            auto* navigation=dialog.findChild<QListWidget*>("settingsNavigation");QVERIFY(navigation);
            for(int i=0;i<navigation->count();++i) {
                const QRect before=navigation->visualItemRect(navigation->item(i));
                navigation->setCurrentRow(i);QApplication::processEvents();
                QCOMPARE(navigation->visualItemRect(navigation->item(i)).size(),before.size());
                QVERIFY(navigation->item(i)->font().bold()||navigation->font().bold());
            }
            navigation->setCurrentRow(0);QApplication::processEvents();
            for(int modeIndex=0;modeIndex<3;++modeIndex) {
                auto* choice=dialog.findChild<QPushButton*>(QString("themeChoice%1").arg(modeIndex));QVERIFY(choice);
                QVERIFY2(choice->height()>=145,"Theme preview must not collapse to a regular button height");
            }
            auto* themeChoice=dialog.findChild<QPushButton*>(mode==ThemeMode::Light?"themeChoice1":"themeChoice2");
            themeChoice->click();QCOMPARE(theme.themeMode(),mode);
            if(auto* toggle=dialog.findChild<QCheckBox*>("autoStart")) {
                const bool before=toggle->isChecked();toggle->setFocus();QTest::keyClick(toggle,Qt::Key_Space);
                QCOMPARE(toggle->isChecked(),!before);QTest::keyClick(toggle,Qt::Key_Space);QCOMPARE(toggle->isChecked(),before);
            }
            QVERIFY(dialog.grab().save(evidence+"/settings-"+suffix+".png"));
            auto* fonts=dialog.findChild<QComboBox*>("resultFontSize");QVERIFY(fonts);
            const int previous=fonts->currentIndex();
            dialog.raise();dialog.activateWindow();fonts->setFocus();QTest::qWait(50);
            QTest::keyClick(fonts,Qt::Key_Down);QTest::qWait(30);
            QCOMPARE(fonts->currentIndex(),previous);
            QTRY_VERIFY_WITH_TIMEOUT(fonts->view()->isVisible(),500);
            QVERIFY(fonts->view()->window()->grab().save(evidence+"/select-"+suffix+".png"));
            QTest::keyClick(fonts->view(),Qt::Key_Down);QTest::keyClick(fonts->view(),Qt::Key_Escape);
            QCOMPARE(fonts->currentIndex(),previous);
            fonts->showPopup();QTRY_VERIFY_WITH_TIMEOUT(fonts->view()->isVisible(),500);
            QTest::keyClick(fonts->view(),Qt::Key_End);QTest::keyClick(fonts->view(),Qt::Key_Return);
            QCOMPARE(fonts->currentData().toInt(),20);
            dialog.reject();
            theme.setThemeMode(mode,false);
            QWidget host;host.resize(1200,500);FloatingToolbar toolbar(&host);host.show();toolbar.show();
            toolbar.updatePosition(QRect(200,100,650,260),host.rect());QApplication::processEvents();
            QVERIFY(host.rect().contains(toolbar.geometry()));
            QVERIFY(toolbar.grab().save(evidence+"/toolbar-"+suffix+".png"));
            OcrResultDialog result;QPixmap image(600,360);image.fill(QColor("#FAFBF6"));
            {QPainter p(&image);p.setPen(QColor("#243E33"));p.drawText(QRect(30,60,540,240),Qt::TextWordWrap,QStringLiteral("A useful capture begins with a precise boundary.\nKeep image, source and translation together."));}
            result.setImage(image);OcrResult text;text.success=true;text.fullText=QStringLiteral("A useful capture begins with a precise boundary.\nKeep image, source and translation together.");result.setResult(text,"WindowsMedia");
            result.findChild<QPushButton*>("showTranslation")->setChecked(true);result.resize(1120,760);result.show();QTest::qWait(30);
            QVERIFY(result.grab().save(evidence+"/result-"+suffix+".png"));
        }
    }
    void independentFeatureSchemes() {
        QSettings cfg(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");
        AiProfile p;p.id="shared-account";p.name="Shared visual account";p.endpoint="https://example.com/v1";p.model="legacy-model";p.vision=true;p.key="test-only-key";
        QVERIFY(AiService::saveProfiles({p}));
        const auto cleanup=qScopeGuard([&]{cfg.remove("AI/TaskModels");cfg.remove("AI/TaskPrompts");cfg.remove("Markdown/Provider");AiService::saveProfiles(AiService::presets());PluginManager::instance().uninstallPlugin("markdown-vision");OcrManager::instance().setPreferredEngineType("WindowsMedia");TranslationPluginManager::instance().setPreferredEnginePreference("Online");FormulaOcrManager::instance().setPreferredMode("Online");});
        {
            HotkeySettingsDialog dialog;
            auto* selection=dialog.findChild<QComboBox*>("markdownDefault");QVERIFY(selection);
            selection->setCurrentIndex(selection->findData("ai:shared-account"));
            dialog.findChild<QLineEdit*>("markdownModel")->setText("cancelled-model");dialog.reject();
        }
        QCOMPARE(AiService::forTask(p.id,"markdown").model,QString("legacy-model"));
        QVERIFY(!cfg.contains("AI/TaskModels/markdown/shared-account"));
        HotkeySettingsDialog dialog;dialog.show();
        QVERIFY(!dialog.findChild<QLineEdit*>("aiModel"));
        for(const QString task:{"ocr","translation","formula","markdown"}) {
            auto* choice=dialog.findChild<QComboBox*>(task+"Default");QVERIFY(choice);
            choice->setCurrentIndex(choice->findData("ai:shared-account"));
            dialog.findChild<QLineEdit*>(task+"Model")->setText(task+"-model");
            dialog.findChild<QPlainTextEdit*>(task+"Prompt")->setPlainText(task+" instruction");
        }
        // Switching to a local option and back preserves this feature's model draft.
        auto* ocr=dialog.findChild<QComboBox*>("ocrDefault");ocr->setCurrentIndex(ocr->findData("WindowsMedia"));
        QVERIFY(!dialog.findChild<QLineEdit*>("ocrModel")->isEnabled());
        ocr->setCurrentIndex(ocr->findData("ai:shared-account"));
        QCOMPARE(dialog.findChild<QLineEdit*>("ocrModel")->text(),QString("ocr-model"));
        auto* pages=dialog.findChild<QTabWidget*>("settingsPages");
        const auto evidence=qEnvironmentVariable("EVAN_SCHEME_UI_DIR","docs/evidence/feature-schemes-2026-10-18");QDir().mkpath(evidence);
        for(auto mode:{ThemeMode::Light,ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            for(int page:{5,7,8,9}) {pages->setCurrentIndex(page);QApplication::processEvents();QVERIFY(dialog.grab().save(evidence+QString("/page-%1-%2.png").arg(page).arg(mode==ThemeMode::Light?"light":"dark")));}
        }
        dialog.findChild<QPushButton*>("settingsSave")->click();QCOMPARE(dialog.result(),int(QDialog::Accepted));
        for(const QString task:{"ocr","translation","formula","markdown"}) {
            const auto configured=AiService::forTask(p.id,task);
            QCOMPARE(configured.model,task+"-model");QCOMPARE(configured.key,p.key);
            QCOMPARE(AiService::taskPrompt(p.id,task,{}),task+" instruction");
            QCOMPARE(AiService::payload(configured,"test")["model"].toString(),task+"-model");
        }
        QCOMPARE(AiService::profile(p.id).model,QString("legacy-model"));
        QCOMPARE(OcrManager::instance().preferredEngineType(),QString("ai:shared-account"));
        QCOMPARE(TranslationPluginManager::instance().preferredEnginePreference(),QString("ai:shared-account"));
        QCOMPARE(FormulaOcrManager::instance().preferredMode(),QString("ai:shared-account"));
        QCOMPARE(cfg.value("Markdown/Provider").toString(),QString("ai:shared-account"));
        bool available=false;for(const auto& item:MarkdownConverter::providers())if(item.id=="ai:shared-account")available=item.available;QVERIFY(available);
        // Changing one feature never rewrites the shared account or another feature.
        HotkeySettingsDialog changed;changed.findChild<QLineEdit*>("markdownModel")->setText("markdown-new");changed.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(AiService::forTask(p.id,"markdown").model,QString("markdown-new"));
        QCOMPARE(AiService::forTask(p.id,"translation").model,QString("translation-model"));
        OcrResult plain;plain.success=true;plain.fullText="Online text\nwithout coordinates";
        OcrResultDialog result;result.setResult(plain,p.name);QCOMPARE(result.findChild<QPlainTextEdit*>("sourceText")->toPlainText(),plain.fullText);
    }
    void markdownPlugins() {
        auto& plugins=PluginManager::instance();
        for(const QString id:{"markdown-vision","markdown-mathpix"}) {
            QVERIFY(!LocalPlugin::ready(id));
            plugins.installPlugin(id);QVERIFY(LocalPlugin::ready(id));
            QCOMPARE(plugins.pluginInfo(id).status,PluginStatus::Installed);
            QVERIFY(plugins.uninstallPlugin(id));QVERIFY(!LocalPlugin::ready(id));
        }
        QImage image(100,80,QImage::Format_RGB32);image.fill(Qt::white);
        bool called=false;
        MarkdownConverter::convert("pix2text-page",image,this,[&](MarkdownResult r){called=true;QVERIFY(!r.success);QVERIFY(!r.error.isEmpty());});
        QVERIFY(called);
        const auto escaped=MarkdownConverter::plainText("# title\n<x> | [link](url)");
        QVERIFY(escaped.startsWith("\\# title  \n"));QVERIFY(escaped.contains("\\<x\\>"));
        auto presets=AiService::presets();
        QVERIFY(AiService::parse("openai",R"({"choices":[{"message":{"content":"partial"},"finish_reason":"length"}]})").truncated);
        for(const auto& p:presets) {
            if(p.id=="qwen-markdown") {
                QVERIFY(p.vision);QCOMPARE(p.model,QString("qwen3-vl-plus"));
                const auto body=AiService::payload(p,MarkdownConverter::prompt(),image);
                const auto content=body["messages"].toArray().first().toObject()["content"].toArray();
                QCOMPARE(content.size(),2);QCOMPARE(content[1].toObject()["type"].toString(),QString("image_url"));
                QVERIFY(AiService::parse("openai",R"({"choices":[{"message":{"content":"# Test"}}]})").success);
            }
            if(p.protocol=="mathpix") {
                QCOMPARE(AiService::payload(p,{},image)["formats"].toArray().first().toString(),QString("text"));
                QCOMPARE(AiService::parse("mathpix",R"({"text":"$x=1$"})").text,QString("$x=1$"));
            }
        }
    }
    void markdownExportAndThemes() {
        QTemporaryDir output;
        MarkdownResult result;result.files=std::make_shared<QTemporaryDir>();
        QDir().mkpath(result.files->filePath("assets/figures"));
        QImage image(160,80,QImage::Format_RGB32);image.fill(Qt::cyan);
        QVERIFY(image.save(result.files->filePath("assets/figures/test.png")));
        QString error;
        const QString markdown="# 标题\n\n| 项目 | 数值 |\n|---|---|\n| Alpha | 12 |\n\n```cpp\nint n = 2;\n```\n\n$x^2$\n\n![](assets/figures/test.png)";
        const auto path=output.filePath("含空格 output.md");
        QVERIFY2(MarkdownConverter::save(path,markdown,result,&error),qPrintable(error));
        QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));const auto content=QString::fromUtf8(saved.readAll());
        QVERIFY(!content.contains("![](assets/"));
        const int begin=content.indexOf("![](")+4;const auto relative=content.mid(begin,content.indexOf(')',begin)-begin);
        QVERIFY(QFile::exists(output.filePath(relative)));
        QVERIFY(!MarkdownConverter::save(output.filePath("missing/output.md"),"text",{},&error));
        MarkdownResultDialog dialog;dialog.setImage(QPixmap::fromImage(image));dialog.show();
        auto* edit=dialog.findChild<QPlainTextEdit*>("markdownSource");QVERIFY(edit);
        edit->setPlainText(markdown);QVERIFY(dialog.findChild<QPushButton*>("markdownSave")->isEnabled());
        const auto evidence=qEnvironmentVariable("EVAN_MARKDOWN_UI_DIR","docs/evidence/markdown-2026-10-18");
        QDir().mkpath(evidence);
        const auto old=ThemeManager::instance().themeMode();
        for(const auto theme:{ThemeMode::Light,ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(theme,false);QTest::qWait(40);
            QVERIFY(dialog.grab().save(evidence+(theme==ThemeMode::Light?"/light.png":"/dark.png")));
            dialog.findChild<QTabWidget*>()->setCurrentIndex(1);QTest::qWait(20);
            QVERIFY(dialog.grab().save(evidence+(theme==ThemeMode::Light?"/preview-light.png":"/preview-dark.png")));
            dialog.findChild<QTabWidget*>()->setCurrentIndex(0);
        }
        ThemeManager::instance().setThemeMode(old,false);
    }
    void markdownRealLocal() {
        if(!qEnvironmentVariableIsSet("EVAN_MARKDOWN_LOCAL"))QSKIP("Explicit real-model probe only");
        const QString provider=qEnvironmentVariable("EVAN_MARKDOWN_LOCAL");
        const QImage image(qEnvironmentVariable("EVAN_MARKDOWN_IMAGE"));QVERIFY(!image.isNull());
        bool done=false;MarkdownResult result;QElapsedTimer timer;timer.start();
        MarkdownConverter::convert(provider,image,this,[&](MarkdownResult r){result=std::move(r);done=true;});
        QTRY_VERIFY_WITH_TIMEOUT(done,185000);
        QVERIFY2(result.success,qPrintable(result.error));
        const QString output=qEnvironmentVariable("EVAN_MARKDOWN_OUTPUT");QString error;
        QVERIFY2(MarkdownConverter::save(output,result.text,result,&error),qPrintable(error));
        qInfo()<<"provider"<<provider<<"elapsed_ms"<<timer.elapsed()<<"characters"<<result.text.size();
    }
    void markdownScreenshotEntry() {
        auto& overlay=SnippingOverlay::instance();
        overlay.triggerCancelAction();
        QPixmap sample(640,480);sample.fill(Qt::white);
        overlay.m_snapshot.fullSnapshot=sample;
        overlay.m_snapshot.virtualGeometry=QRect(0,0,640,480);
        overlay.m_snapshot.isValid=true;
        overlay.m_selectionRect=QRect(30,30,400,200);
        overlay.m_state=SnippingState::Selected;
        const auto cleanup=qScopeGuard([&]{overlay.triggerCancelAction();});
        auto* action=overlay.findChild<QToolButton*>("tool_markdown");QVERIFY(action);
        overlay.m_toolbar->setOcrBusy(true);QVERIFY(!action->isEnabled());
        overlay.m_toolbar->setOcrBusy(false);QVERIFY(action->isEnabled());
        QSignalSpy finished(&overlay,&SnippingOverlay::snippingFinished);
        action->click();QCOMPARE(finished.count(),1);
        QPointer<MarkdownResultDialog> result;
        for(auto* window:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<MarkdownResultDialog*>(window))result=dialog;
        QVERIFY(result);QVERIFY(result->isVisible());
        QVERIFY(result->findChild<QPlainTextEdit*>("markdownSource")->toPlainText().isEmpty());
        result->close();QTRY_VERIFY(result.isNull());
    }
    void initTestCase() {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
        QApplication::setFont(QFont("Microsoft YaHei UI", 9));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QVERIFY(QSettings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan").fileName().startsWith(settings.path()));
        QSettings native(QSettings::NativeFormat, QSettings::UserScope, "Evan", "Evan");
        for (const auto& key : native.allKeys()) nativeBefore.insert(key, native.value(key));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qApp->setStyleSheet(ThemeManager::instance().getApplicationStyle());
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [] {
            qApp->setStyleSheet(ThemeManager::instance().getApplicationStyle());
        });
    }
    void cleanupTestCase() {
        QSettings native(QSettings::NativeFormat, QSettings::UserScope, "Evan", "Evan");
        QVariantMap after;
        for (const auto& key : native.allKeys()) after.insert(key, native.value(key));
        QVERIFY2(after == nativeBefore, "Tests must not modify real user settings");
    }
    void manualLocalUi() {
        if (!qEnvironmentVariableIsSet("EVAN_MANUAL_LOCAL_UI")) QSKIP("Interactive native verification only");
        QVERIFY(QApplication::platformName() == "windows");
        OcrManager::instance().setPreferredEngineType("RapidOCR");
        FormulaOcrManager::instance().setPreferredMode("Offline");
        TranslationPluginManager::instance().setPreferredEnginePreference("Offline");
        QDialog host;
        host.setWindowTitle("Evan 本地插件原生验收");
        auto* layout = new QVBoxLayout(&host);
        layout->addWidget(new QLabel("临时设置 · 真实产品窗口 · 真实本地模型", &host));
        const QString samples = qEnvironmentVariable("EVAN_MANUAL_SAMPLES");
        auto* settingsButton = new QPushButton("打开设置", &host);
        auto* ocrButton = new QPushButton("文字识别与翻译样本", &host);
        auto* formulaButton = new QPushButton("公式样本", &host);
        for (auto* b : {settingsButton, ocrButton, formulaButton}) layout->addWidget(b);
        connect(settingsButton, &QPushButton::clicked, &host, [&] {
            auto* d = new HotkeySettingsDialog; d->setAttribute(Qt::WA_DeleteOnClose); d->show();
        });
        connect(ocrButton, &QPushButton::clicked, &host, [&] {
            auto* d = new OcrResultDialog; d->setAttribute(Qt::WA_DeleteOnClose);
            d->setImage(QPixmap(samples + "/ocr.png")); d->show(); d->startRecognition();
        });
        connect(formulaButton, &QPushButton::clicked, &host, [&] {
            auto* d = new FormulaResultDialog; d->setAttribute(Qt::WA_DeleteOnClose);
            d->show(); d->startRecognition(QPixmap(samples + "/fraction.png"));
        });
        host.resize(440, 220); host.show();
        QEventLoop loop;
        connect(&host, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    void ocrResultUsesActualEngine() {
        OcrResultDialog dialog;
        OcrResult result; result.success = true; result.fullText = "Native OCR result";
        dialog.setResult(result, "Windows.Media.Ocr");
        QComboBox* engine = nullptr;
        for (auto* combo : dialog.findChildren<QComboBox*>())
            if (combo->findData("WindowsMedia") >= 0) engine = combo;
        QVERIFY(engine);
        QCOMPARE(engine->currentData().toString(), QString("WindowsMedia"));
        QCOMPARE(dialog.findChild<QPlainTextEdit*>("sourceText")->toPlainText(), result.fullText);
    }
    void installedLocalPluginsUi() {
        if(!qEnvironmentVariableIsSet("EVAN_TEST_INSTALLED_PLUGINS"))QSKIP("Separate real-model installation test; run beside installed plugins");
        for(const auto& id:{"translation-en-zh","latex-ocr-engine","pix2text","paddleformula"})QVERIFY2(LocalPlugin::ready(id),id);
        HotkeySettingsDialog dialog;dialog.show();auto* pages=dialog.findChild<QTabWidget*>("settingsPages");QVERIFY(pages);
        bool finished=false;bool success=false;
        connect(&PluginManager::instance(),&PluginManager::pluginStatusChanged,this,[&](const QString& id,PluginStatus state,const QString&){
            if(id=="translation-en-zh"&&(state==PluginStatus::Installed||state==PluginStatus::Failed)){finished=true;success=state==PluginStatus::Installed;}
        });
        PluginManager::instance().installPlugin("translation-en-zh");
        QTRY_VERIFY_WITH_TIMEOUT(finished,90000);QVERIFY(success);
        for(auto mode:{ThemeMode::Light,ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            const QString suffix=mode==ThemeMode::Dark?"dark":"light";
            for(int index:{2,6,7,8}) {pages->setCurrentIndex(index);QApplication::processEvents();QVERIFY(dialog.grab().save("screenshots/local-installed-"+QString::number(index)+"-"+suffix+".png"));}
        }
        auto* formula=dialog.findChild<QComboBox*>("formulaDefault");QVERIFY(formula);
        auto* model=qobject_cast<QStandardItemModel*>(formula->model());QVERIFY(model);
        for(const auto& id:{"Offline","local:pix2text","local:paddleformula"})QVERIFY(model->item(formula->findData(id))->isEnabled());
        dialog.reject();
    }
    void localPluginsRequireInstallation() {
        const auto root=LocalPlugin::directory("pix2text");
        QVERIFY2(!QDir(root).exists(),"Run installation-state regression in the isolated build-result-ui directory");
        const auto cleanup=qScopeGuard([&]{QDir(root).removeRecursively();PluginManager::instance().refreshAllStatus();});
        QVERIFY(!LocalPlugin::ready("pix2text"));
        HotkeySettingsDialog dialog;
        auto* combo=dialog.findChild<QComboBox*>("formulaDefault");QVERIFY(combo);
        const int index=combo->findData("local:pix2text");QVERIFY(index>=0);
        auto* model=qobject_cast<QStandardItemModel*>(combo->model());QVERIFY(model);QVERIFY(!model->item(index)->isEnabled());
        bool done=false;FormulaOcrResult result;QImage sample(20,20,QImage::Format_RGB32);sample.fill(Qt::white);
        FormulaOcrManager::instance().recognizeAsync(sample,1,[&](const FormulaOcrResult& value){result=value;done=true;},"local:pix2text");
        QTRY_VERIFY(done);QVERIFY(!result.success);QVERIFY(result.errorMessage.contains("安装"));
        for (const auto& mode : {"Offline", "local:paddleformula"}) {
            done = false;
            FormulaOcrManager::instance().recognizeAsync(sample, 1, [&](const FormulaOcrResult& value){result=value;done=true;}, mode);
            QTRY_VERIFY(done); QVERIFY(!result.success); QVERIFY(result.errorMessage.contains("安装"));
        }
        done = false;
        OcrManager::instance().recognizeWithEngine("RapidOCR", sample, 1, [&](const OcrResult& value, const QString&) {
            QVERIFY(!value.success); QVERIFY(value.errorMessage.contains("安装")); done = true;
        });
        QTRY_VERIFY(done);
        done = false;
        TranslationPluginManager::instance().translateAsync("test", "en", "zh", [&](const TranslationResult& value) {
            QVERIFY(!value.success); QVERIFY(value.errorMessage.contains("安装")); done = true;
        }, "Offline");
        QTRY_VERIFY(done);
        QDir().mkpath(root+"/venv/Scripts");
        for(const auto& name:{"runner.py","venv/Scripts/python.exe"}){QFile file(root+"/"+name);QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        QFile weights(root+"/model.onnx");QVERIFY(weights.open(QIODevice::WriteOnly));weights.resize(1024*1024+1);weights.close();
        QFile marker(root+"/installed.json");QVERIFY(marker.open(QIODevice::WriteOnly));marker.write(R"({"id":"pix2text","weights":["model.onnx"]})");marker.close();
        PluginManager::instance().refreshAllStatus();QVERIFY(LocalPlugin::ready("pix2text"));QVERIFY(model->item(combo->findData("local:pix2text"))->isEnabled());
        QVERIFY(weights.remove());PluginManager::instance().refreshAllStatus();QVERIFY(!LocalPlugin::ready("pix2text"));QVERIFY(!model->item(combo->findData("local:pix2text"))->isEnabled());
        QVERIFY(PluginManager::instance().uninstallPlugin("pix2text"));QVERIFY(!QDir(root).exists());
        AiProfile legacy;legacy.protocol="formula";legacy.endpoint="http://127.0.0.1:8787/recognize";
        bool called=false;AiService::run(legacy,"test",sample,this,[&](AiResult r){called=true;QVERIFY(!r.success);});QVERIFY(called);
    }
    void aiProtocolsAndStorage() {
        AiProfile p;p.id="test-service";p.name="测试服务";p.endpoint="https://example.com/v1";p.model="test-model";p.key="test-secret";p.headers={{"private-header","header-secret"}};
        QVERIFY(AiService::saveProfiles({p}));
        const auto saved=AiService::profiles();QCOMPARE(saved.size(),1);QCOMPARE(saved[0].key,p.key);QCOMPARE(saved[0].headers,p.headers);
        const auto json=QSettings(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan").value("AI/Profiles").toString();QVERIFY(!json.contains("test-secret"));QVERIFY(!json.contains("header-secret"));
        QCOMPARE(AiService::request(p).url().path(),QString("/v1/chat/completions"));
        QVERIFY(AiService::payload(p,"test")["messages"].isArray());
        QVERIFY(AiService::parse("openai",R"({"choices":[{"message":{"content":"你好"}}]})").success);
        QVERIFY(AiService::parse("claude",R"({"content":[{"text":"你好"}]})").success);
        QVERIFY(AiService::parse("gemini",R"({"candidates":[{"content":{"parts":[{"text":"你好"}]}}]})").success);
        QVERIFY(AiService::parse("mathpix",R"({"text":"x^2"})").success);
        QVERIFY(AiService::parse("formula",R"({"latex":"x^2"})").success);
        QVERIFY(!AiService::parse("openai","{}").success);QVERIFY(!AiService::parse("openai","not-json").success);
        p.protocol="claude";QCOMPARE(AiService::request(p).rawHeader("x-api-key"),QByteArray("test-secret"));
        p.protocol="gemini";QVERIFY(AiService::request(p).url().path().endsWith("test-model:generateContent"));
        QImage sample(10,10,QImage::Format_RGB32);sample.fill(Qt::white);QVERIFY(AiService::payload(p,"formula",sample)["contents"].isArray());
        QVERIFY(AiService::saveProfiles(AiService::presets()));
    }
    void aiNetworkAndCancel() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&]{auto* socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[socket]{socket->readAll();const QByteArray body=R"({"choices":[{"message":{"content":"示例译文"}}]})";socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
        AiProfile p;p.endpoint=QString("http://127.0.0.1:%1").arg(server.serverPort());p.model="test";
        bool done=false;AiResult result;AiService::run(p,"test",{},this,[&](AiResult r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY(result.success);QCOMPARE(result.text,QString("示例译文"));
        done=false;auto* reply=AiService::run(p,"test",{},this,[&](AiResult r){result=r;done=true;});QVERIFY(reply);reply->abort();QTRY_VERIFY(done);QVERIFY(!result.success);
        done=false;p.endpoint="http://example.com";QVERIFY(!AiService::run(p,"test",{},this,[&](AiResult r){result=r;done=true;}));QVERIFY(done);QVERIFY(!result.success);
    }
    void localApiCannotBypassPlugins() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        AiProfile p;p.id="legacy-local";p.name="Legacy local API";p.endpoint=QString("http://127.0.0.1:%1").arg(server.serverPort());p.model="test";p.vision=true;
        QVERIFY(AiService::saveProfiles({p})); // Existing custom configuration can remain stored.
        bool done=false;TranslationResult translated;
        TranslationPluginManager::instance().translateAsync("test","en","zh",[&](const TranslationResult& r){translated=r;done=true;},"ai:legacy-local");
        QTRY_VERIFY(done);QVERIFY(!translated.success);QVERIFY(translated.errorMessage.contains("插件"));
        done=false;FormulaOcrResult formula;QImage sample(20,20,QImage::Format_RGB32);sample.fill(Qt::white);
        FormulaOcrManager::instance().recognizeAsync(sample,1,[&](const FormulaOcrResult& r){formula=r;done=true;},"ai:legacy-local");
        QTRY_VERIFY(done);QVERIFY(!formula.success);QVERIFY(formula.errorMessage.contains("插件"));QVERIFY(!server.hasPendingConnections());
        done=false;
        FormulaOcrManager::instance().recognizeAsync(sample,1,[&](const FormulaOcrResult& r){formula=r;done=true;},"local:removed-plugin");
        QVERIFY(done);QVERIFY(!formula.success);QVERIFY(formula.errorMessage.contains("原公式引擎不可用"));
        HotkeySettingsDialog dialog;
        QVERIFY(dialog.findChild<QComboBox*>("formulaDefault")->findData("ai:legacy-local")<0);
        QVERIFY(dialog.findChild<QComboBox*>("translationDefault")->findData("ai:legacy-local")<0);
        const auto previous=FormulaOcrManager::instance().preferredMode();
        FormulaOcrManager::instance().setPreferredMode("ai:pix2text");QCOMPARE(FormulaOcrManager::instance().preferredMode(),QString("local:pix2text"));
        FormulaOcrManager::instance().setPreferredMode(previous);
        QVERIFY(AiService::saveProfiles(AiService::presets()));
    }
    void settingsServicesAndSnapDraft() {
        auto cfg=QSettings(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");cfg.remove("Snapping/Mode");
        {HotkeySettingsDialog dialog;auto* snap=dialog.findChild<QComboBox*>("snapMode");QVERIFY(snap);QCOMPARE(snap->currentData().toString(),QString("Window"));QVERIFY(snap->findData("Hybrid")>=0);snap->setCurrentIndex(snap->findData("Hybrid"));dialog.reject();}
        QVERIFY(!cfg.contains("Snapping/Mode"));
        HotkeySettingsDialog dialog;dialog.show();auto* pages=dialog.findChild<QTabWidget*>("settingsPages");QVERIFY(pages);dialog.openPlugins();QCOMPARE(pages->currentIndex(),6);QVERIFY(!dialog.findChildren<PluginCardWidget*>().isEmpty());
        QDir().mkpath("screenshots");for(auto mode:{ThemeMode::Light,ThemeMode::Dark}){ThemeManager::instance().setThemeMode(mode);for(int index:{5,6,7,1}){pages->setCurrentIndex(index);QApplication::processEvents();QVERIFY(dialog.grab().save(QString("screenshots/phase5-settings-%1-%2.png").arg(index).arg(mode==ThemeMode::Light?"light":"dark")));}}
    }
    void selectionCursorOutside() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires a native screenshot surface");
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan").setValue("Snapping/EnableSmartSnapping", false);
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            if (auto* focused = QApplication::focusWidget()) focused->clearFocus();
            overlay.triggerCancelAction();
            QApplication::processEvents();
        });
        overlay.startSnipping();
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.cursor().shape(), Qt::CrossCursor);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QVERIFY(overlay.hasValidSelection());
        const auto move = [&](QPoint point) {
            QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(overlay.mapToGlobal(point)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&overlay, &event);
        };
        move(QPoint(50, 50)); QCOMPARE(overlay.cursor().shape(), Qt::ArrowCursor);
        move(QPoint(200, 200)); QCOMPARE(overlay.cursor().shape(), Qt::SizeAllCursor);
        move(QPoint(400, 200)); QCOMPARE(overlay.cursor().shape(), Qt::SizeHorCursor);
        for (auto tool : {ToolAction::Rect, ToolAction::Text}) {
            overlay.selectTool(tool);
            move(QPoint(50, 50)); QCOMPARE(overlay.cursor().shape(), Qt::ArrowCursor);
            move(QPoint(200, 200));
            QCOMPARE(overlay.cursor().shape(), tool == ToolAction::Text ? Qt::IBeamCursor : Qt::CrossCursor);
        }
        // Selecting the current tool again returns to moving the existing region.
        overlay.selectTool(ToolAction::Text);
        move(QPoint(200, 200)); QCOMPARE(overlay.cursor().shape(), Qt::SizeAllCursor);
        QVERIFY(overlay.hasValidSelection());
        overlay.selectTool(ToolAction::Cancel);
        QTest::mouseClick(&overlay, Qt::RightButton, Qt::NoModifier, QPoint(50, 50));
        QVERIFY(!overlay.hasValidSelection());
        QCOMPARE(overlay.cursor().shape(), Qt::CrossCursor);
    }
    void actionHoverFeedback() {
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            QWidget host;
            host.setStyleSheet(ThemeManager::instance().getResultDialogStyle());
            auto* layout = new QVBoxLayout(&host);
            for (const auto& name : {"secondaryAction", "primaryAction", "saveBtn", "stopBtn", "recBtn", "dangerAction"}) {
                auto* button = new QPushButton("操作反馈", &host);
                button->setObjectName(name);
                layout->addWidget(button);
            }
            host.show(); QApplication::processEvents();
            for (auto* button : host.findChildren<QPushButton*>()) {
                const auto renderState = [button](bool hovered) {
                    QStyleOptionButton option;
                    option.initFrom(button);
                    option.text = button->text();
                    option.state &= ~QStyle::State_MouseOver;
                    if (hovered) option.state |= QStyle::State_MouseOver;
                    QImage image(button->size(), QImage::Format_ARGB32_Premultiplied);
                    image.fill(Qt::transparent);
                    QPainter painter(&image);
                    button->style()->drawControl(QStyle::CE_PushButton, &option, &painter, button);
                    return image;
                };
                QVERIFY2(renderState(false) != renderState(true), qPrintable(button->objectName()));
                button->setEnabled(false);
                QVERIFY2(renderState(false) == renderState(true), qPrintable(button->objectName()));
                button->setEnabled(true);
            }
        }
    }
    void trayMenuHoverFeedback() {
        QDir().mkpath("screenshots");
        QDir().mkpath("docs/evidence/accushot-brand-tray-2026-10-18");
        const auto brand=appIcon();
        for(int size:{16,20,24,32,40,48,64,128,256}) {
            QVERIFY(brand.availableSizes().contains(QSize(size,size)));
            QVERIFY(!brand.pixmap(size,size).isNull());
        }
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            QMenu menu;
            menu.setStyleSheet(ThemeManager::instance().getTrayMenuStyle());
            const auto ink=ThemeManager::instance().palette().text;
            auto* capture = menu.addAction(uiIcon("capture",ink),"开始截屏\tF1");
            auto* pin = menu.addAction(uiIcon("pin",ink),"桌面贴图\tF3");
            menu.addSeparator();
            auto* preferences=menu.addAction(uiIcon("settings",ink),"设置…");
            menu.addSeparator();
            auto* quit=menu.addAction(uiIcon("close",ink),"退出 AccuShot");
            menu.popup(QPoint(100, 100));
            QTRY_VERIFY_WITH_TIMEOUT(menu.isVisible(),500);
            const auto first=menu.actionGeometry(capture);
            QVERIFY(menu.width()>=224);
            QVERIFY(first.left()>=8 && first.top()>=8);
            QVERIFY(first.height()>=36);
            for(auto* action:{pin,preferences,quit}) {
                QCOMPARE(menu.actionGeometry(action).height(),first.height());
                QCOMPARE(menu.actionGeometry(action).left(),first.left());
            }
            for (auto* action : {capture, pin}) {
                const QRect rect = menu.actionGeometry(action);
                // Dispatch to this menu explicitly; offscreen platforms do not route
                // global cursor motion through popup mouse grabs.
                QMouseEvent move(QEvent::MouseMove, QPointF(rect.center()),
                                 QPointF(menu.mapToGlobal(rect.center())),
                                 Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(&menu, &move);
                QTRY_COMPARE(menu.activeAction(), action);
                const auto rendered = menu.grab().toImage();
                const QPoint sample = rect.topLeft() + QPoint(8, rect.height() / 2);
                QCOMPARE(rendered.pixelColor(sample * rendered.devicePixelRatio()), ThemeManager::instance().palette().selected);
            }
            QVERIFY(menu.grab().save(mode == ThemeMode::Light ? "screenshots/tray-hover-light.png" : "screenshots/tray-hover-dark.png"));
            QVERIFY(menu.grab().save(QString("docs/evidence/accushot-brand-tray-2026-10-18/tray-%1.png").arg(mode==ThemeMode::Light?"light":"dark")));
            QSignalSpy activated(preferences,&QAction::triggered);
            pin->setEnabled(false);
            menu.setActiveAction(capture);
            QTest::keyClick(&menu,Qt::Key_Down);
            QCOMPARE(menu.activeAction(),preferences);
            QTest::keyClick(&menu,Qt::Key_Return);
            QCOMPARE(activated.count(),1);
            menu.close();
        }
    }
    void renderDialogPopups() {
        QDir().mkpath("screenshots");
        FormulaResultDialog formula;
        OcrResultDialog ocr;
        ocr.findChild<QPushButton*>("showTranslation")->setChecked(true);
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            const QString suffix = mode == ThemeMode::Light ? "light" : "dark";
            formula.show(); QApplication::processEvents();
            QVERIFY(formula.grab().save("screenshots/formula-" + suffix + ".png"));
            int index = 0;
            for (QDialog* dialog : {static_cast<QDialog*>(&formula), static_cast<QDialog*>(&ocr)}) {
                dialog->show(); QApplication::processEvents();
                for (auto* combo : dialog->findChildren<QComboBox*>()) {
                    if (!combo->isVisible() || !combo->isEnabled()) continue;
                    combo->showPopup(); QApplication::processEvents();
                    QVERIFY(combo->view()->window()->grab().save(QString("screenshots/dialog-popup-%1-%2.png").arg(index++).arg(suffix)));
                    combo->hidePopup();
                }
            }
            QVERIFY(index >= 3);
        }
    }
    void nativeTitleBarAndSingleRowCategories() {
        if(QGuiApplication::platformName()!="windows") QSKIP("Native DWM window frame requires Windows platform");
        auto& theme=ThemeManager::instance();const auto previous=theme.themeMode();
        const auto restore=qScopeGuard([&]{theme.setThemeMode(previous,false);});
        QDir().mkpath("docs/evidence/window-frame-2026-10-18");
        HotkeySettingsDialog settingsDialog;settingsDialog.resize(760,560);settingsDialog.show();
        auto* navigation=settingsDialog.findChild<QListWidget*>("settingsNavigation");QVERIFY(navigation);
        for(int row=0;row<navigation->count();++row)
            if(navigation->item(row)->text()=="插件与模型") navigation->setCurrentRow(row);
        auto verifyRow=[&](QWidget& root) {
            int rowY=-1,count=0;
            for(auto* btn:root.findChildren<QPushButton*>()) {
                if(!btn->property("pluginCategory").isValid()) continue;
                const QPoint position=btn->mapTo(&root,QPoint());
                if(rowY<0)rowY=position.y();
                QCOMPARE(position.y(),rowY);
                QVERIFY(root.rect().contains(QRect(position,btn->size())));
                QVERIFY(btn->width()>=btn->fontMetrics().horizontalAdvance(btn->text())+24);
                ++count;
            }
            QCOMPARE(count,6);
        };
        auto verifyFrame=[&](QWidget& widget) {
            const auto colors=theme.palette();
            const HWND handle=reinterpret_cast<HWND>(widget.winId());
            BOOL dark=FALSE;
            QCOMPARE(DwmGetWindowAttribute(handle,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark)),S_OK);
            QCOMPARE(bool(dark),theme.isDarkMode());
            // Caption/text colors are write-only DWM attributes; verify the rendered frame.
            widget.raise();widget.activateWindow();QTest::qWait(60);
            const auto frame=widget.frameGeometry();
            const auto origin=frame.topLeft()-widget.screen()->geometry().topLeft();
            const auto pixels=widget.screen()->grabWindow(0,origin.x(),origin.y(),frame.width(),frame.height()).toImage();
            const int captionHeight=widget.geometry().top()-frame.top();
            QVERIFY(captionHeight>0);
            QCOMPARE(pixels.pixelColor(QPoint(frame.width()/2,captionHeight/2)*pixels.devicePixelRatio()),colors.surface);
        };
        for(auto mode:{ThemeMode::Dark,ThemeMode::Light,ThemeMode::Auto}) {
            theme.setThemeMode(mode,false);settingsDialog.raise();settingsDialog.activateWindow();QTest::qWait(100);
            QVERIFY(settingsDialog.width()>=1120);verifyRow(settingsDialog);verifyFrame(settingsDialog);
            if(mode!=ThemeMode::Auto) {
                const auto frame=settingsDialog.frameGeometry();
                const auto origin=frame.topLeft()-settingsDialog.screen()->geometry().topLeft();
                QVERIFY(settingsDialog.screen()->grabWindow(0,origin.x(),origin.y(),frame.width(),frame.height()).save(
                    QString("docs/evidence/window-frame-2026-10-18/settings-%1.png").arg(mode==ThemeMode::Dark?"dark":"light")));
            }
        }
        PluginMarketDialog plugins;plugins.resize(500,520);plugins.show();QTest::qWait(50);
        QVERIFY(plugins.width()>=800);verifyRow(plugins);verifyFrame(plugins);
        settingsDialog.reject();
    }
    void pluginCategoryCoverage() {
        PluginMarketDialog dialog;dialog.resize(820,640);dialog.show();
        auto* search=dialog.findChild<QLineEdit*>();QVERIFY(search);
        auto select=[&](const QString& key) {
            for(auto* btn:dialog.findChildren<QPushButton*>())
                if(btn->property("pluginCategory").toString()==key) {btn->click();return btn->isChecked();}
            return false;
        };
        auto visibleIds=[&] {
            QStringList result;
            for(auto* card:dialog.findChildren<PluginCardWidget*>())
                if(!card->isHidden()) result.append(card->property("pluginId").toString());
            result.sort();return result;
        };
        const QMap<QString,QStringList> expected={
            {"文字识别",{"rapidocr-engine"}},
            {"离线翻译",{"translation-en-zh"}},
            {"公式识别",{"latex-ocr-engine","paddleformula","pix2text"}},
            {"Markdown",{"markdown-mathpix","markdown-vision","pix2text-page"}}};
        QStringList all;
        for(auto it=expected.cbegin();it!=expected.cend();++it) {
            QVERIFY(select(it.key()));QCOMPARE(visibleIds(),it.value());
            all.append(it.value());
        }
        all.sort();QVERIFY(select("All"));QCOMPARE(visibleIds(),all);
        QCOMPARE(all.size(),PluginManager::instance().plugins().size());
        QVERIFY(select("Markdown"));search->setText("Pix2Text");
        QCOMPARE(visibleIds(),QStringList{"pix2text-page"});
        PluginManager::instance().refreshAllStatus();
        QCOMPARE(visibleIds(),QStringList{"pix2text-page"});
        search->setText("no-such-plugin");QVERIFY(visibleIds().isEmpty());search->clear();
        QVERIFY(select("Installed"));
        QStringList installed;for(const auto& p:PluginManager::instance().plugins())
            if(p.status==PluginStatus::Installed) installed.append(p.id);
        installed.sort();QCOMPARE(visibleIds(),installed);
        QVERIFY(select("Markdown"));
        QDir().mkpath("docs/evidence/window-frame-2026-10-18");
        for(auto mode:{ThemeMode::Light,ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode,false);QApplication::processEvents();
            for(auto* btn:dialog.findChildren<QPushButton*>()) {
                if(!btn->property("pluginCategory").isValid())continue;
                QVERIFY(dialog.rect().contains(QRect(btn->mapTo(&dialog,QPoint()),btn->size())));
            }
            QVERIFY(dialog.grab().save(QString("docs/evidence/window-frame-2026-10-18/markdown-%1.png").arg(mode==ThemeMode::Light?"light":"dark")));
        }
        dialog.close();
    }
    void pluginCardStates() {
        PluginInfo info; info.id = "test"; info.name = "测试插件";
        PluginCardWidget card(info); card.show();
        auto* action = card.findChild<QPushButton*>("primaryAction"); QVERIFY(action);
        QSignalSpy install(&card, &PluginCardWidget::installRequested);
        QSignalSpy cancel(&card, &PluginCardWidget::cancelRequested);
        action->click(); QCOMPARE(install.count(), 1);
        info.status = PluginStatus::Downloading; card.updateInfo(info);
        QCOMPARE(action->text(), QString("取消下载")); action->click(); QCOMPARE(cancel.count(), 1);
        info.status = PluginStatus::Extracting; card.updateInfo(info); QVERIFY(!action->isEnabled());
        info.status = PluginStatus::Installed; card.updateInfo(info); QVERIFY(!action->isEnabled());
        QVERIFY(card.findChild<QPushButton*>("dangerAction")->isVisible());
        info.status = PluginStatus::Failed; card.updateInfo(info);
        QCOMPARE(action->text(), QString("重试")); action->click(); QCOMPARE(install.count(), 2);
    }
    void unifiedControlsAndAuxiliaryWindows() {
        QDir().mkpath("screenshots");
        QVERIFY(!QPixmap(":/ui/down-light.png").isNull());
        QVERIFY(!QPixmap(":/ui/down-dark.png").isNull());
        QWidget canvas; canvas.resize(1000, 600); canvas.show();
        SingleCharEditor editor(&canvas);
        SingleCharUnit unit; unit.character = "字"; unit.logicalBox = QRect(100, 100, 24, 24);
        unit.estimatedFontFamily = "MiSans"; unit.estimatedFontSize = 14;
        editor.startEdit(unit, canvas.rect());
        QCOMPARE(editor.findChild<QComboBox*>()->currentData().toString(), QString("MiSans"));
        QCOMPARE(editor.currentFont().family(), QString("MiSans"));
        PluginMarketDialog plugins;
        plugins.resize(740, 520); plugins.show();
        auto* search = plugins.findChild<QLineEdit*>(); QVERIFY(search);
        search->setText("no-such-plugin");
        for (auto* card : plugins.findChildren<PluginCardWidget*>()) QVERIFY(card->isHidden());
        search->clear();
        ScrollCaptureSession scroll;
        QImage frame(200, 100, QImage::Format_RGB32); frame.fill(Qt::gray);
        scroll.start(QRect(100, 100, 200, 100), frame);
        GifRecordSession gif; gif.start(QRect(100, 100, 200, 100));
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            const QString suffix = mode == ThemeMode::Light ? "light" : "dark";
            QApplication::processEvents();
            QVERIFY(plugins.grab().save("screenshots/plugins-" + suffix + ".png"));
            QMessageBox notification(QMessageBox::Information, "操作完成", "图片已保存。", QMessageBox::Ok);
            notification.show(); QApplication::processEvents();
            QVERIFY(notification.grab().save("screenshots/message-" + suffix + ".png"));
            notification.close();
            QVERIFY(editor.grab().save("screenshots/char-editor-" + suffix + ".png"));
            MagnifierWidget magnifier;
            QPixmap sample(200, 200); sample.fill(QColor("#8099aa"));
            magnifier.updatePosition(QPoint(100, 100), sample, QRect(0, 0, 500, 500));
            magnifier.show();
            QVERIFY(magnifier.grab().save("screenshots/magnifier-" + suffix + ".png"));
            for (auto* w : QApplication::topLevelWidgets()) {
                if (w->windowTitle() == "Evan 长截图" || w->windowTitle() == "Evan GIF 录制")
                    QVERIFY(w->grab().save("screenshots/" + QString(w->windowTitle().contains("GIF") ? "gif-" : "scroll-") + suffix + ".png"));
            }
            HotkeySettingsDialog settingsDialog; settingsDialog.show();
            auto* combo = settingsDialog.findChild<QComboBox*>("themeMode"); QVERIFY(combo);
            combo->setFocus(); combo->showPopup(); QApplication::processEvents();
            QVERIFY(combo->view()->window()->grab().save("screenshots/combo-popup-" + suffix + ".png"));
            QTest::keyClick(combo->view(), Qt::Key_Down); QTest::keyClick(combo->view(), Qt::Key_Return);
            combo->hidePopup();
            combo->setEnabled(false);
            QVERIFY(settingsDialog.grab().save("screenshots/controls-disabled-" + suffix + ".png"));
        }
        QSignalSpy committed(&editor, &SingleCharEditor::editingCommitted);
        QTest::keyClick(editor.findChild<QLineEdit*>(), Qt::Key_Return);
        QCOMPARE(committed.count(), 1);
        scroll.cancel(); gif.cancel();
        QVERIFY(!scroll.isActive()); QVERIFY(!gif.isActive());
        OcrResultDialog ocr;
        OcrResult result; result.success = true; result.fullText = "基金 161725 招商中证白酒";
        QApplication::clipboard()->setText("unchanged");
        ocr.setResult(result, "test");
        for (auto* b : ocr.findChildren<QPushButton*>()) {
            QVERIFY(!b->text().contains("基金")); QVERIFY(!b->toolTip().contains("估值"));
        }
        QCOMPARE(QApplication::clipboard()->text(), QString("unchanged"));
        QCOMPARE(ocr.findChild<QPlainTextEdit*>("sourceText")->toPlainText(), result.fullText);
    }
    void pinZoomAndShortcuts() {
        QPixmap image(400, 240); image.fill(QColor("#447799")); image.setDevicePixelRatio(2);
        PinWindow pin(image, QRect(100, 100, 200, 120));
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        const auto scroll = [&](QPoint delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QPointF pos = pin.contentRect().center();
            QWheelEvent event(pos, pin.mapToGlobal(pos.toPoint()), QPoint(), delta, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&pin, &event);
        };
        const QPoint center = pin.geometry().center();
        scroll(QPoint(120, 0));
        QCOMPARE(pin.contentRect().size(), QSize(200, 120));
        scroll(QPoint(0, 120));
        QCOMPARE(pin.contentRect().size(), QSize(220, 132));
        QVERIFY((pin.geometry().center() - center).manhattanLength() <= 2);
        scroll(QPoint(0, -120));
        QCOMPARE(pin.contentRect().size(), QSize(200, 120));
        scroll(QPoint(0, 60));
        QVERIFY(pin.contentRect().width() > 200 && pin.contentRect().width() < 220);
        const QSize beforeOpacity = pin.size();
        scroll(QPoint(0, -120), Qt::ControlModifier);
        QCOMPARE(pin.size(), beforeOpacity);
        const auto original = HotkeyConfig::instance().data();
        auto custom = original; custom.pinCopy = "Ctrl+K";
        HotkeyConfig::instance().setData(custom);
        QApplication::clipboard()->setText("unchanged");
        QTest::keyClick(&pin, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("unchanged"));
        QTest::keyClick(&pin, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->pixmap().size(), image.size());
        HotkeyConfig::instance().setData(original);
        QTest::keyClick(&pin, Qt::Key_Escape);
        QVERIFY(!pin.isVisible());
    }
    void pinSaveAndMenu() {
        QPixmap image(160, 90); image.fill(QColor("#447799"));
        PinWindow pin(image, QRect(100, 100, 160, 90));
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        QTimer fileDialog;
        bool save = false;
        const QString path = settings.filePath("pin.png");
        connect(&fileDialog, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
                    if (save) { dialog->selectFile(path); QMetaObject::invokeMethod(dialog, "accept"); }
                    else dialog->reject();
                }
            }
        });
        fileDialog.start(20);
        QTest::keyClick(&pin, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(pin.isVisible()); QVERIFY(!QFile::exists(path));
        save = true;
        QTest::keyClick(&pin, Qt::Key_S, Qt::ControlModifier);
        fileDialog.stop();
        QCOMPARE(QImage(path).size(), image.size());
        QVERIFY(pin.isVisible());
        QDir().mkpath("screenshots");
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            bool captured = false;
            QTimer::singleShot(20, this, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu) return;
                captured = menu->grab().save(QString("screenshots/pin-menu-%1.png").arg(mode == ThemeMode::Light ? "light" : "dark"));
                menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, pin.contentRect().center(), pin.mapToGlobal(pin.contentRect().center()));
            QApplication::sendEvent(&pin, &event);
            QVERIFY(captured); QVERIFY(pin.isVisible());
        }
        QTest::mouseDClick(&pin, Qt::LeftButton);
        QVERIFY(!pin.isVisible());
    }
    void snapWheelCandidateNavigation() {
        const auto original = HotkeyConfig::instance().data();
        const auto restore = qScopeGuard([&] { HotkeyConfig::instance().setData(original); });
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction();
            overlay.clearFocus();
            QApplication::processEvents();
        });
        QImage image(640, 480, QImage::Format_RGB32);
        image.fill(QColor("#243440"));
        {
            QPainter painter(&image);
            painter.fillRect(QRect(60, 60, 500, 360), QColor("#e6edf0"));
            painter.fillRect(QRect(140, 100, 340, 280), QColor("#71858e"));
            painter.fillRect(QRect(240, 180, 140, 120), Qt::white);
        }
        const QPoint point(300, 240);
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapper.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        overlay.m_snapper.processSnapshot(image);
        overlay.m_state = SnippingState::Idle;
        overlay.m_enableSmartSnapping = true;
        overlay.m_smartCandidates.clear();
        overlay.updateSmartCandidates(point);
        auto byArea = overlay.m_smartCandidates;
        std::stable_sort(byArea.begin(), byArea.end(), [](const QRect& a, const QRect& b) {
            return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
        });
        QVERIFY(byArea.size() >= 3);
        overlay.m_candidateIndex = size_t(std::find(overlay.m_smartCandidates.begin(),
            overlay.m_smartCandidates.end(), byArea.front()) - overlay.m_smartCandidates.begin());
        auto candidate = [&] { return overlay.m_smartCandidates[overlay.m_candidateIndex]; };
        auto wheel = [&](int delta, const QPoint& at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QWheelEvent event(at, overlay.mapToGlobal(at), QPoint(), QPoint(0, delta),
                              Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&overlay, &event);
        };
        // High-resolution deltas accumulate to one detent, then reverse immediately.
        wheel(60, point); QCOMPARE(candidate(), byArea.front());
        wheel(60, point); QCOMPARE(candidate(), byArea[1]);
        wheel(-120, point); QCOMPARE(candidate(), byArea.front());
        wheel(-120, point); QCOMPARE(candidate(), byArea.front());
        for (size_t i = 1; i < byArea.size(); ++i) {
            wheel(120, point);
            QCOMPARE(candidate(), byArea[i]);
        }
        wheel(120, point); QCOMPARE(candidate(), byArea.back());
        wheel(120, point); QCOMPARE(candidate(), byArea.back());
        wheel(-120, point); QCOMPARE(candidate(), byArea[byArea.size() - 2]);
        wheel(-120 * int(byArea.size() + 2), point); QCOMPARE(candidate(), byArea.front());
        wheel(120, point); QCOMPARE(candidate(), byArea[1]);
        const QRect beforeMove = candidate();
        QMouseEvent move(QEvent::MouseMove, point + QPoint(1, 1),
                         overlay.mapToGlobal(point + QPoint(1, 1)),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &move);
        QCOMPARE(candidate(), beforeMove);
        QVERIFY(!overlay.updateSmartCandidates(point)); // Same query as the native poll.
        QCOMPARE(candidate(), beforeMove);

        // Tab retains the snapper's original ranking and wrapping behavior.
        overlay.m_candidateIndex = overlay.m_smartCandidates.size() - 1;
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        overlay.keyPressEvent(&tab);
        QCOMPARE(overlay.m_candidateIndex, size_t{0});
        QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        overlay.keyPressEvent(&backtab);
        QCOMPARE(overlay.m_candidateIndex, overlay.m_smartCandidates.size() - 1);

        // Swapping wheel bindings reverses behavior without changing area ranking.
        auto custom = HotkeyConfig::instance().data();
        custom.snippingSmallerCandidate = "WheelUp";
        custom.snippingLargerCandidate = "WheelDown";
        HotkeyConfig::instance().setData(custom);
        wheel(120 * int(byArea.size() + 2), point); QCOMPARE(candidate(), byArea.front());
        wheel(-120, point); QCOMPARE(candidate(), byArea[1]);

        // Keyboard and modified wheel bindings replace the old input, and persist on reload.
        custom.snippingSmallerCandidate = "Ctrl+J";
        custom.snippingLargerCandidate = "Alt+WheelUp";
        HotkeyConfig::instance().setData(custom);
        HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingLargerCandidate, QString("Alt+WheelUp"));
        wheel(120, point); QCOMPARE(candidate(), byArea[1]);
        wheel(-120, point); QCOMPARE(candidate(), byArea[1]);
        wheel(120, point, Qt::AltModifier); QCOMPARE(candidate(), byArea[2]);
        QKeyEvent smaller(QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
        QApplication::sendEvent(&overlay, &smaller); QCOMPARE(candidate(), byArea[1]);
        custom.snippingLargerCandidate = "Ctrl+K";
        HotkeyConfig::instance().setData(custom);
        QKeyEvent larger(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        QApplication::sendEvent(&overlay, &larger); QCOMPARE(candidate(), byArea[2]);
        QApplication::sendEvent(&overlay, &smaller); QCOMPARE(candidate(), byArea[1]);
        overlay.m_state = SnippingState::Selecting;
        QApplication::sendEvent(&overlay, &smaller); QCOMPARE(candidate(), byArea[1]);
        overlay.m_state = SnippingState::Idle;
        custom.snippingSmallerCandidate.clear();
        custom.snippingLargerCandidate.clear();
        HotkeyConfig::instance().setData(custom);
        QApplication::sendEvent(&overlay, &smaller); QCOMPARE(candidate(), byArea[1]);
        wheel(120, point, Qt::AltModifier); QCOMPARE(candidate(), byArea[1]);
        HotkeyConfig::instance().setData(HotkeyConfigData{});

        // A new point refreshes the candidate list; selected/dragging/disabled paths ignore the wheel.
        wheel(-60, QPoint(5, 5));
        QCOMPARE(overlay.m_smartCandidates, overlay.m_snapper.findCandidatesAt(QPoint(5, 5)));
        QCOMPARE(overlay.m_candidateIndex, size_t{0});
        const auto before = overlay.m_smartCandidates;
        overlay.m_selectionRect = QRect(20, 20, 80, 60);
        for (auto state : {SnippingState::Selected, SnippingState::Selecting}) {
            overlay.m_state = state;
            wheel(-120, point);
            QCOMPARE(overlay.m_smartCandidates, before);
            QCOMPARE(overlay.m_selectionRect, QRect(20, 20, 80, 60));
        }
        overlay.m_state = SnippingState::Idle;
        overlay.m_enableSmartSnapping = false;
        wheel(-120, point);
        QCOMPARE(overlay.m_smartCandidates, before);
        overlay.m_enableSmartSnapping = true;
        wheel(-120, QPoint(-50, -50));
        QVERIFY(overlay.m_smartCandidates.empty());
    }
    void snapRankedCandidateNavigation() {
        const auto original = HotkeyConfig::instance().data();
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction(); overlay.clearFocus();
            HotkeyConfig::instance().setData(original);
            QApplication::processEvents();
        });
        QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::white);
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapper.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        overlay.m_snapper.processSnapshot(image);
        overlay.m_state = SnippingState::Idle;
        overlay.m_enableSmartSnapping = true;
        overlay.m_smartCandidates.clear();
        overlay.m_candidatesManuallyChosen = false;
        overlay.m_currentTool = ToolAction::Cancel;
        overlay.m_candidateContext = {};
        const QPoint point(300, 240);
        const QRect card(180, 130, 240, 220), window = image.rect();
        const QRect text(260, 220, 90, 40), icon(290, 230, 24, 24);
        const auto pendingPoll = overlay.m_snapper.findCandidatesAt(point);
        const auto context = overlay.m_snapper.candidateContext();
        // The complete card leads relevance ranking, but sits between smaller details and a window by area.
        const std::vector<QRect> ranked = {card, window, text, icon};
        QVERIFY(pendingPoll != ranked);
        QVERIFY(overlay.applySmartCandidates(point, ranked, context));
        QCOMPARE(overlay.m_candidateIndex, size_t{0});
        auto candidate = [&] { return overlay.m_smartCandidates[overlay.m_candidateIndex]; };
        auto wheel = [&](int delta) {
            QWheelEvent event(point, overlay.mapToGlobal(point), QPoint(), QPoint(0, delta),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&overlay, &event);
        };
        QCOMPARE(candidate(), card);
        // The first wheel event starts at the relevance winner, even if a pending poll offers another list.
        wheel(-120); QCOMPARE(candidate(), text);
        QVERIFY(overlay.m_candidatesManuallyChosen);
        QCOMPARE(overlay.m_smartCandidates, ranked);
        const std::vector<QRect> late = {QRect(295, 235, 12, 12), text, card, window};
        QVERIFY(!overlay.applySmartCandidates(point, late, context));
        QCOMPARE(candidate(), text);
        QCOMPARE(overlay.m_smartCandidates, ranked);

        wheel(120); QCOMPARE(candidate(), card);
        wheel(120); QCOMPARE(candidate(), window);
        wheel(120); QCOMPARE(candidate(), window);
        wheel(-120); QCOMPARE(candidate(), card);
        wheel(-120); QCOMPARE(candidate(), text);
        wheel(-120); QCOMPARE(candidate(), icon);
        wheel(-120); QCOMPARE(candidate(), icon);
        wheel(120); QCOMPARE(candidate(), text);
        QCOMPARE(overlay.m_smartCandidates, ranked);

        // Tab walks relevance ranking from the selected rectangle's original index, including wrap.
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        overlay.keyPressEvent(&tab); QCOMPARE(candidate(), icon);
        overlay.keyPressEvent(&tab); QCOMPARE(candidate(), card);
        QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        overlay.keyPressEvent(&backtab); QCOMPARE(candidate(), icon);
        QVERIFY(!overlay.applySmartCandidates(point, late, context));
        QCOMPARE(candidate(), icon);
        QCOMPARE(overlay.m_smartCandidates, ranked);
    }
    void snapAsyncCandidateStability() {
        const auto original = HotkeyConfig::instance().data();
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction(); overlay.clearFocus();
            HotkeyConfig::instance().setData(original);
            QApplication::processEvents();
        });
        QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::white);
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapper.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        overlay.m_snapper.processSnapshot(image);
        overlay.m_state = SnippingState::Idle;
        overlay.m_enableSmartSnapping = true;
        overlay.m_smartCandidates.clear();
        overlay.m_candidatesManuallyChosen = false;
        overlay.m_currentTool = ToolAction::Cancel;
        overlay.m_candidateContext = {};
        const QPoint point(300, 240);
        const QRect window = image.rect(), visual(180, 130, 240, 220), element(260, 210, 90, 70);
        overlay.m_snapper.findCandidatesAt(point);
        const auto context = overlay.m_snapper.candidateContext();
        auto candidate = [&] { return overlay.m_smartCandidates[overlay.m_candidateIndex]; };
        // Exercise the same production merge endpoint used by polling and CV completion.
        overlay.applySmartCandidates(point, {window}, context);
        overlay.applySmartCandidates(point, {visual, window}, context);
        QCOMPARE(candidate(), visual); // An initial window may upgrade to CV.
        overlay.applySmartCandidates(point, {element, visual, window}, context);
        QCOMPARE(candidate(), element); // The first CV selection may upgrade to UIA.
        overlay.applySmartCandidates(point, {element.adjusted(1, 1, -1, -1), visual, window}, context);
        QCOMPARE(candidate(), element); // Near-identical late edges do not flicker.

        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        overlay.keyPressEvent(&tab);
        QCOMPARE(candidate(), visual);
        const auto frozen = overlay.m_smartCandidates;
        const std::vector<QRect> late = {QRect(280, 220, 40, 40), element, visual, window};
        QVERIFY(!overlay.applySmartCandidates(point, late, context));
        QCOMPARE(overlay.m_smartCandidates, frozen);
        QCOMPARE(candidate(), visual);
        // Real wheel dispatch freezes the list even when the snapper now offers another list.
        QWheelEvent wheel(point, overlay.mapToGlobal(point), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&overlay, &wheel);
        QCOMPARE(candidate(), window);
        QMouseEvent hover(QEvent::MouseMove, point + QPoint(2, 1),
                          overlay.mapToGlobal(point + QPoint(2, 1)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &hover);
        QVERIFY(!overlay.applySmartCandidates(point + QPoint(2, 1), late, context));
        QCOMPARE(candidate(), window);
        QCOMPARE(overlay.m_smartCandidates, frozen);
        QMouseEvent depart(QEvent::MouseMove, point + QPoint(8, 0),
                           overlay.mapToGlobal(point + QPoint(8, 0)),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &depart);
        QVERIFY(!overlay.m_candidatesManuallyChosen);
        QVERIFY(overlay.applySmartCandidates(point + QPoint(8, 0), late, context));
        QCOMPARE(candidate(), late.front());
        QVERIFY(!overlay.m_candidatesManuallyChosen);

        overlay.keyPressEvent(&tab);
        auto otherOwner = context; otherOwner.ownerWindow = 42;
        QVERIFY(overlay.applySmartCandidates(point, {visual, window}, otherOwner));
        QCOMPARE(candidate(), visual);
        QVERIFY(!overlay.m_candidatesManuallyChosen); // Same point, new owner releases choice.

        overlay.keyPressEvent(&tab);
        auto covered = otherOwner; covered.occludedRegion = QRegion(QRect(380, 0, 100, 480));
        QVERIFY(overlay.applySmartCandidates(point, {element, window}, covered));
        QCOMPARE(candidate(), element);
        QVERIFY(!overlay.m_candidatesManuallyChosen); // Same owner, new occlusion releases stale choice.

        overlay.applySmartCandidates(point, {element, visual, window}, context);
        const auto idleCandidates = overlay.m_smartCandidates;
        // A real press enters dragging before any late candidate can replace its preview list.
        QMouseEvent press(QEvent::MouseButtonPress, point, overlay.mapToGlobal(point),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &press);
        QCOMPARE(overlay.m_state, SnippingState::Selecting);
        QVERIFY(!overlay.applySmartCandidates(point, late, context));
        QCOMPARE(overlay.m_smartCandidates, idleCandidates);
        const QPoint end = point + QPoint(60, 40);
        QMouseEvent move(QEvent::MouseMove, end, overlay.mapToGlobal(end),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, end, overlay.mapToGlobal(end),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &release);
        QCOMPARE(overlay.m_state, SnippingState::Selected);
        const QRect manual = overlay.selectedRect();
        QVERIFY(!overlay.applySmartCandidates(point, late, context));
        QCOMPARE(overlay.selectedRect(), manual);

        QImage nextImage = image;
        { QPainter painter(&nextImage); painter.fillRect(visual, Qt::black); }
        SmartSnapper completed;
        completed.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        completed.processSnapshot(nextImage);
        const auto beforeAnalysis = overlay.m_snapper.findCandidatesAt(point);
        const auto afterAnalysis = completed.findCandidatesAt(point);
        QVERIFY(afterAnalysis != beforeAnalysis); // The late result must carry a real change.
        overlay.show();
        // Deliver a changed CV analysis with the actual pointer button held, before release.
        overlay.m_state = SnippingState::Idle;
        QApplication::sendEvent(&overlay, &press);
        const auto whilePressed = overlay.selectedRect();
        overlay.acceptContourAnalysis(completed, overlay.m_captureGeneration);
        QCOMPARE(overlay.m_state, SnippingState::Selecting);
        QCOMPARE(overlay.selectedRect(), whilePressed);
        QCOMPARE(overlay.m_smartCandidates, idleCandidates);
        QApplication::sendEvent(&overlay, &move);
        QApplication::sendEvent(&overlay, &release);
        QCOMPARE(overlay.selectedRect(), manual);
        overlay.m_snapper.processSnapshot(image);
        const auto oldGeneration = overlay.m_captureGeneration;
        overlay.triggerCancelAction(); // Cancel invalidates the previous capture's completion.
        overlay.show();
        overlay.acceptContourAnalysis(completed, oldGeneration);
        QCOMPARE(overlay.m_snapper.findCandidatesAt(point), beforeAnalysis);
        overlay.acceptContourAnalysis(completed, overlay.m_captureGeneration);
        QCOMPARE(overlay.m_snapper.findCandidatesAt(point), afterAnalysis);
        QCOMPARE(overlay.selectedRect(), manual);
        QCOMPARE(overlay.m_smartCandidates, idleCandidates);
    }
    void snapCandidatePreviewAnimation() {
        const auto original = HotkeyConfig::instance().data();
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction(); overlay.clearFocus();
            HotkeyConfig::instance().setData(original);
            QApplication::processEvents();
        });
        QImage image(640, 480, QImage::Format_RGB32); image.fill(QColor("#243440"));
        const QPoint point(300, 240);
        const QRect card(170, 120, 280, 250), detail(250, 205, 100, 70), icon(285, 225, 30, 30);
        { QPainter painter(&image);
          painter.fillRect(card, QColor("#e6edf0"));
          painter.fillRect(detail, QColor("#71858e"));
          painter.fillRect(icon, QColor("#ffffff")); }
        overlay.triggerCancelAction();
        overlay.resize(image.size());
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapshot.isValid = true;
        overlay.m_snapshot.virtualGeometry = image.rect();
        overlay.m_snapper.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        overlay.m_snapper.processSnapshot(image);
        overlay.m_snapper.findCandidatesAt(point);
        const auto context = overlay.m_snapper.candidateContext();
        overlay.m_state = SnippingState::Idle;
        overlay.m_selectionRect = {};
        overlay.m_enableSmartSnapping = true;
        overlay.m_candidateAnimationsEnabled = true; // Deterministic independently of host accessibility settings.
        overlay.m_candidatesManuallyChosen = false;
        overlay.m_smartCandidates.clear();
        overlay.m_currentTool = ToolAction::Cancel;
        if (auto* poll = overlay.findChild<QTimer*>("controlSnapPoll")) poll->stop();
        QVERIFY(overlay.applySmartCandidates(point, {card, detail, icon}, context));
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(card)); // First frame appears immediately.
        const QString evidence = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            "../docs/evidence/snap-refinement-2026-10-18/animation");
        QVERIFY(QDir().mkpath(evidence));
        QVERIFY(overlay.grab().save(evidence + "/00-first-frame.png"));
        auto wheel = [&](int delta) {
            QWheelEvent event(point, overlay.mapToGlobal(point), QPoint(), QPoint(0, delta),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&overlay, &event);
        };
        wheel(-120);
        QCOMPARE(overlay.m_smartCandidates[overlay.m_candidateIndex], detail);
        QCOMPARE(overlay.m_candidateAnimation.duration(), 90);
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Running);
        overlay.m_candidateAnimation.pause();
        overlay.m_candidateAnimation.setCurrentTime(0);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(card));
        QVERIFY(overlay.grab().save(evidence + "/01-switch-000ms.png"));
        overlay.m_candidateAnimation.setCurrentTime(45);
        const QRectF middle = overlay.m_candidateDisplayRect;
        QVERIFY(middle != QRectF(card)); QVERIFY(middle != QRectF(detail));
        QVERIFY(QRectF(card).contains(middle)); QVERIFY(middle.contains(QRectF(detail)));
        QVERIFY(overlay.grab().save(evidence + "/02-switch-045ms.png"));
        // A second wheel event starts at the current frame and immediately changes the logical target.
        wheel(-120);
        QCOMPARE(overlay.m_smartCandidates[overlay.m_candidateIndex], icon);
        QCOMPARE(overlay.m_candidateAnimation.startValue().toRectF(), middle);
        QCOMPARE(overlay.m_candidateAnimation.endValue().toRectF(), QRectF(icon));
        QCOMPARE(overlay.m_candidateDisplayRect, middle);
        overlay.m_candidateAnimation.pause();
        overlay.m_candidateAnimation.setCurrentTime(45);
        QVERIFY(overlay.grab().save(evidence + "/03-retarget-045ms.png"));
        // Clicking during interpolation selects the exact icon, not its painted intermediate rectangle.
        QMouseEvent press(QEvent::MouseButtonPress, point, overlay.mapToGlobal(point),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &press);
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        QVERIFY(overlay.m_candidateDisplayRect.isNull());
        QMouseEvent release(QEvent::MouseButtonRelease, point, overlay.mapToGlobal(point),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &release);
        QCOMPARE(overlay.selectedRect(), icon);
        QCOMPARE(overlay.m_state, SnippingState::Selected);
        QVERIFY(overlay.grab().save(evidence + "/04-click-exact-target.png"));

        // Returning to candidate selection is another immediate first frame.
        QMouseEvent reset(QEvent::MouseButtonPress, point, overlay.mapToGlobal(point),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &reset);
        QCOMPARE(overlay.m_state, SnippingState::Idle);
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(overlay.m_smartCandidates.front()));
        overlay.applySmartCandidates(point, {card, detail, icon}, context);
        overlay.selectCandidateByArea(-1);
        overlay.triggerCancelAction();
        QVERIFY(overlay.m_candidateDisplayRect.isNull());
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        overlay.m_smartCandidates.clear();
        overlay.m_candidatesManuallyChosen = false;
        overlay.m_selectionRect = {};
        QVERIFY(overlay.applySmartCandidates(point, {card, detail, icon}, context));
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(card));
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        overlay.selectCandidateByArea(-1);
        overlay.m_candidateAnimation.pause();
        overlay.m_candidateAnimation.setCurrentTime(90);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(detail));
        QVERIFY(overlay.grab().save(evidence + "/05-switch-090ms.png"));

        // A different owner cancels the tween even at the same pointer position.
        auto otherOwner = context; ++otherOwner.ownerWindow;
        QVERIFY(overlay.applySmartCandidates(point, {card, detail, icon}, otherOwner));
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(card));
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        overlay.m_candidateAnimationsEnabled = false;
        overlay.selectCandidateByArea(-1);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(detail));
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        overlay.m_candidateAnimationsEnabled = true;
        overlay.show();
        overlay.selectCandidateByArea(-1);
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Running);
        overlay.hide();
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        QVERIFY(overlay.m_candidateDisplayRect.isNull());
    }
    void snapCandidateResize() {
        const auto original = HotkeyConfig::instance().data();
        const auto originalTheme = ThemeManager::instance().themeMode();
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction(); overlay.clearFocus();
            HotkeyConfig::instance().setData(original);
            ThemeManager::instance().setThemeMode(originalTheme, false);
            QApplication::processEvents();
        });
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        overlay.triggerCancelAction(); overlay.resize(640, 480);
        if (QGuiApplication::platformName() == "windows") {
            overlay.show(); QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        }
        if (auto* focus = QApplication::focusWidget()) focus->clearFocus();
        QImage image(640, 480, QImage::Format_RGB32); image.fill(QColor("#243440"));
        const QPoint point(300, 240);
        const QRect card(170, 120, 280, 250), detail(250, 205, 100, 70);
        { QPainter painter(&image); painter.fillRect(card, QColor("#e6edf0"));
          painter.fillRect(detail, QColor("#71858e")); }
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapshot.virtualGeometry = image.rect(); overlay.m_snapshot.isValid = true;
        overlay.m_snapper.configure("Contour", {}, 0); // Synthetic image, without live desktop ownership.
        overlay.m_snapper.processSnapshot(image); overlay.m_snapper.findCandidatesAt(point);
        const auto context = overlay.m_snapper.candidateContext();
        if (auto* poll = overlay.findChild<QTimer*>("controlSnapPoll")) poll->stop();
        auto seed = [&](const QRect& first = QRect(170, 120, 280, 250)) {
            overlay.m_state = SnippingState::Idle; overlay.m_selectionRect = {};
            overlay.m_enableSmartSnapping = true; overlay.m_currentTool = ToolAction::Cancel;
            overlay.m_smartCandidates.clear(); overlay.m_candidatesManuallyChosen = false;
            overlay.m_candidateWheelDelta = 0; overlay.resetCandidateAdjustment();
            overlay.resetCandidatePreview();
            overlay.applySmartCandidates(point, {first, detail}, context);
        };
        auto wheel = [&](int delta, Qt::KeyboardModifiers mods = Qt::ControlModifier) {
            QWheelEvent event(point, overlay.mapToGlobal(point), QPoint(), QPoint(0, delta),
                              Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&overlay, &event);
        };
        seed(); const auto candidates = overlay.m_smartCandidates;
        wheel(-60); QCOMPARE(overlay.currentCandidateRect(), card);
        wheel(-60); QCOMPARE(overlay.currentCandidateRect(), card.adjusted(1, 1, -1, -1));
        wheel(240); QCOMPARE(overlay.currentCandidateRect(), card.adjusted(-1, -1, 1, 1));
        QCOMPARE(overlay.m_smartCandidates, candidates); // Resizing never rewrites source candidates.
        QVERIFY(overlay.m_candidatesManuallyChosen);
        const auto adjusted = overlay.currentCandidateRect();
        QVERIFY(!overlay.applySmartCandidates(point + QPoint(2, 1), {detail}, context));
        QCOMPARE(overlay.currentCandidateRect(), adjusted); // Late poll and pointer jitter retain adjustment.
        QVERIFY(overlay.applySmartCandidates(point + QPoint(8, 0), {detail}, context));
        QCOMPARE(overlay.currentCandidateRect(), detail);
        seed(QRect(290, 230, 30, 30)); wheel(-120 * 12);
        const QRect smallerThanPointer = overlay.currentCandidateRect();
        QVERIFY(!smallerThanPointer.contains(point));
        QVERIFY(!overlay.applySmartCandidates(point, {detail}, context));
        QCOMPARE(overlay.currentCandidateRect(), smallerThanPointer);
        seed(); wheel(-120);
        auto otherOwner = context; ++otherOwner.ownerWindow;
        QVERIFY(overlay.applySmartCandidates(point, {card, detail}, otherOwner));
        QCOMPARE(overlay.currentCandidateRect(), card);
        seed(); wheel(-120);
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &tab); QCOMPARE(overlay.currentCandidateRect(), detail);
        seed(); wheel(-120); wheel(-120, Qt::NoModifier);
        QCOMPARE(overlay.currentCandidateRect(), detail); // Ordinary layer switching clears adjustment.
        seed(); wheel(-60); wheel(-60, Qt::NoModifier);
        QCOMPARE(overlay.currentCandidateRect(), card); // Partial actions cannot combine across modes.
        wheel(-60); QCOMPARE(overlay.currentCandidateRect(), card);
        wheel(-60); QCOMPARE(overlay.currentCandidateRect(), card.adjusted(1, 1, -1, -1));
        auto custom = HotkeyConfigData{}; custom.snippingCandidateResizeStep = 3;
        custom.snippingShrinkCandidate = "Ctrl+J"; custom.snippingExpandCandidate = "Ctrl+K";
        HotkeyConfig::instance().setData(custom); seed();
        wheel(-120); QCOMPARE(overlay.currentCandidateRect(), card);
        QKeyEvent shrink(QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
        QKeyEvent expand(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        QApplication::sendEvent(&overlay, &shrink);
        QCOMPARE(overlay.currentCandidateRect(), card.adjusted(3, 3, -3, -3));
        QApplication::sendEvent(&overlay, &expand); QCOMPARE(overlay.currentCandidateRect(), card);
        custom.snippingShrinkCandidate.clear(); custom.snippingExpandCandidate.clear();
        HotkeyConfig::instance().setData(custom);
        QApplication::sendEvent(&overlay, &shrink); QCOMPARE(overlay.currentCandidateRect(), card);
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        seed(QRect(299, 239, 3, 3)); wheel(-120 * 1000);
        QCOMPARE(overlay.currentCandidateRect().size(), QSize(1, 1));
        const QRect smallest = overlay.currentCandidateRect(); wheel(120);
        QCOMPARE(overlay.currentCandidateRect(), smallest.adjusted(-1, -1, 1, 1));
        seed(QRect(1, 1, 638, 478)); wheel(120 * 1000);
        QCOMPARE(overlay.currentCandidateRect(), image.rect());
        wheel(-120); QCOMPARE(overlay.currentCandidateRect(), image.rect().adjusted(1, 1, -1, -1));
        for (auto state : {SnippingState::Selecting, SnippingState::Selected}) {
            seed(); overlay.m_state = state; wheel(-120);
            QCOMPARE(overlay.currentCandidateRect(), card);
        }
        seed(); overlay.m_enableSmartSnapping = false; wheel(-120);
        QCOMPARE(overlay.currentCandidateRect(), card);
        seed(); overlay.m_smartCandidates.clear(); overlay.resizeCurrentCandidate(-1);
        QVERIFY(overlay.currentCandidateRect().isEmpty());

        seed(); overlay.m_candidateAnimationsEnabled = true; wheel(-120);
        const QRect target = card.adjusted(1, 1, -1, -1);
        QCOMPARE(overlay.currentCandidateRect(), target);
        overlay.m_candidateAnimation.pause(); overlay.m_candidateAnimation.setCurrentTime(0);
        QCOMPARE(overlay.m_candidateDisplayRect, QRectF(card)); // Paint interpolation is not the target.
        const QString evidence = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            "../docs/evidence/candidate-resize-2026-10-18");
        QVERIFY(QDir().mkpath(evidence));
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode, false);
            overlay.m_candidateAnimation.setCurrentTime(90);
            QVERIFY(overlay.grab().save(evidence + (mode == ThemeMode::Light
                ? "/overlay-light.png" : "/overlay-dark.png")));
        }
        seed(); wheel(-120);
        overlay.m_candidateAnimation.pause(); overlay.m_candidateAnimation.setCurrentTime(0);
        QMouseEvent press(QEvent::MouseButtonPress, point, overlay.mapToGlobal(point),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, overlay.mapToGlobal(point),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &release); QCOMPARE(overlay.selectedRect(), target);
    }
    void snapCandidateResizeNative() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Windows QWidget verification only");
        qInfo() << "Candidate resize: synthetic QWheelEvent/QMouseEvent, not physical hardware input";
        snapCandidateResize();
    }
    void snapCandidatePreviewPaintTiming() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Records Windows QWidget paint timings");
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] {
            overlay.triggerCancelAction(); overlay.clearFocus(); QApplication::processEvents();
        });
        overlay.triggerCancelAction();
        overlay.resize(640, 480);
        const qreal dpr = overlay.devicePixelRatioF();
        QImage image(QSize(qRound(640 * dpr), qRound(480 * dpr)), QImage::Format_RGB32);
        image.setDevicePixelRatio(dpr);
        image.fill(QColor("#243440"));
        const QRect card(170, 120, 280, 250), detail(250, 205, 100, 70);
        { QPainter painter(&image);
          painter.fillRect(card, QColor("#e6edf0"));
          painter.fillRect(detail, QColor("#71858e")); }
        overlay.m_snapshot.fullSnapshot = QPixmap::fromImage(image);
        overlay.m_snapshot.isValid = true;
        overlay.m_snapshot.virtualGeometry = QRect(QPoint(), QSize(640, 480));
        overlay.m_state = SnippingState::Idle;
        overlay.m_selectionRect = {};
        overlay.m_enableSmartSnapping = true;
        overlay.m_candidateAnimationsEnabled = true;
        overlay.m_candidatesManuallyChosen = false;
        overlay.m_smartCandidates.clear();
        overlay.m_magnifier->hide();
        overlay.m_toolbar->hide();
        if (auto* poll = overlay.findChild<QTimer*>("controlSnapPoll")) poll->stop();
        overlay.applySmartCandidates(QPoint(300, 240), {card, detail}, {});
        const QList<int> frameTimes{0, 15, 30, 45, 60, 75, 90};
        constexpr int warmupRepetitions = 10, measuredRepetitions = 50;
        std::vector<double> timings;
        QJsonArray samples;
        QElapsedTimer timer;
        QSize grabbedPhysicalSize;
        for (int repetition = -warmupRepetitions; repetition < measuredRepetitions; ++repetition) {
            overlay.resetCandidatePreview();
            overlay.m_candidateIndex = 0;
            overlay.updateCandidatePreview(false);
            overlay.m_candidateIndex = 1;
            overlay.updateCandidatePreview();
            overlay.m_candidateAnimation.pause();
            for (const int frameTime : frameTimes) {
                overlay.m_candidateAnimation.setCurrentTime(frameTime);
                timer.start();
                const QPixmap frame = overlay.grab(); // Real production paintEvent plus QWidget grab allocation/copy.
                const double elapsedMs = timer.nsecsElapsed() / 1000000.0;
                QVERIFY(!frame.isNull());
                grabbedPhysicalSize = frame.size();
                if (repetition < 0) continue;
                timings.push_back(elapsedMs);
                samples.append(QJsonObject{{"repetition", repetition}, {"animationTimeMs", frameTime},
                                           {"grabRenderMs", elapsedMs}});
            }
        }
        std::sort(timings.begin(), timings.end());
        const auto percentile = [&](double p) {
            return timings[size_t(std::ceil(p * timings.size())) - 1];
        };
        QJsonObject report{
            {"description", "Synthetic 640x480 logical canvas; production SnippingOverlay::paintEvent via QWidget::grab"},
            {"scope", "Synchronous QWidget grab/render wall time including pixmap allocation/copy; excludes animation evaluation, event delivery, compositor and display latency; not screen end-to-end FPS"},
            {"platform", QGuiApplication::platformName()}, {"qtVersion", qVersion()},
            {"devicePixelRatio", dpr}, {"snapshotDevicePixelRatio", image.devicePixelRatio()},
            {"logicalWidth", 640}, {"logicalHeight", 480},
            {"grabPhysicalWidth", grabbedPhysicalSize.width()}, {"grabPhysicalHeight", grabbedPhysicalSize.height()},
            {"warmupRepetitions", warmupRepetitions}, {"measuredRepetitions", measuredRepetitions},
            {"sampleCount", int(timings.size())}, {"animationDurationMs", 90},
            {"percentileMethod", "nearest rank"}, {"renderP50Ms", percentile(0.50)},
            {"renderP95Ms", percentile(0.95)}, {"renderMaxMs", timings.back()}, {"samples", samples}};
        const QString evidence = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            "../docs/evidence/snap-refinement-2026-10-18/animation");
        QVERIFY(QDir().mkpath(evidence));
        QFile output(evidence + "/paint-timing.json");
        QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) > 0);
        qInfo() << "Synthetic candidate preview grab/render" << timings.size() << "frames; DPR" << dpr
                << "P50" << percentile(0.50) << "ms; P95" << percentile(0.95) << "ms";
    }
    void snapFullScreenNavigation() {
        if(QGuiApplication::platformName()!="windows") QSKIP("Requires real Windows ownership");
        const auto original=HotkeyConfig::instance().data();
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        QWidget fixture(nullptr,Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
        fixture.setGeometry(160,160,500,300);fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        auto& overlay=SnippingOverlay::instance();
        const QPoint previousCursor=QCursor::pos();
        const auto cleanup=qScopeGuard([&]{
            overlay.triggerCancelAction();overlay.clearFocus();fixture.hide();
            QCursor::setPos(previousCursor);HotkeyConfig::instance().setData(original);
            QApplication::processEvents();
        });
        QSettings cfg(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");
        cfg.setValue("Snapping/EnableSmartSnapping",true);
        for(const QString mode:{QString("Window"),QString("Element"),QString("Hybrid"),QString("Contour")}) {
            cfg.setValue("Snapping/Mode",mode);
            QCursor::setPos(fixture.geometry().center());overlay.startSnipping();
            const QPoint point=overlay.mapFromGlobal(fixture.geometry().center());
            const QRect screen(QPoint(),overlay.m_snapshot.virtualGeometry.size());
            // Full-screen navigation must work immediately, before CV/UIA completes.
            overlay.updateSmartCandidates(point);
            QCOMPARE(overlay.m_smartCandidates.back(),screen);
            auto wheel=[&](int delta){
                QWheelEvent event(point,overlay.mapToGlobal(point),QPoint(),QPoint(0,delta),
                                  Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                QApplication::sendEvent(&overlay,&event);
            };
            wheel(120*1000);QCOMPARE(overlay.currentCandidateRect(),screen);
            wheel(120);QCOMPARE(overlay.currentCandidateRect(),screen);
            wheel(-120);QVERIFY(overlay.currentCandidateRect()!=screen);
            wheel(120*1000);QCOMPARE(overlay.currentCandidateRect(),screen);
            QTRY_VERIFY(overlay.findChildren<QFutureWatcherBase*>().isEmpty());
            QCOMPARE(overlay.currentCandidateRect(),screen); // Late results do not steal the selection.
            QTest::mouseClick(&overlay,Qt::LeftButton,Qt::NoModifier,point);
            QCOMPARE(overlay.selectedRect(),screen);
            overlay.triggerCancelAction();
        }
    }
    void contourAsyncSelection() {
        if(QGuiApplication::platformName()!="windows") QSKIP("Requires real screen capture");
        QWidget fixture(nullptr,Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:#e4edf0;border:2px solid #151515;");
        fixture.setGeometry(160,160,500,300);fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        auto& overlay=SnippingOverlay::instance();
        const QPoint previousCursor=QCursor::pos();
        const auto cleanup=qScopeGuard([&]{overlay.triggerCancelAction();fixture.hide();QCursor::setPos(previousCursor);QApplication::processEvents();});
        QCursor::setPos(fixture.geometry().center());
        QSettings cfg(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");
        cfg.setValue("Snapping/Mode","Contour");cfg.setValue("Snapping/EnableSmartSnapping",true);
        // Cancel an in-flight capture, then start another; only its result may be applied.
        overlay.startSnipping();
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        overlay.triggerCancelAction();
        QVERIFY(overlay.m_candidateDisplayRect.isNull());
        overlay.startSnipping();
        QCOMPARE(overlay.m_candidateAnimation.state(), QAbstractAnimation::Stopped);
        if (!overlay.m_smartCandidates.empty())
            QCOMPARE(overlay.m_candidateDisplayRect, QRectF(overlay.m_smartCandidates[overlay.m_candidateIndex]));
        QTRY_VERIFY(overlay.findChildren<QFutureWatcherBase*>().isEmpty());
        const QPoint point=overlay.mapFromGlobal(fixture.geometry().center());
        QTest::mouseMove(&overlay,point);QApplication::processEvents();
        QDir().mkpath("docs/evidence/contour-2026-10-18");
        QVERIFY(overlay.grab().save("docs/evidence/contour-2026-10-18/native-overlay.png"));
        QTest::mouseClick(&overlay,Qt::LeftButton,Qt::NoModifier,point);
        QVERIFY(overlay.hasValidSelection());
        QRect selected=overlay.selectedRect().translated(overlay.pos());
        QVERIFY2(std::abs(selected.x()-fixture.x())<8 && std::abs(selected.y()-fixture.y())<8 &&
                 std::abs(selected.width()-fixture.width())<12 && std::abs(selected.height()-fixture.height())<12,
                 qPrintable(QString("Unexpected rectangle %1,%2 %3x%4").arg(selected.x()).arg(selected.y()).arg(selected.width()).arg(selected.height())));
        overlay.triggerCancelAction();overlay.startSnipping();
        const QPoint a=overlay.mapFromGlobal(QPoint(190,190)),b=overlay.mapFromGlobal(QPoint(280,260));
        QTest::mousePress(&overlay,Qt::LeftButton,Qt::NoModifier,a);
        QMouseEvent move(QEvent::MouseMove,b,overlay.mapToGlobal(b),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&overlay,&move);
        QTest::mouseRelease(&overlay,Qt::LeftButton,Qt::NoModifier,b);
        const QRect manual=overlay.selectedRect();QVERIFY(manual.isValid());
        QTRY_VERIFY(overlay.findChildren<QFutureWatcherBase*>().isEmpty());
        QCOMPARE(overlay.selectedRect(),manual);
        overlay.triggerCancelAction();
        QTRY_VERIFY(overlay.findChildren<QFutureWatcherBase*>().isEmpty());
        QVERIFY(!overlay.isVisible());
    }
    void snappedClicksWithModalSettings() {
        if(QGuiApplication::platformName()!="windows")QSKIP("Requires native Windows input dispatch");
        QProcess fixture;fixture.start(QCoreApplication::applicationDirPath()+"/native_snap_probe.exe",{"fixture"});
        QVERIFY(fixture.waitForReadyRead(5000));const auto values=QString::fromUtf8(fixture.readLine()).trimmed().split(' ');QCOMPARE(values.size(),3);
        const QPoint physical(values[1].toInt(),values[2].toInt());
        QDialog settingsDialog;settingsDialog.setWindowModality(Qt::ApplicationModal);settingsDialog.show();
        auto& overlay=SnippingOverlay::instance();
        const auto cleanup=qScopeGuard([&]{overlay.triggerCancelAction();settingsDialog.hide();fixture.kill();fixture.waitForFinished(2000);});
        auto cfg=QSettings(QSettings::defaultFormat(),QSettings::UserScope,"Evan","Evan");cfg.setValue("Snapping/Mode","Window");cfg.setValue("Snapping/EnableSmartSnapping",true);
        overlay.startSnipping();QApplication::processEvents();QVERIFY(overlay.isVisible());
        const auto hwnd=reinterpret_cast<HWND>(overlay.winId());
        QVERIFY2(IsWindowEnabled(hwnd),"Modal settings blocks screenshot mouse input");
        POINT local{physical.x(),physical.y()};ScreenToClient(hwnd,&local);const LPARAM point=MAKELPARAM(local.x,local.y);
        PostMessageW(hwnd,WM_MOUSEMOVE,0,point);QTest::qWait(150);
        QDir().mkpath("screenshots");
        QVERIFY(overlay.grab().save("screenshots/window-snap-native.png"));
        PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);PostMessageW(hwnd,WM_LBUTTONUP,0,point);
        QTRY_VERIFY(overlay.hasValidSelection());
        QSignalSpy finished(&overlay,&SnippingOverlay::snippingFinished);
        QTest::mouseDClick(&overlay,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(local.x/overlay.devicePixelRatioF()),qRound(local.y/overlay.devicePixelRatioF())));
        QTRY_COMPARE(finished.count(),1);QVERIFY(!overlay.isVisible());QCOMPARE(QApplication::activeModalWidget(),&settingsDialog);
        overlay.startSnipping();QApplication::processEvents();QVERIFY(IsWindowEnabled(reinterpret_cast<HWND>(overlay.winId())));
        QTest::keyClick(&overlay,Qt::Key_Escape);QVERIFY(!overlay.isVisible());QCOMPARE(QApplication::activeModalWidget(),&settingsDialog);
    }
    void nativeScreenTranslationEntry() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires native Windows screenshot overlay");
        QSettings cfg(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
        cfg.setValue("Snapping/EnableSmartSnapping", false);
        OcrManager::instance().setPreferredEngineType("RapidOCR");
        TranslationPluginManager::instance().setPreferredEnginePreference("Offline");
        QVERIFY(!OcrManager::instance().isRapidOcrReady());
        QLabel fixture("Screen translation entry fixture\nNo user data or network", nullptr,
                       Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-size:22px");
        fixture.setGeometry(80, 100, 500, 220);
        fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        auto& overlay = SnippingOverlay::instance();
        const auto cleanup = qScopeGuard([&] { overlay.triggerCancelAction(); fixture.close(); });
        overlay.startSnipping();
        QApplication::processEvents();
        const QPoint topLeft = overlay.mapFromGlobal(fixture.mapToGlobal(QPoint(10, 10)));
        const QPoint bottomRight = topLeft + QPoint(440, 160);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&overlay, bottomRight);
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, bottomRight);
        QVERIFY(overlay.hasValidSelection());
        const QRect selected = overlay.selectedRect().normalized();
        const QRect expected(overlay.mapToGlobal(selected.topLeft()), selected.size());
        auto* action = overlay.findChild<QToolButton*>("tool_screen_translate");
        QVERIFY(action);
        action->click();
        QPointer<ScreenTranslationWindow> result;
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* candidate = qobject_cast<ScreenTranslationWindow*>(widget)) result = candidate;
        QVERIFY(result);
        QCOMPARE(result->geometry(), expected);
        QVERIFY(!overlay.isVisible());
        QTRY_VERIFY(result->statusText().contains("安装 RapidOCR"));
        QVERIFY(!result->findChild<QPushButton*>("screenTranslationCopy")->isEnabled());
        result->close();
        QTRY_VERIFY(result.isNull());
    }
    void captureToolbarFlowAndRendering() {
        QWidget host; host.resize(1000, 500); host.show();
        FloatingToolbar toolbar(&host); toolbar.show();
        toolbar.updatePosition(QRect(400, 300, 300, 160), host.rect());
        QVERIFY(host.rect().contains(toolbar.geometry()));
        auto* undo = toolbar.findChild<QToolButton*>("tool_undo");
        auto* rect = toolbar.findChild<QToolButton*>("tool_rect");
        auto* ocr = toolbar.findChild<QToolButton*>("tool_ocr");
        auto* screenTranslate = toolbar.findChild<QToolButton*>("tool_screen_translate");
        QVERIFY(undo && rect && ocr && screenTranslate);
        QCOMPARE(screenTranslate->accessibleName(), QString("原位屏幕翻译"));
        QVERIFY(!undo->isEnabled());
        toolbar.setUndoAvailable(true); QVERIFY(undo->isEnabled());
        toolbar.setOcrBusy(true); QVERIFY(!ocr->isEnabled()); QVERIFY(!screenTranslate->isEnabled());
        toolbar.setOcrBusy(false); QVERIFY(ocr->isEnabled()); QVERIFY(screenTranslate->isEnabled());
        QSignalSpy actions(&toolbar, &FloatingToolbar::actionTriggered);
        screenTranslate->click();
        QCOMPARE(actions.count(), 1);
        QCOMPARE(qvariant_cast<ToolAction>(actions.takeFirst().first()), ToolAction::ScreenTranslate);
        rect->click(); QCOMPARE(actions.count(), 1); QVERIFY(rect->isChecked());
        toolbar.setActiveTool(ToolAction::Text);
        QCOMPARE(toolbar.currentTextFont().family(), QString("Microsoft YaHei UI"));
        QDir().mkpath("screenshots");
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            QApplication::processEvents();
            QVERIFY(host.rect().contains(toolbar.geometry()));
            const QString name = mode == ThemeMode::Light ? "light" : "dark";
            QVERIFY(toolbar.grab().save("screenshots/toolbar-" + name + ".png"));
            QPixmap image(300, 160); image.fill(QColor("#7291ad"));
            PinWindow pin(image, QRect(100, 100, 300, 160));
            pin.setAttribute(Qt::WA_DeleteOnClose, false);
            QVERIFY(pin.grab().save("screenshots/pin-" + name + ".png"));
        }
        toolbar.updatePosition(QRect(10, 10, 50, 50), QRect(0, 0, 360, 300));
        QVERIFY(QRect(0, 0, 360, 300).contains(toolbar.geometry()));
        QVERIFY(toolbar.findChild<QToolButton*>("toolbarMore")->isVisible());
        toolbar.resetSession(); QVERIFY(!rect->isChecked()); QVERIFY(!undo->isEnabled());
    }
    void editsFormattingUndoAndFailure() {
        OcrResultDialog dialog;
        OcrResult result; result.success = true; result.fullText = "Original text";
        dialog.setResult(result, "WindowsMedia");
        auto* source = dialog.findChild<QPlainTextEdit*>("sourceText");
        QVERIFY(source);
        source->setPlainText(QStringLiteral("修 正后的文本\n第二行"));
        button(dialog, "清除空格")->click();
        QCOMPARE(source->toPlainText(), QStringLiteral("修正后的文本\n第二行"));
        source->undo();
        QCOMPARE(source->toPlainText(), QStringLiteral("修 正后的文本\n第二行"));
        button(dialog, "复制原文")->click();
        QCOMPARE(QApplication::clipboard()->text(), source->toPlainText());
        result.success = false; result.errorMessage = "Test failure";
        dialog.setResult(result);
        // Approved v2 retains the editable result when a retry fails.
        QCOMPARE(source->toPlainText(), QStringLiteral("修 正后的文本\n第二行"));
        QVERIFY(button(dialog, "已复制")->isEnabled());
        bool hasFailure=false;
        for(auto* label:dialog.findChildren<QLabel*>())hasFailure|=label->text().contains("Test failure");
        QVERIFY(hasFailure);
        QVERIFY(!button(dialog, "复制译文")->isEnabled());
    }
    void translationPanelIsOptional() {
        OcrResultDialog dialog;
        auto* panel = dialog.findChild<QWidget*>("transSection");
        QVERIFY(!panel->isHidden()); // The design opens in comparison mode.
        dialog.setTranslationRequested(false);
        QVERIFY(panel->isHidden());
        dialog.setTranslationRequested(true);
        QVERIFY(!panel->isHidden());
        dialog.setTranslationRequested(false);
        QVERIFY(panel->isHidden());
    }
    void formulaStateAndPreviewInvalidation() {
        FormulaResultDialog dialog;
        auto* edit = dialog.findChild<QPlainTextEdit*>();
        QVERIFY(!button(dialog, "复制 LaTeX")->isEnabled());
        edit->setPlainText("x^2");
        QVERIFY(button(dialog, "复制 LaTeX")->isEnabled());
        bool pending = false;
        for(auto* label : dialog.findChildren<QLabel*>()) pending |= label->text() == QStringLiteral("预览更新中…");
        QVERIFY(pending);
        button(dialog, "复制 LaTeX")->click();
        QCOMPARE(QApplication::clipboard()->text(), QString("x^2"));
        edit->clear();
        QVERIFY(!button(dialog, "已复制")->isEnabled());
        // Destroy before debounce: no network requests or user data involved.
    }
    void saveCancellationRetainsCapture() {
        ScrollCaptureSession session;
        QImage image(100, 100, QImage::Format_RGB32); image.fill(Qt::blue);
        session.start(QRect(0, 0, 100, 100), image);
        QTimer reject;
        connect(&reject, &QTimer::timeout, this, [] {
            for(auto* widget : QApplication::topLevelWidgets())
                if(auto* dialog = qobject_cast<QFileDialog*>(widget)) dialog->reject();
        });
        reject.start(20);
        QSignalSpy finished(&session, &ScrollCaptureSession::finished);
        QSignalSpy cancelled(&session, &ScrollCaptureSession::cancelled);
        QApplication::clipboard()->setText("unchanged");
        session.save();
        QVERIFY(session.isActive());
        QCOMPARE(finished.count(), 0); QCOMPARE(cancelled.count(), 0);
        QCOMPARE(QApplication::clipboard()->text(), QString("unchanged"));
        session.cancel();
    }
    void themePreviewCancelAndCommit() {
        auto& theme = ThemeManager::instance();
        theme.setThemeMode(ThemeMode::Light);
        theme.setResultFontSize(15);
        QSettings saved(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
        {
            HotkeySettingsDialog dialog;
            auto* mode = dialog.findChild<QComboBox*>("themeMode");
            mode->setCurrentIndex(2);
            auto* fontSize = dialog.findChild<QComboBox*>("resultFontSize");
            QVERIFY(fontSize);
            fontSize->setCurrentIndex(fontSize->findData(18));
            QCOMPARE(theme.resultFontSize(), 18);
            QCOMPARE(saved.value("Appearance/ResultFontSize").toInt(), 15);
            QCOMPARE(theme.themeMode(), ThemeMode::Dark);
            QCOMPARE(theme.palette().page, QColor("#101E1B"));
            QCOMPARE(saved.value("Theme/Mode").toString(), QString("Light"));
            dialog.reject();
            QCOMPARE(theme.themeMode(), ThemeMode::Light);
            QCOMPARE(theme.resultFontSize(), 15);
        }
        {
            HotkeySettingsDialog dialog;
            dialog.findChild<QComboBox*>("themeMode")->setCurrentIndex(2);
            auto* fontSize = dialog.findChild<QComboBox*>("resultFontSize");
            fontSize->setCurrentIndex(fontSize->findData(20));
            dialog.findChild<QPushButton*>("settingsSave")->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            saved.sync();
            QCOMPARE(saved.value("Theme/Mode").toString(), QString("Dark"));
            QCOMPARE(saved.value("Appearance/ResultFontSize").toInt(), 20);
        }
        QCOMPARE(theme.themeMode(), ThemeMode::Dark);
        theme.setThemeMode(ThemeMode::Light);
        theme.setResultFontSize(15);
        QCOMPARE(theme.palette().page, QColor("#E7EEE9"));
    }
    void candidateShortcutSettings() {
        const auto original = HotkeyConfig::instance().data();
        const auto originalTheme = ThemeManager::instance().themeMode();
        const auto restore = qScopeGuard([&] {
            HotkeyConfig::instance().setData(original);
            ThemeManager::instance().setThemeMode(originalTheme, false);
        });
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        HotkeySettingsDialog dialog;
        dialog.show(); dialog.activateWindow();
        dialog.findChild<QTabWidget*>("settingsPages")->setCurrentIndex(3);
        dialog.findChild<QLineEdit*>("shortcutSearch")->setText("候选");
        auto* smaller = dialog.findChild<QLineEdit*>("smallerCandidateShortcut");
        auto* larger = dialog.findChild<QLineEdit*>("largerCandidateShortcut");
        QVERIFY(smaller); QVERIFY(larger);
        QCOMPARE(smaller->text(), QString("向下滚"));
        QCOMPARE(larger->text(), QString("向上滚"));
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode, false);
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(mode == ThemeMode::Light
                ? "build-result-ui/candidate-shortcuts-light.png" : "build-result-ui/candidate-shortcuts-dark.png"));
        }
        auto wheel = [&](QLineEdit* edit, int delta, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            edit->window()->activateWindow();
            QApplication::processEvents();
            edit->setFocus();
            QApplication::processEvents();
            QVERIFY(edit->hasFocus());
            QWheelEvent event(edit->rect().center(), edit->mapToGlobal(edit->rect().center()),
                QPoint(), QPoint(0, delta), Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QApplication::sendEvent(edit, &event);
        };
        wheel(smaller, 60); QCOMPARE(smaller->text(), QString("向下滚"));
        wheel(smaller, 60); QCOMPARE(smaller->text(), QString("向上滚"));
        // Conflicting wheel bindings are rejected without committing either draft.
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget)) box->accept();
        }); dismiss.start(10);
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(HotkeyConfig::instance().data().snippingSmallerCandidate, QString("WheelDown"));
        wheel(larger, -120);
        QCOMPARE(larger->text(), QString("向下滚"));
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingSmallerCandidate, QString("WheelUp"));
        QCOMPARE(HotkeyConfig::instance().data().snippingLargerCandidate, QString("WheelDown"));

        HotkeySettingsDialog keys;
        auto* smallKey = keys.findChild<QLineEdit*>("smallerCandidateShortcut");
        auto* largeKey = keys.findChild<QLineEdit*>("largerCandidateShortcut");
        QTest::keyClick(smallKey, Qt::Key_J, Qt::ControlModifier);
        QTest::keyClick(largeKey, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(smallKey->text(), QString("Ctrl+J"));
        QCOMPARE(largeKey->text(), QString("Ctrl+K"));
        keys.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(keys.result(), int(QDialog::Accepted));
        QCOMPARE(HotkeyConfig::instance().data().snippingSmallerCandidate, QString("Ctrl+J"));
        {
            HotkeySettingsDialog draft;
            auto* edit = draft.findChild<QLineEdit*>("smallerCandidateShortcut");
            QTest::keyClick(edit, Qt::Key_Escape); QCOMPARE(edit->text(), QString("Esc"));
            QTest::keyClick(edit, Qt::Key_Tab); QCOMPARE(edit->text(), QString("Tab"));
            QTest::keyClick(edit, Qt::Key_Return); QCOMPARE(edit->text(), QString("Return"));
            draft.findChild<QPushButton*>("settingsSave")->click(); // Conflicts with confirm.
            QCOMPARE(draft.result(), int(QDialog::Rejected));
            button(draft, "恢复快捷键默认")->click();
            QCOMPARE(edit->text(), QString("向下滚"));
            draft.reject();
        }
        QCOMPARE(HotkeyConfig::instance().data().snippingSmallerCandidate, QString("Ctrl+J"));
        HotkeySettingsDialog clear;
        auto* edit = clear.findChild<QLineEdit*>("smallerCandidateShortcut");
        auto* clearButton = button(*edit->parentWidget(), "清空");
        QVERIFY(clearButton); clearButton->click();
        clear.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(clear.result(), int(QDialog::Accepted));
        HotkeyConfig::instance().load();
        QVERIFY(HotkeyConfig::instance().data().snippingSmallerCandidate.isEmpty());
    }
    void candidateResizeSettings() {
        const auto original = HotkeyConfig::instance().data();
        const auto theme = ThemeManager::instance().themeMode();
        const auto restore = qScopeGuard([&] {
            HotkeyConfig::instance().setData(original);
            ThemeManager::instance().setThemeMode(theme, false);
        });
        HotkeyConfig::instance().setData(HotkeyConfigData{});
        HotkeySettingsDialog dialog; dialog.show();
        auto* shrink = dialog.findChild<QLineEdit*>("shrinkCandidateShortcut");
        auto* expand = dialog.findChild<QLineEdit*>("expandCandidateShortcut");
        auto* step = dialog.findChild<QSpinBox*>("candidateResizeStep");
        QVERIFY(shrink); QVERIFY(expand); QVERIFY(step);
        QCOMPARE(step->value(), 1); QCOMPARE(step->minimum(), 1); QCOMPARE(step->maximum(), 50);
        QCOMPARE(shrink->text(), QString("Ctrl+向下滚"));
        QCOMPARE(expand->text(), QString("Ctrl+向上滚"));
        const QString evidence = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            "../docs/evidence/candidate-resize-2026-10-18");
        QVERIFY(QDir().mkpath(evidence));
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode, false);
            auto* pages = dialog.findChild<QTabWidget*>("settingsPages");
            const QString suffix = mode == ThemeMode::Light ? "light" : "dark";
            for (int page : {1, 3}) {
                pages->setCurrentIndex(page);
                dialog.findChild<QLineEdit*>("shortcutSearch")->setText("候选");
                QApplication::processEvents();
                QVERIFY(dialog.grab().save(evidence + "/settings-" + QString::number(page) + "-" + suffix + ".png"));
            }
        }
        QTest::keyClick(shrink, Qt::Key_J, Qt::ControlModifier);
        QTest::keyClick(expand, Qt::Key_J, Qt::ControlModifier); step->setValue(7);
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget)) box->accept();
        }); dismiss.start(10);
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(HotkeyConfig::instance().data().snippingCandidateResizeStep, 1);
        QTest::keyClick(expand, Qt::Key_K, Qt::ControlModifier);
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingShrinkCandidate, QString("Ctrl+J"));
        QCOMPARE(HotkeyConfig::instance().data().snippingExpandCandidate, QString("Ctrl+K"));
        QCOMPARE(HotkeyConfig::instance().data().snippingCandidateResizeStep, 7);
        {
            HotkeySettingsDialog draft;
            auto* edit = draft.findChild<QLineEdit*>("shrinkCandidateShortcut");
            auto* draftStep = draft.findChild<QSpinBox*>("candidateResizeStep");
            QCOMPARE(draftStep->value(), 7); draftStep->setValue(50);
            button(draft, "恢复快捷键默认")->click();
            QCOMPARE(edit->text(), QString("Ctrl+向下滚")); QCOMPARE(draftStep->value(), 1);
            QCOMPARE(HotkeyConfig::instance().data().snippingShrinkCandidate, QString("Ctrl+J"));
            draft.reject();
        }
        QCOMPARE(HotkeyConfig::instance().data().snippingCandidateResizeStep, 7);
        HotkeySettingsDialog clear;
        auto* edit = clear.findChild<QLineEdit*>("shrinkCandidateShortcut");
        button(*edit->parentWidget(), "清空")->click();
        clear.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(clear.result(), int(QDialog::Accepted)); HotkeyConfig::instance().load();
        QVERIFY(HotkeyConfig::instance().data().snippingShrinkCandidate.isEmpty());
        QSettings cfg(QSettings::defaultFormat(), QSettings::UserScope, "Evan", "Evan");
        for (int invalid : {0, 100}) {
            cfg.setValue("Hotkey/SnippingCandidateResizeStep", invalid); cfg.sync();
            HotkeyConfig::instance().load();
            QCOMPARE(HotkeyConfig::instance().data().snippingCandidateResizeStep, invalid == 0 ? 1 : 50);
        }
        cfg.setValue("Hotkey/SnippingSmallerCandidate", "Ctrl+WheelDown");
        cfg.setValue("Hotkey/SnippingLargerCandidate", "Ctrl+WheelUp");
        cfg.remove("Hotkey/SnippingShrinkCandidate"); cfg.remove("Hotkey/SnippingExpandCandidate");
        cfg.sync(); HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingSmallerCandidate, QString("Ctrl+WheelDown"));
        QCOMPARE(HotkeyConfig::instance().data().snippingLargerCandidate, QString("Ctrl+WheelUp"));
        QVERIFY(HotkeyConfig::instance().data().snippingShrinkCandidate.isEmpty());
        QVERIFY(HotkeyConfig::instance().data().snippingExpandCandidate.isEmpty());
        cfg.setValue("Hotkey/SnippingShrinkCandidate", "Ctrl+WheelDown");
        cfg.setValue("Hotkey/SnippingExpandCandidate", "Ctrl+WheelUp");
        cfg.sync(); HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingShrinkCandidate, QString("Ctrl+WheelDown"));
        QCOMPARE(HotkeyConfig::instance().data().snippingExpandCandidate, QString("Ctrl+WheelUp"));
        cfg.setValue("Hotkey/SnippingSmallerCandidate", "WheelDown");
        cfg.setValue("Hotkey/SnippingLargerCandidate", "WheelUp");
        cfg.remove("Hotkey/SnippingShrinkCandidate"); cfg.remove("Hotkey/SnippingExpandCandidate");
        cfg.sync(); HotkeyConfig::instance().load();
        QCOMPARE(HotkeyConfig::instance().data().snippingShrinkCandidate, QString("Ctrl+WheelDown"));
        QCOMPARE(HotkeyConfig::instance().data().snippingExpandCandidate, QString("Ctrl+WheelUp"));
    }
    void settingsDraftCancelAndSave() {
        const auto originalHotkeys = HotkeyConfig::instance().data();
        const auto originalEntries = ToolbarConfig::instance().entries();
        const bool autoStart = AutoStartHelper::isAutoStartEnabled();
        {
            HotkeySettingsDialog dialog;
            dialog.findChild<QComboBox*>("themeMode")->setCurrentIndex(1);
            dialog.findChild<QCheckBox*>("autoStart")->setChecked(!autoStart);
            dialog.findChild<QLineEdit*>("formulaToken")->setText("draft-token");
            auto* toolbar = dialog.findChild<ToolbarSettingsDialog*>();
            auto draft = toolbar->entries(); draft[0].visibility = ToolVisibility::Hidden;
            toolbar->loadEntries(draft);
            button(dialog, "恢复快捷键默认")->click();
            dialog.reject();
        }
        QCOMPARE(AutoStartHelper::isAutoStartEnabled(), autoStart);
        QCOMPARE(HotkeyConfig::instance().data().globalSnipping, originalHotkeys.globalSnipping);
        QCOMPARE(ToolbarConfig::instance().entries()[0].visibility, originalEntries[0].visibility);
        QCOMPARE(FormulaOcrManager::instance().simpleTexToken(), QString());
        {
            HotkeySettingsDialog dialog;
            dialog.findChild<QComboBox*>("themeMode")->setCurrentIndex(1);
            dialog.findChild<QCheckBox*>("smartSnapping")->setChecked(false);
            dialog.findChild<QComboBox*>("textCommitMode")->setCurrentIndex(1);
            dialog.findChild<QLineEdit*>("formulaToken")->setText("test-token");
            auto* toolbar = dialog.findChild<ToolbarSettingsDialog*>();
            auto draft = toolbar->entries(); draft[0].visibility = ToolVisibility::Hidden;
            toolbar->loadEntries(draft);
            dialog.findChild<QPushButton*>("settingsSave")->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QCOMPARE(ThemeManager::instance().themeMode(), ThemeMode::Light);
        QCOMPARE(FormulaOcrManager::instance().simpleTexToken(), QString("test-token"));
        QCOMPARE(ToolbarConfig::instance().entries()[0].visibility, ToolVisibility::Hidden);
        QCOMPARE(AutoStartHelper::isAutoStartEnabled(), autoStart);
        HotkeySettingsDialog reopened;
        QVERIFY(!reopened.findChild<QCheckBox*>("smartSnapping")->isChecked());
        QCOMPARE(reopened.findChild<QComboBox*>("textCommitMode")->currentIndex(), 1);
        QCOMPARE(reopened.findChild<QLineEdit*>("formulaToken")->text(), QString("test-token"));
        FormulaOcrManager::instance().setSimpleTexToken("");
        ToolbarConfig::instance().setEntries(originalEntries);
    }
    void shortcutValidationAndResetRemainDraft() {
        auto custom = HotkeyConfig::instance().data(); custom.globalSnipping = "Ctrl+F9";
        HotkeyConfig::instance().setData(custom);
        HotkeySettingsDialog dialog;
        button(dialog, "恢复快捷键默认")->click();
        QCOMPARE(HotkeyConfig::instance().data().globalSnipping, QString("Ctrl+F9"));
        auto edits = dialog.findChildren<QKeySequenceEdit*>();
        auto* search = dialog.findChild<QLineEdit*>("shortcutSearch");
        search->setText("矩形框");
        QVERIFY(edits[0]->parentWidget()->isHidden());
        search->clear();
        QVERIFY(!edits[0]->parentWidget()->isHidden());
        edits[0]->clear();
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget)) box->accept();
        }); dismiss.start(10);
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(dialog.findChild<QTabWidget*>("settingsPages")->currentIndex(), 3);
        edits[0]->setKeySequence(QKeySequence("F1"));
        edits[1]->setKeySequence(QKeySequence("F1"));
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(HotkeyConfig::instance().data().globalSnipping, QString("Ctrl+F9"));
        button(dialog, "恢复快捷键默认")->click();
        edits[2]->setKeySequence(QKeySequence("R"));
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(HotkeyConfig::instance().data().globalSnipping, QString("Ctrl+F9"));
        button(dialog, "恢复快捷键默认")->click();
        dialog.findChild<QPushButton*>("settingsSave")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(HotkeyConfig::instance().data().globalSnipping, QString("F1"));
    }
    void toolbarOrderVisibilityThemeAndReset() {
        ToolbarConfig::instance().setEntries(ToolbarConfig::defaults());
        ToolbarSettingsDialog dialog;
        auto* list = dialog.findChild<QListWidget*>("toolbarToolList");
        list->setCurrentRow(0);
        button(dialog, "下移")->click();
        QCOMPARE(dialog.entries()[1].action, ToolAction::Rect);
        auto* combo = list->itemWidget(list->item(1))->findChild<QComboBox*>();
        combo->setCurrentIndex(combo->findData(int(ToolVisibility::More)));
        ThemeManager::instance().setThemeMode(ThemeMode::Dark);
        QCOMPARE(dialog.entries()[1].visibility, ToolVisibility::More);
        QCOMPARE(dialog.entries()[1].action, ToolAction::Rect);
        QCOMPARE(ToolbarConfig::instance().entries()[0].action, ToolAction::Rect);
        dialog.accept();
        QCOMPARE(ToolbarConfig::instance().entries()[1].visibility, ToolVisibility::More);
        ToolbarSettingsDialog reopened;
        button(reopened, "恢复默认")->click();
        QCOMPARE(reopened.entries()[0].action, ToolAction::Rect);
        reopened.reject();
        QCOMPARE(ToolbarConfig::instance().entries()[1].visibility, ToolVisibility::More);
        ToolbarConfig::instance().setEntries(ToolbarConfig::defaults());
    }
    void renderSettingsThemes() {
        QDir().mkpath("screenshots");
        HotkeySettingsDialog dialog;
        auto* tabs = dialog.findChild<QTabWidget*>("settingsPages");
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            dialog.findChild<QComboBox*>("themeMode")->setCurrentIndex(mode == ThemeMode::Light ? 1 : 2);
            dialog.resize(760, 560); dialog.show();
            for (int page = 0; page < tabs->count(); ++page) {
                tabs->setCurrentIndex(page); QTest::qWait(20);
                for (auto* b : dialog.findChildren<QPushButton*>()) {
                    if (!b->isVisible() || b->visibleRegion().isEmpty() || b->objectName() == "clearBtn") continue;
                    QVERIFY2(dialog.rect().contains(QRect(b->mapTo(&dialog, QPoint()), b->size())), qPrintable(b->text()));
                }
                QCOMPARE(QFontInfo(tabs->font()).family(), QString("Segoe UI"));
                QVERIFY(dialog.grab().save(QString("screenshots/settings-%1-%2.png")
                    .arg(mode == ThemeMode::Light ? "light" : "dark").arg(page)));
            }
        }
    }
    void renderThemes() {
        QDir().mkpath("screenshots");
        OcrResultDialog dialog;
        QPixmap image(260, 160); image.fill(QColor("#f6f8fc"));
        { QPainter painter(&image); painter.setPen(QColor("#253148"));
          painter.drawText(QRect(16, 20, 228, 120), Qt::TextWordWrap,
            QStringLiteral("识别示例\n高效、简洁的文字工作区。\nEdit, then translate.")); }
        dialog.findChild<QPushButton*>("showTranslation")->setChecked(true);
        dialog.setImage(image);
        OcrResult result; result.success = true;
        result.fullText = QStringLiteral("高效、简洁的文字工作区。\n在这里修正识别结果，整理段落，再复制或翻译。\nThe text you edit is the text you translate.");
        dialog.setResult(result, "WindowsMedia");
        for(auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ThemeManager::instance().setThemeMode(mode);
            dialog.resize(1040, 700); dialog.show();
            QTest::qWait(30);
            QCOMPARE(QFontInfo(dialog.findChild<QPlainTextEdit*>("sourceText")->font()).family(), QString("Segoe UI"));
            QVERIFY(dialog.grab().save(mode == ThemeMode::Light ? "screenshots/ocr-light.png" : "screenshots/ocr-dark.png"));
            dialog.resize(840, 520);
            QTest::qWait(30);
            for(auto* b : dialog.findChildren<QPushButton*>()) {
                if (!b->isVisible()) continue;
                const QRect rect(b->mapTo(&dialog, QPoint()), b->size());
                QVERIFY2(dialog.rect().contains(rect), qPrintable(b->text()));
            }
            QVERIFY(dialog.grab().save(mode == ThemeMode::Light ? "screenshots/ocr-compact-light.png" : "screenshots/ocr-compact-dark.png"));
        }
    }
};
QTEST_MAIN(ResultUiTest)
#include "result_ui.moc"
