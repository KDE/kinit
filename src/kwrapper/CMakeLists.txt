add_executable(kwrapper5)
ecm_mark_nongui_executable(kwrapper5)

if (WIN32)
    target_sources(kwrapper5 PRIVATE kwrapper_win.cpp)
else ()
    target_sources(kwrapper5 PRIVATE kwrapper.cpp)
endif ()

target_link_libraries(kwrapper5 ${KINIT_SOCKET_LIBRARY} Qt5::Core)

if (NOT WIN32)
    target_link_libraries(kwrapper5 KF5::DBusAddons)
endif()

install(TARGETS kwrapper5 ${KF5_INSTALL_TARGETS_DEFAULT_ARGS} )
