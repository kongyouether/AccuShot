#include "plugin_market_dialog.h"
#include "../../core/plugin/local_plugin.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QUrl>
#include <QDir>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>
#include <QButtonGroup>
#include <QComboBox>
#include <QSignalBlocker>
#include <QShowEvent>
#include "../../core/theme_manager.h"
#include "../toolbar/toolbar_icons.h"

// -------------------------------------------------------------
// PluginCardWidget: 单个扩展插件卡片组件
// -------------------------------------------------------------
PluginCardWidget::PluginCardWidget(const PluginInfo& info, QWidget* parent)
    : QWidget(parent)
    , m_info(info)
{
    setupUi();
    auto theme = [this] {
        setStyleSheet(ThemeManager::instance().getResultDialogStyle());
        const QString icon = m_info.category.contains("文字") ? "ocr" : m_info.category.contains("翻译") ? "translate"
            : m_info.category == "公式识别" ? "formula" : m_info.category.startsWith("Markdown") ? "markdown" : "plugin";
        m_iconLabel->setPixmap(uiIcon(icon, ThemeManager::instance().palette().text).pixmap(QSize(20,20),devicePixelRatioF()));
    };
    theme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, theme);
    refreshState();
}

void PluginCardWidget::setupUi()
{
    setObjectName("pluginCard");
    setProperty("pluginId",m_info.id);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("");

    auto* cardLayout = new QVBoxLayout(this);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(8);

    // 顶部行：图标 + 标题 + Badge + 大小 + 操作按钮
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(10);

    m_iconLabel = new QLabel(this);

    m_iconLabel->setFixedSize(24,24);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_iconLabel);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);

    auto* titleLine = new QHBoxLayout();
    titleLine->setSpacing(6);

    m_titleLabel = new QLabel(m_info.name, this);
    m_titleLabel->setObjectName("cardTitle");
    m_titleLabel->setWordWrap(true);
    titleLayout->addWidget(m_titleLabel);

    m_categoryBadge = new QLabel(m_info.category, this);
    m_categoryBadge->setObjectName("secondaryText");
    titleLine->addWidget(m_categoryBadge);

    m_sizeLabel = new QLabel(QString("大小: %1").arg(m_info.sizeDisplay), this);
    m_sizeLabel->setObjectName("secondaryText");
    m_sizeLabel->setWordWrap(true);
    titleLine->addWidget(m_sizeLabel,1);
    titleLayout->addLayout(titleLine);

    topRow->addLayout(titleLayout, 1);

    // 状态标签
    m_statusLabel = new QLabel(this);

    topRow->addWidget(m_statusLabel);

    // 主操作按钮 (安装 / 取消 / 重试)
    m_actionBtn = new QPushButton(this);
    m_actionBtn->setCursor(Qt::PointingHandCursor);
    m_actionBtn->setMinimumHeight(32);
    m_actionBtn->setObjectName("primaryAction");
    topRow->addWidget(m_actionBtn);

    // 卸载按钮 (平时隐藏或已安装时出现)
    m_uninstallBtn = new QPushButton("卸载", this);
    m_uninstallBtn->setCursor(Qt::PointingHandCursor);
    m_uninstallBtn->setMinimumHeight(32);
    m_uninstallBtn->setObjectName("dangerAction");

    topRow->addWidget(m_uninstallBtn);

    cardLayout->addLayout(topRow);

    // 描述信息
    m_descLabel = new QLabel(m_info.description, this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setObjectName("secondaryText");
    cardLayout->addWidget(m_descLabel);

    // 下载/解压进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setTextVisible(false);

    m_progressBar->hide();
    cardLayout->addWidget(m_progressBar);

    // 事件连接
    connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
        if (m_info.status == PluginStatus::NotInstalled || m_info.status == PluginStatus::Failed) {
            emit installRequested(m_info.id);
        } else if (m_info.status == PluginStatus::Downloading) {
            emit cancelRequested(m_info.id);
        }
    });

    connect(m_uninstallBtn, &QPushButton::clicked, this, [this]() {
        emit uninstallRequested(m_info.id);
    });
}

void PluginCardWidget::refreshState()
{
    const bool onlineAdapter=LocalPlugin::isOnlineAdapter(m_info.id);
    switch (m_info.status) {
    case PluginStatus::NotInstalled:
        m_statusLabel->setText("未安装");

        m_actionBtn->setText(onlineAdapter?"启用":"一键安装");
        m_actionBtn->setEnabled(true);

        m_uninstallBtn->hide();
        m_progressBar->hide();
        break;

    case PluginStatus::Downloading:
        m_progressBar->setRange(0, 100);
        m_statusLabel->setText(m_info.statusMessage.isEmpty() ? "下载中..." : m_info.statusMessage);

        m_actionBtn->setText("取消下载");
        m_actionBtn->setEnabled(true);

        m_uninstallBtn->hide();
        m_progressBar->show();
        m_progressBar->setValue(m_info.progressPercent);
        break;

    case PluginStatus::Extracting:
        m_statusLabel->setText("安装中…");
        m_statusLabel->setToolTip(m_info.statusMessage);

        m_actionBtn->setText("正在部署...");
        m_actionBtn->setEnabled(false);

        m_uninstallBtn->hide();
        m_progressBar->show();
        m_progressBar->setRange(0, 0);
        break;

    case PluginStatus::Installed:
        m_statusLabel->setText(onlineAdapter?"已启用 · 需配置服务":"已就绪");

        m_actionBtn->setText(onlineAdapter?"已启用":"已安装");
        m_actionBtn->setEnabled(false);

        m_uninstallBtn->show();
        m_progressBar->hide();
        break;

    case PluginStatus::Failed:
        {
            QString errSummary = "安装失败";
            if (m_info.statusMessage.contains("404") || m_info.statusMessage.contains("Not Found", Qt::CaseInsensitive)) {
                errSummary = "失败 (404 资源未上传)";
            } else if (m_info.statusMessage.contains("timeout", Qt::CaseInsensitive) || m_info.statusMessage.contains("timed out", Qt::CaseInsensitive)) {
                errSummary = "失败 (网络超时)";
            } else if (m_info.statusMessage.contains("Connection refused", Qt::CaseInsensitive) || m_info.statusMessage.contains("Host not found", Qt::CaseInsensitive)) {
                errSummary = "失败 (网络无法连通)";
            }
            m_statusLabel->setText(errSummary);
            m_statusLabel->setToolTip(m_info.statusMessage.isEmpty() ? "下载或部署发生错误，请检查网络或检查 GitHub Release 资源是否发布" : m_info.statusMessage);
        }

        m_actionBtn->setText("重试");
        m_actionBtn->setEnabled(true);

        m_uninstallBtn->hide();
        m_progressBar->hide();
        break;
    }
}

void PluginCardWidget::updateInfo(const PluginInfo& info)
{
    m_info = info;
    refreshState();
}

void PluginCardWidget::updateProgress(int percent, qint64 downloaded, qint64 total, double speedMBs)
{
    m_info.progressPercent = percent;
    m_info.downloadedBytes = downloaded;
    m_info.totalBytes = total;
    m_info.speedBytesPerSec = speedMBs;

    double dlMB = downloaded / (1024.0 * 1024.0);
    double totMB = (total > 0 ? total : m_info.sizeBytes) / (1024.0 * 1024.0);

    m_statusLabel->setText(QString("下载中 %1% (%2/%3 MB · %4 MB/s)")
                           .arg(percent)
                           .arg(dlMB, 0, 'f', 1)
                           .arg(totMB, 0, 'f', 1)
                           .arg(speedMBs, 0, 'f', 1));
    m_progressBar->show();
    m_progressBar->setValue(percent);
}

// -------------------------------------------------------------
// PluginMarketDialog: 插件中心主窗口
// -------------------------------------------------------------
PluginMarketDialog::PluginMarketDialog(QWidget* parent, bool embedded)
    : QDialog(parent)
{
    m_embedded = embedded;
    setWindowTitle("插件与模型");
    setWindowIcon(appIcon());
    resize(820, 600);
    if (!embedded) setMinimumSize(800, 520);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint);

    if (embedded) setWindowFlags(Qt::Widget);
    setupUi();

    // 监听 PluginManager 信号
    connect(&PluginManager::instance(), &PluginManager::pluginsRefreshed, this, &PluginMarketDialog::renderPluginList);

    connect(&PluginManager::instance(), &PluginManager::pluginStatusChanged, this, [this](const QString& id, PluginStatus, const QString&) {
        if (m_cardMap.contains(id)) {
            m_cardMap[id]->updateInfo(PluginManager::instance().pluginInfo(id));
            filterPlugins();
        }
    });

    connect(&PluginManager::instance(), &PluginManager::pluginProgressChanged, this, [this](const QString& id, int percent, qint64 downloaded, qint64 total, double speedMBs) {
        if (m_cardMap.contains(id)) {
            m_cardMap[id]->updateProgress(percent, downloaded, total, speedMBs);
        }
    });

    PluginManager::instance().refreshAllStatus();
}

void PluginMarketDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    auto& manager = PluginManager::instance();
    if (manager.pythonRuntimes().isEmpty() && !manager.pythonDetectionRunning()) manager.detectPython();
}

void PluginMarketDialog::setupUi()
{
    setStyleSheet(ThemeManager::instance().getResultDialogStyle());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { setStyleSheet(ThemeManager::instance().getResultDialogStyle()); });

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(12);

    // ==========================================================
    // 顶部 Word 经典菜单布局区 (Command Bar / Ribbon Action Bar)
    // ==========================================================
    auto* topMenuLayout = new QHBoxLayout();
    topMenuLayout->setSpacing(10);

    auto* titleLabel = new QLabel("插件中心", this);
    titleLabel->setObjectName("pageTitle");
    topMenuLayout->addWidget(titleLabel);

    // 经典顶部功能菜单动作按钮组
    m_refreshBtn = new QPushButton("刷新状态", this);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setToolTip("重新检查本地与云端扩展组件状态");

    connect(m_refreshBtn, &QPushButton::clicked, this, []() {
        PluginManager::instance().refreshAllStatus();
    });
    topMenuLayout->addWidget(m_refreshBtn);

    auto* installZipBtn = new QPushButton("导入 RapidOCR 包", this);
    installZipBtn->setCursor(Qt::PointingHandCursor);
    installZipBtn->setToolTip("导入 RapidOCR 离线扩展包（ZIP / 7z）");

    connect(installZipBtn, &QPushButton::clicked, this, [this]() {
        QString zipPath = QFileDialog::getOpenFileName(
            this,
            "选择 RapidOCR 扩展包",
            "",
            "扩展包 (*.zip *.7z)"
        );
        if (!zipPath.isEmpty()) {
            PluginManager::instance().installFromLocalZip(zipPath, "rapidocr-engine");
        }
    });
    topMenuLayout->addWidget(installZipBtn);

    auto* openDirBtn = new QPushButton("打开插件目录", this);
    openDirBtn->setCursor(Qt::PointingHandCursor);
    openDirBtn->setToolTip("在资源管理器中查看本地 plugins/ 插件安装目录");

    connect(openDirBtn, &QPushButton::clicked, this, [this]() {
        QString dir = PluginManager::instance().pluginRootDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    topMenuLayout->addWidget(openDirBtn);

    topMenuLayout->addStretch();

    // 顶部右侧：嵌入式搜索框
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索插件名称、描述或语言...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(180);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PluginMarketDialog::filterPlugins);


    mainLayout->addLayout(topMenuLayout);
    auto* pythonRow = new QHBoxLayout();
    auto* pythonChoice = new QComboBox(this);
    pythonChoice->setObjectName("pluginPythonChoice");
    pythonChoice->setMinimumWidth(200);
    pythonChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pythonChoice->setMinimumContentsLength(24);
    auto* detectPython = new QPushButton("重新检测", this);
    detectPython->setObjectName("detectPluginPython");
    auto* choosePython = new QPushButton("选择 Python", this);
    auto* chooseConda = new QPushButton("选择 Conda", this);
    auto* pythonHelp = new QPushButton("安装 Python 3.12", this);
    pythonHelp->setObjectName("installPluginPython");
    pythonHelp->setToolTip("未找到可用环境时，点击下载官方 Python 3.12 x64 并安装到当前用户；包含 pip，不修改 PATH");
    pythonRow->addWidget(pythonChoice, 1);
    pythonRow->addWidget(detectPython); pythonRow->addWidget(choosePython);
    pythonRow->addWidget(chooseConda); pythonRow->addWidget(pythonHelp);
    mainLayout->addLayout(pythonRow);
    auto* pythonStatus = new QLabel(this);
    pythonStatus->setObjectName("pythonInstallStatus");
    pythonStatus->setWordWrap(true);
    mainLayout->addWidget(pythonStatus);
    auto& manager = PluginManager::instance();
    const auto updatePython = [pythonChoice, detectPython, choosePython, chooseConda, pythonHelp, pythonStatus] {
        auto& mgr = PluginManager::instance();
        const QSignalBlocker blocker(pythonChoice);
        pythonChoice->clear();
        for (const auto& runtime : mgr.pythonRuntimes()) {
            pythonChoice->addItem(runtime.label() + " · " + QDir::toNativeSeparators(runtime.executable));
            pythonChoice->setItemData(pythonChoice->count()-1, runtime.executable, Qt::ToolTipRole);
        }
        pythonChoice->setPlaceholderText(mgr.pythonDetectionRunning() ? "正在检测 Python…" : "请选择 Python 3.12（64 位）");
        pythonChoice->setCurrentIndex(mgr.selectedPython());
        pythonChoice->setToolTip(mgr.pythonMessage());
        const bool busy = mgr.pythonDetectionRunning() || mgr.pythonInstallRunning();
        for (auto* button : {detectPython, choosePython, chooseConda}) button->setEnabled(!busy);
        pythonChoice->setEnabled(!busy);
        pythonHelp->setEnabled(!busy && mgr.pythonRuntimes().isEmpty());
        pythonHelp->setText(mgr.pythonInstallRunning() ? "正在安装 Python…" : "安装 Python 3.12");
        pythonStatus->setText(mgr.pythonInstallMessage());
        if (pythonStatus->text().isEmpty() && !busy && mgr.pythonRuntimes().isEmpty())
            pythonStatus->setText("未找到 Python 3.12（64 位），可选择已有环境或点击安装 Python 3.12。");
        pythonStatus->setVisible(!pythonStatus->text().isEmpty());
    };
    connect(&manager, &PluginManager::pythonRuntimesChanged, this, updatePython);
    connect(pythonChoice, &QComboBox::activated, &manager, &PluginManager::selectPython);
    connect(detectPython, &QPushButton::clicked, this, [&manager] { manager.detectPython(); });
    connect(choosePython, &QPushButton::clicked, this, [this, &manager] {
        const auto path = QFileDialog::getOpenFileName(this, "选择 Python 3.12（64 位）", {}, "Python (python.exe)");
        if (!path.isEmpty()) manager.detectPython(path);
    });
    connect(chooseConda, &QPushButton::clicked, this, [this, &manager] {
        const auto path = QFileDialog::getExistingDirectory(this, "选择 Conda 安装目录");
        if (!path.isEmpty()) manager.detectPython(path);
    });
    connect(pythonHelp, &QPushButton::clicked, &manager, &PluginManager::installPythonRuntime);
    updatePython();
    mainLayout->addWidget(m_searchEdit);

    // Word 风格细分割线
    auto* ribbonSeparator = new QFrame(this);
    ribbonSeparator->setFrameShape(QFrame::NoFrame);
    ribbonSeparator->setFixedHeight(0);

    mainLayout->addWidget(ribbonSeparator);

    // ==========================================
    // 第二行：分类切换标签页 (Ribbon Tabs)
    // ==========================================
    auto* filterSegment = new QWidget(this);
    filterSegment->setObjectName("segmentedControl");
    auto* filterLayout = new QHBoxLayout(filterSegment);
    filterLayout->setContentsMargins(4,4,4,4);
    filterLayout->setSpacing(4);

    auto* categoryGroup=new QButtonGroup(this);
    const QList<QPair<QString,QString>> categories={
        {"All","全部插件"},{"文字识别","文字识别"},{"离线翻译","离线翻译"},
        {"公式识别","公式识别"},{"Markdown","Markdown"},{"Installed","已安装"}};
    for(int index=0;index<categories.size();++index) {
        const auto [key,label]=categories.at(index);
        auto* btn=new QPushButton(label,filterSegment);
        btn->setCheckable(true);
        btn->setProperty("segmented",true);
        btn->setProperty("pluginCategory",key);
        btn->setChecked(key==m_currentCategory);
        categoryGroup->addButton(btn);
        filterLayout->addWidget(btn);
        connect(btn,&QPushButton::clicked,this,[this,key] {
            m_currentCategory=key;
            filterPlugins();
        });
    }

    mainLayout->addWidget(filterSegment,0,Qt::AlignLeft);

    // ==========================================
    // 中间区域：插件卡片工作区
    // ==========================================
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);

    m_cardsContainer = new QWidget();

    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 4, 8, 4);
    m_cardsLayout->setSpacing(10);

    m_scrollArea->setWidget(m_cardsContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    renderPluginList();

    // ==========================================
    // 底部栏：极简状态与关闭
    // ==========================================
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);

    auto* hintLabel = new QLabel("提示: 扩展包下载或导入后将自动解压热加载，无需重启软件", this);
    hintLabel->setObjectName("secondaryText");
    hintLabel->setWordWrap(true);

    bottomLayout->addWidget(hintLabel);

    bottomLayout->addStretch();

    auto* closeBtn = new QPushButton("关闭", this);
    closeBtn->setCursor(Qt::PointingHandCursor);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(closeBtn);
    closeBtn->setVisible(!m_embedded);
    if(m_embedded) {auto* note=new QLabel("安装与卸载立即生效，不受设置的保存／取消影响。",this);note->setWordWrap(true);bottomLayout->insertWidget(0,note,1); }

    mainLayout->addLayout(bottomLayout);
    auto icons = [this,installZipBtn,openDirBtn,closeBtn] {
        const auto color = ThemeManager::instance().palette().text;
        m_refreshBtn->setIcon(uiIcon("plugin",color));
        installZipBtn->setIcon(uiIcon("plugin",color));
        openDirBtn->setIcon(uiIcon("layers",color));
        closeBtn->setIcon(uiIcon("close",color));
    };
    icons();
    connect(&ThemeManager::instance(),&ThemeManager::themeChanged,this,icons);
}

void PluginMarketDialog::renderPluginList()
{
    // 清除既有卡片
    m_cardMap.clear();
    QLayoutItem* item;
    while ((item = m_cardsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    const auto& list = PluginManager::instance().plugins();
    for (const auto& p : list) {
        auto* card = new PluginCardWidget(p, m_cardsContainer);
        connect(card, &PluginCardWidget::installRequested, this, [](const QString& id) {
            PluginManager::instance().installPlugin(id);
        });
        connect(card, &PluginCardWidget::cancelRequested, this, [](const QString& id) {
            PluginManager::instance().cancelDownload(id);
        });
        connect(card, &PluginCardWidget::uninstallRequested, this, [this](const QString& id) {
            auto reply = QMessageBox::question(
                this,
                "确认卸载插件",
                "确定要卸载该扩展插件吗？卸载后将自动删除本地模型文件并释放磁盘空间。",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );
            if (reply == QMessageBox::Yes) {
                PluginManager::instance().uninstallPlugin(id);
            }
        });

        m_cardsLayout->addWidget(card);
        m_cardMap[p.id] = card;
    }

    m_cardsLayout->addStretch();
    filterPlugins();
}

void PluginMarketDialog::filterPlugins()
{
    QString query = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : "";
    const auto& list = PluginManager::instance().plugins();

    for (const auto& p : list) {
        if (!m_cardMap.contains(p.id)) continue;
        auto* card = m_cardMap[p.id];

        const bool matchCategory = m_currentCategory == "All"
            || (m_currentCategory == "Installed" ? p.status == PluginStatus::Installed
                : m_currentCategory == "Markdown" ? p.category.startsWith("Markdown")
                : p.category == m_currentCategory);

        bool matchQuery = true;
        if (!query.isEmpty()) {
            matchQuery = p.name.toLower().contains(query) ||
                         p.description.toLower().contains(query) ||
                         p.category.toLower().contains(query);
        }

        card->setVisible(matchCategory && matchQuery);
    }
}
