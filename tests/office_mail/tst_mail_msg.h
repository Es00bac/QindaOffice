// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// tst_mail_msg (M64): the Outlook .msg reader stack — the MS-CFB container
// (cfb_reader.h), MS-OXRTFCP decompression (rtf_compressed.h), MS-OXRTFEX
// de-encapsulation and RTF text (rtf_text.h), and the MS-OXMSG mapping into
// MessageContent (msg_reader.h). One QtTest class split over four files:
// tst_mail_msg.cpp (.msg envelope + main), tst_mail_msg_parts.cpp (.msg
// bodies and attachments), tst_mail_msg_cfb.cpp (compound files),
// tst_mail_msg_rtf.cpp (RTF). Every fixture is generated in-process by
// cfb_writer.h / cfb_writer_msg.h; nothing reads the owner's mail.

#include <QObject>

class TestMailMsg : public QObject {
    Q_OBJECT

private slots:
    // Compound files — tst_mail_msg_cfb.cpp
    void cfbRoundTrip_data();
    void cfbRoundTrip();
    void cfbCaseInsensitiveLookup();
    void cfbRejectsBadHeaders();
    void cfbTruncatedSector();
    void cfbFatSelfLoop();
    void cfbFatLongerCycle();
    void cfbOutOfRangeSectors();
    void cfbDirectorySiblingCycle();
    void cfbChildPointsAtItself();
    void cfbAbsurdStreamSizes();
    void cfbMiniFatOutsideMiniStream();
    void cfbDifatSectors_data();
    void cfbDifatSectors();
    void cfbDifatCorruption();
    void cfbLargeFileNeedsDifat();
    void cfbTruncationFuzz();
    void cfbByteFlipFuzz();

    // Compressed RTF and RTF text — tst_mail_msg_rtf.cpp
    void rtfCrcSpecVectors();
    void rtfDecompressSpecVectors();
    void rtfDecompressStored();
    void rtfDecompressRejectsCorruption();
    void rtfCompressorRoundTrip();
    void rtfEncapsulationKinds();
    void rtfHtmlSpecExample();
    void rtfHtmlEscapesAndCodePages();
    void rtfHtmlHostileInput();
    void rtfPlainText();
    void rtfPlainTextHostileInput();

    // Outlook messages — tst_mail_msg.cpp and tst_mail_msg_parts.cpp
    void msgEnvelope();
    void msgString8CodePages_data();
    void msgString8CodePages();
    void msgHtmlBody();
    void msgPlainBody();
    void msgRtfEncapsulatedHtml_data();
    void msgRtfEncapsulatedHtml();
    void msgRtfOnlyText();
    void msgAttachments();
    void msgEmbeddedMessages();
    void msgEmbeddingDepthLimit();
    void msgTransportHeaderFallback();
    void msgDisplayListFallback();
    void msgDateFallbacks();
    void msgNotAMessage();
    void msgCorruptionFuzz();
};
