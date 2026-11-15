// NekoBoxP3R: geosite / geoip rule-sets (lists of sites and IP addresses used by the routing rules).
// The program ships them in <program>/rule-set. Fresh copies are downloaded into <config>/rule-set
// between releases; the newest readable copy is used.
#pragma once

#include <QString>
#include <QStringList>

namespace NekoGui {
    // absolute path of the list "geosite-<code>" / "geoip-<code>", the downloaded copy first; empty if there is none
    QString P3R_RuleSetPath(const QString &tag);

    // the lists the routing rules use now (written by the config builder, read by the updater)
    void P3R_RuleSetsNoteUsed(const QStringList &tags);

    // checks a downloaded file: sing-box rule-set header ("SRS", version, zlib stream) and a sane size
    bool P3R_RuleSetLooksValid(const QByteArray &data);

    struct P3R_RuleSetUpdateResult {
        int updated = 0;   // files replaced by newer ones
        int unchanged = 0; // already up to date
        QStringList failed;
    };

    // downloads fresh copies of the used lists (blocking: call it from a worker thread)
    P3R_RuleSetUpdateResult P3R_RuleSetsUpdate();

    // the folder for downloaded copies
    QString P3R_RuleSetDownloadDir();
} // namespace NekoGui
