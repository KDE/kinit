/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999-2000 Waldo Bastian <bastian@kde.org>
    SPDX-FileCopyrightText: 1999 Mario Weilguni <mweilguni@sime.com>
    SPDX-FileCopyrightText: 2001 Lubos Lunak <l.lunak@kde.org>

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
#include <cerrno>
#include <fcntl.h>
#include "proctitle.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>

#include <QLibrary>
#include <QString>
#include <QFile>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegExp>
#include <QFont>
#include <KCrash>
#include <KConfig>
#include <KLocalizedString>
#include <QDebug>
#include <QSaveFile>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif
#endif

#include <kinit_version.h>

#include "klauncher_cmds.h"

#if HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <fixx11h.h>
#endif

#if HAVE_XCB
#include <NETWM>
#include <xcb/xcb.h>
#endif

#include <kstartupinfo.h>

#include <QStandardPaths>

#include "kinit.h"
#ifdef Q_OS_OSX
#include "kinit_mac.h"
#endif

#ifdef Q_OS_UNIX
//TODO: make sure what libraries we want here...
static const char *extra_libs[] = {
#ifdef Q_OS_OSX
    "libKF5KIOCore.5.dylib",
    "libKF5Parts.5.dylib",
    "libKF5Plasma.5.dylib"
#else
    "libKF5KIOCore.so.5",
    "libKF5Parts.so.5",
    "libKF5Plasma.so.5"
#endif
};
#endif

// #define SKIP_PROCTITLE 1

namespace KCrash
{
extern KCRASH_EXPORT bool loadedByKdeinit;
}

extern char **environ;

#if HAVE_X11
static int X11fd = -1;
static Display *X11display = nullptr;
static int X11_startup_notify_fd = -1;
#endif
#if HAVE_XCB
static xcb_connection_t *s_startup_notify_connection = nullptr;
static int s_startup_notify_screen = 0;
#endif
// Finds size of sun_path without allocating a sockaddr_un to do it.
// Assumes sun_path is at end of sockaddr_un though
#define MAX_SOCK_FILE (sizeof(struct sockaddr_un) - offsetof(struct sockaddr_un,sun_path))
static char sock_file[MAX_SOCK_FILE];

// Can't use QGuiApplication::platformName() here, there is no app instance.
#if HAVE_X11 || HAVE_XCB
static const char* displayEnvVarName_c()
{
    return "DISPLAY";
}
static inline QByteArray displayEnvVarName()
{
    return QByteArray::fromRawData(displayEnvVarName_c(), strlen(displayEnvVarName_c()));
}
#endif

static struct child *children;

#if HAVE_X11
extern "C" {
    int kdeinit_xio_errhandler(Display *);
    int kdeinit_x_errhandler(Display *, XErrorEvent *err);
}
#endif

static int oom_pipe = -1;

/*
 * Clean up the file descriptor table by closing all file descriptors
 * that are still open.
 *
 * This function is called very early in the main() function, so that
 * we don't leak anything that was leaked to us.
 */
static void cleanup_fds()
{
#if HAVE_CLOSE_RANGE
    if (oom_pipe >= 3) {
        if (oom_pipe > 3) {
            close_range(3, oom_pipe - 1, 0);
        }
        close_range(oom_pipe + 1, ~0, 0);
    } else {
        close_range(3, ~0, 0);
    }
#else
    int maxfd = FD_SETSIZE;
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        maxfd = rl.rlim_cur;
    }
    for (int fd = 3; fd < maxfd; ++fd) {
        if (fd != oom_pipe)
            close(fd);
    }
#endif
}

/*
 * Close fd's which are only useful for the parent process.
 * Restore default signal handlers.
 */
void close_fds()
{
    while (struct child *child = children) {
        close(child->sock);
        children = child->next;
        free(child);
    }

    if (d.deadpipe[0] != -1) {
        close(d.deadpipe[0]);
        d.deadpipe[0] = -1;
    }

    if (d.deadpipe[1] != -1) {
        close(d.deadpipe[1]);
        d.deadpipe[1] = -1;
    }

    if (d.initpipe[0] != -1) {
        close(d.initpipe[0]);
        d.initpipe[0] = -1;
    }

    if (d.initpipe[1] != -1) {
        close(d.initpipe[1]);
        d.initpipe[1] = -1;
    }

    if (d.launcher[0] != -1) {
        close(d.launcher[0]);
        d.launcher[0] = -1;
    }
    if (d.wrapper != -1) {
        close(d.wrapper);
        d.wrapper = -1;
    }
    if (d.accepted_fd != -1) {
        close(d.accepted_fd);
        d.accepted_fd = -1;
    }
#if HAVE_X11
    if (X11fd >= 0) {
        close(X11fd);
        X11fd = -1;
    }
    if (X11_startup_notify_fd >= 0 && X11_startup_notify_fd != X11fd) {
        close(X11_startup_notify_fd);
        X11_startup_notify_fd = -1;
    }
#endif

    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

/* Notify wrapper program that the child it started has finished. */
static void child_died(pid_t exit_pid, int exit_status)
{
    struct child *child, **childptr = &children;

    while ((child = *childptr)) {
        if (child->pid == exit_pid) {
            // Send a message with the return value of the child on the control socket
            klauncher_header request_header;
            long request_data[2];
            request_header.cmd = LAUNCHER_CHILD_DIED;
            request_header.arg_length = sizeof(long) * 2;
            request_data[0] = exit_pid;
            request_data[1] = exit_status;
            write(child->sock, &request_header, sizeof(request_header));
            write(child->sock, request_data, request_header.arg_length);
            close(child->sock);

            *childptr = child->next;
            free(child);
            return;
        }

        childptr = &child->next;
    }
}

void setup_tty(const char *tty)
{
    if (tty == nullptr || *tty == '\0') {
        return;
    }
    QFile f(QFile::decodeName(tty));
    f.open(QFileDevice::WriteOnly);
    int fd = f.handle();

    if (fd < 0) {
        perror("kdeinit5: could not open() tty");
        return;
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("kdeinit5: could not dup2() stdout tty");
    }
    if (dup2(fd, STDERR_FILENO) < 0) {
        perror("kdeinit5: could not dup2() stderr tty");
    }
    close(fd);
}

// var has to be e.g. "DISPLAY=", i.e. with =
const char *get_env_var(const char *var, int envc, const char *envs)
{
    if (envc > 0) {
        // get the var from envs
        const char *env_l = envs;
        int ln = strlen(var);
        for (int i = 0;  i < envc; i++) {
            if (strncmp(env_l, var, ln) == 0) {
                return env_l + ln;
            }
            while (*env_l != 0) {
                env_l++;
            }
            env_l++;
        }
    }
    return nullptr;
}

#if HAVE_XCB
static void init_startup_info(KStartupInfoId &id, const QByteArray &bin,
                              int envc, const char *envs)
{
    QByteArray envname = displayEnvVarName() + "=";
    const char *dpy = get_env_var(envname.constData(), envc, envs);
    // this may be called in a child, so it can't use display open using X11display
    // also needed for multihead
    s_startup_notify_connection = xcb_connect(dpy, &s_startup_notify_screen);
    if (!s_startup_notify_connection) {
        return;
    }
    if (xcb_connection_has_error(s_startup_notify_connection)) {
        xcb_disconnect(s_startup_notify_connection);
        s_startup_notify_connection = nullptr;
        return;
    }
    X11_startup_notify_fd = xcb_get_file_descriptor(s_startup_notify_connection);
    NETRootInfo rootInfo(s_startup_notify_connection, NET::CurrentDesktop);
    KStartupInfoData data;
    data.setDesktop(rootInfo.currentDesktop(true));
    data.setBin(QFile::decodeName(bin));
    KStartupInfo::sendChangeXcb(s_startup_notify_connection, s_startup_notify_screen, id, data);
    xcb_flush(s_startup_notify_connection);
}

static void complete_startup_info(KStartupInfoId &id, pid_t pid)
{
    if (!s_startup_notify_connection) {
        return;
    }
    if (pid == 0) { // failure
        KStartupInfo::sendFinishXcb(s_startup_notify_connection, s_startup_notify_screen, id);
    } else {
        KStartupInfoData data;
        data.addPid(pid);
        data.setHostname();
        KStartupInfo::sendChangeXcb(s_startup_notify_connection, s_startup_notify_screen, id, data);
    }
    xcb_disconnect(s_startup_notify_connection);
    s_startup_notify_connection = nullptr;
    s_startup_notify_screen = 0;
    X11_startup_notify_fd = -1;
}
#endif

QByteArray execpath_avoid_loops(const QByteArray &exec, int envc, const char *envs, bool avoid_loops)
{
    QStringList paths;
    const QRegExp pathSepRegExp(QStringLiteral("[:\b]"));
    if (envc > 0) { // use the passed environment
        const char *path = get_env_var("PATH=", envc, envs);
        if (path != nullptr) {
            paths = QFile::decodeName(path).split(pathSepRegExp);
        }
    } else {
        paths = QString::fromLocal8Bit(qgetenv("PATH")).split(pathSepRegExp, Qt::KeepEmptyParts);
        paths.prepend(QFile::decodeName(KDE_INSTALL_FULL_LIBEXECDIR_KF5));
    }
    QString execpath = QStandardPaths::findExecutable(QFile::decodeName(exec), paths);
    if (avoid_loops && !execpath.isEmpty()) {
        const int pos = execpath.lastIndexOf(QLatin1Char('/'));
        const QString bin_path = execpath.left(pos);
        for (QStringList::Iterator it = paths.begin();
                it != paths.end();
                ++it) {
            if (*it == bin_path || *it == bin_path + QLatin1Char('/')) {
                paths.erase(it);
                break; // -->
            }
        }
        execpath = QStandardPaths::findExecutable(QFile::decodeName(exec), paths);
    }
    return QFile::encodeName(execpath);
}

#if KDEINIT_OOM_PROTECT
static void oom_protect_sighandler(int)
{
}

void reset_oom_protect()
{
    if (oom_pipe <= 0) {
        return;
    }
    struct sigaction act, oldact;
    act.sa_handler = oom_protect_sighandler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGUSR1, &act, &oldact);
    sigset_t sigs, oldsigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGUSR1);
    sigprocmask(SIG_BLOCK, &sigs, &oldsigs);
    pid_t pid = getpid();
    if (write(oom_pipe, &pid, sizeof(pid_t)) > 0) {
        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;
        do {
            if (sigtimedwait(&sigs, nullptr, &timeout) < 0) { // wait for the signal to come
                if (errno == EINTR) {
                    // other signal
                    continue;
                } else if (errno == EAGAIN) {
                    fprintf(stderr, "Timed out during reset OOM protection: %d\n", pid);
                } else {
                    fprintf(stderr, "Error during reset OOM protection: %d\n", pid);
                }
            }
            break;
        } while (true);
    } else {
#ifndef NDEBUG
        fprintf(stderr, "Failed to reset OOM protection: %d\n", pid);
#endif
    }
    sigprocmask(SIG_SETMASK, &oldsigs, nullptr);
    sigaction(SIGUSR1, &oldact, nullptr);
    close(oom_pipe);
    oom_pipe = -1;
}
#else
void reset_oom_protect()
{
}
#endif

#ifndef Q_OS_OSX

static pid_t launch(int argc, const char *_name, const char *args,
                    const char *cwd = nullptr, int envc = 0, const char *envs = nullptr,
                    bool reset_env = false,
                    const char *tty = nullptr, bool avoid_loops = false,
                    const char *startup_id_str = "0")  // krazy:exclude=doublequote_chars
{
    QByteArray name = _name;
    QByteArray execpath;

    if (name[0] != '/') {
        execpath = execpath_avoid_loops(name, envc, envs, avoid_loops);
    } else {
        name = name.mid(name.lastIndexOf('/') + 1);
        execpath = _name;
    }
#ifndef NDEBUG
    fprintf(stderr, "kdeinit5: preparing to launch '%s'\n", execpath.constData());
#endif
    if (!args) {
        argc = 1;
    }

    if (0 > pipe(d.fd)) {
        perror("kdeinit5: pipe() failed");
        d.result = 3;
        d.errorMsg = i18n("Unable to start new process.\n"
                          "The system may have reached the maximum number of open files possible or the maximum number of open files that you are allowed to use has been reached.").toUtf8();
        d.fork = 0;
        return d.fork;
    }

#if HAVE_XCB
    KStartupInfoId startup_id;
    startup_id.initId(startup_id_str);
    if (!startup_id.isNull()) {
        init_startup_info(startup_id, name, envc, envs);
    }
#endif
    // find out this path before forking, doing it afterwards
    // crashes on some platforms, notably OSX

    d.errorMsg = nullptr;
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

#if HAVE_X11
        if (startup_id.isNull()) {
            KStartupInfo::resetStartupEnv();
        } else {
            startup_id.setupStartupEnv();
        }
#endif
        {
            int r;
            QByteArray procTitle;
            d.argv = (char **) malloc(sizeof(char *) * (argc + 1));
            d.argv[0] = (char *) _name;
            for (int i = 1;  i < argc; i++) {
                d.argv[i] = (char *) args;
                procTitle += ' ';
                procTitle += (char *) args;
                while (*args != 0) {
                    args++;
                }
                args++;
            }
            d.argv[argc] = nullptr;

#ifndef SKIP_PROCTITLE
            // Give the process a new name
#ifdef Q_OS_LINUX
            // set the process name, so that killall works like intended
            r = prctl(PR_SET_NAME, (unsigned long) name.data(), 0, 0, 0);
            if (r == 0) {
                proctitle_set("-%s [kdeinit5]%s", name.data(), procTitle.data() ? procTitle.data() : "");
            } else {
                proctitle_set("%s%s", name.data(), procTitle.data() ? procTitle.data() : "");
            }
#else
            proctitle_set("%s%s", name.data(), procTitle.data() ? procTitle.data() : "");
#endif
#endif
        }

        d.result = 2; // Try execing
        write(d.fd[1], &d.result, 1);

        // We set the close on exec flag.
        // Closing of d.fd[1] indicates that the execvp succeeded!
        fcntl(d.fd[1], F_SETFD, FD_CLOEXEC);

        setup_tty(tty);

        QByteArray executable = execpath;
        if (!executable.isEmpty()) {
            execvp(executable.constData(), d.argv);
        }

        d.result = 1; // Error
        write(d.fd[1], &d.result, 1);
        close(d.fd[1]);
        exit(255);
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
#if HAVE_XCB
    if (!startup_id.isNull()) {
        if (d.fork && d.result == 0) { // launched successfully
            complete_startup_info(startup_id, d.fork);
        } else { // failure, cancel ASN
            complete_startup_info(startup_id, 0);
        }
    }
#endif
    return d.fork;
}
#endif // !Q_OS_OSX

extern "C" {

    static void sig_child_handler(int)
    {
        /*
         * Write into the pipe of death.
         * This way we are sure that we return from the select()
         *
         * A signal itself causes select to return as well, but
         * this creates a race-condition in case the signal arrives
         * just before we enter the select.
         */
        char c = 0;
        write(d.deadpipe[1], &c, 1);
    }

}

static void init_signals()
{
    struct sigaction act;
    long options;

    if (pipe(d.deadpipe) != 0) {
        perror("kdeinit5: Aborting. Can not create pipe");
        exit(255);
    }

    options = fcntl(d.deadpipe[0], F_GETFL);
    if (options == -1) {
        perror("kdeinit5: Aborting. Can not make pipe non-blocking");
        exit(255);
    }

    if (fcntl(d.deadpipe[0], F_SETFL, options | O_NONBLOCK) == -1) {
        perror("kdeinit5: Aborting. Can not make pipe non-blocking");
        exit(255);
    }

    /*
     * A SIGCHLD handler is installed which sends a byte into the
     * pipe of death. This is to ensure that a dying child causes
     * an exit from select().
     */
    act.sa_handler = sig_child_handler;
    sigemptyset(&(act.sa_mask));
    sigaddset(&(act.sa_mask), SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &(act.sa_mask), nullptr);
    act.sa_flags = SA_NOCLDSTOP;

    // CC: take care of SunOS which automatically restarts interrupted system
    // calls (and thus does not have SA_RESTART)

#ifdef SA_RESTART
    act.sa_flags |= SA_RESTART;
#endif
    sigaction(SIGCHLD, &act, nullptr);

    act.sa_handler = SIG_IGN;
    sigemptyset(&(act.sa_mask));
    sigaddset(&(act.sa_mask), SIGPIPE);
    sigprocmask(SIG_UNBLOCK, &(act.sa_mask), nullptr);
    act.sa_flags = 0;
    sigaction(SIGPIPE, &act, nullptr);
}

static void init_kdeinit_socket()
{
    struct sockaddr_un sa;
    kde_socklen_t socklen;
    long options;
    const QByteArray home_dir = qgetenv("HOME");
    int max_tries = 10;
    if (home_dir.isEmpty()) {
        fprintf(stderr, "kdeinit5: Aborting. $HOME not set!");
        exit(255);
    }
    if (chdir(home_dir.constData()) != 0) {
        fprintf(stderr, "kdeinit5: Aborting. Couldn't enter '%s'!", home_dir.constData());
        exit(255);
    }

    {
        const QByteArray path = home_dir;
        const QByteArray readOnly = qgetenv("KDE_HOME_READONLY");
        if (access(path.data(), R_OK | W_OK)) {
            if (errno == ENOENT) {
                fprintf(stderr, "kdeinit5: Aborting. $HOME directory (%s) does not exist.\n", path.data());
                exit(255);
            } else if (readOnly.isEmpty()) {
                fprintf(stderr, "kdeinit5: Aborting. No write access to $HOME directory (%s).\n", path.data());
                exit(255);
            }
        }
    }

    /** Test if socket file is already present
     *  note that access() resolves symlinks, and so we check the actual
     *  socket file if it exists
     */
    if (access(sock_file, W_OK) == 0) {
        int s;
        struct sockaddr_un server;

//     fprintf(stderr, "kdeinit5: Warning, socket_file already exists!\n");
        // create the socket stream
        s = socket(PF_UNIX, SOCK_STREAM, 0);
        if (s < 0) {
            perror("socket() failed");
            exit(255);
        }
        server.sun_family = AF_UNIX;
        qstrncpy(server.sun_path, sock_file, sizeof(server.sun_path));
        socklen = sizeof(server);

        if (connect(s, (struct sockaddr *)&server, socklen) == 0) {
            fprintf(stderr, "kdeinit5: Shutting down running client.\n");
            klauncher_header request_header;
            request_header.cmd = LAUNCHER_TERMINATE_KDEINIT;
            request_header.arg_length = 0;
            write(s, &request_header, sizeof(request_header));
            sleep(1); // Give it some time
        }
        close(s);
    }

    // Delete any stale socket file (and symlink)
    unlink(sock_file);

    // Create socket
    d.wrapper = socket(PF_UNIX, SOCK_STREAM, 0);
    if (d.wrapper < 0) {
        perror("kdeinit5: Aborting. socket() failed");
        exit(255);
    }

    options = fcntl(d.wrapper, F_GETFL);
    if (options == -1) {
        perror("kdeinit5: Aborting. Can not make socket non-blocking");
        close(d.wrapper);
        exit(255);
    }

    if (fcntl(d.wrapper, F_SETFL, options | O_NONBLOCK) == -1) {
        perror("kdeinit5: Aborting. Can not make socket non-blocking");
        close(d.wrapper);
        exit(255);
    }

    while (1) {
        // bind it
        socklen = sizeof(sa);
        memset(&sa, 0, socklen);
        sa.sun_family = AF_UNIX;
        qstrncpy(sa.sun_path, sock_file, sizeof(sa.sun_path));
        if (bind(d.wrapper, (struct sockaddr *)&sa, socklen) != 0) {
            if (max_tries == 0) {
                perror("kdeinit5: Aborting. bind() failed");
                fprintf(stderr, "Could not bind to socket '%s'\n", sock_file);
                close(d.wrapper);
                exit(255);
            }
            max_tries--;
        } else {
            break;
        }
    }

    // set permissions
    if (chmod(sock_file, 0600) != 0) {
        perror("kdeinit5: Aborting. Can not set permissions on socket");
        fprintf(stderr, "Wrong permissions of socket '%s'\n", sock_file);
        unlink(sock_file);
        close(d.wrapper);
        exit(255);
    }

    if (listen(d.wrapper, SOMAXCONN) < 0) {
        perror("kdeinit5: Aborting. listen() failed");
        unlink(sock_file);
        close(d.wrapper);
        exit(255);
    }
}

/*
 * Read 'len' bytes from 'sock' into buffer.
 * returns 0 on success, -1 on failure.
 */
static int read_socket(int sock, char *buffer, int len)
{
    ssize_t result;
    int bytes_left = len;
    while (bytes_left > 0) {
        result = read(sock, buffer, bytes_left);
        if (result > 0) {
            buffer += result;
            bytes_left -= result;
        } else if (result == 0) {
            return -1;
        } else if ((result == -1) && (errno != EINTR) && (errno != EAGAIN)) {
            return -1;
        }
    }
    return 0;
}

static void start_klauncher()
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, d.launcher) < 0) {
        perror("kdeinit5: socketpair() failed");
        exit(255);
    }
    char args[32];
    strcpy(args, "--fd=");
    sprintf(args + 5, "%d", d.launcher[1]);
    d.launcher_pid = launch(2, KDE_INSTALL_FULL_LIBEXECDIR_KF5 "/klauncher", args);
    close(d.launcher[1]);
#ifndef NDEBUG
    fprintf(stderr, "kdeinit5: Launched KLauncher, pid = %ld, result = %d\n",
            (long) d.launcher_pid, d.result);
#endif
}

static void launcher_died()
{
    if (!d.launcher_ok) {
        // This is bad.
        fprintf(stderr, "kdeinit5: Communication error with launcher. Exiting!\n");
        ::exit(255);
        return;
    }

    // KLauncher died... restart
#ifndef NDEBUG
    fprintf(stderr, "kdeinit5: KLauncher died unexpectedly.\n");
#endif
    // Make sure it's really dead.
    if (d.launcher_pid) {
        kill(d.launcher_pid, SIGKILL);
        sleep(1); // Give it some time
    }

    d.launcher_ok = false;
    d.launcher_pid = 0;
    close(d.launcher[0]);
    d.launcher[0] = -1;

    start_klauncher();
}

static bool handle_launcher_request(int sock, const char *who)
{
    (void)who; // for NDEBUG

    klauncher_header request_header;
    char *request_data = nullptr;
    int result = read_socket(sock, (char *) &request_header, sizeof(request_header));
    if (result != 0) {
        return false;
    }

    if (request_header.arg_length != 0) {
        request_data = (char *) malloc(request_header.arg_length + 1);
        request_data[request_header.arg_length] = '\0';

        result = read_socket(sock, request_data, request_header.arg_length);
        if (result != 0) {
            free(request_data);
            return false;
        }
    }

    //qDebug() << "Got cmd" << request_header.cmd << commandToString(request_header.cmd);
    if (request_header.cmd == LAUNCHER_OK) {
        d.launcher_ok = true;
    } else if (request_header.arg_length &&
               ((request_header.cmd == LAUNCHER_EXEC) ||
                (request_header.cmd == LAUNCHER_EXT_EXEC) ||
                (request_header.cmd == LAUNCHER_SHELL) ||
                (request_header.cmd == LAUNCHER_KWRAPPER) ||
                (request_header.cmd == LAUNCHER_EXEC_NEW))) {
        pid_t pid;
        klauncher_header response_header;
        long response_data;
        long l;
        memcpy(&l, request_data, sizeof(long));
        int argc = l;
        const char *name = request_data + sizeof(long);
        const char *args = name + strlen(name) + 1;
        const char *cwd = nullptr;
        int envc = 0;
        const char *envs = nullptr;
        const char *tty = nullptr;
        int avoid_loops = 0;
        const char *startup_id_str = "0"; // krazy:exclude=doublequote_chars

#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: Got %s '%s' from %s.\n",
                commandToString(request_header.cmd),
                name, who);
#endif

        const char *arg_n = args;
        for (int i = 1; i < argc; i++) {
            arg_n = arg_n + strlen(arg_n) + 1;
        }

        if (request_header.cmd == LAUNCHER_SHELL || request_header.cmd == LAUNCHER_KWRAPPER) {
            // Shell or kwrapper
            cwd = arg_n; arg_n += strlen(cwd) + 1;
        }
        if (request_header.cmd == LAUNCHER_SHELL || request_header.cmd == LAUNCHER_KWRAPPER
                || request_header.cmd == LAUNCHER_EXT_EXEC || request_header.cmd == LAUNCHER_EXEC_NEW) {
            memcpy(&l, arg_n, sizeof(long));
            envc = l;
            arg_n += sizeof(long);
            envs = arg_n;
            for (int i = 0; i < envc; i++) {
                arg_n = arg_n + strlen(arg_n) + 1;
            }
            if (request_header.cmd == LAUNCHER_KWRAPPER) {
                tty = arg_n;
                arg_n += strlen(tty) + 1;
            }
        }

        if (request_header.cmd == LAUNCHER_SHELL || request_header.cmd == LAUNCHER_KWRAPPER
                || request_header.cmd == LAUNCHER_EXT_EXEC || request_header.cmd == LAUNCHER_EXEC_NEW) {
            memcpy(&l, arg_n, sizeof(long));
            avoid_loops = l;
            arg_n += sizeof(long);
        }

        if (request_header.cmd == LAUNCHER_SHELL || request_header.cmd == LAUNCHER_KWRAPPER
                || request_header.cmd == LAUNCHER_EXT_EXEC) {
            startup_id_str = arg_n;
            arg_n += strlen(startup_id_str) + 1;
        }

        if ((request_header.arg_length > (arg_n - request_data)) &&
                (request_header.cmd == LAUNCHER_EXT_EXEC || request_header.cmd == LAUNCHER_EXEC_NEW)) {
            // Optional cwd
            cwd = arg_n; arg_n += strlen(cwd) + 1;
        }

        if ((arg_n - request_data) != request_header.arg_length) {
#ifndef NDEBUG
            fprintf(stderr, "kdeinit5: EXEC request has invalid format.\n");
#endif
            free(request_data);
            d.debug_wait = false;
            return true; // sure?
        }

#if HAVE_X11 || HAVE_XCB
        // support for the old a bit broken way of setting DISPLAY for multihead
        QByteArray olddisplay = qgetenv(displayEnvVarName_c());
        QByteArray kdedisplay = qgetenv("KDE_DISPLAY");
        bool reset_display = (! olddisplay.isEmpty() &&
                              ! kdedisplay.isEmpty() &&
                              olddisplay != kdedisplay);

        if (reset_display) {
            qputenv(displayEnvVarName_c(), kdedisplay);
        }
#endif
        pid = launch(argc, name, args, cwd, envc, envs,
                     request_header.cmd == LAUNCHER_SHELL || request_header.cmd == LAUNCHER_KWRAPPER,
                     tty, avoid_loops, startup_id_str);

#if HAVE_X11 || HAVE_XCB
        if (reset_display) {
            unsetenv("KDE_DISPLAY");
            qputenv(displayEnvVarName_c(), olddisplay);
        }
#endif

        if (pid && (d.result == 0)) {
            response_header.cmd = LAUNCHER_OK;
            response_header.arg_length = sizeof(response_data);
            response_data = pid;
            write(sock, &response_header, sizeof(response_header));
            write(sock, &response_data, response_header.arg_length);

            // add new child to list
            struct child *child = (struct child *) malloc(sizeof(struct child));
            child->pid = pid;
            child->sock = dup(sock);
            child->next = children;
            children = child;
        } else {
            int l = d.errorMsg.length();
            if (l) {
                l++;    // Include trailing null.
            }
            response_header.cmd = LAUNCHER_ERROR;
            response_header.arg_length = l;
            write(sock, &response_header, sizeof(response_header));
            if (l) {
                write(sock, d.errorMsg.data(), l);
            }
        }
        d.debug_wait = false;
    } else if (request_header.arg_length && request_header.cmd == LAUNCHER_SETENV) {
        const char *env_name;
        const char *env_value;
        env_name = request_data;
        env_value = env_name + strlen(env_name) + 1;

#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: Got SETENV '%s=%s' from %s.\n", env_name, env_value, who);
#endif

        if (request_header.arg_length !=
                (int)(strlen(env_name) + strlen(env_value) + 2)) {
#ifndef NDEBUG
            fprintf(stderr, "kdeinit5: SETENV request has invalid format.\n");
#endif
            free(request_data);
            return true; // sure?
        }
        qputenv(env_name, env_value);
    } else if (request_header.cmd == LAUNCHER_TERMINATE_KDE) {
#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: terminate KDE.\n");
#endif
#if HAVE_X11
        kdeinit_xio_errhandler(nullptr);
#endif
    } else if (request_header.cmd == LAUNCHER_TERMINATE_KDEINIT) {
#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: Got termination request (PID %ld).\n", (long) getpid());
#endif
        if (d.launcher_pid) {
            kill(d.launcher_pid, SIGTERM);
            d.launcher_pid = 0;
            close(d.launcher[0]);
            d.launcher[0] = -1;
        }
        unlink(sock_file);
        if (children) {
            close(d.wrapper);
            d.wrapper = -1;
#ifndef NDEBUG
            fprintf(stderr, "kdeinit5: Closed sockets, but not exiting until all children terminate.\n");
#endif
        } else {
            raise(SIGTERM);
        }
    } else if (request_header.cmd == LAUNCHER_DEBUG_WAIT) {
#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: Debug wait activated.\n");
#endif
        d.debug_wait = true;
    }
    if (request_data) {
        free(request_data);
    }
    return true;
}

static void handle_requests(pid_t waitForPid)
{
    int max_sock = d.deadpipe[0];
    if (d.wrapper > max_sock) {
        max_sock = d.wrapper;
    }
    if (d.launcher[0] > max_sock) {
        max_sock = d.launcher[0];
    }
#if HAVE_X11
    if (X11fd > max_sock) {
        max_sock = X11fd;
    }
#endif
    max_sock++;

    while (1) {
        fd_set rd_set;
        fd_set wr_set;
        fd_set e_set;
        int result;
        pid_t exit_pid;
        int exit_status;
        char c;

        // Flush the pipe of death
        while (read(d.deadpipe[0], &c, 1) == 1) {
        }

        // Handle dying children
        do {
            exit_pid = waitpid(-1, &exit_status, WNOHANG);
            if (exit_pid > 0) {
#ifndef NDEBUG
                fprintf(stderr, "kdeinit5: PID %ld terminated.\n", (long) exit_pid);
#endif
                if (waitForPid && (exit_pid == waitForPid)) {
                    return;
                }

                if (WIFEXITED(exit_status)) { // fix process return value
                    exit_status = WEXITSTATUS(exit_status);
                } else if (WIFSIGNALED(exit_status)) {
                    exit_status = 128 + WTERMSIG(exit_status);
                }
                child_died(exit_pid, exit_status);

                if (d.wrapper < 0 && !children) {
#ifndef NDEBUG
                    fprintf(stderr, "kdeinit5: Last child terminated, exiting (PID %ld).\n",
                            (long) getpid());
#endif
                    raise(SIGTERM);
                }
            }
        } while (exit_pid > 0);

        FD_ZERO(&rd_set);
        FD_ZERO(&wr_set);
        FD_ZERO(&e_set);

        if (d.launcher[0] >= 0) {
            FD_SET(d.launcher[0], &rd_set);
        }
        if (d.wrapper >= 0) {
            FD_SET(d.wrapper, &rd_set);
        }
        FD_SET(d.deadpipe[0], &rd_set);
#if HAVE_X11
        if (X11fd >= 0) {
            FD_SET(X11fd, &rd_set);
        }
#endif

        result = select(max_sock, &rd_set, &wr_set, &e_set, nullptr);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            perror("kdeinit5: Aborting. select() failed");
            return;
        }

        // Handle wrapper request
        if (d.wrapper >= 0 && FD_ISSET(d.wrapper, &rd_set)) {
            struct sockaddr_un client;
            kde_socklen_t sClient = sizeof(client);
            int sock = accept(d.wrapper, (struct sockaddr *)&client, &sClient);
            if (sock >= 0) {
                d.accepted_fd = sock;
                handle_launcher_request(sock, "wrapper");
                close(sock);
                d.accepted_fd = -1;
            }
        }

        // Handle launcher request
        if (d.launcher[0] >= 0 && FD_ISSET(d.launcher[0], &rd_set)) {
            if (!handle_launcher_request(d.launcher[0], "launcher")) {
                launcher_died();
            }
            if (waitForPid == d.launcher_pid) {
                return;
            }
        }

#if HAVE_X11
        // Look for incoming X11 events
        if (X11fd >= 0 && FD_ISSET(X11fd, &rd_set)) {
            if (X11display != nullptr) {
                XEvent event_return;
                while (XPending(X11display)) {
                    XNextEvent(X11display, &event_return);
                }
            }
        }
#endif
    }
}

static void generate_socket_name()
{
#if HAVE_X11 || HAVE_XCB
    QByteArray display = qgetenv(displayEnvVarName_c());
    if (display.isEmpty()) {
        fprintf(stderr, "kdeinit5: Aborting. $%s is not set. \n", displayEnvVarName_c());
        exit(255);
    }
    int i;
    if ((i = display.lastIndexOf('.')) > display.lastIndexOf(':') && i >= 0) {
        display.truncate(i);
    }

    display.replace(':', '_');
#ifdef __APPLE__
    // not entirely impossible, so let's leave it
    display.replace('/', '_');
#endif
#else
    // not using a DISPLAY variable; use an empty string instead
    QByteArray display = "";
#endif
    // WARNING, if you change the socket name, adjust kwrapper too
    const QString socketFileName = QStringLiteral("kdeinit5_%1").arg(QLatin1String(display));
    const QByteArray socketName = QFile::encodeName(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/') + socketFileName);
    if (static_cast<unsigned>(socketName.length()) >= MAX_SOCK_FILE) {
        fprintf(stderr, "kdeinit5: Aborting. Socket name will be too long:\n");
        fprintf(stderr, "         '%s'\n", socketName.data());
        exit(255);
    }
    strcpy(sock_file, socketName.data());
}

#if HAVE_X11
int kdeinit_xio_errhandler(Display *disp)
{
    // disp is nullptr when KDE shuts down. We don't want those warnings then.

    if (disp) {
        qWarning("kdeinit5: Fatal IO error: client killed");
    }

    if (sock_file[0]) {
        // Delete any stale socket file
        unlink(sock_file);
    }

    // Don't kill our children in suicide mode, they may still be in use
    if (d.suicide) {
        if (d.launcher_pid) {
            kill(d.launcher_pid, SIGTERM);
        }
        if (d.kded_pid) {
            kill(d.kded_pid, SIGTERM);
        }
        exit(0);
    }

    if (disp) {
        qWarning("kdeinit5: sending SIGHUP to children.");
    }

    // this should remove all children we started
    signal(SIGHUP, SIG_IGN);
    kill(0, SIGHUP);

    sleep(2);

    if (disp) {
        qWarning("kdeinit5: sending SIGTERM to children.");
    }

    // and if they don't listen to us, this should work
    signal(SIGTERM, SIG_IGN);
    kill(0, SIGTERM);

    if (disp) {
        qWarning("kdeinit5: Exit.");
    }

    exit(0);
    return 0;
}

int kdeinit_x_errhandler(Display *dpy, XErrorEvent *err)
{
#ifndef NDEBUG
    char errstr[256];
    // kdeinit almost doesn't use X, and therefore there shouldn't be any X error
    XGetErrorText(dpy, err->error_code, errstr, 256);
    fprintf(stderr, "kdeinit5(%d) : KDE detected X Error: %s %d\n"
            "         Major opcode: %d\n"
            "         Minor opcode: %d\n"
            "         Resource id:  0x%lx\n",
            getpid(), errstr, err->error_code, err->request_code, err->minor_code, err->resourceid);

#else
    Q_UNUSED(dpy);
    Q_UNUSED(err);
#endif
    return 0;
}

// needs to be done sooner than initXconnection() because of also opening
// another X connection for startup notification purposes
static void setupX()
{
    XSetIOErrorHandler(kdeinit_xio_errhandler);
    XSetErrorHandler(kdeinit_x_errhandler);
    /*
        Handle the tricky case of running via kdesu/su/sudo/etc. There the usual case
        is that kdesu (etc.) creates a file with xauth information, sets XAUTHORITY,
        runs the command and removes the xauth file after the command finishes. However,
        dbus and kdeinit daemon currently don't clean up properly and keeping running.
        Which means that running a KDE app via kdesu the second time talks to kdeinit
        with obsolete xauth information, which makes it unable to connect to X or launch
        any X11 applications.
        Even fixing the cleanup probably wouldn't be sufficient, since it'd be possible to
        launch one kdesu session, another one, exit the first one and the app from the second
        session would be using kdeinit from the first one.
        So the trick here is to duplicate the xauth file to another file in KDE's tmp
        location, make the file have a consistent name so that future sessions will use it
        as well, point XAUTHORITY there and never remove the file (except for possible
        tmp cleanup).
    */
    if (!qEnvironmentVariableIsEmpty("XAUTHORITY")) {
        QByteArray display = qgetenv(displayEnvVarName_c());
        int i;
        if ((i = display.lastIndexOf('.')) > display.lastIndexOf(':') && i >= 0) {
            display.truncate(i);
        }
        display.replace(':', '_');
#ifdef __APPLE__
        display.replace('/', '_');
#endif
        QString xauth = QDir::tempPath() + QLatin1String("/xauth-")
                        + QString::number(getuid()) + QLatin1Char('-') + QString::fromLocal8Bit(display);
        QSaveFile xauthfile(xauth);
        QFile xauthfrom(QFile::decodeName(qgetenv("XAUTHORITY")));
        // Set umask to make sure the file permissions of xauthfile are correct
        mode_t oldMask = umask(S_IRGRP | S_IROTH | S_IWGRP | S_IWOTH);
        if (!xauthfrom.open(QFile::ReadOnly) || !xauthfile.open(QFile::WriteOnly)
                || xauthfile.write(xauthfrom.readAll()) != xauthfrom.size() || !xauthfile.commit()) {
            // error
        } else {
            qputenv("XAUTHORITY", QFile::encodeName(xauth));
        }
        umask(oldMask);
    }
}

static int initXconnection()
{
    X11display = XOpenDisplay(nullptr);
    if (X11display != nullptr) {
        XCreateSimpleWindow(X11display, DefaultRootWindow(X11display), 0, 0, 1, 1, \
                            0,
                            BlackPixelOfScreen(DefaultScreenOfDisplay(X11display)),
                            BlackPixelOfScreen(DefaultScreenOfDisplay(X11display)));
#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: opened connection to %s\n", DisplayString(X11display));
#endif
        int fd = XConnectionNumber(X11display);
        int on = 1;
        (void) setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, (int) sizeof(on));
        return fd;
    } else
        fprintf(stderr, "kdeinit5: Can not connect to the X Server.\n" \
                "kdeinit5: Might not terminate at end of session.\n");

    return -1;
}
#endif

#ifndef Q_OS_OSX
// Find a shared lib in the lib dir, e.g. libkio.so.
// Completely unrelated to plugins.
static QString findSharedLib(const QString &lib)
{
    QString path = QFile::decodeName(CMAKE_INSTALL_PREFIX "/" KDE_INSTALL_LIBDIR "/") + lib;
    if (QFile::exists(path)) {
        return path;
    }
    // We could also look in LD_LIBRARY_PATH, but really, who installs the main libs in different prefixes?
    return QString();
}
#endif

extern "C" {

    static void secondary_child_handler(int)
    {
        waitpid(-1, nullptr, WNOHANG);
    }

}

int main(int argc, char **argv)
{
#ifndef _WIN32_WCE
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
#endif

    pid_t pid;
    bool do_fork = true;
    int launch_klauncher = 1;
    int launch_kded = 0;
    int keep_running = 1;
    d.suicide = false;

    // Save arguments first...
    char **safe_argv = (char **) malloc(sizeof(char *) * argc);
    for (int i = 0; i < argc; i++) {
        safe_argv[i] = strcpy((char *)malloc(strlen(argv[i]) + 1), argv[i]);
        if (strcmp(safe_argv[i], "--no-klauncher") == 0) {
            launch_klauncher = 0;
        }
        if (strcmp(safe_argv[i], "--kded") == 0) {
            launch_kded = 1;
        }
        // allow both nofork and no-fork for compatibility with
        // old versions (both of this and of KUniqueApplication)
        if (strcmp(safe_argv[i], "--nofork") == 0) {
            do_fork = false;
        }
        if (strcmp(safe_argv[i], "--no-fork") == 0) {
            do_fork = false;
        }
        if (strcmp(safe_argv[i], "--suicide") == 0) {
            d.suicide = true;
        }
        if (strcmp(safe_argv[i], "--exit") == 0) {
            keep_running = 0;
        }
        if (strcmp(safe_argv[i], "--version") == 0) {
            printf("Qt: %s\n", qVersion());
            printf("KDE: %s\n", KINIT_VERSION_STRING);
            exit(0);
        }
#if KDEINIT_OOM_PROTECT
        if (strcmp(safe_argv[i], "--oom-pipe") == 0 && i + 1 < argc) {
            oom_pipe = atol(argv[i + 1]);
        }
#endif
        if (strcmp(safe_argv[i], "--help") == 0) {
            printf("Usage: kdeinit5 [options]\n");
            printf("    --no-fork         Do not fork\n");
            // printf("    --no-klauncher    Do not start klauncher\n");
            printf("    --kded            Start kded\n");
            printf("    --suicide         Terminate when no KDE applications are left running\n");
            printf("    --version         Show version information\n");
            // printf("    --exit            Terminate when kded has run\n");
            exit(0);
        }
    }

    cleanup_fds();

    // Redirect stdout to stderr. We have no reason to use stdout anyway.
    // This minimizes our impact on commands used in pipes.
    (void)dup2(2, 1);

    if (do_fork) {
#ifdef Q_OS_OSX
        mac_fork_and_reexec_self();
#else
        if (pipe(d.initpipe) != 0) {
            perror("kdeinit5: pipe failed");
            return 1;
        }

        // Fork here and let parent process exit.
        // Parent process may only exit after all required services have been
        // launched. (dcopserver/klauncher and services which start with '+')
        signal(SIGCHLD, secondary_child_handler);
        if (fork() > 0) { // Go into background
            close(d.initpipe[1]);
            d.initpipe[1] = -1;
            // wait till init is complete
            char c;
            while (read(d.initpipe[0], &c, 1) < 0)
                ;
            // then exit;
            close(d.initpipe[0]);
            d.initpipe[0] = -1;
            return 0;
        }
        close(d.initpipe[0]);
        d.initpipe[0] = -1;
#endif
    }

    // Make process group leader (for shutting down children later)
    if (keep_running) {
        setsid();
    }

    // Prepare to change process name
#ifndef SKIP_PROCTITLE
    proctitle_init(argc, argv);
#endif

    // don't change envvars before proctitle_init()
    unsetenv("LD_BIND_NOW");
    unsetenv("DYLD_BIND_AT_LAUNCH");
    KCrash::loadedByKdeinit = true;

    d.maxname = strlen(argv[0]);
    d.launcher_pid = 0;
    d.kded_pid = 0;
    d.wrapper = -1;
    d.accepted_fd = -1;
    d.debug_wait = false;
    d.launcher_ok = false;
    children = nullptr;
    init_signals();
#if HAVE_X11
    setupX();
#endif

    generate_socket_name();
    if (keep_running) {
        // Create <runtime dir>/kdeinit5-<display> socket for incoming wrapper requests.
        init_kdeinit_socket();
    }
#if defined(Q_OS_UNIX) && !defined(Q_OS_OSX)
    if (!d.suicide && qEnvironmentVariableIsEmpty("KDE_IS_PRELINKED")) {
        for (const char *extra_lib : extra_libs) {
            const QString extra = findSharedLib(QString::fromLatin1(extra_lib));
            if (!extra.isEmpty()) {
                QLibrary l(extra);
                l.setLoadHints(QLibrary::ExportExternalSymbolsHint);
                (void)l.load();
            }
#ifndef NDEBUG
            else {
                fprintf(stderr, "%s was not found.\n", extra_lib);
            }
#endif

        }
    }
#endif
    if (launch_klauncher) {
        start_klauncher();
        handle_requests(d.launcher_pid); // Wait for klauncher to be ready
    }

#if HAVE_X11
    X11fd = initXconnection();
#endif

    QFont::initialize();
#if HAVE_X11
    if (XSupportsLocale()) {
        // Similar to QApplication::create_xim()
        // but we need to use our own display
        XOpenIM(X11display, nullptr, nullptr, nullptr);
    }
#endif

    if (launch_kded) {
        qputenv("KDED_STARTED_BY_KDEINIT", "1");
        pid = launch(1, KDED_EXENAME, nullptr);
        unsetenv("KDED_STARTED_BY_KDEINIT");
#ifndef NDEBUG
        fprintf(stderr, "kdeinit5: Launched KDED, pid = %ld result = %d\n", (long) pid, d.result);
#endif
        d.kded_pid = pid;
        // kded5 doesn't fork on startup anymore, so just move on.
        //handle_requests(pid);
    }

    for (int i = 1; i < argc; i++) {
        if (safe_argv[i][0] == '+') {
            pid = launch(1, safe_argv[i] + 1, nullptr);
#ifndef NDEBUG
            fprintf(stderr, "kdeinit5: Launched '%s', pid = %ld result = %d\n", safe_argv[i] + 1, (long) pid, d.result);
#endif
            handle_requests(pid);
        } else if (safe_argv[i][0] == '-'
#if KDEINIT_OOM_PROTECT
                   || isdigit(safe_argv[i][0])
#endif
                  ) {
            // Ignore
        } else {
            pid = launch(1, safe_argv[i], nullptr);
#ifndef NDEBUG
            fprintf(stderr, "kdeinit5: Launched '%s', pid = %ld result = %d\n", safe_argv[i], (long) pid, d.result);
#endif
        }
    }

    // Free arguments
    for (int i = 0; i < argc; i++) {
        free(safe_argv[i]);
    }
    free(safe_argv);

#ifndef SKIP_PROCTITLE
    proctitle_set("Running...");
#endif

    if (!keep_running) {
        return 0;
    }

    if (d.initpipe[1] != -1) {
        char c = 0;
        write(d.initpipe[1], &c, 1); // Kdeinit is started.
        close(d.initpipe[1]);
        d.initpipe[1] = -1;
    }

    handle_requests(0);

    return 0;
}
