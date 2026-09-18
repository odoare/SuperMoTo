cmake_minimum_required(VERSION 3.22)

# MUST stay before project(): that is where CMake configures the Apple toolchain
# and reads these. project() also creates the CMAKE_OSX_DEPLOYMENT_TARGET cache
# entry itself, so a set(... CACHE ...) placed after it is silently a no-op and
# the deployment target falls back to the build machine's SDK. The failure is
# invisible on an Apple Silicon runner: the bundle is arm64-only, is still named
# "universal", installs happily, and then fails on every Intel Mac.
#
# 10.13, not 11.0: 11.0 excludes Catalina even with a correct x86_64 slice (the
# plugin is skipped during the host's scan and nothing appears). The arm64 slice
# is clamped up to 11.0 by the toolchain anyway, so a low target costs nothing.
# The release workflow passes both as -D on the configure line as well, since a
# command-line -D is in the cache before any line here runs.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(SuperMoTo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()        # register the offline test executables with CTest

# JUCE is expected as a sibling of this repo (../JUCE, a juce-framework/JUCE
# checkout). Shared FX-Mechanics code — GUI controls, look-and-feel, VU meters
# and DSP (Biquad) plus the WDL-backed FirFilter — now comes from the FxmeTools
# submodule under lib/. The helper registers the `FxmeTools` JUCE module and
# defines fxmetools_attach(<target>).
add_subdirectory(../JUCE JUCE)
include(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/cmake/FxmeTools.cmake)

juce_add_plugin(SuperMoTo
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 SMTO
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "SuperMoTo"
    DESCRIPTION                 "FX-Mechanics 32x32 monitoring matrix with speaker calibration"
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

# ── FxmeTools: shared JUCE module + WDL convolution engine (per-output FIR) ───
# Links the FxmeTools module (auto-included into JuceHeader.h, exposing the
# fxme:: GUI controls and Biquad) and compiles the WDL sources used by
# <FxmeTools/dsp/FirFilter.h>. Also defines NOMINMAX on MSVC.
fxmetools_attach(SuperMoTo)

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
        # NOMINMAX (needed by WDL on MSVC) is added by fxmetools_attach().
)

# ── Offline test of the analysis part (build & run manually) ─────────────────
juce_add_console_app(SuperMoToTests PRODUCT_NAME "SuperMoToTests")
target_sources(SuperMoToTests PRIVATE
    Tests/AnalysisTest.cpp
    Source/Dsp/AnalysisEngine.cpp
    # MicCalibration lives in FxmeTools now; compiled straight from the submodule
    # rather than linking the whole module (this target has no GUI dependencies).
    lib/FxmeTools/FxmeTools/dsp/MicCalibration.cpp
)
juce_generate_juce_header(SuperMoToTests)
# Reach <FxmeTools/dsp/MicCalibration.h> (compiled above) without pulling in
# the whole JUCE module, and link FxmeCore for the JUCE-free half of the
# library — <FxmeTools/dsp/Biquad.h> lives there since the core/shell split.
# FxmeCore exports core/ as a PUBLIC include directory, so the <FxmeTools/...>
# spelling keeps working without this target knowing which side a header is on.
target_include_directories(SuperMoToTests PRIVATE ${FXMETOOLS_ROOT})
target_link_libraries(SuperMoToTests PRIVATE FxmeCore)
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
add_test(NAME analysis COMMAND SuperMoToTests)

# ── Offline test of the microphone-calibration logic ─────────────────────────
juce_add_console_app(SuperMoToMicCalTests PRODUCT_NAME "SuperMoToMicCalTests")
target_sources(SuperMoToMicCalTests PRIVATE
    Tests/MicCalTest.cpp
    lib/FxmeTools/FxmeTools/dsp/MicCalibration.cpp
)
juce_generate_juce_header(SuperMoToMicCalTests)
# Reach <FxmeTools/dsp/MicCalibration.h> without linking the module; FxmeCore
# covers whatever it pulls in from the JUCE-free half of the library.
target_include_directories(SuperMoToMicCalTests PRIVATE ${FXMETOOLS_ROOT})
target_link_libraries(SuperMoToMicCalTests PRIVATE FxmeCore)
target_compile_definitions(SuperMoToMicCalTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    MICCAL_TEST_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/mic_cal_synthetic.txt"
)
target_link_libraries(SuperMoToMicCalTests PRIVATE
    juce::juce_core juce::juce_data_structures juce::juce_events
    juce::juce_recommended_config_flags
)
set_target_properties(SuperMoToMicCalTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME miccal COMMAND SuperMoToMicCalTests)

# ── Offline test of the synchronized swept-sine logic (fxme, Novak 2015) ─────
juce_add_console_app(SuperMoToSweepTests PRODUCT_NAME "SuperMoToSweepTests")
target_sources(SuperMoToSweepTests PRIVATE
    Tests/SweepTest.cpp
)
juce_generate_juce_header(SuperMoToSweepTests)
# <FxmeTools/dsp/SynchronizedSweep.h> is module-side; FxmeCore covers the rest.
target_include_directories(SuperMoToSweepTests PRIVATE ${FXMETOOLS_ROOT})
target_link_libraries(SuperMoToSweepTests PRIVATE FxmeCore)
target_compile_definitions(SuperMoToSweepTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SuperMoToSweepTests PRIVATE
    juce::juce_audio_basics juce::juce_core juce::juce_data_structures
    juce::juce_dsp juce::juce_events
    juce::juce_recommended_config_flags
)
set_target_properties(SuperMoToSweepTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME sweep COMMAND SuperMoToSweepTests)

# ── Offline test of the output polarity (model round trip + MatrixEngine) ─────
juce_add_console_app(SuperMoToOutputTests PRODUCT_NAME "SuperMoToOutputTests")
target_sources(SuperMoToOutputTests PRIVATE
    Tests/OutputPolarityTest.cpp
    Source/Model/ConfigModel.cpp
    Source/Dsp/MatrixEngine.cpp
)
# Unlike the tests above, this one runs the real engine, which needs the whole
# FxmeTools module: fxme::Biquad reaches OutputProcessor through JuceHeader.h,
# and fxme::FirFilter needs the WDL sources. fxmetools_attach() adds both, and
# FxmeCore with them.
fxmetools_attach(SuperMoToOutputTests)
target_compile_definitions(SuperMoToOutputTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SuperMoToOutputTests PRIVATE
    juce::juce_audio_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_audio_utils juce::juce_core juce::juce_data_structures juce::juce_dsp
    juce::juce_events juce::juce_graphics juce::juce_gui_basics
    juce::juce_recommended_config_flags
)
juce_generate_juce_header(SuperMoToOutputTests)
set_target_properties(SuperMoToOutputTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME output COMMAND SuperMoToOutputTests)

# ── Offline test of the per-output fractional time-alignment delay ───────────
juce_add_console_app(SuperMoToDelayTests PRODUCT_NAME "SuperMoToDelayTests")
target_sources(SuperMoToDelayTests PRIVATE
    Tests/FractionalDelayTest.cpp
    Source/Model/ConfigModel.cpp
)
# OutputProcessor reaches fxme::Biquad through JuceHeader.h, so the module has
# to be attached here as it is for the output test.
fxmetools_attach(SuperMoToDelayTests)
target_compile_definitions(SuperMoToDelayTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SuperMoToDelayTests PRIVATE
    juce::juce_audio_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_audio_utils juce::juce_core juce::juce_data_structures juce::juce_dsp
    juce::juce_events juce::juce_graphics juce::juce_gui_basics
    juce::juce_recommended_config_flags
)
juce_generate_juce_header(SuperMoToDelayTests)
set_target_properties(SuperMoToDelayTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME delay COMMAND SuperMoToDelayTests)

# ── Offline test of the measurement-folder reader and run helpers ────────────
# MeasurementFolder needs juce_core only, which is why it lives apart from
# MeasurementEngine.
juce_add_console_app(SuperMoToFolderTests PRODUCT_NAME "SuperMoToFolderTests")
target_sources(SuperMoToFolderTests PRIVATE
    Tests/MeasurementFolderTest.cpp
    Source/Dsp/MeasurementFolder.cpp
)
target_compile_definitions(SuperMoToFolderTests PRIVATE
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(SuperMoToFolderTests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
)
juce_generate_juce_header(SuperMoToFolderTests)
set_target_properties(SuperMoToFolderTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME folder COMMAND SuperMoToFolderTests)

target_link_libraries(SuperMoTo
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
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
