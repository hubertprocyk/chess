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

# KOPIOWANIE FOLDERU Z MODELAMI
# Ta komenda uruchomi się po każdym udanym zbudowaniu pliku Chess.exe
# i skopiuje folder 'models' bezpośrednio tam, gdzie powstał program.
add_custom_command(TARGET Chess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:Chess>/models"
    COMMENT "Kopiowanie folderu z modelami do katalogu budowania..."
)