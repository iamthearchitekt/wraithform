cmake_minimum_required(VERSION 3.15)

project(WraithForm VERSION 1.1.0)

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
    COMPANY_NAME "Arcane Synthesis"
    BUNDLE_ID "com.arcanesynthesis.wraithform"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_STEP_AFTER_BUILD TRUE
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
    Source/WraithForm.rc
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
# Standalone App
install(TARGETS WraithForm_Standalone
    DESTINATION .
    COMPONENT Standalone
)

# README — placed next to the exe so users can open it from the Start Menu
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION .
    COMPONENT Standalone
    RENAME "ReadMe.txt"
)

# VST3 bundle (directory) — copied into the install dir for the user to place manually
install(DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/WraithForm_artefacts/Release/VST3/WraithForm.vst3"
    DESTINATION "VST3"
    COMPONENT VST3
)

# 2. CPack Configuration
# ----------------------
set(CPACK_PACKAGE_VENDOR "Arcane Synthesis")
set(CPACK_PACKAGE_CONTACT "support@arcanesynthesis.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WraithForm Audio Visualizer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WraithForm")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# NSIS Specifics
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "WraithForm")
set(CPACK_NSIS_PACKAGE_NAME "WraithForm")
set(CPACK_NSIS_HELP_LINK "https://github.com/iamthearchitekt/wraithform")
set(CPACK_NSIS_BRANDING_TEXT "WraithForm v${PROJECT_VERSION} - Arcane Synthesis")

# Install to Program Files (64-bit) — use variable so $ is not mis-parsed by CMake
set(_NSIS_PF64 "\$PROGRAMFILES64")
set(CPACK_NSIS_INSTALL_ROOT "${_NSIS_PF64}")

# Only package our components (exclude 'Unspecified' which contains all JUCE sources)
set(CPACK_COMPONENTS_ALL "Standalone;VST3")

# Branding Images (Must be BMP)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Source/INSTALLER-HEADER.bmp" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Source/INSTALLER-BANNER.bmp" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/INSTALLER-HEADER.bmp" NATIVE_HEADER_BMP)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/INSTALLER-BANNER.bmp" NATIVE_BANNER_BMP)

string(REPLACE "\\" "\\\\" ESCAPED_HEADER_BMP "${NATIVE_HEADER_BMP}")
string(REPLACE "\\" "\\\\" ESCAPED_BANNER_BMP "${NATIVE_BANNER_BMP}")

set(CPACK_NSIS_MUI_HEADERIMAGE_BITMAP "${ESCAPED_HEADER_BMP}")
set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${ESCAPED_BANNER_BMP}")

# Component Descriptions
set(CPACK_COMPONENT_STANDALONE_DISPLAY_NAME "Standalone Application")
set(CPACK_COMPONENT_STANDALONE_DESCRIPTION "The WraithForm standalone executable and ReadMe.")

set(CPACK_COMPONENT_VST3_DISPLAY_NAME "VST3 Plugin")
set(CPACK_COMPONENT_VST3_DESCRIPTION "The VST3 plugin bundle. Copy to C:\\Program Files\\Common Files\\VST3\\ to use in a DAW.")

# 3. Icon handling (NSIS)
# ----------------------
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Source/app_icon.ico" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/app_icon.ico" NATIVE_ICON_ICO)
string(REPLACE "\\" "\\\\" ESCAPED_ICON_ICO "${NATIVE_ICON_ICO}")

set(CPACK_NSIS_MUI_ICON "${ESCAPED_ICON_ICO}")
set(CPACK_NSIS_MUI_UNIICON "${ESCAPED_ICON_ICO}")
set(CPACK_PACKAGE_EXECUTABLES "WraithForm" "WraithForm")
set(CPACK_CREATE_DESKTOP_LINKS "WraithForm")
set(CPACK_NSIS_INSTALLED_ICON_NAME "WraithForm.exe")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "WraithForm.exe")
set(CPACK_NSIS_MODIFY_PATH OFF)

# 4. Custom Install Steps
# -----------------------
# Shortcuts: Desktop, Start Menu (app + readme + uninstall)
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    CreateShortCut \\\"$DESKTOP\\\\WraithForm.lnk\\\" \\\"$INSTDIR\\\\WraithForm.exe\\\" \\\"\\\" \\\"$INSTDIR\\\\WraithForm.exe\\\" 0
    CreateDirectory \\\"$SMPROGRAMS\\\\WraithForm\\\"
    CreateShortCut \\\"$SMPROGRAMS\\\\WraithForm\\\\WraithForm.lnk\\\" \\\"$INSTDIR\\\\WraithForm.exe\\\" \\\"\\\" \\\"$INSTDIR\\\\WraithForm.exe\\\" 0
    CreateShortCut \\\"$SMPROGRAMS\\\\WraithForm\\\\ReadMe.lnk\\\" \\\"$INSTDIR\\\\ReadMe.txt\\\"
    CreateShortCut \\\"$SMPROGRAMS\\\\WraithForm\\\\Uninstall WraithForm.lnk\\\" \\\"$INSTDIR\\\\Uninstall.exe\\\"
")

# Uninstall: remove shortcuts, Start Menu folder, desktop icon
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    Delete \\\"$DESKTOP\\\\WraithForm.lnk\\\"
    Delete \\\"$SMPROGRAMS\\\\WraithForm\\\\WraithForm.lnk\\\"
    Delete \\\"$SMPROGRAMS\\\\WraithForm\\\\ReadMe.lnk\\\"
    Delete \\\"$SMPROGRAMS\\\\WraithForm\\\\Uninstall WraithForm.lnk\\\"
    RMDir  \\\"$SMPROGRAMS\\\\WraithForm\\\"
")

include(CPack)

