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

# Create an executable
add_executable(${PROJECT_NAME} main.cxx)

# Link to the library foo
target_link_libraries(${PROJECT_NAME} ${LIB_NAME})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_features(${LIB_NAME} PUBLIC cxx_std_23)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG TRUE)
    message(STATUS "Debug mode is enabled.")
else ()
    set(IS_DEBUG FALSE)
    message(STATUS "Debug mode is disabled.")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(IS_GCC TRUE)
    set(IS_CLANG FALSE)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(IS_GCC FALSE)
    set(IS_CLANG TRUE)
endif ()

if (IS_GCC AND IS_DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O3 -fopt-info-missed -fconcepts-diagnostics-depth=10 -ftemplate-backtrace-limit=0 -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer")

    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ftime-report")
elseif (IS_CLANG AND IS_DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O3 -Rpass-missed -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")

    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ftime-trace")
endif ()


target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{HOME}/Projects/webpp")
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
