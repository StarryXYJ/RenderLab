option(RENDERLAB_ENVIRONMENT_SIMD "Use the optional pinned ISPC environment prefilter" OFF)
if(NOT RENDERLAB_ENVIRONMENT_SIMD)
    return()
endif()

find_program(RENDERLAB_ISPC NAMES ispc REQUIRED)
file(SHA256 "${RENDERLAB_ISPC}" renderlab_ispc_sha256)
# Official ispc-v1.28.2-windows.zip; see manual/build.md for the archive checksum.
if(NOT renderlab_ispc_sha256 STREQUAL "886c7b3c82164dbcce02479a5a9e5880cd7f91b543c551eff561c57007b1f28d")
    message(FATAL_ERROR "Environment SIMD requires the official Windows x64 ISPC 1.28.2 executable.")
endif()
set(RENDERLAB_ISPC_TARGETS "sse2-i32x4;avx2-i32x8" CACHE STRING "Environment SIMD targets")
if(NOT RENDERLAB_ISPC_TARGETS STREQUAL "sse2-i32x4;avx2-i32x8"
   AND NOT RENDERLAB_ISPC_TARGETS STREQUAL "sse2-i32x4")
    message(FATAL_ERROR "Use SSE2 alone or the SSE2/AVX2 runtime-dispatched pair.")
endif()
set(renderlab_ispc_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/ispc")
file(MAKE_DIRECTORY "${renderlab_ispc_dir}")
set(renderlab_ispc_objects "${renderlab_ispc_dir}/environment_ispc.obj")
if(RENDERLAB_ISPC_TARGETS MATCHES "avx2")
    list(APPEND renderlab_ispc_objects
        "${renderlab_ispc_dir}/environment_ispc_sse2.obj"
        "${renderlab_ispc_dir}/environment_ispc_avx2.obj")
endif()
string(REPLACE ";" "," renderlab_ispc_targets "${RENDERLAB_ISPC_TARGETS}")
add_custom_command(
    OUTPUT ${renderlab_ispc_objects} "${renderlab_ispc_dir}/environment_ispc.h"
    COMMAND "${RENDERLAB_ISPC}" "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/environment.ispc"
        --arch=x86-64 --target-os=windows "--target=${renderlab_ispc_targets}"
        --math-lib=system --opt=disable-fma "$<IF:$<CONFIG:Debug>,-O0,-O2>"
        -h "${renderlab_ispc_dir}/environment_ispc.h"
        -o "${renderlab_ispc_dir}/environment_ispc.obj"
    DEPENDS src/renderer/environment.ispc "${RENDERLAB_ISPC}"
    VERBATIM)
set_source_files_properties(${renderlab_ispc_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_library(renderlab_environment_simd STATIC ${renderlab_ispc_objects})
set_target_properties(renderlab_environment_simd PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(renderlab_renderer PRIVATE renderlab_environment_simd)
set_property(SOURCE src/renderer/environment.cpp APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated/ispc")
set_property(SOURCE src/renderer/environment.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS RENDERLAB_ENVIRONMENT_SIMD=1)
get_filename_component(renderlab_ispc_bin "${RENDERLAB_ISPC}" DIRECTORY)
get_filename_component(renderlab_ispc_root "${renderlab_ispc_bin}" DIRECTORY)
install(FILES "${renderlab_ispc_root}/LICENSE.txt" DESTINATION licenses RENAME ISPC-LICENSE.txt)
install(FILES "${renderlab_ispc_root}/third-party-programs.txt" DESTINATION licenses
    RENAME ISPC-third-party-programs.txt)
