/*
   SPDX-FileCopyrightText: 2026 Yang Liu <yangliu2005@mail.com>

   SPDX-License-Identifier: BSD-2-Clause
*/

#include "generic/kpModernLook.h"

#include "kpDefs.h"
#include "kpLogCategories.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>

//---------------------------------------------------------------------

namespace
{
const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString PortalSettings = QStringLiteral("org.freedesktop.portal.Settings");
const QString AppearanceNameSpace = QStringLiteral("org.freedesktop.appearance");
const QString GnomeInterfaceNameSpace = QStringLiteral("org.gnome.desktop.interface");

const QString BreezeIcons = QStringLiteral("breeze");
const QString BreezeDarkIcons = QStringLiteral("breeze-dark");

struct Appearance {
    bool dark = false;
    QColor accent;
};

// Returns an invalid QVariant if the setting is unset; sets <*portalFound>
// to false if there is no settings portal to ask.
QVariant ReadPortalSetting(const QString &nameSpace, const QString &key, bool *portalFound)
{
    QDBusMessage message = QDBusMessage::createMethodCall(PortalService, PortalPath, PortalSettings, QStringLiteral("Read"));
    message << nameSpace << key;

    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1000 /*ms*/);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (portalFound) {
            *portalFound = (reply.errorName() == QLatin1String("org.freedesktop.portal.Error.NotFound"));
        }
        return {};
    }

    if (portalFound) {
        *portalFound = true;
    }

    // Read() wraps the value in one more variant than ReadOne().
    QVariant value = reply.arguments().constFirst();
    while (value.userType() == qMetaTypeId<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

// The portal's accent color is an (ddd) struct, components in [0, 1].
QColor AccentFromPortalValue(const QVariant &value)
{
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        return {};
    }

    const QDBusArgument argument = value.value<QDBusArgument>();
    double rgb[3] = {-1, -1, -1};
    argument.beginStructure();
    argument >> rgb[0] >> rgb[1] >> rgb[2];
    argument.endStructure();

    for (double component : rgb) {
        if (component < 0 || component > 1) {
            return {};
        }
    }
    return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
}

// GNOME before 47 has no accent color setting; Ubuntu encodes its accent in
// the Yaru theme name instead (e.g. "Yaru-sage-dark").
QColor AccentForGtkTheme(const QString &themeName)
{
    static const struct {
        const char *name;
        QRgb color;
    } YaruAccents[] = {
        {"Yaru", 0xE95420},
        {"Yaru-bark", 0x787859},
        {"Yaru-sage", 0x657B69},
        {"Yaru-blue", 0x0073E5},
        {"Yaru-olive", 0x4B8501},
        {"Yaru-viridian", 0x03875B},
        {"Yaru-prussiangreen", 0x308280},
        {"Yaru-purple", 0x7764D8},
        {"Yaru-magenta", 0xB34CB3},
        {"Yaru-red", 0xDA3450},
    };

    QString name = themeName;
    if (name.endsWith(QLatin1String("-dark"), Qt::CaseInsensitive)) {
        name.chop(5);
    }

    for (const auto &yaru : YaruAccents) {
        if (name.compare(QLatin1String(yaru.name), Qt::CaseInsensitive) == 0) {
            return QColor(yaru.color);
        }
    }
    return {};
}

Appearance SystemAppearance()
{
    Appearance appearance;
    QString gtkTheme;
    bool haveColorScheme = false;

    bool portalFound = false;
    const QVariant colorScheme = ReadPortalSetting(AppearanceNameSpace, QStringLiteral("color-scheme"), &portalFound);
    if (portalFound) {
        // 0: no preference, 1: prefer dark, 2: prefer light
        if (colorScheme.isValid() && colorScheme.toUInt() != 0) {
            appearance.dark = (colorScheme.toUInt() == 1);
            haveColorScheme = true;
        }
        appearance.accent = AccentFromPortalValue(ReadPortalSetting(AppearanceNameSpace, QStringLiteral("accent-color"), nullptr));
        gtkTheme = ReadPortalSetting(GnomeInterfaceNameSpace, QStringLiteral("gtk-theme"), nullptr).toString();
    } else {
        // No portal (e.g. a bare X session): fall back on GTK's own settings.
        QSettings gtkSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/gtk-3.0/settings.ini"),
                              QSettings::IniFormat);
        gtkSettings.beginGroup(QStringLiteral("Settings"));
        gtkTheme = gtkSettings.value(QStringLiteral("gtk-theme-name")).toString();
        const QString preferDark = QStringLiteral("gtk-application-prefer-dark-theme");
        if (gtkSettings.contains(preferDark)) {
            appearance.dark = gtkSettings.value(preferDark).toBool();
            haveColorScheme = true;
        }
    }

    if (!haveColorScheme) {
        appearance.dark = gtkTheme.endsWith(QLatin1String("-dark"), Qt::CaseInsensitive);
    }

    if (!appearance.accent.isValid()) {
        appearance.accent = AccentForGtkTheme(gtkTheme);
    }
    if (!appearance.accent.isValid()) {
        appearance.accent = QColor(0x3584E4); // GNOME's default blue
    }

    const QByteArray forcedScheme = qgetenv("KOLOURPAINT_COLOR_SCHEME");
    if (forcedScheme == "dark") {
        appearance.dark = true;
    } else if (forcedScheme == "light") {
        appearance.dark = false;
    }

    return appearance;
}

QPalette MakePalette(const Appearance &appearance)
{
    const bool dark = appearance.dark;
    const QColor accent = appearance.accent;

    const QColor window(dark ? 0x2B2B2B : 0xF3F3F3);
    const QColor text(dark ? 0xECECEC : 0x1C1C1C);
    const QColor disabledText(dark ? 0x7A7A7A : 0xA3A3A3);

    QPalette palette;
    auto set = [&palette](QPalette::ColorRole role, const QColor &color) {
        palette.setColor(QPalette::All, role, color);
    };

    set(QPalette::Window, window);
    set(QPalette::WindowText, text);
    set(QPalette::Base, QColor(dark ? 0x1F1F1F : 0xFFFFFF));
    set(QPalette::AlternateBase, QColor(dark ? 0x262626 : 0xF6F6F6));
    set(QPalette::Button, QColor(dark ? 0x363636 : 0xFDFDFD));
    set(QPalette::ButtonText, text);
    set(QPalette::Text, text);
    set(QPalette::BrightText, QColor(dark ? 0xFFFFFF : 0x000000));
    set(QPalette::Light, QColor(dark ? 0x474747 : 0xFFFFFF));
    set(QPalette::Midlight, QColor(dark ? 0x3A3A3A : 0xE9E9E9));
    set(QPalette::Mid, QColor(dark ? 0x242424 : 0xC2C2C2));
    set(QPalette::Dark, QColor(dark ? 0x1A1A1A : 0x9E9E9E));
    set(QPalette::Shadow, QColor(dark ? 0x0E0E0E : 0x6E6E6E));
    set(QPalette::ToolTipBase, QColor(dark ? 0x3A3A3A : 0xFFFFFF));
    set(QPalette::ToolTipText, text);
    set(QPalette::PlaceholderText, QColor(dark ? 0x8C8C8C : 0x8A8A8A));
    set(QPalette::Highlight, accent);
    set(QPalette::HighlightedText, qGray(accent.rgb()) > 160 ? QColor(0x1C1C1C) : QColor(Qt::white));
    set(QPalette::Link, dark ? accent.lighter(140) : accent.darker(110));
    set(QPalette::LinkVisited, dark ? accent.lighter(120) : accent.darker(130));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Base, window);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(dark ? 0x4A4A4A : 0xC8C8C8));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);

    return palette;
}

QString Css(const QColor &color)
{
    if (color.alpha() == 255) {
        return color.name();
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QString StyleSheet(const QPalette &palette)
{
    const bool dark = kpModernLook::isDark(palette);
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor accent = palette.color(QPalette::Highlight);

    const QColor hover = kpModernLook::mix(window, text, 0.08);
    const QColor pressed = kpModernLook::mix(window, text, 0.14);
    const QColor border = kpModernLook::borderColor(palette);
    const QColor checked = kpModernLook::mix(window, accent, dark ? 0.35 : 0.2);
    const QColor checkedBorder = kpModernLook::mix(window, accent, 0.75);
    const QColor menu = dark ? kpModernLook::mix(window, text, 0.04) : palette.color(QPalette::Base);
    const QColor button = dark ? kpModernLook::mix(window, text, 0.07) : palette.color(QPalette::Button);
    const QColor scrollHandle = kpModernLook::mix(window, text, 0.35);
    const QColor scrollHandleHover = kpModernLook::mix(window, text, 0.5);

    QString css = QStringLiteral(
        "QMainWindow::separator { background: @window; width: 6px; height: 6px; }"

        "QToolBar { background: @window; border: none; padding: 3px; spacing: 3px; }"
        "QToolBar::separator:horizontal { background: @border; width: 1px; margin: 6px 4px; }"
        "QToolBar::separator:vertical { background: @border; height: 1px; margin: 4px 6px; }"

        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; }"
        "QToolButton:hover { background: @hover; }"
        "QToolButton:pressed { background: @pressed; }"
        "QToolButton:checked { background: @checked; border-color: @checkedBorder; }"
        "QToolButton[popupMode=\"1\"] { padding-right: 16px; }"
        "QToolButton::menu-button { border: none; border-top-right-radius: 6px; border-bottom-right-radius: 6px; width: 14px; }"
        "QToolButton::menu-button:hover { background: @pressed; }"

        "QMenuBar { background: @window; padding: 2px; }"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }"
        "QMenuBar::item:selected { background: @hover; }"
        "QMenuBar::item:pressed { background: @pressed; }"

        "QMenu { background: @menu; border: 1px solid @border; padding: 4px; }"
        "QMenu::item { background: transparent; padding: 5px 28px 5px 8px; border-radius: 5px; }"
        "QMenu::item:selected { background: @hover; }"
        "QMenu::separator { background: @border; height: 1px; margin: 4px 8px; }"

        "QComboBox, QLineEdit { background: @base; border: 1px solid @border; border-radius: 6px; padding: 3px 6px;"
        " selection-background-color: @accent; selection-color: @accentText; }"
        "QComboBox:focus, QLineEdit:focus { border-color: @accent; }"
        "QComboBox::drop-down { border: none; width: 18px; }"
        "QComboBox::down-arrow { image: url(@chevron); width: 10px; height: 10px; }"

        "QPushButton { background: @button; border: 1px solid @border; border-radius: 6px; padding: 5px 14px; }"
        "QPushButton:hover { background: @hover; }"
        "QPushButton:pressed { background: @pressed; }"
        "QPushButton:default { background: @accent; color: @accentText; border-color: @accent; }"

        "QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }"
        "QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }"
        "QScrollBar::handle { background: @scrollHandle; border-radius: 3px; }"
        "QScrollBar::handle:hover { background: @scrollHandleHover; }"
        "QScrollBar::handle:vertical { min-height: 28px; margin: 2px 3px; }"
        "QScrollBar::handle:horizontal { min-width: 28px; margin: 3px 2px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }"

        "QStatusBar { background: @window; border-top: 1px solid @border; }"
        "QStatusBar::item { border: none; }"

        "QToolTip { background: @toolTip; color: @toolTipText; border: 1px solid @border; border-radius: 6px; padding: 4px 6px; }"

        "kpViewScrollableContainer { border: none; }"
        "kpToolWidgetBase { background: @base; border: 1px solid @border; border-radius: 6px; }"
        "kpDualColorButton { border: none; }");

    // Styling the drop-down hides the style's own arrow.
    css.replace(QStringLiteral("@chevron"), dark ? QStringLiteral(":/icons/chevron_down_light.svg") : QStringLiteral(":/icons/chevron_down_dark.svg"));

    // Longest names first so that e.g. "@checked" does not eat "@checkedBorder".
    const QList<QPair<QString, QColor>> colors = {
        {QStringLiteral("@scrollHandleHover"), scrollHandleHover},
        {QStringLiteral("@scrollHandle"), scrollHandle},
        {QStringLiteral("@checkedBorder"), checkedBorder},
        {QStringLiteral("@checked"), checked},
        {QStringLiteral("@toolTipText"), palette.color(QPalette::ToolTipText)},
        {QStringLiteral("@toolTip"), palette.color(QPalette::ToolTipBase)},
        {QStringLiteral("@accentText"), palette.color(QPalette::HighlightedText)},
        {QStringLiteral("@accent"), accent},
        {QStringLiteral("@window"), window},
        {QStringLiteral("@hover"), hover},
        {QStringLiteral("@pressed"), pressed},
        {QStringLiteral("@border"), border},
        {QStringLiteral("@menu"), menu},
        {QStringLiteral("@button"), button},
        {QStringLiteral("@base"), palette.color(QPalette::Base)},
    };
    for (const auto &color : colors) {
        css.replace(color.first, Css(color.second));
    }
    return css;
}

// Only replace a palette nobody chose: Qt's built-in one, which is what
// KolourPaint gets outside Plasma.  A palette from the platform theme (e.g. a
// KDE color scheme) is the user's choice and is kept.
bool IsStandardPalette(const QPalette &palette)
{
    const QPalette standard = QApplication::style()->standardPalette();
    for (auto role : {QPalette::Window, QPalette::WindowText, QPalette::Base, QPalette::Button, QPalette::Highlight}) {
        if (palette.color(QPalette::Active, role) != standard.color(QPalette::Active, role)) {
            return false;
        }
    }
    return true;
}
} // namespace

//---------------------------------------------------------------------

kpModernLook::kpModernLook()
    : QObject(qApp)
    , m_enabled(false)
    , m_applied(false)
    , m_applying(false)
    , m_ownPalette(false)
{
}

//---------------------------------------------------------------------

// public static
kpModernLook *kpModernLook::instance()
{
    static kpModernLook *look = new kpModernLook();
    return look;
}

//---------------------------------------------------------------------

// public static
void kpModernLook::initialize()
{
    kpModernLook *look = instance();

    look->m_originalPalette = QApplication::palette();
    look->m_appliedPalette = look->m_originalPalette;
    look->m_originalIconFallback = QIcon::fallbackThemeName();
    look->m_ownPalette = IsStandardPalette(look->m_originalPalette);

    QDBusConnection::sessionBus().connect(PortalService,
                                          PortalPath,
                                          PortalSettings,
                                          QStringLiteral("SettingChanged"),
                                          look,
                                          SLOT(slotPortalSettingChanged(QString, QString, QDBusVariant)));
    connect(qApp, &QGuiApplication::paletteChanged, look, &kpModernLook::slotApplicationPaletteChanged);

    const KConfigGroup cfg(KSharedConfig::openConfig(), QStringLiteral(kpSettingsGroupGeneral));
    look->m_enabled = cfg.readEntry(kpSettingModernLook, true);
    if (look->m_enabled) {
        look->apply();
    }
}

//---------------------------------------------------------------------

// public static
bool kpModernLook::isActive()
{
    return instance()->m_applied;
}

//---------------------------------------------------------------------

// public
void kpModernLook::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }

    m_enabled = enabled;

    KConfigGroup cfg(KSharedConfig::openConfig(), QStringLiteral(kpSettingsGroupGeneral));
    cfg.writeEntry(kpSettingModernLook, m_enabled);
    cfg.sync();

    apply();
}

//---------------------------------------------------------------------

// private
void kpModernLook::apply()
{
    QPalette palette = m_originalPalette;
    QString iconFallback = m_originalIconFallback;
    QString styleSheet;

    if (m_enabled) {
        if (m_ownPalette) {
            const Appearance appearance = ::SystemAppearance();
            palette = ::MakePalette(appearance);

            // Breeze's monochrome tool icons vanish on a dark background.
            if (appearance.dark && iconFallback == BreezeIcons) {
                iconFallback = BreezeDarkIcons;
            }
        }
        styleSheet = ::StyleSheet(palette);
    }

    qCDebug(kpLogMisc) << "kpModernLook::apply() enabled=" << m_enabled << "ownPalette=" << m_ownPalette << "dark=" << isDark(palette)
                       << "accent=" << palette.color(QPalette::Highlight) << "iconFallback=" << iconFallback;

    m_applying = true;
    QApplication::setPalette(palette);
    qApp->setStyleSheet(styleSheet);
    m_appliedPalette = QApplication::palette();
    m_applying = false;

    if (iconFallback != QIcon::fallbackThemeName()) {
        QIcon::setFallbackThemeName(iconFallback);
        // Qt caches each theme with its parents, the last of which is the
        // fallback, and only drops that cache when the search path is set.
        // This also makes icons that are already loaded look themselves up again.
        QIcon::setThemeSearchPaths(QIcon::themeSearchPaths());
    }

    m_applied = m_enabled;
    Q_EMIT changed();
}

//---------------------------------------------------------------------

// private slot
void kpModernLook::slotPortalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value)
{
    Q_UNUSED(value)

    const bool relevant = (nameSpace == AppearanceNameSpace && (key == QLatin1String("color-scheme") || key == QLatin1String("accent-color")))
        || (nameSpace == GnomeInterfaceNameSpace && key == QLatin1String("gtk-theme"));

    if (relevant && m_applied && m_ownPalette) {
        apply();
    }
}

//---------------------------------------------------------------------

// private slot
void kpModernLook::slotApplicationPaletteChanged()
{
    // Qt setting our palette again is no news.
    const QPalette palette = QApplication::palette();
    if (m_applying || palette == m_appliedPalette) {
        return;
    }

    // Someone else, e.g. the platform theme switching to another KDE color
    // scheme, set a palette: it is now the one to keep or to restore.
    qCDebug(kpLogMisc) << "kpModernLook::slotApplicationPaletteChanged() window=" << palette.color(QPalette::Window);
    m_originalPalette = palette;
    m_ownPalette = IsStandardPalette(palette);
    m_appliedPalette = palette;

    if (m_enabled) {
        // Rebuild the style sheet once this palette change has been delivered.
        QMetaObject::invokeMethod(this, &kpModernLook::apply, Qt::QueuedConnection);
    }
}

//---------------------------------------------------------------------

// public static
QColor kpModernLook::mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

//---------------------------------------------------------------------

// public static
bool kpModernLook::isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < palette.color(QPalette::WindowText).lightness();
}

//---------------------------------------------------------------------

// public static
QColor kpModernLook::borderColor(const QPalette &palette)
{
    return mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), isDark(palette) ? 0.16 : 0.18);
}

//---------------------------------------------------------------------

// public static
QColor kpModernLook::workspaceColor(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    return isDark(palette) ? window.darker(130) : mix(window, QColor(0xD8DEE6), 0.6);
}

//---------------------------------------------------------------------

// public static
QPixmap kpModernLook::recolorInk(const QPixmap &pixmap, const QColor &color)
{
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    const QRgb ink = color.rgb() & RGB_MASK;

    for (int y = 0; y < image.height(); y++) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); x++) {
            if ((line[x] & RGB_MASK) == 0 && qAlpha(line[x]) != 0) {
                line[x] = (line[x] & ~RGB_MASK) | ink;
            }
        }
    }

    QPixmap recolored = QPixmap::fromImage(image);
    recolored.setDevicePixelRatio(pixmap.devicePixelRatio());
    return recolored;
}

//---------------------------------------------------------------------

#include "moc_kpModernLook.cpp"
