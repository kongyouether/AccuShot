#include "markdown_result_dialog.h"
#include "../../core/plugin/plugin_manager.h"
#include "../../core/theme_manager.h"
#include "../plugin/plugin_market_dialog.h"
#include "../settings/hotkey_settings_dialog.h"
#include "../themed_combo_box.h"
#include "../toolbar/toolbar_icons.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QFileInfo>

// Preview only the generated document's assets; never fetch image URLs from model output.
class MarkdownPreview : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    QString assetRoot;
    QVariant loadResource(int type,const QUrl& url) override {
        const QString path=QFileInfo(document()->baseUrl().resolved(url).toLocalFile()).canonicalFilePath();
        if(type==QTextDocument::ImageResource && !assetRoot.isEmpty() && path.startsWith(assetRoot+"/"))return QImage(path);
        return {};
    }
};
MarkdownResultDialog::MarkdownResultDialog(QWidget* parent):QDialog(parent) {
    setWindowTitle("截图转 Markdown - AccuShot");setWindowIcon(appIcon());resize(1080,720);setMinimumSize(780,520);
    setWindowFlags(Qt::Window|Qt::WindowCloseButtonHint|Qt::WindowMinMaxButtonsHint);
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(24,24,24,24);layout->setSpacing(12);
    auto* title=new QLabel("截图转 Markdown",this);title->setObjectName("pageTitle");layout->addWidget(title);
    auto* top=new QHBoxLayout;
    m_providers=new ThemedComboBox(this);m_providers->setObjectName("markdownProvider");m_providers->setMinimumWidth(300);
    top->addWidget(m_providers,1);
    auto* plugins=new QPushButton("插件与模型",this);top->addWidget(plugins);
    auto* settings=new QPushButton("Markdown 方案设置",this);top->addWidget(settings);
    m_run=new QPushButton("开始转换",this);m_run->setObjectName("primaryAction");top->addWidget(m_run);layout->addLayout(top);
    m_description=new QLabel(this);m_description->setObjectName("secondaryText");m_description->setWordWrap(true);layout->addWidget(m_description);
    auto* split=new QSplitter(this);
    split->setHandleWidth(16);split->setChildrenCollapsible(false);
    m_original=new QLabel("截图原图",this);m_original->setObjectName("markdownOriginal");m_original->setAlignment(Qt::AlignCenter);m_original->setMinimumWidth(200);split->addWidget(m_original);
    auto* tabs=new QTabWidget(this);
    m_editor=new QPlainTextEdit(this);m_editor->setObjectName("markdownSource");m_editor->setPlaceholderText("选择已安装的插件后开始转换，也可以在此编辑 Markdown。");tabs->addTab(m_editor,"Markdown 源码");
    m_preview=new MarkdownPreview(this);m_preview->setOpenLinks(false);m_preview->setOpenExternalLinks(false);tabs->addTab(m_preview,"排版预览");split->addWidget(tabs);split->setStretchFactor(1,2);layout->addWidget(split,1);
    auto* note=new QLabel("预览支持基础 Markdown；数学公式保留 LaTeX 源码。含插图时请保存 .md 和配套图片。",this);note->setWordWrap(true);note->setObjectName("secondaryText");layout->addWidget(note);
    m_status=new QLabel("选择插件后点击开始转换。",this);m_status->setWordWrap(true);layout->addWidget(m_status);
    auto* bottom=new QHBoxLayout;
    auto* setDefault=new QPushButton("设为默认",this);bottom->addWidget(setDefault);bottom->addStretch();
    m_copy=new QPushButton("复制 Markdown",this);m_copy->setObjectName("markdownCopy");bottom->addWidget(m_copy);
    m_save=new QPushButton("保存 .md…",this);m_save->setObjectName("markdownSave");bottom->addWidget(m_save);layout->addLayout(bottom);
    connect(plugins,&QPushButton::clicked,this,[this]{PluginMarketDialog dialog(this);dialog.exec();refreshProviders();});
    connect(settings,&QPushButton::clicked,this,[this]{HotkeySettingsDialog dialog(this);dialog.openMarkdown();dialog.exec();refreshProviders();});
    connect(&PluginManager::instance(),&PluginManager::pluginsRefreshed,this,&MarkdownResultDialog::refreshProviders);
    connect(m_providers,&QComboBox::currentIndexChanged,this,[this]{
        m_description->setText(m_providers->currentData(Qt::ToolTipRole).toString());
        m_run->setEnabled(!m_busy && !m_image.isNull() && m_providers->currentData(Qt::UserRole+1).toBool());
    });
    connect(setDefault,&QPushButton::clicked,this,[this]{if(m_providers->currentData(Qt::UserRole+1).toBool()){QSettings().setValue("Markdown/Provider",m_providers->currentData());m_status->setText("已保存默认插件。");}});
    connect(m_run,&QPushButton::clicked,this,&MarkdownResultDialog::recognize);
    connect(m_editor,&QPlainTextEdit::textChanged,this,&MarkdownResultDialog::updatePreview);
    connect(m_copy,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(m_editor->toPlainText());m_status->setText("已复制源码；配套插图请使用保存功能。");});
    connect(m_save,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,"保存 Markdown","截图.md","Markdown (*.md)");
        if(path.isEmpty())return;
        QString error;
        m_status->setText(MarkdownConverter::save(path,m_editor->toPlainText(),m_result,&error)?"已保存："+path:error);
    });
    auto theme=[this,plugins,settings]{
        auto& manager=ThemeManager::instance();const auto palette=manager.palette();
        setStyleSheet(manager.getResultDialogStyle());
        m_preview->setStyleSheet(QString("QTextBrowser { background: %1; color: %2; border: none; border-radius: 12px; padding: 16px; }").arg(palette.raised.name(),palette.text.name()));
        m_original->setStyleSheet(QString("background:%1;border-radius:12px;padding:16px;").arg(palette.well.name()));
        auto font=m_editor->font();font.setPixelSize(manager.resultFontSize());m_editor->setFont(font);m_preview->setFont(font);
        plugins->setIcon(uiIcon("plugin",palette.text));settings->setIcon(uiIcon("settings",palette.text));
        m_run->setIcon(uiIcon("markdown",palette.onAction));m_copy->setIcon(uiIcon("copytext",palette.text));m_save->setIcon(uiIcon("save",palette.text));
    };theme();
    connect(&ThemeManager::instance(),&ThemeManager::themeChanged,this,theme);
    connect(&ThemeManager::instance(),&ThemeManager::resultFontSizeChanged,this,theme);
    refreshProviders();updatePreview();
    for(auto* button:findChildren<QPushButton*>())button->setAutoDefault(false);
}
MarkdownResultDialog::~MarkdownResultDialog(){if(m_reply){m_reply->disconnect(this);m_reply->abort();}}
void MarkdownResultDialog::setImage(const QPixmap& image) {
    m_image=image.toImage();m_original->setPixmap(image.scaled(340,520,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    m_run->setEnabled(!m_busy && !m_image.isNull() && m_providers->currentData(Qt::UserRole+1).toBool());
}
void MarkdownResultDialog::refreshProviders() {
    const auto selected=m_providers->currentData().toString();
    const auto wanted=selected.isEmpty()?QSettings().value("Markdown/Provider","rapidocr").toString():selected;
    m_providers->clear();
    for(const auto& p:MarkdownConverter::providers()) {
        m_providers->addItem(p.name+(p.available?"":"（未安装或未配置）"),p.id);
        const int i=m_providers->count()-1;m_providers->setItemData(i,p.available,Qt::UserRole+1);m_providers->setItemData(i,p.description,Qt::ToolTipRole);
        if(!p.available)qobject_cast<QStandardItemModel*>(m_providers->model())->item(i)->setEnabled(false);
    }
    const int i=m_providers->findData(wanted);if(i>=0)m_providers->setCurrentIndex(i);
    m_description->setText(m_providers->currentData(Qt::ToolTipRole).toString());
    m_run->setEnabled(!m_busy&&!m_image.isNull()&&m_providers->currentData(Qt::UserRole+1).toBool());
}
void MarkdownResultDialog::recognize() {
    if(m_busy)return;
    m_busy=true;m_run->setEnabled(false);m_providers->setEnabled(false);m_status->setText("正在转换…");
    QElapsedTimer timer;timer.start();
    QPointer<MarkdownResultDialog> guard(this);
    m_reply=MarkdownConverter::convert(m_providers->currentData().toString(),m_image,this,[guard,timer](MarkdownResult result){
        if(!guard)return;
        guard->m_busy=false;guard->m_providers->setEnabled(true);guard->refreshProviders();
        if(!result.success){guard->m_status->setText("转换失败："+result.error);return;}
        guard->m_result=std::move(result);
        auto* preview=static_cast<MarkdownPreview*>(guard->m_preview);
        preview->assetRoot=guard->m_result.files?QFileInfo(guard->m_result.files->filePath("assets")).canonicalFilePath():QString();
        preview->document()->setBaseUrl(guard->m_result.files?QUrl::fromLocalFile(guard->m_result.files->path()+"/"):QUrl());
        guard->m_editor->setPlainText(guard->m_result.text);
        guard->m_status->setText(QString("转换完成 · %1 秒 · 请对照原图检查文字、表格及公式。").arg(timer.elapsed()/1000.0,0,'f',2));
    });
}
void MarkdownResultDialog::updatePreview() {
    const auto text=m_editor->toPlainText();m_copy->setEnabled(!text.trimmed().isEmpty());m_save->setEnabled(!text.trimmed().isEmpty());m_preview->setMarkdown(text);
}
