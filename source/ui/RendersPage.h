#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// THE RENDERS page (PR7-I 0.4.0: the 0.2.0 GUITAR page's folder and render lists, the playing controls gone to the new
// GUITAR page, GuitarView):
//   - THE GUITAR: the folder of takes it plays, and how much of it there is; KEY MAP (0.3.0: how the articulation
//     keyswitches work);
//   - RENDERS: SUSTAIN or PALM MUTE (0.3.0), then a row per string: its render list (click to save the MIDI file; drag
//     it onto a DAW track), IMPORT... (or drop the rendered audio on the row), and what's been imported for it.
//     NOISES (0.6.0): a row per noise list (each string's releases, power chords, unpitched, fret noise), the same way.
class RendersPage final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit RendersPage (ParagonX1AudioProcessor&);
    ~RendersPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    class StringRow;
    class NoiseRow;
    StringRow& getRow (int string) noexcept { return *rows[(size_t) string]; }
    NoiseRow& getNoiseRow (int row) noexcept { return *noiseRows[(size_t) row]; }
    void showNoises();                               // 0.6.0: the NOISES tab
    bool isShowingNoises() const noexcept { return showingNoises; }
    void showArticulation (guitar::Articulation);   // which lists RENDERS shows
    guitar::Articulation getShownArticulation() const noexcept { return shown; }
    juce::String getGuitarStatus() const { return guitarStatus.getText(); }
    void refresh();   // the folder, counts and reports, now (the broadcaster's; tests)

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void chooseFolder();
    void saveAllLists();

    ParagonX1AudioProcessor& processor;
    SynthPanel guitarPanel { "THE GUITAR" }, renders { "RENDERS" };
    ParamControl keyMap;
    juce::TextButton sustainTab { "SUSTAIN" }, muteTab { "PALM MUTE" }, noisesTab { "NOISES" };
    bool showingNoises = false;
    std::vector<std::unique_ptr<NoiseRow>> noiseRows;
    guitar::Articulation shown = guitar::Articulation::sustain;
    juce::Label folderLabel, guitarStatus, renderHelp;
    juce::TextButton chooseButton { "CHOOSE FOLDER..." }, reloadButton { "RELOAD" }, saveAllButton { "SAVE ALL LISTS..." };
    std::vector<std::unique_ptr<StringRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};

// A string's row in RENDERS.
class RendersPage::StringRow final : public juce::Component,
                                    public juce::FileDragAndDropTarget
{
public:
    StringRow (ParagonX1AudioProcessor&, int string, std::function<guitar::Articulation()> articulation);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;   // from the MIDI button: the list as a file
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void refresh();
    juce::TextButton& getMidiButton() noexcept { return midiButton; }
    juce::String getStatus() const { return status.getText(); }

private:
    void importFile();
    ParagonX1AudioProcessor& processor;
    int string;
    std::function<guitar::Articulation()> articulation;
    bool dropping = false;
    juce::Label name, status;
    juce::TextButton midiButton { "MIDI" }, importButton { "IMPORT..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

// 0.6.0: a noise list's row in RENDERS' NOISES (noises::rowList).
class RendersPage::NoiseRow final : public juce::Component,
                                   public juce::FileDragAndDropTarget
{
public:
    NoiseRow (ParagonX1AudioProcessor&, int row);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void refresh();
    juce::String getStatus() const { return status.getText(); }

private:
    ParagonX1AudioProcessor& processor;
    int row;
    bool dropping = false;
    juce::Label name, status;
    juce::TextButton midiButton { "MIDI" }, importButton { "IMPORT..." };
    std::unique_ptr<juce::FileChooser> chooser;
};
