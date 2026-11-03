cmake_minimum_required(VERSION 3.24)

# macOS: build one universal binary for Apple Silicon and Intel Macs.
# These must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(ParagonX1 VERSION 0.33.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PARAGON_BUILD_TESTS "Build the unit tests" ON)

include(cmake/Dependencies.cmake)

juce_add_plugin(ParagonX1
    PRODUCT_NAME             "Paragon PR7-D" # the drum instrument of the PR7 family: its own name, bundle ID and plugin code, so it installs beside the others
    COMPANY_NAME             "Paragon"
    BUNDLE_ID                "com.paragon.paragonpr7d"
    PLUGIN_MANUFACTURER_CODE Prgn     # four characters, at least one upper case
    PLUGIN_CODE              Pr7d     # four characters, exactly one upper case; never PR7-G's Px1a, PR7-B's Pr7b, PR7-V's Pr7v, PR7-C's Pr7c, PR7-E's Pr7e or PR7-K's Pr7k
    FORMATS                  VST3
    IS_SYNTH                 TRUE     # an instrument: MIDI in, sound out
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    VST3_CATEGORIES          Instrument Drum
    COPY_PLUGIN_AFTER_BUILD  FALSE)

# Inter (SIL Open Font License, see resources/fonts/OFL-LICENSE.txt), built
# into the plugin so it looks the same on every computer, as in the mockup.
juce_add_binary_data(ParagonX1Fonts
    HEADER_NAME ParagonFonts.h
    NAMESPACE   ParagonFonts
    SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/InterDisplay-BlackItalic.ttf)

# THE KIT VIEW's pictures (0.14.0): our own 3D kit, rendered once (the kit, each piece glowing, a click map, where
# each glow goes). See resources/kit/README.md.
juce_add_binary_data(ParagonX1Kit
    HEADER_NAME KitImages.h
    NAMESPACE   KitImages
    SOURCES
        resources/kit/kit.jpg
        resources/kit/kitzones.png
        resources/kit/kitlayout.txt
        resources/kit/litkick.png
        resources/kit/litsnare.png
        resources/kit/littom1.png
        resources/kit/littom2.png
        resources/kit/litfloor.png
        resources/kit/lithihat.png
        resources/kit/litcrash.png
        resources/kit/litcrash2.png
        resources/kit/litride.png
        resources/kit/litchina.png
        resources/kit/litsplash.png
        resources/kit/littambourine.png)

target_sources(ParagonX1
    PRIVATE
        source/Parameters.cpp
        source/RackParameters.cpp
        source/PluginProcessor.cpp
        source/MixerEffects.cpp
        source/PluginEditor.cpp
        source/dsp/synth/Drums.cpp
        source/dsp/synth/MicSim.cpp
        source/dsp/synth/DrumGizmo.cpp
        source/dsp/synth/SamplePack.cpp
        source/dsp/synth/DiskStreamer.cpp
        source/dsp/synth/Instrument.cpp
        source/grooves/Groove.cpp
        source/grooves/GrooveLibrary.cpp
        source/grooves/Rhythm.cpp
        source/grooves/Riff.cpp
        source/grooves/SqliteReader.cpp
        source/grooves/GrooveDatabase.cpp
        source/song/Song.cpp
        source/song/Creator.cpp
        # The effects: PR7-E's rack (ECHELON, DOMINION, CONSTRICTOR, the LIMITER) and PR7-G's sends and inserts.
        source/dsp/dominion/Dominion.cpp
        source/dsp/effects/Compressor.cpp
        source/dsp/effects/MultibandComp.cpp
        source/dsp/effects/Limiter.cpp
        source/dsp/effects/MasterLimiter.cpp
        source/dsp/effects/Loudness.cpp
        source/dsp/effects/Delay.cpp
        source/dsp/effects/Reverb.cpp
        source/dsp/effects/Aeon.cpp
        source/dsp/effects/Modulation.cpp
        source/dsp/effects/LoFi.cpp
        source/dsp/effects/Drive.cpp
        source/dsp/effects/Glitch.cpp
        source/dsp/effects/Abyss.cpp
        source/dsp/effects/Chimera.cpp
        source/dsp/effects/Cosmonaut.cpp
        source/dsp/effects/Halo.cpp
        source/dsp/effects/Rabid.cpp
        source/dsp/effects/PitchShifter.cpp
        source/dsp/eq/EqDesign.cpp
        source/dsp/eq/ParametricEq.cpp
        source/dsp/eq/EqMatch.cpp
        source/dsp/eq/EqMatchSession.cpp
        source/rigs/Rig.cpp
        source/rigs/FactoryRigs.cpp
        source/rigs/CompPresets.cpp
        source/rigs/DominionPresets.cpp
        source/rigs/RigLibrary.cpp
        source/ui/LevelMeter.cpp
        source/ui/Logo.cpp
        source/ui/RigBrowser.cpp
        source/ui/Icons.cpp
        source/ui/DockPanels.cpp
        source/ui/SynthControls.cpp
        source/ui/SynthDisplays.cpp
        source/ui/MultiPointEditor.cpp
        source/ui/DrumPage.cpp
        source/ui/KitView.cpp
        source/ui/ArticulationPanel.cpp
        source/ui/VelocityCurveEditor.cpp
        source/ui/MidiInPanel.cpp
        source/ui/MixerPage.cpp
        source/ui/GroovesPage.cpp
        source/ui/TapPanel.cpp
        source/ui/RiffPanel.cpp
        source/ui/SongTrack.cpp
        source/ui/BlockEditor.cpp
        source/ui/SongCreator.cpp
        source/ui/DrumSeqPage.cpp
        source/ui/FxPage.cpp
        source/ui/SendPages.cpp
        source/ui/ModFxPages.cpp
        source/ui/InsertPages.cpp
        source/ui/EqPage.cpp
        source/ui/EqDisplay.cpp
        source/ui/EqMatchPanel.cpp
        source/ui/CompPage.cpp
        source/ui/DominionPage.cpp
        source/ui/LimiterPage.cpp
        source/ui/PresetButton.cpp
        source/ui/RackUnit.cpp
        source/ui/RackSpecs.cpp
        source/ui/Footswitch.cpp
        source/ui/LedDisplay.cpp
        source/ui/MainView.cpp
        source/ui/ParagonLookAndFeel.cpp
        source/ui/Finish.cpp)

target_include_directories(ParagonX1 PUBLIC source)

target_compile_definitions(ParagonX1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=0)

target_link_libraries(ParagonX1
    PRIVATE
        ParagonX1Fonts
        ParagonX1Kit
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_warning_flags   # strict warnings for our code only
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(PARAGON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
