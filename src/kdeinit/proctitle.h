/*
    SPDX-FileCopyrightText: 2014 Alex Merry <alex.merry@kde.org>
    SPDX-FileCopyrightText: 2003 Damien Miller
    SPDX-FileCopyrightText: 1983, 1995-1997 Eric P. Allman
    SPDX-FileCopyrightText: 1988, 1993 The Regents of the University of California.  All rights reserved.

    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef SETPROCTITLE_H
#define SETPROCTITLE_H

/**
 * Set up the data structures for changing the process title.
 *
 * This must be called before proctitle_set, and must not be called
 * multiple times.  Be warned that this function and proctitle_set may
 * alter the contents of argv, and so any argument parsing should be
 * done before calling this function.
 *
 * @param argc  argc, as passed to main()
 * @param argv  argv, as passed to main() (NB: this MUST NOT be a copy
 *              of argv!)
 */
void proctitle_init(int argc, char *argv[]);

/**
 * Set the process title that appears on the ps command.
 *
 * The title is set to the executable's name, followed by the result
 * of a printf-style expansion of the arguments as specified by the fmt
 * argument.  If fmt begins with a '-' character, the executable's name
 * is skipped (providing the platform implementation supports it;
 * OpenBSD and NetBSD do not).
 *
 * Note that proctitle_init must be called before using this function.
 */
void proctitle_set(const char *fmt, ...);

#endif // SETPROCTITLE_H
