cmake_minimum_required(VERSION 3.16)

project(
    taiyin_ephemeris
    VERSION 1.0.0
    DESCRIPTION "Taiyin ephemeris runtime and stable C ABI"
    LANGUAGES C CXX
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/taiyin/c/base.h" TAIYIN_C_BASE_HEADER)
string(
    REGEX MATCH
    "#define TAIYIN_LIBRARY_VERSION_STRING \"([^\"]+)\""
    TAIYIN_LIBRARY_VERSION_MATCH
    "${TAIYIN_C_BASE_HEADER}"
)
set(TAIYIN_LIBRARY_VERSION_STRING "${CMAKE_MATCH_1}")
string(
    REGEX MATCH
    "#define TAIYIN_LIBRARY_VERSION_PRERELEASE \"([^\"]*)\""
    TAIYIN_LIBRARY_PRERELEASE_MATCH
    "${TAIYIN_C_BASE_HEADER}"
)
set(TAIYIN_LIBRARY_VERSION_PRERELEASE "${CMAKE_MATCH_1}")
set(TAIYIN_EXPECTED_LIBRARY_VERSION "${PROJECT_VERSION}")
if(NOT TAIYIN_LIBRARY_VERSION_PRERELEASE STREQUAL "")
    string(APPEND TAIYIN_EXPECTED_LIBRARY_VERSION
        "-${TAIYIN_LIBRARY_VERSION_PRERELEASE}")
endif()
if(NOT TAIYIN_LIBRARY_VERSION_STRING STREQUAL TAIYIN_EXPECTED_LIBRARY_VERSION)
    message(FATAL_ERROR
        "CMake project version ${PROJECT_VERSION} and prerelease "
        "${TAIYIN_LIBRARY_VERSION_PRERELEASE} do not match C ABI header "
        "version ${TAIYIN_LIBRARY_VERSION_STRING}")
endif()
string(
    REGEX MATCH
    "#define TAIYIN_C_ABI_VERSION ([0-9]+)u"
    TAIYIN_C_ABI_VERSION_MATCH
    "${TAIYIN_C_BASE_HEADER}"
)
if(NOT TAIYIN_C_ABI_VERSION_MATCH)
    message(FATAL_ERROR "Could not read TAIYIN_C_ABI_VERSION from C ABI header")
endif()
set(TAIYIN_C_ABI_VERSION "${CMAKE_MATCH_1}")

include(GNUInstallDirs)

find_package(Python3 COMPONENTS Interpreter QUIET)

option(
    TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS
    "Enable optional Chinese metaphysics extensions such as BaZi"
    OFF
)
option(
    TAIYIN_BUILD_BAZI_EXTENSION
    "Build the optional BaZi rule extension"
    OFF
)
option(
    TAIYIN_BUILD_MODULAR_C_API
    "Build the aggregate base C API library and optional shared extensions"
    OFF
)
option(
    TAIYIN_BUILD_MAINTAINER_TOOLS
    "Build private catalog generators, exporters, benchmarks, and fit experiments"
    OFF
)
if(TAIYIN_BUILD_MAINTAINER_TOOLS
    AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    message(FATAL_ERROR
        "TAIYIN_BUILD_MAINTAINER_TOOLS=ON requires the private tools directory")
endif()
if(TAIYIN_BUILD_BAZI_EXTENSION
    AND NOT TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS)
    message(FATAL_ERROR
        "TAIYIN_BUILD_BAZI_EXTENSION requires "
        "TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON")
endif()
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# MSVC exposes the non-standard M_PI family only when this is defined before
# <cmath>. Existing numerical kernels use those constants extensively.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

set(TAIYIN_NATIVE_CORE_SOURCES
    src/angle.cpp
    src/apparent_position.cpp
    src/builtin_loader.cpp
    src/chebyshev.cpp
    src/coordinates.cpp
    src/corrections.cpp
    src/dispatch.cpp
    src/dispatch_wrappers/frame_route.cpp
    src/dispatch_wrappers/nutation.cpp
    src/dispatch_wrappers/precession.cpp
    src/dispatch_wrappers/refraction.cpp
    src/dispatch_wrappers/tdb.cpp
    src/dispatch_wrappers/delta_t.cpp
    src/dispatch_wrappers/aberration.cpp
    src/dispatch_wrappers/heliacal_visibility.cpp
    src/dispatch_wrappers/lunar_eclipse.cpp
    src/earth_rotation.cpp
    src/eop.cpp
    src/custom_ephemeris_method.cpp
    src/descriptor_loader.cpp
    src/opm2.cpp
    src/opm2_catalog_discovery.cpp
    src/opc_catalog_persistent.cpp
    src/ephemeris_block.cpp
    src/ephemeris_catalog.cpp
    src/ephemeris_discovery.cpp
    src/ephemeris_file_loader.cpp
    src/ephemeris_route_rule.cpp
    src/ephemeris_segment_cache.cpp
    src/ephemeris_source_identity.cpp
    src/ephemeris_source_priority.cpp
    src/field_set.cpp
    src/geometry.cpp
    src/internal/base64.cpp
    src/internal/coordinate_model_data.cpp
    src/internal/delta_t_data.cpp
    src/internal/event_frame.cpp
    src/internal/erfa_cirs_subset.cpp
    src/internal/eop_finals2000a_data.cpp
    src/internal/gzip.cpp
    src/internal/mapped_file.cpp
    src/runtime/core/body_registry.cpp
    src/runtime/core/builtin_body_rules.cpp
    src/runtime/core/calc_spec.cpp
    src/runtime/core/ephemeris_engine.cpp
    src/runtime/core/native_context.cpp
    src/runtime/core/native_position.cpp
    src/runtime/core/runtime.cpp
    src/runtime/core/runtime_state_block_adapter.cpp
    src/runtime/apparent/fast_apparent.cpp
    src/runtime/apparent/major_body_apparent.cpp
    src/runtime/apparent/observed_position.cpp
    src/runtime/apparent/star_position.cpp
    src/runtime/visibility/heliacal_visibility.cpp
    src/runtime/visibility/heliacal_visibility_search.cpp
    src/runtime/visibility/moon_visibility.cpp
    src/runtime/visibility/planet_visibility.cpp
    src/runtime/visibility/solar_visibility.cpp
    src/runtime/visibility/star_visibility.cpp
    src/runtime/visibility/visibility_angle_search.cpp
    src/runtime/visibility/visibility_math.cpp
    src/runtime/visibility/visibility_sampling.cpp
    src/runtime/visibility/visibility_search.cpp
    src/runtime/events/event_search.cpp
    src/runtime/events/orbital_events.cpp
    src/runtime/events/phenomena.cpp
    src/runtime/events/solar_time.cpp
    src/runtime/eclipse/eclipse_search.cpp
    src/runtime/eclipse/eclipse_time.cpp
    src/runtime/eclipse/local_lunar_eclipse.cpp
    src/runtime/eclipse/local_solar_eclipse.cpp
    src/runtime/eclipse/lunar_eclipse_search.cpp
    src/runtime/eclipse/lunar_shadow_geometry.cpp
    src/runtime/eclipse/lunar_limb.cpp
    src/runtime/eclipse/solar_besselian.cpp
    src/runtime/eclipse/solar_eclipse_besselian_solver.cpp
    src/runtime/eclipse/solar_eclipse_search.cpp
    src/runtime/eclipse/solar_route_geometry.cpp
    src/runtime/eclipse/solar_shadow_geometry.cpp
    src/runtime/occultation/occultation_search.cpp
    src/runtime/occultation/occultation_geometry.cpp
    src/math_solvers.cpp
    src/interpolation.cpp
    src/kepler.cpp
    src/kepler_catalog_discovery.cpp
    src/kepler_catalog_tkc1.cpp
    src/kepler_file.cpp
    src/lunar_limb_tll1.cpp
    src/lunar_orientation.cpp
    src/observer.cpp
    src/route_inflight_map.cpp
    src/semi_analytic.cpp
    src/spk.cpp
    src/spk_catalog_discovery.cpp
    src/star_file.cpp
    src/star_provider_tsf1.cpp
    src/tkc1_catalog_discovery.cpp
    src/star_catalog_tsc1.cpp
    src/tsc1_catalog_discovery.cpp
    src/state.cpp
    src/third_party/miniz/miniz.c
    src/third_party/miniz/miniz_tdef.c
    src/third_party/miniz/miniz_tinfl.c
    src/third_party/miniz/miniz_zip.c
    src/time.cpp
    src/vector3.cpp
)

add_library(taiyin_ephemeris STATIC ${TAIYIN_NATIVE_CORE_SOURCES})

target_include_directories(taiyin_ephemeris
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/miniz
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(taiyin_ephemeris PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(TAIYIN_BUILD_MODULAR_C_API)
    function(taiyin_configure_modular_shared_target target)
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH_USE_ORIGIN ON
        )
        if(APPLE)
            set_target_properties(${target} PROPERTIES
                INSTALL_NAME_DIR "@rpath"
                INSTALL_RPATH "@loader_path"
            )
        elseif(UNIX)
            set_target_properties(${target} PROPERTIES
                INSTALL_RPATH "$ORIGIN"
            )
        endif()
    endfunction()

endif()

set(TAIYIN_ASTROLOGY_NATIVE_SOURCES
    src/astrology/ayanamsha_models.cpp
    src/astrology/houses.cpp
    src/astrology/house_systems.cpp
    src/astrology/lunar_points.cpp
    src/astrology/sidereal.cpp
    src/astrology/targets.cpp
)
add_library(taiyin_astrology_extension STATIC ${TAIYIN_ASTROLOGY_NATIVE_SOURCES})
target_link_libraries(taiyin_astrology_extension PUBLIC taiyin_ephemeris)
target_include_directories(taiyin_astrology_extension
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(taiyin_astrology_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

set(TAIYIN_CHINESE_CALENDAR_NATIVE_SOURCES src/chinese_calendar/calendar.cpp)
add_library(taiyin_chinese_calendar_extension STATIC
    ${TAIYIN_CHINESE_CALENDAR_NATIVE_SOURCES})
target_link_libraries(taiyin_chinese_calendar_extension PUBLIC taiyin_ephemeris)
target_include_directories(taiyin_chinese_calendar_extension
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(taiyin_chinese_calendar_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(taiyin_chinese_calendar_ganzhi_extension STATIC
    src/chinese_calendar/ganzhi.cpp
    src/chinese_calendar/ganzhi_rules.cpp
)
target_link_libraries(taiyin_chinese_calendar_ganzhi_extension
    PUBLIC taiyin_chinese_calendar_extension
)
target_include_directories(taiyin_chinese_calendar_ganzhi_extension
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(taiyin_chinese_calendar_ganzhi_extension PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

set(TAIYIN_C_CORE_API_SOURCES
    src/c_api/base.cpp
    src/c_api/context.cpp
    src/c_api/eclipse.cpp
    src/c_api/events.cpp
    src/c_api/heliacal.cpp
    src/c_api/observed.cpp
    src/c_api/occultation.cpp
    src/c_api/orbital.cpp
    src/c_api/phenomena.cpp
    src/c_api/position.cpp
    src/c_api/runtime.cpp
    src/c_api/solar_time.cpp
    src/c_api/star.cpp
    src/c_api/time.cpp
    src/c_api/visibility.cpp
)
set(TAIYIN_C_ASTROLOGY_API_SOURCES
    src/c_api/astrology.cpp
)
set(TAIYIN_C_CHINESE_CALENDAR_API_SOURCES
    src/c_api/chinese_calendar.cpp
)
set(TAIYIN_C_GANZHI_API_SOURCES
    src/c_api/chinese_calendar_ganzhi.cpp
)
set(TAIYIN_C_API_SOURCES
    ${TAIYIN_C_ASTROLOGY_API_SOURCES}
    ${TAIYIN_C_CORE_API_SOURCES}
    ${TAIYIN_C_CHINESE_CALENDAR_API_SOURCES}
    ${TAIYIN_C_GANZHI_API_SOURCES}
)

if(TAIYIN_BUILD_MODULAR_C_API)
    # Ganzhi is calendrical infrastructure, so it belongs in the base DLL.
    # BaZi remains the only optional shared extension.
    add_library(taiyin SHARED
        ${TAIYIN_NATIVE_CORE_SOURCES}
        ${TAIYIN_ASTROLOGY_NATIVE_SOURCES}
        ${TAIYIN_CHINESE_CALENDAR_NATIVE_SOURCES}
        src/chinese_calendar/ganzhi.cpp
        src/chinese_calendar/ganzhi_rules.cpp
        ${TAIYIN_C_CORE_API_SOURCES}
        ${TAIYIN_C_ASTROLOGY_API_SOURCES}
        ${TAIYIN_C_CHINESE_CALENDAR_API_SOURCES}
        ${TAIYIN_C_GANZHI_API_SOURCES}
    )
    target_include_directories(taiyin
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/c_api
            ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/miniz
    )
    target_compile_definitions(taiyin PRIVATE
        TAIYIN_C_BUILD
        TAIYIN_GANZHI_SHARED_BUILD
    )
    taiyin_configure_modular_shared_target(taiyin)
    set_target_properties(taiyin PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET default
        # Ganzhi and BaZi use the native calendar/runtime API internally.
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN YES
        VERSION ${TAIYIN_C_ABI_VERSION}.0.0
        SOVERSION ${TAIYIN_C_ABI_VERSION}
    )
    if(WIN32)
        set_target_properties(taiyin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()

else()
    add_library(taiyin_c SHARED ${TAIYIN_C_API_SOURCES})
    target_link_libraries(taiyin_c
        PRIVATE
            taiyin_astrology_extension
    )
    target_link_libraries(
        taiyin_c PRIVATE taiyin_chinese_calendar_ganzhi_extension)
    target_include_directories(taiyin_c
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/c_api
    )
    target_compile_definitions(taiyin_c PRIVATE TAIYIN_C_BUILD)
    if(WIN32)
        set(TAIYIN_C_SHARED_OUTPUT_NAME "taiyin-${TAIYIN_C_ABI_VERSION}")
    else()
        set(TAIYIN_C_SHARED_OUTPUT_NAME taiyin)
    endif()
    set_target_properties(taiyin_c PROPERTIES
        OUTPUT_NAME ${TAIYIN_C_SHARED_OUTPUT_NAME}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        VERSION ${TAIYIN_C_ABI_VERSION}.0.0
        SOVERSION ${TAIYIN_C_ABI_VERSION}
    )
endif()

add_library(taiyin_c_static STATIC ${TAIYIN_C_API_SOURCES})
target_link_libraries(taiyin_c_static
    PRIVATE
        taiyin_astrology_extension
)
target_link_libraries(
    taiyin_c_static PRIVATE taiyin_chinese_calendar_ganzhi_extension)
target_include_directories(taiyin_c_static
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/c_api
)
target_compile_definitions(taiyin_c_static PUBLIC TAIYIN_C_STATIC)
set_target_properties(taiyin_c_static PROPERTIES
    OUTPUT_NAME taiyin_static
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

enable_testing()

if(TAIYIN_BUILD_BAZI_EXTENSION)
    add_subdirectory(bazi_astrology)
endif()

if(TAIYIN_BUILD_MODULAR_C_API)
    install(
        TARGETS
            taiyin
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    if(TAIYIN_BUILD_BAZI_EXTENSION)
        install(
            TARGETS taiyin_bazi
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
else()
    install(
        TARGETS taiyin_c
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(
    DIRECTORY include/taiyin/c
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/taiyin
    FILES_MATCHING PATTERN "*.h"
)
install(
    FILES LICENSE NOTICE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/taiyin
)

set(CPACK_PACKAGE_NAME "taiyin")
set(CPACK_PACKAGE_VENDOR "RedSC1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)

set_source_files_properties(
    src/third_party/miniz/miniz_zip.c
    PROPERTIES COMPILE_DEFINITIONS "_POSIX_C_SOURCE=200809L"
)

add_executable(test_math_primitives
    tests/test_math_primitives.cpp
)
target_link_libraries(test_math_primitives PRIVATE taiyin_ephemeris)

add_executable(test_c_api
    tests/test_c_api.c
)
if(TAIYIN_BUILD_MODULAR_C_API)
    target_link_libraries(test_c_api PRIVATE taiyin)
else()
    target_link_libraries(test_c_api PRIVATE taiyin_c)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_c_api PRIVATE m)
endif()

add_executable(test_c_api_cpp
    tests/test_c_api_cpp.cpp
)
target_link_libraries(test_c_api_cpp PRIVATE taiyin_c_static)

add_executable(test_c_api_static
    tests/test_c_api.c
)
target_link_libraries(test_c_api_static PRIVATE taiyin_c_static)
if(TAIYIN_BUILD_BAZI_EXTENSION)
    if(TAIYIN_BUILD_MODULAR_C_API)
        target_compile_definitions(test_c_api PRIVATE TAIYIN_TEST_EXPECT_BAZI=0)
    else()
        target_compile_definitions(test_c_api PRIVATE TAIYIN_TEST_EXPECT_BAZI=1)
    endif()
    target_compile_definitions(test_c_api_static PRIVATE TAIYIN_TEST_EXPECT_BAZI=1)
else()
    target_compile_definitions(test_c_api PRIVATE TAIYIN_TEST_EXPECT_BAZI=0)
    target_compile_definitions(test_c_api_static PRIVATE TAIYIN_TEST_EXPECT_BAZI=0)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_c_api_static PRIVATE m)
endif()

add_executable(test_field_set
    tests/test_field_set.cpp
)
target_link_libraries(test_field_set PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_catalog
    tests/test_ephemeris_catalog.cpp
)
target_link_libraries(test_ephemeris_catalog PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_segment_cache
    tests/test_ephemeris_segment_cache.cpp
)
target_link_libraries(test_ephemeris_segment_cache PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_route_rule
    tests/test_ephemeris_route_rule.cpp
)
target_link_libraries(test_ephemeris_route_rule PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_source_identity
    tests/test_ephemeris_source_identity.cpp
)
target_link_libraries(test_ephemeris_source_identity PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_routing_matrix
    tests/test_ephemeris_routing_matrix.cpp
)
target_link_libraries(test_ephemeris_routing_matrix PRIVATE taiyin_ephemeris)

add_executable(test_semi_analytic
    tests/test_semi_analytic.cpp
)
target_link_libraries(test_semi_analytic PRIVATE taiyin_ephemeris)

add_executable(test_custom_ephemeris_method
    tests/test_custom_ephemeris_method.cpp
)
target_link_libraries(test_custom_ephemeris_method PRIVATE taiyin_ephemeris)

add_executable(test_global_ephemeris_runtime
    tests/test_global_ephemeris_runtime.cpp
)
target_link_libraries(test_global_ephemeris_runtime PRIVATE taiyin_ephemeris)

add_executable(test_discovery_descriptors
    tests/test_discovery_descriptors.cpp
)
target_link_libraries(test_discovery_descriptors PRIVATE taiyin_ephemeris)

add_executable(test_spk
    tests/test_spk.cpp
)
target_link_libraries(test_spk PRIVATE taiyin_ephemeris)

add_executable(oracle_state_driver
    tests/oracle_state_driver.cpp
)
target_link_libraries(oracle_state_driver PRIVATE taiyin_ephemeris)

add_executable(test_kepler
    tests/test_kepler.cpp
)
target_link_libraries(test_kepler PRIVATE taiyin_ephemeris)

add_executable(test_kepler_file
    tests/test_kepler_file.cpp
)
target_link_libraries(test_kepler_file PRIVATE taiyin_ephemeris)

add_executable(test_kepler_catalog_tkc1
    tests/test_kepler_catalog_tkc1.cpp
)
target_link_libraries(test_kepler_catalog_tkc1 PRIVATE taiyin_ephemeris)

add_executable(test_corrections
    tests/test_corrections.cpp
)
target_link_libraries(test_corrections PRIVATE taiyin_ephemeris)

add_executable(test_corrections_oracles
    tests/test_corrections_oracles.cpp
)
target_link_libraries(test_corrections_oracles PRIVATE taiyin_ephemeris)

add_executable(test_geometry
    tests/test_geometry.cpp
)
target_link_libraries(test_geometry PRIVATE taiyin_ephemeris)

add_executable(test_coordinates
    tests/test_coordinates.cpp
)
target_link_libraries(test_coordinates PRIVATE taiyin_ephemeris)

add_executable(test_sidereal_astrology
    tests/test_sidereal_astrology.cpp
)
target_link_libraries(test_sidereal_astrology PRIVATE taiyin_astrology_extension)

add_executable(test_houses_astrology
    tests/test_houses_astrology.cpp
)
target_link_libraries(test_houses_astrology PRIVATE taiyin_astrology_extension)
target_include_directories(test_houses_astrology PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/astrology)

add_executable(test_solar_time
    tests/test_solar_time.cpp
)
target_link_libraries(test_solar_time PRIVATE taiyin_ephemeris)

add_executable(test_lunar_points_astrology
    tests/test_lunar_points_astrology.cpp
)
target_link_libraries(test_lunar_points_astrology PRIVATE taiyin_astrology_extension)

add_executable(test_chinese_calendar
    tests/test_chinese_calendar.cpp
)

add_executable(test_ganzhi_calendar tests/test_ganzhi_calendar.cpp)
target_link_libraries(
    test_ganzhi_calendar PRIVATE taiyin_chinese_calendar_ganzhi_extension)

add_executable(test_ganzhi_calendar_c_api tests/test_ganzhi_calendar_c_api.c)
if(TAIYIN_BUILD_MODULAR_C_API)
    target_link_libraries(test_ganzhi_calendar_c_api PRIVATE taiyin)
else()
    target_link_libraries(test_ganzhi_calendar_c_api PRIVATE taiyin_c)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_ganzhi_calendar_c_api PRIVATE m)
endif()

add_executable(test_ganzhi_calendar_c_api_static tests/test_ganzhi_calendar_c_api.c)
target_link_libraries(test_ganzhi_calendar_c_api_static PRIVATE taiyin_c_static)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_ganzhi_calendar_c_api_static PRIVATE m)
endif()

target_include_directories(
    test_chinese_calendar
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    test_chinese_calendar
    PRIVATE taiyin_chinese_calendar_extension
)

add_executable(test_low_level_oracles
    tests/test_low_level_oracles.cpp
)
target_link_libraries(test_low_level_oracles PRIVATE taiyin_ephemeris)

add_executable(test_pure_functions_full
    tests/test_pure_functions_full.cpp
)
target_link_libraries(test_pure_functions_full PRIVATE taiyin_ephemeris)

add_executable(test_time_angle_interpolation
    tests/test_time_angle_interpolation.cpp
)
target_link_libraries(test_time_angle_interpolation PRIVATE taiyin_ephemeris)

add_executable(test_eop
    tests/test_eop.cpp
)
target_link_libraries(test_eop PRIVATE taiyin_ephemeris)

add_executable(test_star_file
    tests/test_star_file.cpp
)
target_link_libraries(test_star_file PRIVATE taiyin_ephemeris)

add_executable(test_tsc1_catalog_discovery
    tests/test_tsc1_catalog_discovery.cpp
)
target_link_libraries(test_tsc1_catalog_discovery PRIVATE taiyin_ephemeris)

add_executable(test_mapped_file
    tests/test_mapped_file.cpp
)
target_link_libraries(test_mapped_file PRIVATE taiyin_ephemeris)

add_executable(test_math_solvers
    tests/test_math_solvers.cpp
)
target_link_libraries(test_math_solvers PRIVATE taiyin_ephemeris)

add_executable(test_celestial_body_registry
    tests/test_celestial_body_registry.cpp
)
target_link_libraries(test_celestial_body_registry PRIVATE taiyin_ephemeris)

add_executable(test_calc_spec
    tests/test_calc_spec.cpp
)
target_link_libraries(test_calc_spec PRIVATE taiyin_ephemeris)

add_executable(test_opc_catalog_persistent
    tests/test_opc_catalog_persistent.cpp
)
target_link_libraries(test_opc_catalog_persistent PRIVATE taiyin_ephemeris)

add_executable(test_opm2_staged_data
    tests/test_opm2_staged_data.cpp
)
target_link_libraries(test_opm2_staged_data PRIVATE taiyin_ephemeris)

add_executable(test_opm2_spk_cob_oracles
    tests/test_opm2_spk_cob_oracles.cpp
)
target_link_libraries(test_opm2_spk_cob_oracles PRIVATE taiyin_ephemeris)

add_executable(test_native_apparent_runtime
    tests/test_native_apparent_runtime.cpp
)
target_link_libraries(test_native_apparent_runtime PRIVATE taiyin_ephemeris)

add_executable(test_event_search
    tests/test_event_search.cpp
)
target_link_libraries(test_event_search PRIVATE taiyin_ephemeris)

add_executable(test_orbital_events
    tests/test_orbital_events.cpp
)
target_link_libraries(test_orbital_events PRIVATE taiyin_ephemeris)

add_executable(test_phenomena
    tests/test_phenomena.cpp
)
target_link_libraries(test_phenomena PRIVATE taiyin_ephemeris)
target_include_directories(test_phenomena PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(test_visibility_math
    tests/test_visibility_math.cpp
)
target_include_directories(test_visibility_math PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_visibility_math PRIVATE taiyin_ephemeris)

add_executable(test_visibility_sampling
    tests/test_visibility_sampling.cpp
)
target_include_directories(test_visibility_sampling PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_visibility_sampling PRIVATE taiyin_ephemeris)

add_executable(test_visibility_search
    tests/test_visibility_search.cpp
)
target_include_directories(test_visibility_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_visibility_search PRIVATE taiyin_ephemeris)

add_executable(test_solar_visibility
    tests/test_solar_visibility.cpp
)
target_include_directories(test_solar_visibility PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_solar_visibility PRIVATE taiyin_ephemeris)

add_executable(test_solar_visibility_public
    tests/test_solar_visibility_public.cpp
)
target_link_libraries(test_solar_visibility_public PRIVATE taiyin_ephemeris)

add_executable(test_moon_visibility
    tests/test_moon_visibility.cpp
)
target_include_directories(test_moon_visibility PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_moon_visibility PRIVATE taiyin_ephemeris)

add_executable(test_moon_visibility_public
    tests/test_moon_visibility_public.cpp
)
target_link_libraries(test_moon_visibility_public PRIVATE taiyin_ephemeris)

add_executable(test_star_visibility_public
    tests/test_star_visibility_public.cpp
)
target_link_libraries(test_star_visibility_public PRIVATE taiyin_ephemeris)

add_executable(test_heliacal_visibility
    tests/test_heliacal_visibility.cpp
)
target_link_libraries(test_heliacal_visibility PRIVATE taiyin_ephemeris)

add_executable(test_occultation_search
    tests/test_occultation_search.cpp
)
target_link_libraries(test_occultation_search PRIVATE taiyin_ephemeris)

add_executable(test_planet_visibility
    tests/test_planet_visibility.cpp
)
target_include_directories(test_planet_visibility PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_planet_visibility PRIVATE taiyin_ephemeris)

add_executable(test_planet_visibility_spk_oracles
    tests/test_planet_visibility_spk_oracles.cpp
)
target_include_directories(test_planet_visibility_spk_oracles PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_planet_visibility_spk_oracles PRIVATE taiyin_ephemeris)

add_executable(test_planet_visibility_public
    tests/test_planet_visibility_public.cpp
)
target_link_libraries(test_planet_visibility_public PRIVATE taiyin_ephemeris)

add_executable(test_eclipse_search_smoke
    tests/test_eclipse_search_smoke.cpp
)
target_link_libraries(test_eclipse_search_smoke PRIVATE taiyin_ephemeris)

add_executable(test_eclipse_search_spk_oracles
    tests/test_eclipse_search_spk_oracles.cpp
)
target_include_directories(test_eclipse_search_spk_oracles PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_eclipse_search_spk_oracles PRIVATE taiyin_ephemeris)

add_executable(test_eclipse_ts_regression
    tests/test_eclipse_ts_regression.cpp
)
target_link_libraries(test_eclipse_ts_regression PRIVATE taiyin_ephemeris)

add_executable(test_sxwnl_solar_oracles
    tests/test_sxwnl_solar_oracles.cpp
    legacy/sxwnl/eclipse/solar_eclipse_sxwnl.cpp
)
target_include_directories(test_sxwnl_solar_oracles PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/sxwnl/eclipse
)
target_link_libraries(test_sxwnl_solar_oracles PRIVATE taiyin_ephemeris)

add_executable(test_sxwnl_lunar_oracles
    tests/test_sxwnl_lunar_oracles.cpp
    legacy/sxwnl/eclipse/lunar_eclipse_sxwnl.cpp
)
target_include_directories(test_sxwnl_lunar_oracles PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/sxwnl/eclipse
)
target_link_libraries(test_sxwnl_lunar_oracles PRIVATE taiyin_ephemeris)

add_executable(test_lunar_shadow_geometry
    tests/test_lunar_shadow_geometry.cpp
)
target_include_directories(test_lunar_shadow_geometry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_lunar_shadow_geometry PRIVATE taiyin_ephemeris)

add_executable(test_solar_shadow_geometry
    tests/test_solar_shadow_geometry.cpp
    legacy/sxwnl/eclipse/solar_eclipse_sxwnl.cpp
)
target_include_directories(test_solar_shadow_geometry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/sxwnl/eclipse
)
target_link_libraries(test_solar_shadow_geometry PRIVATE taiyin_ephemeris)

add_executable(test_solar_direct_solver
    tests/test_solar_direct_solver.cpp
)
target_include_directories(test_solar_direct_solver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_solar_direct_solver PRIVATE taiyin_ephemeris)

add_executable(test_lunar_eclipse_model_comparison
    tests/test_lunar_eclipse_model_comparison.cpp
    legacy/sxwnl/eclipse/lunar_eclipse_sxwnl.cpp
)
target_include_directories(test_lunar_eclipse_model_comparison PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/sxwnl/eclipse
)
target_link_libraries(test_lunar_eclipse_model_comparison PRIVATE taiyin_ephemeris)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private/tests/test_event_search_swiss.cpp)
    add_executable(test_event_search_swiss
        private/tests/test_event_search_swiss.cpp
    )
    target_include_directories(test_event_search_swiss PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_event_search_swiss PRIVATE taiyin_ephemeris)
endif()

add_executable(test_orbital_events_swiss
    tests/test_orbital_events_swiss.cpp
)
target_include_directories(test_orbital_events_swiss PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_orbital_events_swiss PRIVATE taiyin_ephemeris)
if(Python3_Interpreter_FOUND)
    target_compile_definitions(test_orbital_events_swiss PRIVATE
        TAIYIN_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private/tests/eclipse_external_compare.cpp)
    add_executable(eclipse_external_compare
        private/tests/eclipse_external_compare.cpp
    )
    target_link_libraries(eclipse_external_compare PRIVATE taiyin_ephemeris)
endif()

add_executable(test_writer_preferred_rwlock
    tests/test_writer_preferred_rwlock.cpp
)
target_link_libraries(test_writer_preferred_rwlock PRIVATE taiyin_ephemeris)

add_executable(test_route_inflight_map
    tests/test_route_inflight_map.cpp
)
target_link_libraries(test_route_inflight_map PRIVATE taiyin_ephemeris)

add_executable(test_body_registry
    tests/test_body_registry.cpp
)
target_link_libraries(test_body_registry PRIVATE taiyin_ephemeris)

add_executable(test_dispatch_models
    tests/test_dispatch_models.cpp
)
target_link_libraries(test_dispatch_models PRIVATE taiyin_ephemeris)

add_executable(test_lunar_limb_tll1
    tests/test_lunar_limb_tll1.cpp
)
target_link_libraries(test_lunar_limb_tll1 PRIVATE taiyin_ephemeris)

add_executable(test_lunar_orientation
    tests/test_lunar_orientation.cpp
)
target_link_libraries(test_lunar_orientation PRIVATE taiyin_ephemeris)

add_executable(test_lunar_limb_eclipse
    tests/test_lunar_limb_eclipse.cpp
)
target_link_libraries(test_lunar_limb_eclipse PRIVATE taiyin_ephemeris)

add_executable(test_event_frame
    tests/test_event_frame.cpp
)
target_link_libraries(test_event_frame PRIVATE taiyin_ephemeris)

add_executable(test_apparent_position
    tests/test_apparent_position.cpp
)
target_link_libraries(test_apparent_position PRIVATE taiyin_ephemeris)

add_executable(test_apparent_position_oracles
    tests/test_apparent_position_oracles.cpp
)
target_link_libraries(test_apparent_position_oracles PRIVATE taiyin_ephemeris)

add_executable(example_observed_ut_bare_chart
    examples/observed_ut_bare_chart.cpp
)
target_link_libraries(example_observed_ut_bare_chart PRIVATE taiyin_ephemeris)

add_executable(example_observed_utc_eop_bare_chart
    examples/observed_utc_eop_bare_chart.cpp
)
target_link_libraries(example_observed_utc_eop_bare_chart PRIVATE taiyin_ephemeris)

add_executable(example_apparent_ut_chart_table
    examples/apparent_ut_chart_table.cpp
)
target_link_libraries(example_apparent_ut_chart_table PRIVATE taiyin_ephemeris)

add_executable(example_eclipse_search
    examples/eclipse_search.cpp
)
target_link_libraries(example_eclipse_search PRIVATE taiyin_ephemeris)

add_executable(example_chinese_calendar_han
    examples/chinese_calendar_han.cpp
)
target_link_libraries(example_chinese_calendar_han
    PRIVATE taiyin_chinese_calendar_extension)

add_executable(example_ganzhi_bce
    examples/ganzhi_bce.cpp
)
target_link_libraries(example_ganzhi_bce
    PRIVATE taiyin_chinese_calendar_ganzhi_extension)

add_executable(example_muye_jupiter
    examples/muye_jupiter.cpp
)
target_link_libraries(example_muye_jupiter PRIVATE taiyin_ephemeris)

add_executable(example_tang_833_antares
    examples/tang_833_antares.cpp
)
target_link_libraries(example_tang_833_antares
    PRIVATE taiyin_chinese_calendar_ganzhi_extension)

if(TAIYIN_BUILD_MAINTAINER_TOOLS)
    add_executable(generate_ephemeris_catalog
        tools/generate_ephemeris_catalog.cpp
    )
    target_link_libraries(generate_ephemeris_catalog PRIVATE taiyin_ephemeris)

    add_executable(taiyin_solar_eclipse_export
        tools/taiyin_solar_eclipse_export.cpp
    )
    target_link_libraries(taiyin_solar_eclipse_export PRIVATE taiyin_ephemeris ${CMAKE_DL_LIBS})

    add_executable(taiyin_opm2_loader_bench
        tools/taiyin_opm2_loader_bench.cpp
    )
    target_link_libraries(taiyin_opm2_loader_bench PRIVATE taiyin_ephemeris ${CMAKE_DL_LIBS})

    add_executable(taiyin_chart_random_bench
        tools/taiyin_chart_random_bench.cpp
    )
    target_link_libraries(taiyin_chart_random_bench PRIVATE taiyin_ephemeris ${CMAKE_DL_LIBS})

    add_executable(taiyin_single_body_trace_bench
        tools/taiyin_single_body_trace_bench.cpp
    )
    target_link_libraries(taiyin_single_body_trace_bench PRIVATE taiyin_ephemeris ${CMAKE_DL_LIBS})

    add_executable(taiyin_eclipse_bench
        benchmark/taiyin_eclipse_bench.cpp
    )
    target_link_libraries(taiyin_eclipse_bench PRIVATE taiyin_ephemeris ${CMAKE_DL_LIBS})

    add_executable(lunar_shadow_fit_experiment
        tools/lunar_shadow_fit_experiment.cpp
    )
    target_include_directories(lunar_shadow_fit_experiment PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(lunar_shadow_fit_experiment PRIVATE taiyin_ephemeris)

    add_executable(lunar_apparent_window_bench
        tools/lunar_apparent_window_bench.cpp
    )
    target_include_directories(lunar_apparent_window_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(lunar_apparent_window_bench PRIVATE taiyin_ephemeris)
endif()



add_test(NAME math_primitives COMMAND test_math_primitives)
add_test(NAME c_api COMMAND test_c_api ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(NAME c_api_cpp COMMAND test_c_api_cpp)
add_test(NAME c_api_static COMMAND test_c_api_static ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(NAME field_set COMMAND test_field_set)
add_test(NAME ephemeris_catalog COMMAND test_ephemeris_catalog)
add_test(NAME ephemeris_segment_cache COMMAND test_ephemeris_segment_cache)
add_test(NAME ephemeris_route_rule COMMAND test_ephemeris_route_rule)
add_test(NAME ephemeris_source_identity COMMAND test_ephemeris_source_identity)
add_test(NAME ephemeris_routing_matrix COMMAND test_ephemeris_routing_matrix)
add_test(NAME semi_analytic COMMAND test_semi_analytic)
set_tests_properties(semi_analytic PROPERTIES
    ENVIRONMENT "TAIYIN_DATA_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME custom_ephemeris_method COMMAND test_custom_ephemeris_method)
add_test(NAME global_ephemeris_runtime COMMAND test_global_ephemeris_runtime)
set_tests_properties(global_ephemeris_runtime PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME discovery_descriptors COMMAND test_discovery_descriptors)
add_test(NAME spk COMMAND test_spk)
if(Python3_Interpreter_FOUND)
    if(TAIYIN_BUILD_MAINTAINER_TOOLS)
        add_test(
            NAME tsc1_lite_catalog
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tsc1_lite_catalog.py
        )
    endif()
    add_test(
        NAME spk_opm2_jplephem_oracles
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spk_opm2_jplephem_oracles.py
            $<TARGET_FILE:oracle_state_driver>
    )
    if(TAIYIN_BUILD_MAINTAINER_TOOLS)
        add_test(
            NAME satellite_fit_tool_contracts
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_satellite_fit_tool_contracts.py
        )
    endif()
endif()
add_test(NAME kepler COMMAND test_kepler)
add_test(NAME kepler_file COMMAND test_kepler_file)
add_test(NAME kepler_catalog_tkc1 COMMAND test_kepler_catalog_tkc1)
add_test(NAME corrections COMMAND test_corrections)
add_test(NAME corrections_oracles COMMAND test_corrections_oracles)
add_test(NAME geometry COMMAND test_geometry)
add_test(NAME coordinates COMMAND test_coordinates)
add_test(NAME sidereal_astrology COMMAND test_sidereal_astrology)
add_test(NAME houses_astrology COMMAND test_houses_astrology)
add_test(NAME lunar_points_astrology COMMAND test_lunar_points_astrology)
add_test(NAME chinese_calendar COMMAND test_chinese_calendar)
add_test(NAME ganzhi_calendar COMMAND test_ganzhi_calendar)
set_tests_properties(ganzhi_calendar PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME ganzhi_calendar_c_api
    COMMAND test_ganzhi_calendar_c_api ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME ganzhi_calendar_c_api_static
    COMMAND test_ganzhi_calendar_c_api_static ${CMAKE_CURRENT_SOURCE_DIR}/data)
set_tests_properties(sidereal_astrology PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(lunar_points_astrology PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(chinese_calendar PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
if(UNIX)
    if(TAIYIN_BUILD_MODULAR_C_API)
        add_test(
            NAME c_api_export_symbols_base
            COMMAND ${CMAKE_COMMAND}
                -DTAIYIN_NM=${CMAKE_NM}
                -DTAIYIN_LIBRARY=$<TARGET_FILE:taiyin>
                -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
                -DTAIYIN_EXPECT_BAZI=OFF
                -DTAIYIN_ALLOW_CPP_SYMBOLS=ON
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
        )
        add_test(
            NAME c_api_export_symbols_base_internal
            COMMAND ${CMAKE_COMMAND}
                -DTAIYIN_NM=${CMAKE_NM}
                -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
                -DTAIYIN_RUNTIME_LIBRARY=$<TARGET_FILE:taiyin>
                "-DTAIYIN_RUNTIME_FORBIDDEN_REGEX=(^|\\n)[^\\n]*[_]*ZN6taiyin16chinese_calendar5rules"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
        )
        if(TAIYIN_BUILD_BAZI_EXTENSION)
            add_test(
                NAME c_api_export_symbols_bazi
                COMMAND ${CMAKE_COMMAND}
                    -DTAIYIN_NM=${CMAKE_NM}
                    -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
                    -DTAIYIN_MODULE_LIBRARIES=$<TARGET_FILE:taiyin_bazi>
                    -DTAIYIN_MODULE_LABELS=bazi
                    "-DTAIYIN_MODULE_EXPECTED_REGEXES=^_?taiyin_bazi_"
                    "-DTAIYIN_MODULE_FORBIDDEN_REGEXES=^_?taiyin_(astrology|chinese_calendar|ganzhi)_"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
            )
        endif()
    else()
        add_test(
            NAME c_api_export_symbols
            COMMAND ${CMAKE_COMMAND}
                -DTAIYIN_NM=${CMAKE_NM}
                -DTAIYIN_LIBRARY=$<TARGET_FILE:taiyin_c>
                -DTAIYIN_PLATFORM=${CMAKE_SYSTEM_NAME}
                -DTAIYIN_EXPECT_BAZI=${TAIYIN_BUILD_BAZI_EXTENSION}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckTaiyinCExports.cmake
        )
    endif()
endif()
add_test(NAME low_level_oracles COMMAND test_low_level_oracles)
add_test(NAME pure_functions_full COMMAND test_pure_functions_full)
add_test(NAME time_angle_interpolation COMMAND test_time_angle_interpolation)
add_test(NAME eop COMMAND test_eop)
add_test(NAME star_file COMMAND test_star_file)
add_test(NAME tsc1_catalog_discovery COMMAND test_tsc1_catalog_discovery)
add_test(NAME mapped_file COMMAND test_mapped_file)
add_test(NAME math_solvers COMMAND test_math_solvers)
add_test(NAME celestial_body_registry COMMAND test_celestial_body_registry)
add_test(NAME calc_spec COMMAND test_calc_spec)
add_test(NAME opm2_staged_data COMMAND test_opm2_staged_data)
add_test(NAME opm2_spk_cob_oracles COMMAND test_opm2_spk_cob_oracles)
add_test(NAME opc_catalog_persistent COMMAND test_opc_catalog_persistent)
add_test(NAME native_apparent_runtime COMMAND test_native_apparent_runtime)
add_test(NAME event_search COMMAND test_event_search)
add_test(NAME orbital_events COMMAND test_orbital_events)
set_tests_properties(orbital_events PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME solar_time COMMAND test_solar_time)
set_tests_properties(solar_time PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME phenomena COMMAND test_phenomena)
set_tests_properties(phenomena PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME visibility_math COMMAND test_visibility_math)
add_test(NAME visibility_sampling COMMAND test_visibility_sampling)
add_test(NAME visibility_search COMMAND test_visibility_search)
add_test(NAME solar_visibility COMMAND test_solar_visibility)
add_test(NAME solar_visibility_public COMMAND test_solar_visibility_public)
add_test(NAME moon_visibility COMMAND test_moon_visibility)
add_test(NAME moon_visibility_public COMMAND test_moon_visibility_public)
add_test(NAME star_visibility_public COMMAND test_star_visibility_public)
set_tests_properties(star_visibility_public PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME heliacal_visibility COMMAND test_heliacal_visibility)
set_tests_properties(heliacal_visibility PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME occultation_search COMMAND test_occultation_search)
set_tests_properties(occultation_search PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME planet_visibility COMMAND test_planet_visibility)
add_test(NAME planet_visibility_spk_oracles COMMAND test_planet_visibility_spk_oracles)
add_test(NAME planet_visibility_public COMMAND test_planet_visibility_public)
add_test(NAME eclipse_search_smoke COMMAND test_eclipse_search_smoke)
set_tests_properties(eclipse_search_smoke PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME eclipse_search_spk_oracles COMMAND test_eclipse_search_spk_oracles)
add_test(NAME sxwnl_solar_oracles COMMAND test_sxwnl_solar_oracles)
if(Python3_Interpreter_FOUND AND TAIYIN_BUILD_MAINTAINER_TOOLS)
    add_test(
        NAME eclipse_forecast_json
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_eclipse_forecast_json.py
            $<TARGET_FILE:taiyin_solar_eclipse_export>
            ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
endif()
if(TARGET test_event_search_swiss)
    add_test(NAME event_search_swiss COMMAND test_event_search_swiss)
endif()
add_test(NAME orbital_events_swiss COMMAND test_orbital_events_swiss)
add_test(NAME writer_preferred_rwlock COMMAND test_writer_preferred_rwlock)
add_test(NAME route_inflight_map COMMAND test_route_inflight_map)
add_test(NAME body_registry COMMAND test_body_registry)
add_test(NAME dispatch_models COMMAND test_dispatch_models)
add_test(NAME lunar_limb_tll1 COMMAND test_lunar_limb_tll1)
set_tests_properties(lunar_limb_tll1 PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME lunar_orientation COMMAND test_lunar_orientation)
set_tests_properties(lunar_orientation PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME lunar_limb_eclipse COMMAND test_lunar_limb_eclipse)
set_tests_properties(lunar_limb_eclipse PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME sxwnl_lunar_oracles COMMAND test_sxwnl_lunar_oracles)
add_test(NAME lunar_shadow_geometry COMMAND test_lunar_shadow_geometry)
set_tests_properties(lunar_shadow_geometry PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME solar_shadow_geometry COMMAND test_solar_shadow_geometry)
add_test(NAME solar_direct_solver COMMAND test_solar_direct_solver)
set_tests_properties(solar_direct_solver PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME lunar_eclipse_model_comparison COMMAND test_lunar_eclipse_model_comparison)
set_tests_properties(lunar_eclipse_model_comparison PROPERTIES
    ENVIRONMENT "TAIYIN_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME event_frame COMMAND test_event_frame)
add_test(NAME apparent_position COMMAND test_apparent_position)
add_test(NAME apparent_position_oracles COMMAND test_apparent_position_oracles)

add_test(
    NAME example_observed_ut_bare_chart_run
    COMMAND example_observed_ut_bare_chart ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_observed_utc_eop_bare_chart_run
    COMMAND example_observed_utc_eop_bare_chart ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_apparent_ut_chart_table_run
    COMMAND example_apparent_ut_chart_table ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_eclipse_search_run
    COMMAND example_eclipse_search ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_chinese_calendar_han_run
    COMMAND example_chinese_calendar_han ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_ganzhi_bce_run
    COMMAND example_ganzhi_bce ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_muye_jupiter_run
    COMMAND example_muye_jupiter ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(
    NAME example_tang_833_antares_run
    COMMAND example_tang_833_antares ${CMAKE_CURRENT_SOURCE_DIR}/data)
set_tests_properties(
    example_observed_ut_bare_chart_run
    example_observed_utc_eop_bare_chart_run
    example_apparent_ut_chart_table_run
    example_eclipse_search_run
    example_chinese_calendar_han_run
    example_ganzhi_bce_run
    example_muye_jupiter_run
    example_tang_833_antares_run
    PROPERTIES LABELS examples)


add_executable(test_apparent_self_skip
    tests/test_apparent_self_skip.cpp
)
target_link_libraries(test_apparent_self_skip PRIVATE taiyin_ephemeris)
add_test(NAME apparent_self_skip COMMAND test_apparent_self_skip)

set_tests_properties(
    ephemeris_catalog
    ephemeris_segment_cache
    PROPERTIES TIMEOUT 10
)
