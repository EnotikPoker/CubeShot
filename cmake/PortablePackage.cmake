# Build-tree metadata for tools/package_game.py. Never shipped to players.
if(CUBESHOT_BUILD_CLIENT)
    set(portable_msvc "")
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        set(portable_msvc "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    endif()
    set(portable_dlls "")
    if(WIN32)
        set(portable_dlls "$<TARGET_RUNTIME_DLLS:cubeshot_client>;$<TARGET_RUNTIME_DLLS:cubeshot_server>")
    endif()
    # CMake paths use forward slashes; list values stay semicolon-separated.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/portable-$<CONFIG>.json" CONTENT
"{\n  \"version\": \"${PROJECT_VERSION}\",\n  \"client\": \"$<TARGET_FILE:cubeshot_client>\",\n  \"server\": \"$<TARGET_FILE:cubeshot_server>\",\n  \"dlls\": \"${portable_dlls}\",\n  \"msvc_runtime\": \"${portable_msvc}\",\n  \"vcpkg_installed\": \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}\",\n  \"gns_source\": \"${gamenetworkingsockets_SOURCE_DIR}\",\n  \"linker\": \"${CMAKE_LINKER}\"\n}\n")
endif()
