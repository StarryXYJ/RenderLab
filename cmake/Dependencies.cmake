include(FetchContent)

# Vulkan SDK 1.4.321.0 sources; the runtime minimum remains Vulkan 1.3.
# FetchContent sources/build products remain inside the ignored build tree.
# SDK tools/layers are local; installing a newer SDK does not replace these pinned sources.
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 2cd90f9d20df57eac214c148f3aed885372ddcfe)
FetchContent_MakeAvailable(vulkan_headers)

set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "Use the pinned Vulkan-Headers target" FORCE)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG a8da8ef3368482b0ee9b0ec0c6079a16a89c6924)
FetchContent_MakeAvailable(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)
target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)

FetchContent_Declare(spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG e55086b044225f9b511ae44dbf9b079d3625943f)
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "Disable third-party command line tool" FORCE)
set(SPIRV_REFLECT_STATIC_LIB OFF CACHE BOOL "Use the scoped RenderLab reflection target" FORCE)
FetchContent_MakeAvailable(spirv_reflect)
add_library(renderlab_spirv_reflect STATIC "${spirv_reflect_SOURCE_DIR}/spirv_reflect.c")
target_include_directories(renderlab_spirv_reflect SYSTEM PUBLIC "${spirv_reflect_SOURCE_DIR}")

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b)
set(JSON_BuildTests OFF CACHE BOOL "Disable dependency tests" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(winpix
    URL https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001
    URL_HASH SHA256=726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(winpix)
add_library(renderlab_pix SHARED IMPORTED)
set_target_properties(renderlab_pix PROPERTIES
    IMPORTED_IMPLIB "${winpix_SOURCE_DIR}/bin/x64/WinPixEventRuntime.lib"
    IMPORTED_LOCATION "${winpix_SOURCE_DIR}/bin/x64/WinPixEventRuntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${winpix_SOURCE_DIR}/Include/WinPixEventRuntime")
