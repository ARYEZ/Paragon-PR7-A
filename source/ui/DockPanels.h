#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"
#include "ui/MidiInPanel.h"
#include "ui/SynthControls.h"

// The panels' shared style, and the SETTINGS page.

// Dark rounded panel with a small grey title, shared by the panels.
namespace DockStyle
{
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area);
    void drawTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
}

//==============================================================================
// THE NOTE MAP's list (0.2.0): every MIDI note, 0 to 127, and the drum it plays (or none). Changing one makes the map
// CUSTOM (a copy of the layout chosen, with the change).
class NoteMapList final : public juce::Component
{
public:
    explicit NoteMapList (ParagonX1AudioProcessor&);
    void refresh();                                         // from the processor's map now
    juce::ComboBox& getBox (int note) { return *rows[(size_t) note]->box; }
    static constexpr int rowHeight = 26;
    // 0.18.0: one note chosen (click its number), for MIDI IN's editing.
    void select (int note);
    int getSelected() const noexcept { return selected; }
    std::function<void (int note)> onSelect;

    void resized() override;

private:
    struct Row final : public juce::Component
    {
        juce::Label note, source;   // 0.58.0: `source`, the name GROOVE LIBRARY's map gives the note
        std::unique_ptr<juce::ComboBox> box;
        bool mapped = false, chosen = false, showsNames = false;
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };
    ParagonX1AudioProcessor& processor;
    int selected = 38;
    std::vector<std::unique_ptr<Row>> rows;
    synth::drums::notemap::Map shown {};
    int shownOctave = 0;   // (0.35.1) the NOTE NAMES the rows are written with
    int shownLayout = -1;  // (0.58.0) the layout the rows' names are for
};

//==============================================================================
// SETTINGS: THE KEYS (the note map), where presets are kept, MIDI links, and what the plugin is built from.
class SettingsPage final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit SettingsPage (ParagonX1AudioProcessor&);
    ~SettingsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // MIDI LINKS (PR7-K's stage 4f): one row per link, each with a button to remove it. For tests: the rows shown.
    int getNumLinkRows() const noexcept { return (int) linkRows.size(); }
    juce::TextButton* getRemoveButton (int row) { return row < (int) linkRows.size() ? linkRows[(size_t) row]->remove.get() : nullptr; }

private:
    void timerCallback() override;
    void rebuildLinks();

    struct LinkRow final : public juce::Component
    {
        juce::Label text;
        std::unique_ptr<juce::TextButton> remove;
        void resized() override;
    };
    std::vector<std::unique_ptr<LinkRow>> linkRows;
    juce::Viewport linkView;
    juce::Component linkList;
    juce::TextButton saveDefault { "SAVE AS DEFAULT" }, exportLinks { "EXPORT..." }, importLinks { "IMPORT..." }, clearLinks { "CLEAR ALL" };
    std::unique_ptr<juce::FileChooser> chooser;
    MidiLinks shownLinks;
    juce::Rectangle<int> midiArea;

    ParagonX1AudioProcessor& processor;
    juce::TextButton openFolder { "OPEN PRESETS FOLDER" };
    juce::Rectangle<int> keysArea, rigsArea, aboutArea;
    ParamControl mapChoice, channelChoice, namesChoice;
    juce::TextButton saveMap { "SAVE AS DEFAULT" }, presetButton;
    NoteMapList mapList;
    juce::Viewport mapView;
    MidiInPanel midiInPanel;
    void showPresetMenu();
    void chooseNote (int note);

public:
    NoteMapList& getMapList() noexcept { return mapList; }
    juce::Viewport& getMapView() noexcept { return mapView; }
    MidiInPanel& getMidiInPanel() noexcept { return midiInPanel; }

private:

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
