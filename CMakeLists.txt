cmake_minimum_required(VERSION 3.16)

project(KInit)

include(FeatureSummary)
find_package(ECM 5.86.0  NO_MODULE)
set_package_properties(ECM PROPERTIES TYPE REQUIRED DESCRIPTION "Extra CMake Modules." URL "https://commits.kde.org/extra-cmake-modules")
feature_summary(WHAT REQUIRED_PACKAGES_NOT_FOUND FATAL_ON_MISSING_REQUIRED_PACKAGES)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(REQUIRED_QT_VERSION 5.15.2)
find_package(Qt5 "${REQUIRED_QT_VERSION}" CONFIG REQUIRED Core Gui DBus)
include(KDEInstallDirs)
include(KDEFrameworkCompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)
include(ECMSetupVersion)
include(ECMQtDeclareLoggingCategory)

set(KF_VERSION "5.86.0") # handled by release scripts
set(KF_DEP_VERSION "5.86.0") # handled by release scripts

ecm_setup_version(${KF_VERSION} VARIABLE_PREFIX KINIT
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

find_package(XCB MODULE COMPONENTS XCB)
set_package_properties(XCB PROPERTIES TYPE OPTIONAL)
set(HAVE_XCB ${XCB_XCB_FOUND})

# used by 4 executables in this module
if(HAVE_SOCKET_LIBRARY)
  set(KINIT_SOCKET_LIBRARY socket)
else()
  set(KINIT_SOCKET_LIBRARY)
endif()

find_package(KF5Service ${KF_DEP_VERSION} REQUIRED)
find_package(KF5KIO ${KF_DEP_VERSION} REQUIRED)
find_package(KF5I18n ${KF_DEP_VERSION} REQUIRED)
find_package(KF5WindowSystem ${KF_DEP_VERSION} REQUIRED)
find_package(KF5Crash ${KF_DEP_VERSION} REQUIRED)
find_package(KF5Config ${KF_DEP_VERSION} REQUIRED)
find_package(KF5DocTools ${KF_DEP_VERSION})

if (NOT WIN32)
find_package(Libcap)
find_package(KF5DBusAddons ${KF_DEP_VERSION} REQUIRED)
set_package_properties(Libcap PROPERTIES
                       TYPE OPTIONAL
                       PURPOSE "KInit needs setcap in order to install start_kdeinit with CAP_SYS_RESOURCE capabilities"
                      )
endif ()
set(HAVE_CAPABILITIES ${Libcap_FOUND})

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KF5Init")
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/KF5InitConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050f02)
add_definitions(-DKF_DISABLE_DEPRECATED_BEFORE_AND_AT=0x055100)
add_definitions(-DTRANSLATION_DOMAIN=\"kinit5\")
ki18n_install(po)

if (KF5DocTools_FOUND)
    kdoctools_install(po)
endif()
add_subdirectory(src)

if (KF5DocTools_FOUND)
    add_subdirectory(docs)
endif()

install( FILES
	"${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfig.cmake"
    KF5InitMacros.cmake ${CMAKE_CURRENT_BINARY_DIR}/KF5InitConfigVersion.cmake
    DESTINATION ${CMAKECONFIG_INSTALL_DIR} COMPONENT Devel )

install(FILES
        kde5init_dummy.cpp.in
        DESTINATION ${CMAKECONFIG_INSTALL_DIR} COMPONENT Devel)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
