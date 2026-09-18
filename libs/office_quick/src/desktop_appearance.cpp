// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_appearance.h"

#include "desktop_settings.h"

#include <qindaqt/design_tokens/accessibility_inputs.h>
#include <qindaqt/design_tokens/token_facade.h>
#include <qindaqt/themes/theme_catalog.h>
#include <qindaqt/themes/theme_spec.h>

#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

namespace QindaOffice::Quick {

namespace {

QString installedThemeDirectory()
{
    const QString directory = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                     QStringLiteral("qindaqt/themes"),
                                                     QStandardPaths::LocateDirectory);
    return directory.isEmpty() ? QStringLiteral("/usr/share/qindaqt/themes") : directory;
}

// Naming the Tokens singleton from an inline component loads the installed
// QML module (and its backing library) so singletonInstance() can find it
// — the pattern the desktop's Welcome app and QindaStudio use.
QindaQt::DesignTokens::TokenFacade *tokenFacade(QQmlEngine &engine, QString *error)
{
    QQmlComponent registration(&engine);
    registration.setData("import QtQuick\nimport QindaQt.Tokens 1.0\n"
                         "QtObject { property int revision: Tokens.qstRevision }\n",
                         QUrl(QStringLiteral("inline:qindaoffice-token-registration.qml")));
    // AGENT-GUARD: the module's plugin may resolve on the type-loader
    // thread, leaving the component Loading for a moment; wait, bounded.
    if (registration.status() == QQmlComponent::Loading) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&registration, &QQmlComponent::statusChanged, &loop,
                         [&loop](QQmlComponent::Status status) {
                             if (status != QQmlComponent::Loading) {
                                 loop.quit();
                             }
                         });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(5000);
        loop.exec();
    }
    if (!registration.isReady()) {
        if (error != nullptr) {
            *error = registration.errorString().trimmed();
        }
        return nullptr;
    }
    const std::unique_ptr<QObject> object(registration.create());
    if (object == nullptr) {
        if (error != nullptr) {
            *error = registration.errorString().trimmed();
        }
        return nullptr;
    }
    auto *facade = engine.singletonInstance<QindaQt::DesignTokens::TokenFacade *>(
        QStringLiteral("QindaQt.Tokens"), QStringLiteral("Tokens"));
    if (facade == nullptr && error != nullptr) {
        *error = QStringLiteral("the QindaQt.Tokens singleton is not registered");
    }
    return facade;
}

} // namespace

bool DesktopAppearance::publishTheme(QQmlEngine &engine, const QString &themeId, bool reducedMotion,
                                     QString *error)
{
    QindaQt::DesignTokens::TokenFacade *facade = tokenFacade(engine, error);
    if (facade == nullptr) {
        return false;
    }
    QindaQt::Themes::ThemeCatalog catalog;
    QString catalogError;
    if (!catalog.loadDirectory(installedThemeDirectory(), &catalogError)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot load the QindaQt theme catalog: %1").arg(catalogError);
        }
        return false;
    }
    // AGENT-GUARD: an unknown id is NOT mapped to some other theme — the
    // desktop and the office would then disagree on what "the theme" is.
    // The window keeps the preset and the log says why.
    if (!catalog.selectById(themeId)) {
        if (error != nullptr) {
            *error = QStringLiteral("no installed QindaQt theme named %1").arg(themeId);
        }
        return false;
    }
    QindaQt::DesignTokens::AccessibilityInputs inputs;
    inputs.reducedMotion = reducedMotion;
    // Text scale stays 1: QindaTK's own Density handles the office's
    // comfortable/compact preference; scaling the tokens as well would
    // apply it twice.
    QString publishError;
    if (!facade->publish(catalog.themes().at(catalog.currentIndex()), inputs, &publishError)) {
        if (error != nullptr) {
            *error = publishError.isEmpty() ? QStringLiteral("token publication failed")
                                            : publishError;
        }
        return false;
    }
    return true;
}

bool DesktopAppearance::publishDesktopTheme(QQmlEngine &engine, QString *error)
{
    const QVariantMap values = DesktopSettings::values(
        {QStringLiteral("appearance.theme"), QStringLiteral("accessibility.reducedMotion")});
    const QString themeId = values.value(QStringLiteral("appearance.theme")).toString();
    if (themeId.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no QindaQt desktop session answered");
        }
        return false;
    }
    return publishTheme(engine, themeId,
                        values.value(QStringLiteral("accessibility.reducedMotion")).toBool(), error);
}

} // namespace QindaOffice::Quick
