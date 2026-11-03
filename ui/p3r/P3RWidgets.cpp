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
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QEvent>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QFontMetricsF>
#include <QAbstractItemView>
#include <QDesktopServices>
#include <cmath>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#ifndef P3R_VERSION
#define P3R_VERSION "dev"
#endif

namespace P3R {

    // palette (switched by SetDarkHourPalette)
    static QColor kNavy("#0A1A33");
    static QColor kDeep("#061026");
    static QColor kBlue("#1F4F8A");
    static QColor kCyan("#2EE6FF");
    static QColor kIce("#EAF6FF");
    static QColor kRed("#FF2B3B");
    static QColor kDarkRed("#8A0F1C");
    static bool gDarkHour = false;

    static QColor withAlpha(QColor c, int a) {
        c.setAlpha(a);
        return c;
    }

    void SetDarkHourPalette(bool on) {
        gDarkHour = on;
        kNavy = QColor(on ? "#08190C" : "#0A1A33");
        kDeep = QColor(on ? "#040E06" : "#061026");
        kBlue = QColor(on ? "#1F5A2A" : "#1F4F8A");
        kCyan = QColor(on ? "#7CFF4F" : "#2EE6FF");
        kIce = QColor(on ? "#E9FFD9" : "#EAF6FF");
        kRed = QColor(on ? "#E0102A" : "#FF2B3B");
        kDarkRed = QColor(on ? "#6A0010" : "#8A0F1C");
    }

    bool IsDarkHour() { return gDarkHour; }

    QString DarkHourQss(QString qss) {
        static const char *map[][2] = {
            {"#2EE6FF", "#7CFF4F"}, {"#0A1A33", "#08190C"}, {"#061026", "#040E06"}, {"#1F4F8A", "#1F5A2A"},
            {"#EAF6FF", "#E9FFD9"}, {"#0F2547", "#0D2A14"}, {"#1B4A86", "#1C5226"}, {"#9FC3E6", "#A9D99A"},
            {"#5B7A9C", "#5E7F55"}, {"#16345C", "#17391C"}, {"#13305A", "#133519"}, {"#081530", "#06160A"},
            {"#050C1A", "#030A04"}, {"#BFE6FF", "#C8F5B5"}, {"#163E73", "#18452A"}, {"#102A50", "#0F2E15"},
            {"#0E2D5E", "#0F3A17"}, {"#0D2448", "#0C2A12"}, {"#0C1E3E", "#0A2210"}, {"#FF2B3B", "#E0102A"},
        };
        for (const auto &m: map) qss.replace(m[0], m[1], Qt::CaseInsensitive);
        return qss;
    }

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
        g.setColorAt(1, withAlpha(kBlue, 0));
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

        // Dark Hour: a huge sickly moon and the label
        if (gDarkHour) {
            qreal R = h * 1.3;
            QPointF c(w * 0.5, h * 0.15);
            QRadialGradient mg(c, R);
            mg.setColorAt(0, QColor(230, 255, 140, 200));
            mg.setColorAt(0.55, QColor(170, 255, 90, 90));
            mg.setColorAt(1, QColor(124, 255, 79, 0));
            p.setBrush(mg);
            p.drawEllipse(c, R, R);
            p.setBrush(QColor(240, 255, 170, 230));
            p.drawEllipse(c, h * 0.42, h * 0.42);
        }

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

        // version right after the title
        QFontMetricsF titleMetrics(f);
        QFont s("Segoe UI");
        s.setItalic(true);
        s.setWeight(QFont::Bold);
        s.setPointSizeF(9);
        p.setFont(s);
        p.setPen(withAlpha(kCyan, int(255 * t)));
        qreal subX = titleRect.left() + titleMetrics.horizontalAdvance(m_title) + 12;
        p.drawText(QRectF(subX, 0, 120, h - 6), Qt::AlignLeft | Qt::AlignBottom, m_subtitle);
        if (gDarkHour) {
            QFont df("Segoe UI");
            df.setItalic(true);
            df.setWeight(QFont::Black);
            df.setPointSizeF(12);
            p.setFont(df);
            p.setPen(QColor(224, 16, 42));
            p.drawText(QRectF(subX + 60, 0, 220, h), Qt::AlignLeft | Qt::AlignVCenter, tr("DARK HOUR"));
        }

        // traffic graph on the right
        if (w < 560) return;
        const qreal gw = 230, gh = h - 14;
        QRectF graphRect(w - gw - 10, 7, gw, gh);
        // slanted background block
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(kDeep, 200));
        p.drawPolygon(slanted(graphRect.adjusted(-14, 0, 0, 0), 12));

        auto human = [](qint64 v) {
            if (v < 0) v = 0;
            if (v < 1024) return QStringLiteral("%1 B/s").arg(v);
            if (v < 1024 * 1024) return QStringLiteral("%1 KB/s").arg(v / 1024.0, 0, 'f', v < 10 * 1024 ? 1 : 0);
            return QStringLiteral("%1 MB/s").arg(v / 1048576.0, 0, 'f', 1);
        };
        qint64 maxV = 64 * 1024;
        for (auto v: m_down) maxV = qMax(maxV, v);
        for (auto v: m_up) maxV = qMax(maxV, v);

        QRectF plot = graphRect.adjusted(6, 4, -96, -4);
        auto line = [&](const QList<qint64> &data) {
            QPolygonF poly;
            const int n = 60;
            for (int i = 0; i < data.size(); i++) {
                qreal x = plot.right() - (data.size() - 1 - i) * plot.width() / (n - 1);
                qreal y = plot.bottom() - plot.height() * qreal(data[i]) / qreal(maxV);
                poly << QPointF(x, y);
            }
            return poly;
        };
        if (m_down.size() >= 2) {
            auto down = line(m_down);
            QPolygonF area = down;
            area << QPointF(down.last().x(), plot.bottom()) << QPointF(down.first().x(), plot.bottom());
            QLinearGradient ag(plot.topLeft(), plot.bottomLeft());
            ag.setColorAt(0, withAlpha(kCyan, 150));
            ag.setColorAt(1, withAlpha(kCyan, 10));
            p.setBrush(ag);
            p.drawPolygon(area);
            p.setPen(QPen(kCyan, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(down);
        }
        if (m_up.size() >= 2) {
            p.setPen(QPen(QColor(255, 255, 255, 210), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(line(m_up));
        }
        // baseline
        p.setPen(QPen(withAlpha(kCyan, 60), 1));
        p.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));

        // numbers
        QFont nf("Segoe UI");
        nf.setItalic(true);
        nf.setWeight(QFont::Bold);
        nf.setPointSizeF(8.5);
        p.setFont(nf);
        QRectF txt(plot.right() + 8, graphRect.top() + 2, 90, graphRect.height() / 2 - 2);
        bool idle = m_down.isEmpty();
        p.setPen(kCyan);
        p.drawText(txt, Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("\u2193 ") + (idle ? QString("—") : human(m_down.last())));
        p.setPen(Qt::white);
        p.drawText(txt.translated(0, graphRect.height() / 2 - 2), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("\u2191 ") + (idle ? QString("—") : human(m_up.last())));
    }

    void Banner::addTraffic(qint64 up, qint64 down) {
        m_up << up;
        m_down << down;
        while (m_up.size() > 60) m_up.removeFirst();
        while (m_down.size() > 60) m_down.removeFirst();
        update();
    }

    void Banner::clearTraffic() {
        m_up.clear();
        m_down.clear();
        update();
    }

    // ======================================================== latency

    static void latencyLook(int ms, int green, QColor &color, int &bars) {
        if (ms < 0) {
            color = kRed;
            bars = 0;
        } else if (ms < green) {
            color = QColor("#22DD66");
            bars = 4;
        } else if (ms < green * 5 / 2) {
            color = QColor("#FFD23F");
            bars = 3;
        } else if (ms < 1000) {
            color = QColor("#FF8A3D");
            bars = 2;
        } else {
            color = QColor("#FF5A6A");
            bars = 1;
        }
    }

    void LatencyDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
        auto latencyData = index.data(LatencyRole);
        int ms = latencyData.isValid() ? latencyData.toInt() : 0;
        if (!latencyData.isValid() || ms == 0) {
            QStyledItemDelegate::paint(painter, option, index); // untested or full test report: plain text
            return;
        }

        // background / selection without text
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        auto style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        int green = index.data(GreenRole).toInt();
        if (green <= 0) green = 200;
        QColor color;
        int bars;
        latencyLook(ms, green, color, bars);
        QString text = ms < 0 ? index.data(Qt::DisplayRole).toString() : QStringLiteral("%1 ms").arg(ms);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        QRectF r = QRectF(option.rect).adjusted(6, 4, -6, -4);

        // signal bars
        const qreal barW = 4, gap = 2, barsW = 4 * barW + 3 * gap;
        for (int i = 0; i < 4; i++) {
            qreal bh = r.height() * (0.35 + 0.65 * (i + 1) / 4.0);
            QRectF b(r.left() + i * (barW + gap), r.bottom() - bh, barW, bh);
            painter->setBrush(i < bars ? color : QColor(255, 255, 255, 40));
            painter->drawRect(b);
        }

        // slanted pill with the value
        QFont f = option.font;
        f.setBold(true);
        f.setItalic(true);
        f.setPointSizeF(qMax<qreal>(f.pointSizeF(), 10.5));
        painter->setFont(f);
        qreal textW = QFontMetricsF(f).horizontalAdvance(text);
        const qreal skew = r.height() * 0.3;
        QRectF pill(r.left() + barsW + 8, r.top(), textW + 18 + skew, r.height());
        painter->setBrush(color);
        painter->drawPolygon(slanted(pill, skew));
        painter->setPen(kDeep);
        painter->drawText(pill.adjusted(skew / 2, 0, -skew / 2, 0), Qt::AlignCenter, text);
        painter->restore();
    }

    QSize LatencyDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
        auto s = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(LatencyRole).toInt() != 0) s.setWidth(s.width() + 60);
        return s;
    }

    // ======================================================== countries

    struct CountryInfo {
        const char *code;
        const char *names; // lowercase, '|' separated: english, russian, big cities
    };
    static const CountryInfo kCountries[] = {
        {"DE", "germany|deutschland|германия|frankfurt|франкфурт|berlin|берлин|munich|мюнхен|nuremberg|nürnberg|falkenstein|düsseldorf|dusseldorf"},
        {"NL", "netherlands|holland|нидерланды|голландия|amsterdam|амстердам|rotterdam"},
        {"FI", "finland|финляндия|helsinki|хельсинки"},
        {"SE", "sweden|швеция|stockholm|стокгольм"},
        {"NO", "norway|норвегия|oslo|осло"},
        {"DK", "denmark|дания|copenhagen|копенгаген"},
        {"FR", "france|франция|paris|париж|marseille|марсель|gravelines|roubaix|strasbourg"},
        {"IT", "italy|италия|milan|милан|rome|рим"},
        {"ES", "spain|испания|madrid|мадрид|barcelona|барселона"},
        {"PT", "portugal|португалия|lisbon|лиссабон"},
        {"GB", "united kingdom|britain|england|великобритания|британия|англия|london|лондон|manchester"},
        {"IE", "ireland|ирландия|dublin|дублин"},
        {"BE", "belgium|бельгия|brussels|брюссель"},
        {"LU", "luxembourg|люксембург"},
        {"CH", "switzerland|швейцария|zurich|zürich|цюрих|geneva|женева"},
        {"AT", "austria|австрия|vienna|вена"},
        {"PL", "poland|польша|warsaw|варшава"},
        {"CZ", "czech|чехия|prague|прага"},
        {"HU", "hungary|венгрия|budapest|будапешт"},
        {"RO", "romania|румыния|bucharest|бухарест"},
        {"BG", "bulgaria|болгария|sofia|софия"},
        {"LT", "lithuania|литва|vilnius|вильнюс"},
        {"LV", "latvia|латвия|riga|рига"},
        {"EE", "estonia|эстония|tallinn|таллин"},
        {"UA", "ukraine|украина|kyiv|kiev|киев"},
        {"MD", "moldova|молдова|chisinau|кишинёв|кишинев"},
        {"RU", "russia|россия|moscow|москва|saint petersburg|санкт-петербург|питер"},
        {"KZ", "kazakhstan|казахстан|almaty|алматы|astana|астана"},
        {"TR", "turkey|türkiye|турция|istanbul|стамбул"},
        {"AM", "armenia|армения|yerevan|ереван"},
        {"GE", "georgia|грузия|tbilisi|тбилиси"},
        {"AE", "emirates|uae|оаэ|эмираты|dubai|дубай"},
        {"IL", "israel|израиль|tel aviv"},
        {"US", "united states|usa|америка|сша|new york|нью-йорк|los angeles|лос-анджелес|chicago|dallas|seattle|miami|ashburn|silicon valley|san jose"},
        {"CA", "canada|канада|toronto|торонто|montreal|vancouver"},
        {"JP", "japan|япония|tokyo|токио|osaka|осака"},
        {"KR", "korea|корея|seoul|сеул"},
        {"SG", "singapore|сингапур"},
        {"HK", "hong kong|гонконг"},
        {"IN", "india|индия|mumbai|мумбаи"},
        {"AU", "australia|австралия|sydney|сидней"},
        {"BR", "brazil|бразилия|são paulo|sao paulo"},
    };

    static QString flagEmojiCode(const QString &name) {
        auto ucs = name.toUcs4();
        for (int i = 0; i + 1 < ucs.size(); i++) {
            if (ucs[i] >= 0x1F1E6 && ucs[i] <= 0x1F1FF && ucs[i + 1] >= 0x1F1E6 && ucs[i + 1] <= 0x1F1FF) {
                return QString(QChar('A' + int(ucs[i] - 0x1F1E6))) + QChar('A' + int(ucs[i + 1] - 0x1F1E6));
            }
        }
        return {};
    }

    QString DetectCountry(const QString &name) {
        auto code = flagEmojiCode(name);
        if (!code.isEmpty()) return code == "UK" ? "GB" : code;
        // ISO code as a separate upper-case token: "DE-1", "[NL]", "US | Fast"
        static const QRegularExpression token("(?<![A-Za-z])([A-Z]{2})(?![A-Za-z])");
        auto it = token.globalMatch(name);
        while (it.hasNext()) {
            auto t = it.next().captured(1);
            if (t == "UK") t = "GB";
            for (const auto &c: kCountries) {
                if (t == c.code) return t;
            }
        }
        auto lower = name.toLower();
        for (const auto &c: kCountries) {
            for (const auto &word: QString::fromUtf8(c.names).split('|')) {
                if (lower.contains(word)) return c.code;
            }
        }
        return {};
    }

    QString StripFlags(const QString &name) {
        auto ucs = name.toUcs4();
        QList<char32_t> out;
        for (int i = 0; i < ucs.size(); i++) {
            if (ucs[i] >= 0x1F1E6 && ucs[i] <= 0x1F1FF) continue; // regional indicators
            out << ucs[i];
        }
        return QString::fromUcs4(out.constData(), out.size()).trimmed();
    }

    // --- mini flags ---
    enum class FlagKind { H3, H2, V3, Cross, Circle, SwissCross, None };
    struct FlagSpec {
        const char *code;
        FlagKind kind;
        const char *c1, *c2, *c3;
    };
    static const FlagSpec kFlags[] = {
        {"DE", FlagKind::H3, "#000000", "#DD0000", "#FFCE00"},
        {"NL", FlagKind::H3, "#AE1C28", "#FFFFFF", "#21468B"},
        {"RU", FlagKind::H3, "#FFFFFF", "#0039A6", "#D52B1E"},
        {"AT", FlagKind::H3, "#ED2939", "#FFFFFF", "#ED2939"},
        {"HU", FlagKind::H3, "#CE2939", "#FFFFFF", "#477050"},
        {"BG", FlagKind::H3, "#FFFFFF", "#00966E", "#D62612"},
        {"LT", FlagKind::H3, "#FDB913", "#006A44", "#C1272D"},
        {"EE", FlagKind::H3, "#0072CE", "#000000", "#FFFFFF"},
        {"LU", FlagKind::H3, "#EA141D", "#FFFFFF", "#51ADDA"},
        {"AM", FlagKind::H3, "#D90012", "#0033A0", "#F2A800"},
        {"LV", FlagKind::H3, "#9E3039", "#FFFFFF", "#9E3039"},
        {"ES", FlagKind::H3, "#AA151B", "#F1BF00", "#AA151B"},
        {"PL", FlagKind::H2, "#FFFFFF", "#DC143C", nullptr},
        {"UA", FlagKind::H2, "#0057B7", "#FFD700", nullptr},
        {"FR", FlagKind::V3, "#0055A4", "#FFFFFF", "#EF4135"},
        {"IT", FlagKind::V3, "#009246", "#FFFFFF", "#CE2B37"},
        {"BE", FlagKind::V3, "#000000", "#FDDA24", "#EF3340"},
        {"IE", FlagKind::V3, "#169B62", "#FFFFFF", "#FF883E"},
        {"RO", FlagKind::V3, "#002B7F", "#FCD116", "#CE1126"},
        {"MD", FlagKind::V3, "#0046AE", "#FFD200", "#CC092F"},
        {"CA", FlagKind::V3, "#D80621", "#FFFFFF", "#D80621"},
        {"SE", FlagKind::Cross, "#006AA7", "#FECC00", nullptr},
        {"FI", FlagKind::Cross, "#FFFFFF", "#002F6C", nullptr},
        {"DK", FlagKind::Cross, "#C8102E", "#FFFFFF", nullptr},
        {"NO", FlagKind::Cross, "#BA0C2F", "#00205B", nullptr},
        {"JP", FlagKind::Circle, "#FFFFFF", "#BC002D", nullptr},
        {"CH", FlagKind::SwissCross, "#DA291C", "#FFFFFF", nullptr},
    };

    static void drawFlag(QPainter &p, const QRectF &r, const QString &code) {
        const FlagSpec *spec = nullptr;
        for (const auto &f: kFlags) {
            if (code == f.code) spec = &f;
        }
        p.save();
        p.setPen(Qt::NoPen);
        QPainterPath clip;
        clip.addRoundedRect(r, 2, 2);
        p.setClipPath(clip);
        if (spec == nullptr) {
            // unknown flag: slanted badge with the code
            p.setBrush(kBlue);
            p.drawRect(r);
            p.setBrush(kCyan);
            p.drawPolygon(slanted(QRectF(r.left(), r.top(), 6, r.height()), 3));
            QFont f("Segoe UI");
            f.setBold(true);
            f.setItalic(true);
            f.setPixelSize(int(r.height() * 0.72));
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(r.adjusted(4, 0, 0, 0), Qt::AlignCenter, code);
        } else {
            QColor a(spec->c1), b(spec->c2), c(spec->c3 ? spec->c3 : spec->c1);
            switch (spec->kind) {
                case FlagKind::H3:
                    p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() / 3), a);
                    p.fillRect(QRectF(r.left(), r.top() + r.height() / 3, r.width(), r.height() / 3), b);
                    p.fillRect(QRectF(r.left(), r.top() + 2 * r.height() / 3, r.width(), r.height() / 3 + 0.5), c);
                    break;
                case FlagKind::H2:
                    p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() / 2), a);
                    p.fillRect(QRectF(r.left(), r.top() + r.height() / 2, r.width(), r.height() / 2 + 0.5), b);
                    break;
                case FlagKind::V3:
                    p.fillRect(QRectF(r.left(), r.top(), r.width() / 3, r.height()), a);
                    p.fillRect(QRectF(r.left() + r.width() / 3, r.top(), r.width() / 3, r.height()), b);
                    p.fillRect(QRectF(r.left() + 2 * r.width() / 3, r.top(), r.width() / 3 + 0.5, r.height()), c);
                    break;
                case FlagKind::Cross: {
                    p.fillRect(r, a);
                    qreal t = r.height() * 0.22;
                    p.fillRect(QRectF(r.left(), r.center().y() - t / 2, r.width(), t), b);
                    p.fillRect(QRectF(r.left() + r.width() * 0.32, r.top(), t, r.height()), b);
                    break;
                }
                case FlagKind::Circle:
                    p.fillRect(r, a);
                    p.setBrush(b);
                    p.drawEllipse(r.center(), r.height() * 0.3, r.height() * 0.3);
                    break;
                case FlagKind::SwissCross: {
                    p.fillRect(r, a);
                    qreal t = r.height() * 0.2, l = r.height() * 0.62;
                    p.fillRect(QRectF(r.center().x() - l / 2, r.center().y() - t / 2, l, t), b);
                    p.fillRect(QRectF(r.center().x() - t / 2, r.center().y() - l / 2, t, l), b);
                    break;
                }
                default:
                    break;
            }
        }
        p.restore();
        p.setPen(QPen(QColor(255, 255, 255, 70), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    }

    static void drawStar(QPainter &p, const QRectF &r, bool filled) {
        QPolygonF star;
        auto c = r.center();
        qreal R = r.width() / 2, rr = R * 0.45;
        for (int i = 0; i < 10; i++) {
            const qreal pi = 3.14159265358979323846;
            qreal a = -pi / 2 + i * pi / 5;
            qreal rad = (i % 2 == 0) ? R : rr;
            star << QPointF(c.x() + rad * std::cos(a), c.y() + rad * std::sin(a));
        }
        if (filled) {
            p.setPen(QPen(QColor("#B8860B"), 1));
            p.setBrush(QColor("#FFD23F"));
        } else {
            p.setPen(QPen(QColor(159, 195, 230, 110), 1.2));
            p.setBrush(Qt::NoBrush);
        }
        p.drawPolygon(star);
    }

    QPixmap NameIcon(const QString &code, bool favorite, qreal dpr) {
        QPixmap pm(QSize(46, 16) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        drawStar(p, QRectF(1, 1, 14, 14), favorite);
        if (!code.isEmpty()) drawFlag(p, QRectF(21, 2, 22, 12), code);
        return pm;
    }

    // ======================================================== empty state

    EmptyState::EmptyState(QWidget *parentToCover) : QWidget(parentToCover) {
        setAttribute(Qt::WA_StyledBackground, false);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(40, 30, 40, 30);
        layout->addStretch();
        auto title = new QLabel(tr("No servers yet"), this);
        title->setStyleSheet("font-size:20pt; font-weight:900; font-style:italic; color:#FFFFFF; background:transparent;");
        layout->addWidget(title);
        auto steps = new QLabel(
            "<p style='line-height:150%'>" +
                tr("<b style='color:#2EE6FF'>1.</b> Copy a server link (<i>vless://, vmess://, trojan://, ss://, hy2://</i>…) or a subscription link (<i>https://…</i>) from your provider.") + "<br>" +
                tr("<b style='color:#2EE6FF'>2.</b> Press the button below — the program takes it from the clipboard.") + "<br>" +
                tr("<b style='color:#2EE6FF'>3.</b> Press <b>CONNECT</b>.") + "</p>",
            this);
        steps->setWordWrap(true);
        steps->setTextFormat(Qt::RichText);
        steps->setStyleSheet("font-size:11pt; color:#EAF6FF; background:transparent;");
        layout->addWidget(steps);
        layout->addSpacing(10);
        auto button = new QPushButton(QString::fromUtf8("\uFF0B  ") + tr("PASTE LINK OR SUBSCRIPTION"), this);
        button->setObjectName("p3rPasteBig");
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(52);
        button->setStyleSheet("QPushButton#p3rPasteBig{font-size:14pt;font-weight:900;font-style:italic;color:#061026;"
                              "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #FFFFFF,stop:0.7 #EAF6FF,stop:1 #2EE6FF);"
                              "border:none;border-left:8px solid #2EE6FF;padding:6px 24px;min-height:40px;}"
                              "QPushButton#p3rPasteBig:hover{border-left-color:#FF2B3B;background:#FFFFFF;}");
        connect(button, &QPushButton::clicked, this, [=] { emit pasteClicked(); });
        layout->addWidget(button, 0, Qt::AlignLeft);
        layout->addStretch();
        parentToCover->installEventFilter(this);
        setGeometry(parentToCover->rect());
    }

    bool EmptyState::eventFilter(QObject *obj, QEvent *event) {
        if (obj == parentWidget() && event->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
        return false;
    }

    void EmptyState::paintEvent(QPaintEvent *) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), withAlpha(kNavy, 245));
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(kBlue, 90));
        p.drawPolygon(slanted(QRectF(width() * 0.55, 0, width() * 0.5, height()), height() * 0.35));
        // thin cyan stripe parallel to the block edge
        qreal x0 = width() * 0.55, sk = height() * 0.35;
        QPolygonF stripe;
        stripe << QPointF(x0 + sk - 22, 0) << QPointF(x0 + sk - 12, 0) << QPointF(x0 - 12, height()) << QPointF(x0 - 22, height());
        p.setBrush(withAlpha(kCyan, 70));
        p.drawPolygon(stripe);
    }

    // ======================================================== splash

    Splash::Splash(const QPixmap &icon) : QWidget(nullptr), m_icon(icon) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setCursor(Qt::PointingHandCursor);
        if (auto screen = QGuiApplication::primaryScreen()) setGeometry(screen->geometry());
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(2300);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [=](const QVariant &v) {
            m_t = v.toReal();
            if (m_t >= 0.68 && !m_revealed) {
                m_revealed = true;
                emit reveal(); // show the main window under the fading splash
            }
            update();
        });
        connect(m_anim, &QVariantAnimation::finished, this, [=] {
            if (!m_revealed) {
                m_revealed = true;
                emit reveal();
            }
            close();
        });
    }

    void Splash::start() {
        show();
        raise();
        m_anim->start();
    }

    void Splash::mousePressEvent(QMouseEvent *) {
        // skip: jump to the fade-out
        if (m_anim->state() == QAbstractAnimation::Running && m_anim->currentTime() < 1550) m_anim->setCurrentTime(1550);
    }

    static qreal smooth(qreal a, qreal b, qreal x) {
        qreal t = qBound(0.0, (x - a) / (b - a), 1.0);
        return t * t * (3 - 2 * t);
    }

    void Splash::paintEvent(QPaintEvent *) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setPen(Qt::NoPen);
        const qreal t = m_t;
        const qreal in = smooth(0.0, 0.18, t), out = smooth(0.68, 1.0, t);
        const qreal alpha = in * (1 - out);
        const QPointF c = rect().center();
        const qreal w = width(), h = height();

        // backdrop
        p.fillRect(rect(), withAlpha(kDeep, int(225 * alpha)));
        QRadialGradient glow(c, qMin(w, h) * 0.45);
        glow.setColorAt(0, withAlpha(kCyan, int(90 * alpha)));
        glow.setColorAt(1, withAlpha(kCyan, 0));
        p.setBrush(glow);
        p.drawEllipse(c, qMin(w, h) * 0.45, qMin(w, h) * 0.45);

        // slanted P3R bars sweeping across
        qreal sweep = smooth(0.05, 0.75, t);
        qreal skew = h * 0.25;
        p.setBrush(withAlpha(kBlue, int(150 * alpha)));
        p.drawPolygon(slanted(QRectF(-w + sweep * w * 1.6, c.y() - 70, w * 0.9, 140), skew * 0.3));
        p.setBrush(withAlpha(kCyan, int(200 * alpha)));
        p.drawPolygon(slanted(QRectF(w - sweep * w * 1.7, c.y() + 80, w * 0.6, 10), 8));
        p.setBrush(QColor(255, 255, 255, int(180 * alpha)));
        p.drawPolygon(slanted(QRectF(-w * 0.5 + sweep * w * 1.5, c.y() - 92, w * 0.5, 4), 4));

        // ring
        qreal size = qMin(w, h) * 0.26 * (0.6 + 0.4 * in + 0.35 * out);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(withAlpha(kCyan, int(200 * alpha)), 3));
        p.drawEllipse(c, size * 0.62, size * 0.62);
        p.setPen(Qt::NoPen);

        // spinning icon: two turns, slowing down at the end
        qreal spin = smooth(0.08, 0.72, t) * 720.0;
        p.save();
        p.setOpacity(alpha);
        p.translate(c);
        p.rotate(spin);
        QRectF target(-size / 2, -size / 2, size, size);
        p.drawPixmap(target, m_icon, QRectF(m_icon.rect()));
        p.restore();
    }

    // ======================================================== about

    AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle(tr("About NekoBoxP3R"));
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(640, 360);

        auto root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 18, 14);
        root->setSpacing(0);

        auto iconLabel = new QLabel;
        iconLabel->setFixedWidth(180);
        iconLabel->setAlignment(Qt::AlignCenter);
        QPixmap icon(":/neko/nekobox.png");
        iconLabel->setPixmap(icon.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setStyleSheet("background: transparent;");
        root->addWidget(iconLabel);
        root->addSpacing(60); // keep text clear of the slanted stripes

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
        body->setText(body->text().replace("<a href=", "<a style=\"color:#2EE6FF; font-weight:600;\" href="));
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
        bg.setColorAt(0, gDarkHour ? QColor("#0F3A17") : QColor("#0E2D5E"));
        bg.setColorAt(0.6, kNavy);
        bg.setColorAt(1, kDeep);
        p.fillRect(rect(), bg);

        // slanted panel behind the icon; everything stays left of x = 210 (text starts at 240)
        const qreal lean = 50; // how far the top edge is shifted to the right
        auto band = [&](qreal xBottom, qreal w, const QColor &c) {
            QPolygonF poly;
            poly << QPointF(xBottom + lean, 0) << QPointF(xBottom + lean + w, 0)
                 << QPointF(xBottom + w, h) << QPointF(xBottom, h);
            p.setBrush(c);
            p.drawPolygon(poly);
        };
        band(-lean, 180, kBlue);    // panel: top 0..180, bottom -50..130
        band(136, 12, kCyan);       // cyan stripe: top 186..198
        band(153, 3, Qt::white);    // thin white line: top 203..206
        p.setBrush(kRed);
        p.drawPolygon(slanted(QRectF(0, h - 28, 150, 10), 8));
    }

} // namespace P3R
