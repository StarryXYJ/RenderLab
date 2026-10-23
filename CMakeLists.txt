cmake_minimum_required(VERSION 3.28)
project(RenderLab VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenderLab currently requires Windows x64.")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset windows-debug")
endif()

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

add_library(renderlab_core STATIC src/core/log.cpp)
target_include_directories(renderlab_core PUBLIC src)
target_link_libraries(renderlab_core PRIVATE renderlab_options)

add_library(renderlab_platform STATIC src/platform/window.cpp src/platform/text.cpp)
target_include_directories(renderlab_platform PUBLIC src)
target_link_libraries(renderlab_platform PRIVATE renderlab_options user32)

add_library(renderlab_rhi STATIC
    src/rhi/device.cpp
    src/rhi/resources.cpp
    src/rhi/commands.cpp
    src/rhi/runtime.cpp
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

add_library(renderlab_sandbox_options STATIC src/apps/sandbox/options.cpp)
target_include_directories(renderlab_sandbox_options PUBLIC src)
target_link_libraries(renderlab_sandbox_options PRIVATE renderlab_options)

add_executable(renderlab_sandbox src/apps/sandbox/main.cpp src/apps/sandbox/presentation.cpp)
add_library(renderlab_samples STATIC
    src/samples/rhi_demo.cpp
    src/samples/rhi_acceptance.cpp
    src/samples/rhi_contract_checks.cpp
    src/samples/shader_io.cpp)
target_include_directories(renderlab_samples PUBLIC src)
target_link_libraries(renderlab_samples PRIVATE renderlab_options renderlab_rhi renderlab_core renderlab_platform)
add_dependencies(renderlab_sandbox renderlab_m1_shaders)
add_custom_command(TARGET renderlab_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
        $<TARGET_FILE_DIR:renderlab_sandbox> VERBATIM)
target_link_libraries(renderlab_sandbox PRIVATE renderlab_options renderlab_core
    renderlab_platform renderlab_rhi renderlab_sandbox_options renderlab_samples)

if(BUILD_TESTING)
    add_executable(renderlab_core_tests tests/core_tests.cpp)
    target_link_libraries(renderlab_core_tests PRIVATE renderlab_options renderlab_core
        renderlab_sandbox_options renderlab_rhi)
    add_custom_command(TARGET renderlab_core_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:renderlab_pix>
            $<TARGET_FILE_DIR:renderlab_core_tests> VERBATIM)
    add_test(NAME core.contracts COMMAND renderlab_core_tests)
    add_test(NAME cli.help COMMAND renderlab_sandbox --help)
    add_test(NAME cli.invalid_backend COMMAND renderlab_sandbox --backend unknown)
    add_test(NAME cli.invalid_adapter COMMAND renderlab_sandbox --adapter -1)
    add_test(NAME cli.unknown_option COMMAND renderlab_sandbox --unknown)
    set_tests_properties(cli.invalid_backend cli.invalid_adapter cli.unknown_option
        PROPERTIES WILL_FAIL TRUE)
    if(RENDERLAB_GPU_TESTS)
        foreach(backend IN ITEMS dx12 vulkan)
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
            add_test(NAME gpu.${backend}.immediate_exit COMMAND renderlab_sandbox
                --backend ${backend} --validation on --frames 1
                --log ${CMAKE_BINARY_DIR}/logs/${backend}-exit.log)
            set_tests_properties(gpu.${backend}.init gpu.${backend}.window gpu.${backend}.rhi
                gpu.${backend}.immediate_exit PROPERTIES
                LABELS gpu RUN_SERIAL TRUE TIMEOUT 30)
        endforeach()
    endif()
endif()
