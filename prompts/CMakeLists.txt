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

# Always: clangd and the code-spanning tools read the build directory.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/import.cmake)

file(GLOB PROMPT_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prompts/*.cpp")

# The `new` prompt embeds the SDK headers and an example prompt so an AI can
# write a consistent new one. They are baked into the binary as raw strings at
# build time rather than read from the source tree at run time, so the binary
# stays self-contained; the header is regenerated whenever one of them changes.
set(PROMPT_ASSETS
    prompt_registry_hpp|src/sdk/prompt_registry.hpp
    prompt_context_hpp|src/sdk/prompt_context.hpp
    errors_hpp|src/sdk/errors.hpp
    embed_hpp|src/sdk/embed.hpp
    args_hpp|src/sdk/args.hpp
    completion_hpp|src/sdk/completion.hpp
    sink_types_hpp|src/sdk/sink_types.hpp
    sink_hpp|src/sdk/sink.hpp
    process_hpp|src/core/process.hpp
    fs_hpp|src/core/fs.hpp
    example_fix_cpp|src/prompts/fix.cpp
)
set(PROMPT_ASSET_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(PROMPT_ASSET_HPP ${PROMPT_ASSET_DIR}/prompt_assets.hpp)
set(PROMPT_ASSET_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed-assets.cmake)
foreach(asset IN LISTS PROMPT_ASSETS)
    string(FIND "${asset}" "|" bar)
    math(EXPR asset_start "${bar} + 1")
    string(SUBSTRING "${asset}" ${asset_start} -1 asset_relpath)
    list(APPEND PROMPT_ASSET_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/${asset_relpath}")
endforeach()
add_custom_command(
    OUTPUT ${PROMPT_ASSET_HPP}
    COMMAND ${CMAKE_COMMAND}
        "-DASSET_OUT=${PROMPT_ASSET_HPP}"
        "-DASSET_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DASSET_FILES=${PROMPT_ASSETS}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed-assets.cmake"
    DEPENDS ${PROMPT_ASSET_DEPS}
    VERBATIM
)

# OBJECT, not a static library: every prompt registers itself from its own
# translation unit (see sdk/prompt_registry.hpp), and a static archive only
# pulls in members that resolve some undefined symbol — the registrar of a
# prompt nothing references would be dropped, silently unregistering it.
add_library(prompt_lib OBJECT
    src/core/fs.cpp
    src/core/process.cpp
    src/core/clipboard.cpp
    src/core/sanitize.cpp
    src/core/strings.cpp
    src/sdk/prompt_context.cpp
    src/sdk/prompt_registry.cpp
    src/sdk/chaining.cpp
    src/sdk/pipeline.cpp
    src/sdk/embed.cpp
    src/sdk/args.cpp
    src/sdk/sink.cpp
    ${PROMPT_SRCS}
)
target_sources(prompt_lib PRIVATE ${PROMPT_ASSET_HPP})

target_compile_features(prompt_lib PUBLIC cxx_std_26)
target_include_directories(prompt_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROMPT_ASSET_DIR}
)
target_link_libraries(prompt_lib PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(prompt src/main.cpp)
target_link_libraries(prompt PRIVATE prompt_lib)
target_compile_definitions(prompt PRIVATE PROMPT_VERSION="${PROMPT_VERSION}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_testing()
    add_subdirectory(tests)
endif()

# Component "prompt" keeps `cmake --install --component prompt` from also
# installing the FetchContent'd dependencies (httplib/date) into the prefix.
install(TARGETS prompt RUNTIME DESTINATION bin COMPONENT prompt)
