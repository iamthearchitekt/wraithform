cmake_minimum_required(VERSION 3.15)

project(WraithForm VERSION 1.0.1)

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
    COPY_PLUGIN_AFTER_BUILD FALSE
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

target_link_libraries(WraithForm PRIVATE
    juce::juce_audio_utils
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_dsp
)

# ==============================================================================
# INSTALLATION & PACKAGING (NSIS)
# ==============================================================================

# 1. Install Rules
# ----------------
# Standalone App -> bin/ (Program Files/WraithForm/bin)
install(TARGETS WraithForm_Standalone
    DESTINATION bin
    COMPONENT Standalone
)

# VST3 Plugin -> Install to application directory to avoid permission issues during build.
# The installer will package this, and the user can move it or we can let NSIS handle it later.
install(TARGETS WraithForm_VST3
    DESTINATION VST3
    COMPONENT VST3
)

# 2. CPack Configuration
# ----------------------
set(CPACK_PACKAGE_VENDOR "Antigravity")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WraithForm Audio Visualizer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WraithForm")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md") # Use README as "License" for now

# NSIS Specifics
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "WraithForm v${PROJECT_VERSION}")
set(CPACK_NSIS_PACKAGE_NAME "WraithForm")
set(CPACK_NSIS_BRANDING_TEXT "WraithForm - High Performance Visualizer")

# Only package our components (Exclude 'Unspecified' which contains all JUCE sources)
set(CPACK_COMPONENTS_ALL "Standalone;VST3")

# Branding Images (Must be BMP)
# Copy them to build dir to ensure NSIS finds them (fixes path issues)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Source/INSTALLER-HEADER.bmp" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Source/INSTALLER-BANNER.bmp" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Convert path to Native Windows (Backslashes)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/INSTALLER-HEADER.bmp" NATIVE_HEADER_BMP)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/INSTALLER-BANNER.bmp" NATIVE_BANNER_BMP)

# Escape backslashes for CPack/NSIS literal string
string(REPLACE "\\" "\\\\" ESCAPED_HEADER_BMP "${NATIVE_HEADER_BMP}")
string(REPLACE "\\" "\\\\" ESCAPED_BANNER_BMP "${NATIVE_BANNER_BMP}")

set(CPACK_NSIS_MUI_HEADERIMAGE_BITMAP "${ESCAPED_HEADER_BMP}")
set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${ESCAPED_BANNER_BMP}")

# Component Description
set(CPACK_COMPONENT_STANDALONE_DISPLAY_NAME "Standalone Application")
set(CPACK_COMPONENT_STANDALONE_DESCRIPTION "The standalone executable version of WraithForm.")

set(CPACK_COMPONENT_VST3_DISPLAY_NAME "VST3 Plugin")
set(CPACK_COMPONENT_VST3_DESCRIPTION "The VST3 plugin for DAWs (install to System VST3 folder manually if needed).")

# 3. Shortcuts (Start Menu & Desktop)
# -----------------------------------
# Syntax: "ExecutableName" "Shortcut Name"
set(CPACK_PACKAGE_EXECUTABLES "WraithForm_Standalone" "WraithForm")

# Create Desktop Shortcut
set(CPACK_CREATE_DESKTOP_LINKS "WraithForm_Standalone")
set(CPACK_NSIS_MODIFY_PATH ON)

# Create Desktop Shortcut
set(CPACK_CREATE_DESKTOP_LINKS "WraithForm_Standalone")
set(CPACK_NSIS_MODIFY_PATH ON)

# 4. Custom Install Steps (VST3 Placement)
# ----------------------------------------
# We need to copy the VST3 bundle to the system VST3 folder
# VST3 is a Directory (Bundle), so we use /r for recursive copy if needed, or ensuring structure.
# But "CopyFiles" in NSIS is simple file copy. "CopyFiles /SILENT"
# Better to use specific NSIS script injection.
# This copies from INSTDIR/VST3/WraithForm.vst3 to $COMMONFILES64/VST3/WraithForm.vst3

set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    SetOutPath \\\"$COMMONFILES64\\\\VST3\\\"
    CopyFiles /SILENT /FILESONLY \\\"$INSTDIR\\\\VST3\\\\WraithForm.vst3\\\" \\\"$COMMONFILES64\\\\VST3\\\\WraithForm.vst3\\\"
")

# Also ensure valid uninstall
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    Delete \\\"$COMMONFILES64\\\\VST3\\\\WraithForm.vst3\\\"
    RMDir \\\"$COMMONFILES64\\\\VST3\\\\WraithForm.vst3\\\"
")
