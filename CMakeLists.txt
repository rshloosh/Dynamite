cmake_minimum_required(VERSION 3.22)
project(Dynamite VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE as a subdirectory
add_subdirectory(/Applications/JUCE JUCE)

# Define the plugin target
juce_add_plugin(Dynamite
    COMPANY_NAME "Savta"
    PLUGIN_MANUFACTURER_CODE Savt
    PLUGIN_CODE Dyna
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Dynamite"
    PLUGIN_NAME "Dynamite"
    PLUGIN_DESCRIPTION "Vintage Style Compressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(Dynamite)

target_sources(Dynamite
    PRIVATE
        NewProject/Source/PluginProcessor.cpp
        NewProject/Source/PluginEditor.cpp
        NewProject/Source/DynamiteEngine.cpp
        NewProject/Source/DynamiteComponent.cpp
)

target_include_directories(Dynamite
    PRIVATE
        NewProject/Source
)

target_compile_definitions(Dynamite
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(Dynamite
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Unit-test console app: builds the DSP engine against JUCE and asserts the
# Dyna-Mite transfer curves. Build/run:  cmake --build build --target Dynamite_Tests
#==============================================================================
juce_add_console_app(Dynamite_Tests PRODUCT_NAME "Dynamite_Tests")
juce_generate_juce_header(Dynamite_Tests)

target_sources(Dynamite_Tests
    PRIVATE
        NewProject/Source/DynamiteEngine.cpp
        NewProject/Tests/dynamite_test.cpp
)

target_include_directories(Dynamite_Tests
    PRIVATE
        NewProject/Source
)

target_compile_definitions(Dynamite_Tests
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(Dynamite_Tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
