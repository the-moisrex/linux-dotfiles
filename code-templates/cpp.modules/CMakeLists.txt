# Tutorial:
#   https://www.kitware.com/import-cmake-the-experiment-is-over/

cmake_minimum_required(VERSION 3.28)
project(a.out CXX)

set(LIB_NAME ${PROJECT_NAME}-lib)

# Turning off extensions avoids and issue with the clang 16 compiler
# clang 17 and greater can avoid this setting
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the version of C++ for the project
set(CMAKE_CXX_STANDARD 26)

# Create a library
add_library(${LIB_NAME})

# Add the module file to the library
target_sources(${LIB_NAME}
  PUBLIC
    FILE_SET CXX_MODULES FILES
      foo.cxx
)

# Create an executable
add_executable(${PROJECT_NAME} main.cxx)

# Link to the library foo
target_link_libraries(${PROJECT_NAME} ${LIB_NAME})
