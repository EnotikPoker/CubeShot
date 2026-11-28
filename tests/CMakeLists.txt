add_executable(cubeshot_game_tests game_tests.cpp)
target_link_libraries(cubeshot_game_tests PRIVATE cubeshot_game)
add_test(NAME game COMMAND cubeshot_game_tests)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_renderer_tests
        renderer_tests.cpp
        ../src/client/SceneRenderer.cpp
        ../src/client/Effects.cpp
    )
    target_link_libraries(cubeshot_renderer_tests PRIVATE cubeshot_game SDL3::SDL3 cubeshot_visuals)
    add_test(NAME renderer COMMAND cubeshot_renderer_tests)

    add_executable(cubeshot_menu_tests
        menu_tests.cpp
        ../src/client/Menu.cpp
        ../src/client/MenuRenderer.cpp
    )
    target_link_libraries(cubeshot_menu_tests PRIVATE cubeshot_preferences cubeshot_network)
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
set_tests_properties(network PROPERTIES TIMEOUT 90)
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

add_executable(cubeshot_prediction_tests prediction_tests.cpp)
target_link_libraries(cubeshot_prediction_tests PRIVATE cubeshot_game)
add_test(NAME prediction COMMAND cubeshot_prediction_tests)

add_executable(cubeshot_online_tests online_tests.cpp)
target_link_libraries(cubeshot_online_tests PRIVATE cubeshot_online cubeshot_directory httplib::httplib nlohmann_json::nlohmann_json)
add_test(NAME online COMMAND cubeshot_online_tests)
set_tests_properties(online PROPERTIES TIMEOUT 45)

add_executable(cubeshot_tls_tests tls_tests.cpp)
target_link_libraries(cubeshot_tls_tests PRIVATE cubeshot_directory httplib::httplib OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME tls COMMAND cubeshot_tls_tests)
set_tests_properties(tls PROPERTIES TIMEOUT 15)

add_executable(cubeshot_interpolation_tests interpolation_tests.cpp)
target_link_libraries(cubeshot_interpolation_tests PRIVATE cubeshot_network)
add_test(NAME interpolation COMMAND cubeshot_interpolation_tests)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_connection_tests
        connection_tests.cpp
        ../src/client/ConnectionController.cpp
        ../src/client/HostProcess.cpp
        ../src/client/Menu.cpp)
    target_link_libraries(cubeshot_connection_tests PRIVATE
        cubeshot_online cubeshot_directory cubeshot_preferences SDL3::SDL3)
    add_test(NAME connection COMMAND cubeshot_connection_tests)
    set_tests_properties(connection PROPERTIES TIMEOUT 35)
endif()

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_settings_tests settings_tests.cpp ../src/client/Menu.cpp)
    target_link_libraries(cubeshot_settings_tests PRIVATE cubeshot_preferences nlohmann_json::nlohmann_json)
    add_test(NAME settings COMMAND cubeshot_settings_tests)
endif()

add_executable(cubeshot_combat_tests combat_tests.cpp)
target_link_libraries(cubeshot_combat_tests PRIVATE cubeshot_network)
add_test(NAME combat COMMAND cubeshot_combat_tests)
if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_effects_tests effects_tests.cpp
        ../src/client/Effects.cpp ../src/client/SceneRenderer.cpp)
    target_link_libraries(cubeshot_effects_tests PRIVATE cubeshot_preferences)
    add_test(NAME effects COMMAND cubeshot_effects_tests)
endif()

add_executable(cubeshot_bot_tests bot_tests.cpp)
target_link_libraries(cubeshot_bot_tests PRIVATE cubeshot_game)
add_test(NAME bots COMMAND cubeshot_bot_tests)
set_tests_properties(bots PROPERTIES TIMEOUT 60)
add_executable(cubeshot_bot_match_tests bot_match_tests.cpp)
target_link_libraries(cubeshot_bot_match_tests PRIVATE cubeshot_game)
add_test(NAME bot_match COMMAND cubeshot_bot_match_tests)
set_tests_properties(bot_match PROPERTIES TIMEOUT 60)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_bot_game_tests bot_game_tests.cpp
        ../src/client/BotGameController.cpp ../src/client/Menu.cpp)
    target_link_libraries(cubeshot_bot_game_tests PRIVATE cubeshot_directory cubeshot_preferences)
    add_test(NAME bot_game COMMAND cubeshot_bot_game_tests)
endif()

add_executable(cubeshot_bot_behavior_tests bot_behavior_tests.cpp)
target_link_libraries(cubeshot_bot_behavior_tests PRIVATE cubeshot_game)
add_test(NAME bot_behavior COMMAND cubeshot_bot_behavior_tests)
set_tests_properties(bot_behavior PROPERTIES TIMEOUT 60)

add_executable(cubeshot_weapons_tests weapons_tests.cpp)
target_link_libraries(cubeshot_weapons_tests PRIVATE cubeshot_game)
add_test(NAME weapons COMMAND cubeshot_weapons_tests)
add_executable(cubeshot_debris_tests debris_tests.cpp)
target_link_libraries(cubeshot_debris_tests PRIVATE cubeshot_game)
add_test(NAME debris COMMAND cubeshot_debris_tests)
add_executable(cubeshot_armory_tests armory_tests.cpp)
target_link_libraries(cubeshot_armory_tests PRIVATE cubeshot_game)
add_test(NAME armory COMMAND cubeshot_armory_tests)

add_executable(cubeshot_arsenal_tests arsenal_tests.cpp)
target_link_libraries(cubeshot_arsenal_tests PRIVATE cubeshot_network)
add_test(NAME arsenal COMMAND cubeshot_arsenal_tests)

add_executable(cubeshot_visual_geometry_tests visual_geometry_tests.cpp)
target_link_libraries(cubeshot_visual_geometry_tests PRIVATE cubeshot_game)
add_test(NAME visual_geometry COMMAND cubeshot_visual_geometry_tests)

add_executable(cubeshot_bot_tactics_tests bot_tactics_tests.cpp)
target_link_libraries(cubeshot_bot_tactics_tests PRIVATE cubeshot_game)
add_test(NAME bot_tactics COMMAND cubeshot_bot_tactics_tests)
set_tests_properties(bot_tactics PROPERTIES TIMEOUT 60)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_text_tests text_renderer_tests.cpp)
    target_link_libraries(cubeshot_text_tests PRIVATE cubeshot_visuals)
    add_test(NAME text_renderer COMMAND cubeshot_text_tests)
endif()

add_executable(cubeshot_weapon_rework_tests weapon_rework_tests.cpp)
target_link_libraries(cubeshot_weapon_rework_tests PRIVATE cubeshot_network)
add_test(NAME weapon_rework COMMAND cubeshot_weapon_rework_tests)

add_executable(cubeshot_abilities_tests abilities_tests.cpp)
target_link_libraries(cubeshot_abilities_tests PRIVATE cubeshot_network)
add_test(NAME abilities COMMAND cubeshot_abilities_tests)

add_executable(cubeshot_destruction_tests destruction_tests.cpp)
target_link_libraries(cubeshot_destruction_tests PRIVATE cubeshot_network)
add_test(NAME destruction COMMAND cubeshot_destruction_tests)

add_executable(cubeshot_sandbox_tests sandbox_tests.cpp)
target_link_libraries(cubeshot_sandbox_tests PRIVATE cubeshot_game)
add_test(NAME sandbox COMMAND cubeshot_sandbox_tests)

add_executable(cubeshot_bot_overhaul_tests bot_overhaul_tests.cpp)
target_link_libraries(cubeshot_bot_overhaul_tests PRIVATE cubeshot_game)
add_test(NAME bot_overhaul COMMAND cubeshot_bot_overhaul_tests)
set_tests_properties(bot_overhaul PROPERTIES TIMEOUT 60)

add_executable(cubeshot_active_tests active_tests.cpp)
target_link_libraries(cubeshot_active_tests PRIVATE cubeshot_network)
add_test(NAME active_abilities COMMAND cubeshot_active_tests)
add_executable(cubeshot_build_upgrades_tests build_upgrades_tests.cpp)
target_link_libraries(cubeshot_build_upgrades_tests PRIVATE cubeshot_game)
add_test(NAME build_upgrades COMMAND cubeshot_build_upgrades_tests)

add_executable(cubeshot_modes_tests modes_tests.cpp)
target_link_libraries(cubeshot_modes_tests PRIVATE cubeshot_network)
add_test(NAME modes COMMAND cubeshot_modes_tests)
add_executable(cubeshot_expansion_tests expansion_tests.cpp)
target_link_libraries(cubeshot_expansion_tests PRIVATE cubeshot_network)
add_test(NAME expansion COMMAND cubeshot_expansion_tests)

add_executable(cubeshot_expansion_perks_tests expansion_perks_tests.cpp)
target_link_libraries(cubeshot_expansion_perks_tests PRIVATE cubeshot_network)
add_test(NAME expansion_perks COMMAND cubeshot_expansion_perks_tests)

add_executable(cubeshot_control_network_tests control_network_tests.cpp)
target_link_libraries(cubeshot_control_network_tests PRIVATE cubeshot_network)
add_test(NAME control_network COMMAND cubeshot_control_network_tests)
set_tests_properties(control_network PROPERTIES TIMEOUT 45)

if(CUBESHOT_BUILD_CLIENT)
    add_executable(cubeshot_audio_tests audio_tests.cpp)
    target_link_libraries(cubeshot_audio_tests PRIVATE cubeshot_audio)
    add_dependencies(cubeshot_audio_tests cubeshot_assets)
    add_test(NAME audio COMMAND cubeshot_audio_tests)
    set_tests_properties(audio PROPERTIES TIMEOUT 20)
    add_executable(cubeshot_audio_network_tests audio_network_tests.cpp)
    target_link_libraries(cubeshot_audio_network_tests PRIVATE cubeshot_audio cubeshot_network)
    add_dependencies(cubeshot_audio_network_tests cubeshot_assets)
    add_test(NAME audio_network COMMAND cubeshot_audio_network_tests)
    set_tests_properties(audio_network PROPERTIES TIMEOUT 30)
endif()

add_executable(cubeshot_four_player_tests four_player_tests.cpp)
target_link_libraries(cubeshot_four_player_tests PRIVATE cubeshot_network)
add_test(NAME four_player COMMAND cubeshot_four_player_tests)
add_executable(cubeshot_four_network_tests four_network_tests.cpp)
target_link_libraries(cubeshot_four_network_tests PRIVATE cubeshot_network)
add_test(NAME four_network COMMAND cubeshot_four_network_tests)
set_tests_properties(four_network PROPERTIES TIMEOUT 60)
