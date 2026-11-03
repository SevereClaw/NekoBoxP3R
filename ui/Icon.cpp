#include "Icon.hpp"

#include "main/NekoGui.hpp"

#include <QPainter>

QPixmap Icon::GetTrayIcon(Icon::TrayIconStatus status) {
    QPixmap pixmap;

    // software embedded icon
    auto pixmap_read = QPixmap(":/neko/" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    // software pack icon
    pixmap_read = QPixmap("../" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    // user icon
    pixmap_read = QPixmap("./" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    if (status == TrayIconStatus::NONE) return pixmap;

    auto p = QPainter(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    // NekoBoxP3R: big status badge with a navy + white outline, so it stays
    // visible on top of the red ring of the icon even at 16x16
    auto side = pixmap.width();
    qreal d = side * 0.46;
    qreal margin = side * 0.02;
    QRectF badge(side - d - margin, side - d - margin, d, d);

    QColor fill;
    if (status == TrayIconStatus::RUNNING) {
        fill = QColor("#22DD66"); // green: connected
    } else if (status == TrayIconStatus::SYSTEM_PROXY) {
        fill = QColor("#2EE6FF"); // cyan: system proxy
    } else if (status == TrayIconStatus::VPN) {
        fill = QColor("#FFC21A"); // amber: VPN (TUN) - not red, so it does not merge with the ring
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#061026"));
    p.drawEllipse(badge);
    p.setBrush(Qt::white);
    p.drawEllipse(badge.adjusted(d * 0.12, d * 0.12, -d * 0.12, -d * 0.12));
    p.setBrush(fill);
    p.drawEllipse(badge.adjusted(d * 0.22, d * 0.22, -d * 0.22, -d * 0.22));
    p.end();

    return pixmap;
}

QPixmap Icon::GetMaterialIcon(const QString &name) {
    QPixmap pixmap(":/icon/material/" + name + ".svg");
    return pixmap;
}
