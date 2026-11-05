// EDIT PLAY STYLE (PR7-D 0.46.0): changing how a song block's drums play, instrument by instrument, on the drawn kit.
// It follows section 7.3.1 of the manual the owner sent (the song track chapter): what it does and how it's used; the
// look, the words and the AMOUNT calculation are ours (song/PlayStyle.h).
//
// Opened on the song track's chosen block (double-click, the block's small arrow, right-click > EDIT MIDI > EDIT PLAY
// STYLE, or the P key); SHIFT-click more blocks in the strip while it's open to change them together. The kit: the
// instruments the block(s) play lit and outlined, the rest dimmed; click one to choose it (SHIFT / CTRL: more), or SELECT
// INSTRUMENT (every instrument, the played ones marked; ALL PLAYING DRUMS, ALL TOMS, ALL CRASHES). The chosen ones' notes
// are lit in the strip. AMOUNT thins (left) or fills (right) the chosen instruments' notes, with TOTAL HITS; its arrow (or
// CTRL / CMD-click the knob) resets it. VELOCITY: louder or softer, their balance kept. SOLO SELECTED: only the chosen
// ones heard. Right-click an instrument: CUT / COPY / PASTE MIDI (onto it, replacing its notes), REMOVE MIDI (AMOUNT
// brings them back), QUANTIZE, ARTICULATION (one instrument). The POWER HAND flag sits on the groove's leading
// instrument: drag it to another and its notes move there (merged); its arrow: the articulation. The OPENING HIT flag:
// one hit at the block's start, ON / OFF, dragged to any instrument, its arrow: the articulation. Each change is an undo
// step; recording into the song waits while the window is open. GRID EDITOR: the same block in the note-by-note editor.
#pragma once

#include "PluginProcessor.h"
#include "song/PlayStyle.h"
#include "ui/KitView.h"

class PlayStylePanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit PlayStylePanel (ParagonX1AudioProcessor&);
    ~PlayStylePanel() override;

    void open (std::vector<int> blocks);   // shows it, on those blocks (the first is the one named)
    void setBlocks (std::vector<int> blocks);
    const std::vector<int>& getBlocks() const noexcept { return blocks; }
    void close();
    std::function<void()> onClose;
    std::function<void (int block)> onGridEditor;              // the GRID EDITOR tab
    std::function<void (song::SlotMask)> onHighlightChanged;   // the chosen instruments' slots (the strip lights their notes)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // For tests and snapshots (each as the control does it).
    const std::vector<int>& getChosen() const noexcept { return chosen; }
    void choose (int piece, bool add = false);                 // as clicking it on the kit
    void chooseGroup (song::SlotMask);                         // as SELECT INSTRUMENT's groups (the pieces with those slots)
    void setAmount (double amount);                            // -1 to 1, as turning AMOUNT and letting go
    double getAmount() const;
    void resetAmount();
    void setVelocity (double amount);                          // -1 (half as loud) to 1 (twice), as VELOCITY
    void setSolo (bool);
    juce::String learnedText() const;                          // 0.54.0: AMOUNT learned or by rules, for the label
    int getTotalHits() const;                                  // the chosen instruments' notes in the blocks
    void movePowerHand (int toPiece, int toSlot = -1);
    void setPowerHandArticulation (int slot);
    void setOpeningOn (bool);
    void moveOpeningHit (int slot);
    void cutMidi();
    void copyMidi();
    void pasteMidi (int fromPiece);                            // the copied instrument's notes onto the chosen ones
    void removeMidi();
    void quantize (int quantizeChoice);                        // grooves::quantizeNames' index
    void setArticulation (int slot);                           // the one chosen instrument's notes on that articulation
    juce::PopupMenu instrumentMenu();                          // right-click on a chosen instrument
    juce::PopupMenu selectMenu();                              // SELECT INSTRUMENT
    KitView& getKit() noexcept { return kit; }
    juce::Rectangle<int> getPowerHandTag() const;              // where the flags are drawn (in this panel)
    juce::Rectangle<int> getOpeningTag() const;
    juce::Slider& getAmountKnob() noexcept { return amount; }
    juce::Slider& getVelocityKnob() noexcept { return velocity; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Each block's instrument, as AMOUNT and VELOCITY last left it: the notes it was worked out from, what it made.
    struct Item
    {
        int block = -1, piece = -1;
        std::vector<song::Hit> source, made;
        double amount = 0.0, velocity = 0.0;
    };
    Item& itemFor (const song::Song&, int block, int piece);   // started again from the block's notes if they changed
    std::vector<song::Hit> notesFor (const song::Song&, const Item&) const;
    void applyKnobs (bool asUndoStep);                          // AMOUNT and VELOCITY onto the chosen instruments
    void edit (const std::function<void (song::Song&)>&);      // an undo step
    void refresh();                                             // the kit's highlight, the knobs, the tags, the labels
    void placeTags();
    std::vector<int> usedPieces() const;                        // in any of the blocks
    juce::String chosenText() const;
    int pieceAt (juce::Point<int>) const;                       // the instrument under a point in this panel (-1)
    std::optional<juce::Point<int>> tagPointFor (int slot) const;
    juce::Rectangle<int> chipOf (int piece) const;              // the small buttons for the instruments not in the picture
    void showArticulations (bool power, juce::Component* target);   // a flag's arrow, or a click on it
public:
    juce::PopupMenu tagMenu (bool power);   // its list: the instrument it's on and its articulations, then the others A-Z (tests)
private:
    void dropTag (bool power, juce::Point<int> at);             // a flag let go there

    class Tag;   // a flag on the kit: POWER HAND or OPENING HIT
    friend class Tag;
    ParagonX1AudioProcessor& processor;
    KitView kit;
    std::vector<int> blocks, chosen;
    std::vector<Item> items;
    struct Clip { int piece; std::vector<song::Hit> notes; double length; };
    std::vector<Clip> clipboard;
    bool dragging = false, solo = false;

    juce::TextButton playStyleTab { "EDIT PLAY STYLE" }, gridTab { "GRID EDITOR" }, closeButton { "X" };
    juce::TextButton selectButton { "SELECT INSTRUMENT" }, amountReset { "v" }, velocityReset { "v" },
                     soloButton { "SOLO SELECTED" }, openingButton { "OPENING HIT: OFF" };
    juce::Slider amount { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 velocity { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label title, chosenLabel, amountLabel { {}, "AMOUNT" }, velocityLabel { {}, "VELOCITY" }, totalLabel, velocityValue, hint, learnedLabel;
    std::unique_ptr<Tag> powerTag, openingTag;
    juce::Rectangle<int> kitArea, chipsArea, side;
    std::vector<int> chipPieces;   // the instruments with no piece in the picture, in the chips' order
};
