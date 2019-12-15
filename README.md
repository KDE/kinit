# KInit

Helper library to speed up start of applications on KDE workspaces

## Introduction

kdeinit is a process launcher somewhat similar to the famous init used for
booting UNIX.

It launches processes by forking and then loading a dynamic library which
should contain a 'kdemain(...)' function.

Using kdeinit to launch KDE applications makes starting a typical KDE
applications 2.5 times faster (100ms instead of 250ms on a P-III 500) It
reduces memory consumption by approx. 350Kb per application.


## How it works

kdeinit is linked against all libraries a standard KDE application needs. With
this technique starting an application becomes much faster because now only the
application itself needs to be linked whereas otherwise both the application as
well as all the libraries it uses need to be linked.


## Startup Speed

Starting an application linked against libqt, libkdecore and libkdeui in the
conventional way takes approx. 150ms on a Pentium III - 500Mhz.  Starting the
same application via kdeinit takes less than 10ms.

(application without KApplication constructor, the KApplication constructor
requires an extra 100ms in both cases)


## Memory Usage

An application linked against libqt, libkdecore and libkdeui started in the
conventional way requires about 498Kb memory.  (average of 10 instances) If the
same application is started via kdeinit it requires about 142Kb. A difference
of 356Kb (application without KApplication constructor)

If we take the KApplication constructor into account, an application started in
the conventional way takes about 679Kb memory while the same application
started via kdeinit requires about 380Kb. Here the difference is somewhat less,
299Kb. This seems to be caused by the fact that the dynamic linker does "lazy
linking". We can force the linker to link everything at startup by specifying
"LD\_BIND\_NOW=true". When kdeinit is started with this option on, kdeinit is
back to its full efficiency, an application with a KApplication constructor now
uses 338Kb of memory.  A difference of 341Kb with the normal case.


## Adapting programs to use kdeinit

The source code of a program does not require any change to take advantage of
kdeinit, only the build system needs to be adjusted:

First you need to find the KF5Init package:

    find_package(KF5Init 5.0.0 REQUIRED)

Then, instead of declaring your executable and the libraries it links to like this:

    add_executable(myexe ${myexe_SRCS})

    target_link_libraries(myexe ...)

You must use:

    kf5_add_kdeinit_executable(myexe ${myexe_SRCS})

    # Note the different target name
    target_link_libraries(kdeinit_myexe ...)


## Disadvantages

The process name of applications started via kdeinit is "kdeinit". This problem
can be corrected to a degree by changing the application name as shown by 'ps'.
However, applications like `killall` will only see "kdeinit" as process name.
To workaround this, use `kdekillall`, from [kde-dev-scripts][], for applications
started via kdeinit.

[kde-dev-scripts]: https://commits.kde.org/kde-dev-scripts

