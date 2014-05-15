add_executable(start_kdeinit start_kdeinit.c)
add_executable(start_kdeinit_wrapper start_kdeinit_wrapper.c)

install(TARGETS start_kdeinit DESTINATION ${KF5_LIBEXEC_INSTALL_DIR})
install(TARGETS start_kdeinit_wrapper DESTINATION ${KF5_LIBEXEC_INSTALL_DIR})

if (KDEINIT_OOM_PROTECT)
    install(CODE "
      set(START_KDEINIT_PATH \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${KF5_LIBEXEC_INSTALL_DIR}/start_kdeinit\")
        EXECUTE_PROCESS(COMMAND sh -c \"chown 0 '\${START_KDEINIT_PATH}' && chmod u+s '\${START_KDEINIT_PATH}'\")
    ")
endif ()
