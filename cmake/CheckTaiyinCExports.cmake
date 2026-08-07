if(NOT DEFINED TAIYIN_NM OR NOT EXISTS "${TAIYIN_NM}")
    message(FATAL_ERROR "TAIYIN_NM must name the platform nm executable")
endif()
if(NOT DEFINED TAIYIN_LIBRARY OR NOT EXISTS "${TAIYIN_LIBRARY}")
    message(FATAL_ERROR "TAIYIN_LIBRARY must name the built taiyin C library")
endif()
if(NOT DEFINED TAIYIN_EXPECT_BAZI)
    message(FATAL_ERROR "TAIYIN_EXPECT_BAZI must be ON or OFF")
endif()

if(TAIYIN_PLATFORM STREQUAL "Darwin")
    execute_process(
        COMMAND "${TAIYIN_NM}" -gU "${TAIYIN_LIBRARY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE symbols
        ERROR_VARIABLE error_output
    )
else()
    execute_process(
        COMMAND "${TAIYIN_NM}" -D --defined-only "${TAIYIN_LIBRARY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE symbols
        ERROR_VARIABLE error_output
    )
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "nm failed: ${error_output}")
endif()

string(REPLACE "\n" ";" symbol_lines "${symbols}")
set(has_bazi_symbols FALSE)
foreach(line IN LISTS symbol_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "")
        continue()
    endif()
    string(REGEX MATCH "[_A-Za-z][_A-Za-z0-9]*$" symbol "${line}")
    if(symbol MATCHES "^_?taiyin_bazi_")
        set(has_bazi_symbols TRUE)
    endif()
    if(NOT symbol MATCHES "^_?taiyin_")
        message(FATAL_ERROR
            "taiyin C library leaked a non-C-ABI symbol: ${symbol} (${line})")
    endif()
endforeach()

if(TAIYIN_EXPECT_BAZI AND NOT has_bazi_symbols)
    message(FATAL_ERROR "BaZi-enabled taiyin C library exported no taiyin_bazi_* symbols")
endif()
if(NOT TAIYIN_EXPECT_BAZI AND has_bazi_symbols)
    message(FATAL_ERROR "BaZi-disabled taiyin C library exported taiyin_bazi_* symbols")
endif()
