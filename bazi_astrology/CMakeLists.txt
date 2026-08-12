set(TAIYIN_BAZI_RULE_SOURCES
    src/bazi.cpp
    src/bazi_rules.cpp
)

if(TAIYIN_BUILD_MAINTAINER_TOOLS AND Python3_Interpreter_FOUND)
    add_custom_target(taiyin_bazi_shen_sha_tables_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shen_sha_tables.py
            --input ${CMAKE_CURRENT_SOURCE_DIR}/data/shen_sha_simple_rules.json
            --output ${CMAKE_CURRENT_SOURCE_DIR}/generated/taiyin_bazi_shen_sha_tables.h
            --check
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shen_sha_tables.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/shen_sha_simple_rules.json
            ${CMAKE_CURRENT_SOURCE_DIR}/generated/taiyin_bazi_shen_sha_tables.h
        VERBATIM
    )
endif()

add_library(taiyin_bazi_extension STATIC ${TAIYIN_BAZI_RULE_SOURCES})
target_link_libraries(taiyin_bazi_extension PUBLIC taiyin_chinese_calendar_ganzhi_extension)
target_include_directories(taiyin_bazi_extension
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/internal
        ${PROJECT_SOURCE_DIR}/src
)
set_target_properties(taiyin_bazi_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(TARGET taiyin_bazi_shen_sha_tables_check)
    add_dependencies(taiyin_bazi_extension taiyin_bazi_shen_sha_tables_check)
endif()

if(TAIYIN_BUILD_MODULAR_C_API)
    add_library(taiyin_bazi SHARED
        ${TAIYIN_BAZI_RULE_SOURCES}
        src/bazi_c_api.cpp
    )
    target_include_directories(taiyin_bazi
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/internal
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/src/c_api
    )
    target_link_libraries(taiyin_bazi PRIVATE
        taiyin
    )
    target_compile_definitions(taiyin_bazi PRIVATE
        TAIYIN_C_BUILD
        TAIYIN_C_BAZI_BUILD
        TAIYIN_C_HAS_BAZI_EXTENSION
        TAIYIN_GANZHI_SHARED_IMPORT
    )
    taiyin_configure_modular_shared_target(taiyin_bazi)
    set_target_properties(taiyin_bazi PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME taiyin_bazi
        VERSION 1.0.0
        SOVERSION 1
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    if(TARGET taiyin_bazi_shen_sha_tables_check)
        add_dependencies(taiyin_bazi taiyin_bazi_shen_sha_tables_check)
    endif()
else()
    target_sources(taiyin_c PRIVATE src/bazi_c_api.cpp)
    target_include_directories(taiyin_c
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${PROJECT_SOURCE_DIR}/src/c_api
    )
    target_link_libraries(taiyin_c PRIVATE taiyin_bazi_extension)
    target_compile_definitions(taiyin_c PRIVATE TAIYIN_C_HAS_BAZI_EXTENSION)
endif()

target_sources(taiyin_c_static PRIVATE src/bazi_c_api.cpp)
target_include_directories(taiyin_c_static
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${PROJECT_SOURCE_DIR}/src/c_api
)
target_link_libraries(taiyin_c_static PRIVATE taiyin_bazi_extension)
target_compile_definitions(taiyin_c_static PRIVATE TAIYIN_C_HAS_BAZI_EXTENSION)

install(FILES include/taiyin/c/bazi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/taiyin/c)

add_executable(test_bazi tests/test_bazi.cpp)
target_link_libraries(test_bazi PRIVATE taiyin_bazi_extension)

add_executable(benchmark_bazi_10000 tests/benchmark_bazi_10000.cpp)
target_link_libraries(benchmark_bazi_10000 PRIVATE taiyin_bazi_extension)

add_executable(qiyun_records_10000 tests/qiyun_records_10000.cpp)
target_link_libraries(qiyun_records_10000 PRIVATE taiyin_bazi_extension)

set(TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE "" CACHE FILEPATH
    "Optional pre-migration qiyun_records_10000 executable for differential testing")
if(TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE requires Python 3")
    endif()
    if(NOT EXISTS "${TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE}")
        message(FATAL_ERROR
            "QiYun reference executable does not exist: "
            "${TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE}")
    endif()
    add_test(
        NAME bazi_qiyun_10000_differential
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_qiyun_records.py
            --reference ${TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE}
            --candidate $<TARGET_FILE:qiyun_records_10000>
            --data-root ${PROJECT_SOURCE_DIR}/data
    )
    set_tests_properties(bazi_qiyun_10000_differential PROPERTIES
        TIMEOUT 900
        LABELS "bazi;differential;legacy"
    )
endif()

add_executable(test_bazi_calendar_integration tests/test_bazi_calendar_integration.cpp)
target_link_libraries(test_bazi_calendar_integration PRIVATE taiyin_bazi_extension)

add_executable(test_bazi_c_api tests/test_bazi_c_api.c)
target_include_directories(test_bazi_c_api PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(TAIYIN_BUILD_MODULAR_C_API)
    target_compile_definitions(test_bazi_c_api PRIVATE TAIYIN_TEST_MODULAR_C_API=1)
    target_link_libraries(test_bazi_c_api PRIVATE
        taiyin
        taiyin_bazi
    )
else()
    target_link_libraries(test_bazi_c_api PRIVATE taiyin_c)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_bazi_c_api PRIVATE m)
endif()

add_executable(test_bazi_c_api_static tests/test_bazi_c_api.c)
target_include_directories(test_bazi_c_api_static PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(test_bazi_c_api_static PRIVATE taiyin_c_static)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_bazi_c_api_static PRIVATE m)
endif()

add_test(NAME bazi COMMAND test_bazi)
set_tests_properties(bazi PROPERTIES ENVIRONMENT "TAIYIN_REPO_ROOT=${PROJECT_SOURCE_DIR}")
add_test(NAME bazi_calendar_integration
    COMMAND test_bazi_calendar_integration ${PROJECT_SOURCE_DIR}/data)
add_test(NAME bazi_c_api COMMAND test_bazi_c_api ${PROJECT_SOURCE_DIR}/data)
add_test(NAME bazi_c_api_static COMMAND test_bazi_c_api_static ${PROJECT_SOURCE_DIR}/data)

# In a Windows build tree, taiyin_bazi.dll sits beside this test while its
# taiyin.dll dependency is emitted in the base target directory. Installed
# DLLs share one directory; make the CTest layout behave the same without
# changing the install layout.
if(TAIYIN_BUILD_MODULAR_C_API AND WIN32)
    set_tests_properties(bazi_c_api PROPERTIES
        ENVIRONMENT
            "PATH=$<TARGET_FILE_DIR:taiyin>$<SEMICOLON>$ENV{PATH}"
    )
endif()

if(TAIYIN_BUILD_MODULAR_C_API AND UNIX)
    add_test(
        NAME c_api_export_symbols_bazi_internal
        COMMAND ${CMAKE_COMMAND}
            -DTAIYIN_NM=${CMAKE_NM}
            -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
            -DTAIYIN_RUNTIME_LIBRARY=$<TARGET_FILE:taiyin_bazi>
            "-DTAIYIN_RUNTIME_FORBIDDEN_REGEX=(^|\\n)[^\\n]*[_]*ZN6taiyin4bazi5rules"
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
    )
endif()
