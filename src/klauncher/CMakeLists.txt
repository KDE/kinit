set(klauncher_SRCS
   klauncher.cpp
   klauncher.h
   klauncher_main.cpp
   klauncher_adaptor.cpp
   klauncher_adaptor.h
   ../klauncher_cmds.cpp
   ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KLauncher.xml # just so that it gets generated
   )

ecm_qt_declare_logging_category(klauncher_SRCS
    HEADER klauncher_debug.h
    IDENTIFIER KLAUNCHER
    CATEGORY_NAME kf.init.klauncher
    OLD_CATEGORY_NAMES kf5.kinit.klauncher
    DEFAULT_SEVERITY Warning
    DESCRIPTION "KLauncher"
    EXPORT KINIT
)

# Use org.kde.KSlaveLauncher.xml installed by kio
qt_add_dbus_adaptor(klauncher_SRCS ${KSlaveLauncher_DBUS_INTERFACE} klauncher.h KLauncher)

# The adaptor is manually edited, generate the xml from it.
qt_generate_dbus_interface(klauncher_adaptor.h org.kde.KLauncher.xml)

kf5_add_kdeinit_executable(klauncher NOGUI ${klauncher_SRCS})
set_target_properties(kdeinit_klauncher PROPERTIES DEFINE_SYMBOL MAKE_KLAUNCHER)
target_link_libraries(kdeinit_klauncher
   KF5::KIOCore # KIO::ConnectionServer
   KF5::KIOWidgets # KRun
   KF5::Crash  # sig_handler
   KF5::WindowSystem # KStartupInfo + fixx11
   KF5::I18n   # i18n()
   Qt5::DBus
)
if (APPLE)
    target_link_libraries(kdeinit_klauncher "-framework CoreFoundation")
endif()

if (HAVE_XCB)
    target_link_libraries(kdeinit_klauncher XCB::XCB)
endif()

install(TARGETS kdeinit_klauncher ${KF5_INSTALL_TARGETS_DEFAULT_ARGS})
install(TARGETS klauncher DESTINATION ${KDE_INSTALL_FULL_LIBEXECDIR_KF5} )
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KLauncher.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR} RENAME kf5_org.kde.KLauncher.xml)
