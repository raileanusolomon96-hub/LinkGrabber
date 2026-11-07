cmake_minimum_required(VERSION 3.15)

# Plugin formats: VST3, AU, Standalone
# Native JUCE UI (no WebView): NEEDS_WEB_BROWSER FALSE, utility passthrough (not a synth, no MIDI)
juce_add_plugin(LinkGrabber
    COMPANY_NAME "PluginFreedom"
    PLUGIN_MANUFACTURER_CODE Plgf
    PLUGIN_CODE Lkgb
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "LinkGrabber"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_WEB_BROWSER FALSE
)

# Source files
target_sources(LinkGrabber
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# Include paths
target_include_directories(LinkGrabber
    PRIVATE
        Source
)

# Required JUCE modules
target_link_libraries(LinkGrabber
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Generate JuceHeader.h (JUCE 8 requirement, pattern #1)
juce_generate_juce_header(LinkGrabber)

# Compile definitions (no WebView; native UI)
target_compile_definitions(LinkGrabber
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
