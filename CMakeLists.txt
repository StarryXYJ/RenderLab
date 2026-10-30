cmake_minimum_required(VERSION 3.28)
project(RenderLab VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenderLab currently requires Windows x64.")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset windows-debug")
endif()

include(cmake/MsvcIncludes.cmake)
include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)
option(RENDERLAB_GPU_TESTS "Register tests requiring a GPU, validation layers and desktop session" OFF)

add_library(renderlab_options INTERFACE)
target_compile_features(renderlab_options INTERFACE cxx_std_20)
target_compile_definitions(renderlab_options INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(renderlab_options INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(renderlab_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(renderlab_core STATIC src/core/log.cpp src/core/hash.cpp src/core/file_io.cpp)
target_include_directories(renderlab_core PUBLIC src)
target_link_libraries(renderlab_core PRIVATE renderlab_options bcrypt)

add_library(renderlab_platform STATIC src/platform/window.cpp src/platform/text.cpp)
target_include_directories(renderlab_platform PUBLIC src)
target_link_libraries(renderlab_platform PRIVATE renderlab_options user32)

add_library(renderlab_content STATIC
    src/math/math.cpp src/project/project.cpp src/assets/database.cpp
    src/assets/gltf.cpp src/assets/image.cpp src/scene/scene.cpp)
target_include_directories(renderlab_content PUBLIC src)
target_link_libraries(renderlab_content PRIVATE renderlab_options renderlab_core
    renderlab_platform nlohmann_json::nlohmann_json DirectXMath windowscodecs ole32 crypt32)
target_compile_definitions(renderlab_content PRIVATE _XM_NO_XMVECTOR_OVERLOADS_)

add_library(renderlab_rhi STATIC
    src/rhi/device.cpp
    src/rhi/resources.cpp
    src/rhi/commands.cpp
    src/rhi/recording_workers.cpp
    src/rhi/runtime.cpp
    src/rhi/pipeline_cache.cpp
    src/rhi/upload.cpp
    src/rhi/dx12/dx12_device.cpp
    src/rhi/dx12/dx12_common.cpp
    src/rhi/dx12/dx12_resources.cpp
    src/rhi/dx12/dx12_pipeline.cpp
    src/rhi/dx12/dx12_commands.cpp
    src/rhi/dx12/dx12_swapchain.cpp
    src/rhi/dx12/dx12_runtime.cpp
    src/rhi/vulkan/vulkan_device.cpp
    src/rhi/vulkan/vulkan_common.cpp
    src/rhi/vulkan/vulkan_resources.cpp
    src/rhi/vulkan/vulkan_pipeline.cpp
    src/rhi/vulkan/vulkan_commands.cpp
    src/rhi/vulkan/vulkan_swapchain.cpp
    src/rhi/vulkan/vulkan_runtime.cpp)
target_include_directories(renderlab_rhi PUBLIC src)
target_link_libraries(renderlab_rhi PRIVATE renderlab_options renderlab_core renderlab_platform
    d3d12 dxgi dxguid volk renderlab_pix)
target_compile_definitions(renderlab_rhi PRIVATE USE_PIX)

add_library(renderlab_rendergraph STATIC
    src/rendergraph/graph.cpp src/rendergraph/builder.cpp src/rendergraph/validation.cpp
    src/rendergraph/compiler.cpp src/rendergraph/queue_plan.cpp src/rendergraph/alias_plan.cpp src/rendergraph/pool.cpp src/rendergraph/context.cpp
    src/rendergraph/executor.cpp src/rendergraph/diagnostics.cpp)
target_include_directories(renderlab_rendergraph PUBLIC src)
target_link_libraries(renderlab_rendergraph PRIVATE renderlab_options renderlab_rhi
    nlohmann_json::nlohmann_json)

add_library(renderlab_renderer STATIC src/renderer/forward.cpp src/renderer/deferred.cpp
    src/renderer/raytracing.cpp src/renderer/acceleration_scene.cpp
    src/renderer/pipeline.cpp src/renderer/indexed_scene.cpp
    src/renderer/scene_renderer.cpp src/renderer/environment.cpp src/renderer/shadow.cpp)
target_include_directories(renderlab_renderer PUBLIC src)
target_link_libraries(renderlab_renderer PRIVATE renderlab_options renderlab_rendergraph renderlab_rhi
    renderlab_content renderlab_material nlohmann_json::nlohmann_json)
include(cmake/EnvironmentSimd.cmake)

add_library(renderlab_sandbox_options STATIC src/apps/sandbox/options.cpp)
target_include_directories(renderlab_sandbox_options PUBLIC src)
target_link_libraries(renderlab_sandbox_options PRIVATE renderlab_options)

add_executable(renderlab_cpu_benchmark src/tools/cpu_benchmark/main.cpp)
target_link_libraries(renderlab_cpu_benchmark PRIVATE renderlab_options renderlab_renderer
    renderlab_content renderlab_core nlohmann_json::nlohmann_json)
target_compile_definitions(renderlab_cpu_benchmark PRIVATE
    RENDERLAB_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    RENDERLAB_ENVIRONMENT_SIMD=$<BOOL:${RENDERLAB_ENVIRONMENT_SIMD}>)

add_library(renderlab_benchmark STATIC src/benchmark/recording.cpp)
target_include_directories(renderlab_benchmark PUBLIC src)
target_link_libraries(renderlab_benchmark PUBLIC nlohmann_json::nlohmann_json
    PRIVATE renderlab_options renderlab_platform)

add_library(renderlab_shader STATIC
    src/shader/lexer.cpp
    src/shader/types.cpp
    src/shader/parameter_parser.cpp
    src/shader/parser.cpp
    src/shader/value.cpp
    src/shader/layout.cpp
    src/shader/features.cpp
    src/shader/render_state.cpp
    src/shader/state_parser.cpp
    src/shader/bindings.cpp
    src/shader/cpp_generator.cpp
    src/shader/dxc_compiler.cpp
    src/shader/dxil_reflection.cpp
    src/shader/schema.cpp
    src/shader/spirv_reflection.cpp
    src/shader/compiler.cpp
    src/shader/reload.cpp)
target_include_directories(renderlab_shader PUBLIC src)
target_link_libraries(renderlab_shader PRIVATE renderlab_options renderlab_core renderlab_platform
    renderlab_dxc renderlab_spirv_reflect nlohmann_json::nlohmann_json ole32 dxguid)
add_executable(renderlab_shaderc src/tools/shaderc/main.cpp)
target_link_libraries(renderlab_shaderc PRIVATE renderlab_options renderlab_shader renderlab_platform)
add_custom_command(TARGET renderlab_shaderc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
        $<TARGET_FILE_DIR:renderlab_shaderc> VERBATIM)

file(GLOB renderlab_scene_shader_sources CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shaders/RenderLab/*.params" "${CMAKE_SOURCE_DIR}/shaders/RenderLab/*.shader"
    "${CMAKE_SOURCE_DIR}/shaders/RenderLab/*.hlsl" "${CMAKE_SOURCE_DIR}/shaders/RenderLab/*.hlsli")
set(renderlab_scene_parameters "${CMAKE_BINARY_DIR}/generated/scene_parameters.hpp")
add_custom_command(OUTPUT "${renderlab_scene_parameters}"
    COMMAND $<TARGET_FILE:renderlab_shaderc> "${CMAKE_SOURCE_DIR}/shaders/RenderLab/Lit.shader"
        --target dxil --cache "${CMAKE_BINARY_DIR}/scene-parameter-cache"
        --schema "${CMAKE_BINARY_DIR}/generated/scene_parameters.json" --cpp "${renderlab_scene_parameters}"
    DEPENDS renderlab_shaderc ${renderlab_scene_shader_sources} VERBATIM)
target_sources(renderlab_renderer PRIVATE "${renderlab_scene_parameters}")
target_include_directories(renderlab_renderer PRIVATE "${CMAKE_BINARY_DIR}/generated")

add_library(renderlab_material STATIC
    src/material/asset.cpp src/material/packing.cpp src/material/serialization.cpp src/material/runtime.cpp src/material/render_state.cpp src/material/live_material.cpp)
target_include_directories(renderlab_material PUBLIC src)
target_link_libraries(renderlab_material PRIVATE renderlab_options renderlab_shader renderlab_rhi
    renderlab_platform renderlab_core nlohmann_json::nlohmann_json)

add_executable(renderlab_material_demo src/tools/material_demo/main.cpp src/samples/material/material_demo.cpp
    src/samples/material/material_readback.cpp src/samples/material/material_compute.cpp src/samples/material/material_reload.cpp)
target_link_libraries(renderlab_material_demo PRIVATE renderlab_options renderlab_material
    renderlab_shader renderlab_rhi renderlab_core renderlab_platform)
add_custom_command(TARGET renderlab_material_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
        $<TARGET_FILE_DIR:renderlab_material_demo>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
        $<TARGET_FILE_DIR:renderlab_material_demo> VERBATIM)

set(RENDERLAB_HOST_SOURCES src/apps/sandbox/presentation.cpp
    src/apps/sandbox/debug_panel.cpp src/apps/sandbox/statistics.cpp src/apps/sandbox/capture.cpp)
add_library(renderlab_app_host STATIC ${RENDERLAB_HOST_SOURCES})
target_include_directories(renderlab_app_host PUBLIC src)
add_executable(renderlab_sandbox src/apps/sandbox/main.cpp)
add_executable(renderlab_player src/apps/sandbox/main.cpp)
target_compile_definitions(renderlab_player PRIVATE RENDERLAB_PLAYER=1)
add_library(renderlab_samples STATIC
    src/samples/rhi/rhi_demo.cpp
    src/samples/common/demo_geometry.cpp
    src/samples/common/image_io.cpp
    src/samples/graph/graph_demo.cpp
    src/samples/graph/graph_features.cpp
    src/samples/scene/scene_acceptance.cpp
    src/samples/scene/reflection_temporal_motion.cpp
    src/samples/graph/forward_acceptance.cpp
    src/samples/graph/graph_acceptance.cpp
    src/samples/graph/graph_buffer_checks.cpp
    src/samples/rhi/rhi_acceptance.cpp
    src/samples/rhi/acceleration_structure_acceptance.cpp
    src/samples/rhi/reflection_temporal_acceptance.cpp
    src/samples/rhi/rhi_contract_checks.cpp
    src/samples/common/shader_io.cpp)
target_include_directories(renderlab_samples PUBLIC src)
target_link_libraries(renderlab_samples PRIVATE renderlab_options renderlab_rhi renderlab_core renderlab_platform renderlab_rendergraph renderlab_renderer renderlab_shader)
add_dependencies(renderlab_sandbox renderlab_m1_shaders renderlab_m3_shaders)
add_custom_target(renderlab_m5_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/shaders/m5"
        "$<TARGET_FILE_DIR:renderlab_sandbox>/shaders/m5" VERBATIM)
add_dependencies(renderlab_sandbox renderlab_m5_shaders)
add_custom_target(renderlab_scene_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/shaders/RenderLab"
        "$<TARGET_FILE_DIR:renderlab_sandbox>/shaders/RenderLab" VERBATIM)
add_dependencies(renderlab_sandbox renderlab_scene_shaders)
add_dependencies(renderlab_player renderlab_scene_shaders)
add_library(renderlab_editor_bridge SHARED src/editor/native/editor_bridge.cpp)
target_compile_definitions(renderlab_editor_bridge PRIVATE RENDERLAB_EDITOR_BRIDGE_EXPORTS)
target_include_directories(renderlab_editor_bridge PUBLIC src)
target_link_libraries(renderlab_editor_bridge PRIVATE renderlab_options renderlab_renderer
    renderlab_rhi renderlab_platform renderlab_core nlohmann_json::nlohmann_json ole32 user32)
add_dependencies(renderlab_editor_bridge renderlab_scene_shaders)
add_custom_command(TARGET renderlab_editor_bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
        $<TARGET_FILE_DIR:renderlab_editor_bridge>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
        $<TARGET_FILE_DIR:renderlab_editor_bridge> VERBATIM)
add_custom_command(TARGET renderlab_player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
        $<TARGET_FILE_DIR:renderlab_player>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
        $<TARGET_FILE_DIR:renderlab_player> VERBATIM)
add_custom_command(TARGET renderlab_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
        $<TARGET_FILE_DIR:renderlab_sandbox> VERBATIM)
add_custom_command(TARGET renderlab_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
        $<TARGET_FILE_DIR:renderlab_sandbox> VERBATIM)
target_link_libraries(renderlab_sandbox PRIVATE renderlab_options renderlab_core
    renderlab_platform renderlab_rhi renderlab_sandbox_options renderlab_app_host ole32)
target_link_libraries(renderlab_player PRIVATE renderlab_options renderlab_core
    renderlab_platform renderlab_rhi renderlab_sandbox_options renderlab_app_host ole32)
target_link_libraries(renderlab_app_host PRIVATE renderlab_options renderlab_core renderlab_platform
    renderlab_rhi renderlab_renderer renderlab_sandbox_options renderlab_samples renderlab_benchmark nlohmann_json::nlohmann_json ole32 gdi32 psapi)

install(TARGETS renderlab_player RUNTIME DESTINATION .)
install(FILES "${directxmath_SOURCE_DIR}/LICENSE" DESTINATION licenses
    RENAME DirectXMath-LICENSE.txt)
install(FILES $<TARGET_FILE:renderlab_dxc> $<TARGET_FILE:renderlab_pix> DESTINATION .)
install(DIRECTORY shaders/RenderLab DESTINATION shaders)
install(DIRECTORY sample/m4/Assets DESTINATION project)
install(FILES sample/m4/RenderLab.rlproject sample/m4/Pbr.rlproject sample/m4/Shadow.rlproject DESTINATION project)

if(BUILD_TESTING)
    add_executable(renderlab_environment_tests tests/environment_tests.cpp tests/reference/environment.cpp)
    target_include_directories(renderlab_environment_tests PRIVATE tests)
    target_link_libraries(renderlab_environment_tests PRIVATE renderlab_options renderlab_renderer)
    if(RENDERLAB_ENVIRONMENT_SIMD)
        add_dependencies(renderlab_environment_tests renderlab_environment_simd)
        target_include_directories(renderlab_environment_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/ispc")
        target_compile_definitions(renderlab_environment_tests PRIVATE RENDERLAB_ENVIRONMENT_SIMD=1)
    endif()
    add_test(NAME environment.contracts COMMAND renderlab_environment_tests)
    set_tests_properties(environment.contracts PROPERTIES TIMEOUT 180)
    add_executable(renderlab_math_tests tests/math_tests.cpp)
    target_link_libraries(renderlab_math_tests PRIVATE renderlab_options renderlab_content)
    add_test(NAME math.contracts COMMAND renderlab_math_tests)
    add_executable(renderlab_scene_tests tests/scene_tests.cpp)
    target_link_libraries(renderlab_scene_tests PRIVATE renderlab_options renderlab_content)
    add_test(NAME scene.contracts COMMAND renderlab_scene_tests)
    add_test(NAME cpu_benchmark.contracts COMMAND renderlab_cpu_benchmark --verify)
    set_tests_properties(cpu_benchmark.contracts PROPERTIES TIMEOUT 180)
    add_executable(renderlab_recording_tests tests/recording_tests.cpp)
    target_link_libraries(renderlab_recording_tests PRIVATE renderlab_options renderlab_rhi)
    add_test(NAME recording.contracts COMMAND renderlab_recording_tests)
    set_tests_properties(recording.contracts PROPERTIES TIMEOUT 30)
    add_executable(renderlab_indexed_scene_tests tests/indexed_scene_tests.cpp)
    target_link_libraries(renderlab_indexed_scene_tests PRIVATE renderlab_options renderlab_renderer
        renderlab_samples renderlab_rhi renderlab_content renderlab_core renderlab_platform)
    add_dependencies(renderlab_indexed_scene_tests renderlab_scene_shaders)
    add_custom_command(TARGET renderlab_indexed_scene_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_indexed_scene_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_indexed_scene_tests> VERBATIM)
    add_executable(renderlab_descriptor_array_tests tests/descriptor_array_tests.cpp)
    target_link_libraries(renderlab_descriptor_array_tests PRIVATE renderlab_options renderlab_shader
        renderlab_rendergraph renderlab_rhi renderlab_core renderlab_platform)
    add_custom_command(TARGET renderlab_descriptor_array_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_descriptor_array_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_descriptor_array_tests> VERBATIM)
    add_executable(renderlab_indirect_tests tests/indirect_tests.cpp)
    target_link_libraries(renderlab_indirect_tests PRIVATE renderlab_options renderlab_shader
        renderlab_rendergraph renderlab_rhi renderlab_core renderlab_platform)
    add_custom_command(TARGET renderlab_indirect_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_indirect_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_indirect_tests> VERBATIM)
    add_executable(renderlab_editor_input_tests tests/editor/native/camera_input_tests.cpp)
    target_link_libraries(renderlab_editor_input_tests PRIVATE renderlab_options renderlab_content)
    add_test(NAME editor.input COMMAND renderlab_editor_input_tests)
    add_executable(renderlab_benchmark_tests tests/benchmark_tests.cpp)
    target_link_libraries(renderlab_benchmark_tests PRIVATE renderlab_options renderlab_benchmark)
    add_test(NAME benchmark.contracts COMMAND renderlab_benchmark_tests)
    set(parameter_fixture "${CMAKE_BINARY_DIR}/generated/parameter_fixture.hpp")
    add_custom_command(OUTPUT "${parameter_fixture}"
        COMMAND $<TARGET_FILE:renderlab_shaderc> "${CMAKE_SOURCE_DIR}/shaders/tests/parameters.shader"
            --target dxil --cache "${CMAKE_BINARY_DIR}/parameter-generator-cache"
            --schema "${CMAKE_BINARY_DIR}/generated/parameter_fixture.json" --cpp "${parameter_fixture}"
        DEPENDS renderlab_shaderc shaders/tests/parameters.shader shaders/tests/parameters.params
            shaders/tests/parameters.hlsl shaders/tests/parameter_library.hlsli VERBATIM)
    add_executable(renderlab_parameter_tests tests/parameter_tests.cpp "${parameter_fixture}")
    target_include_directories(renderlab_parameter_tests PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(renderlab_parameter_tests PRIVATE renderlab_options renderlab_material renderlab_shader)
    add_test(NAME parameters.contracts COMMAND renderlab_parameter_tests
        "${CMAKE_SOURCE_DIR}/shaders/tests" "${CMAKE_BINARY_DIR}/parameter-tests")
    set_tests_properties(parameters.contracts PROPERTIES TIMEOUT 60)
    add_executable(renderlab_render_state_tests tests/render_state_tests.cpp)
    target_link_libraries(renderlab_render_state_tests PRIVATE renderlab_options renderlab_shader renderlab_material
        renderlab_rendergraph renderlab_rhi renderlab_core renderlab_platform)
    add_custom_command(TARGET renderlab_render_state_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_render_state_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_render_state_tests> VERBATIM)
    add_executable(renderlab_surface_tests tests/surface_tests.cpp)
    target_link_libraries(renderlab_surface_tests PRIVATE renderlab_options renderlab_renderer
        renderlab_material renderlab_rhi renderlab_content renderlab_core renderlab_platform
        renderlab_samples nlohmann_json::nlohmann_json)
    add_dependencies(renderlab_surface_tests renderlab_scene_shaders)
    add_test(NAME surface.contracts COMMAND renderlab_surface_tests cpu
        ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject ${CMAKE_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/surface/cpu)
    add_executable(renderlab_pbr_tests tests/pbr_tests.cpp)
    target_link_libraries(renderlab_pbr_tests PRIVATE renderlab_options renderlab_renderer
        renderlab_material renderlab_rhi renderlab_content renderlab_core renderlab_platform
        renderlab_samples nlohmann_json::nlohmann_json)
    add_dependencies(renderlab_pbr_tests renderlab_scene_shaders)
    add_custom_command(TARGET renderlab_pbr_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_pbr_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_pbr_tests> VERBATIM)
    add_test(NAME pbr.contracts COMMAND renderlab_pbr_tests cpu
        ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject ${CMAKE_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/pbr/cpu)
    set_tests_properties(pbr.contracts PROPERTIES TIMEOUT 90)
    add_executable(renderlab_content_tests tests/content_tests.cpp)
    target_link_libraries(renderlab_content_tests PRIVATE renderlab_options renderlab_content
        renderlab_shader nlohmann_json::nlohmann_json)
    add_custom_command(TARGET renderlab_content_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_content_tests> VERBATIM)
    add_test(NAME content.contracts COMMAND renderlab_content_tests
        ${CMAKE_SOURCE_DIR}/sample/m4/RenderLab.rlproject ${CMAKE_BINARY_DIR}/content-tests
        ${CMAKE_SOURCE_DIR}/shaders)
    set_tests_properties(content.contracts PROPERTIES TIMEOUT 60)
    add_executable(renderlab_rendergraph_tests tests/rendergraph_tests.cpp)
    target_link_libraries(renderlab_rendergraph_tests PRIVATE renderlab_options renderlab_rendergraph renderlab_rhi renderlab_renderer)
    add_test(NAME rendergraph.contracts COMMAND renderlab_rendergraph_tests)
    add_executable(renderlab_alias_tests tests/alias_tests.cpp)
    target_link_libraries(renderlab_alias_tests PRIVATE renderlab_options renderlab_rendergraph
        renderlab_rhi)
    add_custom_command(TARGET renderlab_alias_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_alias_tests> VERBATIM)
    add_executable(renderlab_shader_tests tests/shader_tests.cpp)
    target_link_libraries(renderlab_shader_tests PRIVATE renderlab_options renderlab_material
        renderlab_shader renderlab_rhi)
    add_custom_command(TARGET renderlab_shader_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_dxc>
            $<TARGET_FILE_DIR:renderlab_shader_tests> VERBATIM)
    add_test(NAME shader.contracts COMMAND renderlab_shader_tests
        ${CMAKE_SOURCE_DIR}/shaders/m2 ${CMAKE_BINARY_DIR}/shader-tests)
    set_tests_properties(shader.contracts PROPERTIES TIMEOUT 90)
    add_executable(renderlab_core_tests tests/core_tests.cpp)
    target_link_libraries(renderlab_core_tests PRIVATE renderlab_options renderlab_core
        renderlab_sandbox_options renderlab_rhi)
    add_custom_command(TARGET renderlab_core_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_core_tests> VERBATIM)
    add_test(NAME core.contracts COMMAND renderlab_core_tests)
    add_test(NAME cli.help COMMAND renderlab_sandbox --help)
    add_test(NAME cli.player_requires_project COMMAND renderlab_player)
    set_tests_properties(cli.player_requires_project PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli.invalid_backend COMMAND renderlab_sandbox --backend unknown)
    add_test(NAME cli.invalid_adapter COMMAND renderlab_sandbox --adapter -1)
    add_test(NAME cli.unknown_option COMMAND renderlab_sandbox --unknown)
    set_tests_properties(cli.invalid_backend cli.invalid_adapter cli.unknown_option
        PROPERTIES WILL_FAIL TRUE)
    if(RENDERLAB_GPU_TESTS)
        foreach(backend IN ITEMS dx12 vulkan)
            add_test(NAME gpu.${backend}.alias COMMAND renderlab_alias_tests ${backend}
                ${CMAKE_BINARY_DIR}/alias/${backend})
            set_tests_properties(gpu.${backend}.alias PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.indexed_scene COMMAND renderlab_indexed_scene_tests ${backend}
                ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject
                ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/indexed-scene/${backend})
            set_tests_properties(gpu.${backend}.indexed_scene PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 180)
            add_test(NAME gpu.${backend}.descriptor_array COMMAND renderlab_descriptor_array_tests
                ${backend} ${CMAKE_BINARY_DIR}/descriptor-array/${backend})
            set_tests_properties(gpu.${backend}.descriptor_array PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 120)
            add_test(NAME gpu.${backend}.indirect COMMAND renderlab_indirect_tests ${backend}
                ${CMAKE_SOURCE_DIR}/shaders/RenderLab ${CMAKE_BINARY_DIR}/indirect/${backend})
            set_tests_properties(gpu.${backend}.indirect PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 120)
            add_test(NAME gpu.${backend}.render_state COMMAND renderlab_render_state_tests
                ${backend} ${CMAKE_BINARY_DIR}/render-state/${backend})
            set_tests_properties(gpu.${backend}.render_state PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)
            add_test(NAME gpu.${backend}.tools_window COMMAND renderlab_sandbox
                --backend ${backend} --validation on --window-test --debug-panel --fixed-camera
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject
                --width 640 --height 360 --quality medium
                --profile ${CMAKE_BINARY_DIR}/tools/${backend}-window.json
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-tools-window.log)
            add_test(NAME gpu.${backend}.player COMMAND renderlab_player
                --backend ${backend} --validation on --headless --benchmark 3
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject
                --width 257 --height 193 --quality low --view shadow
                --profile ${CMAKE_BINARY_DIR}/tools/${backend}-player.json
                --output ${CMAKE_BINARY_DIR}/tools/player
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-player.log)
            set_tests_properties(gpu.${backend}.tools_window gpu.${backend}.player PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.surface COMMAND renderlab_surface_tests ${backend}
                ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject ${CMAKE_SOURCE_DIR}/shaders
                ${CMAKE_BINARY_DIR}/surface/${backend})
            set_tests_properties(gpu.${backend}.surface PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 120)
            add_test(NAME gpu.${backend}.surface_window COMMAND renderlab_sandbox
                --backend ${backend} --validation on --window-test
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Shadow.rlproject
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-surface-window.log)
            set_tests_properties(gpu.${backend}.surface_window PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)
            add_test(NAME gpu.${backend}.pbr_window COMMAND renderlab_sandbox
                --backend ${backend} --validation on --window-test
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-pbr-window.log)
            set_tests_properties(gpu.${backend}.pbr_window PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)
            add_test(NAME gpu.${backend}.deferred COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --pipeline deferred
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject
                --width 640 --height 360 --quality medium
                --output ${CMAKE_BINARY_DIR}/deferred
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-deferred.log)
            set_tests_properties(gpu.${backend}.deferred PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.ray_query_scene COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --scene-test --pipeline raytracing
                --project ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject
                --output ${CMAKE_BINARY_DIR}/ray-query-scene
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-ray-query-scene.log)
            set_tests_properties(gpu.${backend}.ray_query_scene PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 480)
            add_test(NAME gpu.${backend}.reflections COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --scene-test --pipeline raytracing
                --project ${CMAKE_SOURCE_DIR}/sample/m4/RayTracing.rlproject
                --output ${CMAKE_BINARY_DIR}/reflections
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-reflections.log)
            set_tests_properties(gpu.${backend}.reflections PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 480)
            add_test(NAME gpu.${backend}.reflection_window COMMAND renderlab_sandbox
                --backend ${backend} --validation on --frames 16 --debug-panel --fixed-camera
                --pipeline raytracing --project ${CMAKE_SOURCE_DIR}/sample/m4/RayTracing.rlproject
                --width 1920 --height 1080
                --profile ${CMAKE_BINARY_DIR}/tools/${backend}-reflection-window.json
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-reflection-window.log)
            set_tests_properties(gpu.${backend}.reflection_window PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.pbr COMMAND renderlab_pbr_tests ${backend}
                ${CMAKE_SOURCE_DIR}/sample/m4/Pbr.rlproject ${CMAKE_SOURCE_DIR}/shaders
                ${CMAKE_BINARY_DIR}/pbr/${backend})
            set_tests_properties(gpu.${backend}.pbr PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 120)
            add_test(NAME gpu.${backend}.scene COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --scene-test
                --project ${CMAKE_SOURCE_DIR}/sample/m4/RenderLab.rlproject
                --output ${CMAKE_BINARY_DIR}/scene --log ${CMAKE_BINARY_DIR}/logs/${backend}-scene.log)
            set_tests_properties(gpu.${backend}.scene PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.graph COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --graph-test
                --output ${CMAKE_BINARY_DIR}/graph --log ${CMAKE_BINARY_DIR}/logs/${backend}-graph.log)
            set_tests_properties(gpu.${backend}.graph PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)
            add_test(NAME gpu.${backend}.material COMMAND renderlab_material_demo ${backend}
                ${CMAKE_SOURCE_DIR}/shaders/m2/surface.shader ${CMAKE_BINARY_DIR}/material/${backend})
            set_tests_properties(gpu.${backend}.material PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 90)
            add_test(NAME gpu.${backend}.init COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on
                --report ${CMAKE_BINARY_DIR}/reports/${backend}.json
                --log ${CMAKE_BINARY_DIR}/logs/${backend}.log)
            add_test(NAME gpu.${backend}.window COMMAND renderlab_sandbox
                --backend ${backend} --validation on --window-test
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-window.log)
            add_test(NAME gpu.${backend}.rhi COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --rhi-test
                --output ${CMAKE_BINARY_DIR}/acceptance
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-rhi.log)
            add_test(NAME gpu.${backend}.acceleration_structure COMMAND renderlab_sandbox
                --backend ${backend} --headless --validation on --as-test
                --output ${CMAKE_BINARY_DIR}/acceleration-structure
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-acceleration-structure.log)
            add_test(NAME gpu.${backend}.immediate_exit COMMAND renderlab_sandbox
                --backend ${backend} --validation on --frames 1
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-exit.log)
            set_tests_properties(gpu.${backend}.init gpu.${backend}.window gpu.${backend}.rhi
                gpu.${backend}.acceleration_structure
                gpu.${backend}.immediate_exit PROPERTIES
                LABELS gpu RUN_SERIAL TRUE TIMEOUT 30)
        endforeach()
    endif()
endif()
