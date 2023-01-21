#define kde_socklen_t socklen_t

#define CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}"
#define KDE_INSTALL_LIBDIR "${KDE_INSTALL_LIBDIR}"
#define KDE_INSTALL_FULL_LIBEXECDIR_KF5 "${KDE_INSTALL_FULL_LIBEXECDIR_KF5}"
#define KDE_INSTALL_FULL_BINDIR "${KDE_INSTALL_FULL_BINDIR}"

/* These are for proctitle.cpp: */
#cmakedefine01 HAVE___PROGNAME
#cmakedefine01 HAVE_SYS_PSTAT_H
#cmakedefine01 HAVE_PSTAT
#cmakedefine01 HAVE_SETPROCTITLE
#cmakedefine01 CAN_CLOBBER_ARGV

#cmakedefine01 HAVE_X11
#cmakedefine01 HAVE_XCB
#cmakedefine01 HAVE_CAPABILITIES
#cmakedefine01 HAVE_SYS_SELECT_H
#cmakedefine01 HAVE_CLOSE_RANGE

/* for start_kdeinit */
#cmakedefine01 KDEINIT_OOM_PROTECT

