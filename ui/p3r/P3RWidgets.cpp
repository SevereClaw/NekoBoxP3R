// NekoBoxP3R: Persona 3 styled widgets, animations and sounds.
#include "P3RWidgets.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QMenu>
#include <QFile>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QEnterEvent>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#ifndef P3R_VERSION
#define P3R_VERSION "dev"
#endif

namespace P3R {

    // palette
    static const QColor kNavy("#0A1A33");
    static const QColor kDeep("#061026");
    static const QColor kBlue("#1F4F8A");
    static const QColor kCyan("#2EE6FF");
    static const QColor kIce("#EAF6FF");
    static const QColor kRed("#FF2B3B");
    static const QColor kDarkRed("#8A0F1C");

    // A parallelogram leaning right (like P3R menu blocks)
    static QPolygonF slanted(const QRectF &r, qreal skew) {
        QPolygonF p;
        p << QPointF(r.left() + skew, r.top()) << QPointF(r.right(), r.top())
          << QPointF(r.right() - skew, r.bottom()) << QPointF(r.left(), r.bottom());
        return p;
    }

    // ======================================================== sounds

    void PlayFx(Sound s) {
        if (!soundsEnabled) return;
#ifdef Q_OS_WIN
        static QByteArray data[3];
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            const char *names[3] = {":/p3r/sounds/menu.wav", ":/p3r/sounds/connect.wav", ":/p3r/sounds/disconnect.wav"};
            for (int i = 0; i < 3; i++) {
                QFile f(names[i]);
                if (f.open(QIODevice::ReadOnly)) data[i] = f.readAll();
            }
        }
        const auto &buf = data[static_cast<int>(s)];
        if (buf.isEmpty()) return;
        ::PlaySoundW(reinterpret_cast<LPCWSTR>(buf.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
        Q_UNUSED(s)
#endif
    }

    // ======================================================== animator

    class Animator : public QObject {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *obj, QEvent *event) override {
            if (event->type() != QEvent::Show) return false;

            if (auto menu = qobject_cast<QMenu *>(obj)) {
                if (!menu->isWindow()) return false;
                bool topLevel = qobject_cast<QMenu *>(menu->parentWidget()) == nullptr;
                if (topLevel) PlayFx(Sound::Menu);
                if (!animationsEnabled) return false;

                auto end = menu->pos();
                auto group = new QParallelAnimationGroup(menu);
                auto slide = new QPropertyAnimation(menu, "pos");
                slide->setDuration(170);
                slide->setStartValue(end - QPoint(22, 0));
                slide->setEndValue(end);
                slide->setEasingCurve(QEasingCurve::OutCubic);
                auto fade = new QPropertyAnimation(menu, "windowOpacity");
                fade->setDuration(150);
                fade->setStartValue(0.0);
                fade->setEndValue(1.0);
                group->addAnimation(slide);
                group->addAnimation(fade);
                menu->setWindowOpacity(0.0);
                group->start(QAbstractAnimation::DeleteWhenStopped);
                return false;
            }

            if (auto dialog = qobject_cast<QDialog *>(obj)) {
                if (!dialog->isWindow() || !animationsEnabled) return false;
                auto fade = new QPropertyAnimation(dialog, "windowOpacity", dialog);
                fade->setDuration(180);
                fade->setStartValue(0.0);
                fade->setEndValue(1.0);
                fade->setEasingCurve(QEasingCurve::OutCubic);
                dialog->setWindowOpacity(0.0);
                fade->start(QAbstractAnimation::DeleteWhenStopped);
            }
            return false;
        }
    };

    void InstallAnimator() {
        static Animator *animator = nullptr;
        if (animator != nullptr) return;
        animator = new Animator(qApp);
        qApp->installEventFilter(animator);
    }

    // ======================================================== connect button

    ConnectButton::ConnectButton(QWidget *parent) : QPushButton(parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(68);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover);

        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(160);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [=](const QVariant &v) {
            m_hover = v.toReal();
            update();
        });

        m_sweepAnim = new QVariantAnimation(this);
        m_sweepAnim->setDuration(650);
        m_sweepAnim->setStartValue(0.0);
        m_sweepAnim->setEndValue(1.0);
        m_sweepAnim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(m_sweepAnim, &QVariantAnimation::valueChanged, this, [=](const QVariant &v) {
            m_sweep = v.toReal();
            update();
        });
        connect(m_sweepAnim, &QVariantAnimation::finished, this, [=] {
            m_sweep = -1;
            update();
        });
    }

    void ConnectButton::setConnected(bool connected) {
        if (m_connected == connected) return;
        m_connected = connected;
        update();
    }

    void ConnectButton::sweep() {
        if (!animationsEnabled) return;
        m_sweepAnim->stop();
        m_sweepAnim->start();
    }

    void ConnectButton::animateHover(qreal to) {
        if (!animationsEnabled) {
            m_hover = to;
            update();
            return;
        }
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hover);
        m_hoverAnim->setEndValue(to);
        m_hoverAnim->start();
    }

    void ConnectButton::enterEvent(QEnterEvent *e) {
        animateHover(1);
        QPushButton::enterEvent(e);
    }

    void ConnectButton::leaveEvent(QEvent *e) {
        animateHover(0);
        QPushButton::leaveEvent(e);
    }

    void ConnectButton::paintEvent(QPaintEvent *) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);

        const qreal press = isDown() ? 2 : 0;
        QRectF r = QRectF(rect()).adjusted(4, 6, -14, -10).translated(m_hover * 6 + press, press);
        const qreal skew = r.height() * 0.32;

        // shadow block
        p.setBrush(m_connected ? kDarkRed : kBlue);
        p.drawPolygon(slanted(r.translated(6 + m_hover * 4, 5), skew));

        // body
        QLinearGradient g(r.topLeft(), r.topRight());
        if (m_connected) {
            g.setColorAt(0, kRed.lighter(100 + int(m_hover * 15)));
            g.setColorAt(1, kDarkRed);
        } else {
            g.setColorAt(0, Qt::white);
            g.setColorAt(0.6, kIce);
            g.setColorAt(1, kCyan.lighter(100 + int(m_hover * 10)));
        }
        auto body = slanted(r, skew);
        p.setBrush(g);
        p.drawPolygon(body);

        // accent stripe on the left edge
        QRectF stripe(r.left(), r.top(), skew + 10, r.height());
        p.setBrush(m_connected ? QColor(Qt::white) : (m_hover > 0.5 ? kRed : kCyan));
        QPolygonF sp;
        sp << QPointF(stripe.left() + skew, stripe.top()) << QPointF(stripe.left() + skew + 10, stripe.top())
           << QPointF(stripe.left() + 10, stripe.bottom()) << QPointF(stripe.left(), stripe.bottom());
        p.drawPolygon(sp);

        // light sweep
        if (m_sweep >= 0) {
            QPainterPath clip;
            clip.addPolygon(body);
            p.save();
            p.setClipPath(clip);
            qreal x = r.left() - 80 + m_sweep * (r.width() + 160);
            QLinearGradient sg(QPointF(x - 40, 0), QPointF(x + 40, 0));
            sg.setColorAt(0, QColor(255, 255, 255, 0));
            sg.setColorAt(0.5, QColor(255, 255, 255, 170));
            sg.setColorAt(1, QColor(255, 255, 255, 0));
            p.setBrush(sg);
            p.drawPolygon(slanted(QRectF(x - 40, r.top(), 80, r.height()), skew));
            p.restore();
        }

        // text: first line big, second line small (server name)
        auto lines = text().split('\n');
        QColor fg = m_connected ? QColor(Qt::white) : kNavy;
        p.setPen(fg);
        QFont mainFont = font();
        mainFont.setFamily("Segoe UI");
        mainFont.setItalic(true);
        mainFont.setWeight(QFont::Black);
        mainFont.setPointSizeF(lines.size() > 1 ? 15 : 17);
        QFont subFont = mainFont;
        subFont.setWeight(QFont::DemiBold);
        subFont.setPointSizeF(9.5);

        QRectF textRect = r.adjusted(skew + 26, 0, -skew, 0);
        if (lines.size() > 1) {
            QRectF top = textRect;
            top.setBottom(r.center().y() + 6);
            p.setFont(mainFont);
            p.drawText(top, Qt::AlignLeft | Qt::AlignBottom, lines[0]);
            QRectF bottom = textRect.translated(-6, 0);
            bottom.setTop(r.center().y() + 6);
            p.setFont(subFont);
            p.drawText(bottom, Qt::AlignLeft | Qt::AlignTop, lines.mid(1).join(' '));
        } else {
            p.setFont(mainFont);
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, lines.value(0));
        }

        if (hasFocus()) {
            p.setPen(QPen(fg, 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(slanted(r.adjusted(3, 3, -3, -3), skew - 2));
        }
    }

    // ======================================================== banner

    Banner::Banner(const QString &title, const QString &subtitle, QWidget *parent)
        : QWidget(parent), m_title(title), m_subtitle(subtitle) {
        setMinimumHeight(46);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(650);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [=](const QVariant &v) {
            m_progress = v.toReal();
            update();
        });
    }

    void Banner::playIntro() {
        if (!animationsEnabled) {
            m_progress = 1;
            update();
            return;
        }
        m_anim->stop();
        m_anim->start();
    }

    void Banner::showEvent(QShowEvent *e) {
        QWidget::showEvent(e);
        if (!m_introPlayed) {
            m_introPlayed = true;
            playIntro();
        }
    }

    void Banner::paintEvent(QPaintEvent *) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const qreal w = width(), h = height();
        const qreal skew = h * 0.45;
        const qreal t = m_progress;

        // long dark-blue slab
        QRectF slab(-skew + (t - 1) * w, 4, w * 0.78, h - 8);
        QLinearGradient g(slab.topLeft(), slab.topRight());
        g.setColorAt(0, kBlue);
        g.setColorAt(1, QColor(31, 79, 138, 0));
        p.setBrush(g);
        p.drawPolygon(slanted(slab, skew));

        // cyan and white thin bars (arrive a bit later)
        qreal t2 = qBound(0.0, t * 1.4 - 0.2, 1.0);
        p.setBrush(kCyan);
        p.drawPolygon(slanted(QRectF(-skew + (t2 - 1) * w, h - 9, w * 0.55, 4), 4));
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawPolygon(slanted(QRectF(w * 0.35 + (1 - t2) * w, 2, w * 0.4, 3), 3));
        p.setBrush(kRed);
        p.drawPolygon(slanted(QRectF(10 + (t2 - 1) * 200, 8, 10, h - 18), 6));

        // title
        QFont f("Segoe UI");
        f.setItalic(true);
        f.setWeight(QFont::Black);
        f.setPointSizeF(17);
        p.setFont(f);
        QRectF titleRect(30 + (t - 1) * 120, 0, w, h);
        p.setPen(kDeep);
        p.drawText(titleRect.translated(2, 2), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        p.setPen(Qt::white);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);

        // subtitle (version) on the right
        QFont s("Segoe UI");
        s.setItalic(true);
        s.setWeight(QFont::Bold);
        s.setPointSizeF(9);
        p.setFont(s);
        p.setPen(QColor(46, 230, 255, int(255 * t)));
        p.drawText(QRectF(0, 0, w - 12, h), Qt::AlignRight | Qt::AlignVCenter, m_subtitle);
    }

    // ======================================================== about

    AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle(tr("About NekoBoxP3R"));
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(640, 360);

        auto root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 18, 14);
        root->setSpacing(18);

        auto iconLabel = new QLabel;
        iconLabel->setFixedWidth(220);
        iconLabel->setAlignment(Qt::AlignCenter);
        QPixmap icon(":/neko/nekobox.png");
        iconLabel->setPixmap(icon.scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setStyleSheet("background: transparent;");
        root->addWidget(iconLabel);

        auto right = new QVBoxLayout;
        right->setSpacing(6);
        right->setContentsMargins(0, 18, 0, 0);

        auto title = new QLabel("NekoBoxP3R");
        title->setStyleSheet("font-size: 26pt; font-weight: 900; font-style: italic; color: #FFFFFF; background: transparent;");
        right->addWidget(title);

        auto version = new QLabel(tr("Version %1").arg(P3R_VERSION) + "  ·  NekoBox " + QString(NKR_VERSION));
        version->setStyleSheet("color: #2EE6FF; font-weight: 700; font-style: italic; background: transparent;");
        right->addWidget(version);

        const QString repo = "https://github.com/SevereClaw/NekoBoxP3R";
        auto body = new QLabel(
            "<p>" + tr("NekoBox in the style of Persona 3: theme, big connect button, simple mode, animations, sounds and updates from GitHub.") + "</p>" +
            "<p>" + tr("Based on <a href=\"%1\">NekoBox (nekoray)</a> by MatsuriDayo and contributors. Core: <a href=\"%2\">sing-box</a>. UI: Qt.")
                        .arg("https://github.com/MatsuriDayo/nekoray", "https://github.com/SagerNet/sing-box") + "</p>" +
            "<p>" + tr("License: <a href=\"%1\">GNU GPL v3.0</a>. Source code: <a href=\"%2\">%2</a>")
                        .arg("https://www.gnu.org/licenses/gpl-3.0.html", repo) + "</p>" +
            "<p style=\"color:#7FA3C8\">" + tr("Persona is a trademark of ATLUS / SEGA. This is a fan project and is not affiliated with them.") + "</p>");
        body->setWordWrap(true);
        body->setOpenExternalLinks(true);
        body->setTextFormat(Qt::RichText);
        body->setStyleSheet("background: transparent;");
        right->addWidget(body, 1);

        auto buttons = new QHBoxLayout;
        buttons->addStretch();
        auto btnUpdate = new QPushButton(tr("Check for updates"));
        auto btnSource = new QPushButton(tr("Source code"));
        auto btnClose = new QPushButton(tr("Close"));
        btnClose->setDefault(true);
        buttons->addWidget(btnUpdate);
        buttons->addWidget(btnSource);
        buttons->addWidget(btnClose);
        right->addLayout(buttons);
        root->addLayout(right, 1);

        connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnUpdate, &QPushButton::clicked, this, [=] {
            emit checkUpdateRequested();
            accept();
        });
        connect(btnSource, &QPushButton::clicked, this, [=] {
            QDesktopServices::openUrl(QUrl(repo));
        });
    }

    void AboutDialog::paintEvent(QPaintEvent *) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const qreal h = height();

        QLinearGradient bg(0, 0, width(), h);
        bg.setColorAt(0, QColor("#0E2D5E"));
        bg.setColorAt(0.6, kNavy);
        bg.setColorAt(1, kDeep);
        p.fillRect(rect(), bg);

        // slanted panel behind the icon
        p.setBrush(kBlue);
        p.drawPolygon(slanted(QRectF(-60, 0, 300, h), 70));
        p.setBrush(kCyan);
        p.drawPolygon(slanted(QRectF(196, 0, 14, h), 70));
        p.setBrush(Qt::white);
        p.drawPolygon(slanted(QRectF(216, 0, 4, h), 70));
        p.setBrush(kRed);
        p.drawPolygon(slanted(QRectF(0, h - 28, 180, 10), 8));
    }

} // namespace P3R
