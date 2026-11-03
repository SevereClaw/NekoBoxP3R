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
} // namespace P3R
