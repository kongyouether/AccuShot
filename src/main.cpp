#include <QApplication>
#include <QDebug>
#include <QKeySequence>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QSettings>
#include <QFile>
#include <QClipboard>
#include <QPainter>
#include "ui/toolbar/toolbar_icons.h"
#include <QFont>
#include "core/hotkey_config.h"
#include "core/theme_manager.h"
#include "core/diagnostics.h"
#include "core/autostart_helper.h"
#include "platform/hotkey_manager.h"
#include "ui/overlay/snipping_overlay.h"
#include "ui/pin/pin_manager.h"
#include "ui/settings/hotkey_settings_dialog.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 声明 Windows Per-Monitor DPI Aware V2，彻底消除系统二次位图拉伸造成的模糊
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 单实例互斥锁，禁止重复点击启动多个后台进程
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"AccuShot_SingleInstance_Mutex_Unique");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        MessageBoxW(nullptr,
            L"AccuShot 已经在后台运行中！\n可在任务栏右下角托盘图标中操作，或使用预设快捷键截屏。",
            L"AccuShot",
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }
#endif

    // 启用高 DPI 缩放支持与整数缩放策略
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    app.setApplicationName("AccuShot");
    app.setApplicationDisplayName("AccuShot");
    app.setOrganizationName("AccuShot");
    Diagnostics::initialize();
    app.setQuitOnLastWindowClosed(false);

    // 4K / 高 DPI 屏幕全真抗锯齿字体渲染策略 (消除文字与边缘锯齿感)
    QFont appFont;
    appFont.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
    appFont.setPixelSize(14);
    appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    app.setFont(appFont);
    const auto updateApplicationStyle = [&app] {
        app.setStyleSheet(ThemeManager::instance().getApplicationStyle());
    };
    updateApplicationStyle();
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, &app, updateApplicationStyle);

    QIcon appIcon = ::appIcon();
    app.setWindowIcon(appIcon);

    // 确保初次启动默认开启开机自启动
    AutoStartHelper::ensureInitialDefaultSetup();

    // 初始化快捷键配置
    HotkeyConfig::instance().load();
    const auto& currentConfig = HotkeyConfig::instance().data();

    // 创建系统托盘图标
    QSystemTrayIcon trayIcon(appIcon, &app);
    trayIcon.setIcon(appIcon);
    trayIcon.setVisible(true);

    QMenu trayMenu;
    trayMenu.setObjectName("trayMenu");
    auto updateTrayStyle = [&trayMenu]() {
        trayMenu.setStyleSheet(ThemeManager::instance().getTrayMenuStyle());
    };
    updateTrayStyle();
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, updateTrayStyle);

    auto* snipAction = trayMenu.addAction("开始截屏");
    auto* pinAction = trayMenu.addAction("桌面贴图");
    trayMenu.addSeparator();
    auto* settingsAction = trayMenu.addAction("设置…");
    trayMenu.addSeparator();
    auto* quitAction = trayMenu.addAction("退出 AccuShot");
    const auto updateTrayIcons = [=] {
        const auto color=ThemeManager::instance().palette().text;
        snipAction->setIcon(uiIcon("capture",color));
        pinAction->setIcon(uiIcon("pin",color));
        settingsAction->setIcon(uiIcon("settings",color));
        quitAction->setIcon(uiIcon("close",color));
    };
    updateTrayIcons();
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, &trayMenu, updateTrayIcons);


    auto doClipboardPin = [&trayIcon]() {
        if (SnippingOverlay::instance().isVisible() && SnippingOverlay::instance().hasValidSelection()) {
            SnippingOverlay::instance().triggerPinAction();
            return;
        }

        QClipboard* clipboard = QApplication::clipboard();
        QPixmap pix = clipboard->pixmap();
        if (!pix.isNull() && pix.width() > 0 && pix.height() > 0) {
            QPoint cursorPos = QCursor::pos();
            int w = static_cast<int>(pix.width() / pix.devicePixelRatio());
            int h = static_cast<int>(pix.height() / pix.devicePixelRatio());
            QRect targetRect(cursorPos.x() - w / 2, cursorPos.y() - h / 2, w, h);
            PinManager::instance().createPin(pix, targetRect);
        } else {
            trayIcon.showMessage("贴图提示", "剪贴板中未检测到有效图像数据，请先截图或复制图片。", QSystemTrayIcon::Information, 2000);
        }
    };

    QObject::connect(snipAction, &QAction::triggered, []() {
        SnippingOverlay::instance().startSnipping();
    });

    QObject::connect(pinAction, &QAction::triggered, doClipboardPin);

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    trayIcon.setContextMenu(&trayMenu);

    // 单击托盘图标直接触发截图
    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, [](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            SnippingOverlay::instance().startSnipping();
        }
    });

    trayIcon.show();

    // 动态全局热键管理变量
    static int g_snipHotkeyId = 0;
    static int g_pinHotkeyId = 0;

    auto registerAllGlobalHotkeys = [&]() {
        const auto& c = HotkeyConfig::instance().data();

        // 1. 全局截屏热键
        if (g_snipHotkeyId != 0) {
            HotkeyManager::instance().unregisterHotkey(g_snipHotkeyId);
            g_snipHotkeyId = 0;
        }
        if (!c.globalSnipping.trimmed().isEmpty()) {
            g_snipHotkeyId = HotkeyManager::instance().registerHotkey(QKeySequence(c.globalSnipping), []() {
                qDebug() << "[AccuShot] 捕获全局热键唤醒截屏";
                SnippingOverlay::instance().startSnipping();
            });
            if (g_snipHotkeyId == 0) {
                qWarning() << "[AccuShot] 全局截屏热键注册失败:" << c.globalSnipping;
            }
        }

        // 2. 全局贴图热键
        if (g_pinHotkeyId != 0) {
            HotkeyManager::instance().unregisterHotkey(g_pinHotkeyId);
            g_pinHotkeyId = 0;
        }
        if (!c.globalPin.trimmed().isEmpty()) {
            g_pinHotkeyId = HotkeyManager::instance().registerHotkey(QKeySequence(c.globalPin), doClipboardPin);
            if (g_pinHotkeyId == 0) {
                qWarning() << "[AccuShot] 全局贴图热键注册失败:" << c.globalPin;
            }
        }

        // 更新托盘与动作提示
        snipAction->setText(c.globalSnipping.isEmpty() ? "开始截屏" : QString("开始截屏\t%1").arg(QKeySequence(c.globalSnipping).toString(QKeySequence::NativeText)));
        pinAction->setText(c.globalPin.isEmpty() ? "桌面贴图" : QString("桌面贴图\t%1").arg(QKeySequence(c.globalPin).toString(QKeySequence::NativeText)));
        trayIcon.setToolTip(QString("AccuShot\n截屏: %1 | 贴图: %2").arg(
            c.globalSnipping.isEmpty() ? "未设置" : c.globalSnipping,
            c.globalPin.isEmpty() ? "未设置" : c.globalPin
        ));
    };

    // 初始注册全局热键
    registerAllGlobalHotkeys();

    // 监听快捷键配置变动
    QObject::connect(&HotkeyConfig::instance(), &HotkeyConfig::configChanged, [&]() {
        registerAllGlobalHotkeys();
    });

    // 快捷键设置对话框联动
    QObject::connect(settingsAction, &QAction::triggered, [&]() {
        HotkeySettingsDialog dlg;
        dlg.exec();
    });

    // 弹出启动气泡提示
    trayIcon.showMessage("AccuShot 已启动",
        QString("截屏快捷键: %1，贴图快捷键: %2\n可在托盘右键个性化自定义快捷键。")
        .arg(currentConfig.globalSnipping, currentConfig.globalPin),
        QSystemTrayIcon::Information, 3000);

    qDebug() << "[AccuShot] 应用程序初始化成功，当前截屏快捷键:" << currentConfig.globalSnipping
             << "贴图快捷键:" << currentConfig.globalPin;

    // 监听截图完成事件
    QObject::connect(&SnippingOverlay::instance(), &SnippingOverlay::snippingFinished,
                     [](const QPixmap& pixmap, const QRect& region) {
        qDebug() << "[AccuShot] 截图完成，已写入系统剪贴板! 选区:" << region
                 << "尺寸:" << pixmap.size();
    });

    // 监听贴图请求事件 (将选区及其标注转为独立置顶 Pin 窗口)
    QObject::connect(&SnippingOverlay::instance(), &SnippingOverlay::pinRequested,
                     [](const QPixmap& pixmap, const QRect& screenPos) {
        qDebug() << "[AccuShot] 收到贴图请求，创建独立 PinWindow 置顶贴图，物理位置:" << screenPos;
        PinManager::instance().createPin(pixmap, screenPos);
    });

    // 监听截图取消事件
    QObject::connect(&SnippingOverlay::instance(), &SnippingOverlay::snippingCancelled, []() {
        qDebug() << "[AccuShot] 截图已取消";
    });

    int ret = app.exec();

#ifdef Q_OS_WIN
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
#endif

    return ret;
}
