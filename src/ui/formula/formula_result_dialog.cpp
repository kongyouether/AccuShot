#include "../../core/plugin/local_plugin.h"
#include "../../core/plugin/plugin_manager.h"
#include "../../core/ai/ai_service.h"
#include "../settings/hotkey_settings_dialog.h"
#include "../themed_combo_box.h"
#include "../toolbar/toolbar_icons.h"
#include "formula_result_dialog.h"
#include "../../core/formula/formula_format_helper.h"
#include "../plugin/plugin_market_dialog.h"
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QPainter>
#include <QMessageBox>
#include <QInputDialog>
#include <QPointer>
#include <QDebug>
#include "../../core/theme_manager.h"

FormulaResultDialog::FormulaResultDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle("数学公式识别与转换 - AccuShot");
    setWindowIcon(appIcon());
    resize(940, 620);
    setMinimumSize(780, 500);

    setupUi();
    m_copyWordBtn->setObjectName("primaryAction");
    auto applyTheme = [this] {
        setStyleSheet(ThemeManager::instance().getResultDialogStyle());
        const auto colors = ThemeManager::instance().palette();
        m_copyLatexBtn->setIcon(uiIcon("copytext", colors.text));
        m_copyMarkdownBtn->setIcon(uiIcon("copytext", colors.text));
        m_copyWordBtn->setIcon(uiIcon("copy", colors.onAction));
        m_retryBtn->setIcon(uiIcon("ocr", colors.text));
        m_tokenBtn->setIcon(uiIcon("settings", colors.text));
        m_closeBtn->setIcon(uiIcon("close", colors.text));
    };
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, applyTheme);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(500);
    connect(m_previewTimer, &QTimer::timeout, this, &FormulaResultDialog::refreshPreview);
    connect(m_latexEdit, &QPlainTextEdit::textChanged, this, [this] {
        ++m_previewRevision;
        updateCopyButtonsState();
        m_renderedFormulaLabel->setText(m_latexEdit->toPlainText().trimmed().isEmpty() ? "暂无公式" : "预览更新中…");
        m_previewTimer->start();
    });
    updateCopyButtonsState();
    for (auto* button : findChildren<QPushButton*>()) button->setAutoDefault(false);
}

void FormulaResultDialog::setupUi()
{


    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(12);

    // ==========================================
    // 顶部控制栏 (Header Bar)
    // ==========================================
    auto* topHeaderLayout = new QHBoxLayout();
    topHeaderLayout->setSpacing(10);

    auto* titleLabel = new QLabel("公式识别与转换", this);
    titleLabel->setObjectName("pageTitle");

    topHeaderLayout->addWidget(titleLabel);

    auto* badgeLabel = new QLabel("LaTeX / MathML / Word", this);
    badgeLabel->setObjectName("secondaryText");

    topHeaderLayout->addWidget(badgeLabel);

    topHeaderLayout->addStretch();

    auto* modeLabel = new QLabel("识别通道:", this);

    topHeaderLayout->addWidget(modeLabel);

    m_engineCombo = new ThemedComboBox(this);
    m_engineCombo->setFixedWidth(200);
    refreshEngineComboState();
    connect(&PluginManager::instance(),&PluginManager::pluginsRefreshed,this,&FormulaResultDialog::refreshEngineComboState);

    connect(m_engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        // Selection applies to this result window only.
        if (!m_sourceImage.isNull()) {
            startRecognition(m_sourceImage, m_dpr);
        }
    });
    topHeaderLayout->addWidget(m_engineCombo);
    auto* setDefault=new QPushButton("设为默认",this);connect(setDefault,&QPushButton::clicked,this,[this]{const auto mode=m_engineCombo->currentData().toString();if(mode!="Offline_missing")FormulaOcrManager::instance().setPreferredMode(mode);});topHeaderLayout->addWidget(setDefault);
    m_retryBtn=new QPushButton("重新识别",this);m_retryBtn->setEnabled(false);connect(m_retryBtn,&QPushButton::clicked,this,[this]{if(m_aiReply)m_aiReply->abort();else if(!m_recognizing&&!m_sourceImage.isNull())startRecognition(m_sourceImage,m_dpr);});topHeaderLayout->addWidget(m_retryBtn);

    m_tokenBtn = new QPushButton("配置 Token", this);
    m_tokenBtn->setCursor(Qt::PointingHandCursor);
    m_tokenBtn->setToolTip("配置 SimpleTeX 授权 Token (可在 simpletex.cn 免费申请，每月赠送上千次调用)");
    connect(m_tokenBtn, &QPushButton::clicked, this, &FormulaResultDialog::onTokenConfigClicked);
    topHeaderLayout->addWidget(m_tokenBtn);

    mainLayout->addLayout(topHeaderLayout);

    // ==========================================
    // 核心垂直分割器 (Top Comparison / Bottom Code)
    // ==========================================
    auto* vSplitter = new QSplitter(Qt::Vertical, this);
    vSplitter->setHandleWidth(12);
    vSplitter->setChildrenCollapsible(false);

    // 上半部分水平对比 Splitter
    auto* hSplitter = new QSplitter(Qt::Horizontal, vSplitter);
    hSplitter->setHandleWidth(16);
    hSplitter->setChildrenCollapsible(false);

    // 左侧卡片：原图
    auto* leftBox = new QWidget(hSplitter);
    leftBox->setObjectName("resultPanel");
    leftBox->setProperty("surfaceRole", "well");
    auto* leftLayout = new QVBoxLayout(leftBox);
    leftLayout->setContentsMargins(16, 16, 16, 16);
    leftLayout->setSpacing(12);

    auto* origHeader = new QHBoxLayout();
    auto* origTitle = new QLabel("选区原图", leftBox);
    origTitle->setObjectName("sectionTitle");

    origHeader->addWidget(origTitle);
    origHeader->addStretch();
    leftLayout->addLayout(origHeader);

    m_originalScrollArea = new QScrollArea(leftBox);
    m_originalScrollArea->setWidgetResizable(true);
    m_originalScrollArea->setAlignment(Qt::AlignCenter);
    m_originalImgLabel = new QLabel(m_originalScrollArea);
    m_originalImgLabel->setAlignment(Qt::AlignCenter);
    m_originalScrollArea->setWidget(m_originalImgLabel);
    leftLayout->addWidget(m_originalScrollArea, 1);

    hSplitter->addWidget(leftBox);

    // 右侧卡片：实时渲染公式预览 (超高清防模糊)
    auto* rightBox = new QWidget(hSplitter);
    rightBox->setObjectName("resultPanel");
    rightBox->setProperty("surfaceRole", "raised");
    auto* rightLayout = new QVBoxLayout(rightBox);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    rightLayout->setSpacing(12);

    auto* prevHeader = new QHBoxLayout();
    auto* previewTitle = new QLabel("公式矢量渲染预览", rightBox);
    previewTitle->setObjectName("sectionTitle");

    prevHeader->addWidget(previewTitle);
    prevHeader->addStretch();
    rightLayout->addLayout(prevHeader);

    m_renderedScrollArea = new QScrollArea(rightBox);
    m_renderedScrollArea->setWidgetResizable(true);
    m_renderedScrollArea->setAlignment(Qt::AlignCenter);
    m_renderedFormulaLabel = new QLabel("等待公式识别结果...", m_renderedScrollArea);
    m_renderedFormulaLabel->setAlignment(Qt::AlignCenter);

    m_renderedScrollArea->setWidget(m_renderedFormulaLabel);
    rightLayout->addWidget(m_renderedScrollArea, 1);

    hSplitter->addWidget(rightBox);
    hSplitter->setSizes({450, 450});

    vSplitter->addWidget(hSplitter);

    // 下半部卡片：LaTeX 源码与微调编辑
    auto* codeBox = new QWidget(vSplitter);
    codeBox->setObjectName("resultPanel");
    codeBox->setProperty("surfaceRole", "raised");
    auto* codeLayout = new QVBoxLayout(codeBox);
    codeLayout->setContentsMargins(16, 16, 16, 16);
    codeLayout->setSpacing(12);

    auto* codeHeaderLayout = new QHBoxLayout();
    auto* codeTitle = new QLabel("LaTeX 源码 (可直接修改编辑):", codeBox);
    codeTitle->setObjectName("sectionTitle");

    codeHeaderLayout->addWidget(codeTitle);
    codeHeaderLayout->addStretch();
    codeLayout->addLayout(codeHeaderLayout);

    m_latexEdit = new QPlainTextEdit(codeBox);
    m_latexEdit->setObjectName("latexSource");
    m_latexEdit->setPlaceholderText("公式识别完成后，LaTeX 代码将在此呈现并支持编辑修改...");
    codeLayout->addWidget(m_latexEdit, 1);

    vSplitter->addWidget(codeBox);
    vSplitter->setSizes({240, 280});

    mainLayout->addWidget(vSplitter, 1);

    // ==========================================
    // 底部工具与动作栏 (Footer Action Bar)
    // ==========================================
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);

    m_statusLabel = new QLabel("就绪", this);
    m_statusLabel->setObjectName("secondaryText");

    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    // 1. 复制 LaTeX
    m_copyLatexBtn = new QPushButton("复制 LaTeX", this);
    m_copyLatexBtn->setCursor(Qt::PointingHandCursor);
    m_copyLatexBtn->setToolTip("复制纯净 LaTeX 公式代码");
    connect(m_copyLatexBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_latexEdit->toPlainText().trimmed();
        if (text.isEmpty()) return;
        QApplication::clipboard()->setText(text);
        m_copyLatexBtn->setText("已复制");
        QTimer::singleShot(1500, this, [this]() {
            if (m_copyLatexBtn) m_copyLatexBtn->setText("复制 LaTeX");
        });
    });
    bottomLayout->addWidget(m_copyLatexBtn);

    // 2. 复制 Markdown
    m_copyMarkdownBtn = new QPushButton("复制 Markdown", this);
    m_copyMarkdownBtn->setCursor(Qt::PointingHandCursor);
    m_copyMarkdownBtn->setToolTip("复制为 $$ ... $$ 块级 Markdown 公式语法");
    connect(m_copyMarkdownBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_latexEdit->toPlainText().trimmed();
        if (text.isEmpty()) return;
        QString md = FormulaFormatHelper::toBlockMarkdown(text);
        QApplication::clipboard()->setText(md);
        m_copyMarkdownBtn->setText("已复制");
        QTimer::singleShot(1500, this, [this]() {
            if (m_copyMarkdownBtn) m_copyMarkdownBtn->setText("复制 Markdown");
        });
    });
    bottomLayout->addWidget(m_copyMarkdownBtn);

    // 3. 复制 MathML (Word 直粘，视觉主按钮)
    m_copyWordBtn = new QPushButton("复制 Word (MathML)", this);
    m_copyWordBtn->setCursor(Qt::PointingHandCursor);
    m_copyWordBtn->setToolTip("复制为标准 MathML XML，切换至 Microsoft Word 后直接 Ctrl+V 即刻转为原生 Word 数学公式");

    connect(m_copyWordBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_latexEdit->toPlainText().trimmed();
        if (text.isEmpty()) return;
        QString mathml = FormulaFormatHelper::toWordMathML(text);
        QApplication::clipboard()->setText(mathml);
        m_copyWordBtn->setText("已复制");
        QTimer::singleShot(1500, this, [this]() {
            if (m_copyWordBtn) m_copyWordBtn->setText("复制 Word (MathML)");
        });
    });
    bottomLayout->addWidget(m_copyWordBtn);

    m_closeBtn = new QPushButton("关闭", this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(bottomLayout);
}

void FormulaResultDialog::onTokenConfigClicked()
{
    QString currentToken = FormulaOcrManager::instance().simpleTexToken();
    bool ok = false;
    QString text = QInputDialog::getText(
        this,
        "SimpleTeX Token 配置",
        "请输入 SimpleTeX 授权 Token (免费申请地址: https://simpletex.cn):\n填入后即可永久解锁每月上千次免费高精度在线公式识别：",
        QLineEdit::Password,
        currentToken,
        &ok
    );
    if (ok) {
        FormulaOcrManager::instance().setSimpleTexToken(text.trimmed());
        QMessageBox::information(this, "提示", "SimpleTeX Token 配置已成功保存！");
        if (!m_sourceImage.isNull()) {
            startRecognition(m_sourceImage, m_dpr);
        }
    }
}

void FormulaResultDialog::refreshEngineComboState()
{
    if (!m_engineCombo) return;
    const QString selected=m_engineCombo->currentData().toString();
    QSignalBlocker blocker(m_engineCombo);
    m_engineCombo->clear();

    m_engineCombo->addItem("在线识别", "Online");

    m_engineCombo->addItem("RapidLaTeXOCR", "Offline");
    m_engineCombo->addItem("Pix2Text MFR", "local:pix2text");
    m_engineCombo->addItem("PP-FormulaNet_plus-M", "local:paddleformula");
    setPluginChoiceEnabled(m_engineCombo,"Offline",LocalPlugin::ready("latex-ocr-engine"));
    setPluginChoiceEnabled(m_engineCombo,"local:pix2text",LocalPlugin::ready("pix2text"));
    setPluginChoiceEnabled(m_engineCombo,"local:paddleformula",LocalPlugin::ready("paddleformula"));
    for(const auto& p:AiService::profiles())if(p.vision && !AiService::isLocalProfile(p))m_engineCombo->addItem(p.name,"ai:"+p.id);
    QString pref = selected.isEmpty()?FormulaOcrManager::instance().preferredMode():selected;
    int idx = m_engineCombo->findData(pref);
    if (idx >= 0) {
        m_engineCombo->setCurrentIndex(idx);
    } else {
        m_engineCombo->addItem("原引擎不可用，请选择已安装插件", pref);
        setPluginChoiceEnabled(m_engineCombo,pref,false);
        m_engineCombo->setCurrentIndex(m_engineCombo->count()-1);
    }
}

void FormulaResultDialog::startRecognition(const QPixmap& pixmap, qreal dpr)
{
    m_sourceImage = pixmap;
    m_dpr = dpr;

    // 显示原图
    if (m_originalImgLabel) {
        m_originalImgLabel->setPixmap(pixmap);
    }

    // 重置状态
    if(m_recognizing)return;
    m_recognizing = true;
    m_retryBtn->setEnabled(false);
    m_engineCombo->setEnabled(false);
    m_latexEdit->setReadOnly(true);
    m_latexEdit->setPlaceholderText("正在识别公式…");
    updateCopyButtonsState();
    if (m_latexEdit->toPlainText().isEmpty()) m_renderedFormulaLabel->setText("公式正在生成中...");
    m_statusLabel->setText("正在连接通道并提取公式结构...");

    // 使用 QPointer 安全守卫：彻底防止用户中途关闭窗口时异步回调触发崩溃卡退
    QPointer<FormulaResultDialog> guard(this);

    m_aiReply=FormulaOcrManager::instance().recognizeAsync(pixmap.toImage(), dpr, [guard, this](const FormulaOcrResult& res) {
        if (!guard) return; // 窗口已关闭，安全退出
        m_recognizing = false;
        m_retryBtn->setText("重新识别");m_retryBtn->setEnabled(true);
        m_engineCombo->setEnabled(true);
        m_latexEdit->setReadOnly(false);
        updateCopyButtonsState();
        m_lastResult = res;

        if (!res.success) {
            m_latexEdit->setPlaceholderText(QString("识别失败：%1。可切换通道重试。").arg(res.errorMessage));
            m_statusLabel->setText(QString("识别失败：%1。已有公式已保留，可重新识别。通道：%2").arg(res.errorMessage, res.engineName));
            if (m_latexEdit->toPlainText().isEmpty()) m_renderedFormulaLabel->setText("选区内未提取到有效公式");

            if (res.isAuthError && FormulaOcrManager::instance().isOfflinePluginReady()) {
                auto reply = QMessageBox::question(
                    this,
                    "在线通道未授权",
                    res.errorMessage + "\n\n检测到本地已安装离线公式识别插件 (RapidLaTeX)，是否立即切换至本地离线插件进行识别？",
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes
                );
                if (reply == QMessageBox::Yes) {
                    QSignalBlocker block(m_engineCombo);
                    m_engineCombo->setCurrentIndex(m_engineCombo->findData("Offline"));
                    startRecognition(m_sourceImage, m_dpr);
                }
            }
            return;
        }

        m_latexEdit->setPlainText(res.latex);
        m_statusLabel->setText(QString("耗时: %1ms | 通道: %2").arg(res.elapsedMs).arg(res.engineName));

    }, m_engineCombo->currentData().toString());
    if(m_aiReply){m_retryBtn->setText("取消识别");m_retryBtn->setEnabled(true);connect(this,&QObject::destroyed,m_aiReply,&QNetworkReply::abort);}
}

void FormulaResultDialog::updateCopyButtonsState()
{
    const bool enabled = !m_recognizing && !m_latexEdit->toPlainText().trimmed().isEmpty();
    m_copyLatexBtn->setEnabled(enabled);
    m_copyMarkdownBtn->setEnabled(enabled);
    m_copyWordBtn->setEnabled(enabled);
}

void FormulaResultDialog::refreshPreview()
{
    const auto engine=m_engineCombo->currentData().toString();
    if(engine=="Offline" || engine.startsWith("local:")) {
        m_renderedFormulaLabel->setText("本地模式：可编辑、复制 LaTeX；不发送公式到在线预览服务。");
        return;
    }
    const QString latex = m_latexEdit->toPlainText().trimmed();
    if (m_recognizing || latex.isEmpty()) return;
    const int revision = m_previewRevision;
    QPointer<FormulaResultDialog> guard(this);
    FormulaOcrManager::instance().renderFormulaAsync(latex,
        [guard, revision](bool success, const QByteArray& imageData, const QString&) {
            if (!guard || revision != guard->m_previewRevision) return;
            QPixmap pixmap;
            if (!success || !pixmap.loadFromData(imageData)) {
                guard->m_renderedFormulaLabel->setText("预览暂不可用，仍可编辑和复制源码");
                return;
            }
            pixmap.setDevicePixelRatio(300.0 / 96.0);
            guard->m_renderedFormulaLabel->setPixmap(pixmap);
        });
}
