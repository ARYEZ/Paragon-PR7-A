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
#include "ui/MixerPage.h"
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
//   header:   logo | DRUMS MIXER FX PRESETS SETTINGS | < preset > | undo redo save settings
//   DRUMS:    the kit and the chosen drum (or the sequencer)       | OUTPUT
//   strip:    empty for now (PR7-I 0.1.0: the song track is gone; the guitar's note view is meant to go here)
//
// MIXER, FX, PRESETS and SETTINGS cover the DRUMS page; the strip and OUTPUT stay.
class MainView final : public juce::Component,
                       public juce::DragAndDropContainer,
                       public ControlHost,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    enum class Page { drums, fx, presets, settings, mixer };   // mixer: 0.20.0 (PR7-I 0.1.0: GROOVES gone)
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    // For snapshots and tests.
    FxPage& getFxPage() noexcept { return fxPage; }
    DrumPage& getDrumPage() noexcept { return drumPage; }
    MixerPage& getMixerPage() noexcept { return mixerPage; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }
    juce::Label& getStripNote() noexcept { return stripNote; }   // PR7-I 0.1.0: the empty strip's note
    // THE DRUM MACHINE (0.38.0): the DRUMS page's MACHINE view, over everything under the header (the page, the
    // strip and OUTPUT hidden under it while it's shown).
    MachineView& getMachine() noexcept { return machine; }
    // 0.40.0: a long job's progress (IMPORT A RENDER, a kit loading for more than half a second), PR7-S's box over the window.
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
    juce::Label stripNote;   // PR7-I 0.1.0: what the empty strip is for
    // What the knobs show (links, learning), as last drawn: a change repaints them.
    MidiLinks shownLinks;
    juce::String shownLearning;

    // The pages
    DrumPage drumPage;
    MixerPage mixerPage;
    FxPage fxPage;
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    MachineView machine;       // THE DRUM MACHINE (0.38.0): over everything under the header
    ProgressBox progressBox;   // over everything while a long job runs (0.40.0)
    double loadingSince = -1.0;
    Page page = Page::drums;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
