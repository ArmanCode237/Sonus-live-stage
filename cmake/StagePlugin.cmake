# Optional: JUCE is only downloaded when STAGE_BUILD_PLUGIN=ON. No VST SDK needed
# for VST3 with current JUCE; do not attempt to generate obsolete VST2 formats.
#
# v33: the plug-in hosts the same core as the app (StageApp + HTTP / WebSocket API) and
# shows the same console in a WebView (WebView2 on Windows, WKWebView on macOS). Only the
# app's device layer (RtAudio / RtMidi, native window, main) stays out of the plug-in.
if(STAGE_BUILD_PLUGIN)
  if(CMAKE_VERSION VERSION_LESS "3.22")
    message(FATAL_ERROR "VST3/AU targets require CMake >= 3.22")
  endif()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
  set(stage_formats VST3)
  if(APPLE)
    list(APPEND stage_formats AU)
  endif()
  juce_add_plugin(LiveStagePlugin
    COMPANY_NAME "Sonus"
    BUNDLE_ID "com.livestage.instrument"
    PLUGIN_MANUFACTURER_CODE LStg
    PLUGIN_CODE LS16
    PRODUCT_NAME "Sonus Live Stage"
    FORMATS ${stage_formats}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE never change: DAW projects identify the plug-in by them.
    VST3_CATEGORIES Instrument Synth)
  target_sources(LiveStagePlugin PRIVATE
    src/plugin/StagePluginProcessor.cpp
    # ---- app core (shared with SonusLiveStage.exe) ----
    src/app/StageApp.cpp
    src/app/AudioBackend.cpp
    src/app/MidiInput.cpp
    src/app/NativeWindow.cpp            # stub here (no STAGE_NATIVE_WINDOW): the editor is the window
    src/app/UserDataPaths.cpp
    src/app/ParamSerialization.cpp
    src/app/SampleSources.cpp
    src/app/StateHub.cpp
    src/app/D50Import.cpp
    src/app/routes/PageRoutes.cpp
    src/app/routes/LibraryRoutes.cpp
    src/app/routes/ProgramRoutes.cpp
    src/app/routes/HardwareRoutes.cpp
    src/app/routes/PianoRoutes.cpp
    src/app/routes/SoundRoutes.cpp
    src/app/routes/SynthRoutes.cpp
    src/app/routes/Dx7Routes.cpp
    src/app/routes/EngineRoutes.cpp
    src/app/routes/OrganRoutes.cpp
    src/app/routes/RealtimeRoutes.cpp
    src/app/routes/MappingRoutes.cpp
    src/app/routes/BrowserRoutes.cpp
    src/app/routes/MidiRoutingRoutes.cpp
    src/app/routes/AboutRoutes.cpp
    src/control/MidiOscMapper.cpp
    src/loaders/InstrumentPool.cpp
    src/loaders/DecodedSampleCache.cpp
    src/loaders/SampleLibraryManager.cpp
    # ---- engine ----
    src/core/WorkstationHost.cpp
    src/control/ControlMappings.cpp
    src/control/ParamValueReader.cpp
    src/loaders/BankLoader.cpp
    src/loaders/NsmpLoader.cpp
    src/loaders/Sf2Loader.cpp
    src/modules/StagePianoEngine.cpp
    src/modules/StageSynthModule.cpp
    src/modules/StageOrganModule.cpp)
  target_include_directories(LiveStagePlugin PRIVATE src src/core src/loaders src/modules src/dsp src/control third_party)
  target_compile_definitions(LiveStagePlugin PRIVATE
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    STAGE_PLUGIN=1
    STAGE_VERSION="${PROJECT_VERSION}"
    # Local builds find ui/ and presets/ in the source tree when the bundle has no Resources.
    STAGE_DEV_ASSET_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(LiveStagePlugin PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
    nlohmann_json::nlohmann_json)
  if(WIN32)
    # WebView2 linked statically: a plug-in cannot rely on WebView2Loader.dll next to the DAW.
    # The loader comes from the Microsoft.Web.WebView2 NuGet package (build-vst3.ps1 fetches it).
    set(stage_webview2 "${CMAKE_SOURCE_DIR}/third_party/webview2/build/native")
    if(NOT EXISTS "${stage_webview2}/x64/WebView2LoaderStatic.lib")
      message(FATAL_ERROR "Falta ${stage_webview2}/x64/WebView2LoaderStatic.lib (ejecuta .\\build-vst3.cmd, que lo descarga).")
    endif()
    target_include_directories(LiveStagePlugin PRIVATE "${stage_webview2}/include")
    target_compile_definitions(LiveStagePlugin PRIVATE
      JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 _WIN32_WINNT=0x0A00 NOMINMAX)
    target_link_libraries(LiveStagePlugin PRIVATE "${stage_webview2}/x64/WebView2LoaderStatic.lib" ws2_32 shell32)
  endif()
  juce_generate_juce_header(LiveStagePlugin)

  # Test host (no DAW needed): loads the built .vst3 like a DAW and checks it.
  option(STAGE_BUILD_PLUGIN_CHECK "Build the plug-in host check tool" OFF)
  if(STAGE_BUILD_PLUGIN_CHECK)
    juce_add_console_app(LiveStagePluginHostCheck PRODUCT_NAME "LiveStagePluginHostCheck")
    target_sources(LiveStagePluginHostCheck PRIVATE tools/plugin-host-check/Main.cpp)
    target_compile_definitions(LiveStagePluginHostCheck PRIVATE
      JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(LiveStagePluginHostCheck PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
  endif()
endif()
