#include "diagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QDebug>
#include <mutex>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
QFile logFile;
std::mutex logMutex;
#ifdef Q_OS_WIN
std::wstring dumpPath, crashPath;
LONG WINAPI recordCrash(EXCEPTION_POINTERS* exception)
{
    // Avoid Qt and the logging mutex on a damaged process's exception path.
    HANDLE report = CreateFileW(crashPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    char details[256];
    int length = sprintf_s(details, "exception=0x%08lX address=%p thread=%lu\r\n",
                           exception->ExceptionRecord->ExceptionCode,
                           exception->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    DWORD written;
    if (report != INVALID_HANDLE_VALUE) WriteFile(report, details, length, &written, nullptr);
    HANDLE dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL ok = FALSE;
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                              MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(dump);
    }
    if (report != INVALID_HANDLE_VALUE) {
        length = sprintf_s(details, "minidump_written=%d\r\n", int(ok));
        WriteFile(report, details, length, &written, nullptr);
        CloseHandle(report);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void writeMessage(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    const char* level = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO" :
                        type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "ERROR" : "FATAL";
    const QByteArray line = QString("%1 [%2] [thread=%3] %4\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QString::fromLatin1(level))
        .arg(quintptr(QThread::currentThreadId()), 0, 16).arg(message).toUtf8();
    std::lock_guard<std::mutex> lock(logMutex);
    logFile.write(line);
    logFile.flush();
}
}

void Diagnostics::initialize()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    if (!QDir().mkpath(directory)) return;
    const QString base = directory + "/accushot-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") +
                         "-" + QString::number(QCoreApplication::applicationPid());
    logFile.setFileName(base + ".log");
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) return;
#ifdef Q_OS_WIN
    dumpPath = (base + ".dmp").toStdWString();
    crashPath = (base + ".crash.txt").toStdWString();
    SetUnhandledExceptionFilter(recordCrash);
#endif
    qInstallMessageHandler(writeMessage);
    qInfo() << "[Startup]" << QCoreApplication::applicationFilePath()
            << "build=" << __DATE__ << __TIME__ << "Qt=" << qVersion()
            << "binaryModified=" << QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toString(Qt::ISODateWithMs)
            << "pid=" << QCoreApplication::applicationPid() << "log=" << logFile.fileName();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] {
        qInfo() << "[Shutdown] normal exit";
    });
}
