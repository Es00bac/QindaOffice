// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_ai.h"

#include "chat_types.h"
#include "model_catalog.h"
#include "secret_store.h"
#include "show_window.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QUuid>

namespace QindaOffice::Apps::Show {

namespace {

QString outlineForDeck(const ShowModel::Presentation &deck, int currentSlide)
{
    QStringList lines;
    for (int i = 0; i < deck.slides.size(); ++i) {
        const ShowModel::Slide &slide = deck.slides.at(i);
        lines.append(QStringLiteral("%1%2").arg(i == currentSlide ? QLatin1String("> ") : QLatin1String("  "))
                         .arg(slide.title));
        for (const ShowModel::Shape &shape : slide.shapes) {
            const QString text = shape.displayText();
            if (!text.isEmpty()) {
                lines.append(QStringLiteral("    - %1").arg(text));
                break; // one text summary per slide is enough context
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}



QString generationPrompt(const QString &outline)
{
    return QStringLiteral(
               "Turn the following outline into a slide deck. Reply with ONLY the deck in "
               "this exact format, one stanza per slide:\n\n## <slide title>\n- <body line>\n- "
               "<body line>\nNotes: <one sentence of speaker notes>\n\nRules: 3 to 8 slides, "
               "at most 4 body lines per slide, no blank lines between stanzas, plain text "
               "only.\n\nOutline:\n%1")
        .arg(outline);
}

} // namespace

ShowContext::ShowContext(ShowWindow *window)
    : m_window(window)
{
}

Ai::DocumentContext ShowContext::context(bool withDocument)
{
    Ai::DocumentContext out;
    const ShowModel::Presentation *deck = m_window->showDocument()->presentation();
    const int current = m_window->currentSlideRow();
    if (current >= 0 && current < deck->slides.size()) {
        out.selection = deck->slides.at(current).title;
    }
    if (withDocument) {
        out.document = outlineForDeck(*deck, current);
    }
    return out;
}

ShowApplier::ShowApplier(ShowWindow *window)
    : m_window(window)
{
}

void ShowApplier::replaceSelection(const QString &text)
{
    // Replace the current slide's first text box content with the reply
    // (Rewrite slide / speaker-notes flows arrive here).
    m_window->replaceCurrentSlideText(text);
}

void ShowApplier::insertAtCursor(const QString &text)
{
    // Deck outlines arrive as insert requests from the panel.
    const ShowModel::Presentation *deck = m_window->showDocument()->presentation();
    m_window->appendSlides(slidesFromOutline(text, deck->slideSize));
}

QList<ShowModel::Slide> slidesFromOutline(const QString &outline, const QSizeF &slideSize)
{
    QList<ShowModel::Slide> slides;
    ShowModel::Slide *current = nullptr;
    const QStringList lines = outline.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("## "))) {
            ShowModel::Slide slide;
            slide.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            slide.title = line.mid(3).trimmed();
            slide.layout = QStringLiteral("title+content");
            slides.append(slide);
            current = &slides.last();
            Q_UNUSED(slideSize);
            continue;
        }
        if (current == nullptr) {
            continue; // preamble before the first stanza is ignored
        }
        if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* "))) {
            ShowModel::Shape shape;
            shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            shape.kind = ShowModel::ShapeKind::TextBox;
            shape.rect = QRectF(slideSize.width() * 0.1, slideSize.height() * 0.35,
                                slideSize.width() * 0.8, slideSize.height() * 0.5);
            shape.strokeWidth = 0;
            ShowModel::TextRun run;
            run.text = line.mid(2).trimmed();
            run.pointSize = 24;
            ShowModel::TextParagraph paragraph;
            paragraph.runs = {run};
            shape.paragraphs = {paragraph};
            current->shapes.append(shape);
        } else if (line.startsWith(QLatin1String("Notes:"), Qt::CaseInsensitive)) {
            current->notes = line.mid(6).trimmed();
        }
    }
    return slides;
}

void generateDeckFromOutline(ShowWindow *window, Ai::ModelCatalog *catalog, QWidget *dialogParent)
{
    const ShowModel::Presentation *deck = window->showDocument()->presentation();
    const QString outline = QInputDialog::getText(
        dialogParent, QObject::tr("Generate deck"), QObject::tr("Topic or outline:"));
    if (outline.isEmpty()) {
        return;
    }
    Ai::ChatRequest request;
    request.maxTokens = 4096;
    request.temperature = 0.4;
    request.messages.append({QStringLiteral("user"), generationPrompt(outline), {}});

    const QStringList ids = catalog->modelIds();
    if (ids.isEmpty()) {
        QMessageBox::information(dialogParent, QObject::tr("Generate deck"),
                                 QObject::tr("No AI providers are configured."));
        return;
    }
    const QString modelId = ids.first();
    // AGENT-GUARD: see InlineActionFlow — modelIds() hands out "<id>/<model>",
    // so comparing against candidate.id alone left secretKey empty and the
    // generation request went out unauthenticated.
    const Ai::ProviderConfig config =
        catalog->configFor(modelId).value_or(Ai::ProviderConfig{});
    auto *store = new Ai::SecretStore(catalog);
    store->retrieve(config.secretKey,
                    [window, catalog, request, modelId, dialogParent](const QString &secret) {
                            Ai::ChatProvider *provider = catalog->createProvider(modelId, secret, catalog);
                        if (provider == nullptr) {
                            return;
                        }
                        auto *state = new QString;
                        Ai::ChatRun *run = provider->start(request);
                        QObject::connect(run, &Ai::ChatRun::textDelta, catalog,
                                         [state](const QString &delta) { *state += delta; });
                        QObject::connect(run, &Ai::ChatRun::failed, catalog,
                                         [state](const Ai::ChatError &) { delete state; });
                        QObject::connect(run, &Ai::ChatRun::finished, catalog,
                                         [window, state](const Ai::ChatResult &result) {
                                             const QString deckText = *state;
                                             delete state;
                                             if (result.text.isEmpty()) {
                                                 return; // generation is best-effort
                                             }
                                             const ShowModel::Presentation *deck =
                                                 window->showDocument()->presentation();
                                             window->appendSlides(
                                                 slidesFromOutline(result.text, deck->slideSize));
                                         });
                    });
}

} // namespace QindaOffice::Apps::Show
