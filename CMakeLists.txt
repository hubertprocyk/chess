cmake_minimum_required(VERSION 3.15)

project(Chess)

set(CMAKE_CXX_STANDARD 17)

# GLFW
add_subdirectory(glfw)

# EXE
add_executable(Chess
    main.cpp
    pgn.cpp          # <-- moduł parsera PGN
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

# Kopiowanie folderu z modelami
add_custom_command(TARGET Chess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:Chess>/models"
    COMMENT "Kopiowanie folderu z modelami do katalogu budowania..."
)

# Kopiowanie folderu z grami
add_custom_command(TARGET Chess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/games"
        "$<TARGET_FILE_DIR:Chess>/games"
    COMMENT "Kopiowanie folderu z grami do katalogu budowania..."
)
