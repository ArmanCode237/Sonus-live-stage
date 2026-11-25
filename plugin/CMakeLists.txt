# Optional, native VST3 (Windows/macOS) and AU (macOS) plug-in. This target uses
# the instrument DSP directly; it does NOT embed an HTTP server or open ASIO.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10)
FetchContent_MakeAvailable(juce)
set(STAGE_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND STAGE_PLUGIN_FORMATS AU)
endif()
juce_add_plugin(LiveStageInstrument
    COMPANY_NAME "Live Stage"
    BUNDLE_ID "com.livestage.instrument"
    PLUGIN_MANUFACTURER_CODE LvSt
    PLUGIN_CODE Ls16
    FORMATS ${STAGE_PLUGIN_FORMATS}
    PRODUCT_NAME "Live Stage Instrument"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
)
target_sources(LiveStageInstrument PRIVATE
    PluginProcessor.cpp
    ../src/core/WorkstationHost.cpp
    ../src/control/ControlMappings.cpp
    ../src/control/ParamValueReader.cpp
    ../src/loaders/BankLoader.cpp
    ../src/loaders/Sf2Loader.cpp
    ../src/loaders/NsmpLoader.cpp
    ../src/modules/StagePianoEngine.cpp
    ../src/modules/StageSynthModule.cpp
    ../src/modules/StageOrganModule.cpp
)
target_include_directories(LiveStageInstrument PRIVATE ../src ../src/core ../src/modules ../src/loaders ../src/dsp ../third_party)
target_compile_definitions(LiveStageInstrument PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(LiveStageInstrument PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    nlohmann_json::nlohmann_json
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
