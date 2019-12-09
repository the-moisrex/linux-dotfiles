cmake_minimum_required(VERSION 3.12)

project(a.out)
set(exec_name a.out)

add_compile_options("-std=c++2a")
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-pthread")

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Debug
       CACHE STRING "Choose the type of build : None Debug Release RelWithDebInfo MinSizeRel Coverage."
       FORCE)
endif()
message("Current build type is: ${CMAKE_BUILD_TYPE}")


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PROJECT_RELEASE_OPTIONS "-s -O3 -flto -Qy -ffunction-sections -fdata-sections -Wl,--gc-sections")
set(PROJECT_DEBUG_OPTIONS "-g")


#add_compile_options("$<$<CONFIG:Release>:${PROJECT_RELEASE_OPTIONS}>")
#add_compile_options("$<$<CONFIG:Debug>:${PROJECT_DEBUG_OPTIONS}>")
if(CMAKE_BUILD_TYPE MATCHES "Release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROJECT_RELEASE_OPTIONS}")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROJECT_DEBUG_OPTIONS}")
endif()


find_package(OpenCV REQUIRED)
include_directories( ${OpenCV_INCLUDE_DIRS} )

add_executable(${exec_name} main.cpp)
target_link_libraries(${exec_name} ${OpenCV_LIBS})


