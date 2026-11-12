/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef kpMessageBoxMnemonics_H
#define kpMessageBoxMnemonics_H

#include <QObject>

// Lets a bare letter press a KMessageBox button, e.g. S for "&Save" in the
// "has been modified" warning, as QMessageBox does.  KMessageBox shows a
// plain QDialog, where a button's mnemonic would otherwise need Alt.
class kpMessageBoxMnemonics : public QObject
{
    Q_OBJECT

public:
    // Call once, after the QApplication has been created.
    static void install();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit kpMessageBoxMnemonics(QObject *parent);
};

#endif // kpMessageBoxMnemonics_H
