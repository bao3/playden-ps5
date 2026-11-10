# Self-update: GitHub release check, resumable asset downloads, miniz unpacking and the app swap.

add_library(playden_miniz STATIC "${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c")
target_include_directories(playden_miniz PUBLIC "${CMAKE_SOURCE_DIR}/third_party/miniz")
target_compile_definitions(playden_miniz PUBLIC MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_ARCHIVE_WRITING_APIS)
target_compile_options(playden_miniz PRIVATE -w)

# The mirror address is the Actions variable, read here and not printed. Empty is valid:
# the mirror stays off and no other host is substituted.
set(PLAYDEN_GITHUB_MIRROR_BASE_URL "$ENV{PLAYDEN_GITHUB_MIRROR_BASE_URL}")
if(NOT PLAYDEN_GITHUB_MIRROR_BASE_URL STREQUAL "")
    if(NOT PLAYDEN_GITHUB_MIRROR_BASE_URL MATCHES "^https://[A-Za-z0-9.-]+(:[0-9]+)?$")
        message(FATAL_ERROR "PLAYDEN_GITHUB_MIRROR_BASE_URL is not a valid https origin")
    endif()
endif()
string(REPLACE "\\" "\\\\" playden_mirror_base_escaped "${PLAYDEN_GITHUB_MIRROR_BASE_URL}")
string(REPLACE "\"" "\\\"" playden_mirror_base_escaped "${playden_mirror_base_escaped}")
file(MAKE_DIRECTORY "${PLAYDEN_GENERATED_DIR}")
file(WRITE "${PLAYDEN_GENERATED_DIR}/playden_github_mirror_base.h"
    "#pragma once\n#define PLAYDEN_GITHUB_MIRROR_BASE_URL \"${playden_mirror_base_escaped}\"\n")

add_library(playden_github_mirror STATIC
    "${CMAKE_SOURCE_DIR}/src/update/github_mirror.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/fork_downloads.cpp")
target_include_directories(playden_github_mirror PUBLIC "${CMAKE_SOURCE_DIR}/src" "${PLAYDEN_GENERATED_DIR}")
target_compile_features(playden_github_mirror PUBLIC cxx_std_17)
target_link_libraries(playden_github_mirror PUBLIC playden_storage playden_steam_http)
if(PLAYDEN_PLATFORM_PS5)
    target_compile_options(playden_github_mirror PRIVATE -fno-exceptions -fno-rtti)
endif()

add_library(playden_self_update STATIC
    "${CMAKE_SOURCE_DIR}/src/update/version.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/file_ops.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/zip_extract.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/release.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/asset_download.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/app_swap.cpp"
    "${CMAKE_SOURCE_DIR}/src/update/update_service.cpp")
target_include_directories(playden_self_update PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_include_directories(playden_self_update PRIVATE "${PLAYDEN_GENERATED_DIR}")
add_dependencies(playden_self_update playden_build_commit)
target_compile_features(playden_self_update PUBLIC cxx_std_17)
target_compile_definitions(playden_self_update PRIVATE PLAYDEN_VERSION="${PLAYDEN_FULL_VERSION}"
    PLAYDEN_NIGHTLY_BASE="${PLAYDEN_BUILD_NIGHTLY_BASE}")
target_link_libraries(playden_self_update PUBLIC playden_github_mirror playden_miniz playden_steam_protocol playden_steam_http Threads::Threads)
if(PLAYDEN_PLATFORM_PS5)
    target_compile_options(playden_self_update PRIVATE -fno-exceptions -fno-rtti)
endif()

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    foreach(test_name self_update_version self_update_zip self_update_release self_update_download
                      self_update_swap self_update_service)
        add_executable(${test_name}_test "${CMAKE_SOURCE_DIR}/tests/${test_name}_test.cpp")
        target_link_libraries(${test_name}_test PRIVATE playden_self_update)
        target_compile_definitions(${test_name}_test PRIVATE PLAYDEN_VERSION="${PLAYDEN_FULL_VERSION}")
        target_include_directories(${test_name}_test PRIVATE "${PLAYDEN_GENERATED_DIR}")
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
    add_executable(github_mirror_test "${CMAKE_SOURCE_DIR}/tests/github_mirror_test.cpp")
    target_link_libraries(github_mirror_test PRIVATE playden_github_mirror)
    target_include_directories(github_mirror_test PRIVATE "${PLAYDEN_GENERATED_DIR}")
    add_test(NAME github_mirror COMMAND github_mirror_test)
    add_executable(fork_downloads_test "${CMAKE_SOURCE_DIR}/tests/fork_downloads_test.cpp")
    target_link_libraries(fork_downloads_test PRIVATE playden_github_mirror)
    target_include_directories(fork_downloads_test PRIVATE "${PLAYDEN_GENERATED_DIR}")
    add_test(NAME fork_downloads COMMAND fork_downloads_test)
endif()

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME write_update_manifest
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/write_update_manifest_test.py")
endif()
