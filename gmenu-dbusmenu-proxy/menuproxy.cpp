/*
 * Copyright (C) 2018 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "menuproxy.h"

#include <config-X11.h>

#include "debug.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KWindowSystem>

#include <QX11Info>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include "menu.h"

static const QString s_ourServiceName = QStringLiteral("org.kde.plasma.gmenu_dbusmenu_proxy");

static const QString s_dbusMenuRegistrar = QStringLiteral("com.canonical.AppMenu.Registrar");

static const QByteArray s_gtkUniqueBusName = QByteArrayLiteral("_GTK_UNIQUE_BUS_NAME");

static const QByteArray s_gtkApplicationObjectPath = QByteArrayLiteral("_GTK_APPLICATION_OBJECT_PATH");
static const QByteArray s_unityObjectPath = QByteArrayLiteral("_UNITY_OBJECT_PATH");
static const QByteArray s_gtkWindowObjectPath = QByteArrayLiteral("_GTK_WINDOW_OBJECT_PATH");
static const QByteArray s_gtkMenuBarObjectPath = QByteArrayLiteral("_GTK_MENUBAR_OBJECT_PATH");
// that's the generic app menu with Help and Options and will be used if window doesn't have a fully-blown menu bar
static const QByteArray s_gtkAppMenuObjectPath = QByteArrayLiteral("_GTK_APP_MENU_OBJECT_PATH");

static const QByteArray s_kdeNetWmAppMenuServiceName = QByteArrayLiteral("_KDE_NET_WM_APPMENU_SERVICE_NAME");
static const QByteArray s_kdeNetWmAppMenuObjectPath = QByteArrayLiteral("_KDE_NET_WM_APPMENU_OBJECT_PATH");

MenuProxy::MenuProxy()
    : QObject()
    , m_xConnection(QX11Info::connection())
    , m_serviceWatcher(new QDBusServiceWatcher(this))
{
    m_serviceWatcher->setConnection(QDBusConnection::sessionBus());
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration |
                                     QDBusServiceWatcher::WatchForRegistration);
    m_serviceWatcher->addWatchedService(s_dbusMenuRegistrar);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString &service) {
        Q_UNUSED(service);
        qCDebug(DBUSMENUPROXY) << "Global menu service became available, starting";
        init();
    });
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
        Q_UNUSED(service);
        qCDebug(DBUSMENUPROXY) << "Global menu service disappeared, cleaning up";
        teardown();
    });

    // It's fine to do a blocking call here as we're a separate binary with no UI
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(s_dbusMenuRegistrar)) {
        qCDebug(DBUSMENUPROXY) << "Global menu service is running, starting right away";
        init();
    } else {
        qCDebug(DBUSMENUPROXY) << "No global menu service available, waiting for it to start before doing anything";

        // be sure when started to restore gtk menus when there's no dbus menu around in case we crashed
        setGtkShellShowsMenuBar(false);
    }
}

MenuProxy::~MenuProxy()
{
    teardown();
}

bool MenuProxy::init()
{
    if (!QDBusConnection::sessionBus().registerService(s_ourServiceName)) {
        qCWarning(DBUSMENUPROXY) << "Failed to register DBus service" << s_ourServiceName;
        return false;
    }

    setGtkShellShowsMenuBar(true);

    connect(KWindowSystem::self(), &KWindowSystem::windowAdded, this, &MenuProxy::onWindowAdded);
    connect(KWindowSystem::self(), &KWindowSystem::windowRemoved, this, &MenuProxy::onWindowRemoved);

    const auto windows = KWindowSystem::windows();
    for (WId id : windows) {
        onWindowAdded(id);
    }

    if (m_menus.isEmpty()) {
        qCDebug(DBUSMENUPROXY) << "Up and running but no menus in sight";
    }

    return true;
}

void MenuProxy::teardown()
{
    setGtkShellShowsMenuBar(false);

    QDBusConnection::sessionBus().unregisterService(s_ourServiceName);

    disconnect(KWindowSystem::self(), &KWindowSystem::windowAdded, this, &MenuProxy::onWindowAdded);
    disconnect(KWindowSystem::self(), &KWindowSystem::windowRemoved, this, &MenuProxy::onWindowRemoved);

    qDeleteAll(m_menus);
    m_menus.clear();
}

void MenuProxy::setGtkShellShowsMenuBar(bool show)
{
    qCDebug(DBUSMENUPROXY) << "Setting gtk-shell-shows-menu-bar to" << show << "which will" << (show ? "hide" : "show") << "menu bars in applications";

    // mostly taken from kde-gtk-config
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (root.isEmpty()) {
        root = QFileInfo(QDir::home(), QStringLiteral(".config")).absoluteFilePath();
    }

    const QString settingsFilePath = root + QStringLiteral("/gtk-3.0/settings.ini");

    auto cfg = KSharedConfig::openConfig(settingsFilePath, KConfig::NoGlobals);
    KConfigGroup group(cfg, "Settings");

    if (show) {
        group.writeEntry("gtk-shell-shows-menubar", true);
    } else {
        group.deleteEntry("gtk-shell-shows-menubar");
    }

    group.sync();

    // TODO use gconf/dconf directly or at least signal a change somehow?
}

void MenuProxy::onWindowAdded(WId id)
{
    if (m_menus.contains(id)) {
        return;
    }

    const QString serviceName = QString::fromUtf8(getWindowPropertyString(id, s_gtkUniqueBusName));

    if (serviceName.isEmpty()) {
        return;
    }

    const QString applicationObjectPath = QString::fromUtf8(getWindowPropertyString(id, s_gtkApplicationObjectPath));
    const QString unityObjectPath = QString::fromUtf8(getWindowPropertyString(id, s_unityObjectPath));
    const QString windowObjectPath = QString::fromUtf8(getWindowPropertyString(id, s_gtkWindowObjectPath));

    const QString applicationMenuObjectPath = QString::fromUtf8(getWindowPropertyString(id, s_gtkAppMenuObjectPath));
    const QString menuBarObjectPath = QString::fromUtf8(getWindowPropertyString(id, s_gtkMenuBarObjectPath));

    if (applicationMenuObjectPath.isEmpty() && menuBarObjectPath.isEmpty()) {
        return;
    }

    Menu *menu = new Menu(serviceName);
    menu->setWinId(id);
    menu->setApplicationObjectPath(applicationObjectPath);
    menu->setUnityObjectPath(unityObjectPath);
    menu->setWindowObjectPath(windowObjectPath);
    menu->setApplicationMenuObjectPath(applicationMenuObjectPath);
    menu->setMenuBarObjectPath(menuBarObjectPath);
    m_menus.insert(id, menu);

    connect(menu, &Menu::requestWriteWindowProperties, this, [this, menu] {
       Q_ASSERT(!menu->proxyObjectPath().isEmpty());

       writeWindowProperty(menu->winId(), s_kdeNetWmAppMenuServiceName, s_ourServiceName.toUtf8());
       writeWindowProperty(menu->winId(), s_kdeNetWmAppMenuObjectPath, menu->proxyObjectPath().toUtf8());
    });
    connect(menu, &Menu::requestRemoveWindowProperties, this, [this, menu] {
        writeWindowProperty(menu->winId(), s_kdeNetWmAppMenuServiceName, QByteArray());
        writeWindowProperty(menu->winId(), s_kdeNetWmAppMenuObjectPath, QByteArray());
    });

    menu->init();
}

void MenuProxy::onWindowRemoved(WId id)
{
    // no need to cleanup() (which removes window properties) when the window is gone, delete right away
    delete m_menus.take(id);
}

QByteArray MenuProxy::getWindowPropertyString(WId id, const QByteArray &name)
{
    auto *c = QX11Info::connection(); // FIXME cache

    QByteArray value;

    auto atom = getAtom(name);
    if (atom == XCB_ATOM_NONE) {
        return value;
    }

    static const long MAX_PROP_SIZE = 10000;
    // FIXME figure out what "UT8String" is as atom type, it's 392 or 378 but I don't find that enum
    auto propertyCookie = xcb_get_property(c, false, id, atom, XCB_ATOM_ANY, 0, MAX_PROP_SIZE);
    QScopedPointer<xcb_get_property_reply_t, QScopedPointerPodDeleter> propertyReply(xcb_get_property_reply(c, propertyCookie, NULL));
    if (propertyReply.isNull()) {
        qCWarning(DBUSMENUPROXY) << "XCB property reply for atom" << name << "on" << id << "was null";
        return value;
    }

    // FIXME Check type
    if (/*propertyReply->type == 392 && */propertyReply->format == 8 && propertyReply->value_len > 0) {
        const char *data = (const char *) xcb_get_property_value(propertyReply.data());
        int len = propertyReply->value_len;
        if (data) {
            value = QByteArray(data, data[len - 1] ? len : len - 1);
        }
    }

    return value;
}

void MenuProxy::writeWindowProperty(WId id, const QByteArray &name, const QByteArray &value)
{
    auto atom = getAtom(name);
    if (atom == XCB_ATOM_NONE) {
        return;
    }

    if (value.isEmpty()) {
        xcb_delete_property(m_xConnection, id, atom);
    } else {
        xcb_change_property(m_xConnection, XCB_PROP_MODE_REPLACE, id, atom, XCB_ATOM_STRING,
                            8, value.length(), value.constData());
    }
}

xcb_atom_t MenuProxy::getAtom(const QByteArray &name)
{
    static QHash<QByteArray, xcb_atom_t> s_atoms;

    auto atom = s_atoms.value(name, XCB_ATOM_NONE);
    if (atom == XCB_ATOM_NONE) {
        const xcb_intern_atom_cookie_t atomCookie = xcb_intern_atom(m_xConnection, false, name.length(), name.constData());
        QScopedPointer<xcb_intern_atom_reply_t, QScopedPointerPodDeleter> atomReply(xcb_intern_atom_reply(m_xConnection, atomCookie, Q_NULLPTR));
        if (!atomReply.isNull()) {
            atom = atomReply->atom;
            if (atom != XCB_ATOM_NONE) {
                s_atoms.insert(name, atom);
            }
        }
    }

    return atom;
}
