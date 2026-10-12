add_executable(cubeshot_game_tests game_tests.cpp)
target_link_libraries(cubeshot_game_tests PRIVATE cubeshot_game)
add_test(NAME game COMMAND cubeshot_game_tests)

add_executable(cubeshot_renderer_tests
    renderer_tests.cpp
    ../src/client/SceneRenderer.cpp
)
target_link_libraries(cubeshot_renderer_tests PRIVATE cubeshot_game SDL3::SDL3)
add_test(NAME renderer COMMAND cubeshot_renderer_tests)
