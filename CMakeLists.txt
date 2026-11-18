cmake_minimum_required(VERSION 3.22)
project(VocalTuneStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VTS_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE from GitHub)" ON)
option(VTS_BUILD_TESTS  "Build DSP tests and the headless CLI (no JUCE needed)" ON)

# ---------- JUCE非依存のDSPコア(ヘッダのみ) ----------
add_library(vts_core INTERFACE)
target_include_directories(vts_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(vts_core INTERFACE _USE_MATH_DEFINES NOMINMAX)
if(MSVC)
    target_compile_options(vts_core INTERFACE /utf-8 /permissive- /bigobj /O2)
else()
    target_compile_options(vts_core INTERFACE -O2)
endif()

# ---------- テストとCLI ----------
if(VTS_BUILD_TESTS)
    enable_testing()
    add_executable(vts_cli Tools/vts_cli.cpp)
    target_link_libraries(vts_cli PRIVATE vts_core)
    foreach(t PitchDetector NoteSegmenter PitchShifter PitchEditor VibratoTiming Session Util)
        add_executable(${t}Test Tests/${t}Test.cpp)
        target_link_libraries(${t}Test PRIVATE vts_core)
        add_test(NAME ${t} COMMAND ${t}Test)
    endforeach()
endif()

# ---------- VST3プラグイン ----------
if(VTS_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(VocalTuneStudio
        COMPANY_NAME                "VocalTuneStudio"
        PRODUCT_NAME                "VOCAL TUNE STUDIO"
        FORMATS                     VST3
        VST3_CATEGORIES             Fx "Pitch Shift"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        PLUGIN_MANUFACTURER_CODE    Vtsd
        PLUGIN_CODE                 Vts1
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(VocalTuneStudio PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/PitchEditorView.cpp
        Source/AudioFileManager.cpp
        Source/ProjectManager.cpp)

    target_compile_definitions(VocalTuneStudio PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        _USE_MATH_DEFINES NOMINMAX)

    target_link_libraries(VocalTuneStudio
        PRIVATE
            vts_core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endif()
