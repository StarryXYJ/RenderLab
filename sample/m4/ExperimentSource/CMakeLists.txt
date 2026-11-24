add_library(example_project_render SHARED experiment.cpp)
target_link_libraries(example_project_render PRIVATE renderlab_options renderlab_rendergraph)

set(example_module_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Library/Modules")
add_custom_target(example_project
    COMMAND ${CMAKE_COMMAND} -E make_directory "${example_module_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:example_project_render>
        "${example_module_dir}/experiment.dll"
    DEPENDS example_project_render VERBATIM)
