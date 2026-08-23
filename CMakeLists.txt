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
  src/playback/PlaybackStatistics.cpp
  src/rendering/ShaderManager.cpp
  src/network/ResolverPipeline.cpp
  src/network/YtDlpBridge.cpp
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
  enable_language(RC)
  target_sources(wannaviewer_core PRIVATE
    src/platform/windows/HttpClientWin.cpp
    src/platform/windows/ProcessWin.cpp
  )
  target_link_libraries(wannaviewer_core PUBLIC winhttp pathcch psapi)

  add_executable(WannaViewer WIN32
    src/platform/windows/Main.cpp
    src/platform/windows/PlayerWindow.cpp
    resources/windows/WannaViewer.rc
    resources/windows/app.manifest
  )
  set_target_properties(WannaViewer PROPERTIES OUTPUT_NAME "player")
  target_link_libraries(WannaViewer PRIVATE wannaviewer_core comctl32 comdlg32 ole32 shell32 shlwapi dwmapi dxgi)
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
  install(FILES "${WANNAVIEWER_MPV_DLL}" DESTINATION .)
endif()
install(DIRECTORY config shaders presets resolvers DESTINATION .)
