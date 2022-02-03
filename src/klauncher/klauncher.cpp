/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999 Waldo Bastian <bastian@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#ifndef QT_NO_CAST_FROM_ASCII
#define QT_NO_CAST_FROM_ASCII
#endif

#include "klauncher.h"
#include "klauncher_cmds.h"
#include "klauncher_adaptor.h"
#include "kslavelauncheradaptor.h"
#include <klauncher_debug.h>

#include <stdio.h>
#include <qplatformdefs.h>
#include <signal.h>
#include <cerrno>

#if HAVE_X11
#include <kstartupinfo.h>
#include <QGuiApplication>
#endif

#if HAVE_XCB
#include <xcb/xcb.h>
#endif

#include <QDBusConnectionInterface>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <qplatformdefs.h>

#include <KConfig>
#include <QDebug>
#include <KLocalizedString>
#include <KDesktopFile>
#include <KPluginLoader> // to find kioslave modules
#include <KProtocolManager>
#include <KProtocolInfo>
#include <KRun> // TODO port away from kiofilewidgets

#include <kio/desktopexecparser.h>
#include <kio/global.h>
#include <kio/slaveinterface.h>
#include <kiogui_export.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
//windows.h feels like defining this...
#undef interface
#endif

// Dispose slaves after being idle for SLAVE_MAX_IDLE seconds
#define SLAVE_MAX_IDLE  30

static const char *const s_DBusStartupTypeToString[] =
{ "DBusNone", "DBusUnique", "DBusMulti", "ERROR" };

using namespace KIO;

static KLauncher *g_klauncher_self;

// From qcore_unix_p.h. We could also port to QLocalSocket :)
#define K_EINTR_LOOP(var, cmd)                    \
    do {                                        \
        var = cmd;                              \
    } while (var == -1 && errno == EINTR)

ssize_t kde_safe_write(int fd, const void *buf, size_t count)
{
    ssize_t ret = 0;
    K_EINTR_LOOP(ret, QT_WRITE(fd, buf, count));
    if (ret < 0) {
        qCWarning(KLAUNCHER) << "write failed:" << strerror(errno);
    }
    return ret;
}

#ifndef USE_KPROCESS_FOR_KIOSLAVES
KLauncher::KLauncher(int _kdeinitSocket)
    : QObject(nullptr),
      kdeinitSocket(_kdeinitSocket)
#else
KLauncher::KLauncher()
    : QObject(0)
#endif
{
#if HAVE_X11
    mIsX11 = QGuiApplication::platformName() == QStringLiteral("xcb");
#endif
    Q_ASSERT(g_klauncher_self == nullptr);
    g_klauncher_self = this;

    new KLauncherAdaptor(this);
    mSlaveLauncherAdaptor = new KSlaveLauncherAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/KLauncher"), this); // same as ktoolinvocation.cpp

    connect(QDBusConnection::sessionBus().interface(),
            SIGNAL(serviceOwnerChanged(QString,QString,QString)),
            SLOT(slotNameOwnerChanged(QString,QString,QString)));

    mConnectionServer.listenForRemote();
    connect(&mConnectionServer, SIGNAL(newConnection()), SLOT(acceptSlave()));
    if (!mConnectionServer.isListening()) {
        // Severe error!
        qCWarning(KLAUNCHER, "KLauncher: Fatal error, can't create tempfile!");
        ::_exit(1);
    }

    connect(&mTimer, SIGNAL(timeout()), SLOT(idleTimeout()));

#ifndef USE_KPROCESS_FOR_KIOSLAVES
    kdeinitNotifier = new QSocketNotifier(kdeinitSocket, QSocketNotifier::Read);
    connect(kdeinitNotifier, SIGNAL(activated(int)),
            this, SLOT(slotKDEInitData(int)));
    kdeinitNotifier->setEnabled(true);
#endif
    lastRequest = nullptr;
    bProcessingQueue = false;

    mSlaveDebug = QString::fromLocal8Bit(qgetenv("KDE_SLAVE_DEBUG_WAIT"));
    if (!mSlaveDebug.isEmpty()) {
#ifndef USE_KPROCESS_FOR_KIOSLAVES
        qCWarning(KLAUNCHER, "Klauncher running in slave-debug mode for slaves of protocol '%s'", qPrintable(mSlaveDebug));
#else
        // Slave debug mode causes kdeinit to suspend the process waiting
        // for the developer to attach gdb to the process; we do not have
        // a good way of doing a similar thing if we are using QProcess.
        mSlaveDebug.clear();
        qCWarning(KLAUNCHER, "slave-debug mode is not available as Klauncher is not using kdeinit");
#endif
    }
    mSlaveValgrind = QString::fromLocal8Bit(qgetenv("KDE_SLAVE_VALGRIND"));
    if (!mSlaveValgrind.isEmpty()) {
        mSlaveValgrindSkin = QString::fromLocal8Bit(qgetenv("KDE_SLAVE_VALGRIND_SKIN"));
        qCWarning(KLAUNCHER, "Klauncher running slaves through valgrind for slaves of protocol '%s'", qPrintable(mSlaveValgrind));
    }
#ifdef USE_KPROCESS_FOR_KIOSLAVES
    qCDebug(KLAUNCHER) << "LAUNCHER_OK";
#else
    klauncher_header request_header;
    request_header.cmd = LAUNCHER_OK;
    request_header.arg_length = 0;
    kde_safe_write(kdeinitSocket, &request_header, sizeof(request_header));
#endif
}

KLauncher::~KLauncher()
{
    close();
    g_klauncher_self = nullptr;
}

void KLauncher::close()
{
#if HAVE_XCB
    if (mCached) {
        xcb_disconnect(mCached.conn);
        mCached = XCBConnection();
    }
#endif
}

void
KLauncher::destruct()
{
    if (g_klauncher_self) {
        g_klauncher_self->close();
    }
    // We don't delete the app here, that's intentional.
    ::_exit(255);
}

void KLauncher::setLaunchEnv(const QString &name, const QString &value)
{
#ifndef USE_KPROCESS_FOR_KIOSLAVES
    klauncher_header request_header;
    QByteArray requestData;
    requestData.append(name.toLocal8Bit()).append('\0').append(value.toLocal8Bit()).append('\0');
    request_header.cmd = LAUNCHER_SETENV;
    request_header.arg_length = requestData.size();
    kde_safe_write(kdeinitSocket, &request_header, sizeof(request_header));
    kde_safe_write(kdeinitSocket, requestData.data(), request_header.arg_length);
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
#endif
}

#ifndef USE_KPROCESS_FOR_KIOSLAVES
/*
 * Read 'len' bytes from 'sock' into buffer.
 * returns -1 on failure, 0 on no data.
 */
static int
read_socket(int sock, char *buffer, int len)
{
    int bytes_left = len;
    while (bytes_left > 0) {
        // in case we get a request to start an application and data arrive
        // to kdeinitSocket at the same time, requestStart() will already
        // call slotKDEInitData(), so we must check there's still something
        // to read, otherwise this would block

        // Same thing if kdeinit dies without warning.

        fd_set in;
        timeval tm = { 30, 0 }; // 30 seconds timeout, so we're not stuck in case kdeinit dies on us
        FD_ZERO(&in);
        FD_SET(sock, &in);
        select(sock + 1, &in, nullptr, nullptr, &tm);
        if (!FD_ISSET(sock, &in)) {
            qCDebug(KLAUNCHER) << "read_socket" << sock << "nothing to read, kdeinit5 must be dead";
            return -1;
        }

        const ssize_t result = read(sock, buffer, bytes_left);
        if (result > 0) {
            buffer += result;
            bytes_left -= result;
        } else if (result == 0) {
            return -1;
        } else if ((result == -1) && (errno != EINTR)) {
            return -1;
        }
    }
    return 0;
}
#endif

void
KLauncher::slotKDEInitData(int)
{
#ifndef USE_KPROCESS_FOR_KIOSLAVES
    klauncher_header request_header;
    QByteArray requestData;

    int result = read_socket(kdeinitSocket, (char *) &request_header,
                             sizeof(request_header));
    if (result != -1) {
        requestData.resize(request_header.arg_length);
        result = read_socket(kdeinitSocket, (char *) requestData.data(),
                             request_header.arg_length);
    }
    if (result == -1) {
        qCDebug(KLAUNCHER) << "Exiting on read_socket errno:" << errno;
        signal(SIGHUP, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        destruct(); // Exit!
    }

    processRequestReturn(request_header.cmd, requestData);
#endif
}

void KLauncher::processRequestReturn(int status, const QByteArray &requestData)
{
    if (status == LAUNCHER_CHILD_DIED) {
        long *request_data;
        request_data = (long *) requestData.data();
        processDied(request_data[0], request_data[1]);
        return;
    }
    if (lastRequest && (status == LAUNCHER_OK)) {
        long *request_data;
        request_data = (long *) requestData.data();
        lastRequest->pid = (pid_t)(*request_data);
        qCDebug(KLAUNCHER).nospace() << lastRequest->name << " (pid " << lastRequest->pid << ") up and running.";
        switch (lastRequest->dbus_startup_type) {
        case KService::DBusNone:
            if (lastRequest->wait) {
                lastRequest->status = KLaunchRequest::Launching;
            } else {
                lastRequest->status = KLaunchRequest::Running;
            }
            break;
        case KService::DBusUnique:
        case KService::DBusMulti:
            lastRequest->status = KLaunchRequest::Launching;
            break;
        }
        lastRequest = nullptr;
        return;
    }
    if (lastRequest && (status == LAUNCHER_ERROR)) {
        lastRequest->status = KLaunchRequest::Error;
        qCDebug(KLAUNCHER) << lastRequest->name << " failed.";
        if (!requestData.isEmpty()) {
            lastRequest->errorMsg = QString::fromUtf8((char *) requestData.data());
        }
        lastRequest = nullptr;
        return;
    }

    qCWarning(KLAUNCHER) << "Unexpected request return" << (unsigned int) status;
}

void
KLauncher::processDied(pid_t pid, long exitStatus)
{
    qCDebug(KLAUNCHER) << pid << "exitStatus=" << exitStatus;
    for (KLaunchRequest *request : std::as_const(requestList)) {
        qCDebug(KLAUNCHER) << "  had pending request" << request->pid;
        if (request->pid == pid) {
            if ((request->dbus_startup_type == KService::DBusUnique)
                    && QDBusConnection::sessionBus().interface()->isServiceRegistered(request->dbus_name)) {
                request->status = KLaunchRequest::Running;
                qCDebug(KLAUNCHER) << pid << "running as a unique app";
            } else if(request->dbus_startup_type == KService::DBusNone && request->wait) {
                request->status = KLaunchRequest::Running;
                qCDebug(KLAUNCHER) << pid << "running as DBusNone with wait to true";
            } else if (exitStatus == 0 &&
                       (request->dbus_startup_type == KService::DBusUnique ||
                        request->dbus_startup_type == KService::DBusMulti)) {
                // e.g. opening kate from a widget on the panel/desktop, where it
                // shows the session chooser dialog before ever entering the main
                // app event loop, then quitting/closing the dialog without starting kate
                request->status = KLaunchRequest::Done;
                qCDebug(KLAUNCHER) << pid << "exited without error, requestDone. status=" << request->status;
            } else {
                request->status = KLaunchRequest::Error;
                qCDebug(KLAUNCHER) << pid << "died, requestDone. status=" << request->status;
            }
            requestDone(request);
            return;
        }
    }
    qCDebug(KLAUNCHER) << "found no pending requests for PID" << pid;
}

static bool matchesPendingRequest(const QString &appId, const QString &pendingAppId)
{
    // appId just registered, e.g. org.koffice.kword-12345
    // Let's see if this is what pendingAppId (e.g. org.koffice.kword or *.kword) was waiting for.

    const QString newAppId = appId.left(appId.lastIndexOf(QLatin1Char('-'))); // strip out the -12345 if present.

    qCDebug(KLAUNCHER) << "appId=" << appId << "newAppId=" << newAppId << "pendingAppId=" << pendingAppId;

    if (pendingAppId.startsWith(QLatin1String("*."))) {
        const QString pendingName = pendingAppId.mid(2);
        const QString appName = newAppId.mid(newAppId.lastIndexOf(QLatin1Char('.')) + 1);
        qCDebug(KLAUNCHER) << "appName=" << appName;
        return appName == pendingName;
    }

    // Match sandboxed apps (e.g. flatpak), see https://phabricator.kde.org/D5775
    if (newAppId.endsWith(QLatin1String(".kdbus"))) {
        return newAppId.leftRef(newAppId.length() - 6) == pendingAppId;
    }

    return newAppId == pendingAppId;
}

void
KLauncher::slotNameOwnerChanged(const QString &appId, const QString &oldOwner,
                                const QString &newOwner)
{
    Q_UNUSED(oldOwner);
    if (appId.isEmpty() || newOwner.isEmpty()) {
        return;
    }

    qCDebug(KLAUNCHER) << "new app" << appId;
    for (KLaunchRequest *request : std::as_const(requestList)) {
        if (request->status != KLaunchRequest::Launching) {
            continue;
        }

        qCDebug(KLAUNCHER) << "had pending request" << request->name << s_DBusStartupTypeToString[request->dbus_startup_type] << "dbus_name" << request->dbus_name << request->tolerant_dbus_name;

        // For unique services check the requested service name first
        if (request->dbus_startup_type == KService::DBusUnique) {
            if ((appId == request->dbus_name) || // just started
                    QDBusConnection::sessionBus().interface()->isServiceRegistered(request->dbus_name)) { // was already running
                request->status = KLaunchRequest::Running;
                qCDebug(KLAUNCHER) << "OK, unique app" << request->dbus_name << "is running";
                requestDone(request);
                continue;
            } else {
                qCDebug(KLAUNCHER) << "unique app" << request->dbus_name << "not running yet";
            }
        }

        const QString rAppId = !request->tolerant_dbus_name.isEmpty() ? request->tolerant_dbus_name : request->dbus_name;
        qCDebug(KLAUNCHER) << "using" << rAppId << "for matching";
        if (rAppId.isEmpty()) {
            continue;
        }

        if (matchesPendingRequest(appId, rAppId)) {
            qCDebug(KLAUNCHER) << "ok, request done";
            request->dbus_name = appId;
            request->status = KLaunchRequest::Running;
            requestDone(request);
            continue;
        }
    }
}

void
KLauncher::requestDone(KLaunchRequest *request)
{
    if ((request->status == KLaunchRequest::Running) ||
            (request->status == KLaunchRequest::Done)) {
        requestResult.result = 0;
        requestResult.dbusName = request->dbus_name;
        requestResult.error = QStringLiteral(""); // not null, cf assert further down
        requestResult.pid = request->pid;
    } else {
        requestResult.result = 1;
        requestResult.dbusName.clear();
        requestResult.error = i18n("KDEInit could not launch '%1'", request->name);
        if (!request->errorMsg.isEmpty()) {
            requestResult.error += QStringLiteral(":\n") + request->errorMsg;
        }
        requestResult.pid = 0;

#if HAVE_XCB
        if (!request->startup_dpy.isEmpty() && mIsX11) {
            XCBConnection conn = getXCBConnection(request->startup_dpy);
            if (conn) {
                KStartupInfoId id;
                id.initId(request->startup_id);
                KStartupInfo::sendFinishXcb(conn.conn, conn.screen, id);
            }
        }
#endif
    }

    if (request->transaction.type() != QDBusMessage::InvalidMessage) {
        if (requestResult.dbusName.isNull()) { // null strings can't be sent
            requestResult.dbusName.clear();
        }
        Q_ASSERT(!requestResult.error.isNull());
        quintptr stream_pid = requestResult.pid;
        QDBusConnection::sessionBus().send(request->transaction.createReply(QVariantList() << requestResult.result
                                           << requestResult.dbusName
                                           << requestResult.error
                                           << stream_pid));
    }

    qCDebug(KLAUNCHER) << "removing done request" << request->name << "PID" << request->pid;
    requestList.removeAll(request);
    delete request;
}

static void appendLong(QByteArray &ba, long l)
{
    const int sz = ba.size();
    ba.resize(sz + sizeof(long));
    memcpy(ba.data() + sz, &l, sizeof(long));
}

void
KLauncher::requestStart(KLaunchRequest *request)
{
#ifdef USE_KPROCESS_FOR_KIOSLAVES
    requestList.append(request);
    lastRequest = request;

    QProcess *process  = new QProcess;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(slotGotOutput()));
    connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(slotFinished(int,QProcess::ExitStatus)));
    request->process = process;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &env : std::as_const(request->envs)) {
        const int pos = env.indexOf(QLatin1Char('='));
        const QString envVariable = env.left(pos);
        const QString envValue = env.mid(pos + 1);
        environment.insert(envVariable, envValue);
    }
    process->setProcessEnvironment(environment);

    QStringList args;
    for (const QString &arg : std::as_const(request->arg_list)) {
        args << arg;
    }

    const QString executable = QStandardPaths::findExecutable(request->name);
    if (executable.isEmpty()) {
        qCDebug(KLAUNCHER) << "KLauncher couldn't find" << request->name << "executable.";
        return;
    }

    process->start(executable, args);

    if (!process->waitForStarted()) {
        processRequestReturn(LAUNCHER_ERROR, "");
    } else {
        request->pid = process->processId();
        QByteArray data((char *)&request->pid, sizeof(int));
        processRequestReturn(LAUNCHER_OK, data);
    }
    return;

#else
    requestList.append(request);
    // Send request to kdeinit.
    klauncher_header request_header;
    QByteArray requestData;
    requestData.reserve(1024);

    appendLong(requestData, request->arg_list.count() + 1);
    requestData.append(request->name.toLocal8Bit());
    requestData.append('\0');
    for (const QString &arg : std::as_const(request->arg_list)) {
        requestData.append(arg.toLocal8Bit()).append('\0');
    }
    appendLong(requestData, request->envs.count());
    for (const QString &env : std::as_const(request->envs)) {
        requestData.append(env.toLocal8Bit()).append('\0');
    }
    appendLong(requestData, 0); // avoid_loops, always false here
#if HAVE_X11
    bool startup_notify = mIsX11 && !request->startup_id.isNull() && request->startup_id != "0";
    if (startup_notify) {
        requestData.append(request->startup_id).append('\0');
    }
#endif
    if (!request->cwd.isEmpty()) {
        requestData.append(QFile::encodeName(request->cwd)).append('\0');
    }

#if HAVE_X11
    request_header.cmd = startup_notify ? LAUNCHER_EXT_EXEC : LAUNCHER_EXEC_NEW;
#else
    request_header.cmd = LAUNCHER_EXEC_NEW;
#endif
    request_header.arg_length = requestData.length();

    qCDebug(KLAUNCHER) << "Asking kdeinit to start" << request->name << request->arg_list
            << "cmd=" << commandToString(request_header.cmd);

    kde_safe_write(kdeinitSocket, &request_header, sizeof(request_header));
    kde_safe_write(kdeinitSocket, requestData.data(), requestData.length());

    // Wait for pid to return.
    lastRequest = request;
    do {
        slotKDEInitData(kdeinitSocket);
    } while (lastRequest != nullptr);
#endif
}

void KLauncher::exec_blind(const QString &name, const QStringList &arg_list, const QStringList &envs, const QString &startup_id)
{
    KLaunchRequest *request = new KLaunchRequest;
    request->name = name;
    request->arg_list =  arg_list;
    request->dbus_startup_type = KService::DBusNone;
    request->pid = 0;
    request->status = KLaunchRequest::Launching;
    request->envs = envs;
    request->wait = false;
    // Find service, if any - strip path if needed
    KService::Ptr service = KService::serviceByDesktopName(name.mid(name.lastIndexOf(QLatin1Char('/')) + 1));
    if (service) {
        send_service_startup_info(request, service, startup_id.toLocal8Bit(), QStringList());
    } else { // no .desktop file, no startup info
        cancel_service_startup_info(request, startup_id.toLocal8Bit(), envs);
    }

    requestStart(request);
    // We don't care about this request any longer....
    requestDone(request);
}

bool
KLauncher::start_service_by_desktop_path(const QString &serviceName, const QStringList &urls,
        const QStringList &envs, const QString &startup_id, bool blind, const QDBusMessage &msg)
{
    KService::Ptr service;
    // Find service
    const QFileInfo fi(serviceName);
    if (fi.isAbsolute() && fi.exists()) {
        // Full path
        service = new KService(serviceName);
    } else {
        service = KService::serviceByDesktopPath(serviceName);
        // TODO?
        //if (!service)
        //    service = KService::serviceByStorageId(serviceName); // This method should be named start_service_by_storage_id ideally...
    }
    if (!service) {
        requestResult.result = ENOENT;
        requestResult.error = i18n("Could not find service '%1'.", serviceName);
        cancel_service_startup_info(nullptr, startup_id.toLocal8Bit(), envs);   // cancel it if any
        return false;
    }
    return start_service(service, urls, envs, startup_id.toLocal8Bit(), blind, msg);
}

bool
KLauncher::start_service_by_desktop_name(const QString &serviceName, const QStringList &urls,
        const QStringList &envs, const QString &startup_id, bool blind, const QDBusMessage &msg)
{
    KService::Ptr service = KService::serviceByDesktopName(serviceName);
    if (!service) {
        requestResult.result = ENOENT;
        requestResult.error = i18n("Could not find service '%1'.", serviceName);
        cancel_service_startup_info(nullptr, startup_id.toLocal8Bit(), envs);   // cancel it if any
        return false;
    }
    return start_service(service, urls, envs, startup_id.toLocal8Bit(), blind, msg);
}

bool
KLauncher::start_service(KService::Ptr service, const QStringList &_urls,
                         const QStringList &envs, const QByteArray &startup_id,
                         bool blind, const QDBusMessage &msg)
{
    QStringList urls = _urls;
    bool runPermitted = KDesktopFile::isAuthorizedDesktopFile(service->entryPath());

    if (!service->isValid() || !runPermitted) {
        requestResult.result = ENOEXEC;
        if (service->isValid()) {
            requestResult.error = i18n("Service '%1' must be executable to run.", service->entryPath());
        } else {
            requestResult.error = i18n("Service '%1' is malformatted.", service->entryPath());
        }
        cancel_service_startup_info(nullptr, startup_id, envs);   // cancel it if any
        return false;
    }
    KLaunchRequest *request = new KLaunchRequest;

    enum DiscreteGpuCheck { NotChecked, Present, Absent };
    static DiscreteGpuCheck s_gpuCheck = NotChecked;

    if (service->runOnDiscreteGpu() && s_gpuCheck == NotChecked) {
        // Check whether we have a discrete gpu
        bool hasDiscreteGpu = false;
        QDBusInterface iface(QLatin1String("org.kde.Solid.PowerManagement"),
                             QLatin1String("/org/kde/Solid/PowerManagement"),
                             QLatin1String("org.kde.Solid.PowerManagement"),
                             QDBusConnection::sessionBus());
        if (iface.isValid()) {
            QDBusReply<bool> reply = iface.call(QLatin1String("hasDualGpu"));
            if (reply.isValid()) {
                hasDiscreteGpu = reply.value();
            }
        }

        s_gpuCheck = hasDiscreteGpu ? Present : Absent;
    }

    QStringList _envs = envs;
    if (service->runOnDiscreteGpu() && s_gpuCheck == Present) {
        _envs << QLatin1String("DRI_PRIME=1");
    }

    if ((urls.count() > 1) && !service->allowMultipleFiles()) {
        // We need to launch the application N times. That sucks.
        // We ignore the result for application 2 to N.
        // For the first file we launch the application in the
        // usual way. The reported result is based on this
        // application.
        QStringList::ConstIterator it = urls.constBegin();
        for (++it;
                it != urls.constEnd();
                ++it) {
            QStringList singleUrl;
            singleUrl.append(*it);
            QByteArray startup_id2 = startup_id;
            if (!startup_id2.isEmpty() && startup_id2 != "0") {
                startup_id2 = "0";    // can't use the same startup_id several times // krazy:exclude=doublequote_chars
            }
            start_service(service, singleUrl, _envs, startup_id2, true, msg);
        }
        const QString firstURL = urls.at(0);
        urls.clear();
        urls.append(firstURL);
    }
    const QList<QUrl> qurls = QUrl::fromStringList(urls);

    createArgs(request, service, qurls);

    // We must have one argument at least!
    if (request->arg_list.isEmpty()) {
        requestResult.result = ENOEXEC;
        requestResult.error = i18n("Service '%1' is malformatted.", service->entryPath());
        delete request;
        cancel_service_startup_info(nullptr, startup_id, _envs);
        return false;
    }

    request->name = request->arg_list.takeFirst();

    if (request->name.endsWith(QLatin1String("/kioexec"))) {
        // Special case for kioexec; if createArgs said we were going to use it,
        // then we have to expect a kioexec-PID, not a org.kde.finalapp...
        // Testcase: konqueror www.kde.org, RMB on link, open with, kruler.

        request->dbus_startup_type = KService::DBusMulti;
        request->dbus_name = QStringLiteral("org.kde.kioexec");
    } else {
        request->dbus_startup_type = service->dbusStartupType();

        if ((request->dbus_startup_type == KService::DBusUnique) ||
                (request->dbus_startup_type == KService::DBusMulti)) {
            const QVariant v = service->property(QStringLiteral("X-DBUS-ServiceName"));
            if (v.isValid()) {
                request->dbus_name = v.toString();
            }
            if (request->dbus_name.isEmpty()) {
                const QString binName = KIO::DesktopExecParser::executableName(service->exec());
                request->dbus_name = QStringLiteral("org.kde.") + binName;
                request->tolerant_dbus_name = QStringLiteral("*.") + binName;
            }
        }
    }

    qCDebug(KLAUNCHER) << "name=" << request->name << "dbus_name=" << request->dbus_name
            << "startup type=" << s_DBusStartupTypeToString[request->dbus_startup_type];

    request->pid = 0;
    request->wait = false;
    request->envs = _envs;
    send_service_startup_info(request, service, startup_id, _envs);

    // Request will be handled later.
    if (!blind) {
        msg.setDelayedReply(true);
        request->transaction = msg;
    }
    queueRequest(request);
    return true;
}


namespace KIOGuiPrivate {
// defined in kprocessrunner.cpp
extern bool KIOGUI_EXPORT checkStartupNotify(const KService *service, bool *silent_arg,
                                             QByteArray *wmclass_arg);
}

void
KLauncher::send_service_startup_info(KLaunchRequest *request, KService::Ptr service, const QByteArray &startup_id,
                                     const QStringList &envs)
{
#if HAVE_XCB
    if (!mIsX11) {
        return;
    }
    request->startup_id = "0";// krazy:exclude=doublequote_chars
    if (startup_id == "0") {
        return;
    }
    bool silent;
    QByteArray wmclass;

    if (!KIOGuiPrivate::checkStartupNotify(service.data(), &silent, &wmclass)) {
        return;
    }
    KStartupInfoId id;
    id.initId(startup_id);
    QByteArray dpy_str;
    for (const QString &env : envs) {
        if (env.startsWith(QLatin1String("DISPLAY="))) {
            dpy_str = env.mid(8).toLocal8Bit();
        }
    }

    XCBConnection conn = getXCBConnection(dpy_str);
    request->startup_id = id.id();
    if (!conn) {
        cancel_service_startup_info(request, startup_id, envs);
        return;
    }

    request->startup_dpy = conn.displayName;

    KStartupInfoData data;
    data.setName(service->name());
    data.setIcon(service->icon());
    data.setDescription(i18n("Launching %1",  service->name()));
    if (!wmclass.isEmpty()) {
        data.setWMClass(wmclass);
    }
    if (silent) {
        data.setSilent(KStartupInfoData::Yes);
    }
    data.setApplicationId(service->entryPath());
    // the rest will be sent by kdeinit
    KStartupInfo::sendStartupXcb(conn.conn, conn.screen, id, data);
#endif
}

void
KLauncher::cancel_service_startup_info(KLaunchRequest *request, const QByteArray &startup_id,
                                       const QStringList &envs)
{
#if HAVE_XCB
    if (request != nullptr) {
        request->startup_id = "0";    // krazy:exclude=doublequote_chars
    }
    if (!startup_id.isEmpty() && startup_id != "0" && mIsX11) {
        QString dpy_str;
        for (const QString &env : envs) {
            if (env.startsWith(QLatin1String("DISPLAY="))) {
                dpy_str = env.mid(8);
            }
        }
        XCBConnection conn = getXCBConnection(dpy_str.toLocal8Bit());
        if (!conn) {
            return;
        }
        KStartupInfoId id;
        id.initId(startup_id);
        KStartupInfo::sendFinishXcb(conn.conn, conn.screen, id);
    }
#endif
}

bool
KLauncher::kdeinit_exec(const QString &app, const QStringList &args,
                        const QString &workdir, const QStringList &envs,
                        const QString &startup_id, bool wait, const QDBusMessage &msg)
{
    KLaunchRequest *request = new KLaunchRequest;
    request->arg_list = args;
    request->name = app;
    request->dbus_startup_type = KService::DBusNone;
    request->pid = 0;
    request->wait = wait;
#if HAVE_X11
    request->startup_id = startup_id.toLocal8Bit();
#endif
    request->envs = envs;
    request->cwd = workdir;
#if HAVE_X11
    if (!app.endsWith(QLatin1String("kbuildsycoca5"))) { // avoid stupid loop
        // Find service, if any - strip path if needed
        const QString desktopName = app.mid(app.lastIndexOf(QLatin1Char('/')) + 1);
        KService::Ptr service = KService::serviceByDesktopName(desktopName);
        if (service)
            send_service_startup_info(request, service,
                                      request->startup_id, envs);
        else { // no .desktop file, no startup info
            cancel_service_startup_info(request, request->startup_id, envs);
        }
    }
#endif
    msg.setDelayedReply(true);
    request->transaction = msg;
    queueRequest(request);
    return true;
}

void
KLauncher::queueRequest(KLaunchRequest *request)
{
    requestQueue.append(request);
    if (!bProcessingQueue) {
        bProcessingQueue = true;
        QTimer::singleShot(0, this, SLOT(slotDequeue()));
    }
}

void
KLauncher::slotDequeue()
{
    do {
        KLaunchRequest *request = requestQueue.takeFirst();
        // process request
        request->status = KLaunchRequest::Launching;
        requestStart(request);
        if (request->status != KLaunchRequest::Launching) {
            // Request handled.
            qCDebug(KLAUNCHER) << "Request handled already";
            requestDone(request);
            continue;
        }
    } while (!requestQueue.isEmpty());
    bProcessingQueue = false;
}

void
KLauncher::createArgs(KLaunchRequest *request, const KService::Ptr service,
                      const QList<QUrl> &urls)
{
    KIO::DesktopExecParser parser(*service, urls);
    const QStringList params = parser.resultingArguments();
    for (const QString &arg : params) {
        request->arg_list.append(arg);
    }

    const QString& path = service->workingDirectory();
    if (!path.isEmpty()) {
        request->cwd = path;
    } else if (!urls.isEmpty()) {
        const QUrl& url = urls.first();
        if (url.isLocalFile()) {
            request->cwd = url.adjusted(QUrl::RemoveFilename).toLocalFile();
        }
    }
}

///// IO-Slave functions

pid_t
KLauncher::requestHoldSlave(const QString &urlStr, const QString &app_socket)
{
    const QUrl url(urlStr);
    IdleSlave *slave = nullptr;
    for (IdleSlave *p : std::as_const(mSlaveList)) {
        if (p->onHold(url)) {
            slave = p;
            break;
        }
    }
    if (slave) {
        mSlaveList.removeAll(slave);
        slave->connect(app_socket);
        return slave->pid();
    }
    return 0;
}

pid_t
KLauncher::requestSlave(const QString &protocol,
                        const QString &host,
                        const QString &app_socket,
                        QString &error)
{
    IdleSlave *slave = nullptr;
    for (IdleSlave *p : std::as_const(mSlaveList)) {
        if (p->match(protocol, host, true)) {
            slave = p;
            break;
        }
    }
    if (!slave) {
        for (IdleSlave *p : std::as_const(mSlaveList)) {
            if (p->match(protocol, host, false)) {
                slave = p;
                break;
            }
        }
    }
    if (!slave) {
        for (IdleSlave *p : std::as_const(mSlaveList)) {
            if (p->match(protocol, QString(), false)) {
                slave = p;
                break;
            }
        }
    }
    if (slave) {
        mSlaveList.removeAll(slave);
        slave->connect(app_socket);
        return slave->pid();
    }

    QString slaveModule = KProtocolInfo::exec(protocol);
    if (slaveModule.isEmpty()) {
        error = i18n("Unknown protocol '%1'.\n", protocol);
        return 0;
    }
    KPluginLoader loader(slaveModule);
    QString slaveModulePath = loader.fileName();
    if (slaveModulePath.isEmpty()) {
        error = i18n("Could not find the '%1' plugin.\n", slaveModule);
        return 0;
    }

    QStringList arg_list;
#ifdef USE_KPROCESS_FOR_KIOSLAVES
    arg_list << slaveModulePath;
    arg_list << protocol;
    arg_list << mConnectionServer.address().toString();
    arg_list << app_socket;
#ifdef Q_OS_WIN
    QString name = QLatin1String("kioslave");
#else
    QString name = QFile::decodeName(KDE_INSTALL_FULL_LIBEXECDIR_KF5 "/kioslave");
#endif
#else
    QString arg1 = protocol;
    QString arg2 = mConnectionServer.address().toString();
    QString arg3 = app_socket;
    arg_list.append(arg1);
    arg_list.append(arg2);
    arg_list.append(arg3);
    QString name = slaveModulePath;
#endif

    qCDebug(KLAUNCHER) << "KLauncher: launching new slave" << name << "with protocol=" << protocol << "args=" << arg_list;

#ifdef Q_OS_UNIX
#ifndef USE_KPROCESS_FOR_KIOSLAVES
    // see comments where mSlaveDebug is set in KLauncher::KLauncher
    if (mSlaveDebug == protocol) {
        klauncher_header request_header;
        request_header.cmd = LAUNCHER_DEBUG_WAIT;
        request_header.arg_length = 0;
        kde_safe_write(kdeinitSocket, &request_header, sizeof(request_header));
    }
#endif
    if (mSlaveValgrind == protocol) {
        arg_list.prepend(name);
#ifndef USE_KPROCESS_FOR_KIOSLAVES // otherwise we've already done this
#ifdef Q_OS_WIN
        arg_list.prepend(QLatin1String("kioslave"));
#else
        arg_list.prepend(QFile::decodeName(KDE_INSTALL_FULL_LIBEXECDIR_KF5 "/kioslave"));
#endif
#endif
        name = QStringLiteral("valgrind");

        if (!mSlaveValgrindSkin.isEmpty()) {
            arg_list.prepend(QLatin1String("--tool=") + mSlaveValgrindSkin);
        } else {
            arg_list.prepend(QLatin1String("--tool=memcheck"));
        }
    }
#endif
    KLaunchRequest *request = new KLaunchRequest;
    request->name = name;
    request->arg_list =  arg_list;
    request->dbus_startup_type = KService::DBusNone;
    request->pid = 0;
    request->wait = false;
#if HAVE_X11
    request->startup_id = "0"; // krazy:exclude=doublequote_chars
#endif
    request->status = KLaunchRequest::Launching;
    requestStart(request);
    pid_t pid = request->pid;

//    qCDebug(KLAUNCHER) << "Slave launched, pid = " << pid;

    // We don't care about this request any longer....
    requestDone(request);
    if (!pid) {
        error = i18n("Error loading '%1'.", name);
    }
    return pid;
}

bool KLauncher::checkForHeldSlave(const QString &urlStr)
{
    QUrl url(urlStr);
    for (const IdleSlave *p : std::as_const(mSlaveList)) {
        if (p->onHold(url)) {
            return true;
        }
    }
    return false;
}

void
KLauncher::waitForSlave(int pid)
{
    Q_ASSERT(calledFromDBus());
    for (IdleSlave *slave : std::as_const(mSlaveList)) {
        if (slave->pid() == static_cast<pid_t>(pid)) {
            return;    // Already here.
        }
    }
    SlaveWaitRequest *waitRequest = new SlaveWaitRequest;
    setDelayedReply(true);
    waitRequest->transaction = message(); // from QDBusContext
    waitRequest->pid = static_cast<pid_t>(pid);
    mSlaveWaitRequest.append(waitRequest);
}

void
KLauncher::acceptSlave()
{
    IdleSlave *slave = new IdleSlave(this);
    mConnectionServer.setNextPendingConnection(slave->connection());
    mSlaveList.append(slave);
    connect(slave, SIGNAL(destroyed()), this, SLOT(slotSlaveGone()));
    connect(slave, SIGNAL(statusUpdate(IdleSlave*)),
            this, SLOT(slotSlaveStatus(IdleSlave*)));
    if (!mTimer.isActive()) {
        mTimer.start(1000 * 10);
    }
}

void
KLauncher::slotSlaveStatus(IdleSlave *slave)
{
    QMutableListIterator<SlaveWaitRequest *> it(mSlaveWaitRequest);
    while (it.hasNext()) {
        SlaveWaitRequest *waitRequest = it.next();
        if (waitRequest->pid == slave->pid()) {
            QDBusConnection::sessionBus().send(waitRequest->transaction.createReply());
            it.remove();
            delete waitRequest;
        }
    }

    if (slave->hasTempAuthorization()) {
        mSlaveList.removeAll(slave);
        slave->deleteLater();
    }
}

void
KLauncher::slotSlaveGone()
{
    IdleSlave *slave = (IdleSlave *) sender();
    mSlaveList.removeAll(slave);
    if ((mSlaveList.isEmpty()) && (mTimer.isActive())) {
        mTimer.stop();
    }
}

void
KLauncher::idleTimeout()
{
    bool keepOneFileSlave = true;
    QDateTime now = QDateTime::currentDateTime();
    for (IdleSlave *slave : std::as_const(mSlaveList)) {
        if ((slave->protocol() == QLatin1String("file")) && (keepOneFileSlave)) {
            keepOneFileSlave = false;
        } else if (slave->age(now) > SLAVE_MAX_IDLE) {
            // killing idle slave
            slave->deleteLater();
        }
    }
}

void KLauncher::reparseConfiguration()
{
    KProtocolManager::reparseConfiguration();
    for (IdleSlave *slave : std::as_const(mSlaveList)) {
        slave->reparseConfiguration();
    }
}

void
KLauncher::slotGotOutput()
{
#ifdef USE_KPROCESS_FOR_KIOSLAVES
    QProcess *p = static_cast<QProcess *>(sender());
    QByteArray _stdout = p->readAllStandardOutput();
    qCDebug(KLAUNCHER) << _stdout.data();
#endif
}

void
KLauncher::slotFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
#ifdef USE_KPROCESS_FOR_KIOSLAVES
    QProcess *p = static_cast<QProcess *>(sender());
    qCDebug(KLAUNCHER) << "process finished exitcode=" << exitCode << "exitStatus=" << exitStatus;

    for (KLaunchRequest *request : std::as_const(requestList)) {
        if (request->process == p) {
            qCDebug(KLAUNCHER) << "found KProcess, request done";
            if (exitCode == 0  && exitStatus == QProcess::NormalExit) {
                request->status = KLaunchRequest::Done;
            } else {
                request->status = KLaunchRequest::Error;
            }
            requestDone(request);
            request->process = 0;
        }
    }
    delete p;
#else
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
#endif
}

void KLauncher::terminate_kdeinit()
{
    qCDebug(KLAUNCHER);
#ifndef USE_KPROCESS_FOR_KIOSLAVES
    klauncher_header request_header;
    request_header.cmd = LAUNCHER_TERMINATE_KDEINIT;
    request_header.arg_length = 0;
    kde_safe_write(kdeinitSocket, &request_header, sizeof(request_header));
#endif
}

#if HAVE_XCB
KLauncher::XCBConnection KLauncher::getXCBConnection(const QByteArray &_displayName)
{
    const auto displayName = !_displayName.isEmpty() ? _displayName : qgetenv("DISPLAY");

    // If cached connection is same as request
    if (mCached && displayName == mCached.displayName) {
        // Check error, if so close it, otherwise it's still valid, reuse the cached one
        if (xcb_connection_has_error(mCached.conn)) {
            close();
        } else {
            return mCached;
        }
    }

    // At this point, the cached connection can't be reused, make a new connection
    XCBConnection conn;
    conn.conn = xcb_connect(displayName.constData(), &conn.screen);
    if (conn) {
        // check error first, if so return an empty one
        if (xcb_connection_has_error(conn.conn)) {
            xcb_disconnect(conn.conn);
            return XCBConnection();
        }

        // if it's valid, replace mCached with new connection
        conn.displayName = displayName;
        close();
        mCached = conn;
    }
    return conn;
}
#endif
