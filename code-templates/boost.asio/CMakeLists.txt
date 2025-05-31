cmake_minimum_required(VERSION 3.16..4.2)

project(
    a.out
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(PROJECT_RELEASE_OPTIONS "-s -O3 -Qy -ffunction-sections -fdata-sections -Wl,--gc-sections")
    set(PROJECT_DEBUG_OPTIONS "-g -O0 -Wall -Wextra")

    if (CMAKE_BUILD_TYPE MATCHES "Release")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROJECT_RELEASE_OPTIONS}")
    else ()
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROJECT_DEBUG_OPTIONS}")
    endif ()
endif ()


set(Boost_USE_STATIC_LIBS        ON)  # only find static libs
set(Boost_USE_DEBUG_LIBS         ON)  # ignore debug libs and
set(Boost_USE_RELEASE_LIBS       ON)  # only find release libs
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(Boost CONFIG COMPONENTS program_options filesystem regex)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME}
    PRIVATE Boost::system 
    PRIVATE Boost::filesystem 
    PRIVATE Boost::program_options
)
target_compile_features(${PROJECT_NAME} PUBLIC
    cxx_std_23
    cxx_auto_type
    cxx_lambdas
    cxx_constexpr
    cxx_variadic_templates
    cxx_nullptr
    cxx_attributes
    cxx_decltype
    cxx_generic_lambdas
    cxx_inline_namespaces
    cxx_lambda_init_captures
    cxx_noexcept
    cxx_range_for
    cxx_raw_string_literals
    cxx_static_assert
)
target_precompile_headers(${PROJECT_NAME} PUBLIC pch.hpp)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)



