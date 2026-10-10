# Try find_package first (system-installed), fall back to FetchContent
# This is much faster if packages are already available

# nlohmann_json
find_package(nlohmann_json 3.11 QUIET)
if (NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# cpp-httplib
find_package(cpp-httplib 0.15 QUIET)
if (NOT cpp-httplib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.15.3
    )
    FetchContent_MakeAvailable(cpp-httplib)
endif()

# date (Howard Hinnant's date library)
find_package(date 3.0 QUIET)
if (NOT date_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        date
        GIT_REPOSITORY https://github.com/HowardHinnant/date.git
        GIT_TAG v3.0.1
    )
    FetchContent_MakeAvailable(date)
endif()

# fmt
find_package(fmt 10.2 QUIET)
if (NOT fmt_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 10.2.1
    )
    FetchContent_MakeAvailable(fmt)
endif()

# For benchmarking (Debug only)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    find_package(benchmark 1.8 QUIET)
    if (NOT benchmark_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        FetchContent_MakeAvailable(benchmark)
    endif()
endif()