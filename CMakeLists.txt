cmake_minimum_required(VERSION 3.16)

project(CubeShot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Общие модули пока пустые. После добавления .cpp замените INTERFACE
# на STATIC, перечислите исходники и используйте PUBLIC вместо INTERFACE.
add_library(cubeshot_game INTERFACE)
target_include_directories(cubeshot_game INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(cubeshot_network INTERFACE)
target_include_directories(cubeshot_network INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_package(SDL3 3.2 REQUIRED CONFIG)

add_executable(cubeshot_client src/client/main.cpp)
target_link_libraries(cubeshot_client PRIVATE cubeshot_game cubeshot_network SDL3::SDL3)

add_executable(cubeshot_server src/server/main.cpp)
target_link_libraries(cubeshot_server PRIVATE cubeshot_game cubeshot_network)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
