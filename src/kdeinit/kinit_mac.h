/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999-2000 Waldo Bastian <bastian@kde.org>
    SPDX-FileCopyrightText: 1999 Mario Weilguni <mweilguni@sime.com>
    SPDX-FileCopyrightText: 2001 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2015, 2016 René J.V. Bertin <rjvbertin@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#ifndef _KINIT_MAC_H

extern pid_t launch(int argc, const char *_name, const char *args,
                    const char *cwd = 0, int envc = 0, const char *envs = 0,
                    bool reset_env = false,
                    const char *tty = 0, bool avoid_loops = false,
                    const char *startup_id_str = "0");

extern void mac_fork_and_reexec_self();

#define _KINIT_MAC_H
#endif
