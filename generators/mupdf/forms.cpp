// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QDebug>
#include <QHash>
#include <QMutex>
#include <atomic>
#include <core/action.h>
#include <core/form.h>
#include <core/page.h>
#include <cstring>

class MuPDFFormAccess
{
public:
    MuPDFFormAccess(MuPDFGenerator *generator, pdf_annot *widget, Okular::NormalizedRect rect)
        : generator(generator)
        , widget(pdf_keep_widget(generator->m_context, widget))
        , rectangle(rect)
        , identifier(pdf_to_num(generator->m_context, pdf_annot_obj(generator->m_context, widget)))
    {
    }
    ~MuPDFFormAccess()
    {
        pdf_drop_widget(generator->m_context, widget);
    }
    void read(const std::function<void(fz_context *, pdf_document *, pdf_annot *)> &body) const
    {
        if (initializing)
            body(generator->m_context, generator->m_document, widget);
        else
            generator->readOperation([&] { body(generator->m_context, generator->m_document, widget); }, nullptr);
    }
    void write(const std::function<void(fz_context *, pdf_document *, pdf_annot *)> &body)
    {
        QString error;
        if (!generator->operation(
                [&] {
                    body(generator->m_context, generator->m_document, widget);
                    pdf_update_widget(generator->m_context, widget);
                },
                &error))
            generator->m_editError = error;
    }
    int flags() const
    {
        int result = 0;
        read([&](auto *ctx, auto *, auto *w) { result = pdf_field_flags(ctx, pdf_annot_obj(ctx, w)); });
        return result;
    }
    QString value() const
    {
        QString result;
        read([&](auto *ctx, auto *, auto *w) { result = QString::fromUtf8(pdf_field_value(ctx, pdf_annot_obj(ctx, w))); });
        return result;
    }
    /** JavaScript of the field's /AA/C calculate action, when it has one. */
    QString calculateScript() const
    {
        QString result;
        read([&](auto *ctx, auto *, auto *w) {
            auto *script = pdf_dict_getp_inheritable(ctx, pdf_annot_obj(ctx, w), "AA/C/JS");
            if (!script)
                return;
            char *code = pdf_load_stream_or_string_as_utf8(ctx, script);
            if (code) {
                result = QString::fromUtf8(code);
                fz_free(ctx, code);
            }
        });
        return result;
    }
    void setValue(const QString &value)
    {
        const auto text = value.toUtf8();
        write([&](auto *ctx, auto *doc, auto *w) {
            if (!pdf_set_field_value(ctx, doc, pdf_annot_obj(ctx, w), text.constData(), 0))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "The form field rejected this value");
        });
    }
    void setAppearanceValue(const QString &value)
    {
        const auto bytes = value.toUtf8();
        write([&](auto *ctx, auto *, auto *w) {
            auto *object = pdf_annot_obj(ctx, w);
            auto *old = pdf_keep_obj(ctx, pdf_dict_get(ctx, object, PDF_NAME(V)));
            fz_try(ctx)
            {
                pdf_dict_put_text_string(ctx, object, PDF_NAME(V), bytes.constData());
                pdf_annot_request_resynthesis(ctx, w);
                pdf_update_widget(ctx, w);
            }
            fz_always(ctx)
            {
                if (old)
                    pdf_dict_put(ctx, object, PDF_NAME(V), old);
                else
                    pdf_dict_del(ctx, object, PDF_NAME(V));
                pdf_drop_obj(ctx, old);
            }
            fz_catch(ctx)
            {
                fz_rethrow(ctx);
            }
        });
    }
    MuPDFGenerator *generator;
    pdf_annot *widget;
    Okular::NormalizedRect rectangle;
    int identifier;
    bool initializing = true;
};

template<class Base> class Field : public Base
{
public:
    explicit Field(std::shared_ptr<MuPDFFormAccess> access)
        : access(std::move(access))
    {
    }
    Okular::NormalizedRect rect() const override
    {
        return access->rectangle;
    }
    int id() const override
    {
        return access->identifier;
    }
    QString name() const override
    {
        QString result;
        access->read([&](auto *ctx, auto *, auto *w) { result = QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_get(ctx, pdf_annot_obj(ctx, w), PDF_NAME(T)))); });
        return result;
    }
    QString uiName() const override
    {
        QString result;
        access->read([&](auto *ctx, auto *, auto *w) { result = QString::fromUtf8(pdf_field_label(ctx, pdf_annot_obj(ctx, w))); });
        return result;
    }
    QString fullyQualifiedName() const override
    {
        QString result;
        access->read([&](auto *ctx, auto *, auto *w) {
            char *name = pdf_load_field_name(ctx, pdf_annot_obj(ctx, w));
            result = QString::fromUtf8(name);
            fz_free(ctx, name);
        });
        return result;
    }
    bool isReadOnly() const override
    {
        return access->flags() & PDF_FIELD_IS_READ_ONLY;
    }
    void setReadOnly(bool value) override
    {
        access->write([&](auto *ctx, auto *, auto *w) {
            auto *obj = pdf_annot_obj(ctx, w);
            int flags = pdf_field_flags(ctx, obj);
            pdf_dict_put_int(ctx, obj, PDF_NAME(Ff), value ? flags | 1 : flags & ~1);
        });
    }
    bool isVisible() const override
    {
        int flags = 0;
        access->read([&](auto *ctx, auto *, auto *w) { flags = pdf_annot_flags(ctx, w); });
        return !(flags & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_INVISIBLE | PDF_ANNOT_IS_NO_VIEW));
    }
    void setVisible(bool value) override
    {
        access->write([&](auto *ctx, auto *, auto *w) {
            int flags = pdf_annot_flags(ctx, w);
            pdf_set_annot_flags(ctx, w, value ? flags & ~(1 | 2 | 32) : flags | 2);
        });
    }
    bool isPrintable() const override
    {
        int flags = 0;
        access->read([&](auto *ctx, auto *, auto *w) { flags = pdf_annot_flags(ctx, w); });
        return flags & PDF_ANNOT_IS_PRINT;
    }
    void setPrintable(bool value) override
    {
        access->write([&](auto *ctx, auto *, auto *w) {
            int flags = pdf_annot_flags(ctx, w);
            pdf_set_annot_flags(ctx, w, value ? flags | 4 : flags & ~4);
        });
    }
    /**
     * Publishes the field's /AA/C JavaScript as an Okular CalculateField
     * action. DocumentPrivate::recalculateForms() executes these actions in
     * the /CO order exposed through metaData("FormCalculateOrder").
     */
    void setCalculateScript(const QString &script)
    {
        this->setAdditionalAction(Okular::FormField::CalculateField, new Okular::ScriptAction(Okular::JavaScript, script));
    }

protected:
    Qt::Alignment alignment() const
    {
        int align = 0;
        access->read([&](auto *ctx, auto *, auto *w) { align = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, pdf_annot_obj(ctx, w), PDF_NAME(Q))); });
        return align == 1 ? Qt::AlignHCenter : align == 2 ? Qt::AlignRight : Qt::AlignLeft;
    }
    std::shared_ptr<MuPDFFormAccess> access;
};
class TextField final : public Field<Okular::FormFieldText>
{
public:
    using Field::Field;
    TextType textType() const override
    {
        return access->flags() & PDF_TX_FIELD_IS_MULTILINE ? Multiline : access->flags() & PDF_TX_FIELD_IS_FILE_SELECT ? FileSelect : Normal;
    }
    QString text() const override
    {
        return access->value();
    }
    void setText(const QString &text) override
    {
        access->setValue(text);
    }
    void setAppearanceText(const QString &text) override
    {
        access->setAppearanceValue(text);
    }
    bool isPassword() const override
    {
        return access->flags() & PDF_TX_FIELD_IS_PASSWORD;
    }
    bool isRichText() const override
    {
        return access->flags() & PDF_TX_FIELD_IS_RICH_TEXT;
    }
    int maximumLength() const override
    {
        int result = 0;
        access->read([&](auto *ctx, auto *, auto *w) { result = pdf_text_widget_max_len(ctx, w); });
        return result;
    }
    Qt::Alignment textAlignment() const override
    {
        return alignment();
    }
    bool canBeSpellChecked() const override
    {
        return !(access->flags() & PDF_TX_FIELD_IS_DO_NOT_SPELL_CHECK);
    }
};
class ButtonField final : public Field<Okular::FormFieldButton>
{
public:
    using Field::Field;
    ButtonType buttonType() const override
    {
        return access->flags() & PDF_BTN_FIELD_IS_PUSHBUTTON ? Push : access->flags() & PDF_BTN_FIELD_IS_RADIO ? Radio : CheckBox;
    }
    QString caption() const override
    {
        QString result;
        access->read([&](auto *ctx, auto *, auto *w) { result = QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_getp(ctx, pdf_annot_obj(ctx, w), "MK/CA"))); });
        return result;
    }
    bool state() const override
    {
        bool result = false;
        access->read([&](auto *ctx, auto *, auto *w) {
            auto *state = pdf_dict_get(ctx, pdf_annot_obj(ctx, w), PDF_NAME(AS));
            result = state && !pdf_name_eq(ctx, state, PDF_NAME(Off));
        });
        return result;
    }
    void setState(bool checked) override
    {
        access->write([&](auto *ctx, auto *doc, auto *w) {
            auto *field = pdf_annot_obj(ctx, w);
            auto *on = pdf_button_field_on_state(ctx, field);
            pdf_set_field_value(ctx, doc, field, checked ? pdf_to_name(ctx, on) : "Off", 0);
        });
    }
    QList<int> siblings() const override
    {
        QList<int> result;
        access->read([&](auto *ctx, auto *, auto *w) {
            auto *kids = pdf_dict_getp(ctx, pdf_annot_obj(ctx, w), "Parent/Kids");
            for (int i = 0; i < pdf_array_len(ctx, kids); ++i) {
                const int other = pdf_to_num(ctx, pdf_array_get(ctx, kids, i));
                if (other != id())
                    result.append(other);
            }
        });
        return result;
    }
};
class ChoiceField final : public Field<Okular::FormFieldChoice>
{
public:
    using Field::Field;
    ChoiceType choiceType() const override
    {
        return access->flags() & PDF_CH_FIELD_IS_COMBO ? ComboBox : ListBox;
    }
    QStringList choices() const override
    {
        QStringList result;
        access->read([&](auto *ctx, auto *, auto *w) {
            const int count = pdf_choice_widget_options(ctx, w, 0, nullptr);
            QVector<const char *> options(count);
            pdf_choice_widget_options(ctx, w, 0, options.data());
            for (auto *option : options)
                result.append(QString::fromUtf8(option));
        });
        return result;
    }
    bool isEditable() const override
    {
        return access->flags() & PDF_CH_FIELD_IS_EDIT;
    }
    bool multiSelect() const override
    {
        return access->flags() & PDF_CH_FIELD_IS_MULTI_SELECT;
    }
    QList<int> currentChoices() const override
    {
        QList<int> result;
        access->read([&](auto *ctx, auto *, auto *w) {
            const int count = pdf_choice_widget_value(ctx, w, nullptr);
            QVector<const char *> selected(count);
            pdf_choice_widget_value(ctx, w, selected.data());
            auto *field = pdf_annot_obj(ctx, w);
            for (int i = 0; i < pdf_choice_field_option_count(ctx, field); ++i)
                for (auto *value : selected)
                    if (qstrcmp(value, pdf_choice_field_option(ctx, field, 1, i)) == 0) {
                        result.append(i);
                        break;
                    }
        });
        return result;
    }
    void setCurrentChoices(const QList<int> &choices) override
    {
        access->write([&](auto *ctx, auto *, auto *w) {
            QVector<const char *> values;
            auto *field = pdf_annot_obj(ctx, w);
            for (int i : choices) {
                if (i < 0 || i >= pdf_choice_field_option_count(ctx, field))
                    fz_throw(ctx, FZ_ERROR_ARGUMENT, "Invalid form choice");
                values.append(pdf_choice_field_option(ctx, field, 1, i));
            }
            pdf_choice_widget_set_value(ctx, w, values.size(), values.data());
        });
    }
    QString editChoice() const override
    {
        const QString value = access->value();
        // For an editable combo box, /V may name one of the predefined
        // choices; that is a selected choice, not free text. Poppler exposes
        // the free-text editor value separately, so keep editChoice empty
        // while the value matches a choice.
        return choices().contains(value) ? QString() : value;
    }
    void setEditChoice(const QString &text) override
    {
        access->setValue(text);
    }
    void setAppearanceChoiceText(const QString &text) override
    {
        access->setAppearanceValue(text);
    }
    Qt::Alignment textAlignment() const override
    {
        return alignment();
    }
    bool canBeSpellChecked() const override
    {
        return !(access->flags() & PDF_CH_FIELD_IS_DO_NOT_SPELL_CHECK);
    }
};
class SignatureField final : public Field<Okular::FormFieldSignature>
{
public:
    using Field::Field;
    SignatureType signatureType() const override
    {
        SignatureType result = Okular::FormFieldSignature::UnsignedSignature;
        access->read([&](auto *ctx, auto *doc, auto *w) {
            auto *object = pdf_annot_obj(ctx, w);
            if (!pdf_signature_is_signed(ctx, doc, object)) {
                result = Okular::FormFieldSignature::UnsignedSignature;
                return;
            }
            // The SubFilter lives in the signature dictionary referenced by
            // /V. MuPDF only knows the ADBE detached name, so compare the
            // remaining standard names by value.
            auto *subFilter = pdf_dict_getp(ctx, object, "V/SubFilter");
            if (!pdf_is_name(ctx, subFilter)) {
                result = Okular::FormFieldSignature::UnknownType;
                return;
            }
            const char *name = pdf_to_name(ctx, subFilter);
            if (std::strcmp(name, "adbe.pkcs7.detached") == 0) {
                result = Okular::FormFieldSignature::AdbePkcs7detached;
            } else if (std::strcmp(name, "adbe.pkcs7.sha1") == 0) {
                result = Okular::FormFieldSignature::AdbePkcs7sha1;
            } else if (std::strcmp(name, "ETSI.CAdES.detached") == 0) {
                result = Okular::FormFieldSignature::EtsiCAdESdetached;
            } else {
                result = Okular::FormFieldSignature::UnknownType;
            }
        });
        return result;
    }
    Okular::SignatureInfo signatureInfo() const override
    {
        Okular::SignatureInfo info;
        access->read([&](auto *ctx, auto *doc, auto *w) {
            auto *object = pdf_annot_obj(ctx, w);
            if (!pdf_signature_is_signed(ctx, doc, object)) {
                info.setSignatureStatus(Okular::SignatureInfo::SignatureNotFound);
                return;
            }
            // MuPDF offers no certificate chain validation here. Expose the
            // signing dictionary's descriptive entries and let the GUI treat
            // the cryptographic status as not verified.
            info.setSignatureStatus(Okular::SignatureInfo::SignatureNotVerified);
            info.setCertificateStatus(Okular::SignatureInfo::CertificateNotVerified);
            const auto text = [&](const char *path) {
                return QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_getp(ctx, object, path)));
            };
            info.setSignerName(text("V/Name"));
            info.setReason(text("V/Reason"));
            info.setLocation(text("V/Location"));
        });
        return info;
    }
    std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &, const QString &) const override
    {
        return {Okular::SigningResult::UnsupportedSignatureType, QStringLiteral("Signing is not supported by the MuPDF backend.")};
    }
    SubscriptionHandle subscribeUpdates(const std::function<void()> &callback) const override
    {
        static std::atomic<SubscriptionHandle> nextHandle{0};
        const SubscriptionHandle handle = ++nextHandle;
        m_subscriptions.insert(handle, callback);
        return handle;
    }
    bool unsubscribeUpdates(const SubscriptionHandle &handle) const override
    {
        return m_subscriptions.remove(handle);
    }

private:
    // Kept for interface completeness: MuPDF does not run asynchronous
    // certificate validation, so no callback is ever invoked.
    mutable QHash<SubscriptionHandle, std::function<void()>> m_subscriptions;
};

namespace {
/** Attaches /AA/C as the Okular action DocumentPrivate::recalculateForms() runs. */
template<class FieldType> void attachCalculateAction(FieldType *field, const std::shared_ptr<MuPDFFormAccess> &access)
{
    const QString script = access->calculateScript();
    if (!script.isEmpty())
        field->setCalculateScript(script);
}
}

void MuPDFGenerator::loadForms(pdf_page *native, Okular::Page *page)
{
    QList<Okular::FormField *> fields;
    QList<std::shared_ptr<MuPDFFormAccess>> accesses;
    const auto bounds = pdf_bound_page(m_context, native, FZ_CROP_BOX);
    for (auto *widget = pdf_first_widget(m_context, native); widget; widget = pdf_next_widget(m_context, widget)) {
        const auto box = pdf_bound_widget(m_context, widget);
        auto access = std::make_shared<MuPDFFormAccess>(
            this,
            widget,
            Okular::NormalizedRect((box.x0 - bounds.x0) / (bounds.x1 - bounds.x0), (box.y0 - bounds.y0) / (bounds.y1 - bounds.y0), (box.x1 - bounds.x0) / (bounds.x1 - bounds.x0), (box.y1 - bounds.y0) / (bounds.y1 - bounds.y0)));
        accesses.append(access);
        switch (pdf_widget_type(m_context, widget)) {
        case PDF_WIDGET_TYPE_TEXT: {
            auto *field = new TextField(access);
            attachCalculateAction(field, access);
            fields.append(field);
            break;
        }
        case PDF_WIDGET_TYPE_BUTTON:
        case PDF_WIDGET_TYPE_CHECKBOX:
        case PDF_WIDGET_TYPE_RADIOBUTTON: {
            auto *field = new ButtonField(access);
            attachCalculateAction(field, access);
            fields.append(field);
            break;
        }
        case PDF_WIDGET_TYPE_COMBOBOX:
        case PDF_WIDGET_TYPE_LISTBOX: {
            auto *field = new ChoiceField(access);
            attachCalculateAction(field, access);
            fields.append(field);
            break;
        }
        case PDF_WIDGET_TYPE_SIGNATURE:
            fields.append(new SignatureField(access));
            break;
        default:
            break;
        }
    }
    page->setFormFields(fields);
    for (const auto &access : accesses)
        access->initializing = false;
}

namespace {
/**
 * Appends the widget object numbers of one /CO entry. The calculation array
 * normally names terminal fields, but a field without /Kids is added as-is so
 * non-widget terminal fields do not hide their descendants.
 */
void collectCalculateFieldIds(fz_context *ctx, pdf_obj *entry, QList<int> *ids)
{
    if (!pdf_is_dict(ctx, entry))
        return;
    if (pdf_name_eq(ctx, pdf_dict_get(ctx, entry, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
        const int id = pdf_to_num(ctx, entry);
        if (id > 0 && !ids->contains(id))
            ids->append(id);
        return;
    }
    auto *kids = pdf_dict_get(ctx, entry, PDF_NAME(Kids));
    if (!pdf_is_array(ctx, kids)) {
        const int id = pdf_to_num(ctx, entry);
        if (id > 0 && !ids->contains(id))
            ids->append(id);
        return;
    }
    for (int i = 0; i < pdf_array_len(ctx, kids); ++i)
        collectCalculateFieldIds(ctx, pdf_array_get(ctx, kids, i), ids);
}
}

QList<int> MuPDFGenerator::formCalculateOrder() const
{
    QList<int> ids;
    QMutexLocker lock(userMutex());
    if (!m_document)
        return ids;
    fz_try(m_context)
    {
        auto *order = pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/AcroForm/CO");
        for (int i = 0; i < pdf_array_len(m_context, order); ++i)
            collectCalculateFieldIds(m_context, pdf_array_get(m_context, order, i), &ids);
    }
    fz_catch(m_context)
    {
        qWarning() << "MuPDF form calculate order:" << fz_caught_message(m_context);
        ids.clear();
    }
    return ids;
}