/*
    SPDX-FileCopyrightText: 2006, 2007 Thiago Macieira <thiago@kde.org>
    SPDX-FileCopyrightText: 2006-2008 David Faure <faure@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#ifndef KLAUNCHER_ADAPTOR_H_18181148166088
#define KLAUNCHER_ADAPTOR_H_18181148166088

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>

template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;

/*
 * Adaptor class for interface org.kde.KLauncher
 */
class KLauncherAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KLauncher")
public:
    KLauncherAdaptor(QObject *parent);
    ~KLauncherAdaptor() override;

public: // PROPERTIES
public Q_SLOTS: // METHODS
    void exec_blind(const QString &name, const QStringList &arg_list);
    void exec_blind(const QString &name, const QStringList &arg_list, const QStringList &envs, const QString &startup_id);
    int kdeinit_exec(const QString &app, const QStringList &args, const QStringList &env, const QString &startup_id, const QDBusMessage &msg, QString &dbusServiceName, QString &error, int &pid);
    int kdeinit_exec_wait(const QString &app, const QStringList &args, const QStringList &env, const QString &startup_id, const QDBusMessage &msg, QString &dbusServiceName, QString &error, int &pid);
    int kdeinit_exec_with_workdir(const QString &app, const QStringList &args, const QString &workdir, const QStringList &env, const QString &startup_id, const QDBusMessage &msg, QString &dbusServiceName, QString &error, int &pid);
    void reparseConfiguration();
    void setLaunchEnv(const QString &name, const QString &value);
    int start_service_by_desktop_name(const QString &serviceName, const QStringList &urls, const QStringList &envs, const QString &startup_id, bool blind, const QDBusMessage &msg, QString &dbusServiceName, QString &error, int &pid);
    int start_service_by_desktop_path(const QString &serviceName, const QStringList &urls, const QStringList &envs, const QString &startup_id, bool blind, const QDBusMessage &msg, QString &dbusServiceName, QString &error, int &pid);
    void terminate_kdeinit();
};

#endif
