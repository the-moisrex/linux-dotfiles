cmake_minimum_required(VERSION 3.16)

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

find_package(Poco REQUIRED COMPONENTS Foundation Net Zip)

add_executable(${exec_name}
  main.cpp
  )
target_link_libraries(${exec_name} 
    PUBLIC Poco::Foundation Poco::Zip Poco::Net
  )

# optimize the file size:
#target_compile_options(${exec_name} PRIVATE -pthread)
#add_custom_target(de COMMAND ${CMAKE_COMMAND} -E echo "\'$<$<CONFIG:Release>:${PROJECT_RELEASE_OPTIONS}>\'")
#target_compile_options(${exec_name} PRIVATE "$<$<CONFIG:Release>:${PROJECT_RELEASE_OPTIONS}>")
#target_compile_options(${exec_name} PRIVATE "$<$<CONFIG:Debug>:${PROJECT_DEBUG_OPTIONS}>")

target_precompile_headers(${exec_name} PUBLIC pch.h)

install(TARGETS ${exec_name} DESTINATION bin)



