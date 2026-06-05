cmake_minimum_required(VERSION 3.16)

project(taiyin_ephemeris LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(taiyin_ephemeris STATIC
    src/angle.cpp
    src/apparent_position.cpp
    src/builtin_loader.cpp
    src/chebyshev.cpp
    src/coordinates.cpp
    src/corrections.cpp
    src/custom_ephemeris_source_registry.cpp
    src/dispatch.cpp
    src/dispatch_wrappers/frame_route.cpp
    src/dispatch_wrappers/nutation.cpp
    src/dispatch_wrappers/precession.cpp
    src/dispatch_wrappers/refraction.cpp
    src/dispatch_wrappers/tdb.cpp
    src/earth_rotation.cpp
    src/eop.cpp
    src/descriptor_loader.cpp
    src/ephemeris_block.cpp
    src/ephemeris_cache.cpp
    src/ephemeris_catalog.cpp
    src/ephemeris_discovery.cpp
    src/ephemeris_file_loader.cpp
    src/geometry.cpp
    src/internal/base64.cpp
    src/internal/coordinate_model_data.cpp
    src/internal/delta_t_data.cpp
    src/internal/erfa_cirs_subset.cpp
    src/internal/eop_finals2000a_data.cpp
    src/internal/gzip.cpp
    src/internal/mapped_file.cpp
    src/runtime/runtime_registry.cpp
    src/runtime/service_locator.cpp
    src/runtime/ephemeris_service.cpp
    src/runtime/major_body_apparent.cpp
    src/runtime/pipeline.cpp
    src/runtime/taiyin_runtime.cpp
    src/math_solvers.cpp
    src/interpolation.cpp
    src/kepler.cpp
    src/kepler_catalog_discovery.cpp
    src/kepler_catalog_tkc1.cpp
    src/kepler_file.cpp
    src/kepler_source_registry.cpp
    src/moshier.cpp
    src/moshier_builtin.cpp
    src/observer.cpp
    src/opm_core.cpp
    src/opm4.cpp
    src/opm4_catalog_discovery.cpp
    src/route_inflight_map.cpp
    src/spk.cpp
    src/spk_catalog_discovery.cpp
    src/star_file.cpp
    src/star_provider_tsf1.cpp
    src/stars.cpp
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

target_include_directories(taiyin_ephemeris
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/miniz
)

enable_testing()

add_executable(test_opm4
    tests/test_opm4.cpp
)
target_link_libraries(test_opm4 PRIVATE taiyin_ephemeris)

add_executable(test_math_primitives
    tests/test_math_primitives.cpp
)
target_link_libraries(test_math_primitives PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_block
    tests/test_ephemeris_block.cpp
)
target_link_libraries(test_ephemeris_block PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_catalog
    tests/test_ephemeris_catalog.cpp
)
target_link_libraries(test_ephemeris_catalog PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_cache
    tests/test_ephemeris_cache.cpp
)
target_link_libraries(test_ephemeris_cache PRIVATE taiyin_ephemeris)

add_executable(test_descriptor_loader
    tests/test_descriptor_loader.cpp
)
target_link_libraries(test_descriptor_loader PRIVATE taiyin_ephemeris)

add_executable(test_opm4_file_loader
    tests/test_opm4_file_loader.cpp
)
target_link_libraries(test_opm4_file_loader PRIVATE taiyin_ephemeris)

add_executable(test_opm4_catalog_discovery
    tests/test_opm4_catalog_discovery.cpp
)
target_link_libraries(test_opm4_catalog_discovery PRIVATE taiyin_ephemeris)

add_executable(test_spk_catalog_discovery
    tests/test_spk_catalog_discovery.cpp
)
target_link_libraries(test_spk_catalog_discovery PRIVATE taiyin_ephemeris)

add_executable(test_spk_jplephem_oracles
    tests/test_spk_jplephem_oracles.cpp
)
target_link_libraries(test_spk_jplephem_oracles PRIVATE taiyin_ephemeris)

add_executable(test_spk
    tests/test_spk.cpp
)
target_link_libraries(test_spk PRIVATE taiyin_ephemeris)

add_executable(test_kepler
    tests/test_kepler.cpp
)
target_link_libraries(test_kepler PRIVATE taiyin_ephemeris)

add_executable(test_kepler_file
    tests/test_kepler_file.cpp
)
target_link_libraries(test_kepler_file PRIVATE taiyin_ephemeris)

add_executable(test_kepler_catalog_discovery
    tests/test_kepler_catalog_discovery.cpp
)
target_link_libraries(test_kepler_catalog_discovery PRIVATE taiyin_ephemeris)

add_executable(test_kepler_source_registry
    tests/test_kepler_source_registry.cpp
)
target_link_libraries(test_kepler_source_registry PRIVATE taiyin_ephemeris)

add_executable(test_kepler_catalog_tkc1
    tests/test_kepler_catalog_tkc1.cpp
)
target_link_libraries(test_kepler_catalog_tkc1 PRIVATE taiyin_ephemeris)

add_executable(test_custom_ephemeris_source_registry
    tests/test_custom_ephemeris_source_registry.cpp
)
target_link_libraries(test_custom_ephemeris_source_registry PRIVATE taiyin_ephemeris)

add_executable(test_custom_source_lifecycle
    tests/test_custom_source_lifecycle.cpp
)
target_link_libraries(test_custom_source_lifecycle PRIVATE taiyin_ephemeris)

add_executable(test_tkc1_catalog_discovery
    tests/test_tkc1_catalog_discovery.cpp
)
target_link_libraries(test_tkc1_catalog_discovery PRIVATE taiyin_ephemeris)

add_executable(test_moshier
    tests/test_moshier.cpp
)
target_link_libraries(test_moshier PRIVATE taiyin_ephemeris)

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

add_executable(test_stars
    tests/test_stars.cpp
)
target_link_libraries(test_stars PRIVATE taiyin_ephemeris)

add_executable(test_star_catalog_tsc1
    tests/test_star_catalog_tsc1.cpp
)
target_link_libraries(test_star_catalog_tsc1 PRIVATE taiyin_ephemeris)

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

add_executable(test_runtime_registry
    tests/test_runtime_registry.cpp
)
target_link_libraries(test_runtime_registry PRIVATE taiyin_ephemeris)

add_executable(test_runtime_services
    tests/test_runtime_services.cpp
)
target_link_libraries(test_runtime_services PRIVATE taiyin_ephemeris)

add_executable(test_major_body_apparent
    tests/test_major_body_apparent.cpp
)
target_link_libraries(test_major_body_apparent PRIVATE taiyin_ephemeris)

add_executable(test_pipeline
    tests/test_pipeline.cpp
)
target_link_libraries(test_pipeline PRIVATE taiyin_ephemeris)

add_executable(test_pipeline_bare_chart
    tests/test_pipeline_bare_chart.cpp
)
target_link_libraries(test_pipeline_bare_chart PRIVATE taiyin_ephemeris)

add_executable(test_pipeline_opm4_vs_swiss
    tests/test_pipeline_opm4_vs_swiss.cpp
)
target_link_libraries(test_pipeline_opm4_vs_swiss PRIVATE taiyin_ephemeris)

add_executable(test_global_ephemeris_runtime
    tests/test_global_ephemeris_runtime.cpp
)
target_link_libraries(test_global_ephemeris_runtime PRIVATE taiyin_ephemeris)

add_executable(test_writer_preferred_rwlock
    tests/test_writer_preferred_rwlock.cpp
)
target_link_libraries(test_writer_preferred_rwlock PRIVATE taiyin_ephemeris)

add_executable(test_route_inflight_map
    tests/test_route_inflight_map.cpp
)
target_link_libraries(test_route_inflight_map PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_singleflight
    tests/test_ephemeris_singleflight.cpp
)
target_link_libraries(test_ephemeris_singleflight PRIVATE taiyin_ephemeris)

add_executable(test_ephemeris_mvp_end_to_end
    tests/test_ephemeris_mvp_end_to_end.cpp
)
target_link_libraries(test_ephemeris_mvp_end_to_end PRIVATE taiyin_ephemeris)

add_executable(test_dispatch_models
    tests/test_dispatch_models.cpp
)
target_link_libraries(test_dispatch_models PRIVATE taiyin_ephemeris)

add_executable(test_apparent_position
    tests/test_apparent_position.cpp
)
target_link_libraries(test_apparent_position PRIVATE taiyin_ephemeris)

add_executable(test_apparent_position_oracles
    tests/test_apparent_position_oracles.cpp
)
target_link_libraries(test_apparent_position_oracles PRIVATE taiyin_ephemeris)

add_executable(example_bare_chart_pipeline
    examples/bare_chart_pipeline.cpp
)
target_link_libraries(example_bare_chart_pipeline PRIVATE taiyin_ephemeris)

add_executable(example_apparent_chart_pipeline
    examples/apparent_chart_pipeline.cpp
)
target_link_libraries(example_apparent_chart_pipeline PRIVATE taiyin_ephemeris)

add_executable(validate_private_opm4_vs_spk
    tools/validate_private_opm4_vs_spk.cpp
)
target_link_libraries(validate_private_opm4_vs_spk PRIVATE taiyin_ephemeris)



add_test(NAME opm4 COMMAND test_opm4)
add_test(NAME math_primitives COMMAND test_math_primitives)
add_test(NAME ephemeris_block COMMAND test_ephemeris_block)
add_test(NAME ephemeris_catalog COMMAND test_ephemeris_catalog)
add_test(NAME ephemeris_cache COMMAND test_ephemeris_cache)
add_test(NAME descriptor_loader COMMAND test_descriptor_loader)
add_test(NAME opm4_file_loader COMMAND test_opm4_file_loader)
add_test(NAME opm4_catalog_discovery COMMAND test_opm4_catalog_discovery)
add_test(NAME spk_catalog_discovery COMMAND test_spk_catalog_discovery)
add_test(NAME spk_jplephem_oracles COMMAND test_spk_jplephem_oracles)
add_test(NAME spk COMMAND test_spk)
add_test(NAME kepler COMMAND test_kepler)
add_test(NAME kepler_file COMMAND test_kepler_file)
add_test(NAME kepler_catalog_discovery COMMAND test_kepler_catalog_discovery)
add_test(NAME kepler_source_registry COMMAND test_kepler_source_registry)
add_test(NAME kepler_catalog_tkc1 COMMAND test_kepler_catalog_tkc1)
add_test(NAME custom_ephemeris_source_registry COMMAND test_custom_ephemeris_source_registry)
add_test(NAME custom_source_lifecycle COMMAND test_custom_source_lifecycle)
add_test(NAME tkc1_catalog_discovery COMMAND test_tkc1_catalog_discovery)
add_test(NAME moshier COMMAND test_moshier)
add_test(NAME corrections COMMAND test_corrections)
add_test(NAME corrections_oracles COMMAND test_corrections_oracles)
add_test(NAME geometry COMMAND test_geometry)
add_test(NAME coordinates COMMAND test_coordinates)
add_test(NAME low_level_oracles COMMAND test_low_level_oracles)
add_test(NAME pure_functions_full COMMAND test_pure_functions_full)
add_test(NAME time_angle_interpolation COMMAND test_time_angle_interpolation)
add_test(NAME eop COMMAND test_eop)
add_test(NAME stars COMMAND test_stars)
add_test(NAME star_catalog_tsc1 COMMAND test_star_catalog_tsc1)
add_test(NAME star_file COMMAND test_star_file)
add_test(NAME tsc1_catalog_discovery COMMAND test_tsc1_catalog_discovery)
add_test(NAME mapped_file COMMAND test_mapped_file)
add_test(NAME math_solvers COMMAND test_math_solvers)
add_test(NAME celestial_body_registry COMMAND test_celestial_body_registry)
add_test(NAME runtime_registry COMMAND test_runtime_registry)
add_test(NAME runtime_services COMMAND test_runtime_services)
add_test(NAME major_body_apparent COMMAND test_major_body_apparent)
add_test(NAME pipeline COMMAND test_pipeline)
add_test(NAME pipeline_bare_chart COMMAND test_pipeline_bare_chart)
add_test(NAME pipeline_opm4_vs_swiss COMMAND test_pipeline_opm4_vs_swiss)
add_test(NAME global_ephemeris_runtime COMMAND test_global_ephemeris_runtime)
add_test(NAME writer_preferred_rwlock COMMAND test_writer_preferred_rwlock)
add_test(NAME route_inflight_map COMMAND test_route_inflight_map)
add_test(NAME ephemeris_singleflight COMMAND test_ephemeris_singleflight)
add_test(NAME ephemeris_mvp_end_to_end COMMAND test_ephemeris_mvp_end_to_end)
add_test(NAME dispatch_models COMMAND test_dispatch_models)
add_test(NAME apparent_position COMMAND test_apparent_position)
add_test(NAME apparent_position_oracles COMMAND test_apparent_position_oracles)


add_executable(test_apparent_self_skip
    tests/test_apparent_self_skip.cpp
)
target_link_libraries(test_apparent_self_skip PRIVATE taiyin_ephemeris)
add_test(NAME apparent_self_skip COMMAND test_apparent_self_skip)

add_executable(test_apparent_opm4_vs_swiss
    tests/test_apparent_opm4_vs_swiss.cpp
)
target_link_libraries(test_apparent_opm4_vs_swiss PRIVATE taiyin_ephemeris)
add_test(NAME apparent_opm4_vs_swiss COMMAND test_apparent_opm4_vs_swiss)
