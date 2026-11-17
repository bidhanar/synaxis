find_package(GTest QUIET)
if(NOT GTest_FOUND)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

function(synaxis_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            synaxis_core
            GTest::gtest
            GTest::gtest_main
            nlohmann_json::nlohmann_json
    )
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    if(CMAKE_CROSSCOMPILING)
        # Windows test programs can only be listed where they can run (under
        # Wine, which also takes a moment to start): at ctest time, not while
        # building, so the build works without Wine.
        gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    else()
        gtest_discover_tests(${name})
    endif()
endfunction()

synaxis_test(filename_parser_test)
synaxis_test(media_library_test)
synaxis_test(catalog_test)
synaxis_test(feed_test)
synaxis_test(watch_state_test)
synaxis_test(vlc_player_test)
synaxis_test(media_probe_test)
synaxis_test(fingerprint_test)
synaxis_test(platform_test)

# The sync logic in web/ is plain JavaScript shared with Tandem; its tests
# run on Node's built-in test runner when Node is installed.
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME web_sync_core_test
        COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_CURRENT_SOURCE_DIR}/web/sync-core.test.mjs)
endif()
