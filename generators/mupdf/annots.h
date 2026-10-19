// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QHash>
#include <core/annotations.h>
#include <mupdf/pdf.h>

class MuPDFGenerator;
class MuPDFAnnotationProxy : public Okular::AnnotationProxy
{
public:
    explicit MuPDFAnnotationProxy(MuPDFGenerator *generator)
        : m_generator(generator)
    {
    }
    ~MuPDFAnnotationProxy() override;
    bool supports(Capability) const override
    {
        return true;
    }
    std::shared_ptr<void> annotationAppearance(const Okular::Annotation *) const override;
    void notifyAddition(Okular::Annotation *, int) override;
    void notifyModification(const Okular::Annotation *, int, bool) override;
    void notifyRemoval(Okular::Annotation *, int) override;
    void load(pdf_page *, Okular::Page *);
    void clear();
    void preparePageRotation(Okular::Page *page, bool followPage);
    void finishPageRotation(Okular::Page *page, bool followPage);

private:
    void prepareAnnotationsRotation(const QList<Okular::Annotation *> &annotations, bool followPage);
    void loadOne(pdf_annot *, Okular::Page *, fz_rect, Okular::Annotation *&);
    void write(const Okular::Annotation *, pdf_annot *, bool, pdf_obj *preserved = nullptr);
    MuPDFGenerator *m_generator;
    QHash<const Okular::Annotation *, pdf_annot *> m_annotations;
    QHash<const Okular::Annotation *, pdf_obj *> m_removed;
};
