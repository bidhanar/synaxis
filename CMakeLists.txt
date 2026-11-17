cmake_minimum_required(VERSION 3.20)
project(Synaxis VERSION 0.3.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)  # icon, version info and manifest
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(GNUInstallDirs)

if(WIN32)
    # Windows 10 and later; NOMINMAX keeps windows.h's min/max macros away from std::min.
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
endif()
if(MINGW)
    # One self-contained .exe: no libstdc++/libgcc/winpthread DLLs to ship.
    add_link_options(-static)
endif()

# Header-only dependencies: use an installed copy when there is one,
# otherwise fetch it, so building doesn't need root to install -dev packages.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(httplib 0.18 QUIET)
if(NOT httplib_FOUND)
    # Plain HTTP on localhost only; none of the optional codecs/TLS needed.
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)  # keep its headers out of our package
    # Only 127.0.0.1 is ever looked up; and MinGW lacks the API its async lookup uses.
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.26.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(httplib)
endif()

find_package(Threads REQUIRED)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()

# FFmpeg reads durations and renders thumbnails. Packages bundle a minimal
# static build, because every Ubuntu release ships a different FFmpeg
# soname (libavcodec60, 61, 62...) and a .deb linked against one won't
# install on the others. Development builds use the system copy.
option(SYNAXIS_BUNDLED_FFMPEG "Build a minimal static FFmpeg into Synaxis (for portable packages)" OFF)
if(WIN32)
    set(SYNAXIS_BUNDLED_FFMPEG ON CACHE BOOL "" FORCE)  # there's no system FFmpeg to use
endif()
add_library(synaxis_ffmpeg INTERFACE)
if(SYNAXIS_BUNDLED_FFMPEG)
    include(ExternalProject)
    include(ProcessorCount)
    ProcessorCount(jobs)
    if(jobs EQUAL 0)
        set(jobs 2)
    endif()
    set(ffmpeg_prefix ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg)
    set(ffmpeg_libs avformat avcodec swscale avutil)  # link order matters
    list(TRANSFORM ffmpeg_libs REPLACE "(.+)" "${ffmpeg_prefix}/lib/lib\\1.a" OUTPUT_VARIABLE ffmpeg_archives)
    set(ffmpeg_cross "")
    if(CMAKE_CROSSCOMPILING AND WIN32)
        set(ffmpeg_cross --enable-cross-compile --target-os=mingw32 --arch=x86_64
            --cross-prefix=x86_64-w64-mingw32-)
    endif()
    ExternalProject_Add(ffmpeg_static
        URL https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=${ffmpeg_prefix}
            ${ffmpeg_cross}
            --enable-static --disable-shared --enable-pic
            --disable-programs --disable-doc --disable-debug
            --disable-autodetect --disable-network
            --disable-avdevice --disable-avfilter --disable-swresample
            --disable-everything
            --enable-protocol=file
            --enable-demuxer=matroska,mov,avi,mpegts,mpegps,flv,asf,ogg
            --enable-decoder=h264,hevc,vp8,vp9,mpeg4,mpeg1video,mpeg2video,h263,flv,msmpeg4v1,msmpeg4v2,msmpeg4v3,wmv1,wmv2,wmv3,vc1,mjpeg,theora,rv30,rv40
            --enable-parser=h264,hevc,vp8,vp9,mpeg4video,mpegvideo,vc1,mjpeg
            --enable-encoder=mjpeg
        BUILD_COMMAND make -j${jobs}
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${ffmpeg_archives})
    file(MAKE_DIRECTORY ${ffmpeg_prefix}/include)
    target_include_directories(synaxis_ffmpeg INTERFACE ${ffmpeg_prefix}/include)
    if(WIN32)
        # bcrypt: libavutil's random numbers.
        target_link_libraries(synaxis_ffmpeg INTERFACE ${ffmpeg_archives} bcrypt Threads::Threads)
    else()
        # Listed after the archives so it resolves their (wrapped) atan2f calls.
        add_library(synaxis_glibc_compat STATIC src/glibc_compat.cpp)
        target_link_libraries(synaxis_ffmpeg INTERFACE
            ${ffmpeg_archives} synaxis_glibc_compat m Threads::Threads)
        target_link_options(synaxis_ffmpeg INTERFACE -Wl,--wrap=atan2f)
    endif()
    add_dependencies(synaxis_ffmpeg ffmpeg_static)
else()
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    target_link_libraries(synaxis_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

add_library(synaxis_core
    src/catalog.cpp
    src/config.cpp
    src/feed.cpp
    src/filename_parser.cpp
    src/fingerprint.cpp
    src/library_service.cpp
    src/media_library.cpp
    src/media_probe.cpp
    src/platform.cpp
    src/playback_manager.cpp
    src/player.cpp
    src/process.cpp
    src/vlc_player.cpp
    src/watch_state.cpp
)
target_include_directories(synaxis_core PUBLIC include)
target_link_libraries(synaxis_core
    PUBLIC nlohmann_json::nlohmann_json Threads::Threads
    PRIVATE synaxis_ffmpeg httplib::httplib)

# Icon, version info and manifest (UTF-8 code page, DPI awareness) for the .exe files.
set(SYNAXIS_WIN_RESOURCES "")
if(WIN32)
    set(SYNAXIS_ICO ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/synaxis.ico)
    set(SYNAXIS_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/synaxis.manifest)
    set(SYNAXIS_WIN_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/generated/synaxis.rc)
    configure_file(packaging/windows/synaxis.rc.in ${SYNAXIS_WIN_RESOURCES} @ONLY)
    target_link_libraries(synaxis_core PUBLIC ws2_32)  # FreeLocalPort
endif()

add_executable(synaxis_cli src/main.cpp ${SYNAXIS_WIN_RESOURCES})
target_link_libraries(synaxis_cli PRIVATE synaxis_core)
target_compile_definitions(synaxis_cli PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

# The web UI is compiled into the binary so the app runs from anywhere.
set(SYNAXIS_WEB_FILES index.html app.css app.js icon.svg
    together.js sync-core.js sync-config.js socket.io.esm.min.js)
list(TRANSFORM SYNAXIS_WEB_FILES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/web/ OUTPUT_VARIABLE SYNAXIS_WEB_PATHS)
string(REPLACE ";" "," SYNAXIS_WEB_LIST "${SYNAXIS_WEB_FILES}")
set(SYNAXIS_WEB_ASSETS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/web_assets_data.cpp)
add_custom_command(
    OUTPUT ${SYNAXIS_WEB_ASSETS_CPP}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${SYNAXIS_WEB_ASSETS_CPP}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/web
        -DFILES=${SYNAXIS_WEB_LIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake
    DEPENDS ${SYNAXIS_WEB_PATHS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake
    COMMENT "Embedding web UI"
    VERBATIM)

add_executable(synaxis src/app_main.cpp src/server.cpp ${SYNAXIS_WEB_ASSETS_CPP} ${SYNAXIS_WIN_RESOURCES})
target_include_directories(synaxis PRIVATE src)
target_link_libraries(synaxis PRIVATE synaxis_core httplib::httplib)
target_compile_definitions(synaxis PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

# Its own desktop window via WebKitGTK (Linux) or Edge WebView2 (Windows);
# without one, the UI opens in the browser.
option(SYNAXIS_WINDOW "Show the UI in a native window (needs libwebkit2gtk-4.1-dev on Linux)" ON)
if(WIN32)
    # A windowed program: no console window when started from Explorer.
    set_target_properties(synaxis PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(synaxis PRIVATE shell32)
endif()
if(SYNAXIS_WINDOW AND WIN32)
    set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_AMALGAMATION OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(webview
        URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(webview)
    if(CMAKE_CROSSCOMPILING)
        target_include_directories(webview_core_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-include)
        target_include_directories(synaxis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-include)
    endif()
    target_sources(synaxis PRIVATE src/app_window_win.cpp)
    target_link_libraries(synaxis PRIVATE webview::core_static)
    target_compile_definitions(synaxis PRIVATE SYNAXIS_HAS_WINDOW)
elseif(SYNAXIS_WINDOW)
    pkg_check_modules(WEBKIT IMPORTED_TARGET webkit2gtk-4.1 gtk+-3.0)
    if(WEBKIT_FOUND)
        target_sources(synaxis PRIVATE src/app_window.cpp)
        target_link_libraries(synaxis PRIVATE PkgConfig::WEBKIT)
        target_compile_definitions(synaxis PRIVATE SYNAXIS_HAS_WINDOW)
    else()
        message(WARNING "webkit2gtk-4.1 not found (sudo apt install libwebkit2gtk-4.1-dev): "
                        "building without the app window; the UI will open in the browser.")
    endif()
endif()

if(WIN32)
    # A portable folder: the programs and a few text files side by side.
    install(TARGETS synaxis synaxis_cli RUNTIME DESTINATION .)
    install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
    install(FILES "packaging/windows/READ ME.txt" DESTINATION .)
else()
install(TARGETS synaxis synaxis_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.synaxis.Synaxis.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES web/icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME synaxis.svg)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES packaging/icons/synaxis-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME synaxis.png)
endforeach()
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/synaxis RENAME copyright)
endif()

option(SYNAXIS_BUILD_TESTS "Build the unit tests" ON)
if(SYNAXIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Packages: cmake --build build --target package ----
# A .deb on Linux; a .zip to unpack anywhere on Windows.
set(CPACK_PACKAGE_NAME synaxis)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME synaxis-${PROJECT_VERSION}-windows-x64)
    set(CPACK_STRIP_FILES ON)
    include(CPack)
    return()
endif()
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_CONTACT "bidhanar <arya.bidhan1729@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Streaming-style home screen for the videos on your disk")
set(CPACK_PACKAGE_DESCRIPTION
"Synaxis finds the movies and shows on your computer, groups episodes into
series, makes artwork from the videos themselves and plays everything in VLC.
It remembers where each profile stopped, keeps a Continue Watching row and a
personal list, and queues the next episode automatically. Everything runs
locally; the interface is served to your browser on 127.0.0.1 only.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION video)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # libav*, libstdc++ etc. from the binaries
set(CPACK_DEBIAN_PACKAGE_DEPENDS "vlc, xdg-utils")
include(CPack)
