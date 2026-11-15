# Optional: JUCE is only downloaded when STAGE_BUILD_PLUGIN=ON. No VST SDK needed
# for VST3 with current JUCE; do not attempt to generate obsolete VST2 formats.
if(STAGE_BUILD_PLUGIN)
  if(CMAKE_VERSION VERSION_LESS "3.22")
    message(FATAL_ERROR "VST3/AU targets require CMake >= 3.22")
  endif()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8)
  FetchContent_MakeAvailable(juce)
  set(stage_formats VST3)
  if(APPLE)
    list(APPEND stage_formats AU)
  endif()
  juce_add_plugin(LiveStagePlugin
    COMPANY_NAME "Live Stage"
    BUNDLE_ID "com.livestage.instrument"
    PLUGIN_MANUFACTURER_CODE LStg
    PLUGIN_CODE LS16
    PRODUCT_NAME "Live Stage"
    FORMATS ${stage_formats}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(LiveStagePlugin PRIVATE
    src/plugin/StagePluginProcessor.cpp
    src/core/WorkstationHost.cpp
    src/control/ControlMappings.cpp
    src/control/ParamValueReader.cpp
    src/loaders/BankLoader.cpp
    src/loaders/NsmpLoader.cpp
    src/loaders/Sf2Loader.cpp
    src/modules/NordPianoEngine.cpp
    src/modules/NordSynthModule.cpp
    src/modules/NordOrganModule.cpp)
  target_include_directories(LiveStagePlugin PRIVATE src src/core src/loaders src/modules src/dsp third_party)
  target_compile_definitions(LiveStagePlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(LiveStagePlugin PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
    nlohmann_json::nlohmann_json)
  juce_generate_juce_header(LiveStagePlugin)
endif()
