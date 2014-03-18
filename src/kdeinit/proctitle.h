/*
 * Copyright 2014 Alex Merry <alex.merry@kde.org>
 * Copyright 2003 Damien Miller
 * Copyright (c) 1983, 1995-1997 Eric P. Allman
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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
