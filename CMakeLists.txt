cmake_minimum_required(VERSION 3.21)
project(CubeShot VERSION 0.12.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
option(CUBESHOT_BUILD_CLIENT "Build the SDL3 client and interface tests" ON)

add_library(cubeshot_game STATIC
    src/game/BodyPhysics.cpp src/game/Arena.cpp src/game/Bullet.cpp src/game/Collision.cpp
    src/game/BotNavigation.cpp src/game/BotController.cpp src/game/BotMatch.cpp
    src/game/Prediction.cpp src/game/Player.cpp src/game/Weapon.cpp src/game/World.cpp src/game/Match.cpp)
target_include_directories(cubeshot_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(cubeshot_game PUBLIC CUBESHOT_VERSION="${PROJECT_VERSION}")

set(protobuf_MODULE_COMPATIBLE ON)
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
find_package(OpenSSL 3.0 REQUIRED)
include(cmake/OnlineDependencies.cmake)
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
    src/network/Identity.cpp src/network/Protocol.cpp src/network/Address.cpp
    src/network/Client.cpp src/network/Server.cpp src/network/detail/GnsRuntime.cpp
    src/network/SnapshotInterpolation.cpp src/network/NetworkSimulation.cpp src/network/Discovery.cpp)
target_include_directories(cubeshot_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cubeshot_network PUBLIC cubeshot_game protobuf::libprotobuf PRIVATE GameNetworkingSockets::static OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(cubeshot_network PRIVATE ws2_32 iphlpapi)
endif()

add_library(cubeshot_directory STATIC src/network/Directory.cpp)
target_link_libraries(cubeshot_directory PUBLIC cubeshot_network PRIVATE httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)

if(CUBESHOT_BUILD_CLIENT)
    find_package(SDL3 3.2 REQUIRED CONFIG)
    add_library(cubeshot_preferences STATIC
        src/client/Settings.cpp src/client/SettingsStore.cpp src/client/Profile.cpp
        src/client/WindowSettings.cpp src/client/SettingsController.cpp src/client/SettingsRenderer.cpp
        src/client/BitmapFont.cpp src/client/Input.cpp)
    target_link_libraries(cubeshot_preferences PUBLIC cubeshot_game SDL3::SDL3 PRIVATE cubeshot_directory nlohmann_json::nlohmann_json)
    add_executable(cubeshot_client
        src/client/main.cpp src/client/Application.cpp src/client/ConnectionController.cpp
        src/client/BotGameController.cpp
        src/client/Menu.cpp src/client/MenuRenderer.cpp
        src/client/SceneRenderer.cpp src/client/Effects.cpp src/client/HostProcess.cpp)
    target_link_libraries(cubeshot_client PRIVATE cubeshot_preferences cubeshot_network cubeshot_directory)
    if(WIN32)
        set_target_properties(cubeshot_client PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()
add_executable(cubeshot_server src/server/main.cpp)
target_link_libraries(cubeshot_server PRIVATE cubeshot_network)
add_library(cubeshot_online STATIC
    src/online/Service.cpp src/online/ArenaPool.cpp src/online/Reservations.cpp src/online/HttpApi.cpp)
target_link_libraries(cubeshot_online PUBLIC cubeshot_network PRIVATE httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)
add_executable(cubeshot_online_server src/online/main.cpp)
target_link_libraries(cubeshot_online_server PRIVATE cubeshot_online)
install(TARGETS cubeshot_online_server RUNTIME DESTINATION bin)
include(cmake/PortablePackage.cmake)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
install(TARGETS cubeshot_server RUNTIME DESTINATION bin)
if(CUBESHOT_BUILD_CLIENT)
    install(TARGETS cubeshot_client RUNTIME DESTINATION bin)
endif()

if(WIN32)
    set(runtime_targets cubeshot_server cubeshot_online_server)
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

# Project flags must not change the build of fetched third-party libraries.
# Valve's internal headers contain legacy-encoded comments and manage their own
# Windows header definitions. Apply our Unicode and Windows settings only to
# CubeShot compilation units, including the interface tests.
get_property(cubeshot_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
if(BUILD_TESTING)
    get_property(cubeshot_test_targets DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests" PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND cubeshot_targets ${cubeshot_test_targets})
endif()
foreach(app IN LISTS cubeshot_targets)
    get_target_property(target_type ${app} TYPE)
    if(app MATCHES "^cubeshot_" AND (target_type STREQUAL "STATIC_LIBRARY" OR target_type STREQUAL "EXECUTABLE"))
        if(MSVC)
            target_compile_options(${app} PRIVATE /utf-8)
        endif()
        if(WIN32)
            target_compile_definitions(${app} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
    endif()
endforeach()
