cmake_minimum_required(VERSION 3.22)
project(oraclee VERSION 2.0.1)

add_compile_definitions(ORACLEE_VERSION="${PROJECT_VERSION}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(ORACLEE_INSTALL_PLUGINS "Copy the plug-ins to the system plug-in folders after building" ON)

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------- Plugin
juce_add_plugin(Oraclee
    COMPANY_NAME "oraclee"
    PLUGIN_MANUFACTURER_CODE Orcl
    PLUGIN_CODE Orc1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "oraclee"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${ORACLEE_INSTALL_PLUGINS}
    VST3_CATEGORIES Instrument Tools
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Oraclee PRIVATE
    Source/NoteExtractor.cpp
    Source/PianoRoll.cpp
    Source/SoundModel.cpp
    Source/MidiExport.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Oraclee PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Oraclee
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- CLI (same engine, handy for batch / tests)
juce_add_console_app(sample2midi PRODUCT_NAME "oraclee-cli")

target_sources(sample2midi PRIVATE
    Source/Cli.cpp
    Source/NoteExtractor.cpp
    Source/MidiExport.cpp)

target_compile_definitions(sample2midi PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(sample2midi
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# ---------------------------------------------------------------- UI snapshot (dev only)
option(S2M_BUILD_SNAPSHOT "Build the dev tools (UI snapshot, editor tests, host tests)" OFF)
if(S2M_BUILD_SNAPSHOT)
    juce_add_console_app(s2m_snapshot PRODUCT_NAME "s2m_snapshot")
    target_sources(s2m_snapshot PRIVATE Tools/Snapshot.cpp Source/NoteExtractor.cpp Source/MidiExport.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PianoRoll.cpp Source/SoundModel.cpp)
    target_compile_definitions(s2m_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="oraclee" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp)

    juce_add_console_app(s2m_edittest PRODUCT_NAME "s2m_edittest")
    target_sources(s2m_edittest PRIVATE Tools/EditTest.cpp Source/NoteExtractor.cpp Source/MidiExport.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PianoRoll.cpp Source/SoundModel.cpp)
    target_compile_definitions(s2m_edittest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="oraclee" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_edittest PRIVATE juce::juce_audio_utils juce::juce_dsp)

    juce_add_console_app(s2m_hostload PRODUCT_NAME "s2m_hostload")
    target_sources(s2m_hostload PRIVATE Tools/HostLoad.cpp)
    target_compile_definitions(s2m_hostload PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_hostload PRIVATE juce::juce_audio_utils)

    juce_add_console_app(s2m_peek PRODUCT_NAME "s2m_peek")
    target_sources(s2m_peek PRIVATE Tools/PeekPlugin.mm)
    target_compile_definitions(s2m_peek PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_peek PRIVATE juce::juce_audio_utils "-framework Cocoa")

    juce_add_console_app(s2m_synthtest PRODUCT_NAME "s2m_synthtest")
    target_sources(s2m_synthtest PRIVATE Tools/SynthTest.cpp Source/NoteExtractor.cpp Source/MidiExport.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PianoRoll.cpp Source/SoundModel.cpp)
    target_compile_definitions(s2m_synthtest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="oraclee" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_synthtest PRIVATE juce::juce_audio_utils juce::juce_dsp)

    juce_add_console_app(s2m_hostsync PRODUCT_NAME "s2m_hostsync")
    target_sources(s2m_hostsync PRIVATE Tools/HostSyncTest.cpp Source/NoteExtractor.cpp Source/MidiExport.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PianoRoll.cpp Source/SoundModel.cpp)
    target_compile_definitions(s2m_hostsync PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="oraclee" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(s2m_hostsync PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
