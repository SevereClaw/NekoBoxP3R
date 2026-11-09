#pragma once

#include "Const.hpp"
#include "NekoGui_Utils.hpp"
#include "NekoGui_ConfigItem.hpp"
#include "NekoGui_DataStore.hpp"

// Switch core support

namespace NekoGui {
    inline int coreType = CoreType::SING_BOX;

    QString FindCoreAsset(const QString &name);

    QString FindNekoBoxCoreRealPath();

    bool IsAdmin();

    // NekoBoxP3R: short journal of important events (connect, disconnect, core crash, failover).
    // Kept in memory and in config/p3r_events.log, so a problem report shows what happened
    // even when the core traffic log has already scrolled away.
    void P3R_Event(const QString &text);
    QStringList P3R_Events(int last = 80);

    // NekoBoxP3R: replace the upstream Chinese "direct DNS" defaults with Yandex DoH.
    // Returns true if the value was changed.
    bool P3R_MigrateDirectDns(Routing *routing);
} // namespace NekoGui

#define ROUTES_PREFIX_NAME QStringLiteral("routes_box")
#define ROUTES_PREFIX QString(ROUTES_PREFIX_NAME + "/")
