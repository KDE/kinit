/* Based on setproctitle.c from OpenSSH 6.6p1 */
/*
    SPDX-FileCopyrightText: 2014 Alex Merry <alex.merry@kde.org>
    SPDX-FileCopyrightText: 2003 Damien Miller
    SPDX-FileCopyrightText: 1983, 1995-1997 Eric P. Allman
    SPDX-FileCopyrightText: 1988, 1993 The Regents of the University of California.  All rights reserved.

    SPDX-License-Identifier: BSD-3-Clause
*/

#include "proctitle.h"
#include <config-kdeinit.h>

#define PT_NONE         0    /* don't use it at all */
#define PT_PSTAT        1    /* use pstat(PSTAT_SETCMD, ...) */
#define PT_REUSEARGV    2    /* cover argv with title information */
#define PT_SETPROCTITLE 3    /* forward onto the native setproctitle */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#if HAVE_SETPROCTITLE
#  define PT_TYPE    PT_SETPROCTITLE
   // process title should get prepended automagically
#  define ADD_PROCTITLE 0
#elif HAVE_SYS_PSTAT_H && HAVE_PSTAT
#  include <sys/pstat.h>
#  define PT_TYPE    PT_PSTAT
#elif CAN_CLOBBER_ARGV
#  define PT_TYPE    PT_REUSEARGV
#endif

#ifndef PT_TYPE
#  define PT_TYPE    PT_NONE
#endif
#ifndef ADD_PROCTITLE
#  define ADD_PROCTITLE 1
#endif

#if PT_TYPE == PT_REUSEARGV
static char *argv_start = nullptr;
static size_t argv_env_len = 0;
#endif

#if HAVE___PROGNAME
extern char *__progname;
#else
char *__progname;
#endif

void
proctitle_init(int argc, char *argv[])
{
#if HAVE___PROGNAME
    // progname may be a reference to argv[0]
    __progname = strdup(__progname);
#else
    if (argc == 0 || argv[0] == NULL) {
        __progname = "unknown";    /* XXX */
    } else {
        char *p = strrchr(argv[0], '/');
        if (p == NULL)
            p = argv[0];
        else
            p++;

        __progname = strdup(p);
    }
#endif

#if PT_TYPE == PT_REUSEARGV
    if (argc == 0 || argv[0] == nullptr)
        return;

    extern char **environ;
    char *lastargv = nullptr;
    char **envp = environ;
    int i;

    /*
     * NB: This assumes that argv has already been copied out of the
     * way. This is true for kdeinit, but may not be true for other
     * programs. Beware.
     */

    /* Fail if we can't allocate room for the new environment */
    for (i = 0; envp[i] != nullptr; i++)
        ;
    if ((environ = (char**)calloc(i + 1, sizeof(*environ))) == nullptr) {
        environ = envp;    /* put it back */
        return;
    }

    /*
     * Find the last argv string or environment variable within
     * our process memory area.
     */
    for (i = 0; i < argc; i++) {
        if (lastargv == nullptr || lastargv + 1 == argv[i])
            lastargv = argv[i] + strlen(argv[i]);
    }
    for (i = 0; envp[i] != nullptr; i++) {
        if (lastargv + 1 == envp[i])
            lastargv = envp[i] + strlen(envp[i]);
    }

    argv[1] = nullptr;
    argv_start = argv[0];
    argv_env_len = lastargv - argv[0] - 1;

    /*
     * Copy environment
     * XXX - will truncate env on strdup fail
     */
    for (i = 0; envp[i] != nullptr; i++)
        environ[i] = strdup(envp[i]);
    environ[i] = nullptr;
#endif /* PT_REUSEARGV */
}

void
proctitle_set(const char *fmt, ...)
{
#if PT_TYPE != PT_NONE
#if PT_TYPE == PT_REUSEARGV
    if (argv_env_len <= 0)
        return;
#endif

    bool skip_proctitle = false;
    if (fmt != nullptr && fmt[0] == '-') {
        skip_proctitle = true;
        ++fmt;
    }
    char ptitle[1024];
    memset(ptitle, '\0', sizeof(ptitle));
    size_t len = 0;

#if ADD_PROCTITLE
    if (!skip_proctitle) {
        strncpy(ptitle, __progname, sizeof(ptitle)-1);
        len = strlen(ptitle);
        if (fmt != nullptr && sizeof(ptitle) - len > 2) {
            strcpy(ptitle + len, ": ");
            len += 2;
        }
    }
#endif

    if (fmt != nullptr) {
        int r = -1;
        if (len < sizeof(ptitle) - 1) {
            va_list ap;
            va_start(ap, fmt);
            r = vsnprintf(ptitle + len, sizeof(ptitle) - len , fmt, ap);
            va_end(ap);
        }
        if (r == -1 || (size_t)r >= sizeof(ptitle) - len)
            return;
    }

#if PT_TYPE == PT_PSTAT
    union pstun pst;
    pst.pst_command = ptitle;
    pstat(PSTAT_SETCMD, pst, strlen(ptitle), 0, 0);
#elif PT_TYPE == PT_REUSEARGV
    strncpy(argv_start, ptitle, argv_env_len);
    argv_start[argv_env_len-1] = '\0';
#elif PT_TYPE == PT_SETPROCTITLE
    if (fmt == NULL) {
        setproctitle(NULL);
#if defined(__FreeBSD__)
    } else if (skip_proctitle) {
        // setproctitle on FreeBSD allows skipping the process title
        setproctitle("-%s", ptitle);
#endif
    } else {
        setproctitle("%s", ptitle);
    }
#endif

#endif /* !PT_NONE */
}
