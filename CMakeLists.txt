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

# Reflow sources
file(GLOB_RECURSE REFLOW_SOURCES
    src/*.cpp
    src/*.c
)

add_executable(reflow ${REFLOW_SOURCES})

target_include_directories(reflow PRIVATE
    src
    thirdparty/glad/include
    thirdparty/glm
)

target_link_libraries(reflow PRIVATE glfw glad)

# Windows: hide console in Release
if(WIN32)
    set_target_properties(reflow PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()
