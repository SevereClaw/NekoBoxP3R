#pragma once

#include <QMainWindow>
#include "ui/p3r/P3RWidgets.hpp"
#include <QPushButton>
#include <QCheckBox>
#include <QAction>

#include "main/NekoGui.hpp"

#ifndef MW_INTERFACE

#include <QTime>
#include <QTableWidgetItem>
#include <QKeyEvent>
#include <QSystemTrayIcon>
#include <QProcess>
#include <QTextDocument>
#include <QShortcut>
#include <QSemaphore>
#include <QMutex>

#include "GroupSort.hpp"

#include "db/ProxyEntity.hpp"
#include "main/GuiUtils.hpp"

#endif

namespace NekoGui_sys {
    class CoreProcess;
}

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    void refresh_proxy_list(const int &id = -1);

    void show_group(int gid);

    void refresh_groups();

    void refresh_status(const QString &traffic_update = "");
    void p3r_traffic_sample(qint64 up, qint64 down); // NekoBoxP3R: speed graph in the banner

    void neko_start(int _id = -1);

    void neko_stop(bool crash = false, bool sem = false);

    void neko_set_spmode_system_proxy(bool enable, bool save = true);

    void neko_set_spmode_vpn(bool enable, bool save = true);

    void show_log_impl(const QString &log);

    void start_select_mode(QObject *context, const std::function<void(int)> &callback);

    void refresh_connection_list(const QJsonArray &arr);

    void RegisterHotkey(bool unregister);

    bool StopVPNProcess(bool unconditional = false);

signals:

    void profile_selected(int id);

public slots:

    void on_commitDataRequest();

    void on_menu_exit_triggered();

#ifndef MW_INTERFACE

private slots:

    void on_masterLogBrowser_customContextMenuRequested(const QPoint &pos);

    void on_menu_basic_settings_triggered();

    void on_menu_routing_settings_triggered();

    void on_menu_vpn_settings_triggered();

    void on_menu_hotkey_settings_triggered();

    void on_menu_add_from_input_triggered();

    void on_menu_add_from_clipboard_triggered();

    void on_menu_clone_triggered();

    void on_menu_move_triggered();

    void on_menu_delete_triggered();

    void on_menu_reset_traffic_triggered();

    void on_menu_profile_debug_info_triggered();

    void on_menu_copy_links_triggered();

    void on_menu_copy_links_nkr_triggered();

    void on_menu_export_config_triggered();

    void display_qr_link(bool nkrFormat = false);

    void on_menu_scan_qr_triggered();

    void on_menu_clear_test_result_triggered();

    void on_menu_manage_groups_triggered();

    void on_menu_select_all_triggered();

    void on_menu_delete_repeat_triggered();

    void on_menu_remove_unavailable_triggered();

    void on_menu_update_subscription_triggered();

    void on_menu_resolve_domain_triggered();

    void on_proxyListTable_itemDoubleClicked(QTableWidgetItem *item);

    void on_proxyListTable_customContextMenuRequested(const QPoint &pos);

    void on_tabWidget_currentChanged(int index);

private:
    Ui::MainWindow *ui;
    QSystemTrayIcon *tray;
    QShortcut *shortcut_ctrl_f = new QShortcut(QKeySequence("Ctrl+F"), this);
    QShortcut *shortcut_esc = new QShortcut(QKeySequence("Esc"), this);
    //
    NekoGui_sys::CoreProcess *core_process;
    qint64 vpn_pid = 0;
    //
    bool qvLogAutoScoll = true;
    QTextDocument *qvLogDocument = new QTextDocument(this);
    //
    QString title_error;
    int icon_status = -1;
    std::shared_ptr<NekoGui::ProxyEntity> running;
    QString traffic_update_cache;
    QTime last_test_time;
    //
    int proxy_last_order = -1;
    bool select_mode = false;
    // NekoBoxP3R
    P3R::ConnectButton *p3r_connect_button = nullptr;
    int p3r_last_connected = -1;
    bool p3r_best_pending = false; // waiting for the speed test to pick the fastest server
    QCheckBox *p3r_auto_best_check = nullptr;
    void p3r_connect_best();
    bool p3r_speedtest_running();
    void p3r_speedtest_finished();
    void p3r_apply_clear_names();
    // failover watchdog
    int p3r_fail_count = 0;
    bool p3r_failover_pending = false;
    bool p3r_watchdog_busy = false;
    qint64 p3r_failover_cooldown_until = 0;
    bool p3r_test_current(int timeoutMs);
    void p3r_watchdog_tick();
    void p3r_notify(const QString &title, const QString &text);
    // kill switch
    void p3r_killswitch_on_crash();
    void p3r_killswitch_release(bool notify);
    void p3r_show_apps_dialog();
    QPushButton *p3r_apps_button = nullptr;
    void p3r_refresh_apps_button();
    P3R::Banner *p3r_banner = nullptr;
    P3R::EmptyState *p3r_empty = nullptr;
    bool p3r_is_favorite(int id) const;
    void p3r_toggle_favorite(const QList<int> &ids);
    void p3r_paste_from_clipboard();
    // Dark Hour & splash
    bool p3r_dark_hour_active = false;
    qint64 p3r_dark_hour_preview_until = 0;
    void p3r_dark_hour_tick();
    void p3r_apply_dark_hour(bool on);
    void p3r_show_main_window();
    // My IP, speed test, site rules
    class QNetworkAccessManager *p3r_nam = nullptr;
    int p3r_ip_request = 0;
    void p3r_check_ip();
    void p3r_speed_test_selected();
    QStringList p3r_full_test_flags; // when set, a full test runs without the options dialog
    void p3r_add_site_rule(const QString &host, bool direct);
    void p3r_show_sites_dialog();
    // subscription remaining traffic / expiry
    void p3r_update_group_tabs();
    void p3r_check_subscriptions();
    bool p3r_pick_running_apps(QStringList &out);
    void p3r_show_about();
    QAction *p3r_simple_mode_action = nullptr;
    bool p3r_update_dialog_open = false;
    void p3r_setup();
    void p3r_connect_clicked();
    void p3r_refresh_connect_button();
    void p3r_apply_simple_mode();
    QMutex mu_starting;
    QMutex mu_stopping;
    QMutex mu_exit;
    QSemaphore sem_stopped;
    int exit_reason = 0;

    QList<std::shared_ptr<NekoGui::ProxyEntity>> get_now_selected_list();

    QList<std::shared_ptr<NekoGui::ProxyEntity>> get_selected_or_group();

    void dialog_message_impl(const QString &sender, const QString &info);

    void refresh_proxy_list_impl(const int &id = -1, GroupSortAction groupSortAction = {});

    void refresh_proxy_list_impl_refresh_data(const int &id = -1);

    void keyPressEvent(QKeyEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    //

    void HotkeyEvent(const QString &key);

    bool StartVPNProcess();

    // grpc and ...

    static void setup_grpc();

    void speedtest_current_group(int mode, bool test_group);

    void speedtest_current();

    static void stop_core_daemon();

    void CheckUpdate(bool silent = false);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

#endif // MW_INTERFACE
};

inline MainWindow *GetMainWindow() {
    return (MainWindow *) mainwindow;
}

void UI_InitMainWindow();
