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
