/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#include "kpToolWidgetArrowDirection.h"

#include "tools/rectangular/kpToolArrow.h"

#include <KLocalizedString>

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

//---------------------------------------------------------------------

kpToolWidgetArrowDirection::kpToolWidgetArrowDirection(QWidget *parent, const QString &name)
    : kpToolWidgetBase(parent, name)
{
    for (int i = 0; i < DirectionNum; i++) {
        const auto dir = static_cast<Direction>(i);
        addOption(directionPixmap(dir), directionName(dir) /*tooltip*/);

        if (i % 2 == 1) {
            startNewOptionRow();
        }
    }

    finishConstruction(0, 0);
}

//---------------------------------------------------------------------

kpToolWidgetArrowDirection::~kpToolWidgetArrowDirection() = default;

//---------------------------------------------------------------------

// private
QPixmap kpToolWidgetArrowDirection::directionPixmap(Direction direction) const
{
    // kpToolWidgetBase::relayoutOptions() spreads the options out to fill the
    // widget, so pixmaps of this size give the 2x2 grid cells of equal size.
    QPixmap pixmap(19, 30);
    pixmap.fill(Qt::transparent);

    const bool horizontal = (direction == Right || direction == Left);
    const QRectF rect = horizontal ? QRectF(1, 9, 18, 12) : QRectF(3, 6, 12, 18);
    const QPolygonF outer = kpToolArrow::arrowPolygon(rect, direction);
    const QPolygonF inner = kpToolArrow::insetPolygon(outer, 1);

    // Like the tool icons: a solid outline with a half-transparent inside.
    // Black like the other options, which kpToolWidgetBase recolors.
    const QColor color = Qt::black;
    QColor fillColor = color;
    fillColor.setAlphaF(0.5);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    painter.setBrush(fillColor);
    painter.drawPolygon(kpToolArrow::insetPolygon(outer, 0.5));

    QPainterPath outline;
    outline.addPolygon(outer);
    outline.closeSubpath();
    outline.addPolygon(inner);
    outline.closeSubpath();
    painter.setBrush(color);
    painter.drawPath(outline);
    painter.end();

    return pixmap;
}

//---------------------------------------------------------------------

// private
QString kpToolWidgetArrowDirection::directionName(Direction direction) const
{
    switch (direction) {
    case Right:
        return i18n("Right Arrow");

    case Left:
        return i18n("Left Arrow");

    case Up:
        return i18n("Up Arrow");

    case Down:
        return i18n("Down Arrow");

    default:
        return {};
    }
}

//---------------------------------------------------------------------

// public
kpToolWidgetArrowDirection::Direction kpToolWidgetArrowDirection::direction() const
{
    const int option = selected();
    if (option < 0 || option >= DirectionNum) {
        return Right;
    }
    return static_cast<Direction>(option);
}

//---------------------------------------------------------------------

// virtual protected slot [base kpToolWidgetBase]
bool kpToolWidgetArrowDirection::setSelected(int row, int col, bool saveAsDefault)
{
    const bool ret = kpToolWidgetBase::setSelected(row, col, saveAsDefault);
    if (ret) {
        Q_EMIT directionChanged(direction());
    }
    return ret;
}

//---------------------------------------------------------------------

#include "moc_kpToolWidgetArrowDirection.cpp"
