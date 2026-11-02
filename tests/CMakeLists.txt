FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(catch2)

add_executable(ParagonX1Tests
    TestMain.cpp
    DrumTests.cpp
    DrumKitTests.cpp
    DrumSeqTests.cpp
    DrumEditorTests.cpp
    DrumKitFileTests.cpp
    KitViewTests.cpp
    ArticulationTests.cpp
    MidiInTests.cpp
    InstrumentTests.cpp
    MixerTests.cpp
    AcousticDrumTests.cpp
    MicSimTests.cpp
    DrumGizmoTests.cpp
    LayerTests.cpp
    DrumSeqPluginTests.cpp
    StackSizeTests.cpp
    ParameterIdTests.cpp
    ProcessorTests.cpp
    RigTests.cpp
    UiTests.cpp
    NoteMapTests.cpp
    OutputTests.cpp
    GrooveTests.cpp
    GrooveDatabaseTests.cpp
    GroovePlayerTests.cpp
    RhythmTests.cpp
    SongTests.cpp
    UndoTests.cpp
    EffectTests.cpp
    EqTests.cpp
    LimiterTests.cpp
    MasterLimiterTests.cpp
    RackTests.cpp
    DelayReverbTests.cpp
    ModFxTests.cpp
    ModFxStereoTests.cpp
    DriveGlitchTests.cpp
    InsertTests.cpp
    SendTests.cpp
    CompressorTests.cpp
    DominionTests.cpp
    MultibandTests.cpp
    EditorSnapshot.cpp)

# Link the plugin's shared code (processor, editor, JUCE modules) so the
# tests exercise exactly the code that ships in the VST3. JUCE modules are
# linked privately into that target, so borrow its include paths and defines
# (as SYSTEM, so warnings inside JUCE headers don't flood the test build).
target_link_libraries(ParagonX1Tests PRIVATE ParagonX1 Catch2::Catch2)
target_include_directories(ParagonX1Tests SYSTEM PRIVATE $<TARGET_PROPERTY:ParagonX1,INCLUDE_DIRECTORIES>)
target_compile_definitions(ParagonX1Tests PRIVATE
    $<TARGET_PROPERTY:ParagonX1,COMPILE_DEFINITIONS>)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ParagonX1Tests)

# Test files kept in the repository (tests/data): the groove database made by make_groove_db.py.
target_compile_definitions(ParagonX1Tests PRIVATE PARAGON_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
