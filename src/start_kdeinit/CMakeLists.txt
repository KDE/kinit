add_executable(start_kdeinit start_kdeinit.c)
ecm_mark_nongui_executable(start_kdeinit)
if (Libcap_FOUND)
   target_link_libraries(start_kdeinit ${Libcap_LIBRARIES})
endif()
add_executable(start_kdeinit_wrapper start_kdeinit_wrapper.c)
ecm_mark_nongui_executable(start_kdeinit_wrapper)

install(TARGETS start_kdeinit DESTINATION ${KDE_INSTALL_FULL_LIBEXECDIR_KF5})
install(TARGETS start_kdeinit_wrapper DESTINATION ${KDE_INSTALL_FULL_LIBEXECDIR_KF5})

if (KDEINIT_OOM_PROTECT)
       if (Libcap_FOUND)
                  install( CODE "execute_process(
                          COMMAND
                                 ${SETCAP_EXECUTABLE}
                                 CAP_SYS_RESOURCE=+ep
                                 \$ENV{DESTDIR}${KDE_INSTALL_FULL_LIBEXECDIR_KF5}/start_kdeinit)"
                )
       else()
                  install(CODE "
                  set(START_KDEINIT_PATH \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_LIBEXECDIR_KF5}/start_kdeinit\")
                  EXECUTE_PROCESS(COMMAND sh -c \"chown 0 '\${START_KDEINIT_PATH}' && chmod u+s '\${START_KDEINIT_PATH}'\")
    ")
       endif ()
endif ()
