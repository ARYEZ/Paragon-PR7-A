FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(catch2)

add_executable(ParagonX1Tests
    TestMain.cpp
    GuitarTests.cpp
    PluginTests.cpp
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


# The guitar's own code alone (PR7-I 0.2.0): its tests built without the plugin (quick to build while working on it).
add_executable(GuitarCoreTests
    TestMain.cpp
    GuitarTests.cpp
    ../source/guitar/Fretting.cpp
    ../source/guitar/RenderList.cpp
    ../source/guitar/RenderImport.cpp
    ../source/guitar/RoundRobinCheck.cpp
    ../source/guitar/GuitarLibrary.cpp
    ../source/guitar/Guitar.cpp
    ../source/dsp/DiskStreamer.cpp)
target_include_directories(GuitarCoreTests PRIVATE ../source)
target_compile_definitions(GuitarCoreTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_MP3AUDIOFORMAT=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(GuitarCoreTests PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics Catch2::Catch2
    juce::juce_recommended_config_flags)
set_target_properties(GuitarCoreTests PROPERTIES EXCLUDE_FROM_ALL TRUE)   # built on request; its tests run in ParagonX1Tests too
