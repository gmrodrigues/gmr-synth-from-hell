cmake_minimum_required(VERSION 3.22)

project(GmrSynthQuickTest VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(GmrSynthQuickTest
    PRODUCT_NAME "GMR Synth Quick Test"
    COMPANY_NAME "GMR"
)

set_target_properties(GmrSynthQuickTest PROPERTIES
    OUTPUT_NAME "gmr-synth-quick-test"
)

target_sources(GmrSynthQuickTest PRIVATE
    Source/Main.cpp
)

target_compile_definitions(GmrSynthQuickTest PRIVATE
    JUCE_ALSA=1
    JUCE_JACK=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(GmrSynthQuickTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
