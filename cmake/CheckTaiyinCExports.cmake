if(NOT DEFINED TAIYIN_NM OR NOT EXISTS "${TAIYIN_NM}")
    message(FATAL_ERROR "TAIYIN_NM must name the platform nm executable")
endif()

function(check_taiyin_library library expected_regex forbidden_regex label)
    if(NOT EXISTS "${library}")
        message(FATAL_ERROR "${label} library does not exist: ${library}")
    endif()

    if(TAIYIN_PLATFORM STREQUAL "Darwin")
        execute_process(
            COMMAND "${TAIYIN_NM}" -gU "${library}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE symbols
            ERROR_VARIABLE error_output
        )
    else()
        execute_process(
            COMMAND "${TAIYIN_NM}" -D --defined-only "${library}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE symbols
            ERROR_VARIABLE error_output
        )
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "nm failed for ${label}: ${error_output}")
    endif()

    string(REPLACE "\n" ";" symbol_lines "${symbols}")
    set(has_expected_symbol FALSE)
    foreach(line IN LISTS symbol_lines)
        string(STRIP "${line}" line)
        if(line STREQUAL "")
            continue()
        endif()
        string(REGEX MATCH "[_A-Za-z][_A-Za-z0-9]*$" symbol "${line}")
        if(NOT symbol MATCHES "^_?taiyin_")
            if(TAIYIN_ALLOW_CPP_SYMBOLS)
                continue()
            endif()
            message(FATAL_ERROR
                "${label} leaked a non-C-ABI symbol: ${symbol} (${line})")
        endif()
        if(expected_regex AND symbol MATCHES "${expected_regex}")
            set(has_expected_symbol TRUE)
        endif()
        if(forbidden_regex AND symbol MATCHES "${forbidden_regex}")
            message(FATAL_ERROR
                "${label} exported a symbol owned by another module: ${symbol}")
        endif()
    endforeach()
    if(expected_regex AND NOT has_expected_symbol)
        message(FATAL_ERROR
            "${label} exported no symbol matching ${expected_regex}")
    endif()
endfunction()

if(DEFINED TAIYIN_RUNTIME_LIBRARY)
    if(NOT EXISTS "${TAIYIN_RUNTIME_LIBRARY}")
        message(FATAL_ERROR
            "runtime library does not exist: ${TAIYIN_RUNTIME_LIBRARY}")
    endif()
    if(NOT DEFINED TAIYIN_RUNTIME_FORBIDDEN_REGEX)
        message(FATAL_ERROR "TAIYIN_RUNTIME_FORBIDDEN_REGEX is required")
    endif()
    if(TAIYIN_PLATFORM STREQUAL "Darwin")
        execute_process(
            COMMAND "${TAIYIN_NM}" -gU "${TAIYIN_RUNTIME_LIBRARY}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE symbols
            ERROR_VARIABLE error_output
        )
    else()
        execute_process(
            COMMAND "${TAIYIN_NM}" -D --defined-only "${TAIYIN_RUNTIME_LIBRARY}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE symbols
            ERROR_VARIABLE error_output
        )
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "nm failed for runtime: ${error_output}")
    endif()
    if(symbols MATCHES "${TAIYIN_RUNTIME_FORBIDDEN_REGEX}")
        message(FATAL_ERROR
            "runtime leaked an implementation symbol matching "
            "${TAIYIN_RUNTIME_FORBIDDEN_REGEX}")
    endif()
    return()
endif()

if(DEFINED TAIYIN_MODULE_LIBRARIES)
    string(REPLACE "," ";" TAIYIN_MODULE_LIBRARIES "${TAIYIN_MODULE_LIBRARIES}")
    string(REPLACE "," ";" TAIYIN_MODULE_LABELS "${TAIYIN_MODULE_LABELS}")
    string(REPLACE "," ";" TAIYIN_MODULE_EXPECTED_REGEXES "${TAIYIN_MODULE_EXPECTED_REGEXES}")
    string(REPLACE "," ";" TAIYIN_MODULE_FORBIDDEN_REGEXES "${TAIYIN_MODULE_FORBIDDEN_REGEXES}")
    list(LENGTH TAIYIN_MODULE_LIBRARIES library_count)
    list(LENGTH TAIYIN_MODULE_LABELS label_count)
    list(LENGTH TAIYIN_MODULE_EXPECTED_REGEXES expected_count)
    list(LENGTH TAIYIN_MODULE_FORBIDDEN_REGEXES forbidden_count)
    if(NOT library_count EQUAL label_count
        OR NOT library_count EQUAL expected_count
        OR NOT library_count EQUAL forbidden_count)
        message(FATAL_ERROR "module export check lists must have equal lengths")
    endif()

    math(EXPR last_index "${library_count} - 1")
    foreach(index RANGE ${last_index})
        list(GET TAIYIN_MODULE_LIBRARIES ${index} library)
        list(GET TAIYIN_MODULE_LABELS ${index} label)
        list(GET TAIYIN_MODULE_EXPECTED_REGEXES ${index} expected_regex)
        list(GET TAIYIN_MODULE_FORBIDDEN_REGEXES ${index} forbidden_regex)
        check_taiyin_library(
            "${library}" "${expected_regex}" "${forbidden_regex}" "${label}")
    endforeach()
    return()
endif()

if(NOT DEFINED TAIYIN_LIBRARY OR NOT EXISTS "${TAIYIN_LIBRARY}")
    message(FATAL_ERROR "TAIYIN_LIBRARY must name the built taiyin C library")
endif()
if(NOT DEFINED TAIYIN_EXPECT_BAZI)
    message(FATAL_ERROR "TAIYIN_EXPECT_BAZI must be ON or OFF")
endif()

if(TAIYIN_PLATFORM STREQUAL "Darwin")
    set(TAIYIN_LIBRARY_EXPECTED_REGEX "^_?taiyin_")
else()
    set(TAIYIN_LIBRARY_EXPECTED_REGEX "^_?taiyin_")
endif()
check_taiyin_library(
    "${TAIYIN_LIBRARY}"
    "${TAIYIN_LIBRARY_EXPECTED_REGEX}"
    ""
    "taiyin C library")

if(TAIYIN_PLATFORM STREQUAL "Darwin")
    execute_process(
        COMMAND "${TAIYIN_NM}" -gU "${TAIYIN_LIBRARY}"
        OUTPUT_VARIABLE legacy_symbols
        RESULT_VARIABLE legacy_result
    )
else()
    execute_process(
        COMMAND "${TAIYIN_NM}" -D --defined-only "${TAIYIN_LIBRARY}"
        OUTPUT_VARIABLE legacy_symbols
        RESULT_VARIABLE legacy_result
    )
endif()
if(NOT legacy_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect legacy BaZi exports")
endif()
string(REGEX MATCH "(^|\n)[^\n]*_taiyin_bazi_[A-Za-z0-9_]+" bazi_match "${legacy_symbols}")
if(TAIYIN_EXPECT_BAZI AND NOT bazi_match)
    message(FATAL_ERROR "BaZi-enabled taiyin C library exported no taiyin_bazi_* symbols")
endif()
if(NOT TAIYIN_EXPECT_BAZI AND bazi_match)
    message(FATAL_ERROR "BaZi-disabled taiyin C library exported taiyin_bazi_* symbols")
endif()
