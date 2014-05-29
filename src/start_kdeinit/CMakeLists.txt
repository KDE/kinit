add_executable(start_kdeinit start_kdeinit.c)
if (Libcap_FOUND)
   target_link_libraries(start_kdeinit ${Libcap_LIBRARIES})
endif()
add_executable(start_kdeinit_wrapper start_kdeinit_wrapper.c)

install(TARGETS start_kdeinit DESTINATION ${KF5_LIBEXEC_INSTALL_DIR})
install(TARGETS start_kdeinit_wrapper DESTINATION ${KF5_LIBEXEC_INSTALL_DIR})

if (KDEINIT_OOM_PROTECT)
       if (Libcap_FOUND)
                  install( CODE "execute_process(
                          COMMAND
                                 ${SETCAP_EXECUTABLE}
                                 CAP_SYS_RESOURCE=+ep
                                 $ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${KF5_LIBEXEC_INSTALL_DIR}/start_kdeinit)"
                )
       else()
                  install(CODE "
                  set(START_KDEINIT_PATH \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${KF5_LIBEXEC_INSTALL_DIR}/start_kdeinit\")
                  EXECUTE_PROCESS(COMMAND sh -c \"chown 0 '\${START_KDEINIT_PATH}' && chmod u+s '\${START_KDEINIT_PATH}'\")
    ")
       endif ()
endif ()
