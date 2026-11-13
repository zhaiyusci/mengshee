// SPDX-License-Identifier: GPL-2.0-or-later
#include "dlgaiservice.h"
#include "aichat.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QTimer>

namespace
{
QLineEdit *addField(QFormLayout *form, const QString &name, const QString &label)
{
    auto *edit = new QLineEdit(form->parentWidget());
    edit->setObjectName(QStringLiteral("kcfg_") + name);
    form->addRow(label, edit);
    return edit;
}
}

DlgAiService::DlgAiService(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->addRow(new QLabel(i18n("Searchable OCR text — OpenAI-compatible vision chat (tested with GLM)"), this));
    auto *base = addField(form, QStringLiteral("BaseUrl"), i18n("API base URL:"));
    base->setPlaceholderText(QStringLiteral("https://open.bigmodel.cn/api/paas/v4"));
    auto *model = addField(form, QStringLiteral("ModelId"), i18n("Model name:"));
    model->setPlaceholderText(i18n("Tested with glm-5.3-flash"));
    auto *key = addField(form, QStringLiteral("ApiKey"), i18n("API key:"));
    key->setEchoMode(QLineEdit::Password);
    auto *hint = new QLabel(i18n("A pasted key is saved in your local settings. To keep the key outside Mengshee, enter an environment variable such as $OPENAI_API_KEY. Leave blank for services without authentication."), this);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    auto *test = new QPushButton(i18n("Test connection"), this);
    test->setToolTip(i18n("Send a short text request to the selected model."));
    auto *status = new QLabel(this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    form->addRow(QString(), test);
    form->addRow(QString(), status);
    for (auto *edit : {base, model, key}) {
        connect(edit, &QLineEdit::textChanged, status, &QLabel::clear);
    }

    auto *network = new QNetworkAccessManager(this);
    connect(test, &QPushButton::clicked, this, [=] {
        const QJsonObject message {{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Reply with OK.")}};
        QString error;
        auto *reply = AiChat::post(*network, base->text(), model->text(), key->text(), {message}, 30000, error);
        if (!reply) {
            status->setText(error);
            return;
        }
        const auto originalBase = base->text();
        const auto originalModel = model->text();
        const auto originalKey = key->text();
        test->setEnabled(false);
        status->setText(i18n("Testing connection…"));
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            test->setEnabled(true);
            if (originalBase != base->text() || originalModel != model->text() || originalKey != key->text()) {
                return;
            }
            QString requestError;
            AiChat::answer(reply, requestError);
            status->setText(requestError.isEmpty() ? i18n("The model responded successfully.") : requestError);
        });
    });
}

DlgDocumentAnalysis::DlgDocumentAnalysis(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    auto *provider = new QComboBox(this);
    provider->setObjectName(QStringLiteral("kcfg_LayoutProvider"));
    provider->addItem(i18n("Zhipu GLM-OCR"));
    form->addRow(i18n("Service provider:"), provider);
    auto *key = addField(form, QStringLiteral("LayoutApiKey"), i18n("Zhipu API key:"));
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("$ZHIPU_API_KEY"));
}
