// NekoBoxP3R: run as administrator without a UAC prompt every time.
#include "P3RElevate.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProcess>

#ifdef Q_OS_WIN
// WinCommander first: windows.h defines SW_HIDE etc. as macros
#include "3rdparty/WinCommander.hpp"
#include <windows.h>
#endif

namespace P3R {

    static const char *kTaskName = "NekoBoxP3R";
    static const char *kArgsFile = "p3r_elevate_args.txt"; // relative to the config dir (current dir)

#ifdef Q_OS_WIN
    static int runHidden(const QString &program, const QStringList &args, int timeoutMs = 20000) {
        QProcess p;
        p.setProgram(program);
        p.setArguments(args);
        p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
            a->flags |= CREATE_NO_WINDOW;
        });
        p.start();
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            return -1;
        }
        return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    }

    static QString psQuote(QString s) {
        return "'" + s.replace("'", "''") + "'";
    }
#endif

    bool ElevatedTaskExists() {
#ifdef Q_OS_WIN
        return runHidden("schtasks", {"/Query", "/TN", kTaskName}) == 0;
#else
        return false;
#endif
    }

    bool ElevatedTaskCreate() {
#ifdef Q_OS_WIN
        auto exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
        auto dir = QDir::toNativeSeparators(QApplication::applicationDirPath());
        auto user = qEnvironmentVariable("USERDOMAIN") + "\\" + qEnvironmentVariable("USERNAME");
        QString script =
            "$ErrorActionPreference='Stop';"
            "$a=New-ScheduledTaskAction -Execute " + psQuote(exe) + " -Argument '-p3r_from_task' -WorkingDirectory " + psQuote(dir) + ";"
            "$p=New-ScheduledTaskPrincipal -UserId " + psQuote(user) + " -LogonType Interactive -RunLevel Highest;"
            "$s=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew;"
            "Register-ScheduledTask -TaskName " + psQuote(kTaskName) + " -Action $a -Principal $p -Settings $s -Force | Out-Null";
        // -EncodedCommand takes base64 of UTF-16LE: no quoting problems with paths
        QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), script.size() * 2);
        WinCommander::runProcessElevated("powershell.exe",
                                         {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                          "-WindowStyle", "Hidden", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())},
                                         "", 0 /* hidden */, true);
        return ElevatedTaskExists();
#else
        return false;
#endif
    }

    bool ElevatedTaskDelete() {
#ifdef Q_OS_WIN
        if (!ElevatedTaskExists()) return true;
        WinCommander::runProcessElevated("schtasks", {"/Delete", "/TN", kTaskName, "/F"}, "", 0 /* hidden */, true);
        return !ElevatedTaskExists();
#else
        return true;
#endif
    }

    bool ElevatedTaskRun(const QStringList &args) {
#ifdef Q_OS_WIN
        QFile f(kArgsFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(args.join('\n').toUtf8());
            f.close();
        }
        if (runHidden("schtasks", {"/Run", "/TN", kTaskName}) == 0) return true;
        QFile::remove(kArgsFile);
        return false;
#else
        Q_UNUSED(args)
        return false;
#endif
    }

    QStringList ElevatedTaskTakeArgs() {
        QFile f(kArgsFile);
        if (!f.open(QIODevice::ReadOnly)) return {};
        auto list = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        f.close();
        f.remove();
        return list;
    }

} // namespace P3R
