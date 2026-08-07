get_target_property(TAIYIN_GANZHI_PASCAL_BUILD_DIR
    taiyin_ganzhi_pascal_rules_archive TAIYIN_PASCAL_BUILD_DIR)
if(NOT TAIYIN_GANZHI_PASCAL_BUILD_DIR
    OR TAIYIN_GANZHI_PASCAL_BUILD_DIR STREQUAL
        "TAIYIN_GANZHI_PASCAL_BUILD_DIR-NOTFOUND")
    message(FATAL_ERROR
        "BaZi requires the Ganzhi Pascal rule archive build directory")
endif()

taiyin_add_pascal_archive(
    taiyin_bazi_bitset_pascal_archive
    "${CMAKE_CURRENT_SOURCE_DIR}/pascal/taiyin_dynamic_bitset.pas"
    taiyin_dynamic_bitset
)
get_target_property(TAIYIN_BAZI_BITSET_PASCAL_BUILD_DIR
    taiyin_bazi_bitset_pascal_archive TAIYIN_PASCAL_BUILD_DIR)

if(NOT CMAKE_CROSSCOMPILING)
    taiyin_resolve_fpc_target(
        TAIYIN_BAZI_BITSET_TEST_TARGET_OS
        TAIYIN_BAZI_BITSET_TEST_TARGET_CPU)
    set(TAIYIN_BAZI_BITSET_TEST_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/pascal/taiyin_dynamic_bitset_test")
    set(TAIYIN_BAZI_BITSET_TEST_EXECUTABLE
        "${TAIYIN_BAZI_BITSET_TEST_BUILD_DIR}/taiyin_dynamic_bitset_test${CMAKE_EXECUTABLE_SUFFIX}")
    set(TAIYIN_BAZI_BITSET_TEST_EXTRA_FLAGS)
    if(TAIYIN_FPC_EXTRA_FLAGS)
        separate_arguments(TAIYIN_BAZI_BITSET_TEST_EXTRA_FLAGS
            NATIVE_COMMAND "${TAIYIN_FPC_EXTRA_FLAGS}")
    endif()
    add_custom_command(
        OUTPUT ${TAIYIN_BAZI_BITSET_TEST_EXECUTABLE}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${TAIYIN_BAZI_BITSET_TEST_BUILD_DIR}
        COMMAND ${TAIYIN_FPC_EXECUTABLE}
            -Cg
            -Mobjfpc
            -T${TAIYIN_BAZI_BITSET_TEST_TARGET_OS}
            -P${TAIYIN_BAZI_BITSET_TEST_TARGET_CPU}
            -FU${TAIYIN_BAZI_BITSET_TEST_BUILD_DIR}
            -FE${TAIYIN_BAZI_BITSET_TEST_BUILD_DIR}
            -Fu${TAIYIN_BAZI_BITSET_PASCAL_BUILD_DIR}
            ${TAIYIN_BAZI_BITSET_TEST_EXTRA_FLAGS}
            -o${TAIYIN_BAZI_BITSET_TEST_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/taiyin_dynamic_bitset_test.pas
        DEPENDS
            taiyin_bazi_bitset_pascal_archive_build
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/taiyin_dynamic_bitset_test.pas
        VERBATIM
    )
    add_custom_target(taiyin_dynamic_bitset_test_build ALL
        DEPENDS ${TAIYIN_BAZI_BITSET_TEST_EXECUTABLE})
    add_test(NAME bazi_dynamic_bitset
        COMMAND ${TAIYIN_BAZI_BITSET_TEST_EXECUTABLE})
endif()

taiyin_add_pascal_archive(
    taiyin_bazi_pascal_rules_archive
    "${CMAKE_CURRENT_SOURCE_DIR}/pascal/taiyin_bazi_rules.pas"
    taiyin_bazi_rules
    UNIT_PATHS
        ${TAIYIN_GANZHI_PASCAL_BUILD_DIR}
        ${TAIYIN_BAZI_BITSET_PASCAL_BUILD_DIR}
    DEPENDS
        taiyin_ganzhi_pascal_rules_archive_build
        taiyin_bazi_bitset_pascal_archive_build
        ${CMAKE_CURRENT_SOURCE_DIR}/pascal/taiyin_dynamic_bitset.pas
        ${CMAKE_CURRENT_SOURCE_DIR}/pascal/generated/taiyin_bazi_shen_sha_tables.inc
)

if(Python3_Interpreter_FOUND)
    add_custom_target(taiyin_bazi_shen_sha_tables_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shen_sha_tables.py
            --input ${CMAKE_CURRENT_SOURCE_DIR}/data/shen_sha_simple_rules.json
            --output ${CMAKE_CURRENT_SOURCE_DIR}/pascal/generated/taiyin_bazi_shen_sha_tables.inc
            --check
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shen_sha_tables.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/shen_sha_simple_rules.json
            ${CMAKE_CURRENT_SOURCE_DIR}/pascal/generated/taiyin_bazi_shen_sha_tables.inc
        VERBATIM
    )
    add_dependencies(
        taiyin_bazi_pascal_rules_archive_build
        taiyin_bazi_shen_sha_tables_check)
endif()

add_library(taiyin_bazi_extension STATIC
    src/bazi.cpp
)
target_link_libraries(taiyin_bazi_extension
    PUBLIC
        taiyin_bazi_pascal_rules_archive
        taiyin_bazi_bitset_pascal_archive
        taiyin_chinese_calendar_ganzhi_extension
)
target_include_directories(taiyin_bazi_extension
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/internal
        ${PROJECT_SOURCE_DIR}/src
)
set_target_properties(taiyin_bazi_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

foreach(c_target IN ITEMS taiyin_c taiyin_c_static)
    target_sources(${c_target} PRIVATE src/bazi_c_api.cpp)
    target_include_directories(${c_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/c_api
    )
    target_link_libraries(${c_target} PRIVATE taiyin_bazi_extension)
    target_compile_definitions(${c_target} PRIVATE TAIYIN_C_HAS_BAZI_EXTENSION)
endforeach()

install(FILES include/taiyin/c/bazi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/taiyin/c)

add_executable(test_bazi tests/test_bazi.cpp)
target_link_libraries(test_bazi PRIVATE taiyin_bazi_extension)

add_executable(
    test_bazi_calendar_integration
    tests/test_bazi_calendar_integration.cpp
)
target_link_libraries(test_bazi_calendar_integration PRIVATE taiyin_bazi_extension)

add_executable(test_bazi_c_api tests/test_bazi_c_api.c)
target_link_libraries(test_bazi_c_api PRIVATE taiyin_c)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_bazi_c_api PRIVATE m)
endif()

add_executable(test_bazi_c_api_static tests/test_bazi_c_api.c)
target_link_libraries(test_bazi_c_api_static PRIVATE taiyin_c_static)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_bazi_c_api_static PRIVATE m)
endif()

add_test(NAME bazi COMMAND test_bazi)
set_tests_properties(bazi PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME bazi_calendar_integration
    COMMAND test_bazi_calendar_integration ${PROJECT_SOURCE_DIR}/data
)
add_test(NAME bazi_c_api COMMAND test_bazi_c_api ${PROJECT_SOURCE_DIR}/data)
add_test(
    NAME bazi_c_api_static
    COMMAND test_bazi_c_api_static ${PROJECT_SOURCE_DIR}/data
)
