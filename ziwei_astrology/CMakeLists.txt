set(TAIYIN_ZIWEI_SOURCES
    src/anchors.cpp
    src/chart.cpp
    src/calendar_adapter.cpp
    src/calendar_adapter_internal.cpp
    src/debug_dump.cpp
    src/dynamic_bitset.cpp
    src/flow.cpp
    src/flow_calendar_adapter.cpp
    src/limits.cpp
    src/rules.cpp
    src/rules_loader.cpp
    src/reverse_lookup.cpp
    src/star_registry.cpp
)

add_library(taiyin_ziwei_extension STATIC ${TAIYIN_ZIWEI_SOURCES})
add_library(taiyin::ziwei ALIAS taiyin_ziwei_extension)
target_link_libraries(
    taiyin_ziwei_extension
    PUBLIC taiyin_chinese_calendar_ganzhi_extension
)
target_include_directories(taiyin_ziwei_extension
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(taiyin_ziwei_extension SYSTEM
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toml11)
set_target_properties(taiyin_ziwei_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(TAIYIN_BUILD_MODULAR_C_API)
    add_library(taiyin_ziwei SHARED
        ${TAIYIN_ZIWEI_SOURCES}
        src/ziwei_c_api.cpp
    )
    target_include_directories(taiyin_ziwei
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/src/c_api
    )
    target_include_directories(taiyin_ziwei SYSTEM
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toml11)
    target_link_libraries(taiyin_ziwei PRIVATE taiyin)
    target_compile_definitions(taiyin_ziwei PRIVATE
        TAIYIN_C_BUILD
        TAIYIN_C_ZIWEI_BUILD
        TAIYIN_GANZHI_SHARED_IMPORT
    )
    taiyin_configure_modular_shared_target(taiyin_ziwei)
    set_target_properties(taiyin_ziwei PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME taiyin_ziwei
        VERSION 1.0.0
        SOVERSION 1
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    # -fvisibility=hidden does not suppress every weak C++ template symbol in
    # an unoptimized GNU Debug build.  The version script keeps the modular
    # shared object a deliberate C-only ABI in every build mode.
    if(UNIX AND NOT APPLE)
        target_link_options(taiyin_ziwei PRIVATE
            "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/taiyin_ziwei_exports.map")
    endif()
else()
    target_sources(taiyin_c PRIVATE src/ziwei_c_api.cpp)
    target_include_directories(taiyin_c
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/src/c_api
    )
    target_include_directories(taiyin_c SYSTEM
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toml11)
    target_link_libraries(taiyin_c PRIVATE taiyin_ziwei_extension)
    target_compile_definitions(taiyin_c PRIVATE TAIYIN_C_HAS_ZIWEI_EXTENSION)
endif()

target_sources(taiyin_c_static PRIVATE src/ziwei_c_api.cpp)
target_include_directories(taiyin_c_static
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/c_api
)
target_include_directories(taiyin_c_static SYSTEM
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toml11)
target_link_libraries(taiyin_c_static PRIVATE taiyin_ziwei_extension)
target_compile_definitions(taiyin_c_static PRIVATE
    TAIYIN_C_HAS_ZIWEI_EXTENSION)

install(FILES include/taiyin/c/ziwei.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/taiyin/c)
install(DIRECTORY rules/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/taiyin/ziwei
    FILES_MATCHING PATTERN "*.toml" PATTERN "README.md")

add_executable(example_ziwei_basic_chart examples/basic_chart.cpp)
target_link_libraries(example_ziwei_basic_chart PRIVATE taiyin_ziwei_extension)
target_compile_definitions(example_ziwei_basic_chart PRIVATE
    TAIYIN_ZIWEI_RULES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/rules/default.toml"
)

if(TAIYIN_BUILD_MAINTAINER_TOOLS)
    add_executable(benchmark_ziwei benchmarks/benchmark_ziwei.cpp)
    target_link_libraries(benchmark_ziwei PRIVATE taiyin_ziwei_extension)
    target_compile_definitions(benchmark_ziwei PRIVATE
        TAIYIN_ZIWEI_RULES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/rules/default.toml"
    )

    add_executable(dump_ziwei_exhaustive tools/dump_exhaustive.cpp)
    target_link_libraries(dump_ziwei_exhaustive PRIVATE taiyin_ziwei_extension)
    target_compile_definitions(dump_ziwei_exhaustive PRIVATE
        TAIYIN_ZIWEI_RULES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/rules/default.toml"
    )
endif()

add_executable(test_ziwei_dynamic_bitset tests/test_dynamic_bitset.cpp)
target_link_libraries(test_ziwei_dynamic_bitset PRIVATE taiyin_ziwei_extension)

add_executable(test_ziwei_types tests/test_types.cpp)
target_link_libraries(test_ziwei_types PRIVATE taiyin_ziwei_extension)

add_executable(test_ziwei_rules tests/test_rules.cpp)
target_link_libraries(test_ziwei_rules PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_rules PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_chart_flow tests/test_chart_flow.cpp)
target_link_libraries(test_ziwei_chart_flow PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_chart_flow PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_anchor_computation tests/test_anchor_computation.cpp)
target_link_libraries(test_ziwei_anchor_computation PRIVATE taiyin_ziwei_extension)

add_executable(test_ziwei_oracle_differential tests/test_oracle_differential.cpp)
target_link_libraries(test_ziwei_oracle_differential PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_oracle_differential PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_limits tests/test_limits.cpp)
target_link_libraries(test_ziwei_limits PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_limits PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_calendar_adapter tests/test_calendar_adapter.cpp)
target_link_libraries(test_ziwei_calendar_adapter PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_calendar_adapter PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_oracle_corpus tests/test_oracle_corpus.cpp)
target_link_libraries(test_ziwei_oracle_corpus PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_oracle_corpus PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_limit_oracle tests/test_limit_oracle.cpp)
target_link_libraries(test_ziwei_limit_oracle PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_limit_oracle PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_flow_calendar_adapter
    tests/test_flow_calendar_adapter.cpp)
target_link_libraries(test_ziwei_flow_calendar_adapter
    PRIVATE taiyin_ziwei_extension)
target_compile_definitions(test_ziwei_flow_calendar_adapter PRIVATE
    TAIYIN_ZIWEI_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(test_ziwei_c_api tests/test_ziwei_c_api.c)
target_include_directories(test_ziwei_c_api PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(TAIYIN_BUILD_MODULAR_C_API)
    target_compile_definitions(test_ziwei_c_api PRIVATE
        TAIYIN_TEST_MODULAR_C_API=1)
    target_link_libraries(test_ziwei_c_api PRIVATE taiyin taiyin_ziwei)
else()
    target_link_libraries(test_ziwei_c_api PRIVATE taiyin_c)
endif()

add_executable(test_ziwei_c_api_static tests/test_ziwei_c_api.c)
target_include_directories(test_ziwei_c_api_static PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(test_ziwei_c_api_static PRIVATE taiyin_c_static)

add_test(NAME ziwei_dynamic_bitset COMMAND test_ziwei_dynamic_bitset)
add_test(NAME ziwei_types COMMAND test_ziwei_types)
add_test(NAME ziwei_rules COMMAND test_ziwei_rules)
add_test(NAME ziwei_chart_flow COMMAND test_ziwei_chart_flow)
add_test(NAME ziwei_anchor_computation COMMAND test_ziwei_anchor_computation)
add_test(NAME ziwei_oracle_differential COMMAND test_ziwei_oracle_differential)
add_test(NAME ziwei_limits COMMAND test_ziwei_limits)
add_test(NAME ziwei_calendar_adapter COMMAND test_ziwei_calendar_adapter)
add_test(NAME ziwei_oracle_corpus COMMAND test_ziwei_oracle_corpus)
add_test(NAME ziwei_limit_oracle COMMAND test_ziwei_limit_oracle)
add_test(NAME ziwei_flow_calendar_adapter
    COMMAND test_ziwei_flow_calendar_adapter)
add_test(NAME ziwei_example_basic_chart COMMAND example_ziwei_basic_chart)
add_test(NAME ziwei_c_api COMMAND test_ziwei_c_api
    ${PROJECT_SOURCE_DIR}/data ${CMAKE_CURRENT_SOURCE_DIR}/rules/default.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/option_default.toml)
add_test(NAME ziwei_c_api_static COMMAND test_ziwei_c_api_static
    ${PROJECT_SOURCE_DIR}/data ${CMAKE_CURRENT_SOURCE_DIR}/rules/default.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/option_default.toml)
set_tests_properties(
    ziwei_dynamic_bitset ziwei_types ziwei_rules ziwei_chart_flow
    ziwei_anchor_computation ziwei_oracle_differential ziwei_limits
    ziwei_calendar_adapter
    ziwei_oracle_corpus
    ziwei_limit_oracle
    ziwei_flow_calendar_adapter
    ziwei_example_basic_chart
    ziwei_c_api ziwei_c_api_static
    PROPERTIES LABELS "ziwei"
)

if(TAIYIN_BUILD_MODULAR_C_API AND WIN32)
    set_tests_properties(ziwei_c_api PROPERTIES
        ENVIRONMENT
            "PATH=$<TARGET_FILE_DIR:taiyin>$<SEMICOLON>$ENV{PATH}"
    )
endif()

if(TAIYIN_BUILD_MODULAR_C_API AND UNIX)
    add_test(
        NAME c_api_export_symbols_ziwei
        COMMAND ${CMAKE_COMMAND}
            -DTAIYIN_NM=${CMAKE_NM}
            -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
            -DTAIYIN_MODULE_LIBRARIES=$<TARGET_FILE:taiyin_ziwei>
            -DTAIYIN_MODULE_LABELS=ziwei
            "-DTAIYIN_MODULE_EXPECTED_REGEXES=^_?taiyin_ziwei_"
            "-DTAIYIN_MODULE_FORBIDDEN_REGEXES=^_?taiyin_(bazi|astrology|chinese_calendar|ganzhi)_"
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
    )
    set_tests_properties(c_api_export_symbols_ziwei PROPERTIES LABELS "ziwei")
endif()
