/*
 * This file is part of the KDE libraries
 * Copyright (c) 1999-2000 Waldo Bastian <bastian@kde.org>
 *           (c) 1999 Mario Weilguni <mweilguni@sime.com>
 *           (c) 2001 Lubos Lunak <l.lunak@kde.org>
 *           (c) 2016 René J.V. Bertin <rjvbertin@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _KINIT_H

/* Group data */
static struct {
    int maxname;
    int fd[2];
    int launcher[2]; /* socket pair for launcher communication */
    int deadpipe[2]; /* pipe used to detect dead children */
    int initpipe[2];
    int wrapper; /* socket for wrapper communication */
    int accepted_fd; /* socket accepted and that must be closed in the child process */
    char result;
    int exit_status;
    pid_t fork;
    pid_t launcher_pid;
    pid_t kded_pid;
    int n;
    char **argv;
    int (*func)(int, char *[]);
    int (*launcher_func)(int);
    bool debug_wait;
    QByteArray errorMsg;
    bool launcher_ok;
    bool suicide;
} d;

struct child {
    pid_t pid;
    int sock; /* fd to write message when child is dead*/
    struct child *next;
};

/*
 * Close fd's which are only useful for the parent process.
 * Restore default signal handlers.
 */
extern void close_fds();
extern void setup_tty(const char *tty);
extern QByteArray execpath_avoid_loops(const QByteArray &exec, int envc, const char *envs, bool avoid_loops);
extern void reset_oom_protect();

#define _KINIT_H
#endif
