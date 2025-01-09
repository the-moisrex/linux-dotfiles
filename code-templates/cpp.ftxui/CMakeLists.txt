# Tutorial:
#   https://www.kitware.com/import-cmake-the-experiment-is-over/

cmake_minimum_required(VERSION 3.28...3.29)
project(a.out CXX)

set(LIB_NAME ${PROJECT_NAME}-lib)

# Turning off extensions avoids and issue with the clang 16 compiler
# clang 17 and greater can avoid this setting
set(CMAKE_CXX_EXTENSIONS OFF)

# Create a library
add_library(${LIB_NAME})

# Add the module file to the library
target_sources(${LIB_NAME}
  PUBLIC
    FILE_SET CXX_MODULES FILES
      standard.cxx
)

include(CPM.cmake)

CPMAddPackage(
  NAME ftxui
  URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v5.0.0.tar.gz
  VERSION 5.0.0
)

# Create an executable
add_executable(${PROJECT_NAME} main.cxx)

# Link to the library foo
target_link_libraries(${PROJECT_NAME}
    PRIVATE ${LIB_NAME}
    PRIVATE screen
    PRIVATE dom
    PRIVATE component
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_features(${LIB_NAME} PUBLIC cxx_std_23)

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

set_target_properties(${LIB_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
