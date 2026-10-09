cmake_minimum_required(VERSION 3.20)
project(Synaxis VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(GNUInstallDirs)

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
    FetchContent_Declare(httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.26.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(httplib)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# FFmpeg reads durations and renders thumbnails. Packages bundle a minimal
# static build, because every Ubuntu release ships a different FFmpeg
# soname (libavcodec60, 61, 62...) and a .deb linked against one won't
# install on the others. Development builds use the system copy.
option(SYNAXIS_BUNDLED_FFMPEG "Build a minimal static FFmpeg into Synaxis (for portable packages)" OFF)
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
    ExternalProject_Add(ffmpeg_static
        URL https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=${ffmpeg_prefix}
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
    # Listed after the archives so it resolves their (wrapped) atan2f calls.
    add_library(synaxis_glibc_compat STATIC src/glibc_compat.cpp)
    target_include_directories(synaxis_ffmpeg INTERFACE ${ffmpeg_prefix}/include)
    target_link_libraries(synaxis_ffmpeg INTERFACE
        ${ffmpeg_archives} synaxis_glibc_compat m Threads::Threads)
    target_link_options(synaxis_ffmpeg INTERFACE -Wl,--wrap=atan2f)
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
    src/library_service.cpp
    src/media_library.cpp
    src/media_probe.cpp
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

add_executable(synaxis_cli src/main.cpp)
target_link_libraries(synaxis_cli PRIVATE synaxis_core)
target_compile_definitions(synaxis_cli PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

# The web UI is compiled into the binary so the app runs from anywhere.
set(SYNAXIS_WEB_FILES index.html app.css app.js icon.svg)
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

add_executable(synaxis src/app_main.cpp src/server.cpp ${SYNAXIS_WEB_ASSETS_CPP})
target_include_directories(synaxis PRIVATE src)
target_link_libraries(synaxis PRIVATE synaxis_core httplib::httplib)
target_compile_definitions(synaxis PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

# Its own desktop window via WebKitGTK; without it, the UI opens in the browser.
option(SYNAXIS_WINDOW "Show the UI in a native window (needs libwebkit2gtk-4.1-dev)" ON)
if(SYNAXIS_WINDOW)
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

option(SYNAXIS_BUILD_TESTS "Build the unit tests" ON)
if(SYNAXIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Debian package: cmake --build build --target package ----
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME synaxis)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
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
