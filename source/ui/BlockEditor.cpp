#include "BlockEditor.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int headerHeight = 40, footerHeight = 44, namesWidth = 170;
    const char* const drumNames[] = { "Kick", "Rim", "Snare", "Clap", "Snare 2", "Low Tom", "Closed Hat", "Mid Tom", "Pedal Hat",
                                      "High Tom", "Open Hat", "Crash", "Ride", "Ride Bell", "Crash 2", "China", "Splash",
                                      "Half Hat", "Tambourine", "Shaker", "Ride Edge", "Hat Open 1", "Hat Open 3",
                                      "Tom 4", "Hat Shoulder", "Crash Choke", "Crash 2 Choke", "China Choke", "Splash Choke",
                                      "Crash 3", "Crash 4", "Crash 5", "Crash 6", "FX Crash", "China 2", "Tom 5" };
    static_assert (std::size (drumNames) == synth::drums::numSlots);

    juce::String drumName (int slot)
    {
        return juce::String (synth::drums::slotDefaults[(size_t) slot].key) + "  " + drumNames[slot];
    }

    int countOf (unsigned bits) noexcept
    {
        auto n = 0;
        for (; bits != 0; bits &= bits - 1)
            ++n;
        return n;
    }
}

BlockEditor::BlockEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    close.setTooltip ("Close the block editor (the song plays on).");
    close.onClick = [this]
    {
        setVisible (false);
        if (onClose != nullptr)
            onClose();
    };
    gridChoice.addItemList (grooves::quantizeNames, 1);
    gridChoice.setSelectedItemIndex (2, juce::dontSendNotification);   // 1/16
    gridChoice.setTooltip ("Where a click puts a note, and where QUANTIZE moves them to.");
    gridChoice.onChange = [this] { repaint(); };
    quantizeButton.setTooltip ("The chosen drums' notes (all, when none is chosen) moved onto the GRID.");
    quantizeButton.onClick = [this] { quantizeChosen(); };
    velocity.setRange (25.0, 200.0, 1.0);
    velocity.setValue (100.0, juce::dontSendNotification);
    velocity.setTextValueSuffix ("%");
    velocity.setDoubleClickReturnValue (true, 100.0);
    velocity.setTooltip ("The chosen drums (all, when none is chosen) louder or softer, the balance between their hits kept: "
                         "let go and it's done (and back to 100% for the next).");
    velocity.onDragEnd = [this] { applyVelocity ((float) velocity.getValue() / 100.0f); };
    removeButton.setTooltip ("The chosen drums' notes taken out of the block.");
    removeButton.onClick = [this] { removeChosen(); };
    copyButton.setTooltip ("The chosen drums' notes (all, when none is chosen) to the song's clipboard.");
    copyButton.onClick = [this] { copyChosen(); };
    pasteButton.setTooltip ("The clipboard's notes onto the same drums here (replacing theirs); one copied drum with one chosen "
                            "here: onto the chosen drum.");
    pasteButton.onClick = [this] { pasteChosen(); };
    for (int k = 0; k < synth::drums::numSlots; ++k)
        moveTo.addItem (drumName (k), k + 1);
    moveTo.setTextWhenNothingSelected ("drum...");
    moveTo.setTooltip ("The chosen drum's notes played by another drum instead (with that drum's own).");
    moveTo.onChange = [this]
    {
        if (const auto id = moveTo.getSelectedId(); id > 0)
        {
            moveChosenTo (id - 1);
            moveTo.setSelectedId (0, juce::dontSendNotification);
        }
    };
    addDrum.setTextWhenNothingSelected ("ADD DRUM");
    addDrum.setTooltip ("A row for a drum the block doesn't play yet, to click notes onto.");
    addDrum.onChange = [this]
    {
        if (const auto id = addDrum.getSelectedId(); id > 0)
            addRow (id - 1);
    };
    for (auto* l : { &gridLabel, &velocityLabel, &moveLabel })
    {
        l->setFont (juce::FontOptions (10.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, ParagonColours::textDim);
    hint.setText ("Click: a note.  Drag a note up / down: louder / softer; sideways: moves it (on GRID, ALT: free).  Right-click: remove it.  Click a drum's name to "
                  "choose it (CTRL: more); the buttons below work on the chosen drums, or all.",
                  juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &close, &quantizeButton, &removeButton, &copyButton, &pasteButton, &gridChoice,
                                                             &moveTo, &addDrum, &velocity, &gridLabel, &velocityLabel, &moveLabel, &hint })
        addAndMakeVisible (c);
    processor.songBroadcaster.addChangeListener (this);
}

BlockEditor::~BlockEditor()
{
    processor.songBroadcaster.removeChangeListener (this);
}

//==============================================================================
void BlockEditor::open (int b)
{
    blockIndex = b;
    chosenDrums = extraRows = 0;
    refresh();
    setVisible (true);
    toFront (false);
}

const song::Block* BlockEditor::block() const noexcept
{
    const auto& s = processor.getSong();
    return blockIndex >= 0 && blockIndex < (int) s.blocks.size() ? &s.blocks[(size_t) blockIndex] : nullptr;
}

void BlockEditor::edit (const std::function<void (song::Block&)>& f)
{
    if (block() == nullptr)
        return;
    auto s = processor.getSong();
    f (s.blocks[(size_t) blockIndex]);
    processor.setSong (std::move (s));
    refresh();   // at once (the song's change message comes later)
}

song::SlotMask BlockEditor::targetDrums() const noexcept
{
    return chosenDrums != 0 ? chosenDrums : song::allSlots;
}

void BlockEditor::refresh()
{
    // The rows: the drums the block plays, and any added, in the kit's order.
    rows.clear();
    const auto* b = block();
    const auto shown = (b != nullptr ? song::slotsIn (*b) : song::SlotMask (0)) | extraRows;
    for (int k = 0; k < synth::drums::numSlots; ++k)
        if ((shown & song::slotBit (k)) != 0)
            rows.push_back (k);
    chosenDrums &= shown;
    addDrum.clear (juce::dontSendNotification);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        if ((shown & song::slotBit (k)) == 0)
            addDrum.addItem (drumName (k), k + 1);
    moveTo.setEnabled (countOf (chosenDrums) == 1);
    moveLabel.setEnabled (moveTo.isEnabled());
    pasteButton.setEnabled (processor.getSongClipboard().has_value());
    resized();
    repaint();
}

std::vector<int> BlockEditor::getRows() const
{
    return rows;
}

void BlockEditor::chooseDrum (int slot, bool add)
{
    const auto bit = song::slotBit (slot);
    chosenDrums = add ? chosenDrums ^ bit : (chosenDrums == bit ? 0u : bit);
    refresh();
}

void BlockEditor::addRow (int slot)
{
    extraRows |= song::slotBit (slot);
    chosenDrums = song::slotBit (slot);
    refresh();
}

void BlockEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Undo, or an edit on the strip: the block may be gone (or another one at its place: shown as it is).
    if (block() == nullptr && isVisible())
    {
        setVisible (false);
        if (onClose != nullptr)
            onClose();
        return;
    }
    refresh();
}

//==============================================================================
void BlockEditor::applyVelocity (float factor)
{
    velocity.setValue (100.0, juce::dontSendNotification);
    if (std::abs (factor - 1.0f) > 1.0e-4f)
        edit ([this, factor] (song::Block& b) { song::scaleVelocity (b, factor, targetDrums()); });
}

void BlockEditor::quantizeChosen()
{
    edit ([this] (song::Block& b) { song::quantize (b, gridBeats(), targetDrums()); });
}

void BlockEditor::removeChosen()
{
    edit ([this] (song::Block& b) { song::removeDrums (b, targetDrums()); });
}

void BlockEditor::copyChosen()
{
    if (const auto* b = block())
    {
        auto clip = *b;
        song::removeDrums (clip, song::allSlots & ~targetDrums());
        processor.setSongClipboard (clip);
        refresh();
    }
}

void BlockEditor::pasteChosen()
{
    const auto& clipboard = processor.getSongClipboard();
    if (! clipboard.has_value())
        return;
    auto clip = *clipboard;
    const auto copied = song::slotsIn (clip);
    // One drum copied, one chosen: onto the chosen one (the hats' pattern on the ride, say).
    if (countOf (copied) == 1 && countOf (chosenDrums) == 1)
    {
        for (auto& h : clip.hits)
            h.slot = song::highestSlot (chosenDrums);
        edit ([&clip, this] (song::Block& b) { song::pasteDrums (b, clip, chosenDrums); });
        return;
    }
    edit ([&clip, copied, this] (song::Block& b) { song::pasteDrums (b, clip, copied & targetDrums()); });
}

void BlockEditor::moveChosenTo (int slot)
{
    if (countOf (chosenDrums) != 1)
        return;
    const auto from = song::highestSlot (chosenDrums);
    edit ([from, slot] (song::Block& b) { song::moveDrum (b, from, slot); });
    chosenDrums = song::slotBit (slot);
    refresh();
}

//==============================================================================
void BlockEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto top = area.removeFromTop (headerHeight);
    close.setBounds (top.removeFromRight (90).reduced (0, 6));
    top.removeFromRight (12);
    addDrum.setBounds (top.removeFromRight (160).reduced (0, 7));
    auto bottom = area.removeFromBottom (footerHeight).reduced (0, 8);
    gridLabel.setBounds (bottom.removeFromLeft (40));
    gridChoice.setBounds (bottom.removeFromLeft (110).reduced (4, 0));
    quantizeButton.setBounds (bottom.removeFromLeft (100).reduced (4, 0));
    bottom.removeFromLeft (12);
    velocityLabel.setBounds (bottom.removeFromLeft (64));
    velocity.setBounds (bottom.removeFromLeft (220).reduced (4, 0));
    bottom.removeFromLeft (12);
    removeButton.setBounds (bottom.removeFromLeft (90).reduced (4, 0));
    copyButton.setBounds (bottom.removeFromLeft (80).reduced (4, 0));
    pasteButton.setBounds (bottom.removeFromLeft (80).reduced (4, 0));
    bottom.removeFromLeft (12);
    moveLabel.setBounds (bottom.removeFromLeft (64));
    moveTo.setBounds (bottom.removeFromLeft (170).reduced (4, 0));
    hint.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);
    ruler = area.removeFromTop (18).withTrimmedLeft (namesWidth);
    // The rows as tall as fit, 20 to 72 pixels.
    const auto rowHeight = juce::jlimit (20, 72, area.getHeight() / juce::jmax (1, (int) rows.size()));
    area = area.withHeight (juce::jmax (rowHeight, rowHeight * (int) rows.size()));
    names = area.removeFromLeft (namesWidth);
    grid = area;
}

double BlockEditor::beatAt (float x) const noexcept
{
    const auto* b = block();
    return b == nullptr ? 0.0 : (double) (x - (float) grid.getX()) / juce::jmax (1, grid.getWidth()) * b->loopLength;
}

int BlockEditor::rowAt (float y) const noexcept
{
    if (rows.empty() || y < (float) grid.getY() || y >= (float) grid.getBottom())
        return -1;
    const auto row = (int) ((y - (float) grid.getY()) / ((float) grid.getHeight() / (float) rows.size()));
    return row >= 0 && row < (int) rows.size() ? rows[(size_t) row] : -1;
}

juce::Rectangle<float> BlockEditor::cellOf (int slot, double beat) const
{
    const auto* b = block();
    const auto it = std::find (rows.begin(), rows.end(), slot);
    if (b == nullptr || it == rows.end())
        return {};
    const auto rowHeight = (float) grid.getHeight() / (float) rows.size();
    const auto pxPerBeat = (float) grid.getWidth() / (float) b->loopLength;
    const auto x = (float) grid.getX() + (float) beat * pxPerBeat;
    const auto width = juce::jlimit (4.0f, 14.0f, (float) gridBeats() * pxPerBeat * 0.6f);
    return { x, (float) grid.getY() + rowHeight * (float) std::distance (rows.begin(), it) + 2.0f, width, rowHeight - 4.0f };
}

void BlockEditor::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    const auto* b = block();
    if (b == nullptr)
        return;
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const auto bar = processor.getSong().beatsPerBarAt (b->start);
    const auto bars = b->loopLength / bar;
    const auto times = b->length / b->loopLength;
    auto title = "EDIT BLOCK   " + b->name + "   " + juce::String (bars, std::abs (bars - std::round (bars)) < 1.0e-6 ? 0 : 2)
               + (std::abs (bars - 1.0) < 1.0e-6 ? " bar" : " bars");
    if (times > 1.0 + 1.0e-6)
        title << ", played " << juce::String (times, std::abs (times - std::round (times)) < 1.0e-6 ? 0 : 2) << " times in the song";
    g.drawText (title, 12, 8, 900, headerHeight, juce::Justification::centredLeft, true);
    g.setColour (song::colourOf (b->part));
    g.fillRect (12, 8 + headerHeight - 4, 220, 2);

    if (rows.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.drawText ("No notes in this block: ADD DRUM, then click notes in.", grid.withX (12).withWidth (getWidth() - 24),
                    juce::Justification::centred, false);
        return;
    }
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRect (grid);
    const auto rowHeight = (float) grid.getHeight() / (float) rows.size();
    const auto pxPerBeat = (float) grid.getWidth() / (float) b->loopLength;

    // The drums' names (the chosen lit), their notes counted; the rows' stripes.
    std::array<int, synth::drums::numSlots> counts {};
    for (const auto& h : b->hits)
        if (h.slot >= 0 && h.slot < synth::drums::numSlots)
            ++counts[(size_t) h.slot];
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const auto slot = rows[r];
        const auto y = (float) grid.getY() + rowHeight * (float) r;
        const auto chosen = (chosenDrums & song::slotBit (slot)) != 0;
        if (chosen)
        {
            g.setColour (ParagonColours::accent.withAlpha (0.12f));
            g.fillRect ((float) names.getX(), y, (float) (grid.getRight() - names.getX()), rowHeight);
        }
        else if (r % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRect ((float) grid.getX(), y, (float) grid.getWidth(), rowHeight);
        }
        g.setColour (chosen ? ParagonColours::accent : ParagonColours::text);
        g.setFont (juce::FontOptions (12.0f, chosen ? juce::Font::bold : juce::Font::plain));
        g.drawText (drumName (slot), names.getX() + 4, (int) y, names.getWidth() - 40, (int) rowHeight, juce::Justification::centredLeft, true);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (counts[(size_t) slot]), names.getRight() - 38, (int) y, 30, (int) rowHeight, juce::Justification::centredRight, false);
    }

    // The grid's lines: GRID faint, beats stronger, bars strongest.
    const auto step = gridBeats();
    for (int i = 0; i * step < b->loopLength - 1.0e-9; ++i)
    {
        const auto beat = i * step;
        const auto onBar = std::abs (std::remainder (beat, bar)) < 1.0e-6;
        const auto onBeat = std::abs (std::remainder (beat, 1.0)) < 1.0e-6;
        g.setColour (ParagonColours::panelEdge.withAlpha (onBar ? 1.0f : onBeat ? 0.7f : 0.3f));
        g.fillRect ((float) grid.getX() + (float) beat * pxPerBeat, (float) grid.getY(), onBar ? 1.5f : 1.0f, (float) grid.getHeight());
    }
    // The notes: as tall as they're loud, in the drum group's colour (the one being dragged, as dragged).
    for (const auto& h : b->hits)
    {
        const auto dragged = h.slot == dragSlot && std::abs (h.beat - dragBeat) < 1.0e-6;
        const auto cell = cellOf (h.slot, dragged && dragWay == DragWay::move ? dragTo : h.beat);   // (0.45.0: where it's going)
        if (cell.isEmpty())
            continue;
        const auto v = dragged ? dragVelocity : h.velocity;
        g.setColour (ParagonColours::accent.withAlpha (0.25f));
        g.fillRect (cell);
        g.setColour (dragged ? ParagonColours::glow : ParagonColours::accent);
        g.fillRect (cell.withTop (cell.getBottom() - cell.getHeight() * v));
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (grid);
    // The bars' numbers above the grid (the block's own: 1 is its first bar).
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    for (int n = 0; n * bar < b->loopLength - 1.0e-9; ++n)
        g.drawText (juce::String (n + 1), grid.getX() + juce::roundToInt (n * bar * pxPerBeat) + 3, ruler.getY(), 30, ruler.getHeight(),
                    juce::Justification::centredLeft, false);
}

//==============================================================================
void BlockEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto* b = block();
    if (b == nullptr)
        return;
    if (names.contains (e.getPosition()))
    {
        if (const auto slot = rowAt (e.position.y); slot >= 0)
            chooseDrum (slot, e.mods.isCommandDown() || e.mods.isShiftDown());
        return;
    }
    if (! grid.contains (e.getPosition()))
        return;
    const auto slot = rowAt (e.position.y);
    if (slot < 0)
        return;
    const auto beat = beatAt (e.position.x);
    // The note under the mouse: the one drawn there (a few pixels' grace round it).
    const auto under = std::find_if (b->hits.begin(), b->hits.end(), [this, slot, &e] (const song::Hit& h)
    {
        return h.slot == slot && cellOf (h.slot, h.beat).expanded (3.0f, 0.0f).contains (e.position);
    });
    if (e.mods.isPopupMenu())
    {
        if (under != b->hits.end())
        {
            const auto at = under->beat;
            edit ([slot, at] (song::Block& block)
            {
                std::erase_if (block.hits, [slot, at] (const song::Hit& h) { return h.slot == slot && std::abs (h.beat - at) < 1.0e-9; });
            });
        }
        return;
    }
    // On a note: drag it louder or softer. Else a new one, on the GRID.
    if (under != b->hits.end())
    {
        dragSlot = slot;
        dragBeat = dragTo = under->beat;
        dragFrom = dragVelocity = under->velocity;
        dragWay = DragWay::undecided;
        return;
    }
    auto at = std::round (beat / gridBeats()) * gridBeats();
    if (at >= b->loopLength - 1.0e-9)
        at = std::floor (beat / gridBeats()) * gridBeats();
    edit ([slot, at] (song::Block& block) { song::addHit (block, { at, slot, 0.8f }); });
}

void BlockEditor::moveNote (int slot, double fromBeat, double toBeat)
{
    edit ([slot, fromBeat, toBeat] (song::Block& block) { song::moveHit (block, slot, fromBeat, toBeat); });
}

void BlockEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlot < 0)
        return;
    // 0.45.0: the first few pixels decide: up / down sets its velocity, sideways moves it along the beat.
    if (dragWay == DragWay::undecided)
    {
        const auto dx = std::abs (e.getDistanceFromDragStartX()), dy = std::abs (e.getDistanceFromDragStartY());
        if (juce::jmax (dx, dy) < 4)
            return;
        dragWay = dx > dy ? DragWay::move : DragWay::velocity;
    }
    if (dragWay == DragWay::move)
    {
        const auto* b = block();
        if (b == nullptr)
            return;
        const auto pxPerBeat = (double) grid.getWidth() / juce::jmax (1.0e-6, b->loopLength);
        auto to = dragBeat + (double) e.getDistanceFromDragStartX() / pxPerBeat;
        if (! e.mods.isAltDown())
            to = std::round (to / gridBeats()) * gridBeats();   // on GRID (ALT: free)
        dragTo = juce::jlimit (0.0, b->loopLength - 1.0e-6, to);
        if (dragTo > b->loopLength - 1.0e-6 - 1.0e-9 && ! e.mods.isAltDown())
            dragTo = std::floor ((b->loopLength - 1.0e-6) / gridBeats()) * gridBeats();
    }
    else
        dragVelocity = juce::jlimit (1.0f / 127.0f, 1.0f, dragFrom - (float) e.getDistanceFromDragStartY() / 120.0f);
    repaint();
}

void BlockEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragSlot < 0)
        return;
    const auto slot = dragSlot;
    const auto at = dragBeat;
    const auto v = dragVelocity;
    const auto way = dragWay;
    dragSlot = -1;
    dragWay = DragWay::undecided;
    if (way == DragWay::move)
    {
        if (std::abs (dragTo - at) > 1.0e-9)
            moveNote (slot, at, dragTo);
        repaint();
        return;
    }
    if (std::abs (v - dragFrom) > 1.0e-4f)
        edit ([slot, at, v] (song::Block& block)
        {
            for (auto& h : block.hits)
                if (h.slot == slot && std::abs (h.beat - at) < 1.0e-6)
                    h.velocity = v;
        });
    repaint();
}
