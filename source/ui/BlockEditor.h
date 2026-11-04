// THE GRID EDITOR (PR7-D 0.47.0; EDIT BLOCK from 0.9.0): one song block's notes, drum by drum, over the page, after
// section 7.3.2 of the manual the owner sent (the song track chapter). How it works follows the manual; the look, the
// words and the swing / randomize / dynamics calculations are ours.
//
// The rows: one per instrument (drums::pieces) the block plays or the kit has on a note (ROWS: SHOW UNMAPPED for every
// one), each with an arrow that folds out its articulations' own rows; drag a row's name up or down to reorder (ROWS:
// RESET ORDER), M / S mute or solo it while the song plays (CLEAR / RECALL SOLO-MUTE). The notes are diamonds, brighter
// the louder; the velocity lane below has a stalk per note.
//   POINTER: click a note to choose it (SHIFT: more), drag on empty space for a lasso, click a row's name for all its
//   notes; drag chosen notes along the beat (on the grid while SNAP is on) and onto other rows; ALT-drag copies them.
//   PENCIL (or CTRL + ALT held): click or drag along a row to draw notes on the resolution; in the velocity lane, draw
//   velocities. Right-click a note: gone. DELETE: the chosen notes gone; ALT + LEFT / RIGHT: nudge; LEFT / RIGHT: jump
//   to the next / previous note; SHIFT + I: invert; CTRL + A / C / V: all, copy, paste.
//   RESOLUTION (AUTO: the finest grid line drawn at this zoom) for SNAP, QUANTIZE, SWING, RANDOMIZE and NUDGE;
//   QUANTIZE % (MAX), SWING and RANDOMIZE move the chosen notes (all, when none) from where they were when chosen;
//   VELOCITY, DYNAMICS, RANDOMIZE VELOCITY and SLOPE likewise change their velocities. SELECT: invert, exclude on / off
//   beats, by note value, jump. OPTIONS: stretch notes, preview sounds. The time bar (right-click): straight or
//   triplet lines. ZOOM (ALT + wheel), AUTO SCROLL with the song. Each change is one undo step.
#pragma once

#include "PluginProcessor.h"

class BlockEditor final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener,
                          private juce::Timer
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;

    // A row: an instrument (folded: all its articulations' notes on it; unfolded: a heading, its articulations below),
    // or one articulation under its unfolded instrument.
    struct Row
    {
        int piece = -1, slot = -1;   // slot -1: the instrument's own row
        bool heading = false;        // an unfolded instrument's row (its notes are on the rows below)
        bool operator== (const Row&) const = default;
    };
    enum class Tool { pointer, pencil };
    enum class Slider { quantize, swing, randomize, velocity, dynamics, randomVelocity, slope };

    // For tests and snapshots (each as the control does it).
    const std::vector<Row>& getRows() const noexcept { return rows; }
    int rowOf (int slot) const;                          // the row a slot's notes are drawn on (-1: not shown)
    juce::Rectangle<float> cellOf (int slot, double beat) const;   // a note there, as drawn
    juce::Rectangle<int> rowNameArea (int row) const;
    juce::Rectangle<float> velocityPointOf (int slot, double beat) const;
    juce::Rectangle<int> getGrid() const noexcept { return grid; }
    juce::Rectangle<int> getLane() const noexcept { return lane; }
    void setTool (Tool);
    Tool getTool() const noexcept { return tool; }
    void setSnap (bool);
    void setResolution (int index);                       // resolutionNames' index (0: AUTO)
    double resolutionBeats() const noexcept;              // what it comes to now
    static const juce::StringArray resolutionNames;
    void setTriplets (bool);                              // the time bar's lines
    void setZoom (double pixelsPerBeat);
    double getZoom() const noexcept { return pxPerBeat; }
    void setViewStart (double beat);
    double getViewStart() const noexcept { return viewBeat; }
    void setAutoScroll (bool);

    std::vector<song::Hit> getSelection() const;          // the chosen notes, in time order
    void select (int slot, double beat, bool add);
    void selectRow (int row, bool add);
    void selectAll();
    void selectNone();
    void invertSelection();
    void exclude (bool onBeats);                          // SELECT > EXCLUDE ON BEATS / OFF BEATS
    void selectNoteValue (double beats);                  // SELECT > NOTE VALUES (refines the choice, or all notes)
    void jump (int direction);                            // the next / previous note on its own
    void nudge (int direction);                           // the chosen notes by the resolution
    void deleteSelected();
    void copySelected();
    void paste();                                         // at the beat the copy came from (merged)
    void stretch (grooves::TempoFactor);                  // OPTIONS > STRETCH NOTES (the chosen, or the whole block)
    void setSliderValue (Slider, double percent, bool asUndoStep = true);   // as dragging that slider and letting go
    double getSliderValue (Slider) const;

    void setExpanded (int piece, bool);
    void setAllExpanded (bool);
    void setShowAll (bool);                               // ROWS > SHOW UNMAPPED
    void moveRow (int piece, int toIndex);                // as dragging its name
    void resetOrder();
    void setRowMute (int row, bool);
    void setRowSolo (int row, bool);
    void clearSoloMute();
    void recallSoloMute();
    bool hasSoloMute() const noexcept;
    void setPreviewSounds (bool on) { previewSounds = on; }
    juce::PopupMenu selectMenu();
    juce::PopupMenu optionsMenu();
    juce::PopupMenu rowsMenu();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    const song::Block* block() const noexcept;
    void edit (const std::function<void (song::Block&)>&);   // the block changed as an undo step
    void refresh();
    void layoutRows();
    void updateHeard();
    double beatAt (float x) const noexcept;
    float xOf (double beat) const noexcept;
    int rowAt (float y) const noexcept;
    float rowTop (int row) const noexcept;
    float rowHeight() const noexcept { return 22.0f; }
    song::SlotMask slotsOfRow (int row) const;
    int slotForRow (int row, int fromSlot) const;        // a note moved onto that row: which slot it plays
    double snap (double beat) const;
    double barBeats() const;
    struct Key { int slot; double beat; };
    bool isSelected (const song::Hit&) const;
    int hitAt (juce::Point<float>) const;                // index in the block's hits (-1)
    int velocityHitAt (juce::Point<float>) const;
    void previewSlot (int slot);
    void capture();                                       // the chosen notes as they are, for the sliders
    void applySliders (bool asUndoStep);
    void syncSliderLabels();
    void clampView();

    ParagonX1AudioProcessor& processor;
    int blockIndex = -1;
    std::vector<Row> rows;
    std::vector<int> order;                              // the instruments, top to bottom
    std::array<bool, synth::drums::numPieces> expanded {};
    std::array<bool, synth::drums::numPieces> muted {}, soloed {}, recallMuted {}, recallSoloed {};
    bool showAll = false, snapOn = true, triplets = false, autoScroll = true, previewSounds = true;
    Tool tool = Tool::pointer;
    bool pencilHeld = false;                              // CTRL + ALT
    int resolution = 0;
    double pxPerBeat = 0.0, viewBeat = 0.0;               // 0: fit the block
    int rowScroll = 0;                                    // the first row shown
    std::vector<Key> selection;

    // The sliders work from the chosen notes as they were when chosen (`base`: their indices in `baseBlock`'s hits).
    std::optional<song::Block> baseBlock, lastMade;
    std::vector<size_t> baseNotes;
    std::array<double, 7> sliderValues {};
    bool sliderDragging = false;

    enum class Drag { none, lasso, move, pencil, velocity, rowOrder, scroll } drag = Drag::none;
    juce::Point<float> dragStart, dragNow;
    bool dragCopy = false;
    double moveBeats = 0.0;
    int moveRows = 0, grabHit = -1, grabRow = -1;
    std::vector<song::Hit> drawn;                         // the pencil's notes while drawing
    std::vector<std::pair<size_t, float>> velocityEdits;  // the lane's drawn velocities, by hit index

    std::optional<song::Block> clipboard;
    juce::Rectangle<int> names, grid, ruler, lane, laneControls, toolbar;

    juce::TextButton playStyleTab { "EDIT PLAY STYLE" }, gridTab { "GRID EDITOR" }, closeButton { "X" },
                     pointerButton { "POINTER" }, pencilButton { "PENCIL" }, snapButton { "SNAP" }, maxButton { "MAX" },
                     nudgeLeft { "<" }, nudgeRight { ">" }, selectButton { "SELECT" }, optionsButton { "OPTIONS" },
                     rowsButton { "ROWS" }, soloMuteButton { "CLEAR SOLO / MUTE" }, autoScrollButton { "AUTO SCROLL" };
    juce::ComboBox resolutionChoice;
    std::array<juce::Slider, 7> sliders;
    std::array<juce::Label, 7> sliderLabels;
    juce::Slider zoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label zoomLabel { {}, "ZOOM" }, nudgeLabel { {}, "NUDGE" }, hint;
    juce::ScrollBar hScroll { false }, vScroll { true };
    double shownPlayhead = -1.0;
};
