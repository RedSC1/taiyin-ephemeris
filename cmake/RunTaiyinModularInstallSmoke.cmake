if(NOT DEFINED TAIYIN_BUILD_DIR
    OR NOT DEFINED TAIYIN_HARNESS
    OR NOT DEFINED TAIYIN_CORE_NAME
    OR NOT DEFINED TAIYIN_ASTROLOGY_NAME
    OR NOT DEFINED CMAKE_INSTALL_LIBDIR
    OR NOT DEFINED CMAKE_INSTALL_BINDIR)
    message(FATAL_ERROR
        "TAIYIN_BUILD_DIR, TAIYIN_HARNESS, TAIYIN_CORE_NAME, "
        "TAIYIN_ASTROLOGY_NAME, CMAKE_INSTALL_LIBDIR, and "
        "CMAKE_INSTALL_BINDIR are required")
endif()

set(smoke_prefix "${TAIYIN_BUILD_DIR}/modular-install-smoke")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${smoke_prefix}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "could not clear modular install smoke prefix")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${TAIYIN_BUILD_DIR}"
        --prefix "${smoke_prefix}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "modular install failed")
endif()

set(core_candidates
    "${smoke_prefix}/${CMAKE_INSTALL_LIBDIR}/${TAIYIN_CORE_NAME}"
    "${smoke_prefix}/${CMAKE_INSTALL_BINDIR}/${TAIYIN_CORE_NAME}"
)
set(astrology_candidates
    "${smoke_prefix}/${CMAKE_INSTALL_LIBDIR}/${TAIYIN_ASTROLOGY_NAME}"
    "${smoke_prefix}/${CMAKE_INSTALL_BINDIR}/${TAIYIN_ASTROLOGY_NAME}"
)
foreach(candidate IN LISTS core_candidates)
    if(EXISTS "${candidate}")
        set(core_path "${candidate}")
        break()
    endif()
endforeach()
foreach(candidate IN LISTS astrology_candidates)
    if(EXISTS "${candidate}")
        set(astrology_path "${candidate}")
        break()
    endif()
endforeach()
if(NOT core_path OR NOT astrology_path)
    message(FATAL_ERROR "installed modular libraries were not found")
endif()

execute_process(
    COMMAND "${TAIYIN_HARNESS}" "${core_path}" "${astrology_path}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "installed modular loader smoke test failed (exit ${result})\n"
        "stdout: ${output}\n"
        "stderr: ${error_output}")
endif()
