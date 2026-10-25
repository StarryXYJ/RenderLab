# Match Ninja's include parser to the compiler's actual output, including redirected builds.
# Locale detection cached by CMake may use a different encoding from the build process.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_renderlab_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/renderlab-showincludes")
    file(MAKE_DIRECTORY "${_renderlab_probe_dir}")
    file(WRITE "${_renderlab_probe_dir}/renderlab_showincludes_probe.hpp" "\n")
    file(WRITE "${_renderlab_probe_dir}/probe.cpp"
        "#include \"renderlab_showincludes_probe.hpp\"\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /Zs /utf-8 probe.cpp
        WORKING_DIRECTORY "${_renderlab_probe_dir}"
        RESULT_VARIABLE _renderlab_probe_result
        OUTPUT_VARIABLE _renderlab_probe_output
        ERROR_VARIABLE _renderlab_probe_error
        ENCODING UTF-8)
    if(NOT _renderlab_probe_result EQUAL 0)
        message(FATAL_ERROR "MSVC include-prefix probe failed: ${_renderlab_probe_error}")
    endif()
    string(REGEX MATCH "([^\r\n]*:)[ \t]+[^\r\n]*renderlab_showincludes_probe[.]hpp"
        _renderlab_probe_match "${_renderlab_probe_output}")
    if(NOT _renderlab_probe_match)
        message(FATAL_ERROR "Cannot detect MSVC include prefix: ${_renderlab_probe_output}")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_1}")
    message(STATUS "MSVC include-prefix detection uses UTF-8 compiler output")
endif()
