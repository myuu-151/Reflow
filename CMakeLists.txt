cmake_minimum_required(VERSION 3.16)
project(reflow VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/glfw)

# glad (OpenGL loader)
add_library(glad STATIC thirdparty/glad/src/glad.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)

# Dear ImGui
add_library(imgui STATIC
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    thirdparty/imgui
    thirdparty/imgui/backends
)
target_link_libraries(imgui PRIVATE glfw glad)

# stb_image
add_library(stb_image STATIC thirdparty/stb/stb_image.c)
target_include_directories(stb_image PUBLIC thirdparty/stb)

# Reflow sources
file(GLOB_RECURSE REFLOW_SOURCES
    src/*.cpp
    src/*.c
)

# Windows resource file (exe icon)
if(WIN32)
    set(REFLOW_RC res/reflow.rc)
endif()

add_executable(reflow ${REFLOW_SOURCES} ${REFLOW_RC})

target_include_directories(reflow PRIVATE
    src
    thirdparty/glad/include
    thirdparty/glm
    thirdparty/imgui
    thirdparty/imgui/backends
    thirdparty/stb
)

target_link_libraries(reflow PRIVATE glfw glad imgui stb_image)

# Copy res/ and assets/ to build output
add_custom_command(TARGET reflow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:reflow>/res
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:reflow>/assets
)

# Windows: hide console in Release
if(WIN32)
    set_target_properties(reflow PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()
