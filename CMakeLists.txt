cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(DJTools VERSION 1.0.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(DJTOOLS_COPY_PLUGIN_AFTER_BUILD "Copy built plugins into local plug-in folders" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.13
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DJTools
    COMPANY_NAME "99Hertz"
    BUNDLE_ID "com.99hertz.djtools"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${DJTOOLS_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Hz99
    PLUGIN_CODE DjTl
    FORMATS VST3 AU Standalone
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Delay
    PRODUCT_NAME "DJ Tools"
)

juce_add_binary_data(DJToolsData SOURCES
    Resources/Fonts/BarlowCondensed-Bold.ttf
    Resources/Fonts/BarlowCondensed-SemiBold.ttf
    Resources/Fonts/BarlowCondensed-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)
set_target_properties(DJToolsData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(DJTOOLS_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine.cpp
    Source/Licence/Licence.cpp
    Source/Licence/tweetnacl.c)

target_sources(DJTools PRIVATE ${DJTOOLS_SOURCES})

target_compile_definitions(DJTools PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_features(DJTools PUBLIC cxx_std_17)

target_link_libraries(DJTools
    PRIVATE DJToolsData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offline test: every effect, grid timing, tails, dead zone, clicks, smoothing, state, licence gate, demos, UI snapshots (not shipped)
option(DJTOOLS_BUILD_TEST "Build the offline test app" OFF)
if(DJTOOLS_BUILD_TEST)
    juce_add_console_app(DJToolsTest PRODUCT_NAME "DJToolsTest")
    target_sources(DJToolsTest PRIVATE Test/Main.cpp ${DJTOOLS_SOURCES})
    target_compile_definitions(DJToolsTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
        DJTOOLS_TEST=1 JucePlugin_VersionString="1.0.0")
    target_compile_features(DJToolsTest PRIVATE cxx_std_17)
    target_link_libraries(DJToolsTest PRIVATE DJToolsData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
