# on win32 kdeinit5 has to be a console application
# to be able to catch stderr based --verbose output
add_executable(kdeinit5)
ecm_mark_nongui_executable(kdeinit5)

if (WIN32)
    target_sources(kdeinit5 PRIVATE kinit_win.cpp ../klauncher_cmds.cpp)
    target_link_libraries(kdeinit5 psapi)
elseif (APPLE)
    target_sources(kdeinit5 PRIVATE kinit.cpp kinit.h kinit_mac.mm proctitle.cpp ../klauncher_cmds.cpp)
else ()
    target_sources(kdeinit5 PRIVATE kinit.cpp kinit.h proctitle.cpp ../klauncher_cmds.cpp)
endif ()

target_include_directories(kdeinit5 PRIVATE ${KInit_BINARY_DIR}) # for kinit_version.h

target_link_libraries(kdeinit5
    ${KINIT_SOCKET_LIBRARY}
    Qt5::Gui #QFont::initialize
    KF5::WindowSystem # KStartupInfo
    KF5::Crash # KCrash::loadedByKdeinit
    KF5::I18n #i18n() used on some error messages
    KF5::ConfigCore
    Qt5::DBus
)

if (NOT WIN32)
    target_link_libraries(kdeinit5 KF5::DBusAddons)
endif()

if (APPLE)
    target_link_libraries(kdeinit5 "-framework Foundation -framework AppKit")
endif ()

target_compile_definitions(kdeinit5 PRIVATE
    CMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
    KDE_INSTALL_LIBDIR="${KDE_INSTALL_LIBDIR}"
    KDE_INSTALL_FULL_LIBEXECDIR_KF5="${KDE_INSTALL_FULL_LIBEXECDIR_KF5}"
)

if (HAVE_X11)
  target_link_libraries(kdeinit5 ${X11_X11_LIB})
endif()

if (HAVE_XCB)
  target_link_libraries(kdeinit5 XCB::XCB)
endif()

install(TARGETS kdeinit5 ${KF5_INSTALL_TARGETS_DEFAULT_ARGS} )

if (NOT WIN32)
  # kdeinit5_shutdown
  add_executable(kdeinit5_shutdown ../wrapper.cpp)
  ecm_mark_nongui_executable(kdeinit5_shutdown)

  target_link_libraries(kdeinit5_shutdown  ${KINIT_SOCKET_LIBRARY} Qt5::Core KF5::DBusAddons)

  install(TARGETS kdeinit5_shutdown ${KF5_INSTALL_TARGETS_DEFAULT_ARGS} )

  # kdeinit5_wrapper
  add_executable(kdeinit5_wrapper ../wrapper.cpp)
  ecm_mark_nongui_executable(kdeinit5_wrapper)

  target_link_libraries(kdeinit5_wrapper  ${KINIT_SOCKET_LIBRARY} Qt5::Core KF5::DBusAddons)

  if (NOT HAVE_X11)
    target_compile_definitions(kdeinit5_wrapper PRIVATE -DNO_DISPLAY)
  endif ()

  install(TARGETS kdeinit5_wrapper ${KF5_INSTALL_TARGETS_DEFAULT_ARGS} )
endif ()
