/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef KP_TOOL_ARROW_H
#define KP_TOOL_ARROW_H

#include "tools/rectangular/kpToolRectangularBase.h"
#include "widgets/toolbars/options/kpToolWidgetArrowDirection.h"

#include <QPolygonF>

class QRectF;

class kpToolArrow : public kpToolRectangularBase
{
    Q_OBJECT

public:
    kpToolArrow(kpToolEnvironment *environ, QObject *parent);

    // The block arrow that fills <rect> and points in <direction>.
    // The proportions are those of the MS Paint and Office arrow shapes:
    // the shaft is half as thick as the arrow, and the head is half as long
    // as the shorter side of <rect>.
    static QPolygonF arrowPolygon(const QRectF &rect, kpToolWidgetArrowDirection::Direction direction);

    // Moves every edge of <polygon> inwards by <distance>, keeping the corners
    // sharp. Edges that this squeezes away are dropped. Returns an empty
    // polygon if nothing is left inside.
    static QPolygonF insetPolygon(const QPolygonF &polygon, qreal distance);

    // One function per direction, so that the kpToolRectangularCommand that
    // stores the function redraws the same arrow on undo/redo.
    static void drawRightArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor);
    static void drawLeftArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor);
    static void drawUpArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor);
    static void drawDownArrow(kpImage *image, int x, int y, int width, int height, const kpColor &fcolor, int penWidth, const kpColor &bcolor);

    void begin() override;
    void end() override;

protected:
    DrawShapeFunc drawShapeFunc() const override;

private Q_SLOTS:
    void slotDirectionChanged();

private:
    static void
    drawArrow(kpImage *image, const QRect &rect, kpToolWidgetArrowDirection::Direction direction, const kpColor &fcolor, int penWidth, const kpColor &bcolor);

    kpToolWidgetArrowDirection *m_toolWidgetArrowDirection = nullptr;
};

#endif // KP_TOOL_ARROW_H
