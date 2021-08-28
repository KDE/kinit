/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999-2000 Waldo Bastian <bastian@kde.org>
    SPDX-FileCopyrightText: 1999 Mario Weilguni <mweilguni@sime.com>
    SPDX-FileCopyrightText: 2001 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2015, 2016 René J.V. Bertin <rjvbertin@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include <config-kdeinit.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#if HAVE_SYS_SELECT_H
#include <sys/select.h>     // Needed on some systems.
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include "proctitle.h"
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>

#include <QtCore/QLibrary>
#include <QtCore/QString>
#include <QtCore/QFile>
#include <QtCore/QDate>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegExp>
#include <QCoreApplication>
#include <QFont>
#include <KConfig>
#include <KLocalizedString>
#include <QDebug>
#include <QSaveFile>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <CoreFoundation/CFBundle.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <crt_externs.h> // for _NSGetArgc and friends
#include <mach-o/dyld.h> // for _NSGetExecutablePath

#include <kinit_version.h>

#include "klauncher_cmds.h"

#include <QStandardPaths>

#include "kinit.h"
#include "kinit_mac.h"

extern char **environ;

static int show_message_box( const QString &title, const QString &message )
{
    int (*_CGSDefaultConnection_p)();
    _CGSDefaultConnection_p = (int (*)()) dlsym(RTLD_DEFAULT, "_CGSDefaultConnection");
    if (!_CGSDefaultConnection_p) {
        NSLog(@"qt_fatal_message_box(%@,%@): couldn't load _CGSDefaultConnection: %s",
            [NSString stringWithCString:message.toUtf8().constData() encoding:NSUTF8StringEncoding],
            [NSString stringWithCString:title.toUtf8().constData() encoding:NSUTF8StringEncoding], dlerror());
    }
    if (_CGSDefaultConnection_p && (*_CGSDefaultConnection_p)()) {
        @autoreleasepool {
            NSAlert* alert = [[[NSAlert alloc] init] autorelease];
            NSString *msg, *tit;
            @synchronized([NSAlert class]){
                [alert addButtonWithTitle:@"OK"];
                [alert setAlertStyle:NSCriticalAlertStyle];
                [alert setMessageText:@"" ];
                if( !(msg = [NSString stringWithCString:message.toUtf8().constData() encoding:NSUTF8StringEncoding]) ){
                    msg = [NSString stringWithCString:message.toLatin1().constData() encoding:NSASCIIStringEncoding];
                }
                if( !(tit = [NSString stringWithCString:title.toUtf8().constData() encoding:NSUTF8StringEncoding]) ){
                    tit = [NSString stringWithCString:title.toLatin1().constData() encoding:NSASCIIStringEncoding];
                }
                if( msg ){
                    [alert setInformativeText:msg];
                }
                else{
                    NSLog( @"msg=%@ tit=%@", msg, tit );
                }
                [[alert window] setTitle:tit];
                return NSAlertSecondButtonReturn == [alert runModal];
            }
        }
    }
    return 0;
}

static void exitWithErrorMsg(const QString &errorMsg, bool doExit)
{
    fprintf(stderr, "%s\n", errorMsg.toLocal8Bit().data());
    QByteArray utf8ErrorMsg = errorMsg.toUtf8();
    d.result = 3; // Error with msg
    write(d.fd[1], &d.result, 1);
    int l = utf8ErrorMsg.length();
    write(d.fd[1], &l, sizeof(int));
    write(d.fd[1], utf8ErrorMsg.data(), l);
    close(d.fd[1]);
    show_message_box(QStringLiteral("kdeinit5"), errorMsg);
    if (doExit) {
        exit(255);
    }
}

pid_t launch(int argc, const char *_name, const char *args,
                    const char *cwd, int envc, const char *envs,
                    bool reset_env, const char *tty, bool avoid_loops,
                    const char *startup_id_str)  // krazy:exclude=doublequote_chars
{
    QString bin, libpath;
    QByteArray name;
    QByteArray execpath;

    if (_name[0] != '/') {
        name = _name;
        bin = QFile::decodeName(name);
        execpath = execpath_avoid_loops(name, envc, envs, avoid_loops);
    } else {
        name = _name;
        bin = QFile::decodeName(name);
        name = name.mid(name.lastIndexOf('/') + 1);

        // FIXME: this .so extension stuff is very Linux-specific
        // a .so extension can occur on OS X, but the typical extension is .dylib
        if (bin.endsWith(QStringLiteral(".so")) || bin.endsWith(QStringLiteral(".dylib"))) {
            libpath = bin;
        } else {
            execpath = _name;
        }
    }
#ifndef NDEBUG
    fprintf(stderr, "kdeinit5: preparing to launch '%s'\n", libpath.isEmpty()
            ? execpath.constData() : libpath.toUtf8().constData());
#endif
    if (!args) {
        argc = 1;
    }

    // do certain checks before forking
    if (execpath.isEmpty()) {
        QString errorMsg = i18n("Could not find '%1' executable.", QFile::decodeName(_name));
        exitWithErrorMsg(errorMsg, false);
        d.result = 3;
        d.fork = 0;
        return d.fork;
    } else if (!libpath.isEmpty()) {
        QString errorMsg = i18n("Launching library '%1' is not supported on OS X.", libpath);
        exitWithErrorMsg(errorMsg, false);
        d.result = 3;
        d.fork = 0;
        return d.fork;
    }

    if (0 > pipe(d.fd)) {
        perror("kdeinit5: pipe() failed");
        d.result = 3;
        d.errorMsg = i18n("Unable to start new process.\n"
                          "The system may have reached the maximum number of open files possible or the maximum number of open files that you are allowed to use has been reached.").toUtf8();
        d.fork = 0;
        return d.fork;
    }

    // find out this path before forking, doing it afterwards
    // crashes on some platforms, notably OSX
    const QString bundlepath = QStandardPaths::findExecutable(QFile::decodeName(execpath));

    const QString argvexe = QStandardPaths::findExecutable(QLatin1String(_name));

    d.errorMsg = 0;
    d.fork = fork();
    switch (d.fork) {
    case -1:
        perror("kdeinit5: fork() failed");
        d.result = 3;
        d.errorMsg = i18n("Unable to create new process.\n"
                          "The system may have reached the maximum number of processes possible or the maximum number of processes that you are allowed to use has been reached.").toUtf8();
        close(d.fd[0]);
        close(d.fd[1]);
        d.fork = 0;
        break;
    case 0: {
        /** Child **/
        close(d.fd[0]);
        close_fds();
        reset_oom_protect();

        // Try to chdir, either to the requested directory or to the user's document path by default.
        // We ignore errors - if you write a desktop file with Exec=foo and Path=/doesnotexist,
        // we still want to execute `foo` even if the chdir() failed.
        if (cwd && *cwd) {
            (void)chdir(cwd);
        }

        if (reset_env) { // KWRAPPER/SHELL

            QList<QByteArray> unset_envs;
            for (int tmp_env_count = 0;
                    environ[tmp_env_count];
                    tmp_env_count++) {
                unset_envs.append(environ[ tmp_env_count ]);
            }
            for (const QByteArray &tmp : std::as_const(unset_envs)) {
                int pos = tmp.indexOf('=');
                if (pos >= 0) {
                    unsetenv(tmp.left(pos).constData());
                }
            }
        }

        for (int i = 0;  i < envc; i++) {
            putenv((char *)envs);
            while (*envs != 0) {
                envs++;
            }
            envs++;
        }

        {
            QByteArray procTitle;
            // launching an executable: can do an exec directly
            d.argv = (char **) malloc(sizeof(char *) * (argc + 1));
            if (!argvexe.isEmpty()) {
                QByteArray cstr = argvexe.toLocal8Bit();
                d.argv[0] = strdup(cstr.data());
            } else {
                d.argv[0] = (char *) _name;
            }
            for (int i = 1;  i < argc; i++) {
                d.argv[i] = (char *) args;
                procTitle += ' ';
                procTitle += (char *) args;
                while (*args != 0) {
                    args++;
                }
                args++;
            }
            d.argv[argc] = 0;

#ifndef SKIP_PROCTITLE
            /** Give the process a new name **/
            proctitle_set("%s%s", name.data(), procTitle.data() ? procTitle.data() : "");
#endif
        }

        if (!execpath.isEmpty()) {
            // we're launching an executable; even after a fork and being exec'ed, that
            // executable is allowed to use non-POSIX APIs.
            d.result = 2; // Try execing
            write(d.fd[1], &d.result, 1);

            // We set the close on exec flag.
            // Closing of d.fd[1] indicates that the execvp succeeded!
            fcntl(d.fd[1], F_SETFD, FD_CLOEXEC);

            setup_tty(tty);

            QByteArray executable = execpath;
            if (!bundlepath.isEmpty()) {
                executable = QFile::encodeName(bundlepath);
            }
            // TODO: we probably want to use [[NSProcessInfo processInfo] setProcessName:NSString*] here to
            // make the new app show up under its own name in non-POSIX process listings.

            if (!executable.isEmpty()) {
#ifndef NDEBUG
                qDebug() << "execvp" << executable;
                for (int i = 0 ; i < argc ; ++i) {
                    qDebug() << "arg #" << i << "=" << d.argv[i];
                }
#endif
                // attempt to the correct application name
                QFileInfo fi(QString::fromUtf8(executable));
                [[NSProcessInfo processInfo] setProcessName:(NSString*)fi.baseName().toCFString()];
                qApp->setApplicationName(fi.baseName());
                execvp(executable.constData(), d.argv);
            }

            d.result = 1; // Error
            write(d.fd[1], &d.result, 1);
            close(d.fd[1]);
            exit(255);
        }

        break;
    }
    default:
        /** Parent **/
        close(d.fd[1]);
        bool exec = false;
        for (;;) {
            d.n = read(d.fd[0], &d.result, 1);
            if (d.n == 1) {
                if (d.result == 2) {
#ifndef NDEBUG
                    //fprintf(stderr, "kdeinit5: no kdeinit module, trying exec....\n");
#endif
                    exec = true;
                    continue;
                }
                if (d.result == 3) {
                    int l = 0;
                    d.n = read(d.fd[0], &l, sizeof(int));
                    if (d.n == sizeof(int)) {
                        QByteArray tmp;
                        tmp.resize(l + 1);
                        d.n = read(d.fd[0], tmp.data(), l);
                        tmp[l] = 0;
                        if (d.n == l) {
                            d.errorMsg = tmp;
                        }
                    }
                }
                // Finished
                break;
            }
            if (d.n == -1) {
                if (errno == ECHILD) {  // a child died.
                    continue;
                }
                if (errno == EINTR || errno == EAGAIN) { // interrupted or more to read
                    continue;
                }
            }
            if (d.n == 0) {
                if (exec) {
                    d.result = 0;
                } else {
                    fprintf(stderr, "kdeinit5: (%s %s) Pipe closed unexpectedly", name.constData(), execpath.constData());
                    perror("kdeinit5: Pipe closed unexpectedly");
                    d.result = 1; // Error
                }
                break;
            }
            perror("kdeinit5: Error reading from pipe");
            d.result = 1; // Error
            break;
        }
        close(d.fd[0]);
    }
    return d.fork;
}

/**
 Calling CoreFoundation and other non-POSIX APIs (which is unavoidable) has always caused issues
 with fork/exec on Mac OS X, but as of 10.5 is explicitly disallowed with an exception. As a
 result, in the case where we would normally fork and then dlopen code, or continue
 to run other code, we must now fork-and-exec, and even then we need to use a helper (proxy)
 to launch the actual application we wish to launch, a proxy that will only have used POSIX APIs.
 This probably cancels the whole idea of preloading libraries, but it is as it is.
 Note that this function is called only when kdeinit5 is forking itself,
 in order to disappear into the background.
*/
// Copied from kkernel_mac.cpp
void mac_fork_and_reexec_self()
{
    int argc = *_NSGetArgc();
    char **argv = *_NSGetArgv();
    char *newargv[argc + 2];
    char progname[PATH_MAX];
    uint32_t buflen = PATH_MAX;
    _NSGetExecutablePath(progname, &buflen);

    for (int i = 0; i < argc; i++) {
        newargv[i] = argv[i];
    }

    newargv[argc] = (char*)"--nofork";
    newargv[argc + 1] = 0;

    int x_fork_result = fork();
    switch (x_fork_result) {

    case -1:
#ifndef NDEBUG
        fprintf(stderr, "Mac OS X workaround fork() failed!\n");
#endif
        ::_exit(255);
        break;

    case 0:
        // Child
        execvp(progname, newargv);
        break;

    default:
        // Parent
        _exit(0);
        break;

    }
}
