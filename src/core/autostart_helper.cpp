#include "autostart_helper.h"
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

static const char* REG_RUN_PATH = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* APP_REG_NAME = "AccuShot";

bool AutoStartHelper::isAutoStartEnabled()
{
#ifdef Q_OS_WIN
    QSettings bootSettings(REG_RUN_PATH, QSettings::NativeFormat);
    return bootSettings.contains(APP_REG_NAME);
#else
    return false;
#endif
}

void AutoStartHelper::setAutoStartEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings bootSettings(REG_RUN_PATH, QSettings::NativeFormat);
    QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString cmdValue = QString("\"%1\"").arg(appPath);

    if (enabled) {
        bootSettings.setValue(APP_REG_NAME, cmdValue);
    } else {
        bootSettings.remove(APP_REG_NAME);
    }
    bootSettings.sync();

    QSettings appConfig(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    appConfig.setValue("General/AutoStart", enabled);
#endif
}

void AutoStartHelper::ensureInitialDefaultSetup()
{
#ifdef Q_OS_WIN
    QSettings appConfig(QSettings::defaultFormat(), QSettings::UserScope, "AccuShot", "AccuShot");
    if (!appConfig.contains("General/AutoStartInitialized")) {
        // 初次运行默认开启开机自启动
        setAutoStartEnabled(true);
        appConfig.setValue("General/AutoStartInitialized", true);
    } else {
        // 如果用户已配置，根据用户配置校验同步注册表状态
        bool shouldEnable = appConfig.value("General/AutoStart", true).toBool();
        QSettings bootSettings(REG_RUN_PATH, QSettings::NativeFormat);
        const QString currentCommand = QString("\"%1\"").arg(
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        // Preserve the preference while following the renamed/moved executable.
        if (shouldEnable ? bootSettings.value(APP_REG_NAME).toString() != currentCommand
                         : bootSettings.contains(APP_REG_NAME)) {
            setAutoStartEnabled(shouldEnable);
        }
    }
#endif
}
