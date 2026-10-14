cmake_minimum_required(VERSION 3.21)
project(CubeShot VERSION 0.6.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    add_compile_options(/utf-8)
endif()
option(CUBESHOT_BUILD_CLIENT "Build the SDL3 client and interface tests" ON)

add_library(cubeshot_game STATIC
    src/game/Arena.cpp src/game/Bullet.cpp src/game/Collision.cpp
    src/game/Player.cpp src/game/Weapon.cpp src/game/World.cpp src/game/Match.cpp)
target_include_directories(cubeshot_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(cubeshot_game PUBLIC CUBESHOT_VERSION="${PROJECT_VERSION}")

set(protobuf_MODULE_COMPATIBLE ON)
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
find_package(OpenSSL REQUIRED)
include(FetchContent)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ICE OFF CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG 2cb93a06350bb065db53abdb0d87cf297e0bfd34) # v1.6.0
FetchContent_MakeAvailable(GameNetworkingSockets)
protobuf_generate_cpp(PROTO_SOURCES PROTO_HEADERS src/network/protocol.proto)
add_library(cubeshot_network STATIC
    ${PROTO_SOURCES} ${PROTO_HEADERS}
    src/network/Protocol.cpp src/network/Session.cpp src/network/Discovery.cpp)
target_include_directories(cubeshot_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cubeshot_network PUBLIC cubeshot_game protobuf::libprotobuf PRIVATE GameNetworkingSockets::static)
if(WIN32)
    target_link_libraries(cubeshot_network PRIVATE ws2_32 iphlpapi)
endif()

if(CUBESHOT_BUILD_CLIENT)
    find_package(SDL3 3.2 REQUIRED CONFIG)
    add_executable(cubeshot_client
        src/client/main.cpp src/client/Application.cpp src/client/Input.cpp
        src/client/Menu.cpp src/client/MenuRenderer.cpp src/client/BitmapFont.cpp
        src/client/SceneRenderer.cpp src/client/HostProcess.cpp)
    target_link_libraries(cubeshot_client PRIVATE cubeshot_game cubeshot_network SDL3::SDL3)
endif()
add_executable(cubeshot_server src/server/main.cpp)
target_link_libraries(cubeshot_server PRIVATE cubeshot_network)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
install(TARGETS cubeshot_server RUNTIME DESTINATION bin)
if(CUBESHOT_BUILD_CLIENT)
    install(TARGETS cubeshot_client RUNTIME DESTINATION bin)
endif()

if(WIN32)
    set(runtime_targets cubeshot_server)
    if(CUBESHOT_BUILD_CLIENT)
        list(APPEND runtime_targets cubeshot_client)
    endif()
    foreach(app IN LISTS runtime_targets)
        add_custom_command(TARGET ${app} POST_BUILD
            COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:${app}>"
                "-DDESTINATION=$<TARGET_FILE_DIR:${app}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntime.cmake"
            VERBATIM)
    endforeach()
endif()
