// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation.h"

namespace QindaOffice::Show {

QString Shape::displayText() const
{
    QString out;
    for (const TextParagraph &paragraph : paragraphs) {
        for (const TextRun &run : paragraph.runs) {
            out += run.text;
        }
        out += QLatin1Char('\n');
    }
    return out.trimmed();
}

Shape *Slide::findShape(const QString &shapeId)
{
    for (Shape &shape : shapes) {
        if (shape.id == shapeId) {
            return &shape;
        }
    }
    return nullptr;
}

Slide &Presentation::addSlide(const QString &slideTitle)
{
    Slide slide;
    slide.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    slide.title = slideTitle;
    slides.append(slide);
    return slides.last();
}

Slide *Presentation::findSlide(const QString &slideId)
{
    for (Slide &slide : slides) {
        if (slide.id == slideId) {
            return &slide;
        }
    }
    return nullptr;
}

} // namespace QindaOffice::Show
