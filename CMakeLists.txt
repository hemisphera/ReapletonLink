cmake_minimum_required(VERSION 3.15)
project(ReapletonLink LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# Ableton Link C++ SDK
# ---------------------------------------------------------------------------
# We download the source tree but only invoke AbletonLinkConfig.cmake to set up
# the Ableton::Link INTERFACE target, avoiding tests/examples/extensions.
FetchContent_Declare(
  ableton_link
  GIT_REPOSITORY https://github.com/Ableton/link.git
  GIT_TAG master
  GIT_SHALLOW TRUE
  GIT_SUBMODULES "modules/asio-standalone"
)
FetchContent_GetProperties(ableton_link)
if(NOT ableton_link_POPULATED)
  FetchContent_Populate(ableton_link)
  include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)
endif()

# ---------------------------------------------------------------------------
# REAPER SDK (header-only — just need reaper_plugin.h)
# ---------------------------------------------------------------------------
FetchContent_Declare(
  reaper_sdk
  GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(reaper_sdk)
if(NOT reaper_sdk_POPULATED)
  FetchContent_Populate(reaper_sdk)
endif()

# On non-Windows, reaper_plugin.h includes ../WDL/swell/swell.h.
if(NOT WIN32)
  FetchContent_Declare(
    wdl
    GIT_REPOSITORY https://github.com/justinfrankel/WDL.git
    GIT_TAG main
    GIT_SHALLOW TRUE
  )
  FetchContent_GetProperties(wdl)
  if(NOT wdl_POPULATED)
    FetchContent_Populate(wdl)
  endif()
  # Place WDL as a sibling of reaper-plugins/ so the relative include resolves.
  file(COPY ${wdl_SOURCE_DIR}/ DESTINATION ${reaper_sdk_SOURCE_DIR}/WDL)
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
add_library(ReapletonLink SHARED
  src/main.cpp
  src/LinkController.cpp
)

target_include_directories(ReapletonLink PRIVATE
  src
  ${reaper_sdk_SOURCE_DIR}/reaper-plugins
)

target_link_libraries(ReapletonLink PRIVATE Ableton::Link)

# REAPER requires the "reaper_" prefix to load extension DLLs.
set_target_properties(ReapletonLink PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_ReapletonLink"
)

if(WIN32)
  target_compile_definitions(ReapletonLink PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  # Winsock and IP helper libs required by the Ableton Link network layer.
  target_link_libraries(ReapletonLink PRIVATE ws2_32 iphlpapi)
elseif(APPLE)
  target_link_libraries(ReapletonLink PRIVATE
    "-framework SystemConfiguration"
    "-framework CoreFoundation"
  )
endif()