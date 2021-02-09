cmake_minimum_required(VERSION 3.19)
project(a.out)

set(CMAKE_CXX_STANDARD 20)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME}
        OpenGL::GL
        glfw
        GLEW
)

target_precompile_headers(${PROJECT_NAME} PUBLIC pch.h)
