#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// THE GUITAR page (PR7-I 0.2.0, plain: 0.3.0 gives it the Tron look and the reference instrument's layout):
//   - PLAYING: FRETTING MODE, PICKING MODE, FORCE STRING, HAND POSITION, TRANSPOSE;
//   - SOUND: VEL > VOL, RELEASE, TONE, the preset's LEVEL;
//   - THE GUITAR: the folder of takes it plays, and how much of it there is;
//   - RENDERS: a row per string: its render list (click to save the MIDI file; drag it onto a DAW track), IMPORT... (or
//     drop the rendered audio on the row), and what's been imported for it.
class GuitarPage final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit GuitarPage (ParagonX1AudioProcessor&);
    ~GuitarPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    class StringRow;
    StringRow& getRow (int string) noexcept { return *rows[(size_t) string]; }
    juce::String getGuitarStatus() const { return guitarStatus.getText(); }
    void refresh();   // the folder, counts and reports, now (the broadcaster's; tests)

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void chooseFolder();
    void saveAllLists();

    ParagonX1AudioProcessor& processor;
    SynthPanel playing { "PLAYING" }, sound { "SOUND" }, guitarPanel { "THE GUITAR" }, renders { "RENDERS" };
    ParamControl fretMode, pickMode, forceString, hand, transpose, velToVol, release, tone, level;
    juce::Label folderLabel, guitarStatus, renderHelp;
    juce::TextButton chooseButton { "CHOOSE FOLDER..." }, reloadButton { "RELOAD" }, saveAllButton { "SAVE ALL LISTS..." };
    std::vector<std::unique_ptr<StringRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};

// A string's row in RENDERS.
class GuitarPage::StringRow final : public juce::Component,
                                    public juce::FileDragAndDropTarget
{
public:
    StringRow (ParagonX1AudioProcessor&, int string);
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
    bool dropping = false;
    juce::Label name, status;
    juce::TextButton midiButton { "MIDI" }, importButton { "IMPORT..." };
    std::unique_ptr<juce::FileChooser> chooser;
};
