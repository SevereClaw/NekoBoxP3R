#include "db/ConfigBuilder.hpp"
#include "db/Database.hpp"
#include "fmt/includes.h"
#include "fmt/Preset.hpp"
#include "db/P3RRuleSets.hpp"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#define BOX_UNDERLYING_DNS dataStore->core_box_underlying_dns.isEmpty() ? "local" : dataStore->core_box_underlying_dns

namespace NekoGui {

    static bool P3R_HasGeosite(const QString &code) {
        return !P3R_RuleSetPath("geosite-" + code).isEmpty();
    }

    // NekoBoxP3R: AI services (blocked for Russian IPs) always go through the proxy
    static QStringList P3R_AiDomains() {
        QStringList domains = {
            "domain:gemini.google.com", "domain:aistudio.google.com", "domain:ai.google.dev",
            "domain:generativelanguage.googleapis.com", "domain:alkalimakersuite-pa.clients6.google.com",
            "domain:notebooklm.google.com", "domain:notebooklm.google", "domain:labs.google",
            "domain:bard.google.com", "domain:deepmind.google", "domain:gemini.google",
            "domain:proactivebackend-pa.googleapis.com", "domain:robinfrontend-pa.googleapis.com",
            "domain:openai.com", "domain:chatgpt.com", "domain:oaistatic.com", "domain:oaiusercontent.com",
            "domain:anthropic.com", "domain:claude.ai", "domain:claude.com", "domain:perplexity.ai",
            "domain:copilot.microsoft.com", "domain:grok.com", "domain:x.ai",
            // Google must never see the real Russian IP during sign-in: its .ru domains would otherwise
            // go directly because of "Russian sites directly", and Google would tie the session to Russia
            "domain:google.ru", "domain:google.com.ru", "domain:accounts.google.com", "domain:myaccount.google.com",
            "domain:gstatic.com", "domain:googleusercontent.com", "domain:play.google.com", "domain:ogs.google.com",
        };
        // whole Google (+ YouTube): one country for every Google request, otherwise Gemini fails with "Something went wrong"
        for (const auto &code: {"google-gemini", "google-deepmind", "google", "youtube", "openai", "anthropic", "perplexity", "category-ai-chat-!cn"}) {
            if (P3R_HasGeosite(code)) domains << QString("geosite:") + code;
        }
        return domains;
    }

    // NekoBoxP3R: "Russian sites directly" rule lists
    static void P3R_AddRussianDirect(const std::shared_ptr<BuildConfigStatus> &status) {
        QStringList domains = {
            "domain:ru", "domain:su", "domain:xn--p1ai", // .ru .su .рф
            "domain:xn--d1acj3b", "domain:xn--80adxhks", // .дети .москва
            "domain:moscow", "domain:tatar",
        };
        for (const auto &code: {"category-ru", "category-gov-ru", "yandex", "vk", "mailru"}) {
            if (P3R_HasGeosite(code)) domains << QString("geosite:") + code;
        }
        for (const auto &d: domains) {
            if (dataStore->routing->dns_routing) status->domainListDNSDirect += d;
            status->domainListDirect += d;
        }
        status->ipListDirect += "geoip:ru";
    }

    QStringList getAutoBypassExternalProcessPaths(const std::shared_ptr<BuildConfigResult> &result) {
        QStringList paths;
        for (const auto &extR: result->extRs) {
            auto path = extR->program;
            if (path.trimmed().isEmpty()) continue;
            paths << path.replace("\\", "/");
        }
        return paths;
    }

    QString genTunName() {
        auto tun_name = "neko-tun";
#ifdef Q_OS_MACOS
        tun_name = "utun9";
#endif
        return tun_name;
    }

    void MergeJson(QJsonObject &dst, const QJsonObject &src) {
        // 合并
        if (src.isEmpty()) return;
        for (const auto &key: src.keys()) {
            auto v_src = src[key];
            if (dst.contains(key)) {
                auto v_dst = dst[key];
                if (v_src.isObject() && v_dst.isObject()) { // isObject 则合并？
                    auto v_src_obj = v_src.toObject();
                    auto v_dst_obj = v_dst.toObject();
                    MergeJson(v_dst_obj, v_src_obj);
                    dst[key] = v_dst_obj;
                } else {
                    dst[key] = v_src;
                }
            } else if (v_src.isArray()) {
                if (key.startsWith("+")) {
                    auto key2 = SubStrAfter(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_src_arr, v_dst_arr)
                    dst[key2] = v_src_arr;
                } else if (key.endsWith("+")) {
                    auto key2 = SubStrBefore(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_dst_arr, v_src_arr)
                    dst[key2] = v_dst_arr;
                } else {
                    dst[key] = v_src;
                }
            } else {
                dst[key] = v_src;
            }
        }
    }

    // Common

    std::shared_ptr<BuildConfigResult> BuildConfig(const std::shared_ptr<ProxyEntity> &ent, bool forTest, bool forExport) {
        auto result = std::make_shared<BuildConfigResult>();
        auto status = std::make_shared<BuildConfigStatus>();
        status->ent = ent;
        status->result = result;
        status->forTest = forTest;
        status->forExport = forExport;

        auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
        if (customBean != nullptr && customBean->core == "internal-full") {
            result->coreConfig = QString2QJsonObject(customBean->config_simple);
        } else {
            BuildConfigSingBox(status);
        }

        // apply custom config
        MergeJson(result->coreConfig, QString2QJsonObject(ent->bean->custom_config));

        return result;
    }

    QString BuildChain(int chainId, const std::shared_ptr<BuildConfigStatus> &status) {
        auto group = profileManager->GetGroup(status->ent->gid);
        if (group == nullptr) {
            status->result->error = QStringLiteral("This profile is not in any group, your data may be corrupted.");
            return {};
        }

        auto resolveChain = [=](const std::shared_ptr<ProxyEntity> &ent) {
            QList<std::shared_ptr<ProxyEntity>> resolved;
            if (ent->type == "chain") {
                auto list = ent->ChainBean()->list;
                std::reverse(std::begin(list), std::end(list));
                for (auto id: list) {
                    resolved += profileManager->GetProfile(id);
                    if (resolved.last() == nullptr) {
                        status->result->error = QStringLiteral("chain missing ent: %1").arg(id);
                        break;
                    }
                    if (resolved.last()->type == "chain") {
                        status->result->error = QStringLiteral("chain in chain is not allowed: %1").arg(id);
                        break;
                    }
                }
            } else {
                resolved += ent;
            };
            return resolved;
        };

        // Make list
        auto ents = resolveChain(status->ent);
        if (!status->result->error.isEmpty()) return {};

        if (group->front_proxy_id >= 0) {
            auto fEnt = profileManager->GetProfile(group->front_proxy_id);
            if (fEnt == nullptr) {
                status->result->error = QStringLiteral("front proxy ent not found.");
                return {};
            }
            ents += resolveChain(fEnt);
            if (!status->result->error.isEmpty()) return {};
        }

        // BuildChain
        QString chainTagOut = BuildChainInternal(0, ents, status);

        // Chain ent traffic stat
        if (ents.length() > 1) {
            status->ent->traffic_data->id = status->ent->id;
            status->ent->traffic_data->tag = chainTagOut.toStdString();
            status->result->outboundStats += status->ent->traffic_data;
        }

        return chainTagOut;
    }

#define DOMAIN_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_domain)) {  \
        if (dataStore->routing->dns_routing) status->domainListDNSRemote += line;    \
        status->domainListRemote += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_domain)) { \
        if (dataStore->routing->dns_routing) status->domainListDNSDirect += line;    \
        status->domainListDirect += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_domain)) {  \
        status->domainListBlock += line;                                             \
    }

#define IP_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_ip)) {  \
        status->ipListBlock += line;                                             \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_ip)) {  \
        status->ipListRemote += line;                                            \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_ip)) { \
        status->ipListDirect += line;                                            \
    }

    QString BuildChainInternal(int chainId, const QList<std::shared_ptr<ProxyEntity>> &ents,
                               const std::shared_ptr<BuildConfigStatus> &status) {
        QString chainTag = "c-" + Int2String(chainId);
        QString chainTagOut;
        bool muxApplied = false;

        QString pastTag;
        int pastExternalStat = 0;
        int index = 0;

        for (const auto &ent: ents) {
            // tagOut: v2ray outbound tag for a profile
            // profile2 (in) (global)   tag g-(id)
            // profile1                 tag (chainTag)-(id)
            // profile0 (out)           tag (chainTag)-(id) / single: chainTag=g-(id)
            auto tagOut = chainTag + "-" + Int2String(ent->id);

            // needGlobal: can only contain one?
            bool needGlobal = false;

            // first profile set as global
            auto isFirstProfile = index == ents.length() - 1;
            if (isFirstProfile) {
                needGlobal = true;
                tagOut = "g-" + Int2String(ent->id);
            }

            // last profile set as "proxy"
            if (chainId == 0 && index == 0) {
                needGlobal = false;
                tagOut = "proxy";
            }

            // ignoreConnTag
            if (index != 0) {
                status->result->ignoreConnTag << tagOut;
            }

            if (needGlobal) {
                if (status->globalProfiles.contains(ent->id)) {
                    continue;
                }
                status->globalProfiles += ent->id;
            }

            if (index > 0) {
                // chain rules: past
                if (pastExternalStat == 0) {
                    auto replaced = status->outbounds.last().toObject();
                    replaced["detour"] = tagOut;
                    status->outbounds.removeLast();
                    status->outbounds += replaced;
                } else {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{pastTag + "-mapping"}},
                        {"outbound", tagOut},
                    };
                }
            } else {
                // index == 0 means last profile in chain / not chain
                chainTagOut = tagOut;
                status->result->outboundStat = ent->traffic_data;
            }

            // chain rules: this
            auto ext_mapping_port = 0;
            auto ext_socks_port = 0;
            auto thisExternalStat = ent->bean->NeedExternal(isFirstProfile);
            if (thisExternalStat < 0) {
                status->result->error = "This configuration cannot be set automatically, please try another.";
                return {};
            }

            // determine port
            if (thisExternalStat > 0) {
                if (ent->type == "custom") {
                    auto bean = ent->CustomBean();
                    if (IsValidPort(bean->mapping_port)) {
                        ext_mapping_port = bean->mapping_port;
                    } else {
                        ext_mapping_port = MkPort();
                    }
                    if (IsValidPort(bean->socks_port)) {
                        ext_socks_port = bean->socks_port;
                    } else {
                        ext_socks_port = MkPort();
                    }
                } else {
                    ext_mapping_port = MkPort();
                    ext_socks_port = MkPort();
                }
            }
            if (thisExternalStat == 2) dataStore->need_keep_vpn_off = true;
            if (thisExternalStat == 1) {
                // mapping
                status->inbounds += QJsonObject{
                    {"type", "direct"},
                    {"tag", tagOut + "-mapping"},
                    {"listen", "127.0.0.1"},
                    {"listen_port", ext_mapping_port},
                    {"override_address", ent->bean->serverAddress},
                    {"override_port", ent->bean->serverPort},
                };
                // no chain rule and not outbound, so need to set to direct
                if (isFirstProfile) {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{tagOut + "-mapping"}},
                        {"outbound", "direct"},
                    };
                }
            }

            // Outbound

            QJsonObject outbound;
            auto stream = GetStreamSettings(ent->bean.get());

            if (thisExternalStat > 0) {
                auto extR = ent->bean->BuildExternal(ext_mapping_port, ext_socks_port, thisExternalStat);
                if (extR.program.isEmpty()) {
                    status->result->error = QObject::tr("Core not found: %1").arg(ent->bean->DisplayCoreType());
                    return {};
                }
                if (!extR.error.isEmpty()) { // rejected
                    status->result->error = extR.error;
                    return {};
                }
                extR.tag = ent->bean->DisplayType();
                status->result->extRs.emplace_back(std::make_shared<NekoGui_fmt::ExternalBuildResult>(extR));

                // SOCKS OUTBOUND
                outbound["type"] = "socks";
                outbound["server"] = "127.0.0.1";
                outbound["server_port"] = ext_socks_port;
            } else {
                const auto coreR = ent->bean->BuildCoreObjSingBox();
                if (coreR.outbound.isEmpty()) {
                    status->result->error = "unsupported outbound";
                    return {};
                }
                if (!coreR.error.isEmpty()) { // rejected
                    status->result->error = coreR.error;
                    return {};
                }
                outbound = coreR.outbound;
            }

            // outbound misc
            outbound["tag"] = tagOut;
            ent->traffic_data->id = ent->id;
            ent->traffic_data->tag = tagOut.toStdString();
            status->result->outboundStats += ent->traffic_data;

            // mux common
            auto needMux = ent->type == "vmess" || ent->type == "trojan" || ent->type == "vless";
            needMux &= dataStore->mux_concurrency > 0;

            if (stream != nullptr) {
                if (stream->network == "grpc" || stream->network == "quic" || (stream->network == "http" && stream->security == "tls")) {
                    needMux = false;
                }
                if (stream->multiplex_status == 0) {
                    if (!dataStore->mux_default_on) needMux = false;
                } else if (stream->multiplex_status == 1) {
                    needMux = true;
                } else if (stream->multiplex_status == 2) {
                    needMux = false;
                }
            }
            if (ent->type == "vless" && outbound["flow"] != "") {
                needMux = false;
            }

            // common
            // (domain strategy of the outbounds is route.default_domain_resolver.strategy since sing-box 1.12)
            // apply mux
            if (!muxApplied && needMux) {
                auto muxObj = QJsonObject{
                    {"enabled", true},
                    {"protocol", dataStore->mux_protocol},
                    {"padding", dataStore->mux_padding},
                    {"max_streams", dataStore->mux_concurrency},
                };
                outbound["multiplex"] = muxObj;
                muxApplied = true;
            }

            // apply custom outbound settings
            MergeJson(outbound, QString2QJsonObject(ent->bean->custom_outbound));

            // Bypass Lookup for the first profile
            auto serverAddress = ent->bean->serverAddress;

            auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
            if (customBean != nullptr && customBean->core == "internal") {
                auto server = QString2QJsonObject(customBean->config_simple)["server"].toString();
                if (!server.isEmpty()) serverAddress = server;
            }

            if (!IsIpAddress(serverAddress)) {
                status->domainListDNSDirect += "full:" + serverAddress;
            }

            status->outbounds += outbound;
            pastTag = tagOut;
            pastExternalStat = thisExternalStat;
            index++;
        }

        return chainTagOut;
    }

    // SingBox
    //
    // NekoBoxP3R: the config is written in the format of current sing-box (1.12+): rule actions instead of the
    // block / dns outbounds and the sniff / domain_strategy inbound fields, typed DNS servers, rule-sets instead
    // of geoip.db / geosite.db, a default domain resolver, the TUN "address" list. The core still converts old
    // JSON that comes from user settings (custom rules, custom inbounds, a custom DNS object, custom configs).

    static bool P3R_IsDomainStrategy(const QString &s) {
        return s == "prefer_ipv4" || s == "prefer_ipv6" || s == "ipv4_only" || s == "ipv6_only";
    }

    // One DNS server from the address syntax of the settings ("https://dns.google/dns-query", "tls://1.1.1.1",
    // "77.88.8.8", "local", "dhcp://auto"...). localTag resolves the name of the server itself.
    static QJsonObject P3R_DnsServer(const QString &tag, const QString &addressIn, const QString &detour, const QString &localTag) {
        QJsonObject server{{"tag", tag}};
        auto address = addressIn.trimmed();
        auto lower = address.toLower();
        if (lower.isEmpty() || lower == "local" || lower.startsWith("underlying://")) {
            server["type"] = "local";
            return server;
        }
        if (lower.startsWith("dhcp://")) {
            server["type"] = "dhcp";
            auto iface = address.mid(7);
            if (!iface.isEmpty() && iface != "auto") server["interface"] = iface;
            return server;
        }
        QString scheme = "udp";
        QString rest = address;
        auto sep = address.indexOf("://");
        if (sep > 0) {
            scheme = lower.left(sep);
            rest = address.mid(sep + 3);
        }
        QString host, path;
        int port = 0;
        if (scheme == "https" || scheme == "h3") {
            QUrl url(address);
            host = url.host();
            port = url.port(0);
            if (!url.path().isEmpty() && url.path() != "/dns-query") path = url.path();
        } else {
            auto cut = rest.indexOf(QRegularExpression("[/?]"));
            if (cut >= 0) rest = rest.left(cut);
            if (rest.startsWith("[")) { // [IPv6]:port
                auto close = rest.indexOf("]");
                host = rest.mid(1, close - 1);
                if (rest.mid(close + 1).startsWith(":")) port = rest.mid(close + 2).toInt();
            } else if (rest.count(':') == 1) {
                host = rest.section(':', 0, 0);
                port = rest.section(':', 1, 1).toInt();
            } else {
                host = rest; // a name, an IPv4 address or a bare IPv6 address
            }
        }
        if (!QStringList{"udp", "tcp", "tls", "quic", "https", "h3"}.contains(scheme)) scheme = "udp";
        server["type"] = scheme;
        server["server"] = host;
        if (port > 0) server["server_port"] = port;
        if (!path.isEmpty()) server["path"] = path;
        if (!IsIpAddress(host)) server["domain_resolver"] = QJsonObject{{"server", localTag}};
        if (!detour.isEmpty() && detour != "direct") server["detour"] = detour;
        return server;
    }

    void BuildConfigSingBox(const std::shared_ptr<BuildConfigStatus> &status) {
        const bool tun = dataStore->vpn_internal_tun && dataStore->spmode_vpn && !status->forTest;
        const bool fakeDns = dataStore->fake_dns && tun;
        const bool sniff = dataStore->routing->sniffing_mode != SniffingMode::DISABLE;
        QJsonArray prefixRules; // sniff / resolve actions, before every other route rule

        // Log
        status->result->coreConfig["log"] = QJsonObject{{"level", dataStore->log_level}};

        // Inbounds
        auto inboundActions = [&](const QString &tag) {
            if (sniff) prefixRules += QJsonObject{{"inbound", QJsonArray{tag}}, {"action", "sniff"}};
        };
        QStringList resolveInbounds;

        // mixed-in
        if (IsValidPort(dataStore->inbound_socks_port) && !status->forTest) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "mixed-in";
            inboundObj["type"] = "mixed";
            inboundObj["listen"] = dataStore->inbound_address;
            inboundObj["listen_port"] = dataStore->inbound_socks_port;
            if (dataStore->inbound_auth->NeedAuth()) {
                inboundObj["users"] = QJsonArray{
                    QJsonObject{
                        {"username", dataStore->inbound_auth->username},
                        {"password", dataStore->inbound_auth->password},
                    },
                };
            }
            status->inbounds += inboundObj;
            inboundActions("mixed-in");
            resolveInbounds << "mixed-in";
        }

        // tun-in
        if (tun) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "tun-in";
            inboundObj["type"] = "tun";
            inboundObj["interface_name"] = genTunName();
            inboundObj["auto_route"] = true;
            inboundObj["mtu"] = dataStore->vpn_mtu;
            inboundObj["stack"] = Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation);
            inboundObj["strict_route"] = dataStore->vpn_strict_route || dataStore->p3r_killswitch || dataStore->p3r_dns_protect; // NekoBoxP3R
            QJsonArray address{"172.19.0.1/28"};
            if (dataStore->vpn_ipv6) address += "fdfe:dcba:9876::1/126";
            inboundObj["address"] = address;
            status->inbounds += inboundObj;
            inboundActions("tun-in");
            resolveInbounds << "tun-in";
        }

        // "resolve the domain before routing" of the inbounds
        if (P3R_IsDomainStrategy(dataStore->routing->domain_strategy)) {
            for (const auto &tag: resolveInbounds) {
                prefixRules += QJsonObject{{"inbound", QJsonArray{tag}}, {"action", "resolve"}, {"strategy", dataStore->routing->domain_strategy}};
            }
        }

        // Outbounds
        auto tagProxy = BuildChain(0, status);
        if (!status->result->error.isEmpty()) return;

        // direct & bypass (blocking and DNS are rule actions now)
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "direct"},
        };
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "bypass"},
        };

        // custom inbound
        if (!status->forTest) QJSONARRAY_ADD(status->inbounds, QString2QJsonObject(dataStore->custom_inbound)["inbounds"].toArray())

        status->result->coreConfig.insert("inbounds", status->inbounds);
        status->result->coreConfig.insert("outbounds", status->outbounds);

        // user rule
        if (!status->forTest) {
            DOMAIN_USER_RULE
            IP_USER_RULE
            if (dataStore->p3r_ai_via_vpn && dataStore->routing->dns_routing) status->domainListDNSRemote += P3R_AiDomains(); // DNS via proxy too
            if (dataStore->p3r_ru_direct) P3R_AddRussianDirect(status);
        }

        // rule-sets used by the rules (geosite:xxx, geoip:xx)
        QStringList ruleSetOrder;
        QSet<QString> missingRuleSets;
        auto useRuleSet = [&](const QString &kind, const QString &codeIn) -> QString {
            auto tag = kind + "-" + codeIn.trimmed().toLower();
            if (ruleSetOrder.contains(tag)) return tag;
            if (!P3R_RuleSetPath(tag).isEmpty()) {
                ruleSetOrder << tag;
                return tag;
            }
            if (!missingRuleSets.contains(tag)) {
                missingRuleSets << tag;
                if (MW_show_log) MW_show_log("[Warning] " + QObject::tr("%1:%2 is not available, this rule item is skipped").arg(kind, codeIn));
            }
            return {};
        };

        // sing-box common rule object; empty if nothing is left to match on
        auto make_rule = [&](const QStringList &list, bool isIP = false) {
            QJsonObject rule;
            QJsonArray ip_cidr, domain_keyword, domain_subdomain, domain_regexp, domain_full, ruleSets;
            bool privateIp = false;
            for (auto item: list) {
                if (isIP) {
                    if (item.startsWith("geoip:")) {
                        auto code = item.mid(6);
                        if (code.trimmed().toLower() == "private") {
                            privateIp = true;
                        } else if (auto tag = useRuleSet("geoip", code); !tag.isEmpty()) {
                            ruleSets += tag;
                        }
                    } else {
                        ip_cidr += item;
                    }
                } else {
                    // https://www.v2fly.org/config/dns.html#dnsobject
                    if (item.startsWith("geosite:")) {
                        if (auto tag = useRuleSet("geosite", item.mid(8)); !tag.isEmpty()) ruleSets += tag;
                    } else if (item.startsWith("full:")) {
                        domain_full += item.replace("full:", "").toLower();
                    } else if (item.startsWith("domain:")) {
                        domain_subdomain += item.replace("domain:", "").toLower();
                    } else if (item.startsWith("regexp:")) {
                        domain_regexp += item.replace("regexp:", "").toLower();
                    } else if (item.startsWith("keyword:")) {
                        domain_keyword += item.replace("keyword:", "").toLower();
                    } else {
                        domain_subdomain += item.toLower();
                    }
                }
            }
            auto put = [&](const char *key, const QJsonArray &arr) {
                if (!arr.isEmpty()) rule[key] = arr;
            };
            if (isIP) {
                put("ip_cidr", ip_cidr);
                if (privateIp) rule["ip_is_private"] = true;
            } else {
                put("domain", domain_full);
                put("domain_suffix", domain_subdomain); // v2ray Subdomain => sing-box suffix
                put("domain_keyword", domain_keyword);
                put("domain_regex", domain_regexp);
            }
            put("rule_set", ruleSets);
            return rule;
        };

        // final add DNS
        QJsonObject dns;
        QJsonArray dnsServers;
        QJsonArray dnsRules;
        const QString localTag = "dns-local";

        // Remote
        if (!status->forTest) dnsServers += P3R_DnsServer("dns-remote", dataStore->routing->remote_dns, tagProxy, localTag);

        // Direct
        auto directObj = P3R_DnsServer("dns-direct", dataStore->routing->direct_dns, "direct", localTag);
        if (dataStore->routing->dns_final_out == "bypass") {
            dnsServers.prepend(directObj);
        } else {
            dnsServers.append(directObj);
        }

        // Fake IP
        if (fakeDns) {
            dnsServers += QJsonObject{
                {"tag", "dns-fake"},
                {"type", "fakeip"},
                {"inet4_range", "198.18.0.0/15"},
                {"inet6_range", "fc00::/18"},
            };
        }

        // Underlying 100% Working DNS: resolves the names of the other DNS servers
        auto localObj = P3R_DnsServer(localTag, BOX_UNDERLYING_DNS, "direct", "dns-local-auto");
        dnsServers += localObj;
        if (localObj.contains("domain_resolver")) dnsServers += QJsonObject{{"tag", "dns-local-auto"}, {"type", "local"}};

        // one strategy for all servers: the per-server strategy is gone
        QString dnsStrategy;
        auto remoteStrategy = status->forTest ? QString() : dataStore->routing->remote_dns_strategy;
        auto directStrategy = dataStore->routing->direct_dns_strategy;
        if (dataStore->routing->dns_final_out == "bypass") {
            dnsStrategy = P3R_IsDomainStrategy(directStrategy) ? directStrategy : remoteStrategy;
        } else {
            dnsStrategy = P3R_IsDomainStrategy(remoteStrategy) ? remoteStrategy : directStrategy;
        }

        // sing-box dns rule object
        auto add_rule_dns = [&](const QStringList &list, const QString &server) {
            auto rule = make_rule(list, false);
            if (rule.isEmpty()) return;
            rule["server"] = server;
            dnsRules += rule;
        };
        add_rule_dns(status->domainListDNSRemote, "dns-remote");
        add_rule_dns(status->domainListDNSDirect, "dns-direct");

        // built-in rules: answer "nothing" for SRV/TXT-like 32/33 queries and local names
        if (!status->forTest) {
            dnsRules += QJsonObject{
                {"query_type", QJsonArray{32, 33}},
                {"action", "predefined"},
                {"rcode", "NOERROR"},
            };
            dnsRules += QJsonObject{
                {"domain_suffix", ".lan"},
                {"action", "predefined"},
                {"rcode", "NOERROR"},
            };
        }

        // fakedns rule
        if (fakeDns) {
            dnsRules += QJsonObject{
                {"inbound", "tun-in"},
                {"query_type", QJsonArray{"A", "AAAA"}},
                {"server", "dns-fake"},
            };
        }

        dns["servers"] = dnsServers;
        dns["rules"] = dnsRules;
        if (P3R_IsDomainStrategy(dnsStrategy)) dns["strategy"] = dnsStrategy;

        bool customDns = dataStore->routing->use_dns_object;
        if (customDns) {
            dns = QString2QJsonObject(dataStore->routing->dns_object); // may be in the old format: the core converts it
        }
        status->result->coreConfig.insert("dns", dns);

        // Routing

        // dns hijack
        if (!status->forTest) {
            status->routingRules += QJsonObject{
                {"protocol", "dns"},
                {"action", "hijack-dns"},
            };
        }

        // sing-box routing rule object
        auto add_rule_route = [&](const QStringList &list, bool isIP, const QString &out) {
            auto rule = make_rule(list, isIP);
            if (rule.isEmpty()) return;
            if (out == "block") {
                rule["action"] = "reject";
            } else {
                rule["outbound"] = out;
            }
            status->routingRules += rule;
        };

        // NekoBoxP3R: torrents go through the normal connection: VPN servers usually forbid or throttle them,
        // and peers would see the server address. The client programs by name, plus any BitTorrent traffic
        // the core recognises (other clients, peer connections, DHT / uTP).
        if (!status->forTest && dataStore->p3r_torrent_direct) {
            status->routingRules += QJsonObject{
                {"process_name", QJsonArray{
                                     "qbittorrent.exe", "qBittorrent.exe", "uTorrent.exe", "utorrent.exe", "utweb.exe",
                                     "BitTorrent.exe", "bittorrent.exe", "btweb.exe", "BitComet.exe", "bitcomet.exe",
                                     "transmission-qt.exe", "transmission-gtk.exe", "transmission-daemon.exe", "Transmission.exe",
                                     "tixati.exe", "Tixati.exe", "deluge.exe", "deluged.exe", "deluge-gtk.exe", "deluge-console.exe",
                                     "BiglyBT.exe", "Vuze.exe", "Azureus.exe", "PicoTorrent.exe", "picotorrent.exe",
                                     "MediaGet.exe", "mediaget.exe", "FrostWire.exe", "WebTorrent.exe", "Motrix.exe",
                                     "qbittorrent", "transmission-daemon", "deluged"}},
                {"outbound", "bypass"},
            };
            status->routingRules += QJsonObject{
                {"protocol", "bittorrent"},
                {"outbound", "bypass"},
            };
        }

        // NekoBoxP3R: AI services always via proxy - the very first rule, beats per-app and Russian-direct rules
        if (!status->forTest && dataStore->p3r_ai_via_vpn) add_rule_route(P3R_AiDomains(), false, tagProxy);

        // NekoBoxP3R: per-app rules
        if (!status->forTest && dataStore->p3r_app_mode != 0 && !dataStore->p3r_app_list.isEmpty()) {
            status->routingRules += QJsonObject{
                {"process_name", QList2QJsonArray(dataStore->p3r_app_list)},
                {"outbound", dataStore->p3r_app_mode == 1 ? QString("bypass") : tagProxy},
            };
        }

        // final add user rule
        add_rule_route(status->domainListBlock, false, "block");
        add_rule_route(status->domainListRemote, false, tagProxy);
        add_rule_route(status->domainListDirect, false, "bypass");
        add_rule_route(status->ipListBlock, true, "block");
        add_rule_route(status->ipListRemote, true, tagProxy);
        add_rule_route(status->ipListDirect, true, "bypass");

        // built-in rules
        status->routingRules += QJsonObject{
            {"network", "udp"},
            {"port", QJsonArray{135, 137, 138, 139, 5353}},
            {"action", "reject"},
        };
        status->routingRules += QJsonObject{
            {"ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"action", "reject"},
        };
        status->routingRules += QJsonObject{
            {"source_ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"action", "reject"},
        };

        // tun user rule
        if (tun) {
            auto match_out = dataStore->vpn_rule_white ? "proxy" : "bypass";

            QString process_name_rule = dataStore->vpn_rule_process.trimmed();
            if (!process_name_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(process_name_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"process_name", QList2QJsonArray(arr)}};
                status->routingRules += rule;
            }

            QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
            if (!cidr_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(cidr_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"ip_cidr", QList2QJsonArray(arr)}};
                status->routingRules += rule;
            }

            auto autoBypassExternalProcessPaths = getAutoBypassExternalProcessPaths(status->result);
            if (!autoBypassExternalProcessPaths.isEmpty()) {
                QJsonObject rule{{"outbound", "bypass"},
                                 {"process_name", QList2QJsonArray(autoBypassExternalProcessPaths)}};
                status->routingRules += rule;
            }
        }

        // final add routing rule
        auto routingRules = prefixRules;
        if (!status->forTest) QJSONARRAY_ADD(routingRules, QString2QJsonObject(dataStore->routing->custom)["rules"].toArray())
        if (!status->forTest) QJSONARRAY_ADD(routingRules, QString2QJsonObject(dataStore->custom_route_global)["rules"].toArray())
        QJSONARRAY_ADD(routingRules, status->routingRules)

        QString finalOut = status->forTest ? QString() : dataStore->routing->def_outbound;
        // NekoBoxP3R: "only these apps via proxy" -> everything else goes direct
        if (!status->forTest && dataStore->p3r_app_mode == 2 && !dataStore->p3r_app_list.isEmpty()) finalOut = "bypass";
        if (finalOut == "block") {
            routingRules += QJsonObject{{"action", "reject"}};
            finalOut.clear();
        }

        auto routeObj = QJsonObject{
            {"rules", routingRules},
            {"auto_detect_interface", dataStore->spmode_vpn},
        };
        if (!finalOut.isEmpty()) routeObj["final"] = finalOut;
        if (!ruleSetOrder.isEmpty()) {
            QJsonArray ruleSets;
            for (const auto &tag: ruleSetOrder) {
                ruleSets += QJsonObject{{"tag", tag}, {"type", "local"}, {"format", "binary"}, {"path", P3R_RuleSetPath(tag)}};
            }
            routeObj["rule_set"] = ruleSets;
        }
        if (!status->forTest && !status->forExport) P3R_RuleSetsNoteUsed(ruleSetOrder);
        // addresses of the proxy servers are resolved by the direct DNS (before: the {"outbound": "any"} DNS rule)
        if (!customDns) {
            QJsonObject resolver{{"server", "dns-direct"}};
            if (P3R_IsDomainStrategy(dataStore->routing->outbound_domain_strategy)) resolver["strategy"] = dataStore->routing->outbound_domain_strategy;
            routeObj["default_domain_resolver"] = resolver;
        }
        if (status->forExport) {
            routeObj.remove("auto_detect_interface");
        }
        status->result->coreConfig.insert("route", routeObj);

        // experimental
        QJsonObject experimentalObj;

        if (!status->forTest && dataStore->core_box_clash_api > 0) {
            QJsonObject clash_api = {
                {"external_controller", "127.0.0.1:" + Int2String(dataStore->core_box_clash_api)},
                {"secret", dataStore->core_box_clash_api_secret},
                {"external_ui", "dashboard"},
            };
            experimentalObj["clash_api"] = clash_api;
        }

        if (!experimentalObj.isEmpty()) status->result->coreConfig.insert("experimental", experimentalObj);
    }

    QString WriteVPNSingBoxConfig() {
        // tun user rule
        auto match_out = dataStore->vpn_rule_white ? "neko-socks" : "direct";
        auto no_match_out = dataStore->vpn_rule_white ? "direct" : "neko-socks";

        QString process_name_rule = dataStore->vpn_rule_process.trimmed();
        if (!process_name_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(process_name_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"process_name", QList2QJsonArray(arr)}};
            process_name_rule = "," + QJsonObject2QString(rule, false);
        }

        QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
        if (!cidr_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(cidr_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"ip_cidr", QList2QJsonArray(arr)}};
            cidr_rule = "," + QJsonObject2QString(rule, false);
        }

        // TODO bypass ext core process path?

        // auth
        QString socks_user_pass;
        if (dataStore->inbound_auth->NeedAuth()) {
            socks_user_pass = R"( "username": "%1", "password": "%2", )";
            socks_user_pass = socks_user_pass.arg(dataStore->inbound_auth->username, dataStore->inbound_auth->password);
        }
        // gen config
        auto configFn = ":/neko/vpn/sing-box-vpn.json";
        if (QFile::exists("vpn/sing-box-vpn.json")) configFn = "vpn/sing-box-vpn.json";
        auto config = ReadFileText(configFn)
                          .replace("//%IPV6_ADDRESS%", dataStore->vpn_ipv6 ? R"(, "fdfe:dcba:9876::1/126")" : "")
                          .replace("%DNS_DIRECT_SERVER%", QJsonObject2QString(P3R_DnsServer("dns-direct", BOX_UNDERLYING_DNS, "direct", "dns-local"), true))
                          .replace("//%SOCKS_USER_PASS%", socks_user_pass)
                          .replace("//%PROCESS_NAME_RULE%", process_name_rule)
                          .replace("//%CIDR_RULE%", cidr_rule)
                          .replace("%MTU%", Int2String(dataStore->vpn_mtu))
                          .replace("%STACK%", Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation))
                          .replace("%TUN_NAME%", genTunName())
                          .replace("%STRICT_ROUTE%", (dataStore->vpn_strict_route || dataStore->p3r_killswitch || dataStore->p3r_dns_protect) ? "true" : "false")
                          .replace("%FINAL_OUT%", no_match_out)
                          .replace("%FAKE_DNS_INBOUND%", dataStore->fake_dns ? "tun-in" : "empty")
                          .replace("%PORT%", Int2String(dataStore->inbound_socks_port));
        // write config
        QFile file;
        file.setFileName(QFileInfo(configFn).fileName());
        file.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file.write(config.toUtf8());
        file.close();
        return QFileInfo(file).absoluteFilePath();
    }

    QString WriteVPNLinuxScript(const QString &configPath) {
#ifdef Q_OS_WIN
        return {};
#endif
        // gen script
        auto scriptFn = ":/neko/vpn/vpn-run-root.sh";
        if (QFile::exists("vpn/vpn-run-root.sh")) scriptFn = "vpn/vpn-run-root.sh";
        auto script = ReadFileText(scriptFn)
                          .replace("./nekobox_core", QApplication::applicationDirPath() + "/nekobox_core")
                          .replace("$CONFIG_PATH", configPath);
        // write script
        QFile file2;
        file2.setFileName(QFileInfo(scriptFn).fileName());
        file2.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file2.write(script.toUtf8());
        file2.close();
        return QFileInfo(file2).absoluteFilePath();
    }

} // namespace NekoGui
