// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <core/form.h>
#include <core/page.h>

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
        return access->value();
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
        case PDF_WIDGET_TYPE_TEXT:
            fields.append(new TextField(access));
            break;
        case PDF_WIDGET_TYPE_BUTTON:
        case PDF_WIDGET_TYPE_CHECKBOX:
        case PDF_WIDGET_TYPE_RADIOBUTTON:
            fields.append(new ButtonField(access));
            break;
        case PDF_WIDGET_TYPE_COMBOBOX:
        case PDF_WIDGET_TYPE_LISTBOX:
            fields.append(new ChoiceField(access));
            break;
        default:
            break;
        }
    }
    page->setFormFields(fields);
    for (const auto &access : accesses)
        access->initializing = false;
}
