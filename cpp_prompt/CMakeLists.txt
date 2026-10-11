cmake_minimum_required(VERSION 3.28)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROMPT_VERSION)
string(STRIP "${PROMPT_VERSION}" PROMPT_VERSION)

project(prompt VERSION ${PROMPT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(IS_GCC TRUE)
    set(IS_CLANG FALSE)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(IS_GCC FALSE)
    set(IS_CLANG TRUE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (IS_GCC)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow -fsanitize=address")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
    elseif (IS_CLANG)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
    endif()
endif()

if (IS_CLANG)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/import.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
set(Python3_EMBED_LIBRARY "/usr/lib/python3.14/config-3.14-x86_64-linux-gnu/libpython3.14.a")

file(GLOB PROMPT_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prompts/*.cpp")

add_library(prompt_lib
    src/core/fs.cpp
    src/core/process.cpp
    src/core/clipboard.cpp
    src/core/sanitize.cpp
    src/core/python.cpp
    src/sdk/prompt_context.cpp
    src/sdk/prompt_registry.cpp
    src/sdk/chaining.cpp
    src/sdk/pipeline.cpp
    src/sdk/embed.cpp
    src/sdk/args.cpp
    src/tse/tse_python.cpp
    src/prompts/prompt_entry.cpp
    src/prompts/register_batches.cpp
    ${PROMPT_SRCS}
)

target_compile_features(prompt_lib PUBLIC cxx_std_26)
target_include_directories(prompt_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(prompt_lib PUBLIC
    nlohmann_json::nlohmann_json
    date::date
    fmt::fmt
    ${Python3_LIBRARIES}
    ${Python3_EMBED_LIBRARY}
)

add_executable(prompt src/main.cpp)
target_link_libraries(prompt PRIVATE prompt_lib)
target_compile_definitions(prompt PRIVATE PROMPT_VERSION="${PROMPT_VERSION}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS prompt RUNTIME DESTINATION bin)
