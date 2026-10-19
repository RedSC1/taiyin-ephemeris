cmake_minimum_required(VERSION 3.16)
project(taiyin_wasm_experiment LANGUAGES C CXX)

# Standalone, opt-in experiment. Normal/native project configuration is intact.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Verification is opt-in: native/Wasm compilation does not require Node.
find_program(TAIYIN_NODE_EXECUTABLE NAMES node nodejs)
if(TAIYIN_NODE_EXECUTABLE)
    add_custom_target(taiyin_check_astronomy_bridge
        COMMAND "${TAIYIN_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/generate-astronomy.mjs" --check
        COMMENT "Checking generated astronomy C ABI transport" VERBATIM)
endif()
if(EMSCRIPTEN)
    # Only this opt-in experiment links the optional modules into one Wasm
    # instance, so their borrowed astronomy/calendar handles remain valid.
    set(TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS ON CACHE BOOL "" FORCE)
    set(TAIYIN_BUILD_BAZI_EXTENSION ON CACHE BOOL "" FORCE)
    set(TAIYIN_BUILD_ZIWEI_EXTENSION ON CACHE BOOL "" FORCE)
endif()
if(MSVC)
    add_compile_options(/fp:strict)
else()
    add_compile_options(-ffp-contract=off)
endif()

if(EMSCRIPTEN)
    # Opt-in Dart Web backend, separate from the benchmark-only probe exports.
    set(TAIYIN_WEB_ENVIRONMENT "web" CACHE STRING
        "Emscripten loader environments (web, or web,node for JS/TS consumers)")
    add_executable(taiyin_web web_bridge.cpp)
    # Directory options below are inherited by core targets created later;
    # this earlier bridge target needs exception catching explicitly as well.
    target_compile_options(taiyin_web PRIVATE -fexceptions)
    target_link_libraries(taiyin_web PRIVATE taiyin_c_static)
    # Flat default rule tables are loaded once per catalog, not per chart.
    target_link_options(taiyin_web PRIVATE
        "--embed-file" "${CMAKE_CURRENT_SOURCE_DIR}/../../ziwei_astrology/rules@/taiyin/ziwei/rules")
    set_target_properties(taiyin_web PROPERTIES SUFFIX ".js")
    target_link_options(taiyin_web PRIVATE
        --no-entry -fexceptions
        "-sMODULARIZE=1" "-sEXPORT_NAME=createTaiyinModule"
        "-sENVIRONMENT=${TAIYIN_WEB_ENVIRONMENT}" "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=67108864" "-sMAXIMUM_MEMORY=1073741824"
        "-sSTACK_SIZE=2097152" "-sFORCE_FILESYSTEM=1"
        "-sEXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPF64']"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_taiyin_web_bridge_version','_taiyin_web_initialize','_taiyin_web_add_source','_taiyin_web_load_file','_taiyin_web_resource','_taiyin_web_star_memory','_taiyin_web_star_magnitude','_taiyin_web_context_create','_taiyin_web_context_clone','_taiyin_web_position','_taiyin_web_positions','_taiyin_web_configure','_taiyin_web_time','_taiyin_web_bazi','_taiyin_web_ziwei','_taiyin_bazi_context_destroy','_taiyin_web_solar_time','_taiyin_web_eclipse','_taiyin_web_eclipse_length','_taiyin_web_eclipse_data','_taiyin_web_eclipse_destroy','_taiyin_web_calendar_create','_taiyin_web_calendar','_taiyin_chinese_calendar_context_destroy','_taiyin_context_destroy','_taiyin_get_c_abi_version','_taiyin_get_library_version','_taiyin_get_library_codename','_taiyin_status_name','_taiyin_status_message','_taiyin_status_category','_taiyin_web_astronomy','_taiyin_web_astronomy_version','_taiyin_web_astronomy_extra','_taiyin_web_astronomy_callbacks']"
    )
endif()
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
endif()
add_subdirectory(../.. core EXCLUDE_FROM_ALL)

add_executable(taiyin_probe probe.cpp)
target_link_libraries(taiyin_probe PRIVATE taiyin_c_static)
if(EMSCRIPTEN)
    set_target_properties(taiyin_probe PROPERTIES SUFFIX ".mjs")
    target_link_options(taiyin_probe PRIVATE
        --no-entry -fexceptions
        "-sMODULARIZE=1" "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web,node"
        "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=67108864"
        "-sMAXIMUM_MEMORY=1073741824" "-sSTACK_SIZE=2097152"
        "-sFORCE_FILESYSTEM=1"
        "-sEXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPU32','HEAPF64']"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_probe_initialize','_probe_create_context','_probe_position','_probe_suite','_probe_seams','_probe_benchmark','_probe_eclipse_benchmark','_probe_opm2_count','_taiyin_context_destroy','_taiyin_runtime_add_source_path','_taiyin_make_call_result','_taiyin_call_result_status','_taiyin_call_result_flags']"
    )
else()
    target_compile_definitions(taiyin_probe PRIVATE TAIYIN_PROBE_NATIVE_MAIN=1)
    add_executable(taiyin_transport_test test_transport.cpp)
    add_executable(taiyin_astronomy_probe astronomy_native_probe.cpp)
    target_link_libraries(taiyin_astronomy_probe PRIVATE taiyin_c_static)
endif()
