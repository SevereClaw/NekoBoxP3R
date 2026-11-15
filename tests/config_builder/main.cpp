// Test program for the GUI config generator: config_builder_test <output dir>
// Writes one sing-box config per scenario (<name>.json). Scenarios cover the settings NekoBoxP3R exposes.
#include "db/ConfigBuilder.hpp"
#include "db/Database.hpp"
#include "fmt/includes.h"
#include "main/NekoGui.hpp"
#include "db/P3RRuleSets.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <functional>

using namespace NekoGui;

struct Scenario {
    QString name;
    QString link;                          // share link of the server
    std::function<void()> settings;        // changes from the defaults
    bool forTest = false;
};

static void resetSettings() {
    auto ds = dataStore;
    ds->spmode_vpn = true;
    ds->vpn_internal_tun = true;
    ds->vpn_implementation = 2; // mixed
    ds->vpn_mtu = 9000;
    ds->vpn_ipv6 = false;
    ds->vpn_strict_route = false;
    ds->fake_dns = false;
    ds->p3r_killswitch = false;
    ds->p3r_dns_protect = false;
    ds->p3r_ru_direct = true;
    ds->p3r_ai_via_vpn = true;
    ds->p3r_torrent_direct = true;
    ds->p3r_app_mode = 0;
    ds->p3r_app_list = {};
    ds->inbound_socks_port = 2080;
    ds->inbound_address = "127.0.0.1";
    ds->inbound_auth->username = "";
    ds->inbound_auth->password = "";
    ds->core_box_clash_api = -9090;
    ds->core_box_underlying_dns = "";
    ds->mux_concurrency = 8;
    ds->mux_default_on = false;
    ds->log_level = "info";
    ds->vpn_rule_process = "";
    ds->vpn_rule_cidr = "";
    ds->vpn_rule_white = false;
    auto &r = ds->routing;
    r->direct_ip = r->direct_domain = r->proxy_ip = r->proxy_domain = r->block_ip = r->block_domain = "";
    r->def_outbound = "proxy";
    r->custom = "{\"rules\": []}";
    r->remote_dns = "https://dns.google/dns-query";
    r->remote_dns_strategy = "";
    r->direct_dns = "https://common.dot.dns.yandex.net/dns-query";
    r->direct_dns_strategy = "";
    r->dns_routing = true;
    r->use_dns_object = false;
    r->dns_final_out = "proxy";
    r->domain_strategy = "";
    r->outbound_domain_strategy = "";
    r->sniffing_mode = SniffingMode::FOR_ROUTING;
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: config_builder_test <output dir>\n");
        return 2;
    }
    QDir out(QString::fromLocal8Bit(argv[1]));
    out.mkpath(".");
    // the profile manager saves groups and profiles into the working directory
    QDir work(QDir::tempPath() + "/config_builder_test_work");
    work.removeRecursively();
    work.mkpath("groups");
    work.mkpath("profiles");
    work.mkpath("route_profiles");
    QDir::setCurrent(work.path());

    if (dataStore->routing == nullptr) dataStore->routing = std::make_unique<Routing>();

    // config_builder_test --rulesets-update <dir> <tag>...: download fresh rule-sets like the program does
    if (QString(argv[1]) == "--rulesets-update") {
        dataStore->p3r_rulesets_used.clear();
        for (int i = 3; i < argc; i++) dataStore->p3r_rulesets_used << QString::fromLocal8Bit(argv[i]);
        QDir::setCurrent(QString::fromLocal8Bit(argv[2]));
        auto result = P3R_RuleSetsUpdate();
        printf("updated %d unchanged %d failed %d\n", result.updated, result.unchanged, int(result.failed.size()));
        for (const auto &f: result.failed) printf("  %s\n", qPrintable(f));
        for (const auto &tag: dataStore->p3r_rulesets_used) printf("  %s -> %s\n", qPrintable(tag), qPrintable(P3R_RuleSetPath(tag)));
        return result.failed.isEmpty() ? 0 : 1;
    }

    // a group to hold the profiles
    auto group = ProfileManager::NewGroup();
    group->name = "test";
    profileManager->AddGroup(group);

    const QString trojanWs = "trojan://pw@de.example.ru:443?security=tls&sni=de.example.ru&type=ws&path=%2Fws&host=de.example.ru&fp=chrome#Trojan%20DE";
    const QString vlessReality = "vless://11111111-2222-3333-4444-555555555555@de.example.ru:443?security=reality&sni=www.microsoft.com&fp=chrome&pbk=Y2xhdWRlLWtleS1mb3ItdGVzdHMtb25seS0wMDAwMDA&sid=0123abcd&type=tcp&flow=xtls-rprx-vision#VLESS%20DE";
    const QString hy2 = "hysteria2://pw@fi.example.ru:443?sni=fi.example.ru&obfs=salamander&obfs-password=x&insecure=1#Hysteria2%20FI";
    const QString hy2hop = "hysteria2://pw@fi.example.ru:443?mport=20000-30000,40000&sni=fi.example.ru#Hysteria2%20hop";
    const QString ss = "ss://YWVzLTI1Ni1nY206cHc@1.2.3.4:8388#SS";
    const QString vmess = "vmess://eyJ2IjoiMiIsInBzIjoiVk1lc3MiLCJhZGQiOiJ1cy5leGFtcGxlLmNvbSIsInBvcnQiOiI0NDMiLCJpZCI6IjExMTExMTExLTIyMjItMzMzMy00NDQ0LTU1NTU1NTU1NTU1NSIsImFpZCI6IjAiLCJuZXQiOiJ3cyIsInR5cGUiOiJub25lIiwiaG9zdCI6InVzLmV4YW1wbGUuY29tIiwicGF0aCI6Ii93cyIsInRscyI6InRscyJ9";
    const QString tuic = "tuic://11111111-2222-3333-4444-555555555555:pw@us.example.com:443?congestion_control=bbr&alpn=h3&sni=us.example.com#TUIC";
    const QString socks = "socks5://user:pass@10.0.0.1:1080#SOCKS";

    QList<Scenario> scenarios = {
        {"vpn_default", trojanWs, [] {}},
        {"system_proxy_only", trojanWs, [] { dataStore->spmode_vpn = false; }},
        {"vpn_everything_off", trojanWs, [] {
             dataStore->p3r_ru_direct = false;
             dataStore->p3r_ai_via_vpn = false;
             dataStore->p3r_torrent_direct = false;
         }},
        {"apps_direct", trojanWs, [] {
             dataStore->p3r_app_mode = 1;
             dataStore->p3r_app_list = {"Discord.exe", "firefox.exe"};
         }},
        {"apps_only_via_vpn", trojanWs, [] {
             dataStore->p3r_app_mode = 2;
             dataStore->p3r_app_list = {"Discord.exe", "firefox.exe", "chrome.exe"};
         }},
        {"killswitch_ipv6_strict", trojanWs, [] {
             dataStore->p3r_killswitch = true;
             dataStore->vpn_ipv6 = true;
         }},
        {"fake_dns", trojanWs, [] { dataStore->fake_dns = true; }},
        {"user_rules", trojanWs, [] {
             auto &r = dataStore->routing;
             r->direct_domain = "domain:example.org\nfull:direct.example.net\nkeyword:local\nregexp:^cdn[0-9]+\\.test$\ngeosite:apple";
             r->proxy_domain = "domain:blocked-site.com";
             r->block_domain = "domain:ads.example.com";
             r->direct_ip = "10.0.0.0/8\ngeoip:private";
             r->proxy_ip = "203.0.113.0/24";
             r->block_ip = "198.51.100.1/32";
         }},
        {"dns_variants", trojanWs, [] {
             auto &r = dataStore->routing;
             r->remote_dns = "tls://1.1.1.1";
             r->direct_dns = "77.88.8.8";
             r->remote_dns_strategy = "ipv4_only";
             r->direct_dns_strategy = "prefer_ipv4";
             r->dns_final_out = "bypass";
             r->domain_strategy = "prefer_ipv4";
             r->outbound_domain_strategy = "ipv4_only";
         }},
        {"dns_local_quic_h3", trojanWs, [] {
             auto &r = dataStore->routing;
             r->remote_dns = "h3://dns.google/dns-query";
             r->direct_dns = "local";
             dataStore->core_box_underlying_dns = "underlying://0.0.0.0";
         }},
        {"no_sniff_auth_clash", trojanWs, [] {
             dataStore->routing->sniffing_mode = SniffingMode::DISABLE;
             dataStore->inbound_auth->username = "u";
             dataStore->inbound_auth->password = "p";
             dataStore->core_box_clash_api = 9090;
         }},
        {"final_bypass_tun_rules", trojanWs, [] {
             dataStore->routing->def_outbound = "bypass";
             dataStore->vpn_rule_process = "game.exe\nlauncher.exe";
             dataStore->vpn_rule_cidr = "192.0.2.0/24";
         }},
        {"vless_reality", vlessReality, [] {}},
        {"hysteria2", hy2, [] {}},
        {"hysteria2_hop", hy2hop, [] {}},
        {"shadowsocks", ss, [] { dataStore->mux_default_on = true; }},
        {"vmess_ws_mux", vmess, [] { dataStore->mux_default_on = true; }},
        {"tuic", tuic, [] {}},
        {"socks", socks, [] {}},
        {"test_trojan", trojanWs, [] {}, true},
        {"test_vless", vlessReality, [] {}, true},
        {"test_hysteria2_hop", hy2hop, [] {}, true},
    };

    int failures = 0;

    // the config of the external VPN process (TUN outside the main core); the template comes from res/vpn
    for (bool variant: {false, true}) {
        resetSettings();
        dataStore->vpn_internal_tun = false;
        if (variant) {
            dataStore->vpn_ipv6 = true;
            dataStore->fake_dns = true;
            dataStore->vpn_rule_white = true;
            dataStore->vpn_rule_process = "game.exe";
            dataStore->vpn_rule_cidr = "192.0.2.0/24";
            dataStore->inbound_auth->username = "u";
            dataStore->inbound_auth->password = "p";
            dataStore->core_box_underlying_dns = "tls://dns.example.org";
        }
        QDir().mkpath("vpn");
        QFile::remove("vpn/sing-box-vpn.json");
        QFile::copy(QString::fromLocal8Bit(NKR_SOURCE_DIR) + "/res/vpn/sing-box-vpn.json", "vpn/sing-box-vpn.json");
        auto written = WriteVPNSingBoxConfig();
        auto name = QString(variant ? "external_vpn_options" : "external_vpn_default") + ".json";
        QFile::remove(out.filePath(name));
        if (!QFile::copy(written, out.filePath(name))) failures++;
        printf("%s\n", qPrintable(name));
    }

    for (const auto &sc: scenarios) {
        resetSettings();
        sc.settings();
        QString error;
        auto scheme = sc.link.section("://", 0, 0);
        QString type = scheme == "socks5" ? "socks" : scheme == "ss" ? "shadowsocks" : scheme;
        auto ent = ProfileManager::NewProxyEntity(type);
        bool ok = false;
        if (type == "trojan" || type == "vless") ok = ent->TrojanVLESSBean()->TryParseLink(sc.link);
        else if (type == "hysteria2" || type == "tuic") ok = ent->QUICBean()->TryParseLink(sc.link);
        else if (type == "shadowsocks") ok = ent->ShadowSocksBean()->TryParseLink(sc.link);
        else if (type == "vmess") ok = ent->VMessBean()->TryParseLink(sc.link);
        else if (type == "socks") ok = ent->SocksHTTPBean()->TryParseLink(sc.link);
        if (!ok) {
            fprintf(stderr, "%s: cannot parse the link\n", qPrintable(sc.name));
            failures++;
            continue;
        }
        profileManager->AddProfile(ent, group->id);
        auto result = BuildConfig(ent, sc.forTest, false);
        if (!result->error.isEmpty()) {
            fprintf(stderr, "%s: %s\n", qPrintable(sc.name), qPrintable(result->error));
            failures++;
            continue;
        }
        QFile f(out.filePath(sc.name + ".json"));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(QJsonDocument(result->coreConfig).toJson(QJsonDocument::Indented));
        f.close();
        printf("%s\n", qPrintable(sc.name));
    }
    return failures == 0 ? 0 : 1;
}
