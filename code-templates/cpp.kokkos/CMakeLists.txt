cmake_minimum_required(VERSION 3.28...4.0)
project(a.out CXX)

include(FetchContent)
FetchContent_Declare(
  Kokkos
  # URL https://github.com/kokkos/kokkos/archive/refs/tags/4.5.01.zip
  URL https://github.com/kokkos/kokkos/releases/download/4.6.00/kokkos-4.6.00.zip
)
FetchContent_MakeAvailable(Kokkos)


# Create an executable
add_executable(${PROJECT_NAME} main.cpp)

# Link to the library foo
target_link_libraries(${PROJECT_NAME} Kokkos::kokkos)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
)

