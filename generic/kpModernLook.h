/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef kpModernLook_H
#define kpModernLook_H

#include <QColor>
#include <QObject>
#include <QPalette>

class QDBusVariant;

// The optional "modern look": a palette that follows the desktop's dark/light
// mode and accent color, a style sheet with rounded, flat controls, and
// flags that let custom-painted widgets (canvas, palette, color button,
// tool options) switch to a matching style.
class kpModernLook : public QObject
{
    Q_OBJECT

public:
    static kpModernLook *instance();

    // Reads the saved setting and applies it.  Call once after the
    // application data has been set, before any window is created.
    static void initialize();

    // Whether the modern look is currently applied.
    static bool isActive();

    void setEnabled(bool enabled);

    // Colors derived from the current application palette.
    static QColor mix(const QColor &from, const QColor &to, qreal amount);
    static bool isDark(const QPalette &palette);
    static QColor borderColor(const QPalette &palette);
    static QColor workspaceColor(const QPalette &palette);

    // Repaints the black parts of <pixmap> (the "ink" of KolourPaint's
    // generated option and button pixmaps) in <color>.
    static QPixmap recolorInk(const QPixmap &pixmap, const QColor &color);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void slotPortalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);
    void slotApplicationPaletteChanged();

private:
    kpModernLook();
    void apply();

    bool m_enabled;
    bool m_applied;
    bool m_applying;
    bool m_ownPalette;
    QPalette m_originalPalette;
    QPalette m_appliedPalette;
    QString m_originalIconFallback;
};

#endif // kpModernLook_H
