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
#include "ui/MachineView.h"
#include "ui/ProgressBox.h"
#include "ui/GroovesPage.h"
#include "ui/MixerPage.h"
#include "ui/SongTrack.h"
#include "ui/BlockEditor.h"
#include "ui/PlayStylePanel.h"
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

// Everything in the plugin window, laid out on a fixed 1600 x 900 canvas
// (the editor scales it to the window, staying sharp: it's all vectors).
//
//   header:   logo | DRUMS FX PRESETS SETTINGS | < preset > | undo redo save settings
//   DRUMS:    the kit and the chosen drum (or the sequencer)       | OUTPUT
//   strip:    the SONG track (0.41.0: the on-screen keyboard that shared it is gone)
//
// FX, PRESETS and SETTINGS cover the DRUMS page; the song strip and OUTPUT stay.
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
    // 0.45.0: THE GROOVES WINDOW: the GROOVES page in a window of its own (its OWN WINDOW button) and back (BACK IN PLUGIN,
    // or closing the window); while it's out, the GROOVES tab shows where it went.
    void setGroovesInOwnWindow (bool);
    // False (tests, snapshots): the window kept off the screen (a display with no window manager can't take one: JUCE's
    // window asks for the manager's properties and X stops the program); the page moves into it and back all the same.
    static inline bool groovesWindowOnDesktop = true;
    bool areGroovesInOwnWindow() const noexcept { return groovesWindow != nullptr; }
    juce::Component* getGroovesWindow() const noexcept;
    MixerPage& getMixerPage() noexcept { return mixerPage; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }
    SongTrack& getSongTrack() noexcept { return songTrack; }
    BlockEditor& getBlockEditor() noexcept { return blockEditor; }
    // 0.48.0: THE GRID EDITOR in a window of its own (OWN WINDOW / BACK IN PLUGIN; closing it brings it back).
    void setGridEditorInOwnWindow (bool);
    bool isGridEditorInOwnWindow() const noexcept { return gridWindow != nullptr; }
    juce::Component* getGridEditorWindow() const noexcept;
    void hideGridEditor();   // closed wherever it is
    PlayStylePanel& getPlayStyle() noexcept { return playStyle; }
    // EDIT PLAY STYLE (0.46.0) on the song track's chosen blocks (one of it, the GRID EDITOR and the SONG CREATOR at a time).
    void openPlayStyle (int block);
    void openGridEditor (int block);
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
    // THE SONG TRACK (0.8.0): the strip under the pages (since 0.41.0 only it: no keyboard, no KEYS | SONG).
    void showSong (bool);
    bool isShowingSong() const noexcept { return songTrack.isVisible(); }   // (0.41.0: always, but under the machine)
    // THE DRUM MACHINE (0.38.0): the DRUMS page's MACHINE view, over everything under the header (the page, the
    // song strip and OUTPUT hidden under it while it's shown).
    MachineView& getMachine() noexcept { return machine; }
    // 0.40.0: a long job's progress (BOUNCE, a kit loading for more than half a second), PR7-S's box over the window.
    ProgressBox& getProgressBox() noexcept { return progressBox; }
    void checkLongJobs (double nowMs);   // the timer's look at them (tests)
    bool isShowingMachine() const noexcept { return machine.isVisible(); }

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
    void updateMachine();

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
    SongTrack songTrack;
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
    BlockEditor blockEditor;   // EDIT BLOCK (0.9.0): over the pages, opened from the song track (0.46.0: the GRID EDITOR)
    PlayStylePanel playStyle;  // EDIT PLAY STYLE (0.46.0): over the pages too
    bool playStyleLast = true; // which of the two a double-click on a block opens (the one used last)
    SongCreator songCreator;   // THE SONG CREATOR (0.10.0): over the pages too (one of the two at a time)
    MachineView machine;       // THE DRUM MACHINE (0.38.0): over everything under the header
    // 0.45.0: the GROOVES page's own window (after the page: it goes first, letting go of the page), and what the GROOVES
    // tab shows meanwhile.
    struct GroovesWindow;
    std::unique_ptr<GroovesWindow> groovesWindow;
    struct GridWindow;
    std::unique_ptr<GridWindow> gridWindow;   // 0.48.0 (after the editor: it lets go of the editor first)
    juce::Label groovesAwayNote;
    juce::TextButton groovesShowWindow { "SHOW ITS WINDOW" }, groovesBringBack { "BACK IN PLUGIN" };
    void showGroovesAway();
    ProgressBox progressBox;   // over everything while a long job runs (0.40.0)
    double loadingSince = -1.0;
    double hoverSince = -1.0;  // a drag over the DRUMS tab since then (-1: none)
    bool dragPeek = false;     // the DRUMS page brought in front for a drag
    Page page = Page::drums;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
