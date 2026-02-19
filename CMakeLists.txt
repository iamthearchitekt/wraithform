cmake_minimum_required(VERSION 3.15)

project(WraithForm VERSION 1.0.0)

# JUCE Setup using FetchContent
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(juce)

# Plugin Format Setup
juce_add_plugin(WraithForm
    COMPANY_NAME "Antigravity"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE WrFm
    FORMATS VST3 Standalone
    PRODUCT_NAME "WraithForm"
    ICON_BIG "Source/icon.png"
)

# Source Files
target_sources(WraithForm PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/RingBuffer.h
    Source/VisualizerShader.h
    Source/SpectrogramShader.h
    Source/CircularOscilloscopeShader.h
)

# Compile Definitions & Linker Flags
target_compile_definitions(WraithForm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Link against JUCE modules
target_link_libraries(WraithForm PRIVATE
    juce::juce_audio_utils
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_dsp
)
