/*
   SPDX-FileCopyrightText: 2003-2007 Clarence Dang <dang@kde.org>
   SPDX-FileCopyrightText: 2011 Martin Koller <kollix@aon.at>

   SPDX-License-Identifier: BSD-2-Clause
*/

#define DEBUG_KP_VIEW_SCROLLABLE_CONTAINER 0

#include "kpViewScrollableContainer.h"

#include <QCursor>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>

#include "kpLogCategories.h"
#include <KLocalizedString>

#include "document/kpDocument.h"
#include "generic/kpModernLook.h"
#include "generic/kpWidgetMapper.h"
#include "kpDefs.h"
#include "pixmapfx/kpPixmapFX.h"
#include "views/kpView.h"

//---------------------------------------------------------------------

// (Pulled from out of Thurston's hat)
static const int DragScrollLeftTopMargin = 0;
static const int DragScrollRightBottomMargin = 16; // scrollbarish
static const int DragScrollInterval = 150;
static const int DragScrollInitialInterval = DragScrollInterval * 2;
static const int DragScrollNumPixels = 10;
static const int DragDistanceFromRectMaxFor1stMultiplier = 50;
static const int DragDistanceFromRectMaxFor2ndMultiplier = 100;

//---------------------------------------------------------------------
//---------------------------------------------------------------------
// a transparent widget above all others in the viewport used only while resizing the document
// to be able to show the resize lines above everything else

class kpOverlay : public QWidget
{
public:
    kpOverlay(QWidget *parent, kpViewScrollableContainer *container)
        : QWidget(parent)
        , m_container(container)
    {
    }

    void paintEvent(QPaintEvent *) override
    {
        m_container->drawResizeLines();
    }

private:
    kpViewScrollableContainer *m_container;
};

//---------------------------------------------------------------------

const int kpGrip::Size = 5;

//---------------------------------------------------------------------

kpGrip::kpGrip(GripType type, QWidget *parent)
    : QWidget(parent)
    , m_type(type)
    , m_startPoint(KP_INVALID_POINT)
    , m_currentPoint(KP_INVALID_POINT)
    , m_shouldReleaseMouseButtons(false)
{
    setCursor(cursorForType(m_type));

    setMouseTracking(true); // mouseMoveEvent's even when no mousebtn down

    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Highlight);

    setFixedSize(kpGrip::Size, kpGrip::Size);
}

//---------------------------------------------------------------------

// public
kpGrip::GripType kpGrip::type() const
{
    return m_type;
}

//---------------------------------------------------------------------

// public static
QCursor kpGrip::cursorForType(GripType type)
{
    switch (type) {
    case kpGrip::Top:
    case kpGrip::Bottom:
        return Qt::SizeVerCursor;

    case kpGrip::Left:
    case kpGrip::Right:
        return Qt::SizeHorCursor;

    case kpGrip::TopLeft:
    case kpGrip::BottomRight:
        return Qt::SizeFDiagCursor;

    case kpGrip::TopRight:
    case kpGrip::BottomLeft:
        return Qt::SizeBDiagCursor;
    }

    return Qt::ArrowCursor;
}

//---------------------------------------------------------------------

// public
bool kpGrip::containsCursor()
{
    return isVisible() && QRect(mapToGlobal(rect().topLeft()), mapToGlobal(rect().bottomRight())).contains(QCursor::pos());
}

//---------------------------------------------------------------------

// public
bool kpGrip::isDrawing() const
{
    return (m_startPoint != KP_INVALID_POINT);
}

//---------------------------------------------------------------------

// public
QString kpGrip::haventBegunDrawUserMessage() const
{
    return i18n("Left drag the handle to resize the image.");
}

//---------------------------------------------------------------------

// public
QString kpGrip::userMessage() const
{
    return m_userMessage;
}

//---------------------------------------------------------------------

// public
void kpGrip::setUserMessage(const QString &message)
{
    // Don't do NOP checking here since another grip might have changed
    // the message so an apparent NOP for this grip is not a NOP in the
    // global sense (kpViewScrollableContainer::slotGripStatusMessageChanged()).

    m_userMessage = message;
    Q_EMIT statusMessageChanged(message);
}

//---------------------------------------------------------------------

// protected
void kpGrip::cancel()
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpGrip::cancel()";
#endif
    if (m_currentPoint == KP_INVALID_POINT) {
        return;
    }

    m_startPoint = KP_INVALID_POINT;
    m_currentPoint = KP_INVALID_POINT;

    setUserMessage(i18n("Resize Image: Let go of all the mouse buttons."));
    setCursor(Qt::ArrowCursor);
    m_shouldReleaseMouseButtons = true;
    update();

    Q_EMIT cancelledDraw();
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::keyReleaseEvent(QKeyEvent *e)
{
    if (m_startPoint != KP_INVALID_POINT && e->key() == Qt::Key_Escape) {
        cancel();
    }
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::mousePressEvent(QMouseEvent *e)
{
    if (m_startPoint == KP_INVALID_POINT && (e->buttons() & Qt::MouseButtonMask) == Qt::LeftButton) {
        m_startPoint = e->pos();
        m_currentPoint = e->pos();
        Q_EMIT beganDraw();
        setFocus(); // allow to receive keyboard events to be able to handle ESC

        setUserMessage(i18n("Resize Image: Right click to cancel."));
        setCursor(cursorForType(m_type));
    } else {
        if (m_startPoint != KP_INVALID_POINT) {
            cancel();
        }
    }

    update();
}

//---------------------------------------------------------------------

// public
QPoint kpGrip::viewDeltaPoint() const
{
    if (m_startPoint == KP_INVALID_POINT) {
        return KP_INVALID_POINT;
    }

    const QPoint point = mapFromGlobal(QCursor::pos());

    // TODO: this is getting out of sync with m_currentPoint

    return {(m_type & (kpGrip::Left | kpGrip::Right)) ? point.x() - m_startPoint.x() : 0,
            (m_type & (kpGrip::Top | kpGrip::Bottom)) ? point.y() - m_startPoint.y() : 0};
}

//---------------------------------------------------------------------

// public
void kpGrip::mouseMovedTo(const QPoint &point, bool dueToDragScroll)
{
    if (m_startPoint == KP_INVALID_POINT) {
        return;
    }

    m_currentPoint = point;

    Q_EMIT continuedDraw(((m_type & (kpGrip::Left | kpGrip::Right)) ? point.x() - m_startPoint.x() : 0),
                         ((m_type & (kpGrip::Top | kpGrip::Bottom)) ? point.y() - m_startPoint.y() : 0),
                         dueToDragScroll);
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::mouseMoveEvent(QMouseEvent *e)
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpGrip::mouseMoveEvent() m_startPoint=" << m_startPoint << " stateAfter: buttons=" << (int *)(int)e->buttons();
#endif

    if (m_startPoint == KP_INVALID_POINT) {
        if ((e->buttons() & Qt::MouseButtonMask) == 0) {
            setUserMessage(haventBegunDrawUserMessage());
        }
        return;
    }

    mouseMovedTo(e->pos(), false /*not due to drag scroll*/);
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::mouseReleaseEvent(QMouseEvent *e)
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpGrip::mouseReleaseEvent() m_startPoint=" << m_startPoint << " stateAfter: buttons=" << (int *)(int)e->buttons();
#endif

    if (m_startPoint != KP_INVALID_POINT) {
        const int dx = m_currentPoint.x() - m_startPoint.x(), dy = m_currentPoint.y() - m_startPoint.y();

        m_currentPoint = KP_INVALID_POINT;
        m_startPoint = KP_INVALID_POINT;

        Q_EMIT endedDraw((m_type & (kpGrip::Left | kpGrip::Right)) ? dx : 0, (m_type & (kpGrip::Top | kpGrip::Bottom)) ? dy : 0);
    }

    if ((e->buttons() & Qt::MouseButtonMask) == 0) {
        m_shouldReleaseMouseButtons = false;
        setUserMessage(QString());
        setCursor(cursorForType(m_type));

        Q_EMIT releasedAllButtons();
    }

    update();
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void kpGrip::enterEvent(QEnterEvent * /*e*/)
#else
void kpGrip::enterEvent(QEvent * /*e*/)
#endif
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpGrip::enterEvent()"
                       << " m_startPoint=" << m_startPoint << " shouldReleaseMouseButtons=" << m_shouldReleaseMouseButtons;
#endif

    if (m_startPoint == KP_INVALID_POINT && !m_shouldReleaseMouseButtons) {
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
        qCDebug(kpLogMisc) << "\tsending message";
#endif
        setUserMessage(haventBegunDrawUserMessage());
    }

    update();
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::leaveEvent(QEvent * /*e*/)
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpGrip::leaveEvent()"
                       << " m_startPoint=" << m_startPoint << " shouldReleaseMouseButtons=" << m_shouldReleaseMouseButtons;
#endif
    if (m_startPoint == KP_INVALID_POINT && !m_shouldReleaseMouseButtons) {
        setUserMessage(QString());
    }

    update();
}

//---------------------------------------------------------------------

// protected virtual [base QWidget]
void kpGrip::paintEvent(QPaintEvent *e)
{
    if (!kpModernLook::isActive()) {
        QWidget::paintEvent(e);
        return;
    }

    // Only a small handle in the middle of the edge (or in the corner) is
    // drawn, but the whole strip still reacts to the mouse.
    QRect handle(0, 0, kpGrip::Size, kpGrip::Size);
    if (m_type == kpGrip::Left || m_type == kpGrip::Right) {
        handle.moveTop((height() - kpGrip::Size) / 2);
    } else if (m_type == kpGrip::Top || m_type == kpGrip::Bottom) {
        handle.moveLeft((width() - kpGrip::Size) / 2);
    }

    const bool hot = underMouse() || isDrawing();
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor border = kpModernLook::mix(kpModernLook::workspaceColor(palette()), palette().color(QPalette::WindowText), 0.6);

    QPainter p(this);
    p.setPen(hot ? accent : border);
    p.setBrush(hot ? accent : QColor(Qt::white));
    p.drawRect(handle.adjusted(0, 0, -1, -1));
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
//---------------------------------------------------------------------

// TODO: Are we checking for m_view == 0 often enough?  Also an issue in KDE 3.
kpViewScrollableContainer::kpViewScrollableContainer(QWidget *parent)
    : QScrollArea(parent)
    , m_view(nullptr)
    , m_overlay(new kpOverlay(viewport(), this))
    , m_docResizingGrip(nullptr)
    , m_dragScrollTimer(new QTimer(this))
    , m_zoomLevel(100)
    , m_scrollTimerRunOnce(false)
    , m_resizeRoundedLastViewLeft(0)
    , m_resizeRoundedLastViewTop(0)
    , m_resizeRoundedLastViewRight(-1)
    , m_resizeRoundedLastViewBottom(-1)
    , m_resizeRoundedLastViewDX(0)
    , m_resizeRoundedLastViewDY(0)
    , m_haveMovedFromOriginalDocSize(false)

{
    // the base widget holding the documents view plus the resize grips
    setWidget(new QWidget(viewport()));

    m_topLeftGrip = new kpGrip(kpGrip::TopLeft, widget());
    m_topGrip = new kpGrip(kpGrip::Top, widget());
    m_topRightGrip = new kpGrip(kpGrip::TopRight, widget());
    m_leftGrip = new kpGrip(kpGrip::Left, widget());
    m_rightGrip = new kpGrip(kpGrip::Right, widget());
    m_bottomLeftGrip = new kpGrip(kpGrip::BottomLeft, widget());
    m_bottomGrip = new kpGrip(kpGrip::Bottom, widget());
    m_bottomRightGrip = new kpGrip(kpGrip::BottomRight, widget());

    m_topLeftGrip->setObjectName(QStringLiteral("TopLeft Grip"));
    m_topGrip->setObjectName(QStringLiteral("Top Grip"));
    m_topRightGrip->setObjectName(QStringLiteral("TopRight Grip"));
    m_leftGrip->setObjectName(QStringLiteral("Left Grip"));
    m_bottomGrip->setObjectName(QStringLiteral("Bottom Grip"));
    m_rightGrip->setObjectName(QStringLiteral("Right Grip"));
    m_bottomLeftGrip->setObjectName(QStringLiteral("BottomLeft Grip"));
    m_bottomRightGrip->setObjectName(QStringLiteral("BottomRight Grip"));

    for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
        grip->hide();
        connectGripSignals(grip);
    }

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &kpViewScrollableContainer::slotContentsMoved);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &kpViewScrollableContainer::slotContentsMoved);

    connect(m_dragScrollTimer, &QTimer::timeout, this, [this] {
        slotDragScroll();
    });

    m_overlay->hide();

    connect(kpModernLook::instance(), &kpModernLook::changed, this, &kpViewScrollableContainer::applyLook);
    applyLook();
}

//---------------------------------------------------------------------

// protected
void kpViewScrollableContainer::connectGripSignals(kpGrip *grip)
{
    connect(grip, &kpGrip::beganDraw, this, &kpViewScrollableContainer::slotGripBeganDraw);

    connect(grip, &kpGrip::continuedDraw, this, &kpViewScrollableContainer::slotGripContinuedDraw);

    connect(grip, &kpGrip::cancelledDraw, this, &kpViewScrollableContainer::slotGripCancelledDraw);

    connect(grip, &kpGrip::endedDraw, this, &kpViewScrollableContainer::slotGripEndedDraw);

    connect(grip, &kpGrip::statusMessageChanged, this, &kpViewScrollableContainer::slotGripStatusMessageChanged);

    connect(grip, &kpGrip::releasedAllButtons, this, &kpViewScrollableContainer::recalculateStatusMessage);
}

//---------------------------------------------------------------------

// public
QSize kpViewScrollableContainer::newDocSize() const
{
    return newDocSize(m_resizeRoundedLastViewDX, m_resizeRoundedLastViewDY);
}

//---------------------------------------------------------------------

// public
bool kpViewScrollableContainer::haveMovedFromOriginalDocSize() const
{
    return m_haveMovedFromOriginalDocSize;
}

//---------------------------------------------------------------------

// public
QString kpViewScrollableContainer::statusMessage() const
{
    return m_gripStatusMessage;
}

//---------------------------------------------------------------------

// public
void kpViewScrollableContainer::clearStatusMessage()
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER && 1
    qCDebug(kpLogMisc) << "kpViewScrollableContainer::clearStatusMessage()";
#endif
    for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
        grip->setUserMessage(QString());
    }
}

//---------------------------------------------------------------------

// protected
QSize kpViewScrollableContainer::newDocSize(int viewDX, int viewDY) const
{
    if (!m_view) {
        return {};
    }

    if (!docResizingGrip()) {
        return {};
    }

    const auto type = docResizingGrip()->type();
    const int targetWidth = m_view->width() + ((type & kpGrip::Left) ? -viewDX : (type & kpGrip::Right) ? viewDX : 0);
    const int targetHeight = m_view->height() + ((type & kpGrip::Top) ? -viewDY : (type & kpGrip::Bottom) ? viewDY : 0);
    const int docX = static_cast<int>(m_view->transformViewToDocX(targetWidth));
    const int docY = static_cast<int>(m_view->transformViewToDocY(targetHeight));

    return {qMax(1, docX), qMax(1, docY)};
}

QPoint kpViewScrollableContainer::newDocOffset(int viewDX, int viewDY) const
{
    const QSize size = newDocSize(viewDX, viewDY);
    if (!m_view || !docResizingGrip() || !size.isValid()) {
        return {};
    }

    const auto type = docResizingGrip()->type();
    return {(type & kpGrip::Left) ? m_view->document()->width() - size.width() : 0,
            (type & kpGrip::Top) ? m_view->document()->height() - size.height() : 0};
}

//---------------------------------------------------------------------

// protected
void kpViewScrollableContainer::calculateDocResizingGrip()
{
    m_docResizingGrip = nullptr;
    for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
        if (grip->isDrawing()) {
            m_docResizingGrip = grip;
            break;
        }
    }
}

//---------------------------------------------------------------------

// protected
kpGrip *kpViewScrollableContainer::docResizingGrip() const
{
    return m_docResizingGrip;
}

//---------------------------------------------------------------------

// protected
int kpViewScrollableContainer::leftResizeLineWidth() const
{
    if (!docResizingGrip()) {
        return -1;
    }

    if (!m_view) {
        return -1;
    }

    if (docResizingGrip()->type() & kpGrip::Left) {
        return qMax(m_view->zoomLevelX() / 100, 1);
    }

    return 1;
}

//---------------------------------------------------------------------

// protected
int kpViewScrollableContainer::rightResizeLineWidth() const
{
    if (!docResizingGrip()) {
        return -1;
    }

    if (!m_view) {
        return -1;
    }

    if (docResizingGrip()->type() & kpGrip::Right) {
        return qMax(m_view->zoomLevelX() / 100, 1);
    }

    return 1;
}

//---------------------------------------------------------------------

// protected
int kpViewScrollableContainer::topResizeLineWidth() const
{
    if (!docResizingGrip() || !m_view) {
        return -1;
    }

    return (docResizingGrip()->type() & kpGrip::Top) ? qMax(m_view->zoomLevelY() / 100, 1) : 1;
}

//---------------------------------------------------------------------

// protected
int kpViewScrollableContainer::bottomResizeLineWidth() const
{
    if (!docResizingGrip() || !m_view) {
        return -1;
    }

    return (docResizingGrip()->type() & kpGrip::Bottom) ? qMax(m_view->zoomLevelY() / 100, 1) : 1;
}

//---------------------------------------------------------------------

// protected
QRect kpViewScrollableContainer::leftResizeLineRect() const
{
    if (m_resizeRoundedLastViewRight < 0 || m_resizeRoundedLastViewBottom < 0) {
        return {};
    }

    return {m_resizeRoundedLastViewLeft, m_resizeRoundedLastViewTop, leftResizeLineWidth(),
            m_resizeRoundedLastViewBottom - m_resizeRoundedLastViewTop};
}

//---------------------------------------------------------------------

// protected
QRect kpViewScrollableContainer::rightResizeLineRect() const
{
    if (m_resizeRoundedLastViewRight < 0 || m_resizeRoundedLastViewBottom < 0) {
        return {};
    }

    return {m_resizeRoundedLastViewRight, m_resizeRoundedLastViewTop, rightResizeLineWidth(),
            m_resizeRoundedLastViewBottom - m_resizeRoundedLastViewTop};
}

//---------------------------------------------------------------------

// protected
QRect kpViewScrollableContainer::topResizeLineRect() const
{
    if (m_resizeRoundedLastViewRight < 0 || m_resizeRoundedLastViewBottom < 0) {
        return {};
    }

    return {m_resizeRoundedLastViewLeft, m_resizeRoundedLastViewTop,
            m_resizeRoundedLastViewRight - m_resizeRoundedLastViewLeft, topResizeLineWidth()};
}

//---------------------------------------------------------------------

// protected
QRect kpViewScrollableContainer::bottomResizeLineRect() const
{
    if (m_resizeRoundedLastViewRight < 0 || m_resizeRoundedLastViewBottom < 0) {
        return {};
    }

    return {m_resizeRoundedLastViewLeft, m_resizeRoundedLastViewBottom,
            m_resizeRoundedLastViewRight - m_resizeRoundedLastViewLeft, bottomResizeLineWidth()};
}

//---------------------------------------------------------------------

// private
QRect kpViewScrollableContainer::mapViewToViewport(const QRect &viewRect)
{
    if (!viewRect.isValid()) {
        return {};
    }

    // The overlay is a child of the viewport, placed at viewport()->pos().
    QRect ret = viewRect;
    ret.translate(widget()->pos() + m_view->pos() - viewport()->pos());
    return ret;
}

//---------------------------------------------------------------------

void kpViewScrollableContainer::drawResizeLines()
{
    static const char *stipple[] =
        {"8 8 2 1", ". c #000000", "# c #ffffff", "....####", "....####", "....####", "....####", "####....", "####....", "####....", "####...."};

    QPainter p(m_overlay);
    p.setBackground(QPixmap(stipple));

    for (const QRect &lineRect : {leftResizeLineRect(), topResizeLineRect(), rightResizeLineRect(), bottomResizeLineRect()}) {
        if (lineRect.isValid()) {
            QRect rect = mapViewToViewport(lineRect);
            p.setBrushOrigin(rect.x(), rect.y());
            p.eraseRect(rect);
        }
    }
}

//---------------------------------------------------------------------

// protected
void kpViewScrollableContainer::updateResizeLines(int viewDX, int viewDY)
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER && 0
    qCDebug(kpLogMisc) << "kpViewScrollableContainer::updateResizeLines()"
                       << " viewDX=" << viewDX << " viewDY=" << viewDY;
#endif

    if (m_view && docResizingGrip()) {
        const QSize size = newDocSize(viewDX, viewDY);
        const QPoint offset = newDocOffset(viewDX, viewDY);
        const int viewOriginX = static_cast<int>(m_view->transformDocToViewX(0));
        const int viewOriginY = static_cast<int>(m_view->transformDocToViewY(0));

        m_resizeRoundedLastViewLeft = viewOriginX - (static_cast<int>(m_view->transformDocToViewX(offset.x())) - viewOriginX);
        m_resizeRoundedLastViewTop = viewOriginY - (static_cast<int>(m_view->transformDocToViewY(offset.y())) - viewOriginY);
        m_resizeRoundedLastViewRight =
            m_resizeRoundedLastViewLeft + (static_cast<int>(m_view->transformDocToViewX(size.width())) - viewOriginX);
        m_resizeRoundedLastViewBottom =
            m_resizeRoundedLastViewTop + (static_cast<int>(m_view->transformDocToViewY(size.height())) - viewOriginY);
        m_resizeRoundedLastViewDX = viewDX;
        m_resizeRoundedLastViewDY = viewDY;
    } else {
        m_resizeRoundedLastViewLeft = 0;
        m_resizeRoundedLastViewTop = 0;
        m_resizeRoundedLastViewRight = -1;
        m_resizeRoundedLastViewBottom = -1;

        m_resizeRoundedLastViewDX = 0;
        m_resizeRoundedLastViewDY = 0;
    }

    m_overlay->update();
}

void kpViewScrollableContainer::clearResizeLines()
{
    m_resizeRoundedLastViewLeft = 0;
    m_resizeRoundedLastViewTop = 0;
    m_resizeRoundedLastViewRight = -1;
    m_resizeRoundedLastViewBottom = -1;
    m_resizeRoundedLastViewDX = 0;
    m_resizeRoundedLastViewDY = 0;

    m_overlay->update();
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotGripBeganDraw()
{
    if (!m_view) {
        return;
    }

    m_overlay->resize(viewport()->size()); // make it cover whole viewport
    m_overlay->move(viewport()->pos());
    m_overlay->show();
    m_overlay->raise(); // make it top-most

    calculateDocResizingGrip();

    m_haveMovedFromOriginalDocSize = false;

    updateResizeLines(0 /*viewDX*/, 0 /*viewDY*/);

    Q_EMIT beganDocResize();
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotGripContinuedDraw(int inViewDX, int inViewDY, bool dueToDragScroll)
{
    int viewDX = inViewDX, viewDY = inViewDY;

#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpViewScrollableContainer::slotGripContinuedDraw(" << viewDX << "," << viewDY << ") size=" << newDocSize(viewDX, viewDY)
                       << " dueToDragScroll=" << dueToDragScroll;
#endif

    if (!m_view) {
        return;
    }

    if (!dueToDragScroll && beginDragScroll(m_view->zoomLevelX())) {
        const QPoint newViewDeltaPoint = docResizingGrip()->viewDeltaPoint();
        viewDX = newViewDeltaPoint.x();
        viewDY = newViewDeltaPoint.y();
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
        qCDebug(kpLogMisc) << "\tdrag scrolled - new view delta point=" << newViewDeltaPoint;
#endif
    }

    m_haveMovedFromOriginalDocSize = true;

    updateResizeLines(viewDX, viewDY);

    Q_EMIT continuedDocResize(newDocSize());
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotGripCancelledDraw()
{
    m_haveMovedFromOriginalDocSize = false;

    clearResizeLines();

    calculateDocResizingGrip();

    Q_EMIT cancelledDocResize();

    endDragScroll();

    m_overlay->hide();
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotGripEndedDraw(int viewDX, int viewDY)
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpViewScrollableContainer::slotGripEndedDraw(" << viewDX << "," << viewDY << ") size=" << newDocSize(viewDX, viewDY);
#endif

    if (!m_view) {
        return;
    }

    const QSize newSize = newDocSize(viewDX, viewDY);
    const QPoint oldImageOffset = newDocOffset(viewDX, viewDY);

    m_haveMovedFromOriginalDocSize = false;

    // must erase lines before view size changes
    clearResizeLines();

    calculateDocResizingGrip();

    Q_EMIT endedDocResize(newSize, oldImageOffset);

    endDragScroll();

    m_overlay->hide();
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotGripStatusMessageChanged(const QString &string)
{
    if (string == m_gripStatusMessage) {
        return;
    }

    m_gripStatusMessage = string;
    Q_EMIT statusMessageChanged(string);
}

//---------------------------------------------------------------------

// public slot
void kpViewScrollableContainer::recalculateStatusMessage()
{
#if DEBUG_KP_VIEW_SCROLLABLE_CONTAINER
    qCDebug(kpLogMisc) << "kpViewScrollabelContainer::recalculateStatusMessage()";
    qCDebug(kpLogMisc) << "\tQCursor::pos=" << QCursor::pos()
                       << " global visibleRect=" << kpWidgetMapper::toGlobal(this, QRect(0, 0, viewport->width(), viewport->height()));
#endif

    // HACK: After dragging to a new size, handles move so that they are now
    //       under the mouse pointer but no mouseMoveEvent() is generated for
    //       any grip.  This also handles the case of canceling over any
    //       grip.
    //
    if (kpWidgetMapper::toGlobal(this, QRect(0, 0, viewport()->width(), viewport()->height())).contains(QCursor::pos())) {
        bool overGrip = false;
        for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
            if (grip->containsCursor()) {
                grip->setUserMessage(i18n("Left drag the handle to resize the image."));
                overGrip = true;
                break;
            }
        }
        if (!overGrip) {
            clearStatusMessage();
        }
    } else {
        clearStatusMessage();
    }
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotContentsMoved()
{
    kpGrip *grip = docResizingGrip();
    if (grip) {
        grip->mouseMovedTo(grip->mapFromGlobal(QCursor::pos()), true /*moved due to drag scroll*/);
    }

    m_overlay->move(viewport()->pos());
    m_overlay->update();

    Q_EMIT contentsMoved();
}

//---------------------------------------------------------------------

// protected
void kpViewScrollableContainer::disconnectViewSignals()
{
    disconnect(m_view, static_cast<void (kpView::*)(int, int)>(&kpView::sizeChanged), this, &kpViewScrollableContainer::updateGrips);

    disconnect(m_view, &kpView::destroyed, this, &kpViewScrollableContainer::slotViewDestroyed);
}

//---------------------------------------------------------------------

// protected
void kpViewScrollableContainer::connectViewSignals()
{
    connect(m_view, static_cast<void (kpView::*)(int, int)>(&kpView::sizeChanged), this, &kpViewScrollableContainer::updateGrips);

    connect(m_view, &kpView::destroyed, this, &kpViewScrollableContainer::slotViewDestroyed);
}

//---------------------------------------------------------------------

// public
kpView *kpViewScrollableContainer::view() const
{
    return m_view;
}

//---------------------------------------------------------------------

// public
void kpViewScrollableContainer::setView(kpView *view)
{
    if (m_view == view) {
        return;
    }

    if (m_view) {
        disconnectViewSignals();
    }

    m_view = view;

    if (m_view) {
        m_view->setParent(widget());
        m_view->show();
    }

    updateGrips();

    if (m_view) {
        connectViewSignals();
    }
}

//---------------------------------------------------------------------

// public slot
void kpViewScrollableContainer::updateGrips()
{
    if (m_view) {
        const int gripSize = kpGrip::Size;
        widget()->resize(m_view->width() + (2 * gripSize), m_view->height() + (2 * gripSize));
        m_view->move(gripSize, gripSize);

        m_topLeftGrip->move(0, 0);
        m_topRightGrip->move(gripSize + m_view->width(), 0);
        m_bottomLeftGrip->move(0, gripSize + m_view->height());
        m_bottomRightGrip->move(gripSize + m_view->width(), gripSize + m_view->height());

        m_topGrip->setFixedWidth(m_view->width());
        m_topGrip->move(gripSize, 0);
        m_bottomGrip->setFixedWidth(m_view->width());
        m_bottomGrip->move(gripSize, gripSize + m_view->height());

        m_leftGrip->setFixedHeight(m_view->height());
        m_leftGrip->move(0, gripSize);
        m_rightGrip->setFixedHeight(m_view->height());
        m_rightGrip->move(gripSize + m_view->width(), gripSize);
    }

    for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
        grip->setHidden(m_view == nullptr);
    }

    // The canvas shadow lies outside of widget(), so moving or resizing
    // widget() alone leaves the old shadow behind.
    if (kpModernLook::isActive()) {
        viewport()->update();
    }

    recalculateStatusMessage();
}

//---------------------------------------------------------------------

// protected slot
void kpViewScrollableContainer::slotViewDestroyed()
{
    m_view = nullptr;
    updateGrips();
}

//---------------------------------------------------------------------

// public slot
bool kpViewScrollableContainer::beginDragScroll(int zoomLevel, bool *didSomething)
{
    if (didSomething) {
        *didSomething = false;
    }

    m_zoomLevel = zoomLevel;

    const QPoint p = mapFromGlobal(QCursor::pos());

    bool stopDragScroll = true;
    bool scrolled = false;

    if (!noDragScrollRect().contains(p)) {
        if (m_dragScrollTimer->isActive()) {
            if (m_scrollTimerRunOnce) {
                scrolled = slotDragScroll();
            }
        } else {
            m_scrollTimerRunOnce = false;
            m_dragScrollTimer->start(DragScrollInitialInterval);
        }

        stopDragScroll = false;
    }

    if (stopDragScroll) {
        m_dragScrollTimer->stop();
    }

    if (didSomething) {
        *didSomething = scrolled;
    }

    return scrolled;
}

//---------------------------------------------------------------------

// public slot
bool kpViewScrollableContainer::beginDragScroll(int zoomLevel)
{
    return beginDragScroll(zoomLevel, nullptr /*don't want scrolled notification*/);
}

//---------------------------------------------------------------------

// public slot
bool kpViewScrollableContainer::endDragScroll()
{
    if (m_dragScrollTimer->isActive()) {
        m_dragScrollTimer->stop();
        return true;
    }

    return false;
}

//---------------------------------------------------------------------

static int distanceFromRectToMultiplier(int dist)
{
    if (dist < 0) {
        return 0;
    }

    if (dist < DragDistanceFromRectMaxFor1stMultiplier) {
        return 1;
    }

    if (dist < DragDistanceFromRectMaxFor2ndMultiplier) {
        return 2;
    }

    return 4;
}

//---------------------------------------------------------------------

// protected slot
bool kpViewScrollableContainer::slotDragScroll(bool *didSomething)
{
    bool scrolled = false;

    if (didSomething) {
        *didSomething = false;
    }

    const QRect rect = noDragScrollRect();
    const QPoint pos = mapFromGlobal(QCursor::pos());

    int dx = 0, dy = 0;
    int dxMultiplier = 0, dyMultiplier = 0;

    if (pos.x() < rect.left()) {
        dx = -DragScrollNumPixels;
        dxMultiplier = distanceFromRectToMultiplier(rect.left() - pos.x());
    } else if (pos.x() > rect.right()) {
        dx = +DragScrollNumPixels;
        dxMultiplier = distanceFromRectToMultiplier(pos.x() - rect.right());
    }

    if (pos.y() < rect.top()) {
        dy = -DragScrollNumPixels;
        dyMultiplier = distanceFromRectToMultiplier(rect.top() - pos.y());
    } else if (pos.y() > rect.bottom()) {
        dy = +DragScrollNumPixels;
        dyMultiplier = distanceFromRectToMultiplier(pos.y() - rect.bottom());
    }

    dx *= dxMultiplier; // * qMax (1, m_zoomLevel / 100);
    dy *= dyMultiplier; // * qMax (1, m_zoomLevel / 100);

    if (dx || dy) {
        const int oldContentsX = horizontalScrollBar()->value(), oldContentsY = verticalScrollBar()->value();

        horizontalScrollBar()->setValue(oldContentsX + dx);
        verticalScrollBar()->setValue(oldContentsY + dy);

        scrolled = (oldContentsX != horizontalScrollBar()->value() || oldContentsY != verticalScrollBar()->value());

        if (scrolled) {
            QRegion region = QRect(horizontalScrollBar()->value(), verticalScrollBar()->value(), viewport()->width(), viewport()->height());
            region -= QRect(oldContentsX, oldContentsY, viewport()->width(), viewport()->height());
            region.translate(-m_view->pos());

            // Repaint newly exposed region immediately to reduce tearing
            // of scrollView.
            m_view->repaint(region);
        }
    }

    m_dragScrollTimer->start(DragScrollInterval);
    m_scrollTimerRunOnce = true;

    if (didSomething) {
        *didSomething = scrolled;
    }

    return scrolled;
}

//---------------------------------------------------------------------

// protected virtual
void kpViewScrollableContainer::wheelEvent(QWheelEvent *e)
{
    e->ignore();

    if (m_view) {
        m_view->wheelEvent(e);
    }

    if (!e->isAccepted()) {
        QScrollArea::wheelEvent(e);
    }
}

//---------------------------------------------------------------------------------

QRect kpViewScrollableContainer::noDragScrollRect() const
{
    return {DragScrollLeftTopMargin,
            DragScrollLeftTopMargin,
            width() - DragScrollLeftTopMargin - DragScrollRightBottomMargin,
            height() - DragScrollLeftTopMargin - DragScrollRightBottomMargin};
}

//---------------------------------------------------------------------

// protected virtual [base QScrollView]
void kpViewScrollableContainer::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);

    Q_EMIT resized();
}

//---------------------------------------------------------------------

// private
void kpViewScrollableContainer::applyLook()
{
    const bool modern = kpModernLook::isActive();

    setAlignment(modern ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignTop);

    // Let paintEvent()'s workspace and canvas shadow show through around
    // the view and between the grip handles.
    widget()->setAutoFillBackground(!modern);
    for (kpGrip *grip : {m_topLeftGrip, m_topGrip, m_topRightGrip, m_leftGrip, m_rightGrip, m_bottomLeftGrip, m_bottomGrip, m_bottomRightGrip}) {
        grip->setAutoFillBackground(!modern);
        grip->update();
    }

    viewport()->update();
}

//---------------------------------------------------------------------

// protected virtual [base QAbstractScrollArea]
void kpViewScrollableContainer::paintEvent(QPaintEvent *e)
{
    if (!kpModernLook::isActive()) {
        QScrollArea::paintEvent(e);
        return;
    }

    QPainter p(viewport());
    p.fillRect(e->rect(), kpModernLook::workspaceColor(palette()));

    if (!m_view || m_view->isHidden()) {
        return;
    }

    // A soft drop shadow: stacked translucent rounded rectangles, a little
    // lower than the canvas, painted only outside of it.
    const QRect canvas(widget()->pos() + m_view->pos(), m_view->size());
    const int layers = 8;

    p.setClipRegion(e->region().subtracted(canvas));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, kpModernLook::isDark(palette()) ? 18 : 6));
    for (int i = layers; i >= 1; i--) {
        p.drawRoundedRect(QRectF(canvas).adjusted(-i, -i + 2, i, i + 2), i, i);
    }
}

//---------------------------------------------------------------------

#include "moc_kpViewScrollableContainer.cpp"
