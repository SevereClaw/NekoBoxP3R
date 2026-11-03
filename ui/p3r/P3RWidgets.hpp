// NekoBoxP3R: Persona 3 styled widgets, animations and sounds.
#pragma once

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

    // Slanted title banner at the top of the main window
    class Banner : public QWidget {
        Q_OBJECT
    public:
        explicit Banner(const QString &title, const QString &subtitle, QWidget *parent = nullptr);
        QSize sizeHint() const override { return {400, 46}; }
        void playIntro();

    protected:
        void paintEvent(QPaintEvent *) override;
        void showEvent(QShowEvent *) override;

    private:
        QString m_title, m_subtitle;
        qreal m_progress = 1;
        bool m_introPlayed = false;
        QVariantAnimation *m_anim;
    };

    // Latency cell: colored slanted pill + signal bars
    // item data: LatencyRole = latency ms (-1 unavailable, 0 untested), GreenRole = "good" threshold ms
    inline constexpr int LatencyRole = Qt::UserRole + 0x51;
    inline constexpr int GreenRole = Qt::UserRole + 0x52;
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
