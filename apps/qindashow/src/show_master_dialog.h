// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The slide-master editor: a list of masters plus the four things a master
// decides — background, title colour, body colour and font. Stock Qt
// widgets only (AGENTS.md rule 3); the colour buttons show their colour as
// an icon rather than through a stylesheet.

#include "presentation.h"

#include <QDialog>
#include <QList>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace QindaOffice::Apps::Show {

class ShowMasterDialog final : public QDialog {
    Q_OBJECT
public:
    ShowMasterDialog(QList<ShowModel::SlideMaster> masters, QWidget *parent = nullptr);

    [[nodiscard]] QList<ShowModel::SlideMaster> masters() const { return m_masters; }

private:
    void rebuildList();
    void loadCurrent();
    void storeCurrent();
    void addMaster();
    void removeCurrent();
    void pickColor(QColor ShowModel::SlideMaster::*field, QPushButton *button,
                   const QString &title);

    QList<ShowModel::SlideMaster> m_masters;
    int m_current = -1;
    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QPushButton *m_background = nullptr;
    QPushButton *m_titleColor = nullptr;
    QPushButton *m_bodyColor = nullptr;
    QFontComboBox *m_font = nullptr;
    bool m_loading = false;
};

} // namespace QindaOffice::Apps::Show
