/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#include "kpToolArrow.h"

#include "environments/tools/kpToolEnvironment.h"
#include "imagelib/kpColor.h"
#include "widgets/toolbars/kpToolToolBar.h"

#include <KLocalizedString>

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QVector>

#include <cmath>

//---------------------------------------------------------------------

kpToolArrow::kpToolArrow(kpToolEnvironment *environ, QObject *parent)
    : kpToolRectangularBase(i18n("Arrow"), i18n("Draws block arrows"), &kpToolArrow::drawRightArrow, Qt::Key_W, environ, parent, QStringLiteral("tool_arrow"))
{
}

//---------------------------------------------------------------------

// public static
QPolygonF kpToolArrow::arrowPolygon(const QRectF &rect, kpToolWidgetArrowDirection::Direction direction)
{
    const bool horizontal = (direction == kpToolWidgetArrowDirection::Right || direction == kpToolWidgetArrowDirection::Left);
    const qreal length = horizontal ? rect.width() : rect.height();
    const qreal breadth = horizontal ? rect.height() : rect.width();
    const qreal head = qMin(length, breadth) / 2;
    const qreal neck = length - head;

    // (u, v) points of an arrow pointing along +u, v going across the arrow.
    const QPointF uv[] = {
        {0, breadth / 4},
        {neck, breadth / 4},
        {neck, 0},
        {length, breadth / 2},
        {neck, breadth},
        {neck, breadth * 3 / 4},
        {0, breadth * 3 / 4},
    };

    QPolygonF polygon;
    for (const QPointF &p : uv) {
        switch (direction) {
        default:
        case kpToolWidgetArrowDirection::Right:
            polygon << QPointF(rect.left() + p.x(), rect.top() + p.y());
            break;
        case kpToolWidgetArrowDirection::Left:
            polygon << QPointF(rect.right() - p.x(), rect.top() + p.y());
            break;
        case kpToolWidgetArrowDirection::Down:
            polygon << QPointF(rect.left() + p.y(), rect.top() + p.x());
            break;
        case kpToolWidgetArrowDirection::Up:
            polygon << QPointF(rect.left() + p.y(), rect.bottom() - p.x());
            break;
        }
    }
    return polygon;
}

//---------------------------------------------------------------------

// public static
QPolygonF kpToolArrow::insetPolygon(const QPolygonF &polygon, qreal distance)
{
    const int n = polygon.size();
    if (n < 3) {
        return {};
    }

    // The sign of the (doubled) signed area tells on which side of its edges
    // the inside of the polygon is.
    qreal area2 = 0;
    for (int i = 0; i < n; i++) {
        const QPointF &a = polygon[i];
        const QPointF &b = polygon[(i + 1) % n];
        area2 += a.x() * b.y() - b.x() * a.y();
    }
    if (qFuzzyIsNull(area2)) {
        return {};
    }
    const qreal side = (area2 > 0) ? 1 : -1;

    // Every edge moved inwards, as a point on it and its unit direction.
    QVector<QPointF> points, directions;
    for (int i = 0; i < n; i++) {
        const QPointF edge = polygon[(i + 1) % n] - polygon[i];
        const qreal length = std::hypot(edge.x(), edge.y());
        if (qFuzzyIsNull(length)) {
            return {};
        }
        directions.append(edge / length);
        points.append(polygon[i] + side * QPointF(-directions.last().y(), directions.last().x()) * distance);
    }

    // Moving the edges inwards can make short ones vanish (they then point
    // backwards between their neighbours); drop those and let the neighbours
    // meet directly, until every edge that is left points the right way.
    for (;;) {
        const int count = points.size();
        if (count < 3) {
            return {};
        }

        // Each corner is where the moved edges on either side of it meet.
        QPolygonF result(count);
        for (int i = 0; i < count; i++) {
            const int prev = (i + count - 1) % count;
            const QPointF &r = directions[prev];
            const QPointF &s = directions[i];
            const qreal cross = r.x() * s.y() - r.y() * s.x();
            if (qFuzzyIsNull(cross)) {
                return {};
            }
            const QPointF qp = points[i] - points[prev];
            const qreal t = (qp.x() * s.y() - qp.y() * s.x()) / cross;
            result[i] = points[prev] + r * t;
        }

        QVector<QPointF> keptPoints, keptDirections;
        for (int i = 0; i < count; i++) {
            if (QPointF::dotProduct(result[(i + 1) % count] - result[i], directions[i]) > 0) {
                keptPoints.append(points[i]);
                keptDirections.append(directions[i]);
            }
        }
        if (keptPoints.size() == count) {
            return result;
        }
        points = keptPoints;
        directions = keptDirections;
    }
}

//---------------------------------------------------------------------

// private static
void kpToolArrow::drawArrow(kpImage *image,
                            const QRect &rect,
                            kpToolWidgetArrowDirection::Direction direction,
                            const kpColor &fcolor,
                            int penWidth,
                            const kpColor &bcolor)
{
    if (rect.width() <= 0 || rect.height() <= 0) {
        return;
    }

    // The outline is the band between <outer> and <inner>, filled rather than
    // stroked with a pen: this keeps the corners mitred and the arrow exactly
    // inside <rect>, like the other shapes, even when the arrow is tiny.
    const QPolygonF outer = arrowPolygon(QRectF(rect), direction);
    const QPolygonF inner = insetPolygon(outer, penWidth);
    // The fill reaches under the middle of the outline so that no gap shows
    // between the two when anti-aliasing.
    const QPolygonF fill = insetPolygon(outer, penWidth / 2.0);

    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing, kpToolEnvironment::drawAntiAliased);
    painter.setPen(Qt::NoPen);

    if (bcolor.isValid() && !fill.isEmpty()) {
        painter.setBrush(QBrush(bcolor.toQColor()));
        painter.drawPolygon(fill);
    }

    QPainterPath outline;
    outline.addPolygon(outer);
    outline.closeSubpath();
    if (!inner.isEmpty()) {
        outline.addPolygon(inner);
        outline.closeSubpath();
    }
    painter.setBrush(QBrush(fcolor.toQColor()));
    painter.drawPath(outline);
}

//---------------------------------------------------------------------

// public static
void kpToolArrow::drawRightArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor)
{
    drawArrow(image, QRect(x, y, width, height), kpToolWidgetArrowDirection::Right, fcolor, penWidth, bcolor);
}

// public static
void kpToolArrow::drawLeftArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor)
{
    drawArrow(image, QRect(x, y, width, height), kpToolWidgetArrowDirection::Left, fcolor, penWidth, bcolor);
}

// public static
void kpToolArrow::drawUpArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor)
{
    drawArrow(image, QRect(x, y, width, height), kpToolWidgetArrowDirection::Up, fcolor, penWidth, bcolor);
}

// public static
void kpToolArrow::drawDownArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor)
{
    drawArrow(image, QRect(x, y, width, height), kpToolWidgetArrowDirection::Down, fcolor, penWidth, bcolor);
}

//---------------------------------------------------------------------

// public virtual [base kpToolRectangularBase]
void kpToolArrow::begin()
{
    kpToolRectangularBase::begin();

    m_toolWidgetArrowDirection = toolToolBar()->toolWidgetArrowDirection();
    connect(m_toolWidgetArrowDirection, &kpToolWidgetArrowDirection::directionChanged, this, &kpToolArrow::slotDirectionChanged);
    m_toolWidgetArrowDirection->show();
}

//---------------------------------------------------------------------

// public virtual [base kpToolRectangularBase]
void kpToolArrow::end()
{
    if (m_toolWidgetArrowDirection) {
        disconnect(m_toolWidgetArrowDirection, &kpToolWidgetArrowDirection::directionChanged, this, &kpToolArrow::slotDirectionChanged);
        m_toolWidgetArrowDirection = nullptr;
    }

    kpToolRectangularBase::end();
}

//---------------------------------------------------------------------

// protected virtual [base kpToolRectangularBase]
kpToolRectangularBase::DrawShapeFunc kpToolArrow::drawShapeFunc() const
{
    const auto direction = m_toolWidgetArrowDirection ? m_toolWidgetArrowDirection->direction() : kpToolWidgetArrowDirection::Right;

    switch (direction) {
    default:
    case kpToolWidgetArrowDirection::Right:
        return &kpToolArrow::drawRightArrow;
    case kpToolWidgetArrowDirection::Left:
        return &kpToolArrow::drawLeftArrow;
    case kpToolWidgetArrowDirection::Up:
        return &kpToolArrow::drawUpArrow;
    case kpToolWidgetArrowDirection::Down:
        return &kpToolArrow::drawDownArrow;
    }
}

//---------------------------------------------------------------------

// private slot
void kpToolArrow::slotDirectionChanged()
{
    if (hasBegunDraw()) {
        updateShape();
    }
}

//---------------------------------------------------------------------

#include "moc_kpToolArrow.cpp"
