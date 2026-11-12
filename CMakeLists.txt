cmake_minimum_required(VERSION 3.22)

# Runtime C++ lie statiquement : l'utilisateur n'a rien a installer (pas de "VC++ Redistributable")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(VoiceCleaner VERSION 0.2.0 LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)

# RNNoise (reseau de neurones de debruitage, licence BSD-3).
# La version 0.1.1 embarque directement le modele dans le code source.
FetchContent_Declare(rnnoise
    GIT_REPOSITORY https://github.com/xiph/rnnoise.git
    GIT_TAG v0.1.1
    GIT_SHALLOW ON)

FetchContent_MakeAvailable(JUCE rnnoise)

# --- RNNoise : bibliotheque C compilee a part (pas de CMakeLists dans le depot d'origine)
file(GLOB RNNOISE_SOURCES CONFIGURE_DEPENDS "${rnnoise_SOURCE_DIR}/src/*.c")
add_library(rnnoise_lib STATIC ${RNNOISE_SOURCES})
target_include_directories(rnnoise_lib
    PUBLIC  "${rnnoise_SOURCE_DIR}/include"
    PRIVATE "${rnnoise_SOURCE_DIR}/src")
target_compile_definitions(rnnoise_lib PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
set_target_properties(rnnoise_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Le plugin
set(PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(VoiceCleaner
    COMPANY_NAME "MonStudio"
    PLUGIN_MANUFACTURER_CODE Mstd
    PLUGIN_CODE Vcln
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Voice Cleaner"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VoiceCleaner PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/FeedbackDetector.h
    Source/Denoiser.h)

target_compile_features(VoiceCleaner PRIVATE cxx_std_17)

target_compile_definitions(VoiceCleaner PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VoiceCleaner
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        rnnoise_lib
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
