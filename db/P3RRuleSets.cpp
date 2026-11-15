#include "db/P3RRuleSets.hpp"
#include "main/NekoGui.hpp"
#include "main/HTTPRequestHelper.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace NekoGui {

    QString P3R_RuleSetDownloadDir() {
        return QDir::current().absoluteFilePath("rule-set"); // the working directory is the config folder
    }

    QString P3R_RuleSetPath(const QString &tag) {
        QFileInfo downloaded(P3R_RuleSetDownloadDir() + "/" + tag + ".srs");
        if (downloaded.exists() && downloaded.size() > 0) return downloaded.absoluteFilePath();
        return FindCoreAsset("rule-set/" + tag + ".srs");
    }

    void P3R_RuleSetsNoteUsed(const QStringList &tagsIn) {
        auto tags = tagsIn;
        tags.sort();
        tags.removeDuplicates();
        if (tags.isEmpty() || tags == dataStore->p3r_rulesets_used) return;
        dataStore->p3r_rulesets_used = tags;
        dataStore->Save();
    }

    bool P3R_RuleSetLooksValid(const QByteArray &data) {
        if (data.size() < 16 || data.size() > 32 * 1024 * 1024) return false;
        if (!data.startsWith("SRS")) return false;
        auto version = static_cast<quint8>(data[3]);
        if (version < 1 || version > 5) return false;
        // zlib stream header: CM = 8 (deflate), (CMF * 256 + FLG) divisible by 31
        auto cmf = static_cast<quint8>(data[4]);
        auto flg = static_cast<quint8>(data[5]);
        return (cmf & 0x0f) == 8 && ((cmf << 8) | flg) % 31 == 0;
    }

    static QStringList P3R_RuleSetUrls(const QString &tag) {
        QString repo = tag.startsWith("geoip-") ? "sing-geoip" : "sing-geosite";
        auto file = QString(QUrl::toPercentEncoding(tag + ".srs", "-._"));
        return {
            "https://raw.githubusercontent.com/SagerNet/" + repo + "/rule-set/" + file,
            "https://cdn.jsdelivr.net/gh/SagerNet/" + repo + "@rule-set/" + file,
        };
    }

    P3R_RuleSetUpdateResult P3R_RuleSetsUpdate() {
        P3R_RuleSetUpdateResult result;
        QDir().mkpath(P3R_RuleSetDownloadDir());
        auto tags = dataStore->p3r_rulesets_used;
        for (const auto &tag: tags) {
            if (!(tag.startsWith("geosite-") || tag.startsWith("geoip-")) || tag.contains("/") || tag.contains("\\") || tag.contains("..")) continue;
            QByteArray data;
            QString lastError;
            for (const auto &url: P3R_RuleSetUrls(tag)) {
                auto response = NekoGui_network::NetworkRequestHelper::HttpGet(QUrl(url));
                if (!response.error.isEmpty()) {
                    lastError = response.error;
                    continue;
                }
                if (!P3R_RuleSetLooksValid(response.data)) {
                    lastError = "not a sing-box rule-set";
                    continue;
                }
                data = response.data;
                break;
            }
            if (data.isEmpty()) {
                result.failed << tag + ": " + lastError;
                continue;
            }
            auto path = P3R_RuleSetDownloadDir() + "/" + tag + ".srs";
            auto currentPath = QFileInfo::exists(path) ? path : P3R_RuleSetPath(tag);
            QFile current(currentPath);
            if (!currentPath.isEmpty() && current.open(QIODevice::ReadOnly)) {
                auto same = QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha256) == QCryptographicHash::hash(data, QCryptographicHash::Sha256);
                current.close();
                if (same && QFileInfo::exists(path)) {
                    result.unchanged++;
                    continue;
                }
            }
            // atomic replace: the core may be reading the old file right now
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
                result.failed << tag + ": " + out.errorString();
                continue;
            }
            result.updated++;
        }
        dataStore->p3r_rulesets_updated = QDateTime::currentSecsSinceEpoch();
        dataStore->Save();
        return result;
    }

} // namespace NekoGui
