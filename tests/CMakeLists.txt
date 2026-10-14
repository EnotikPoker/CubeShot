add_executable(cubeshot_game_tests game_tests.cpp)
target_link_libraries(cubeshot_game_tests PRIVATE cubeshot_game)
add_test(NAME game COMMAND cubeshot_game_tests)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_renderer_tests
        renderer_tests.cpp
        ../src/client/SceneRenderer.cpp
    )
    target_link_libraries(cubeshot_renderer_tests PRIVATE cubeshot_game SDL3::SDL3)
    add_test(NAME renderer COMMAND cubeshot_renderer_tests)

    add_executable(cubeshot_menu_tests
        menu_tests.cpp
        ../src/client/Menu.cpp
        ../src/client/MenuRenderer.cpp
        ../src/client/BitmapFont.cpp
        ../src/client/Input.cpp
    )
    target_link_libraries(cubeshot_menu_tests PRIVATE cubeshot_game SDL3::SDL3)
    add_test(NAME menu COMMAND cubeshot_menu_tests)
endif()

add_executable(cubeshot_match_tests match_tests.cpp)
target_link_libraries(cubeshot_match_tests PRIVATE cubeshot_game)
add_test(NAME match COMMAND cubeshot_match_tests)
add_executable(cubeshot_protocol_tests protocol_tests.cpp)
target_link_libraries(cubeshot_protocol_tests PRIVATE cubeshot_network)
add_test(NAME protocol COMMAND cubeshot_protocol_tests)
add_executable(cubeshot_network_tests network_tests.cpp)
target_link_libraries(cubeshot_network_tests PRIVATE cubeshot_network GameNetworkingSockets::static)
add_test(NAME network COMMAND cubeshot_network_tests)
set_tests_properties(network PROPERTIES TIMEOUT 60)
add_executable(cubeshot_discovery_tests discovery_tests.cpp)
target_link_libraries(cubeshot_discovery_tests PRIVATE cubeshot_network)
add_test(NAME discovery COMMAND cubeshot_discovery_tests)
set_tests_properties(discovery PROPERTIES TIMEOUT 15)
if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_host_process_tests host_process_tests.cpp ../src/client/HostProcess.cpp)
    target_link_libraries(cubeshot_host_process_tests PRIVATE cubeshot_network SDL3::SDL3)
    add_dependencies(cubeshot_host_process_tests cubeshot_server)
    add_test(NAME host_process COMMAND cubeshot_host_process_tests)
    set_tests_properties(host_process PROPERTIES TIMEOUT 15)
endif()
