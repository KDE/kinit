/*
 * This file is part of the KDE libraries
 * Copyright (c) 1999-2000 Waldo Bastian <bastian@kde.org>
 *           (c) 1999 Mario Weilguni <mweilguni@sime.com>
 *           (c) 2001 Lubos Lunak <l.lunak@kde.org>
 *           (c) 2015,2016 René J.V. Bertin <rjvbertin@gmail.com>
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

#ifndef _KINIT_MAC_H

extern pid_t launch(int argc, const char *_name, const char *args,
                    const char *cwd = 0, int envc = 0, const char *envs = 0,
                    bool reset_env = false,
                    const char *tty = 0, bool avoid_loops = false,
                    const char *startup_id_str = "0");

extern void mac_fork_and_reexec_self();

#define _KINIT_MAC_H
#endif
