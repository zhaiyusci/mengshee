/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "part/latexrenderer.h"
#include "part/latexrenderguards.h"
#include <QFile>
#include <QtTest>
#include <limits>
#include <stdexcept>

using namespace GuiUtils;
namespace {
struct State {
    LatexRenderRequest request;
    LatexBackendResult result;
    int calls = 0;
    int warmups = 0;
    int restarts = 0;
    int exception = 0;
};
std::shared_ptr<State> factoryState;
class IndependentBackend final : public LatexBackend
{
public:
    explicit IndependentBackend(std::shared_ptr<State> state) : m_state(std::move(state)) {}
    QString id() const override { return QStringLiteral("independent-test-engine"); }
    LatexBackendResult render(const LatexRenderRequest &request) override
    {
        ++m_state->calls;
        m_state->request = request;
        if (m_state->exception == 1)
            throw std::runtime_error("backend failed");
        if (m_state->exception == 2)
            throw 42;
        return m_state->result;
    }
    void prewarm() override { ++m_state->warmups; }
    void restart() override { ++m_state->restarts; }
private:
    std::shared_ptr<State> m_state;
};
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
}

// Deliberately replace the application composition point. This executable
// does not compile/link any StemTeX adapter, worker, settings or IPC code.
std::unique_ptr<LatexBackend> GuiUtils::createDefaultLatexBackend()
{
    return std::make_unique<IndependentBackend>(factoryState);
}

class LatexBackendTest : public QObject
{
    Q_OBJECT
    std::shared_ptr<State> state;
private Q_SLOTS:
    void init()
    {
        state = std::make_shared<State>();
        state->result.success = true;
        state->result.pdf = read(QStringLiteral(KDESRCDIR "data/file1.pdf"));
        QVERIFY(state->result.pdf.startsWith("%PDF-"));
        factoryState = state;
    }

    void rawRequestAndOwnedOutput()
    {
        QString path;
        const auto bytes = state->result.pdf;
        {
            LatexRenderer renderer(std::make_unique<IndependentBackend>(state));
            QString diagnostic;
            const QString source = QStringLiteral("  $x$\\\\\n% terminal comment\n\n");
            const QColor color(30, 80, 150, 128);
            QCOMPARE(renderer.renderLatexToPdf(source, color, path, diagnostic, 241.75, 12.5), LatexRenderer::NoError);
            QCOMPARE(state->request.source, source);
            QCOMPARE(state->request.textColor, color);
            QCOMPARE(state->request.widthPoints, 241.75);
            QCOMPARE(state->request.fontSizePoints, 12.5);
            QCOMPARE(renderer.lastBackendName(), QStringLiteral("independent-test-engine"));
            state->result.pdf.clear(); // The caller's file owns its bytes.
            QCOMPARE(read(path), bytes);
        }
        QVERIFY(!QFile::exists(path));
    }

    void defaultBackendAndLifecycle()
    {
        LatexRenderer::prewarm();
        LatexRenderer::restart();
        QCOMPARE(state->warmups, 1);
        QCOMPARE(state->restarts, 1);
        LatexRenderer renderer;
        QString path, diagnostic;
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::black, path, diagnostic), LatexRenderer::NoError);
        QCOMPARE(state->request.widthPoints, 0.0);
        QCOMPARE(state->request.fontSizePoints, 0.0);
    }

    void invalidInputNeverCallsBackend()
    {
        LatexRenderer renderer(std::make_unique<IndependentBackend>(state));
        QString path = QStringLiteral("old"), diagnostic;
        QCOMPARE(renderer.renderLatexToPdf(QString(), Qt::black, path, diagnostic), LatexRenderer::LatexFailed);
        QVERIFY(path.isEmpty());
        QVERIFY(!diagnostic.isEmpty());
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::black, path, diagnostic, std::numeric_limits<double>::quiet_NaN()), LatexRenderer::LatexFailed);
        QCOMPARE(state->calls, 0);
    }

    void resultValidation_data()
    {
        QTest::addColumn<QByteArray>("pdf");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("non-pdf") << QByteArray("<html>error</html>");
        QTest::newRow("oversized") << (QByteArray("%PDF-") + QByteArray(LatexRenderGuards::maxPdfBytes, 'x'));
    }

    void resultValidation()
    {
        QFETCH(QByteArray, pdf);
        state->result.pdf = pdf;
        LatexRenderer renderer(std::make_unique<IndependentBackend>(state));
        QString path, diagnostic;
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::black, path, diagnostic), LatexRenderer::LatexFailed);
        QVERIFY(path.isEmpty());
        QVERIFY(!diagnostic.isEmpty());
    }

    void warningsFailuresAndExceptions()
    {
        LatexRenderer renderer(std::make_unique<IndependentBackend>(state));
        state->result.warning = {LatexRenderWarningType::BackendLimitation, QStringLiteral("warning"), .5};
        state->result.diagnostic = QStringLiteral("backend diagnostic");
        QString path, diagnostic;
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::red, path, diagnostic), LatexRenderer::NoError);
        QCOMPARE(diagnostic, state->result.diagnostic);
        QCOMPARE(renderer.lastWarning().type, LatexRenderWarningType::BackendLimitation);
        QCOMPARE(renderer.lastWarning().severity, .5);
        const QString previousPath = path;
        state->result.success = false;
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::red, path, diagnostic), LatexRenderer::LatexFailed);
        QVERIFY(path.isEmpty());
        QVERIFY(!renderer.lastWarning().isValid());
        QVERIFY(QFile::exists(previousPath));
        for (int exception : {1, 2}) {
            state->exception = exception;
            QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::red, path, diagnostic), LatexRenderer::LatexFailed);
            QVERIFY(!diagnostic.isEmpty());
            QVERIFY(path.isEmpty());
        }
    }

    void independentArtifactLifetimes()
    {
        auto other = std::make_shared<State>(*state);
        LatexRenderer second(std::make_unique<IndependentBackend>(other));
        QString firstPath, secondPath, diagnostic;
        {
            LatexRenderer first(std::make_unique<IndependentBackend>(state));
            QCOMPARE(first.renderLatexToPdf(QStringLiteral("same"), Qt::black, firstPath, diagnostic), LatexRenderer::NoError);
            QCOMPARE(second.renderLatexToPdf(QStringLiteral("same"), Qt::black, secondPath, diagnostic), LatexRenderer::NoError);
            QVERIFY(firstPath != secondPath);
        }
        QVERIFY(!QFile::exists(firstPath));
        QCOMPARE(read(secondPath), other->result.pdf);
    }

    void missingBackendFailsExplicitly()
    {
        LatexRenderer renderer(nullptr);
        QString path, diagnostic;
        QCOMPARE(renderer.renderLatexToPdf(QStringLiteral("text"), Qt::black, path, diagnostic), LatexRenderer::LatexFailed);
        QVERIFY(!diagnostic.isEmpty());
        QVERIFY(path.isEmpty());
    }
};
QTEST_GUILESS_MAIN(LatexBackendTest)
#include "latexbackendtest.moc"
