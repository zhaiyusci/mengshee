// SPDX-License-Identifier: GPL-2.0-or-later
#include "../part/rectangleinteraction.h"
#include <QTest>
class RectangleInteractionTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void handles()
    {
        using R = RectangleInteraction;
        for (const QRectF rect : {QRectF(20, 20, 100, 70), QRectF(20, 20, 3, 3)}) {
            for (auto handle : R::handles)
                QCOMPARE(R::hitTest(rect, R::center(rect, handle)), handle);
        }
        QCOMPARE(R::hitTest(QRectF(20, 20, 100, 70), QPointF(50, 50)), R::Handle::Move);
        QCOMPARE(R::cursor(R::Handle::TopRight), Qt::SizeBDiagCursor);
    }
    void drag()
    {
        using R = RectangleInteraction;
        const QRectF rect(20, 20, 100, 70), bounds(0, 0, 200, 200);
        for (auto handle : R::handles) {
            const auto start = R::center(rect, handle) + QPointF(3, 2);
            QCOMPARE(R::drag(rect, handle, start, start, QSizeF(4, 4), bounds), rect);
            auto changed = R::drag(rect, handle, start, start + QPointF(8, 6), QSizeF(4, 4), bounds);
            QVERIFY(changed.isValid());
            if (!R::hasEdge(handle, R::Handle::Left))
                QCOMPARE(changed.left(), rect.left());
            if (!R::hasEdge(handle, R::Handle::Top))
                QCOMPARE(changed.top(), rect.top());
            if (!R::hasEdge(handle, R::Handle::Right))
                QCOMPARE(changed.right(), rect.right());
            if (!R::hasEdge(handle, R::Handle::Bottom))
                QCOMPARE(changed.bottom(), rect.bottom());
        }
        QCOMPARE(R::drag(rect, R::Handle::Left, {}, QPointF(500, 0), QSizeF(4, 4), bounds).width(), 4.0);
        auto moved = R::drag(rect, R::Handle::Move, {}, QPointF(500, 500), QSizeF(4, 4), bounds);
        QCOMPARE(moved.size(), rect.size());
        QCOMPARE(moved.bottomRight(), bounds.bottomRight());
    }
    void proportionalResize()
    {
        using R = RectangleInteraction;
        const QRectF bounds(0,0,1,1);
        for (const QRectF rect : {QRectF(.2,.2,.4,.2), QRectF(.2,.2,.2,.4)}) {
            for (auto handle : R::handles) {
                for (const QPointF delta : {QPointF(), QPointF(.1,-.07), QPointF(5,5), QPointF(-5,-5)}) {
                    const auto result=R::dragProportional(rect,handle,delta,QSizeF(.01,.01),bounds);
                    QVERIFY(result.isValid());
                    QVERIFY(qAbs(result.width()/result.height()-rect.width()/rect.height())<1e-10);
                    QVERIFY(bounds.adjusted(-1e-10,-1e-10,1e-10,1e-10).contains(result));
                    const auto opposite=static_cast<R::Handle>(((int(handle)&1)?4:0)|((int(handle)&4)?1:0)|((int(handle)&2)?8:0)|((int(handle)&8)?2:0));
                    QVERIFY(QLineF(R::center(rect,opposite),R::center(result,opposite)).length()<1e-10);
                    if (delta.isNull()) QCOMPARE(result,rect);
                }
            }
        }
    }
    void transparentInterior()
    {
        QImage image(160, 120, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        painter.setBrush(Qt::white);
        RectangleInteraction::draw(&painter, QRectF(20, 20, 100, 70));
        painter.end();
        QCOMPARE(image.pixelColor(50, 50), QColor(Qt::black));
    }
};
QTEST_GUILESS_MAIN(RectangleInteractionTest)
#include "rectangleinteractiontest.moc"
