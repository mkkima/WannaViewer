cmake_minimum_required(VERSION 3.28)

project(WannaViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WANNAVIEWER_BUILD_TESTS "Build unit tests" ON)
option(WANNAVIEWER_ENABLE_LTO "Enable release link-time optimization" ON)
set(WANNAVIEWER_MPV_ROOT "${CMAKE_SOURCE_DIR}/.deps/mpv" CACHE PATH "Extracted libmpv SDK")

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

find_path(MPV_INCLUDE_DIR mpv/client.h
  HINTS "${WANNAVIEWER_MPV_ROOT}/include"
  REQUIRED
)

set(WANNAVIEWER_COMMON_SOURCES
  src/core/AppPaths.cpp
  src/core/Config.cpp
  src/core/Logger.cpp
  src/core/Redaction.cpp
  src/core/Url.cpp
  src/playback/MpvApi.cpp
  src/playback/MpvEngine.cpp
  src/playback/PlaybackStateStore.cpp
  src/playback/RecentMediaStore.cpp
  src/playback/PlaybackStatistics.cpp
  src/rendering/ShaderManager.cpp
  src/ui/ControlLayout.cpp
  src/network/ResolverPipeline.cpp
  src/network/YtDlpBridge.cpp
  src/resolvers/AnimeGoResolver.cpp
  src/resolvers/AniBoomResolver.cpp
  src/resolvers/CdnVideoHubResolver.cpp
  src/resolvers/DirectMediaResolver.cpp
  src/resolvers/GenericResolver.cpp
  src/resolvers/YummyAnimeResolver.cpp
)

add_library(wannaviewer_core STATIC ${WANNAVIEWER_COMMON_SOURCES})
target_include_directories(wannaviewer_core PUBLIC include "${MPV_INCLUDE_DIR}")
target_link_libraries(wannaviewer_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(wannaviewer_core PRIVATE WANNAVIEWER_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(wannaviewer_core PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /sdl /guard:cf)
  target_compile_definitions(wannaviewer_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
else()
  target_compile_options(wannaviewer_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
  find_package(Qt6 6.8.3 REQUIRED COMPONENTS Widgets OpenGLWidgets)
  FetchContent_Declare(
    webview2_sdk
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4129.50/microsoft.web.webview2.1.0.4129.50.nupkg
    URL_HASH SHA256=D3934F482D484B89FB4825DF720C710664E1143A1E90F7B3A60794EF33F473D2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(webview2_sdk)

  set(WANNAVIEWER_WEBVIEW2_INCLUDE_DIR "${webview2_sdk_SOURCE_DIR}/build/native/include")
  set(WANNAVIEWER_WEBVIEW2_IMPORT_LIBRARY "${webview2_sdk_SOURCE_DIR}/build/native/x64/WebView2Loader.dll.lib")
  set(WANNAVIEWER_WEBVIEW2_LOADER "${webview2_sdk_SOURCE_DIR}/runtimes/win-x64/native/WebView2Loader.dll")
  foreach(webview2_file IN ITEMS
      "${WANNAVIEWER_WEBVIEW2_INCLUDE_DIR}/WebView2.h"
      "${WANNAVIEWER_WEBVIEW2_IMPORT_LIBRARY}"
      "${WANNAVIEWER_WEBVIEW2_LOADER}")
    if(NOT EXISTS "${webview2_file}")
      message(FATAL_ERROR "Pinned WebView2 SDK file is missing: ${webview2_file}")
    endif()
  endforeach()

  enable_language(RC)
  target_sources(wannaviewer_core PRIVATE
    src/platform/windows/BrowserEmbedResolver.cpp
    src/platform/windows/HttpClientWin.cpp
    src/platform/windows/ProcessWin.cpp
  )
  target_include_directories(wannaviewer_core PRIVATE "${WANNAVIEWER_WEBVIEW2_INCLUDE_DIR}")
  target_link_libraries(wannaviewer_core PUBLIC winhttp pathcch psapi "${WANNAVIEWER_WEBVIEW2_IMPORT_LIBRARY}")

  qt_add_executable(WannaViewer WIN32
    src/platform/windows/Main.cpp
    src/platform/windows/QtPlayerWindow.cpp
    resources/windows/WannaViewer.rc
    resources/windows/app.manifest
  )
  set_target_properties(WannaViewer PROPERTIES OUTPUT_NAME "player")
  target_link_libraries(WannaViewer PRIVATE wannaviewer_core Qt6::Widgets Qt6::OpenGLWidgets ole32 shell32 shlwapi dxgi)
  target_compile_options(WannaViewer PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /sdl /guard:cf)
  target_link_options(WannaViewer PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf)

  set(WANNAVIEWER_MPV_DLL "${WANNAVIEWER_MPV_ROOT}/libmpv-2.dll")
  if(EXISTS "${WANNAVIEWER_MPV_DLL}")
    add_custom_command(TARGET WannaViewer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WANNAVIEWER_MPV_DLL}" "$<TARGET_FILE_DIR:WannaViewer>/libmpv-2.dll"
    )
  else()
    message(FATAL_ERROR "libmpv-2.dll is missing under ${WANNAVIEWER_MPV_ROOT}; run scripts/bootstrap-mpv.ps1")
  endif()
  add_custom_command(TARGET WannaViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WANNAVIEWER_WEBVIEW2_LOADER}" "$<TARGET_FILE_DIR:WannaViewer>/WebView2Loader.dll"
  )
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(wannaviewer_core PRIVATE
    src/platform/macos/HttpClientMac.mm
    src/platform/macos/ProcessMac.cpp
  )
  target_compile_options(wannaviewer_core PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>)
  add_executable(WannaViewer MACOSX_BUNDLE src/platform/macos/Main.mm)
  target_link_libraries(WannaViewer PRIVATE wannaviewer_core "-framework AppKit" "-framework Foundation")
  target_compile_options(WannaViewer PRIVATE -fblocks)
  set_target_properties(WannaViewer PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "app.wannaviewer.player")
else()
  message(FATAL_ERROR "WannaViewer currently supports Windows and macOS only")
endif()

set_target_properties(WannaViewer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(asset config presets resolvers shaders)
  add_custom_command(TARGET WannaViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/${asset}" "$<TARGET_FILE_DIR:WannaViewer>/${asset}"
  )
endforeach()

if(WANNAVIEWER_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set_property(TARGET wannaviewer_core WannaViewer PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "IPO/LTO unavailable: ${ipo_error}")
  endif()
endif()

if(WANNAVIEWER_BUILD_TESTS)
  enable_testing()
  add_executable(wannaviewer_tests
    tests/TestMain.cpp
    tests/ConfigTests.cpp
    tests/ControlLayoutTests.cpp
    tests/PlaybackStateStoreTests.cpp
    tests/RecentMediaStoreTests.cpp
    tests/ResolverTests.cpp
    tests/SecurityTests.cpp
    tests/ShaderManagerTests.cpp
  )
  target_link_libraries(wannaviewer_tests PRIVATE wannaviewer_core)
  target_compile_definitions(wannaviewer_tests PRIVATE WANNAVIEWER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(wannaviewer_tests PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()
  add_test(NAME wannaviewer_unit COMMAND wannaviewer_tests)
endif()

install(TARGETS WannaViewer RUNTIME DESTINATION . BUNDLE DESTINATION .)
if(WIN32)
  install(FILES "${WANNAVIEWER_MPV_DLL}" "${WANNAVIEWER_WEBVIEW2_LOADER}" DESTINATION .)
endif()
install(DIRECTORY config shaders presets resolvers DESTINATION .)
