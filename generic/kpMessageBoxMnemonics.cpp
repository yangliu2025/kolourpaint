/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#include "generic/kpMessageBoxMnemonics.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QStringList>

//---------------------------------------------------------------------

namespace
{
bool IsKMessageBox(const QWidget *window)
{
    // KMessageBox names its dialogs after the function that created them
    // (KF 5.90: questionYesNo..., KF 5.100+: questionTwoActions...).
    static const QStringList Names = {
        QStringLiteral("about"),
        QStringLiteral("error"),
        QStringLiteral("information"),
        QStringLiteral("questionTwoActions"),
        QStringLiteral("QuestionTwoActionsCancel"),
        QStringLiteral("questionYesNo"),
        QStringLiteral("questionYesNoCancel"),
        QStringLiteral("sorry"),
        QStringLiteral("warningTwoActionsCancel"),
        QStringLiteral("warningTwoActionsList"),
        QStringLiteral("warningYesNo"),
        QStringLiteral("warningYesNoCancel"),
        QStringLiteral("warningYesNoList"),
    };

    return window->metaObject() == &QDialog::staticMetaObject && Names.contains(window->objectName());
}

QAbstractButton *ButtonForMnemonic(const QWidget *dialog, int key)
{
    const QList<QDialogButtonBox *> buttonBoxes = dialog->findChildren<QDialogButtonBox *>(QString(), Qt::FindDirectChildrenOnly);
    for (const QDialogButtonBox *buttonBox : buttonBoxes) {
        const QList<QAbstractButton *> buttons = buttonBox->buttons();
        for (QAbstractButton *button : buttons) {
            const QKeySequence shortcut = button->shortcut();
            if (button->isEnabled() && !shortcut.isEmpty() && int(shortcut[0] & ~Qt::KeyboardModifierMask) == key) {
                return button;
            }
        }
    }
    return nullptr;
}
}

//---------------------------------------------------------------------

// public static
void kpMessageBoxMnemonics::install()
{
    static kpMessageBoxMnemonics *filter = nullptr;
    if (!filter) {
        filter = new kpMessageBoxMnemonics(qApp);
        qApp->installEventFilter(filter);
    }
}

//---------------------------------------------------------------------

// private
kpMessageBoxMnemonics::kpMessageBoxMnemonics(QObject *parent)
    : QObject(parent)
{
}

//---------------------------------------------------------------------

// protected virtual [base QObject]
bool kpMessageBoxMnemonics::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress || !watched->isWidgetType()) {
        return false;
    }

    // Like QMessageBox::keyPressEvent(), only see keys that the focus widget
    // ignored and that therefore propagated up to the dialog itself.
    const auto *widget = static_cast<QWidget *>(watched);
    if (!widget->isWindow() || !IsKMessageBox(widget)) {
        return false;
    }

    const auto *keyEvent = static_cast<QKeyEvent *>(event);
    // A held key must not also answer the next message box, such as the
    // palette's after the document's when closing.
    if (keyEvent->isAutoRepeat() || (keyEvent->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier))) {
        return false;
    }

    QAbstractButton *button = ButtonForMnemonic(widget, keyEvent->key());
    if (!button) {
        return false;
    }

    button->animateClick();
    return true;
}

//---------------------------------------------------------------------
