cmake_minimum_required(VERSION 3.21)

get_filename_component(proj_name ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" proj_name "${proj_name}")

project(${proj_name} LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_executable(${proj_name}
  main.cpp
)
target_link_libraries(${proj_name} Qt${QT_VERSION_MAJOR}::Core)

install(TARGETS ${proj_name}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
