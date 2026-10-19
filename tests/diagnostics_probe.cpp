#include "core/diagnostics.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDebug>
#include <windows.h>
#include <dbghelp.h>
#include <QFile>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--snapshot") {
        const DWORD pid = args[2].toULong();
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) return 2;
        HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(args[3].utf16()), GENERIC_WRITE,
                                  FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        const BOOL saved = file != INVALID_HANDLE_VALUE && MiniDumpWriteDump(process, pid, file,
            MiniDumpWithThreadInfo, nullptr, nullptr, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        CloseHandle(process);
        return saved ? 0 : 3;
    }
    QCoreApplication::setOrganizationName("AccuShot");
    QCoreApplication::setApplicationName("DiagnosticsProbe");
    QStandardPaths::setTestModeEnabled(true);
    Diagnostics::initialize();
    qInfo() << "[Probe] diagnostic file writing works";
    if (app.arguments().contains("--crash")) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX);
        RaiseException(0xE0421001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    return 0;
}
