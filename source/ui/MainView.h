#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/DockPanels.h"
#include "ui/Icons.h"
#include "ui/PaintCache.h"
#include "ui/LevelMeter.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/RigBrowser.h"
#include "ui/DrumPage.h"
#include "ui/GroovesPage.h"
#include "ui/MixerPage.h"
#include "ui/SongTrack.h"
#include "ui/BlockEditor.h"
#include "ui/SongCreator.h"
#include "ui/FxPage.h"

// A side strip: a title, a gain knob with its value underneath, and a level meter.
class GainColumn final : public juce::Component
{
public:
    GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                const juce::String& parameterId, LevelMeterSource& meterSource);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    LevelMeter meter;
};

// The on-screen keyboard, with the keys the note map plays a drum on tinted (and, with the ROOT layout and the sequencer
// on KEY, the four that start its patterns, more faintly).
class KitKeyboard final : public juce::MidiKeyboardComponent
{
public:
    KitKeyboard (ParagonX1AudioProcessor&, juce::MidiKeyboardState&, Orientation);
    bool isKitKey (int note) const noexcept { return note >= 0 && note < 128 && map[(size_t) note] >= 0; }
    bool isPatternKey (int note) const noexcept { return patternKeys && note >= kitRoot - 4 && note < kitRoot; }
    void paint (juce::Graphics&) override;

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    void followKit();

    // Its own timer (the keyboard already has one for scrolling).
    struct Poll final : juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { callback(); }
    };
    ParagonX1AudioProcessor& processor;
    bool patternKeys = false;
    int kitRoot = 36;
    synth::drums::notemap::Map map = synth::drums::notemap::rootLayout (36);
    Poll poll;
};

// Everything in the plugin window, laid out on a fixed 1600 x 900 canvas
// (the editor scales it to the window, staying sharp: it's all vectors).
//
//   header:   logo | DRUMS FX PRESETS SETTINGS | < preset > | undo redo save settings
//   DRUMS:    the kit and the chosen drum (or the sequencer)       | OUTPUT
//   keyboard: the on-screen keyboard, always there (play the kit with the mouse)
//
// FX, PRESETS and SETTINGS cover the DRUMS page; the keyboard and OUTPUT stay.
class MainView final : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::DragAndDropTarget,
                       public ControlHost,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    enum class Page { drums, fx, presets, settings, grooves, mixer };   // mixer: 0.20.0
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    // For snapshots and tests.
    FxPage& getFxPage() noexcept { return fxPage; }
    DrumPage& getDrumPage() noexcept { return drumPage; }
    GroovesPage& getGroovesPage() noexcept { return groovesPage; }
    MixerPage& getMixerPage() noexcept { return mixerPage; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }
    KitKeyboard& getKeyboard() noexcept { return keyboard; }
    SongTrack& getSongTrack() noexcept { return songTrack; }
    BlockEditor& getBlockEditor() noexcept { return blockEditor; }
    SongCreator& getSongCreator() noexcept { return songCreator; }
    void openSongCreator (const juce::File& source = {});   // over the page (EDIT BLOCK closed), with that source if given

    // A GROOVE DRAGGED OVER THE DRUMS TAB (0.13.0): held there half a second, the DRUMS page with the SEQUENCER comes in
    // front (the page it came from stays under it, so the drag carries on), to drop it on the grid; when the drag is
    // over, the window settles on DRUMS. updateDragHover is the timer's (and tests') look at it.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void updateDragHover (double nowMs, bool dragStillOn);
    bool isPeekingAtSequencer() const noexcept { return dragPeek; }
    juce::Rectangle<int> getDrumsTabBounds() const { return drumsTab.getBounds(); }
    // THE SONG TRACK (0.8.0): the strip under the pages shows the keyboard or the song (KEYS | SONG beside it).
    void showSong (bool);
    bool isShowingSong() const noexcept { return songTrack.isVisible(); }

    // ControlHost: the knobs' MIDI learn.
    int linkedController (const juce::String& parameterId) const override;
    bool isLearning (const juce::String& parameterId) const override;
    void learn (const juce::String& parameterId) override;
    void forget (const juce::String& parameterId) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void paintBackground (juce::Graphics&);
    PaintCache backgroundCache { true };
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshRig();
    void saveRig();

    ParagonX1AudioProcessor& audioProcessor;

    // Header
    IconButton drumsTab { "The drums: a kit of 20, the drum sequencer", Icons::Type::drums, "DRUMS" },
               groovesTab { "Your own MIDI grooves: browse, play through the kit, drag into your DAW", Icons::Type::grooves, "GROOVES" },
               mixerTab { "The mixer: a multi-mic kit's mic channels, bleed, and where each goes", Icons::Type::mixer, "MIXER" },
               fxTab { "The effects: insert buses, DELAY and REVERB sends, and the MASTER rack after the kit", Icons::Type::rack, "FX" },
               presetsTab { "Browse, save and organise presets", Icons::Type::presets, "PRESETS" },
               settingsTab { "The keys, presets folder, MIDI links and more", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous preset", Icons::Type::chevronLeft }, nextRig { "Next preset", Icons::Type::chevronRight };
    juce::TextButton rigNameButton;
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the preset (as a new preset if it's a factory one or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };

    GainColumn outputColumn;
    KitKeyboard keyboard;
    SongTrack songTrack;
    juce::TextButton keysTab { "KEYS" }, songTab { "SONG" };
    // What the knobs show (links, learning), as last drawn: a change repaints them.
    MidiLinks shownLinks;
    juce::String shownLearning;

    // The pages
    DrumPage drumPage;
    GroovesPage groovesPage;
    MixerPage mixerPage;
    FxPage fxPage;
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    BlockEditor blockEditor;   // EDIT BLOCK (0.9.0): over the pages, opened from the song track
    SongCreator songCreator;   // THE SONG CREATOR (0.10.0): over the pages too (one of the two at a time)
    double hoverSince = -1.0;  // a drag over the DRUMS tab since then (-1: none)
    bool dragPeek = false;     // the DRUMS page brought in front for a drag
    Page page = Page::drums;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
