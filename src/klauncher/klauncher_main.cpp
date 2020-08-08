/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999 Waldo Bastian <bastian@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include <config-kdeinit.h>

#include <klauncher_debug.h>

#include <qplatformdefs.h>

#include "klauncher.h"
#include <KCrash>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <KLocalizedString>

#include "klauncher_cmds.h"
#include <QGuiApplication>
#include <QFile>
#include <QDebug>
#include <QDBusConnectionInterface>
#include <QThread>

#ifdef Q_OS_OSX
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifndef USE_KPROCESS_FOR_KIOSLAVES
static int sigpipe[ 2 ];
static void sig_handler(int sig_num)
{
    // No recursion
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    fprintf(stderr, "klauncher: Exiting on signal %d\n", sig_num);
    char tmp = 'x';
    write(sigpipe[ 1 ], &tmp, 1);
}
#endif

#if defined(Q_OS_DARWIN)
// Copied from kkernel_mac.cpp
bool dbus_initialized = false;

/**
 Set the D-Bus environment based on session bus socket
*/

bool mac_set_dbus_address(QString value)
{
    if (!value.isEmpty() && QFile::exists(value) && (QFile::permissions(value) & QFile::WriteUser)) {
        value = QLatin1String("unix:path=") + value;
        qputenv("DBUS_SESSION_BUS_ADDRESS", value.toLocal8Bit());
        // qDebug() << "set session bus address to" << value;
        return true;
    }
    return false;
}

/**
 Make sure D-Bus is initialized, by any means necessary.
*/

void mac_initialize_dbus()
{
    enum
    {
        timeout = 3000 // msec. Copied from old kdecore/kernel/kkernel_mac.cpp
    };

    if (dbus_initialized) {
        return;
    }

    QString dbusVar = QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    if (!dbusVar.isEmpty()) {
        dbus_initialized = true;
        return;
    }

    dbusVar = QFile::decodeName(qgetenv("DBUS_LAUNCHD_SESSION_BUS_SOCKET"));
    if (mac_set_dbus_address(dbusVar)) {
        dbus_initialized = true;
        return;
    }

    QString externalProc;
    QStringList path = QFile::decodeName(qgetenv("KDEDIRS")).split(QLatin1Char(':')).replaceInStrings(QRegExp(QLatin1String("$")), QLatin1String("/bin"));
    path << QFile::decodeName(qgetenv("PATH")).split(QLatin1Char(':')) << QLatin1String("/usr/local/bin");

    for (int i = 0; i < path.size(); ++i) {
        QString testLaunchctl = QString(path.at(i)).append(QLatin1String("/launchctl"));
        if (QFile(testLaunchctl).exists()) {
            externalProc = testLaunchctl;
            break;
        }
    }

    if (!externalProc.isEmpty()) {
        QProcess qp;
        qp.setTextModeEnabled(true);

        qp.start(externalProc, QStringList() << QLatin1String("getenv") << QLatin1String("DBUS_LAUNCHD_SESSION_BUS_SOCKET"));
        if (!qp.waitForFinished(timeout)) {
            // qDebug() << "error running" << externalProc << qp.errorString();
            return;
        }
        if (qp.exitCode() != 0) {
            // qDebug() << externalProc << "unsuccessful:" << qp.readAllStandardError();
            return;
        }

        QString line = QString::fromLatin1(qp.readLine()).trimmed(); // read the first line
        if (mac_set_dbus_address(line)) {
            dbus_initialized = true;    // hooray
        }
    }

    if (dbus_initialized == false) {
        // qDebug() << "warning: unable to initialize D-Bus environment!";
    }

}
#endif

extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
{
#ifndef Q_OS_WIN
    // Started via kdeinit.
    int launcherFd;
    if (argc != 2 || memcmp(argv[1], "--fd=", 5) || !(launcherFd = atoi(argv[1] + 5))) {
        fprintf(stderr, "%s", i18n("klauncher: This program is not supposed to be started manually.\n"
                                   "klauncher: It is started automatically by kdeinit5.\n").toLocal8Bit().data());
        return 1;
    }
#endif

#if defined(Q_OS_DARWIN)
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (mainBundle) {
        // get the application's Info Dictionary. For app bundles this would live in the bundle's Info.plist,
        // for regular executables it is obtained in another way.
        CFMutableDictionaryRef infoDict = (CFMutableDictionaryRef) CFBundleGetInfoDictionary(mainBundle);
        if (infoDict) {
            // Add or set the "LSUIElement" key with/to value "1". This can simply be a CFString.
            CFDictionarySetValue(infoDict, CFSTR("LSUIElement"), CFSTR("1"));
            // That's it. We're now considered as an "agent" by the window server, and thus will have
            // neither menubar nor presence in the Dock or App Switcher.
        }
    }
    mac_initialize_dbus();
#endif

    // WABA: Make sure not to enable session management.
    qunsetenv("SESSION_MANAGER");

    // Disable the GLib event loop (rh#983110)
    const bool wasQtNoGlibSet = !qEnvironmentVariableIsEmpty("QT_NO_GLIB");
    if (!wasQtNoGlibSet) {
       qputenv("QT_NO_GLIB", "1");
    }

    // We need a QGuiApplication as we use X11
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("klauncher"));

    // Now get rid of QT_NO_GLIB again so launched processes don't inherit it
    if (!wasQtNoGlibSet) {
       qunsetenv("QT_NO_GLIB");
    }

    int maxTry = 3;
    while (true) {
        QString service(QStringLiteral("org.kde.klauncher5")); // same as ktoolinvocation.cpp
        if (!QDBusConnection::sessionBus().isConnected()) {
            qCWarning(KLAUNCHER) << "No D-Bus session-bus found. Check if you have started the D-Bus server.";
            return 1;
        }
        QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
            QDBusConnection::sessionBus().interface()->registerService(service);
        if (!reply.isValid()) {
            qCWarning(KLAUNCHER) << "D-Bus communication problem!";
            return 1;
        }
        if (reply == QDBusConnectionInterface::ServiceRegistered) {
            break;
        }

        if (--maxTry == 0) {
            qCWarning(KLAUNCHER) << "Another instance of klauncher is already running!";
            return 1;
        }

        // Wait a bit...
        qCWarning(KLAUNCHER) << "Waiting for already running klauncher to exit.";
        QThread::sleep(1);

        // Try again...
    }

#ifndef USE_KPROCESS_FOR_KIOSLAVES
    KLauncher *launcher = new KLauncher(launcherFd);
#else
    KLauncher *launcher = new KLauncher();
#endif
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), launcher);

#ifndef USE_KPROCESS_FOR_KIOSLAVES
    if (pipe(sigpipe) != 0) {
        perror("klauncher: pipe failed.");
        return 1;
    }
    QSocketNotifier *signotif = new QSocketNotifier(sigpipe[ 0 ], QSocketNotifier::Read, launcher);
    QObject::connect(signotif, SIGNAL(activated(int)), launcher, SLOT(destruct()));
    KCrash::setEmergencySaveFunction(sig_handler);
    signal(SIGHUP, sig_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sig_handler);
#endif

    return app.exec();
}
