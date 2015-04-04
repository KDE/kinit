cmake_minimum_required(VERSION 2.8.12)

project(KInit)

find_package(ECM 1.8.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(REQUIRED_QT_VERSION "5.2")
find_package(Qt5 "${REQUIRED_QT_VERSION}" CONFIG REQUIRED Core Gui DBus)
include(KDEInstallDirs)
include(KDEFrameworkCompilerSettings)
include(KDECMakeSettings)

include(FeatureSummary)
include(ECMPackageConfigHelpers)
include(ECMSetupVersion)

set(KF5_VERSION "5.9.0") # handled by release scripts
set(KF5_DEP_VERSION "5.8.0") # handled by release scripts

ecm_setup_version(${KF5_VERSION} VARIABLE_PREFIX KINIT
                        VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kinit_version.h"
                        PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfigVersion.cmake"
                        SOVERSION 5)

########### Configuring (mainly for proctitle) ###########
include(ConfigureChecks.cmake)

#optional features
if (NOT APPLE)
    find_package(X11)
endif()
set(HAVE_X11 ${X11_FOUND})

# used by 4 executables in this module
if(HAVE_SOCKET_LIBRARY)
  set(KINIT_SOCKET_LIBRARY socket)
else()
  set(KINIT_SOCKET_LIBRARY)
endif()

find_package(KF5Service ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5KIO ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5I18n ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5WindowSystem ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5Crash ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5Config ${KF5_DEP_VERSION} REQUIRED)
find_package(KF5DocTools ${KF5_DEP_VERSION} REQUIRED)

if (NOT WIN32)
find_package(Libcap)
set_package_properties(Libcap PROPERTIES
                       TYPE OPTIONAL
                       PURPOSE "KInit needs setcap in order to install start_kdeinit with CAP_SYS_RESOURCE capabilities"
                      )
endif ()
set(HAVE_CAPABILITIES ${Libcap_FOUND})

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KF5Init")
ecm_configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/KF5InitConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)

add_definitions(-DTRANSLATION_DOMAIN=\"kinit5\")
if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/po")
    ki18n_install(po)
    kdoctools_install(po)
endif()
add_subdirectory(src)
add_subdirectory(docs)
add_subdirectory(tests)

install( FILES
	"${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfig.cmake"
    KF5InitMacros.cmake ${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfigVersion.cmake
    DESTINATION ${CMAKECONFIG_INSTALL_DIR} COMPONENT Devel )

install(FILES
        kde5init_dummy.cpp.in
        DESTINATION ${CMAKECONFIG_INSTALL_DIR} COMPONENT Devel)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
