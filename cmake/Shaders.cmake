# Locate the shared DXC runtime; the custom commands below remain M1 test shaders only.
find_program(RENDERLAB_DXC NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    DOC "DXC with DXIL and SPIR-V support")
if(NOT RENDERLAB_DXC)
    message(FATAL_ERROR "DXC is required. Set RENDERLAB_DXC to dxc.exe or configure VULKAN_SDK.")
endif()
execute_process(COMMAND "${RENDERLAB_DXC}" --version OUTPUT_VARIABLE dxc_version
    RESULT_VARIABLE dxc_result OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT dxc_result EQUAL 0)
    message(FATAL_ERROR "Cannot execute DXC: ${RENDERLAB_DXC}")
endif()
message(STATUS "Shader compiler: ${dxc_version}")
get_filename_component(dxc_bin "${RENDERLAB_DXC}" DIRECTORY)
find_path(RENDERLAB_DXC_INCLUDE dxcapi.h HINTS "${dxc_bin}/../Include/dxc" REQUIRED)
find_library(RENDERLAB_DXC_LIBRARY dxcompiler HINTS "${dxc_bin}/../Lib" REQUIRED)
add_library(renderlab_dxc SHARED IMPORTED)
set_target_properties(renderlab_dxc PROPERTIES
    IMPORTED_IMPLIB "${RENDERLAB_DXC_LIBRARY}"
    IMPORTED_LOCATION "${dxc_bin}/dxcompiler.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${RENDERLAB_DXC_INCLUDE}")
set(shader_outputs)
foreach(backend IN ITEMS dx12 vulkan)
    set(backend_flags)
    if(backend STREQUAL vulkan)
        list(APPEND backend_flags -spirv -fspv-target-env=vulkan1.3 -DSPIRV=1)
    endif()
    foreach(program IN ITEMS vertex pixel noise noise_update)
        set(defines)
        if(program STREQUAL vertex)
            set(source graphics)
            set(profile vs_6_0)
            set(entry VSMain)
        elseif(program STREQUAL pixel)
            set(source graphics)
            set(profile ps_6_0)
            set(entry PSMain)
        else()
            set(source noise)
            set(profile cs_6_0)
            set(entry CSMain)
            if(program STREQUAL noise_update)
                list(APPEND defines -DUPDATE=1)
            endif()
        endif()
        set(output "${CMAKE_BINARY_DIR}/shaders/${backend}/${program}.bin")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/${backend}"
            COMMAND "${RENDERLAB_DXC}" -nologo -WX -Ges -O3 -T ${profile} -E ${entry}
                ${backend_flags} ${defines} -Fo "${output}" "${CMAKE_SOURCE_DIR}/shaders/m1/${source}.hlsl"
            DEPENDS "${CMAKE_SOURCE_DIR}/shaders/m1/${source}.hlsl"
            VERBATIM)
        list(APPEND shader_outputs "${output}")
    endforeach()
endforeach()
add_custom_target(renderlab_m1_shaders ALL DEPENDS ${shader_outputs})
