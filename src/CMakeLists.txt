add_definitions(-DTRANSLATION_DOMAIN="kinit")

include(${CMAKE_CURRENT_SOURCE_DIR}/../KF5InitMacros.cmake)
include(ECMMarkNonGuiExecutable)

if (CMAKE_SYSTEM_NAME MATCHES Linux)
    MESSAGE(STATUS "Using setuid root kdeinit wrapper in order to protect it from bad Linux OOM-killer")
    set(KDEINIT_OOM_PROTECT 1)
endif ()

configure_file(config-kdeinit.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kdeinit.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(kdeinit)
add_subdirectory(kwrapper)
add_subdirectory(klauncher)

if (NOT WIN32)
  add_subdirectory(kshell)
  add_subdirectory(start_kdeinit)
endif ()
