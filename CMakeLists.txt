cmake_minimum_required(VERSION 3.15)

project(Chess)

set(CMAKE_CXX_STANDARD 17)

# GLFW
add_subdirectory(glfw)

# EXE
add_executable(Chess
    main.cpp
    glad/src/gl.c
)

# Include paths
target_include_directories(Chess PRIVATE
    glad/include
    glm
    glfw/include
)

# Link
target_link_libraries(Chess glfw)

# Windows OpenGL
if(WIN32)
    target_link_libraries(Chess opengl32)
endif()