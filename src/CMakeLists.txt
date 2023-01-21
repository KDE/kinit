include(${CMAKE_CURRENT_SOURCE_DIR}/../KF5InitMacros.cmake)
include(ECMMarkNonGuiExecutable)

if (CMAKE_SYSTEM_NAME MATCHES Linux)
   if (Libcap_FOUND)
    message(STATUS "kdeinit wrapper uses capabilities to protect kdeinit from misguided Linux OOM killer")
      else()
    message(STATUS "kdeinit wrapper uses setuid-root to protect kdeinit from misguided Linux OOM killer")
   endif()
    set(KDEINIT_OOM_PROTECT 1)
endif ()

check_symbol_exists(close_range "unistd.h" HAVE_CLOSE_RANGE)

configure_file(config-kdeinit.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kdeinit.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(kdeinit)
add_subdirectory(kwrapper)
add_subdirectory(klauncher)

if (NOT WIN32)
  add_subdirectory(kshell)
  add_subdirectory(start_kdeinit)
endif ()

ecm_qt_install_logging_categories(
    EXPORT KINIT
    FILE kinit.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)
