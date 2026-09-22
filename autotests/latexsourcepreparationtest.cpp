/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "part/latexsource.h"

#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using GuiUtils::LatexSource::prepareLeadingDisplay;
using GuiUtils::LatexSource::prepareSnippet;

class LatexSourcePreparationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void preparation_data();
    void preparation();
    void snippet_data();
    void snippet();
    void exportFixtures();
    void terminalBackslashBoundary();
    void logicalBoxStructure();
    void sourceBytesPreserved();
    void displayMathBytesPreserved();
};

void LatexSourcePreparationTest::preparation_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("expected");

    const auto display = [](const char *name, const QString &prefix, const QString &body) {
        QTest::newRow(name) << prefix + body << prefix + QStringLiteral("\\hrule width\\hsize height0pt depth0pt\\relax\\noindent%\n") + body;
    };
    const auto unchanged = [](const char *name, const QString &source) {
        QTest::newRow(name) << source << source;
    };
    display("dollars-followed-by-text", QString(), QStringLiteral("$$\\alpha$$ test"));
    display("brackets", QString(), QStringLiteral("\\[\\alpha\\]"));
    display("unary-plus", QString(), QStringLiteral("$$+x$$"));
    display("unary-minus", QString(), QStringLiteral("$$-x$$"));
    display("leading-relation", QString(), QStringLiteral("$$=x$$"));
    display("nested-aligned", QString(), QStringLiteral("$$\\begin{aligned}a&=b\\\\c&=d\\end{aligned}$$"));
    QTest::newRow("comment-brace-offset")
        << QStringLiteral("% start\r\n\\begin% fake } and {\r\n {equation% fake }\n}x\\end{equation}")
        << QStringLiteral("% start\r\n\\hrule width\\hsize height0pt depth0pt\\relax\\noindent%\n\\begin% fake } and {\r\n {equation% fake }\n}x\\end{equation}");
    display("leading-crlf", QStringLiteral(" \t\r\n\r\n"), QStringLiteral("$$x$$ test"));
    display("leading-comments", QStringLiteral("% fake \\[ and $$\r\n \t% another\n\n"), QStringLiteral("\\[x\\]"));
    display("comment-before-command-brace", QString(), QStringLiteral("\\begin% keep\r\n {equation}x\\end{equation}"));
    display("comment-before-environment", QString(), QStringLiteral("\\begin { % keep\n align* % keep too\r\n }x\\end{align*}"));
    display("environment-whitespace", QString(), QStringLiteral("\\begin\t\r\n{\t equation* \r\n}x\\end{equation*}"));
    display("tail-comment", QString(), QStringLiteral("$$x$$ % preserved without terminal newline"));
    display("tail-newlines", QString(), QStringLiteral("\\[x\\]\r\n\r\n \t"));
    display("explicit-space", QString(), QStringLiteral("$$x$$\\hspace{2pt} \\kern1pt\n"));
    display("multiple-displays", QString(), QStringLiteral("$$x$$\n$$y$$"));
    display("leading-comment-backslashes", QStringLiteral("% \\% still inside comment\r\n% \\\\% too\n"), QStringLiteral("$$x$$"));

    const QStringList environments = {
        QStringLiteral("displaymath"),
        QStringLiteral("equation"),
        QStringLiteral("equation*"),
        QStringLiteral("align"),
        QStringLiteral("align*"),
        QStringLiteral("gather"),
        QStringLiteral("gather*"),
        QStringLiteral("multline"),
        QStringLiteral("multline*"),
        QStringLiteral("flalign"),
        QStringLiteral("flalign*"),
        QStringLiteral("alignat"),
        QStringLiteral("alignat*"),
    };
    for (const QString &environment : environments) {
        const QByteArray name = environment.toLatin1();
        display(name.constData(), QString(), QStringLiteral("\\begin{") + environment + QStringLiteral("}x\\end{") + environment + QLatin1Char('}'));
    }

    unchanged("empty", QString());
    unchanged("whitespace", QStringLiteral(" \t\r\n\n"));
    unchanged("comment-only", QStringLiteral(" % $$x$$ \\[x\\]"));
    unchanged("comment-then-text", QStringLiteral("% $$x$$\r\ntext"));
    unchanged("escaped-percent-text", QStringLiteral("\\% $$x$$"));
    unchanged("even-backslashes-before-percent", QStringLiteral("\\\\% $$fake$$\n$$x$$"));
    unchanged("odd-backslashes-before-percent", QStringLiteral("\\\\\\% $$x$$"));
    unchanged("escaped-percent-in-begin", QStringLiteral("\\begin\\%\n{equation}x\\end{equation}"));
    unchanged("even-backslashes-in-begin", QStringLiteral("\\begin\\\\% comment\n{equation}x\\end{equation}"));
    unchanged("escaped-opening-bracket", QStringLiteral("\\\\[x\\]"));
    unchanged("inline-dollar", QStringLiteral("$x$"));
    unchanged("inline-parentheses", QStringLiteral("\\(x\\)"));
    unchanged("text-before-display", QStringLiteral("text\n$$x$$"));
    unchanged("inline-before-display", QStringLiteral("$x$\n\\[y\\]"));
    unchanged("macro-before-display", QStringLiteral("\\mysetup\n$$x$$"));
    unchanged("already-prepared", QStringLiteral("\\noindent%\n$$x$$"));
    unchanged("minipage", QStringLiteral("\\begin{minipage}{2cm}$$x$$\\end{minipage}"));
    unchanged("unknown-environment", QStringLiteral("\\begin{aligned}x\\end{aligned}"));
    unchanged("control-word-prefix", QStringLiteral("\\begingroup{equation}$$x$$"));
    unchanged("environment-prefix", QStringLiteral("\\begin{equations}x"));
    unchanged("non-ascii-leading-space", QStringLiteral("\u2003$$x$$"));
    unchanged("split-environment-name", QStringLiteral("\\begin{equa% comment\ntion}x"));
}

void LatexSourcePreparationTest::preparation()
{
    QFETCH(QString, source);
    QFETCH(QString, expected);
    const QString original = source;
    QCOMPARE(prepareLeadingDisplay(source), expected);
    QCOMPARE(source, original);
    QCOMPARE(prepareLeadingDisplay(expected), expected);
}

void LatexSourcePreparationTest::snippet_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("rgb");

    const QStringList sources = {
        QStringLiteral("plain text"),
        QStringLiteral("$$\\alpha$$ test"),
        QStringLiteral("% leading\r\n\r\n\\[x\\]"),
        QStringLiteral("text % terminal comment"),
        QStringLiteral("text\r\n\r\n\n"),
        QStringLiteral("text \\hspace{2pt}\\kern1pt \\ \t"),
        QStringLiteral("\\color{.}text"),
        QStringLiteral("\\color{.!50}text"),
        QStringLiteral("\\colorlet{saved}{.}\\color{saved}text"),
        QStringLiteral("\\color{blue}text"),
    };
    for (qsizetype index = 0; index < sources.size(); ++index) {
        const QByteArray suffix = QByteArray::number(index);
        const QByteArray blackName = QByteArrayLiteral("black-") + suffix;
        const QByteArray invalidName = QByteArrayLiteral("invalid-") + suffix;
        const QByteArray redName = QByteArrayLiteral("red-") + suffix;
        QTest::newRow(blackName.constData()) << sources.at(index) << QColor(Qt::black) << QStringLiteral("0.000000,0.000000,0.000000");
        QTest::newRow(invalidName.constData()) << sources.at(index) << QColor() << QStringLiteral("0.000000,0.000000,0.000000");
        QTest::newRow(redName.constData()) << sources.at(index) << QColor(Qt::red) << QStringLiteral("1.000000,0.000000,0.000000");
    }
}

void LatexSourcePreparationTest::snippet()
{
    QFETCH(QString, source);
    QFETCH(QColor, color);
    QFETCH(QString, rgb);
    const QString original = source;
    QString specialRgb = rgb;
    specialRgb.replace(QLatin1Char(','), QLatin1Char(' '));
    const QString expectedPrefix = QStringLiteral("\\begingroup%\n"
                                                 "\\expandafter\\let\\csname mengshee@note@endcolor\\endcsname\\relax%\n"
                                                 "\\ifdefined\\color%\n"
                                                 "\\color[rgb]{%1}%\n"
                                                 "\\else%\n"
                                                 "\\special{color push rgb %2}%\n"
                                                 "\\expandafter\\def\\csname mengshee@note@endcolor\\endcsname{\\special{color pop}}%\n"
                                                 "\\fi%\n")
                                       .arg(rgb, specialRgb);
    const QString prepared = prepareSnippet(source, color);
    const QString body = prepareLeadingDisplay(source);
    QVERIFY(prepared.startsWith(expectedPrefix));
    QVERIFY(prepared.contains(QStringLiteral("\\setbox\\MengsheeNoteBox=\\vbox{\\begingroup%\n") + body + QStringLiteral("%\n\\par%\n")));
    QVERIFY(prepared.endsWith(QStringLiteral("\\MengsheeNoteDepth%\n\\box\\MengsheeNoteBox%\n\\csname mengshee@note@endcolor\\endcsname%\n\\endgroup")));
    QCOMPARE(source, original);
    if (!color.isValid()) {
        QCOMPARE(prepared, prepareSnippet(source, QColor(Qt::black)));
    }
    QString wrapper = prepared;
    wrapper.remove(wrapper.indexOf(body), body.size());
    // Minimum metrics are measured from TeX, not imposed as fixed PDF padding.
    // In particular, preserve every authored display skip and never manufacture
    // an empty paragraph by inserting a strut in the post-display hmode.
    for (const QString &forbidden : {QStringLiteral("\\noindent\\strut\\par"), QStringLiteral("\\ifhmode\\strut"), QStringLiteral("\\everydisplay"), QStringLiteral("\\vskip"), QStringLiteral("\\vspace"), QStringLiteral("\\abovedisplayskip"), QStringLiteral("\\belowdisplayskip"), QStringLiteral("\\abovedisplayshortskip"), QStringLiteral("\\belowdisplayshortskip"), QStringLiteral("\\vrule"), QStringLiteral("\\vphantom"), QStringLiteral("\\mathopen"), QStringLiteral("\\mathclose")}) {
        QVERIFY(!wrapper.contains(forbidden));
    }
}

void LatexSourcePreparationTest::logicalBoxStructure()
{
    const QString prepared = prepareSnippet(QStringLiteral("a"), QColor(Qt::red));
    QCOMPARE(prepared.count(QStringLiteral("\\newbox")), 2);
    QCOMPARE(prepared.count(QStringLiteral("\\newdimen")), 1);
    for (const QString &name : {QStringLiteral("box"), QStringLiteral("scratch"), QStringLiteral("top")}) {
        const QString allocation = name == QLatin1String("top") ? QStringLiteral("\\newdimen") : QStringLiteral("\\newbox");
        QVERIFY(prepared.contains(QStringLiteral("\\ifcsname mengshee@note@%1\\endcsname\\else\\expandafter%2\\csname mengshee@note@%1\\endcsname\\fi").arg(name, allocation)));
    }
    QVERIFY(!prepared.contains(QStringLiteral("\\global")));
    const QString sourceClose = QStringLiteral("\\par%\n\\ifnum\\lastnodetype=1\\relax\\aftergroup\\aftergroup\\aftergroup\\MengsheeNoteLastLine\\fi%\n\\endgroup}");
    QVERIFY(prepared.contains(sourceClose));
    QVERIFY(prepared.indexOf(QStringLiteral("\\setbox\\MengsheeNoteScratch")) < prepared.lastIndexOf(QStringLiteral("\\MengsheeNoteDepth%\n")));
    const QString display = prepareSnippet(QStringLiteral("$$+x$$"), QColor(Qt::red));
    QVERIFY(display.contains(QStringLiteral("$$+x$$")));
    QVERIFY(!display.contains(QStringLiteral("\\vrule")));
    QVERIFY(!display.contains(QStringLiteral("\\vphantom")));
    QVERIFY(!display.contains(QStringLiteral("\\mathopen")));
    QVERIFY(!display.contains(QStringLiteral("\\mathclose")));
    QVERIFY(!display.contains(QStringLiteral("\\setbox\\MengsheeNoteScratch")));
    QVERIFY(!display.contains(QStringLiteral("\\ifhmode")));
    for (const QString &environment : {QStringLiteral("align"), QStringLiteral("gather"), QStringLiteral("multline"), QStringLiteral("flalign"), QStringLiteral("alignat")}) {
        const QString source = QStringLiteral("\\begin{%1}a&=b\\end{%1}").arg(environment);
        QVERIFY(!prepareLeadingDisplay(source).contains(QStringLiteral("\\vrule width0pt")));
    }
}

void LatexSourcePreparationTest::sourceBytesPreserved()
{
    const QString guard = QStringLiteral("\\hrule width\\hsize height0pt depth0pt\\relax\\noindent%\n");
    const QStringList sources = {
        QStringLiteral(" \t% leading \\% }\r\n\r\n$$+x$$  % tail\\"),
        QStringLiteral("\\begin% ignored }\r\n {equation% another }\n}\\alpha\\end{equation}\r\n\r\n"),
        QStringLiteral("\\begin { % head\n align* % tail\r\n }a&=b\\end{align*}"),
        QStringLiteral("% preserve UTF-8: 中文 α\r\n\\[g\\]\\hspace{3pt} \t"),
        QStringLiteral("a\r\n\r\ng % final comment"),
        QStringLiteral("\\vspace*{5pt}a\\\\[8pt]g\\vspace*{7pt}"),
        QStringLiteral("\\unknownSetup\n$$x$$"),
        QStringLiteral("$$\\alpha\\vrule width0pt height15pt depth2pt\\relax$$"),
        QStringLiteral("$$\\alpha$$ test\r\n\r\ntest"),
    };
    for (const QString &source : sources) {
        QString prepared = prepareLeadingDisplay(source);
        prepared.remove(guard);
        QCOMPARE(prepared.toUtf8(), source.toUtf8());
        const QString snippet = prepareSnippet(source, QColor(Qt::red));
        QVERIFY(snippet.contains(prepareLeadingDisplay(source)));
    }
}

void LatexSourcePreparationTest::displayMathBytesPreserved()
{
    const QString guard = QStringLiteral("\\hrule width\\hsize height0pt depth0pt\\relax\\noindent%\n");
    // The former insertion boundary is marked only to document regressions.
    // Remove the marker, then require the entire math list to stay byte-exact.
    const QStringList markedSources = {
        QStringLiteral("$$^{14}_{6}C|$$ test"),
        QStringLiteral("$$^2x|$$"),
        QStringLiteral("$$_2x|$$"),
        QStringLiteral("$$x+|$$"),
        QStringLiteral("$$x-|$$"),
        QStringLiteral("$$x=|$$"),
        QStringLiteral("$$x,|$$"),
        QStringLiteral("$$x;|$$"),
        QStringLiteral("$$x\\mathpunct{,}|$$"),
        QStringLiteral("$$\\begin{array}{cc}a&b\\\\c&d\\end{array}|$$"),
        QStringLiteral("$$\\mathop{lim}_{x\\to0}f(x)|$$"),
        QStringLiteral("$$\\text{$a$} + {x^{2+y}}|$$"),
        QStringLiteral("$$\\text{fake $$ and \\] }x|$$ trailing $$y$$"),
        QStringLiteral("$$\\alpha% fake $$ and }\r\n +x|$$"),
        QStringLiteral("$$x+\\%+\\{y\\}|$$"),
        QStringLiteral("$$\\begin{aligned}a&=b\\\\c&=d\\end{aligned}|$$"),
        QStringLiteral("\\[^2x% fake \\]\n|\\] tail"),
        QStringLiteral("\\[\\text{fake \\] }x|\\]"),
        QStringLiteral("\\[x\\\\% fake \\]\n+y|\\]"),
        QStringLiteral("\\begin{equation}^2x|\\end% fake }\r\n {equation}"),
        QStringLiteral("\\begin% fake }\n {equation*}x+% fake \\end{equation*}\n|\\end{ equation* }"),
        QStringLiteral("\\begin{displaymath}\\text{\\end{displaymath}}x|\\end{displaymath}"),
        QStringLiteral("$$ {a\\over b} |$$"),
    };
    for (const QString &marked : markedSources) {
        const qsizetype expectedClose = marked.indexOf(QLatin1Char('|'));
        QVERIFY(expectedClose >= 0);
        QString source = marked;
        source.remove(expectedClose, 1);
        const QString actual = prepareLeadingDisplay(source);
        QCOMPARE(actual, guard + source);
        QVERIFY(!actual.contains(QStringLiteral("\\vrule")));
        QVERIFY(!actual.contains(QStringLiteral("\\mathopen")));
        QVERIFY(!actual.contains(QStringLiteral("\\mathclose")));
        QVERIFY(!actual.contains(QStringLiteral("\\vphantom")));
        QString restored = actual;
        restored.remove(guard);
        QCOMPARE(restored.toUtf8(), source.toUtf8());
    }
    // Macro closes, infix primitives and incomplete editor input all retain
    // only the leading guard; preparation no longer needs to guess a close.
    const QStringList uncertainSources = {
        QStringLiteral("$$x"),
        QStringLiteral("$$x% $$"),
        QStringLiteral("$${x$$"),
        QStringLiteral("$$x}$$"),
        QStringLiteral("$$x$"),
        QStringLiteral("\\[x"),
        QStringLiteral("\\[x\\\\]"),
        QStringLiteral("\\begin{equation}x\\end{equation*}"),
        QStringLiteral("\\begin{equation}x% \\end{equation}"),
        QStringLiteral("$$\\bgroup x\\egroup$$"),
        QStringLiteral("$$\\def\\finish{$$}\\alpha\\finish test $$y$$"),
        QStringLiteral("$$\\gdef\\finish{$$}x\\finish test $$y$$"),
        QStringLiteral("$$\\let\\finish\\]x$$"),
        QStringLiteral("$$\\newcommand{\\finish}{$$}x\\finish test $$y$$"),
        QStringLiteral("$$a\\over b$$"),
        QStringLiteral("$$a\\atop b$$"),
        QStringLiteral("$$a\\above1pt b$$"),
        QStringLiteral("$$n\\choose k$$"),
        QStringLiteral("$$n\\brace k$$"),
        QStringLiteral("$$n\\brack k$$"),
        QStringLiteral("$$a\\overwithdelims()b$$"),
        QStringLiteral("$$a\\atopwithdelims()b$$"),
        QStringLiteral("$$a\\abovewithdelims()1pt b$$"),
        QStringLiteral("$$x\\eqno(1)$$"),
        QStringLiteral("$$x\\leqno(1)$$"),
        QStringLiteral("$$\\verb|$$|x$$"),
    };
    for (const QString &source : uncertainSources) {
        QCOMPARE(prepareLeadingDisplay(source), guard + source);
    }
}

void LatexSourcePreparationTest::terminalBackslashBoundary()
{
    const QString single = QStringLiteral("text\\");
    const QString singlePrepared = prepareSnippet(single, QColor(Qt::black));
    QVERIFY(singlePrepared.contains(single + QStringLiteral("\n\\par%\n")));
    QVERIFY(!singlePrepared.contains(single + QStringLiteral("%\n\\par%\n")));
    const QString doubled = QStringLiteral("text\\\\");
    QVERIFY(prepareSnippet(doubled, QColor(Qt::black)).contains(doubled + QStringLiteral("%\n\\par%\n")));
    const QString commented = QStringLiteral("text % comment\\");
    QVERIFY(prepareSnippet(commented, QColor(Qt::black)).contains(commented + QStringLiteral("\n\\par%\n")));
}

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
                {QStringLiteral("prepared"), prepareSnippet(fixture.source, color)},
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
