// NekoBoxP3R: run as administrator without a UAC prompt every time.
// A Windows Task Scheduler task "NekoBoxP3R" with "highest privileges" is created once (one UAC prompt).
// A normal (non-admin) start then relaunches itself through that task.
#pragma once

#include <QStringList>

namespace P3R {
    bool ElevatedTaskExists();
    bool ElevatedTaskCreate(); // asks UAC once
    bool ElevatedTaskDelete(); // asks UAC once
    // Runs the task (elevated, no prompt); args are handed over through a file in the config dir.
    bool ElevatedTaskRun(const QStringList &args);
    // In the instance started by the task: read and remove the handed-over args.
    QStringList ElevatedTaskTakeArgs();

    // Run a console program without a window; returns the exit code or -1.
    int RunHidden(const QString &program, const QStringList &args, int timeoutMs = 20000);

    // ---- Kill switch (Windows Firewall, needs administrator rights) ----
    // Block: allow only nekobox_core.exe outbound, block everything else.
    bool KillSwitchBlock(const QString &corePath);
    // Unblock: restore the default outbound policy (allow) and remove the rule.
    bool KillSwitchUnblock();
    // True while the block is in place (marker file in the config dir).
    bool KillSwitchActive();

    // Names of running programs (e.g. "Discord.exe"), sorted, without duplicates.
    QStringList RunningProcessNames();
} // namespace P3R
