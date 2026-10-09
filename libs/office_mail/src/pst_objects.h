// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Mapi:: property bags over a .pst (M76, D-085): a message or embedded
// message (a property context plus its subnodes), an attachment (the
// attachment's own property context) and a recipient (one row of the
// recipient table). Internal to office_mail; PstFile::openMessage() is the
// way in (pst_reader.h).
//
// AGENT-GUARD: one budget is shared by every bag of a message tree, so a
// hostile file cannot show the same large subnode as many attachments or
// nest embedded messages to multiply what is read (kMaxMsgContentBytes, the
// .msg reader's cap).

#include "mapi_object.h"
#include "pst_ltp.h"

#include <memory>

namespace QindaOffice::Mail::Pst {

class PstFile;

// Bytes still allowed for one message tree.
struct ReadBudget {
    qint64 remaining = 0;
};

// The typed getters of Mapi::PropertyBag over "the raw value of property
// `id`", shared by the three classes below.
template <typename Base>
class ValueBag : public Base {
public:
    bool has(quint16 id, quint16 type) const override { return typeOf(id) == type; }
    std::optional<quint32> longValue(quint16 id) const override;
    std::optional<double> doubleValue(quint16 id) const override;
    bool boolValue(quint16 id) const override;
    QDateTime timeValue(quint16 id) const override;
    QString string(quint16 id) override;
    QByteArray binary(quint16 id) override;
    QByteArray optionalBinary(quint16 id) override;
    QStringList strings(quint16 id) override;
    QList<QByteArray> binaries(quint16 id) override;
    void setString8CodePage(int codePage) override { m_codePage = codePage; }
    int string8CodePage() const override { return m_codePage; }
    bool failed() const override { return !m_error.isEmpty(); }
    QString error() const override { return m_error; }

protected:
    explicit ValueBag(std::shared_ptr<ReadBudget> budget) : m_budget(std::move(budget)) {}
    [[nodiscard]] virtual quint16 typeOf(quint16 id) const = 0;
    // The raw value; false (error empty) when absent.
    virtual bool fetch(quint16 id, PropertyValue *out, QString *error) const = 0;
    // fetch() of `type` only, charged to the budget; a failure is recorded
    // (sticky error) unless `quiet`.
    bool get(quint16 id, quint16 type, QByteArray *out, bool quiet = false) const;

    std::shared_ptr<ReadBudget> m_budget;
    mutable QString m_error;
    int m_codePage = 1252;
};

class PstMessage final : public ValueBag<Mapi::Message> {
public:
    PstMessage(PstFile *file, PropertyContext properties, std::shared_ptr<ReadBudget> budget, int depth);
    bool recipients(std::vector<std::unique_ptr<Mapi::PropertyBag>> *out, QString *error) override;
    bool attachments(std::vector<std::unique_ptr<Mapi::Attachment>> *out, QString *error) override;
    const Mapi::NamedPropertyMap &namedProperties() const override;

protected:
    quint16 typeOf(quint16 id) const override { return m_properties.typeOf(id); }
    bool fetch(quint16 id, PropertyValue *out, QString *error) const override;

private:
    PstFile *m_file;
    PropertyContext m_properties;
    int m_depth;
};

class PstAttachment final : public ValueBag<Mapi::Attachment> {
public:
    PstAttachment(PstFile *file, PropertyContext properties, std::shared_ptr<ReadBudget> budget, int depth);
    std::unique_ptr<Mapi::Message> embeddedMessage(QString *error) override;

protected:
    quint16 typeOf(quint16 id) const override { return m_properties.typeOf(id); }
    bool fetch(quint16 id, PropertyValue *out, QString *error) const override;

private:
    PstFile *m_file;
    PropertyContext m_properties;
    int m_depth;
};

class PstRow final : public ValueBag<Mapi::PropertyBag> {
public:
    PstRow(std::shared_ptr<const TableContext> table, int row, std::shared_ptr<ReadBudget> budget);

protected:
    quint16 typeOf(quint16 id) const override;
    bool fetch(quint16 id, PropertyValue *out, QString *error) const override;

private:
    std::shared_ptr<const TableContext> m_table;
    int m_row;
};

} // namespace QindaOffice::Mail::Pst
