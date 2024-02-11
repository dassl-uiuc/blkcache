cmake_minimum_required(VERSION 3.3)
project(block_cache C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# General
option(BLOCK_CACHE_BUILD_EXAMPLES         "block cache: build examples"                                 ON)
option(BLOCK_CACHE_BUILD_SHARED_LIBRARY   "block cache: build as a shared library"                      OFF)
option(BLOCK_CACHE_STATIC                 "block cache: static link libraries"                          OFF)
option(BLOCK_CACHE_NATIVE                 "block cache: enable -march=native flag"                      OFF)
option(BLOCK_CACHE_LTO                    "block cache: enable link time optimization"                  OFF)

# Sanitizers
option(BLOCK_CACHE_SANITIZE_THREAD        "block cache: enable thread sanitizer"                        OFF)
option(BLOCK_CACHE_SANITIZE_ADDRESS       "block cache: enable address sanitizer"                       OFF)
option(BLOCK_CACHE_SANITIZE_UNDEFINED     "block cache: enable undefined sanitizer"                     OFF)

#
# Compile flags
#

set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG") 
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (NOT MSVC)
    if (BLOCK_CACHE_SANITIZE_THREAD)
        add_compile_options(-fsanitize=thread)
        link_libraries(-fsanitize=thread)
    endif()

    if (BLOCK_CACHE_SANITIZE_ADDRESS)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        link_libraries(-fsanitize=address)
    endif()

    if (BLOCK_CACHE_SANITIZE_UNDEFINED)
        add_compile_options(-fsanitize=undefined)
        link_libraries(-fsanitize=undefined)
    endif()
endif()

if (BLOCK_CACHE_ALL_WARNINGS)
    if (NOT MSVC)
        set(c_flags
            -Wall
            -Wextra
            -Wpedantic
            -Wcast-qual
            -Wdouble-promotion
            -Wshadow
            -Wstrict-prototypes
            -Wpointer-arith
            -Wno-unused-function
        )
        set(cxx_flags
            -Wall
            -Wextra
            -Wpedantic
            -Wcast-qual
            -Wno-unused-function
            -Wno-multichar
        )
    else()
        set(c_flags
            -W4
        )
        set(cxx_flags
            -W4
        )
    endif()

    add_compile_options(
            "$<$<COMPILE_LANGUAGE:C>:${c_flags}>"
            "$<$<COMPILE_LANGUAGE:CXX>:${cxx_flags}>"
    )

endif()

if (BLOCK_CACHE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if (result)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO is not supported: ${output}")
    endif()
endif()

# Architecture specific
# TODO: probably these flags need to be tweaked on some architectures
#       feel free to update the Makefile for your architecture and send a pull request or issue
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
if (NOT MSVC)
    if (BLOCK_CACHE_STATIC)
        add_link_options(-static)
        if (MINGW)
            add_link_options(-static-libgcc -static-libstdc++)
        endif()
    endif()
    if (BLOCK_CACHE_GPROF)
        add_compile_options(-pg)
    endif()
    if (BLOCK_CACHE_NATIVE)
        add_compile_options(-march=native)
    endif()
endif()

#
# Build libraries
#

if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if (BLOCK_CACHE_BUILD_SHARED_LIBRARY)
    set(BLOCK_CACHE_LIBRARY_BUILD SHARED)
else()
    set(BLOCK_CACHE_LIBRARY_BUILD STATIC)
endif()

macro(add_dependency)
    SET(dependency_name ${ARGV0})
    SET(endpoint_url ${ARGV1})
    SET(endpoint_tag ${ARGV2})
    SET(do_build_with_cmake ${ARGV3})

    FetchContent_Declare(
            ${dependency_name}
            GIT_REPOSITORY ${endpoint_url}
            GIT_TAG ${endpoint_tag}
    )

    FetchContent_GetProperties(${dependency_name})

    if (NOT ${dependency_name}_POPULATED)
        FetchContent_Populate(${dependency_name})
        message(STATUS "Working on ${dependency_name}")

        if (${do_build_with_cmake})
            add_subdirectory(${${dependency_name}_SOURCE_DIR} ${${dependency_name}_BINARY_DIR})
        else ()
            message("\tHeader only")
        endif ()
    endif ()
endmacro()

set(BLOCK_CACHE_MSVC_USE_STATIC_CRT on CACHE BOOL "Use MT flags when compiling in MSVC")
if (MSVC)
  if (BLOCK_CACHE_MSVC_USE_STATIC_CRT)
     message("-- Using static CRT linking ${BLOCK_CACHE_MSVC_USE_STATIC_CRT}")
     foreach(flag_var CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                          CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
                          CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                          CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
       string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
     endforeach()
  endif()
endif()

if (BLOCK_CACHE_BUILD_SHARED_LIBRARY)
    # hack...
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # set_property(TARGET fmt PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

add_dependency(fmt https://github.com/fmtlib/fmt 9.1.0 TRUE)
add_dependency(unordered_dense https://github.com/martinus/unordered_dense v4.0.0 TRUE)
add_dependency(stb https://github.com/nothings/stb 5736b15 FALSE)
add_dependency(spdlog https://github.com/gabime/spdlog v1.11.0 TRUE)
add_dependency(nlohmann_json https://github.com/nlohmann/json v3.11.2 TRUE)
add_dependency(magic_enum https://github.com/Neargye/magic_enum v0.9.3 TRUE)
add_dependency(parallel_hashmap https://github.com/greg7mdp/parallel-hashmap v1.3.12 TRUE)
add_dependency(concurrentqueue https://github.com/cameron314/concurrentqueue v1.0.4 TRUE)
add_dependency(tlru https://github.com/tstarling/thread-safe-lru df7b21c FALSE)

set(EXPECTED_BUILD_TESTS OFF)
add_dependency(tl_expected https://github.com/TartanLlama/expected v1.1.0 TRUE)

add_library(block_cache ${BLOCK_CACHE_LIBRARY_BUILD}
            block_cache.cpp
            block_cache.h)

include(FindPkgConfig)
pkg_search_module(TBB REQUIRED tbb)

target_include_directories(block_cache PUBLIC
    .

    ${fmt_SOURCE_DIR}
    ${unordered_dense_SOURCE_DIR}/include/ankerl
    ${spdlog_SOURCE_DIR}
    ${nlohmann_json_SOURCE_DIR}
    ${magic_enum_SOURCE_DIR}/include
    ${tl_expected_SOURCE_DIR}/include/tl
    ${parallel_hashmap_SOURCE_DIR}/parallel_hashmap
    ${concurrentqueue_SOURCE_DIR}
    ${tlru_SOURCE_DIR}

    ${TBB_INCLUDE_DIRS}
)

target_link_libraries(block_cache PUBLIC
    fmt
    unordered_dense
    spdlog
    nlohmann_json
    magic_enum
    expected
    phmap
    concurrentqueue

    ibverbs
    Threads::Threads    

    ${TBB_LIBRARIES}
)

if (MSVC)
  if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
    target_compile_options(block_cache PUBLIC "/ZI")
    target_link_options(block_cache PUBLIC "/INCREMENTAL")
  endif()
endif()

if (BLOCK_CACHE_BUILD_SHARED_LIBRARY)
    set_target_properties(block_cache PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(block_cache PRIVATE BLOCK_CACHE_SHARED BLOCK_CACHE_BUILD)
endif()

if (BLOCK_CACHE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()