/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "part/latexsource.h"
#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
using GuiUtils::LatexSource::requestSetup;
class LatexSourcePreparationTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void requestEnvironment()
    {
        QCOMPARE(requestSetup(QColor()), requestSetup(Qt::black));
        const auto red = requestSetup(Qt::red);
        QVERIFY(red.contains(QStringLiteral("\\color[rgb]{1.000000,0.000000,0.000000}")));
        QVERIFY(red.contains(QStringLiteral("shipout/background")));
        QVERIFY(red.contains(QStringLiteral("shipout/foreground")));
        // Colour setup must not add layout material or alter authored spacing.
        for (const auto &token : {"\\everypar", "\\everydisplay", "\\strut", "\\vbox", "\\noindent", "\\hrule", "\\belowdisplay", "\\vskip"})
            QVERIFY(!red.contains(QString::fromLatin1(token)));
    }
    void exportFixtures();
};

void LatexSourcePreparationTest::exportFixtures()
{
    const QString path = qEnvironmentVariable("MENGSHEE_LATEX_FIXTURES");
    if (path.isEmpty()) {
        QSKIP("Set MENGSHEE_LATEX_FIXTURES to export prepared snippets for native rendering probes.");
    }

    struct Fixture {
        const char *name;
        QString source;
    };
    const Fixture fixtures[] = {
        {"lowercase-a", QStringLiteral("a")},
        {"uppercase-a", QStringLiteral("A")},
        {"descender-g", QStringLiteral("g")},
        {"inline-alpha", QStringLiteral("$\\alpha$")},
        {"inline-tall-fraction", QStringLiteral("$\\displaystyle\\frac{\\frac{a}{b}}{\\frac{c}{d}}$")},
        {"inline-accent", QStringLiteral("$\\widehat{ABCDEFG}$")},
        {"inline-smash-accent", QStringLiteral("$\\smash{\\widehat{ABCDEFG}}$")},
        {"italic-overhang-left", QStringLiteral("\\hspace*{-10pt}\\textit{f}")},
        {"italic-overhang-right", QStringLiteral("\\hspace*{238pt}\\textit{f}")},
        {"multiple-paragraphs", QStringLiteral("a\n\ng")},
        {"explicit-par", QStringLiteral("a\\par")},
        {"author-vertical-spacing", QStringLiteral("\\vspace*{5pt}a\\\\[8pt]g\\vspace*{7pt}")},
        {"pure-display-uppercase", QStringLiteral("$$A$$")},
        {"pure-display-descender", QStringLiteral("$$g$$")},
        {"pure-display-plus", QStringLiteral("$$+x$$")},
        {"pure-display-minus", QStringLiteral("$$-x$$")},
        {"pure-display-relation", QStringLiteral("$$=x$$")},
        {"leading-isotope", QStringLiteral("$$^{14}_{6}C$$")},
        {"leading-superscript", QStringLiteral("$$^2x$$")},
        {"leading-subscript", QStringLiteral("$$_2x$$")},
        {"trailing-plus", QStringLiteral("$$x+$$")},
        {"trailing-minus", QStringLiteral("$$x-$$")},
        {"trailing-relation", QStringLiteral("$$x=$$")},
        {"trailing-comma", QStringLiteral("$$x,$$")},
        {"trailing-semicolon", QStringLiteral("$$x;$$")},
        {"trailing-mathpunct", QStringLiteral("$$x\\mathpunct{,}$$")},
        {"display-array", QStringLiteral("$$\\begin{array}{cc}a&b\\\\c&d\\end{array}$$")},
        {"display-left-right", QStringLiteral("$$\\left(x\\right)$$")},
        {"display-negative-kern", QStringLiteral("$$x\\kern-5pt$$")},
        {"display-phantom", QStringLiteral("$$\\phantom{x}$$")},
        {"display-empty", QStringLiteral("$$$$")},
        {"ordinary-mathop", QStringLiteral("$$\\mathop{lim}_{x\\to0}f(x)$$")},
        {"bracket-leading-superscript", QStringLiteral("\\[^2x\\]")},
        {"equation-leading-isotope", QStringLiteral("\\begin{equation}^{14}_{6}C\\end{equation}")},
        {"display-close-comment", QStringLiteral("$$^2x% fake $$ and \\] and }\r\n$$ test")},
        {"equation-close-comment", QStringLiteral("\\begin{equation}^2x\\end% fake }\n {equation}")},
        {"grouped-inline-dollar", QStringLiteral("$$\\text{$a$}+x$$")},
        {"grouped-generalized-fraction", QStringLiteral("$${a\\over b}$$")},
        {"top-level-generalized-fraction", QStringLiteral("$$a\\over b$$")},
        {"top-level-choose", QStringLiteral("$$n\\choose k$$")},
        {"top-level-atop", QStringLiteral("$$a\\atop b$$")},
        {"top-level-above", QStringLiteral("$$a\\above1pt b$$")},
        {"grouped-choose", QStringLiteral("$${n\\choose k}$$")},
        {"grouped-atop", QStringLiteral("$${a\\atop b}$$")},
        {"primitive-eqno", QStringLiteral("$$x\\eqno(1)$$")},
        {"primitive-leqno", QStringLiteral("$$x\\leqno(1)$$")},
        {"displaymath-isotope", QStringLiteral("\\begin{displaymath}^{14}_{6}C\\end{displaymath}")},
        {"unclosed-display", QStringLiteral("$$^2x")},
        {"unclosed-bracket", QStringLiteral("\\[^2x")},
        {"unclosed-equation", QStringLiteral("\\begin{equation}^2x")},
        {"pure-display-sum", QStringLiteral("$$\\sum_{i=0}^{n}i$$")},
        {"pure-display-fraction", QStringLiteral("$$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$$")},
        {"nested-aligned", QStringLiteral("$$\\begin{aligned}a&=b\\\\c&=d\\end{aligned}$$")},
        {"text-current-color", QStringLiteral("\\color{.}a")},
        {"text-bare-blue", QStringLiteral("\\color{blue}a")},
        {"text-grouped-blue", QStringLiteral("a {\\color{blue}Blue} g")},
        {"equation-comment-offset", QStringLiteral("\\begin% keep }\n {equation}\\alpha\\end{equation}")},
        {"alpha-followed-by-text", QStringLiteral("$$\\alpha$$ test")},
        {"alpha-exact-user-three-lines", QStringLiteral("$$\\alpha$$ test\r\n\r\ntest")},
        {"alpha-comment-before-a", QStringLiteral("$$\\alpha% preserve closing comment\r\n$$ a\r\n\r\na")},
        {"macro-display-close", QStringLiteral("$$\\def\\finish{$$}\\alpha\\finish test")},
        {"leading-author-rule", QStringLiteral("$$\\alpha\\vrule width0pt height15pt depth2pt\\relax$$")},
        {"terminal-backslash", QStringLiteral("text\\")},
        {"terminal-comment-backslash", QStringLiteral("text % comment\\")},
        {"display-terminal-backslash", QStringLiteral("$$\\alpha$$ test\\")},
        {"display-terminal-comment-backslash", QStringLiteral("$$\\alpha$$ test % comment\\")},
        {"pure-display-e", QStringLiteral("$$E=mc^2$$")},
        {"pure-display-alpha", QStringLiteral("$$\\alpha$$")},
        {"pure-display-projection", QStringLiteral("$$\n\\sum_{A} \\boldsymbol{X}_{A} \\times \\frac{\\partial E_{P S}}{\\partial \\boldsymbol{X}_{A}}+\\sum_{A} \\boldsymbol{P}_{A} \\times \\frac{\\partial E_{P S}}{\\partial \\boldsymbol{P}_{A}}=0 .\n$$")},
        {"bracket-display", QStringLiteral("\\[\\alpha\\]")},
        {"multiline-align", QStringLiteral("\\begin{align*}\nx&=1\\\\\ny&=2\n\\end{align*}")},
        {"multiline-gather", QStringLiteral("\\begin{gather*}\nx=1\\\\\ny=2\n\\end{gather*}")},
        {"equation", QStringLiteral("\\begin{equation}E=mc^2\\end{equation}")},
        {"leading-comments-blanklines", QStringLiteral(" \r\n% fake $$ and \\[\r\n\r\n$$\\alpha$$ test")},
        {"trailing-comment", QStringLiteral("$$\\alpha$$ test % terminal comment")},
        {"trailing-blanklines", QStringLiteral("$$\\alpha$$ test\r\n\r\n\n")},
        {"display-after-text", QStringLiteral("test\n\\[\\alpha\\]")},
        {"explicit-spacing", QStringLiteral("$$\\alpha$$ test\\hspace{2pt}\\kern1pt \\ ")},
        {"current-color", QStringLiteral("\\color{.}$$\\alpha$$ test")},
        {"current-color-mix", QStringLiteral("\\color{.!50}$$\\alpha$$ test")},
        {"current-colorlet", QStringLiteral("\\colorlet{saved}{.}\\color{saved}$$\\alpha$$ test")},
        {"explicit-color-override", QStringLiteral("\\color{blue}$$\\alpha$$ test")},
        {"display-current-color", QStringLiteral("$$\\color{.}\\alpha$$ test")},
        {"display-current-color-mix", QStringLiteral("$$\\color{.!50}\\alpha$$ test")},
        {"display-current-colorlet", QStringLiteral("$$\\colorlet{saved}{.}\\color{saved}\\alpha$$ test")},
        {"display-explicit-color-override", QStringLiteral("$$\\color{blue}\\alpha$$ test")},
    };
    QJsonArray output;
    for (const Fixture &fixture : fixtures) {
        for (const QColor &color : {QColor(Qt::black), QColor(Qt::red)}) {
            const QString colorName = color == QColor(Qt::black) ? QStringLiteral("black") : QStringLiteral("red");
            const QString fixtureName = QString::fromLatin1(fixture.name) + QLatin1Char('-') + colorName;
            output.append(QJsonObject{
                {QStringLiteral("name"), fixtureName},
                {QStringLiteral("source"), fixture.source},
                {QStringLiteral("prepared"), fixture.source},
                {QStringLiteral("setup"), requestSetup(color)},
                {QStringLiteral("width"), 240},
                {QStringLiteral("fontSize"), 10},
                {QStringLiteral("color"), color.name(QColor::HexRgb)},
            });
        }
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    const QByteArray json = QJsonDocument(output).toJson(QJsonDocument::Indented);
    QCOMPARE(file.write(json), qint64(json.size()));
    QVERIFY(file.flush());
}

QTEST_GUILESS_MAIN(LatexSourcePreparationTest)

#include "latexsourcepreparationtest.moc"
