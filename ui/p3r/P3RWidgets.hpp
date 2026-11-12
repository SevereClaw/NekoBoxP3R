// NekoBoxP3R: Persona 3 styled widgets, animations and sounds.
#pragma once

#include <QTimer>

#include <QPushButton>
#include <QWidget>
#include <QDialog>
#include <QVariantAnimation>
#include <QPointer>
#include <QStyledItemDelegate>

namespace P3R {

    // ---- sounds (Windows: PlaySound from memory, async) ----
    enum class Sound { Menu = 0, Connect = 1, Disconnect = 2 };
    void PlayFx(Sound s);
    inline bool soundsEnabled = true;
    inline bool animationsEnabled = true;

    // ---- Dark Hour (00:00-01:00): green palette for painted widgets + green theme ----
    void SetDarkHourPalette(bool on);
    bool IsDarkHour();
    QString DarkHourQss(QString qss);

    // Start-up splash: full screen, the icon spins and fades out, then the app appears
    class Splash : public QWidget {
        Q_OBJECT
    public:
        explicit Splash(const QPixmap &icon);
        void start();
    signals:
        void reveal();

    protected:
        void paintEvent(QPaintEvent *) override;
        void mousePressEvent(QMouseEvent *) override;

    private:
        QPixmap m_icon;
        qreal m_t = 0;
        bool m_revealed = false;
        QVariantAnimation *m_anim;
    };

    // Installs the app-wide event filter that slides menus in and fades dialogs.
    void InstallAnimator();

    // Big slanted CONNECT / DISCONNECT button
    class ConnectButton : public QPushButton {
        Q_OBJECT
    public:
        explicit ConnectButton(QWidget *parent = nullptr);
        void setConnected(bool connected);
        bool isConnected() const { return m_connected; }
        void sweep(); // light sweep across the button
        QSize sizeHint() const override { return {320, 68}; }

    protected:
        void paintEvent(QPaintEvent *) override;
        void enterEvent(QEnterEvent *) override;
        void leaveEvent(QEvent *) override;

    private:
        void animateHover(qreal to);
        bool m_connected = false;
        qreal m_hover = 0;
        qreal m_sweep = -1;
        QVariantAnimation *m_hoverAnim;
        QVariantAnimation *m_sweepAnim;
    };

    // ---- countries & favorites in the server list ----
    // Country code ("DE") from a server name: flag emoji, ISO code token, country or city name (en/ru).
    QString DetectCountry(const QString &name);
    // Name without flag emoji (Windows cannot draw them).
    QString StripFlags(const QString &name);
    // Icon for the Name column: favorite star + mini flag (or code badge). Width 46, height 16 (logical px).
    QPixmap NameIcon(const QString &code, bool favorite, qreal dpr);

    // Empty server list: first steps + big paste button
    class EmptyState : public QWidget {
        Q_OBJECT
    public:
        explicit EmptyState(QWidget *parentToCover);
    signals:
        void pasteClicked();

    protected:
        bool eventFilter(QObject *obj, QEvent *event) override;
        void paintEvent(QPaintEvent *) override;
    };

    // Mini mode: a small always-on-top panel with the connect button, server, flag and speed
    class MiniWindow : public QWidget {
        Q_OBJECT
    public:
        explicit MiniWindow();
        // state: 0 disconnected, 1 connected, 2 busy (checking / connecting)
        void setState(int state, const QString &server, const QString &countryCode, const QString &mode);
        void setSpeed(qint64 up, qint64 down);
        // NekoBoxP3R: Shift + wheel = transparency, Ctrl + wheel = size (both in percent)
        void setAppearance(int opacityPercent, int scalePercent);
        int opacityPercent() const { return m_opacity; }
        int scalePercent() const { return m_scale; }
    signals:
        void toggleClicked();
        void openMain();
        void closeClicked();
        void menuRequested(const QPoint &globalPos);
        void moved(const QPoint &pos);
        void appearanceChanged(int opacityPercent, int scalePercent);

    protected:
        void paintEvent(QPaintEvent *) override;
        void mousePressEvent(QMouseEvent *) override;
        void mouseMoveEvent(QMouseEvent *) override;
        void mouseReleaseEvent(QMouseEvent *) override;
        void mouseDoubleClickEvent(QMouseEvent *) override;
        void contextMenuEvent(QContextMenuEvent *) override;
        void enterEvent(QEnterEvent *) override;
        void leaveEvent(QEvent *) override;
        void wheelEvent(QWheelEvent *) override;
        void keyPressEvent(QKeyEvent *) override;

    private:
        static constexpr int kBaseW = 340, kBaseH = 72;
        QRectF buttonRect() const;
        QRectF closeRect() const;
        QPointF toBase(const QPointF &p) const { return p / (m_scale / 100.0); }
        void showHint(const QString &text);
        int m_opacity = 100, m_scale = 100;
        QString m_hint;
        QTimer *m_hintTimer = nullptr, *m_saveTimer = nullptr;
        bool m_pressOnClose = false;
        int m_state = 0;
        QString m_server, m_code, m_mode;
        qint64 m_up = -1, m_down = -1;
        bool m_hover = false, m_dragging = false, m_pressOnButton = false;
        QPoint m_dragOffset, m_pressPos;
    };

    // Slanted title banner at the top of the main window
    class Banner : public QWidget {
        Q_OBJECT
    public:
        explicit Banner(const QString &title, const QString &subtitle, QWidget *parent = nullptr);
        QSize sizeHint() const override { return {400, 46}; }
        void playIntro();
        void addTraffic(qint64 upBytesPerSec, qint64 downBytesPerSec);
        void clearTraffic();
        // "My IP": state 0 = unknown/checking, 1 = through the proxy, 2 = direct (no VPN), 3 = error
        void setIpInfo(int state, const QString &ip, const QString &countryCode, const QString &city);
    signals:
        void clicked();

    protected:
        void mousePressEvent(QMouseEvent *) override;

    protected:
        void paintEvent(QPaintEvent *) override;
        void showEvent(QShowEvent *) override;

    private:
        QString m_title, m_subtitle;
        QList<qint64> m_up, m_down; // last samples, bytes/s
        int m_ipState = 0;
        QString m_ip, m_ipCountry, m_ipCity;
        qreal m_progress = 1;
        bool m_introPlayed = false;
        QVariantAnimation *m_anim;
    };

    // Latency cell: colored slanted pill + signal bars
    // item data: LatencyRole = latency ms (-1 unavailable, 0 untested), GreenRole = "good" threshold ms
    inline constexpr int LatencyRole = Qt::UserRole + 0x51;
    inline constexpr int GreenRole = Qt::UserRole + 0x52;
    inline constexpr int SpeedRole = Qt::UserRole + 0x53; // download speed in Mbit/s (double), -1 = failed
    class LatencyDelegate : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    };

    // About dialog
    class AboutDialog : public QDialog {
        Q_OBJECT
    public:
        explicit AboutDialog(QWidget *parent = nullptr);
    signals:
        void checkUpdateRequested();

    protected:
        void paintEvent(QPaintEvent *) override;
    };

} // namespace P3R
