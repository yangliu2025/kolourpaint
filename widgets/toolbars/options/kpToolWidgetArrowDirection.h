/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef KP_TOOL_WIDGET_ARROW_DIRECTION_H
#define KP_TOOL_WIDGET_ARROW_DIRECTION_H

#include "kpToolWidgetBase.h"

class kpToolWidgetArrowDirection : public kpToolWidgetBase
{
    Q_OBJECT

public:
    kpToolWidgetArrowDirection(QWidget *parent, const QString &name);
    ~kpToolWidgetArrowDirection() override;

    // The options are laid out row by row in this order, 2 per row.
    enum Direction {
        Right,
        Left,
        Up,
        Down,
        DirectionNum /* not (a valid Direction) */
    };

    Direction direction() const;

private:
    QPixmap directionPixmap(Direction direction) const;
    QString directionName(Direction direction) const;

Q_SIGNALS:
    void directionChanged(kpToolWidgetArrowDirection::Direction direction);

protected Q_SLOTS:
    bool setSelected(int row, int col, bool saveAsDefault) override;
};

#endif // KP_TOOL_WIDGET_ARROW_DIRECTION_H
