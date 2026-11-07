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
#include "ui/ProgressBox.h"
#include "ui/Fretboard.h"
#include "ui/GuitarPage.h"

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
//   header:   logo | GUITAR PRESETS SETTINGS | < preset > | undo redo save settings
//   GUITAR:   the playing settings, the guitar's folder, the render lists      | OUTPUT
//   strip:    THE FRETBOARD: each note on the string and fret it's played on
//
// PRESETS and SETTINGS cover the GUITAR page; the fretboard and OUTPUT stay.
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

    enum class Page { guitar, presets, settings };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    // For snapshots and tests.
    GuitarPage& getGuitarPage() noexcept { return guitarPage; }
    Fretboard& getFretboard() noexcept { return fretboard; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }
    // A long job's progress (IMPORT A RENDER, the guitar loading for more than half a second), PR7-S's box over the window.
    ProgressBox& getProgressBox() noexcept { return progressBox; }
    void checkLongJobs (double nowMs);   // the timer's look at them (tests)

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

    IconButton guitarTab { "The guitar: how it plays, its folder of takes, the render lists", Icons::Type::guitar, "GUITAR" },
               presetsTab { "Browse, save and organise presets", Icons::Type::presets, "PRESETS" },
               settingsTab { "The presets folder, MIDI links, note names and more", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous preset", Icons::Type::chevronLeft }, nextRig { "Next preset", Icons::Type::chevronRight };
    juce::TextButton rigNameButton;
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the preset (as a new preset if it's a factory one or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };

    GainColumn outputColumn;
    Fretboard fretboard;
    MidiLinks shownLinks;
    juce::String shownLearning;

    GuitarPage guitarPage;
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    ProgressBox progressBox;   // over everything while a long job runs
    double loadingSince = -1.0;
    Page page = Page::guitar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
