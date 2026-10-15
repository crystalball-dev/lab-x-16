cmake_minimum_required(VERSION 3.22)

project(LABX16 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugins need no VC++ redistributable on other machines.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LABX16_JUCE_PATH "F:/_DEV/tools/JUCE" CACHE PATH "Path to a JUCE 9 checkout")
if(NOT EXISTS "${LABX16_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${LABX16_JUCE_PATH}. Configure with -DLABX16_JUCE_PATH=<path to JUCE 9>.")
endif()
add_subdirectory("${LABX16_JUCE_PATH}" JUCE)

# Configure a subset for fast iteration: -DLABX16_ONLY="kaymanov;silo"
set(LABX16_ONLY "" CACHE STRING "Semicolon list of plugin slugs to configure (empty: all)")

# Header-only libraries: no ABI, no link order, no JUCE flag mismatch.
add_library(lab_core INTERFACE)
target_include_directories(lab_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(lab_ui INTERFACE)
target_include_directories(lab_ui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

set(LABX16_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# lab_add_effect(NAME Kaymanov SLUG kaymanov PRODUCT "KAYMANOV" CODE L6Ka VERSION 0.1.0
#                CATEGORIES Fx Distortion [MIDI] [DEFINITIONS X=1] SOURCES ...)
# Makes Lx16<Name> (VST3 + Standalone) and Lx16<Name>Render (the offline harness, LABX16_HARNESS=1).
function(lab_add_effect)
    cmake_parse_arguments(FX "MIDI" "NAME;SLUG;PRODUCT;CODE;VERSION" "CATEGORIES;SOURCES;DEFINITIONS" ${ARGN})
    if(LABX16_ONLY AND NOT FX_SLUG IN_LIST LABX16_ONLY)
        return()
    endif()

    set(target Lx16${FX_NAME})
    string(REPLACE "_" "-" bundle_slug "${FX_SLUG}")
    if(FX_MIDI)
        set(midi TRUE)
    else()
        set(midi FALSE)
    endif()

    # Per-target config header: product, slug, version and code for lab::host and the harness.
    set(config_dir "${CMAKE_CURRENT_BINARY_DIR}/config/${FX_SLUG}")
    file(WRITE "${config_dir}/LabX16Config.h.in"
        "#pragma once\n#define LABX16_PRODUCT \"@FX_PRODUCT@\"\n#define LABX16_SLUG \"@FX_SLUG@\"\n#define LABX16_VERSION \"@FX_VERSION@\"\n#define LABX16_CODE \"@FX_CODE@\"\n")
    configure_file("${config_dir}/LabX16Config.h.in" "${config_dir}/LabX16Config.h" @ONLY)

    juce_add_plugin(${target}
        PRODUCT_NAME "LAB X-16 ${FX_PRODUCT}"
        VERSION ${FX_VERSION}
        COMPANY_NAME "OPERATION FAIRWAY, LLC"
        COMPANY_WEBSITE "https://github.com/crystalball-dev/lab-x-16"
        BUNDLE_ID com.operationfairway.labx16.${bundle_slug}
        PLUGIN_MANUFACTURER_CODE Opfw
        PLUGIN_CODE ${FX_CODE}
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT ${midi}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${FX_CATEGORIES})

    set(sources ${FX_SOURCES} "${CMAKE_SOURCE_DIR}/core/host/ZoneBusPlatform.cpp")
    target_sources(${target} PRIVATE ${sources})
    target_include_directories(${target} PRIVATE "${config_dir}")
    target_compile_definitions(${target} PUBLIC ${LABX16_DEFINITIONS} PRIVATE ${FX_DEFINITIONS})
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /FILabX16Config.h>)
    target_link_libraries(${target}
        PRIVATE
            lab_core
            lab_ui
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(${target}Render PRODUCT_NAME "${target}Render")
    target_sources(${target}Render PRIVATE "${CMAKE_SOURCE_DIR}/test/harness_main.cpp" ${sources})
    target_include_directories(${target}Render PRIVATE "${config_dir}")
    # JUCE_MODAL_LOOPS_PERMITTED: --png-wait runs the message loop before the snapshot.
    target_compile_definitions(${target}Render PRIVATE ${LABX16_DEFINITIONS} ${FX_DEFINITIONS} LABX16_HARNESS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_compile_options(${target}Render PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /FILabX16Config.h>)
    target_link_libraries(${target}Render
        PRIVATE
            lab_core
            lab_ui
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

# M0: host checks. Two builds of one stub: 250 ms of latency, and none (handoff section 5).
lab_add_effect(NAME Stub SLUG stub PRODUCT "STUB" CODE L6St VERSION 0.0.1 CATEGORIES Fx MIDI
               DEFINITIONS LABX16_STUB_LATENCY_MS=250
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/_stub/Stub.cpp")
lab_add_effect(NAME StubZero SLUG stub_zero PRODUCT "STUB ZERO" CODE L6Sz VERSION 0.0.1 CATEGORIES Fx MIDI
               DEFINITIONS LABX16_STUB_LATENCY_MS=0
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/_stub/Stub.cpp")

# The cross-DLL Zone Bus check: hosts VST3 effects in one process and reads the shared block.
juce_add_console_app(Lx16BusHost PRODUCT_NAME "Lx16BusHost")
target_sources(Lx16BusHost PRIVATE "${CMAKE_SOURCE_DIR}/test/bus_host.cpp" "${CMAKE_SOURCE_DIR}/core/host/ZoneBusPlatform.cpp")
target_compile_definitions(Lx16BusHost PRIVATE ${LABX16_DEFINITIONS} JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0)
target_compile_options(Lx16BusHost PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(Lx16BusHost PRIVATE lab_core juce::juce_audio_processors juce::juce_audio_utils juce::juce_recommended_config_flags)

# Wave 1
lab_add_effect(NAME Kaymanov SLUG kaymanov PRODUCT "KAYMANOV" CODE L6Ka VERSION 0.1.0 CATEGORIES Fx Distortion
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/kaymanov/Kaymanov.cpp")
lab_add_effect(NAME Dosimeter SLUG dosimeter PRODUCT "DOSIMETER" CODE L6Do VERSION 0.1.0 CATEGORIES Fx Distortion
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/dosimeter/Dosimeter.cpp")
lab_add_effect(NAME Silo SLUG silo PRODUCT "SILO" CODE L6Si VERSION 0.1.0 CATEGORIES Fx Reverb
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/silo/Silo.cpp")
lab_add_effect(NAME Rainbow SLUG rainbow PRODUCT "RAINBOW" CODE L6Rb VERSION 0.1.0 CATEGORIES Fx Delay
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/rainbow/Rainbow.cpp")
lab_add_effect(NAME WishGranter SLUG wish_granter PRODUCT "WISH GRANTER" CODE L6Wg VERSION 0.1.0 CATEGORIES Fx Reverb
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/wish_granter/WishGranter.cpp")

# Wave 2
lab_add_effect(NAME PsyField SLUG psy_field PRODUCT "PSY-FIELD" CODE L6Ps VERSION 0.1.0 CATEGORIES Fx Modulation MIDI
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/psy_field/PsyField.cpp")
lab_add_effect(NAME Anomaly SLUG anomaly PRODUCT "ANOMALY" CODE L6An VERSION 0.1.0 CATEGORIES Fx Delay MIDI
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/anomaly/Anomaly.cpp")
lab_add_effect(NAME Apparition SLUG apparition PRODUCT "APPARITION" CODE L6Ap VERSION 0.1.0 CATEGORIES Fx Delay
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/apparition/Apparition.cpp")
lab_add_effect(NAME Carrier SLUG carrier PRODUCT "CARRIER" CODE L6Cr VERSION 0.1.0 CATEGORIES Fx Filter MIDI
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/carrier/Carrier.cpp")
lab_add_effect(NAME Oasis SLUG oasis PRODUCT "OASIS" CODE L6Oa VERSION 0.1.0 CATEGORIES Fx Restoration
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/oasis/Oasis.cpp")
lab_add_effect(NAME Gauss SLUG gauss PRODUCT "GAUSS" CODE L6Ga VERSION 0.1.0 CATEGORIES Fx Dynamics
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/gauss/Gauss.cpp")
lab_add_effect(NAME PsyDog SLUG psy_dog PRODUCT "PSY-DOG" CODE L6Pd VERSION 0.1.0 CATEGORIES Fx Spatial
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/psy_dog/PsyDog.cpp")

# Wave 3: the Zone Bus clients
lab_add_effect(NAME Emission SLUG emission PRODUCT "EMISSION" CODE L6Em VERSION 0.1.0 CATEGORIES Fx
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/emission/Emission.cpp")
lab_add_effect(NAME Contagion SLUG contagion PRODUCT "CONTAGION" CODE L6Co VERSION 0.1.0 CATEGORIES Fx
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/contagion/Contagion.cpp")

# Wave 4
lab_add_effect(NAME HundredRads SLUG hundred_rads PRODUCT "100 RADS" CODE L6Rd VERSION 0.1.0 CATEGORIES Fx Dynamics
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/hundred_rads/HundredRads.cpp")
lab_add_effect(NAME CConsciousness SLUG c_consciousness PRODUCT "C-CONSCIOUSNESS" CODE L6Cc VERSION 0.1.0 CATEGORIES Fx Reverb
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/c_consciousness/CConsciousness.cpp")
lab_add_effect(NAME Artifact SLUG artifact PRODUCT "ARTIFACT" CODE L6Ar VERSION 0.1.0 CATEGORIES Fx Filter
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/artifact/Artifact.cpp")
lab_add_effect(NAME Snork SLUG snork PRODUCT "SNORK" CODE L6Sn VERSION 0.1.0 CATEGORIES Fx
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/snork/Snork.cpp")

# Wave 5
lab_add_effect(NAME Zealots SLUG zealots PRODUCT "ZEALOTS" CODE L6Ze VERSION 0.1.0 CATEGORIES Fx Modulation
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/zealots/Zealots.cpp")
lab_add_effect(NAME NumbersStation SLUG numbers_station PRODUCT "NUMBERS STATION" CODE L6Ns VERSION 0.1.0 CATEGORIES Fx Filter
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/numbers_station/NumbersStation.cpp")
lab_add_effect(NAME Sarcophagus SLUG sarcophagus PRODUCT "SARCOPHAGUS" CODE L6Sa VERSION 0.1.0 CATEGORIES Fx Delay
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/sarcophagus/Sarcophagus.cpp")
lab_add_effect(NAME Poltergeist SLUG poltergeist PRODUCT "POLTERGEIST" CODE L6Po VERSION 0.1.0 CATEGORIES Fx Spatial
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/poltergeist/Poltergeist.cpp")
lab_add_effect(NAME ClearSky SLUG clear_sky PRODUCT "CLEAR SKY" CODE L6Cs VERSION 0.1.0 CATEGORIES Fx Modulation
               SOURCES "${CMAKE_SOURCE_DIR}/plugins/clear_sky/ClearSky.cpp")
