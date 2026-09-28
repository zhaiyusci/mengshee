/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/readingviewlayout_p.h"

#include <QtTest>

#include <limits>

using namespace Okular::ReadingViewLayout;

Q_DECLARE_METATYPE(Okular::ReadingViewLayout::Block)

namespace
{
QRectF rectangle(qreal left, qreal top, qreal right, qreal bottom)
{
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

Block block(qreal left, qreal top, qreal right, qreal bottom, int type = Block::FlowingText)
{
    return {rectangle(left, top, right, bottom), type};
}

void compareRectangles(const QList<QRectF> &actual, const QList<QRectF> &expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype i = 0; i < actual.size(); ++i) {
        QVERIFY2(qAbs(actual[i].left() - expected[i].left()) < 1e-12, qPrintable(QStringLiteral("group %1 left").arg(i)));
        QVERIFY2(qAbs(actual[i].top() - expected[i].top()) < 1e-12, qPrintable(QStringLiteral("group %1 top").arg(i)));
        QVERIFY2(qAbs(actual[i].right() - expected[i].right()) < 1e-12, qPrintable(QStringLiteral("group %1 right").arg(i)));
        QVERIFY2(qAbs(actual[i].bottom() - expected[i].bottom()) < 1e-12, qPrintable(QStringLiteral("group %1 bottom").arg(i)));
    }
}
}

class ReadingViewLayoutTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void paperPage10();
    void previousSamples_data();
    void previousSamples();
    void invalidBoxes();
    void clippingAndPadding();
    void contentWidthScaling();
    void preservesIteratorOrder();
    void requiresOriginalSpanningBlock();
    void shortTableNote();
    void insideTallTable();
    void headingBoundary();
    void spanningTextTypes_data();
    void spanningTextTypes();
};

void ReadingViewLayoutTest::paperPage10()
{
    // Only normalized geometry/types from paper-1.2756518/results/results.json,
    // page 10. Expected groups match width-results/results.json's new_groups;
    // the older results.json groups predate original-width spanning detection.
    const QList<Block> blocks = {
        block(0.081961, 0.033020, 0.409412, 0.040594),
        block(0.081961, 0.061497, 0.912941, 0.073008, Block::PulloutText),
        block(0.081961, 0.074826, 0.503137, 0.085126, Block::HeadingText),
        block(0.703922, 0.032717, 0.912549, 0.042411, Block::PulloutText),
        block(0.081961, 0.090276, 0.913333, 0.091487, Block::HorizontalLine),
        block(0.081961, 0.093305, 0.913333, 0.094517, Block::HorizontalLine),
        block(0.464706, 0.116934, 0.913333, 0.117540, Block::HorizontalLine),
        block(0.081961, 0.138443, 0.913333, 0.139655, Block::HorizontalLine),
        block(0.102353, 0.100273, 0.893333, 0.460769, Block::Table),
        block(0.081961, 0.466222, 0.913333, 0.467434, Block::HorizontalLine),
        block(0.081961, 0.469252, 0.913333, 0.470463, Block::HorizontalLine),
        block(0.081961, 0.472584, 0.912941, 0.496516, Block::HeadingText),
        block(0.081961, 0.498637, 0.482745, 0.508331),
        block(0.081961, 0.511057, 0.638824, 0.523175, Block::HeadingText),
        block(0.081961, 0.525598, 0.912549, 0.561648, Block::HeadingText),
        block(0.109804, 0.590124, 0.482353, 0.605271),
        block(0.081961, 0.608906, 0.482353, 0.640109),
        block(0.081961, 0.643744, 0.482745, 0.793699),
        block(0.081961, 0.797334, 0.482745, 0.957589),
        block(0.512549, 0.591336, 0.913333, 0.632536),
        block(0.512549, 0.651924, 0.890196, 0.674947),
        block(0.512157, 0.689185, 0.913333, 0.957589),
    };
    const QList<QRectF> expected = {
        rectangle(0.075961, 0.026717, 0.919333, 0.567648), // blocks 1-15 (table and notes)
        rectangle(0.075961, 0.584124, 0.488745, 0.963589), // blocks 16-19 (left lane)
        rectangle(0.506157, 0.585336, 0.919333, 0.963589), // blocks 20-22 (right lane)
    };
    compareRectangles(merge(blocks), expected);
    // The function must not mutate or reorder the caller's blocks.
    QCOMPARE(blocks.size(), 22);
    QCOMPARE(blocks[15].rectangle, rectangle(0.109804, 0.590124, 0.482353, 0.605271));
}

void ReadingViewLayoutTest::previousSamples_data()
{
    QTest::addColumn<QList<Block>>("blocks");
    QTest::addColumn<QList<QRectF>>("expected");
    // Compact exact raw_before geometry from tesseract-views/order-results.
    // These tests intentionally merge the tilted input, not deskew or map it.
    QTest::newRow("ieee-p1-heading-table-two-columns")
        << QList<Block>{
               block(0.123137, 0.075432, 0.877647, 0.137837, Block::HeadingText),
               block(0.405882, 0.155407, 0.594118, 0.211754, Block::HeadingText),
               block(0.074510, 0.240230, 0.485098, 0.263254),
               block(0.215686, 0.280521, 0.343137, 0.289306),
               block(0.074510, 0.299909, 0.485490, 0.356862),
               block(0.073725, 0.367464, 0.485490, 0.651621),
               block(0.080784, 0.684641, 0.479216, 0.686762, Block::HorizontalLine),
               block(0.126275, 0.700394, 0.479216, 0.702514, Block::HorizontalLine),
               block(0.080784, 0.720085, 0.479216, 0.722205, Block::HorizontalLine),
               block(0.126275, 0.738261, 0.479216, 0.740382, Block::HorizontalLine),
               block(0.090980, 0.660709, 0.450980, 0.750985, Block::Table),
               block(0.099216, 0.752196, 0.376863, 0.850348),
               block(0.097647, 0.857316, 0.188627, 0.866404),
               block(0.126275, 0.794002, 0.479216, 0.796122, Block::HorizontalLine),
               block(0.126275, 0.806422, 0.479216, 0.808543, Block::HorizontalLine),
               block(0.407059, 0.810058, 0.467843, 0.819146),
               block(0.126275, 0.838837, 0.479216, 0.840957, Block::HorizontalLine),
               block(0.126275, 0.851863, 0.479216, 0.853984, Block::HorizontalLine),
               block(0.081176, 0.685247, 0.081961, 0.869131, Block::VerticalLine),
               block(0.126275, 0.685247, 0.127451, 0.869131, Block::VerticalLine),
               block(0.478039, 0.685247, 0.479216, 0.869131, Block::VerticalLine),
               block(0.323529, 0.701000, 0.324314, 0.869131, Block::VerticalLine),
               block(0.397255, 0.701000, 0.398039, 0.869131, Block::VerticalLine),
               block(0.081176, 0.867616, 0.479216, 0.869736, Block::HorizontalLine),
               block(0.136078, 0.873978, 0.194118, 0.883369),
               block(0.585882, 0.245380, 0.846275, 0.246895, Block::HorizontalLine),
               block(0.534902, 0.248713, 0.543922, 0.315662, Block::VerticalText),
               block(0.585882, 0.245683, 0.587451, 0.317782, Block::VerticalLine),
               block(0.844706, 0.245683, 0.846275, 0.317782, Block::VerticalLine),
               block(0.585882, 0.316571, 0.846275, 0.318085, Block::HorizontalLine),
               block(0.603137, 0.322932, 0.842353, 0.342321),
               block(0.586275, 0.353226, 0.854118, 0.373523),
               block(0.654118, 0.412299, 0.785882, 0.421085),
               block(0.514118, 0.435626, 0.927451, 0.704635),
               block(0.514510, 0.719176, 0.925882, 0.882157),
           }
        << QList<QRectF>{rectangle(0.117137, 0.069432, 0.883647, 0.217754), rectangle(0.067725, 0.234230, 0.491490, 0.889369), rectangle(0.508118, 0.239380, 0.933451, 0.888157)};
    QTest::newRow("ieee-p2-simple-two-columns")
        << QList<Block>{
               block(0.074510, 0.071472, 0.485490, 0.219867),
               block(0.075294, 0.222895, 0.096471, 0.234403),
               block(0.074510, 0.247729, 0.484706, 0.334949),
               block(0.073725, 0.345851, 0.485490, 0.492429),
               block(0.248627, 0.509994, 0.295686, 0.519079),
               block(0.297255, 0.510600, 0.309804, 0.521502, Block::FlowingImage),
               block(0.467451, 0.510600, 0.485490, 0.521502),
               block(0.074510, 0.540279, 0.485490, 0.597820),
               block(0.073725, 0.609933, 0.485490, 0.801333),
               block(0.074510, 0.777105, 0.484706, 0.864930),
               block(0.685490, 0.070260, 0.754510, 0.078740),
               block(0.514510, 0.089037, 0.927843, 0.222895),
               block(0.618824, 0.231981, 0.821961, 0.240460),
               block(0.514510, 0.251363, 0.925490, 0.550575),
               block(0.654118, 0.559661, 0.786667, 0.568141),
               block(0.514510, 0.579043, 0.925490, 0.651726),
               block(0.679216, 0.661417, 0.761569, 0.669897),
               block(0.515294, 0.679588, 0.927059, 0.870987),
               block(0.695686, 0.737129, 0.716863, 0.743792, Block::FlowingImage),
           }
        << QList<QRectF>{rectangle(0.067725, 0.065472, 0.491490, 0.870930), rectangle(0.508510, 0.064260, 0.933843, 0.876987)};
    QTest::newRow("ieee-p2-scan-plus3")
        << QList<Block>{
               block(0.047059, 0.073895, 0.465882, 0.236826),
               block(0.057255, 0.239855, 0.079216, 0.252574),
               block(0.058039, 0.259237, 0.472157, 0.350091),
               block(0.064314, 0.360993, 0.483922, 0.508177),
               block(0.250196, 0.511205, 0.487059, 0.529982),
               block(0.079216, 0.541490, 0.490980, 0.612356),
               block(0.083137, 0.620836, 0.508235, 0.881890),
               block(0.656471, 0.061781, 0.725490, 0.072078),
               block(0.487843, 0.073895, 0.905882, 0.222290),
               block(0.600784, 0.220472, 0.803922, 0.236220),
               block(0.498824, 0.235009, 0.927059, 0.562084),
               block(0.521569, 0.562084, 0.933333, 0.648698),
               block(0.690196, 0.651726, 0.772549, 0.662629),
               block(0.527843, 0.662023, 0.947451, 0.843125),
               block(0.538824, 0.832829, 0.945098, 0.866747),
           }
        << QList<QRectF>{rectangle(0.041059, 0.067895, 0.514235, 0.887890), rectangle(0.481843, 0.055781, 0.953451, 0.872747)};
    QTest::newRow("ieee-p2-scan-minus3")
        << QList<Block>{
               block(0.096471, 0.055724, 0.513725, 0.216838),
               block(0.087059, 0.231375, 0.500392, 0.322835),
               block(0.079216, 0.328892, 0.494118, 0.445790),
               block(0.077647, 0.436099, 0.411765, 0.456693),
               block(0.247843, 0.500909, 0.484706, 0.520896),
               block(0.069020, 0.524531, 0.481569, 0.586311),
               block(0.050196, 0.593580, 0.476078, 0.857056),
               block(0.558431, 0.078134, 0.952157, 0.116293),
               block(0.533333, 0.182919, 0.949020, 0.231375),
               block(0.636078, 0.237432, 0.839216, 0.253786),
               block(0.512157, 0.253180, 0.941176, 0.579649),
               block(0.505098, 0.580860, 0.919216, 0.662023),
               block(0.667451, 0.668686, 0.749804, 0.680194),
               block(0.502745, 0.680194, 0.914510, 0.715324),
               block(0.490980, 0.714113, 0.909020, 0.875227),
           }
        << QList<QRectF>{rectangle(0.044196, 0.049724, 0.519725, 0.863056), rectangle(0.484980, 0.072134, 0.958157, 0.881227)};
    QTest::newRow("napkin-p7-single-column")
        << QList<Block>{
               block(0.150746, 0.063569, 0.849254, 0.072976),
               block(0.149940, 0.075257, 0.850060, 0.076967, Block::HorizontalLine),
               block(0.150343, 0.091505, 0.582023, 0.130274),
               block(0.149536, 0.144242, 0.848851, 0.204105),
               block(0.159613, 0.217788, 0.840387, 0.220068, Block::HorizontalLine),
               block(0.151552, 0.219498, 0.848448, 0.318985),
               block(0.386538, 0.338084, 0.611447, 0.368301),
               block(0.151552, 0.372007, 0.848448, 0.381984),
               block(0.848448, 0.225485, 0.850060, 0.375998, Block::VerticalLine),
               block(0.149940, 0.225485, 0.151955, 0.375998, Block::VerticalLine),
               block(0.160016, 0.380559, 0.839984, 0.382839, Block::HorizontalLine),
               block(0.151149, 0.399658, 0.775091, 0.413056),
               block(0.224103, 0.424458, 0.530834, 0.454105),
               block(0.412334, 0.447548, 0.418380, 0.454105),
               block(0.384119, 0.458381, 0.705361, 0.488027),
               block(0.384119, 0.492303, 0.775091, 0.521950),
               block(0.149940, 0.532782, 0.850464, 0.560718),
               block(0.149940, 0.575257, 0.849657, 0.665051),
               block(0.149940, 0.861174, 0.849657, 0.889966),
           }
        << QList<QRectF>{rectangle(0.143536, 0.057569, 0.856464, 0.895966)};
}

void ReadingViewLayoutTest::previousSamples()
{
    QFETCH(QList<Block>, blocks);
    QFETCH(QList<QRectF>, expected);
    compareRectangles(merge(blocks), expected);
}

void ReadingViewLayoutTest::invalidBoxes()
{
    QVERIFY(merge({}).isEmpty());
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal inf = std::numeric_limits<qreal>::infinity();
    QList<Block> invalid = {
        {QRectF(), Block::Unknown},
        {QRectF(.2, .2, 0, .1), Block::FlowingText},
        {QRectF(.2, .2, .1, 0), Block::FlowingText},
        {QRectF(.2, .2, -.1, .1), Block::FlowingText},
        {QRectF(.2, .2, .1, -.1), Block::FlowingText},
        {QRectF(nan, .2, .1, .1), Block::FlowingText},
        {QRectF(.2, nan, .1, .1), Block::FlowingText},
        {QRectF(.2, .2, inf, .1), Block::FlowingText},
        {QRectF(.2, .2, .1, inf), Block::FlowingText},
        {QRectF(-inf, .2, .1, .1), Block::FlowingText},
        block(1.1, .1, 1.2, .2),
        block(-.2, .1, -.1, .2),
        block(.1, 1.1, .2, 1.2),
        block(.1, -.2, .2, -.1),
    };
    QVERIFY(merge(invalid).isEmpty());
    invalid.prepend(block(.55, .6, .9, .9));
    invalid.append(block(.1, .1, .45, .4));
    compareRectangles(merge(invalid), {rectangle(.544, .594, .906, .906), rectangle(.094, .094, .456, .406)});
}

void ReadingViewLayoutTest::clippingAndPadding()
{
    compareRectangles(merge({block(.2, .3, .4, .5)}), {rectangle(.194, .294, .406, .506)});
    compareRectangles(merge({block(.002, .003, .998, .999)}), {rectangle(0, 0, 1, 1)});
    compareRectangles(merge({block(-.2, -.1, .3, .4)}), {rectangle(0, 0, .306, .406)});
    compareRectangles(merge({block(.8, .7, 1.2, 1.1)}), {rectangle(.794, .694, 1, 1)});
}

void ReadingViewLayoutTest::contentWidthScaling()
{
    const QList<Block> source = {block(.1, .1, .9, .3, Block::Table), block(.1, .4, .45, .9), block(.55, .4, .9, .9)};
    // The spanning detector uses content width, not a fixed fraction of the page.
    for (const qreal scale : {1., .5, .25}) {
        QList<Block> scaled;
        for (const Block &b : source) {
            scaled.append(block(.02 + scale * b.rectangle.left(), b.rectangle.top(), .02 + scale * b.rectangle.right(), b.rectangle.bottom(), b.type));
        }
        compareRectangles(merge(scaled),
                          {rectangle(.014 + scale * .1, .094, .026 + scale * .9, .306), rectangle(.014 + scale * .1, .394, .026 + scale * .45, .906), rectangle(.014 + scale * .55, .394, .026 + scale * .9, .906)});
    }
}

void ReadingViewLayoutTest::preservesIteratorOrder()
{
    // Deliberately right-to-left and bottom-to-top, with a return to an earlier lane.
    compareRectangles(merge({block(.55, .6, .9, .9), block(.1, .1, .45, .4), block(.55, .1, .9, .3)}),
                      {rectangle(.544, .594, .906, .906), rectangle(.094, .094, .456, .406), rectangle(.544, .094, .906, .306)});
    // Moving horizontally without returning up-page must not force a new group.
    compareRectangles(merge({block(.1, .1, .4, .2), block(.6, .3, .9, .4)}), {rectangle(.094, .094, .906, .406)});
}

void ReadingViewLayoutTest::requiresOriginalSpanningBlock()
{
    // The union of the first two blocks is wide, but neither ORIGINAL block is.
    compareRectangles(merge({block(.1, .1, .45, .2), block(.55, .19, .9, .21), block(.1, .3, .45, .6), block(.55, .3, .9, .6)}), {rectangle(.094, .094, .906, .606)});
}

void ReadingViewLayoutTest::shortTableNote()
{
    // A <.01-high note below a table has a peer but is not the next column region.
    compareRectangles(merge({block(.1, .1, .9, .4, Block::Table), block(.1, .41, .45, .419), block(.55, .41, .9, .45), block(.1, .46, .9, .5, Block::CaptionText), block(.1, .55, .45, .9), block(.55, .55, .9, .9)}),
                      {rectangle(.094, .094, .906, .506), rectangle(.094, .544, .456, .906), rectangle(.544, .544, .906, .906)});
}

void ReadingViewLayoutTest::insideTallTable()
{
    // A wide earlier rule has ended, but the current table still extends below us.
    compareRectangles(merge({block(.1, .1, .9, .11, Block::HorizontalLine), block(.1, .12, .9, .6, Block::Table), block(.1, .3, .45, .5), block(.55, .3, .9, .5)}), {rectangle(.094, .094, .906, .606)});
}

void ReadingViewLayoutTest::headingBoundary()
{
    // This original absolute-width heading rule requires no later peer.
    compareRectangles(merge({block(.1, .1, .9, .2, Block::HeadingText), block(.1, .3, .45, .8)}), {rectangle(.094, .094, .906, .206), rectangle(.094, .294, .456, .806)});
    compareRectangles(merge({block(.1, .1, .9, .2, Block::PulloutText), block(.1, .3, .45, .8)}), {rectangle(.094, .094, .906, .806)});
}

void ReadingViewLayoutTest::spanningTextTypes_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<bool>("splits");
    QTest::newRow("flowing") << int(Block::FlowingText) << true;
    QTest::newRow("heading") << int(Block::HeadingText) << true;
    QTest::newRow("pullout") << int(Block::PulloutText) << true;
    QTest::newRow("caption") << int(Block::CaptionText) << true;
    QTest::newRow("vertical") << int(Block::VerticalText) << false;
    QTest::newRow("table") << int(Block::Table) << false;
    QTest::newRow("image") << int(Block::FlowingImage) << false;
    QTest::newRow("line") << int(Block::HorizontalLine) << false;
    QTest::newRow("unknown-future-type") << 100 << false;
}

void ReadingViewLayoutTest::spanningTextTypes()
{
    QFETCH(int, type);
    QFETCH(bool, splits);
    const QList<QRectF> actual = merge({block(.1, .1, .9, .2, Block::Table), block(.1, .3, .45, .8, type), block(.55, .3, .9, .8, type)});
    if (splits) {
        compareRectangles(actual, {rectangle(.094, .094, .906, .206), rectangle(.094, .294, .456, .806), rectangle(.544, .294, .906, .806)});
    } else {
        compareRectangles(actual, {rectangle(.094, .094, .906, .806)});
    }
}

QTEST_GUILESS_MAIN(ReadingViewLayoutTest)

#include "readingviewlayouttest.moc"
