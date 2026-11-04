// EDIT BLOCK (PR7-D 0.9.0, the song track part 2): one song block's groove, drum by drum, over the page (the song
// track stays below it, playing). Our own design, inspired by the block editing in chapter 7 of the manual the owner
// sent (its play-style editor and grid editor, in one simpler panel).
//
// A row for each drum the block plays (ADD DRUM for another); its notes on a grid one time round the groove, each as
// tall as it's loud. Click an empty place: a note there (on GRID); drag a note up or down: louder or softer; sideways
// (0.45.0): along the beat, snapped to GRID (ALT: free); right-click
// a note: gone. Click a drum's name to choose it (CTRL / SHIFT: more); VELOCITY, QUANTIZE, REMOVE, COPY, PASTE and MOVE
// TO work on the chosen drums, or on all of them when none is chosen. Each change is an undo step.
#pragma once

#include "PluginProcessor.h"

class BlockEditor final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit BlockEditor (ParagonX1AudioProcessor&);
    ~BlockEditor() override;

    void open (int block);   // shows it, on that block
    int getBlock() const noexcept { return blockIndex; }
    std::function<void()> onClose;
    std::function<void (int block)> onPlayStyle;   // 0.46.0: its EDIT PLAY STYLE tab

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // For tests and snapshots.
    std::vector<int> getRows() const;                    // the drums shown, top to bottom
    song::SlotMask getChosenDrums() const noexcept { return chosenDrums; }
    void chooseDrum (int slot, bool add);                // as clicking its name (with CTRL: add)
    void moveNote (int slot, double fromBeat, double toBeat);   // 0.45.0: as dragging it sideways (one undo step)
    void addRow (int slot);                              // as ADD DRUM
    juce::Rectangle<float> cellOf (int slot, double beat) const;   // where a note there would be drawn
    juce::Rectangle<int> getGrid() const noexcept { return grid; }
    void setGrid (int quantize) { gridChoice.setSelectedItemIndex (quantize, juce::sendNotificationSync); }
    void applyVelocity (float factor);                   // as letting go of VELOCITY there
    void quantizeChosen();
    void removeChosen();
    void copyChosen();
    void pasteChosen();
    void moveChosenTo (int slot);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    const song::Block* block() const noexcept;
    void edit (const std::function<void (song::Block&)>&);   // the block changed as an undo step
    song::SlotMask targetDrums() const noexcept;                   // the chosen drums, else every one
    void refresh();
    double beatAt (float x) const noexcept;
    int rowAt (float y) const noexcept;
    double gridBeats() const noexcept { return grooves::gridOf (gridChoice.getSelectedItemIndex()); }

    ParagonX1AudioProcessor& processor;
    int blockIndex = -1;
    song::SlotMask chosenDrums = 0, extraRows = 0;
    std::vector<int> rows;
    juce::Rectangle<int> names, grid, ruler;

    juce::TextButton playStyleTab { "EDIT PLAY STYLE" };
    juce::TextButton close { "CLOSE" }, quantizeButton { "QUANTIZE" }, removeButton { "REMOVE" }, copyButton { "COPY" },
                     pasteButton { "PASTE" };
    juce::ComboBox gridChoice, moveTo, addDrum;
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label gridLabel { {}, "GRID" }, velocityLabel { {}, "VELOCITY" }, moveLabel { {}, "MOVE TO" }, hint;

    // A note being dragged up or down: which, and its velocity as dragged.
    int dragSlot = -1;
    double dragBeat = 0.0;
    float dragFrom = 0.0f, dragVelocity = 0.0f;
    // 0.45.0: the drag's way, decided by its first few pixels: up / down (velocity) or sideways (the note moved there).
    enum class DragWay { undecided, velocity, move } dragWay = DragWay::undecided;
    double dragTo = 0.0;
};
