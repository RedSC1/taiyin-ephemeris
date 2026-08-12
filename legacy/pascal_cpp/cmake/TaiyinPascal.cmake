include(CMakeParseArguments)

set(TAIYIN_FPC_EXECUTABLE "" CACHE FILEPATH
    "Free Pascal compiler driver; may point to a configured cross compiler")
set(TAIYIN_FPC_TARGET_OS "" CACHE STRING
    "Free Pascal target OS override, for example darwin, ios, android, linux, win64")
set(TAIYIN_FPC_TARGET_CPU "" CACHE STRING
    "Free Pascal target CPU override, for example aarch64 or x86_64")
set(TAIYIN_FPC_EXTRA_FLAGS "" CACHE STRING
    "Additional flags passed to every Taiyin Free Pascal compilation")

function(taiyin_resolve_fpc_target out_os out_cpu)
    if(TAIYIN_FPC_TARGET_OS)
        set(target_os "${TAIYIN_FPC_TARGET_OS}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(target_os "ios")
    elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(target_os "android")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(target_os "darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(target_os "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(target_os "win64")
        else()
            set(target_os "win32")
        endif()
    else()
        message(FATAL_ERROR
            "Cannot infer a Free Pascal target OS from CMAKE_SYSTEM_NAME="
            "${CMAKE_SYSTEM_NAME}; set TAIYIN_FPC_TARGET_OS explicitly")
    endif()

    if(TAIYIN_FPC_TARGET_CPU)
        set(target_cpu "${TAIYIN_FPC_TARGET_CPU}")
    else()
        set(processor "${CMAKE_SYSTEM_PROCESSOR}")
        if(APPLE AND CMAKE_OSX_ARCHITECTURES)
            list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
            if(architecture_count GREATER 1)
                message(FATAL_ERROR
                    "Free Pascal archives must be built one architecture at a time; "
                    "use separate CMake builds and combine Apple outputs as an XCFramework")
            endif()
            list(GET CMAKE_OSX_ARCHITECTURES 0 processor)
        endif()
        string(TOLOWER "${processor}" processor)
        if(processor STREQUAL "arm64" OR processor STREQUAL "aarch64")
            set(target_cpu "aarch64")
        elseif(processor STREQUAL "x86_64" OR processor STREQUAL "amd64")
            set(target_cpu "x86_64")
        elseif(processor MATCHES "^(i[3-6]86|x86)$")
            set(target_cpu "i386")
        elseif(processor MATCHES "^(armv7|armv7-a|armeabi-v7a|arm)$")
            set(target_cpu "arm")
        else()
            message(FATAL_ERROR
                "Cannot infer a Free Pascal target CPU from CMAKE_SYSTEM_PROCESSOR="
                "${CMAKE_SYSTEM_PROCESSOR}; set TAIYIN_FPC_TARGET_CPU explicitly")
        endif()
    endif()

    set(${out_os} "${target_os}" PARENT_SCOPE)
    set(${out_cpu} "${target_cpu}" PARENT_SCOPE)
endfunction()

function(taiyin_add_pascal_archive target source unit_name)
    set(options)
    set(one_value_args)
    set(multi_value_args UNIT_PATHS DEPENDS)
    cmake_parse_arguments(PASCAL
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT TAIYIN_FPC_EXECUTABLE)
        find_program(fpc_executable NAMES fpc REQUIRED)
        set(TAIYIN_FPC_EXECUTABLE "${fpc_executable}" CACHE FILEPATH
            "Free Pascal compiler driver; may point to a configured cross compiler" FORCE)
    endif()

    taiyin_resolve_fpc_target(target_os target_cpu)
    set(build_dir "${CMAKE_CURRENT_BINARY_DIR}/pascal/${unit_name}")
    set(object_file "${build_dir}/${unit_name}.o")
    set(unit_file "${build_dir}/${unit_name}.ppu")
    if(MSVC)
        set(archive_file "${build_dir}/${unit_name}.lib")
        set(archive_command
            ${CMAKE_AR} /NOLOGO /OUT:${archive_file} ${object_file})
        set(ranlib_command ${CMAKE_COMMAND} -E true)
    else()
        set(archive_file "${build_dir}/lib${unit_name}.a")
        set(archive_command ${CMAKE_AR} rcs ${archive_file} ${object_file})
        set(ranlib_command ${CMAKE_RANLIB} ${archive_file})
    endif()

    set(unit_path_flags)
    foreach(unit_path IN LISTS PASCAL_UNIT_PATHS)
        list(APPEND unit_path_flags "-Fu${unit_path}")
    endforeach()
    set(extra_flags)
    if(TAIYIN_FPC_EXTRA_FLAGS)
        separate_arguments(extra_flags NATIVE_COMMAND "${TAIYIN_FPC_EXTRA_FLAGS}")
    endif()

    add_custom_command(
        OUTPUT ${archive_file}
        BYPRODUCTS ${object_file} ${unit_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${build_dir}
        COMMAND ${TAIYIN_FPC_EXECUTABLE}
            -Cn
            -Cg
            -Mobjfpc
            -T${target_os}
            -P${target_cpu}
            -FU${build_dir}
            -FE${build_dir}
            ${unit_path_flags}
            ${extra_flags}
            ${source}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${archive_file}
        COMMAND ${archive_command}
        COMMAND ${ranlib_command}
        DEPENDS ${source} ${PASCAL_DEPENDS}
        VERBATIM
    )
    add_custom_target(${target}_build DEPENDS ${archive_file})
    add_library(${target} STATIC IMPORTED GLOBAL)
    set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION ${archive_file}
        TAIYIN_PASCAL_BUILD_DIR ${build_dir}
    )
    add_dependencies(${target} ${target}_build)
    message(STATUS
        "${target}: Free Pascal target ${target_cpu}-${target_os} via "
        "${TAIYIN_FPC_EXECUTABLE}")
endfunction()
