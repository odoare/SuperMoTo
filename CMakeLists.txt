cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(SuperMoTo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

# JUCE and the FxmeJuceTools user-module are expected as siblings of this repo,
# mirroring the MechanOdd layout:
#   ../JUCE/                              (juce-framework/JUCE checkout)
#   ../JUCE/usermodules/FxmeJuceTools/    (symlink to FxmeJuceTools/module/FxmeJuceTools)
add_subdirectory(../JUCE JUCE)
juce_add_module(../JUCE/usermodules/FxmeJuceTools)

juce_add_plugin(SuperMoTo
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 SMTO
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "SuperMoTo"
    DESCRIPTION                 "FX-Mechanics 16x16 monitoring matrix with speaker calibration"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
    BUNDLE_ID                   "com.FXMechanics.SuperMoTo"
)

# Source files are discovered recursively so adding a new module/component
# does not require editing this file. Re-run CMake configure after adding files.
file(GLOB_RECURSE SUPERMOTO_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(SuperMoTo PRIVATE ${SUPERMOTO_SOURCES})

# Make every subdirectory under Source/ an include root so headers can be
# included by name regardless of where a module lives.
file(GLOB SUPERMOTO_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)
list(FILTER SUPERMOTO_INCLUDE_DIRS EXCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hpp|hxx|mm)$")
target_include_directories(SuperMoTo PRIVATE ${SUPERMOTO_INCLUDE_DIRS})

# ── WDL convolution engine (used by the per-output FIR correction filters) ────
set(_wdldir ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/WDL/WDL)
target_sources(SuperMoTo PRIVATE
    ${_wdldir}/convoengine.cpp
    ${_wdldir}/fft.c
    ${_wdldir}/resample.cpp
)
target_include_directories(SuperMoTo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/WDL)

# Image assets
file(GLOB SUPERMOTO_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
                           ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg
                           ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.xml)
if(SUPERMOTO_ASSETS)
    juce_add_binary_data(SuperMoToBinaryData SOURCES ${SUPERMOTO_ASSETS})
    set_target_properties(SuperMoToBinaryData PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(SuperMoTo PRIVATE SuperMoToBinaryData)
endif()

juce_generate_juce_header(SuperMoTo)

target_compile_definitions(SuperMoTo
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        # WDL's convoengine.h pulls in <windows.h>; without NOMINMAX its min/max
        # macros clobber std::min/std::max in headers like FirFilter.h (MSVC C2589).
        $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>
)

# ── Offline test of the analysis part (build & run manually) ─────────────────
juce_add_console_app(SuperMoToTests PRODUCT_NAME "SuperMoToTests")
target_sources(SuperMoToTests PRIVATE
    Tests/AnalysisTest.cpp
    Source/Dsp/AnalysisEngine.cpp
)
juce_generate_juce_header(SuperMoToTests)
target_compile_definitions(SuperMoToTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SuperMoToTests PRIVATE
    juce::juce_audio_basics juce::juce_audio_formats juce::juce_core
    juce::juce_data_structures juce::juce_dsp juce::juce_events
    juce::juce_recommended_config_flags
)
set_target_properties(SuperMoToTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_link_libraries(SuperMoTo
    PRIVATE
        FxmeJuceTools
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
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
