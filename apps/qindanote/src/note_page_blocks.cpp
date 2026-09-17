// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageView's content blocks: typed text, checklist items and images.
// Split out of note_page_view.cpp to keep both inside the line budget.
//
// The widgets live over the page surface (text and checklist items are
// editable, images are painted); their POSITIONS come from note_blocks.h,
// the same layout the pure page renderer prints with.

#include "note_page_view.h"

#include "note_blocks.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPainter>
#include <QTextEdit>

namespace QindaOffice::Apps::Notes {

namespace {

// Writes one block back into the array at `index`, leaving the rest alone.
void replaceBlock(QJsonObject *root, int index, const QJsonObject &block)
{
    QJsonArray blocks = root->value(QStringLiteral("blocks")).toArray();
    if (index < 0 || index >= blocks.size()) {
        return;
    }
    blocks.replace(index, block);
    root->insert(QStringLiteral("blocks"), blocks);
}

} // namespace

void NotePageView::rebuildBlockWidgets()
{
    qDeleteAll(m_blockWidgets);
    m_blockWidgets.clear();
    m_blockIndexForWidget.clear();
    if (m_page == nullptr) {
        m_blocks.clear();
        return;
    }
    m_syncingBlocks = true;
    m_blocks = layoutBlocks(m_page->envelope.blocksJson, m_pageWidth);
    for (const PageBlock &block : m_blocks) {
        QWidget *widget = nullptr;
        switch (block.kind) {
        case PageBlock::Kind::Text: {
            auto *edit = new QTextEdit(this);
            edit->setPlainText(block.text);
            edit->setAcceptRichText(false);
            // AGENT-GUARD: every editor writes its content back to
            // blocksJson on change — typed text must be persisted and must
            // mark the page dirty, or it is lost on the next page switch.
            connect(edit, &QTextEdit::textChanged, this, [this] { persistTextBlocks(); });
            widget = edit;
            break;
        }
        case PageBlock::Kind::Check: {
            auto *row = new QWidget(this);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto *box = new QCheckBox(row);
            box->setObjectName(QStringLiteral("noteCheckBox"));
            box->setChecked(block.checked);
            auto *label = new QLineEdit(block.text, row);
            label->setObjectName(QStringLiteral("noteCheckText"));
            label->setFrame(false);
            layout->addWidget(box);
            layout->addWidget(label, 1);
            connect(box, &QCheckBox::toggled, this, [this] { persistTextBlocks(); });
            connect(label, &QLineEdit::textEdited, this, [this] { persistTextBlocks(); });
            widget = row;
            break;
        }
        case PageBlock::Kind::Image:
            // Images are painted, not hosted: a widget would sit above the
            // ink instead of under it.
            break;
        }
        if (widget != nullptr) {
            widget->show();
            m_blockIndexForWidget.insert(widget, block.index);
            m_blockWidgets.append(widget);
        }
    }
    m_syncingBlocks = false;
    syncBlocksGeometry();
}

void NotePageView::syncBlocksGeometry()
{
    // Page coordinates scaled by the zoom, so a zoom repositions every
    // block consistently (fonts stay unscaled: the text tool is for typed
    // notes, not slides).
    const QRectF page = pageRect();
    for (QWidget *widget : m_blockWidgets) {
        const int index = m_blockIndexForWidget.value(widget, -1);
        for (const PageBlock &block : m_blocks) {
            if (block.index != index) {
                continue;
            }
            widget->move(int(page.x() + block.rect.x() * m_zoom),
                         int(page.y() + block.rect.y() * m_zoom));
            widget->resize(int(block.rect.width() * m_zoom),
                           int(block.rect.height() * m_zoom));
            break;
        }
    }
}

void NotePageView::paintImageBlocks(QPainter *painter, const QRectF &page) const
{
    for (const PageBlock &block : m_blocks) {
        if (block.kind != PageBlock::Kind::Image) {
            continue;
        }
        const QRectF target(page.x() + block.rect.x() * m_zoom,
                            page.y() + block.rect.y() * m_zoom,
                            block.rect.width() * m_zoom, block.rect.height() * m_zoom);
        QImage image;
        if (!block.imageData.isEmpty() && image.loadFromData(block.imageData)
            && !image.isNull()) {
            painter->drawImage(target, image);
        } else {
            // An unreadable image says so rather than leaving a gap the
            // user cannot explain.
            painter->setPen(Qt::DashLine);
            painter->drawRect(target);
            painter->drawText(target, Qt::AlignCenter, tr("Image unavailable"));
        }
    }
}

void NotePageView::persistTextBlocks()
{
    if (m_page == nullptr || m_syncingBlocks) {
        return;
    }
    QJsonObject root = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    for (QWidget *widget : m_blockWidgets) {
        const int index = m_blockIndexForWidget.value(widget, -1);
        if (index < 0) {
            continue;
        }
        if (auto *edit = qobject_cast<QTextEdit *>(widget)) {
            replaceBlock(&root, index,
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                     {QStringLiteral("text"), edit->toPlainText()}});
            continue;
        }
        auto *box = widget->findChild<QCheckBox *>(QStringLiteral("noteCheckBox"));
        auto *label = widget->findChild<QLineEdit *>(QStringLiteral("noteCheckText"));
        if (box != nullptr && label != nullptr) {
            replaceBlock(&root, index,
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                                     {QStringLiteral("text"), label->text()},
                                     {QStringLiteral("checked"), box->isChecked()}});
        }
    }
    m_page->envelope.blocksJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    emit pageEdited();
    update();
}

void NotePageView::appendBlock(const QJsonObject &block)
{
    if (m_page == nullptr) {
        return;
    }
    // Persist the existing editors first: they are about to be destroyed
    // and recreated from the JSON.
    persistTextBlocks();
    QJsonObject root = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    blocks.append(block);
    root.insert(QStringLiteral("blocks"), blocks);
    m_page->envelope.blocksJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    rebuildBlockWidgets();
    // The page may have grown past a sheet; the scroll area has to know.
    updatePageMetrics();
    emit pageEdited();
    update();
}

void NotePageView::addTextBlock()
{
    appendBlock(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), QString()}});
    if (!m_blockWidgets.isEmpty()) {
        m_blockWidgets.last()->setFocus();
    }
}

void NotePageView::addCheckItem()
{
    appendBlock(QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                            {QStringLiteral("text"), QString()},
                            {QStringLiteral("checked"), false}});
    if (!m_blockWidgets.isEmpty()) {
        if (auto *label =
                m_blockWidgets.last()->findChild<QLineEdit *>(QStringLiteral("noteCheckText"))) {
            label->setFocus();
        }
    }
}

void NotePageView::addImageBlock(const QByteArray &imageData)
{
    if (imageData.isEmpty()) {
        return;
    }
    // Base64 in the page envelope: a notebook page is one self-contained
    // JSON file, so an image referenced by path would break the moment the
    // notebook moved.
    appendBlock(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                            {QStringLiteral("data"),
                             QString::fromLatin1(imageData.toBase64())}});
}

} // namespace QindaOffice::Apps::Notes
