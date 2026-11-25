cmake_minimum_required(VERSION 3.25)

# ---- Config -----------------------------------------------------------------
set(YREV_JUCE_TAG "9.0.5" CACHE STRING "JUCE git tag to fetch")
option(YREV_COPY_AFTER_BUILD "Install built plugins into the user's plugin folders" OFF)
option(YREV_BUILD_TESTS "Build the Catch2 unit tests (tests/); on in CI, run with ctest" OFF)

# Must be set before project() so it reaches the macOS toolchain (a later set() is silently ignored, and the binary then
# needs the build machine's macOS: REV-246 1.0.0 shipped needing macOS 26). The release checks every slice's minimum.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(YREV57 VERSION 1.0.1 LANGUAGES C CXX)

# Windows: link the C/C++ runtime statically (/MT), so the plug-in does not depend on the Visual C++ Redistributable on
# the user's machine; a plug-in built with a newer MSVC than the installed redistributable fails to load, which a host's
# scanner reports as broken (found on the REV-246 1.0.0 release). CMP0091 is NEW from cmake_minimum_required 3.15. Must
# precede JUCE so every target, JUCE's and the tests', uses the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- JUCE -------------------------------------------------------------------
# Fetched at configure time. Set FETCHCONTENT_BASE_DIR to a shared folder to
# avoid re-downloading per build directory.
include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        ${YREV_JUCE_TAG}
  GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

# ---- Plugin -----------------------------------------------------------------
set(YREV_FORMATS AU VST3 Standalone)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND YREV_FORMATS LV2)   # LV2 is built on Linux only
endif()

juce_add_plugin(YREV57
  COMPANY_NAME                "YREV57 Project"
  PLUGIN_MANUFACTURER_CODE    Yrev
  PLUGIN_CODE                 Yr57
  BUNDLE_ID                   org.yrev57.YREV57
  FORMATS                     ${YREV_FORMATS}
  PRODUCT_NAME                "YREV57"
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            FALSE
  NEEDS_MIDI_OUTPUT           FALSE
  AU_MAIN_TYPE                kAudioUnitType_Effect
  VST3_CATEGORIES             Fx Reverb
  LV2URI                      "https://github.com/tehguitarist/YREV57"   # permanent: hosts identify the plug-in by it
  COPY_PLUGIN_AFTER_BUILD     ${YREV_COPY_AFTER_BUILD}
)

# Skin images and fonts (assets/ui, assets/fonts) compiled into the plugin. Fonts are SIL OFL (see THIRD_PARTY_NOTICES.md).
juce_add_binary_data(YREV57Assets SOURCES
  assets/ui/panel-bg.jpg
  assets/ui/knob-cap.png
  assets/ui/key-sq-off.png
  assets/ui/key-sq-off-pushed.png
  assets/ui/key-sq-on.png
  assets/ui/key-sq-on-pushed.png
  assets/ui/key-rect-off.png
  assets/ui/key-rect-off-pushed.png
  assets/ui/key-rect-on.png
  assets/ui/key-rect-on-pushed.png
  assets/fonts/BarlowCondensed-SemiBold.ttf
  assets/fonts/VT323-Regular.ttf
)

set(YREV_EDITOR_SOURCES
  src/plugin/YrevEditor.cpp
  src/plugin/ui/Skin.cpp
  src/plugin/ui/SkinKey.cpp
  src/plugin/ui/SkinKnob.cpp
  src/plugin/ui/Overlays.cpp
)

target_sources(YREV57 PRIVATE
  src/plugin/Parameters.cpp
  src/plugin/PluginProcessor.cpp
  src/plugin/Presets.cpp
  src/dsp/Chain.cpp
  src/dsp/Engine.cpp
  src/dsp/Converter.cpp
  src/dsp/FxSlot.cpp
  ${YREV_EDITOR_SOURCES}
)

target_include_directories(YREV57 PRIVATE src)

target_compile_definitions(YREV57 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(YREV57
  PRIVATE
    YREV57Assets
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---- Tests ------------------------------------------------------------------
# One Catch2 runner, built as a JUCE console app from the plugin sources (it compiles its own copy of the JUCE
# modules, so it roughly doubles a clean build: off by default locally, on in CI). Run with ctest.
if(YREV_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    ON
  )
  FetchContent_MakeAvailable(Catch2)
  enable_testing()

  juce_add_console_app(YREV57Tests PRODUCT_NAME "YREV57Tests")
  target_sources(YREV57Tests PRIVATE
    src/plugin/Parameters.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/Presets.cpp
    src/dsp/Chain.cpp
    src/dsp/Engine.cpp
    src/dsp/Converter.cpp
    src/dsp/FxSlot.cpp
    ${YREV_EDITOR_SOURCES}
    tests/main.cpp
    tests/ParameterTests.cpp
    tests/StateTests.cpp
    tests/DuckerTests.cpp
    tests/GateTests.cpp
    tests/StereoWidthTests.cpp
    tests/EditorTests.cpp
    tests/PresetTests.cpp
    tests/ChainTests.cpp
    tests/EngineTests.cpp
    tests/ConverterTests.cpp
    tests/FxSlotTests.cpp
    tests/BenchTests.cpp
    tests/ListenTests.cpp
  )
  target_include_directories(YREV57Tests PRIVATE src)
  target_compile_definitions(YREV57Tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="YREV57"
    JucePlugin_VersionString="${PROJECT_VERSION}"
  )
  target_link_libraries(YREV57Tests PRIVATE
    YREV57Assets
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2
  )
  add_test(NAME YREV57Tests COMMAND YREV57Tests)
endif()

