#include "BlockEditor.h"

#include "song/PlayStyle.h"
#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    using synth::drums::pieces;
    constexpr int headerHeight = 34, toolbarHeight = 32, namesWidth = 214, laneHeight = 112, rulerHeight = 18,
                  scrollSize = 10, hintHeight = 18;
    constexpr std::array<double, 8> resolutionBeatsOf { 0.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    constexpr const char* sliderNames[] = { "QUANTIZE", "SWING", "RANDOMIZE", "VELOCITY", "DYNAMICS", "RANDOM VEL", "SLOPE" };

    juce::String pieceTitle (int piece)
    {
        return piece >= 0 && piece < synth::drums::numPieces ? juce::String (pieces[(size_t) piece].name) : juce::String();
    }

    int indexIn (int piece, int slot)
    {
        for (int a = 0; a < pieces[(size_t) piece].count; ++a)
            if (pieces[(size_t) piece].articulations[(size_t) a].slot == slot)
                return a;
        return -1;
    }

    // A fixed "random" number in -1..1 for a note (the same every time, so a slider at the same place gives the same notes).
    double scatter (const song::Hit& h, int salt) noexcept
    {
        auto x = (std::uint32_t) (h.slot * 7919 + salt * 104729) ^ (std::uint32_t) std::llround (h.beat * 1920.0) * 2654435761u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        return (double) (x % 20001u) / 10000.0 - 1.0;
    }

    bool sameBeat (double a, double b) noexcept { return std::abs (a - b) < 1.0e-6; }
}

const juce::StringArray BlockEditor::resolutionNames { "Auto", "1/4", "1/8", "1/16", "1/32", "1/8 Triplet", "1/16 Triplet", "1/32 Triplet" };

BlockEditor::BlockEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    for (int k = 0; k < synth::drums::numPieces; ++k)
        order.push_back (k);

    playStyleTab.setTooltip ("The same block in EDIT PLAY STYLE: instrument by instrument on the kit.");
    playStyleTab.onClick = [this]
    {
        if (onPlayStyle != nullptr && block() != nullptr)
        {
            const auto b = blockIndex;
            setVisible (false);
            onPlayStyle (b);
        }
    };
    gridTab.setToggleState (true, juce::dontSendNotification);
    closeButton.setTooltip ("Close the GRID EDITOR (the song plays on).");
    closeButton.onClick = [this]
    {
        setVisible (false);
        processor.setSongHeard (std::nullopt);
        if (onClose != nullptr)
            onClose();
    };
    pointerButton.setTooltip ("POINTER: click notes to choose them (SHIFT: more), drag empty space for a lasso, drag chosen notes "
                              "to move them (ALT: copy). CTRL + ALT: the pencil while held.");
    pencilButton.setTooltip ("PENCIL: click or drag along a row to draw notes on the resolution; in the velocity lane, draw velocities.");
    for (auto* b : { &pointerButton, &pencilButton })
        b->setRadioGroupId (701);
    pointerButton.setClickingTogglesState (true);
    pencilButton.setClickingTogglesState (true);
    pointerButton.setToggleState (true, juce::dontSendNotification);
    pointerButton.onClick = [this] { setTool (Tool::pointer); };
    pencilButton.onClick = [this] { setTool (Tool::pencil); };
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setTooltip ("SNAP: moved and drawn notes go onto the RESOLUTION's grid.");
    snapButton.onClick = [this] { snapOn = snapButton.getToggleState(); };
    resolutionChoice.addItemList (resolutionNames, 1);
    resolutionChoice.setSelectedItemIndex (0, juce::dontSendNotification);
    resolutionChoice.setTooltip ("RESOLUTION for SNAP, QUANTIZE, SWING, RANDOMIZE and NUDGE. AUTO: the finest grid line drawn at this zoom.");
    resolutionChoice.onChange = [this] { resolution = resolutionChoice.getSelectedItemIndex(); repaint(); };

    for (int k = 0; k < (int) sliders.size(); ++k)
    {
        auto& s = sliders[(size_t) k];
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        const auto bipolar = k == (int) Slider::swing || k == (int) Slider::velocity || k == (int) Slider::dynamics || k == (int) Slider::slope;
        s.setRange (bipolar ? -100.0 : 0.0, 100.0, 1.0);
        s.setValue (0.0, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.0);
        s.onDragStart = [this] { sliderDragging = true; };
        s.onDragEnd = [this] { sliderDragging = false; applySliders (true); };
        s.onValueChange = [this, k]
        {
            sliderValues[(size_t) k] = sliders[(size_t) k].getValue();
            applySliders (! sliderDragging);
        };
        auto& l = sliderLabels[(size_t) k];
        l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, ParagonColours::textDim);
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    }
    sliders[(size_t) Slider::quantize].setTooltip ("QUANTIZE: the chosen notes (all, when none) that far towards the RESOLUTION's grid.");
    sliders[(size_t) Slider::swing].setTooltip ("SWING: every second RESOLUTION step later (right) or earlier (left); 100: a full triplet swing.");
    sliders[(size_t) Slider::randomize].setTooltip ("RANDOMIZE: the chosen notes' timing loosened, up to a third of the RESOLUTION.");
    sliders[(size_t) Slider::velocity].setTooltip ("VELOCITY: the chosen notes softer (to half) or louder (to twice), their balance kept.");
    sliders[(size_t) Slider::dynamics].setTooltip ("DYNAMICS: the chosen notes' loud and soft further apart (right) or closer (left).");
    sliders[(size_t) Slider::randomVelocity].setTooltip ("RANDOM VEL: the chosen notes' velocities varied, for a looser, more human feel.");
    sliders[(size_t) Slider::slope].setTooltip ("SLOPE: a crescendo (right) or decrescendo (left) across the chosen notes.");
    syncSliderLabels();
    maxButton.setTooltip ("QUANTIZE all the way: 100%.");
    maxButton.onClick = [this] { setSliderValue (Slider::quantize, 100.0); };
    nudgeLeft.setTooltip ("NUDGE the chosen notes earlier by the RESOLUTION (ALT + LEFT).");
    nudgeRight.setTooltip ("NUDGE the chosen notes later by the RESOLUTION (ALT + RIGHT).");
    nudgeLeft.onClick = [this] { nudge (-1); };
    nudgeRight.onClick = [this] { nudge (1); };
    selectButton.onClick = [this] { selectMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&selectButton)); };
    optionsButton.onClick = [this] { optionsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton)); };
    rowsButton.onClick = [this] { rowsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rowsButton)); };
    soloMuteButton.onClick = [this]
    {
        if (hasSoloMute())
            clearSoloMute();
        else
            recallSoloMute();
    };
    autoScrollButton.setClickingTogglesState (true);
    autoScrollButton.setToggleState (true, juce::dontSendNotification);
    autoScrollButton.setTooltip ("AUTO SCROLL: the notes follow the song as it plays.");
    autoScrollButton.onClick = [this] { autoScroll = autoScrollButton.getToggleState(); };
    zoom.setRange (0.0, 1.0);
    zoom.setValue (0.0, juce::dontSendNotification);
    zoom.setTooltip ("ZOOM (ALT + the wheel).");
    zoom.onValueChange = [this]
    {
        if (const auto* b = block(); b != nullptr && grid.getWidth() > 0)
        {
            const auto fit = grid.getWidth() / juce::jmax (0.25, b->loopLength);
            const auto centre = beatAt ((float) grid.getCentreX());
            pxPerBeat = fit * std::pow (32.0, zoom.getValue());
            setViewStart (centre - grid.getWidth() / 2.0 / pxPerBeat);
        }
    };
    for (auto* l : { &zoomLabel, &nudgeLabel })
    {
        l->setFont (juce::FontOptions (10.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, ParagonColours::textDim);
    hint.setText ("Click a note to choose it (SHIFT: more), drag for a lasso, drag notes to move (ALT: copy); right-click a note: gone. "
                  "PENCIL (or CTRL + ALT): draw. DELETE, ALT + arrows: nudge, arrows: next / previous, SHIFT + I: invert, CTRL + A / C / V.",
                  juce::dontSendNotification);
    for (auto* bar : { &hScroll, &vScroll })
    {
        bar->setAutoHide (true);
        bar->setColour (juce::ScrollBar::thumbColourId, ParagonColours::accent.withAlpha (0.35f));
        bar->setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    }
    hScroll.addListener (this);
    vScroll.addListener (this);

    for (auto* c : std::initializer_list<juce::Component*> { &playStyleTab, &gridTab, &closeButton, &pointerButton, &pencilButton, &snapButton,
                                                             &resolutionChoice, &maxButton, &nudgeLeft, &nudgeRight, &selectButton,
                                                             &optionsButton, &rowsButton, &autoScrollButton, &zoom, &zoomLabel,
                                                             &nudgeLabel, &hint, &hScroll, &vScroll })
        addAndMakeVisible (c);
    addChildComponent (soloMuteButton);
    processor.songBroadcaster.addChangeListener (this);
}

BlockEditor::~BlockEditor()
{
    processor.songBroadcaster.removeChangeListener (this);
}

//==============================================================================
void BlockEditor::open (int b)
{
    const auto fresh = b != blockIndex;
    blockIndex = b;
    selection.clear();
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    if (fresh)
    {
        pxPerBeat = 0.0;
        viewBeat = 0.0;
        rowScroll = 0;
    }
    refresh();
    setVisible (true);
    toFront (true);
    startTimerHz (30);
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
    if (! (s == processor.getSong()))
        processor.setSong (std::move (s));
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    refresh();   // at once (the song's change message comes later)
}

void BlockEditor::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll)
        viewBeat = start;
    else
        rowScroll = juce::roundToInt (start);
    repaint();
}

void BlockEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The sliders' starting notes are only good while the block is as they left it (not after an undo, say).
    if (baseBlock.has_value() && ! sliderDragging)
        if (const auto* b = block(); b == nullptr || ! (*b == (lastMade.has_value() ? *lastMade : *baseBlock)))
        {
            baseBlock.reset();
            lastMade.reset();
            sliderValues = {};
        }
    // Undo, or an edit on the strip: the block may be gone (or another one at its place: shown as it is).
    if (block() == nullptr && isVisible())
    {
        setVisible (false);
        processor.setSongHeard (std::nullopt);
        if (onClose != nullptr)
            onClose();
        return;
    }
    refresh();
}

void BlockEditor::refresh()
{
    // The chosen notes that are still there.
    if (const auto* b = block())
        std::erase_if (selection, [b] (const Key& k)
        {
            return std::none_of (b->hits.begin(), b->hits.end(), [&k] (const song::Hit& h) { return h.slot == k.slot && sameBeat (h.beat, k.beat); });
        });
    layoutRows();
    syncSliderLabels();
    soloMuteButton.setVisible (hasSoloMute() || std::any_of (recallMuted.begin(), recallMuted.end(), [] (bool m) { return m; })
                               || std::any_of (recallSoloed.begin(), recallSoloed.end(), [] (bool m) { return m; }));
    soloMuteButton.setButtonText (hasSoloMute() ? "CLEAR SOLO / MUTE" : "RECALL SOLO / MUTE");
    resized();
    repaint();
}

void BlockEditor::layoutRows()
{
    rows.clear();
    const auto* b = block();
    const auto played = b != nullptr ? song::slotsIn (*b) : song::SlotMask (0);
    const auto kit = processor.getKitSettings();
    const auto shownSlot = [&] (int slot)
    {
        return (played & song::slotBit (slot)) != 0 || showAll
               || (kit.slots[(size_t) slot].active && processor.homeNote (slot) >= 0);
    };
    for (const auto piece : order)
    {
        const auto& pc = pieces[(size_t) piece];
        auto any = false;
        for (int a = 0; a < pc.count; ++a)
            any = any || shownSlot (pc.articulations[(size_t) a].slot);
        if (! any)
            continue;
        if (pc.count > 1 && expanded[(size_t) piece])
        {
            rows.push_back ({ piece, -1, true });
            for (int a = 0; a < pc.count; ++a)
                if (const auto slot = pc.articulations[(size_t) a].slot; shownSlot (slot))
                    rows.push_back ({ piece, slot, false });
        }
        else
            rows.push_back ({ piece, -1, false });
    }
}

int BlockEditor::rowOf (int slot) const
{
    const auto piece = synth::drums::pieceOfSlot (slot);
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r].slot == slot)
            return (int) r;
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r].piece == piece && rows[r].slot < 0 && ! rows[r].heading)
            return (int) r;
    return -1;
}

song::SlotMask BlockEditor::slotsOfRow (int row) const
{
    if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].heading)
        return 0;
    const auto& r = rows[(size_t) row];
    return r.slot >= 0 ? song::slotBit (r.slot) : song::playstyle::slotsOfPiece (r.piece);
}

int BlockEditor::slotForRow (int row, int fromSlot) const
{
    if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].heading)
        return -1;
    const auto& r = rows[(size_t) row];
    if (r.slot >= 0)
        return r.slot;
    if (synth::drums::pieceOfSlot (fromSlot) == r.piece)
        return fromSlot;
    // Onto another instrument: the same articulation there (its place in the list), else its first.
    const auto from = synth::drums::pieceOfSlot (fromSlot);
    const auto index = from >= 0 ? indexIn (from, fromSlot) : 0;
    const auto& pc = pieces[(size_t) r.piece];
    return pc.articulations[(size_t) (index >= 0 && index < pc.count ? index : 0)].slot;
}

//==============================================================================
double BlockEditor::barBeats() const
{
    const auto* b = block();
    return b != nullptr ? processor.getSong().beatsPerBarAt (b->start) : 4.0;
}

double BlockEditor::resolutionBeats() const noexcept
{
    if (resolution > 0 && resolution < (int) resolutionBeatsOf.size())
        return resolutionBeatsOf[(size_t) resolution];
    // AUTO: the finest line the grid draws at this zoom.
    const std::array<double, 4> straight { 1.0, 0.5, 0.25, 0.125 };
    const std::array<double, 4> triplet { 1.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    auto best = 1.0;
    for (const auto d : triplets ? std::vector<double> (triplet.begin(), triplet.end()) : std::vector<double> (straight.begin(), straight.end()))
        if (d * pxPerBeat >= 12.0)
            best = d;
    return best;
}

double BlockEditor::snap (double beat) const
{
    if (! snapOn)
        return beat;
    const auto r = resolutionBeats();
    return std::round (beat / r) * r;
}

float BlockEditor::xOf (double beat) const noexcept
{
    return (float) grid.getX() + (float) ((beat - viewBeat) * pxPerBeat);
}

double BlockEditor::beatAt (float x) const noexcept
{
    return viewBeat + (double) (x - (float) grid.getX()) / juce::jmax (1.0e-6, pxPerBeat);
}

float BlockEditor::rowTop (int row) const noexcept
{
    return (float) grid.getY() + (float) (row - rowScroll) * rowHeight();
}

int BlockEditor::rowAt (float y) const noexcept
{
    if (y < (float) grid.getY() || y >= (float) grid.getBottom())
        return -1;
    const auto row = rowScroll + (int) ((y - (float) grid.getY()) / rowHeight());
    return row >= 0 && row < (int) rows.size() ? row : -1;
}

juce::Rectangle<float> BlockEditor::cellOf (int slot, double beat) const
{
    const auto row = rowOf (slot);
    if (row < 0)
        return {};
    const auto size = 12.0f;
    return juce::Rectangle<float> (size, size).withCentre ({ xOf (beat), rowTop (row) + rowHeight() * 0.5f });
}

juce::Rectangle<int> BlockEditor::rowNameArea (int row) const
{
    return { names.getX(), juce::roundToInt (rowTop (row)), names.getWidth(), (int) rowHeight() };
}

juce::Rectangle<float> BlockEditor::velocityPointOf (int slot, double beat) const
{
    const auto* b = block();
    if (b == nullptr)
        return {};
    for (const auto& h : b->hits)
        if (h.slot == slot && sameBeat (h.beat, beat))
        {
            const auto y = (float) lane.getBottom() - 3.0f - (float) (lane.getHeight() - 8) * h.velocity;
            return juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ xOf (beat), y });
        }
    return {};
}

bool BlockEditor::isSelected (const song::Hit& h) const
{
    return std::any_of (selection.begin(), selection.end(), [&h] (const Key& k) { return k.slot == h.slot && sameBeat (k.beat, h.beat); });
}

int BlockEditor::hitAt (juce::Point<float> at) const
{
    const auto* b = block();
    if (b == nullptr)
        return -1;
    auto best = -1;
    auto bestDistance = 1.0e9f;
    for (size_t i = 0; i < b->hits.size(); ++i)
    {
        const auto cell = cellOf (b->hits[i].slot, b->hits[i].beat);
        if (cell.isEmpty() || ! cell.expanded (3.0f).contains (at))
            continue;
        if (const auto d = cell.getCentre().getDistanceFrom (at); d < bestDistance)
        {
            best = (int) i;
            bestDistance = d;
        }
    }
    return best;
}

int BlockEditor::velocityHitAt (juce::Point<float> at) const
{
    const auto* b = block();
    if (b == nullptr)
        return -1;
    auto best = -1;
    auto bestScore = 1.0e9f;
    for (size_t i = 0; i < b->hits.size(); ++i)
    {
        const auto& h = b->hits[i];
        if (rowOf (h.slot) < 0)
            continue;
        const auto x = xOf (h.beat);
        if (std::abs (x - at.x) > 5.0f)
            continue;
        const auto score = velocityPointOf (h.slot, h.beat).getCentre().getDistanceFrom (at) - (isSelected (h) ? 50.0f : 0.0f);
        if (score < bestScore)
        {
            best = (int) i;
            bestScore = score;
        }
    }
    return best;
}

void BlockEditor::previewSlot (int slot)
{
    if (previewSounds && slot >= 0)
        processor.previewDrum (slot);
}

//==============================================================================
std::vector<song::Hit> BlockEditor::getSelection() const
{
    std::vector<song::Hit> out;
    if (const auto* b = block())
        for (const auto& h : b->hits)
            if (isSelected (h))
                out.push_back (h);
    return out;
}

void BlockEditor::select (int slot, double beat, bool add)
{
    if (! add)
        selection.clear();
    const auto it = std::find_if (selection.begin(), selection.end(), [&] (const Key& k) { return k.slot == slot && sameBeat (k.beat, beat); });
    if (it != selection.end())
    {
        if (add)
            selection.erase (it);
    }
    else
        selection.push_back ({ slot, beat });
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    syncSliderLabels();
    repaint();
}

void BlockEditor::selectRow (int row, bool add)
{
    if (! add)
        selection.clear();
    auto mask = slotsOfRow (row);
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].heading)
        mask = song::playstyle::slotsOfPiece (rows[(size_t) row].piece);
    if (const auto* b = block())
        for (const auto& h : b->hits)
            if ((mask & song::slotBit (h.slot)) != 0 && ! isSelected (h))
                selection.push_back ({ h.slot, h.beat });
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    syncSliderLabels();
    repaint();
}

void BlockEditor::selectAll()
{
    selection.clear();
    if (const auto* b = block())
        for (const auto& h : b->hits)
            if (rowOf (h.slot) >= 0)
                selection.push_back ({ h.slot, h.beat });
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    syncSliderLabels();
    repaint();
}

void BlockEditor::selectNone()
{
    selection.clear();
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    syncSliderLabels();
    repaint();
}

void BlockEditor::invertSelection()
{
    std::vector<Key> inverted;
    if (const auto* b = block())
        for (const auto& h : b->hits)
            if (rowOf (h.slot) >= 0 && ! isSelected (h))
                inverted.push_back ({ h.slot, h.beat });
    selection = std::move (inverted);
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    syncSliderLabels();
    repaint();
}

void BlockEditor::exclude (bool onBeats)
{
    // The steps the chosen notes all sit on (the longest: a bar's half, a beat, an eighth...): every other one of
    // those is "on" (1, 3...), the rest "off".
    auto notes = getSelection();
    if (notes.empty())
        return;
    auto step = 0.0;
    for (const auto d : { 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 })
        if (std::all_of (notes.begin(), notes.end(), [d] (const song::Hit& h) { return std::abs (h.beat / d - std::round (h.beat / d)) < 1.0e-4; }))
        {
            step = d;
            break;
        }
    if (step <= 0.0)
        return;
    std::erase_if (selection, [step, onBeats] (const Key& k)
    {
        const auto even = (llabs (std::llround (k.beat / step)) % 2) == 0;
        return onBeats ? even : ! even;
    });
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    repaint();
}

void BlockEditor::selectNoteValue (double beats)
{
    // From the chosen notes (all the shown ones, when none), only those on that note value.
    const auto from = selection.empty();
    std::vector<Key> kept;
    if (const auto* b = block())
        for (const auto& h : b->hits)
            if ((from ? rowOf (h.slot) >= 0 : isSelected (h)) && std::abs (h.beat / beats - std::round (h.beat / beats)) < 1.0e-4)
                kept.push_back ({ h.slot, h.beat });
    selection = std::move (kept);
    baseBlock.reset();
    lastMade.reset();
    sliderValues = {};
    repaint();
}

void BlockEditor::jump (int direction)
{
    const auto* b = block();
    if (b == nullptr)
        return;
    std::vector<const song::Hit*> shown;
    for (const auto& h : b->hits)
        if (rowOf (h.slot) >= 0)
            shown.push_back (&h);
    if (shown.empty())
        return;
    std::stable_sort (shown.begin(), shown.end(), [this] (const song::Hit* x, const song::Hit* y)
    {
        return x->beat != y->beat ? x->beat < y->beat : rowOf (x->slot) < rowOf (y->slot);
    });
    auto current = -1;
    for (size_t i = 0; i < shown.size(); ++i)
        if (isSelected (*shown[i]))
        {
            current = (int) i;
            if (direction < 0)
                break;
        }
    const auto next = current < 0 ? (direction > 0 ? 0 : (int) shown.size() - 1)
                                  : juce::jlimit (0, (int) shown.size() - 1, current + (direction > 0 ? 1 : -1));
    select (shown[(size_t) next]->slot, shown[(size_t) next]->beat, false);
    previewSlot (shown[(size_t) next]->slot);
    if (xOf (shown[(size_t) next]->beat) < (float) grid.getX() || xOf (shown[(size_t) next]->beat) > (float) grid.getRight())
        setViewStart (shown[(size_t) next]->beat - grid.getWidth() / 4.0 / pxPerBeat);
}

void BlockEditor::nudge (int direction)
{
    const auto* b = block();
    if (b == nullptr || selection.empty())
        return;
    const auto by = resolutionBeats() * direction;
    const auto loop = b->loopLength;
    std::vector<Key> moved;
    edit ([&] (song::Block& blk)
    {
        std::vector<song::Hit> kept, shifted;
        for (const auto& h : blk.hits)
            if (isSelected (h))
            {
                auto beat = std::fmod (h.beat + by, loop);
                if (beat < 0.0)
                    beat += loop;
                if (beat > loop - 1.0e-6)
                    beat = 0.0;
                shifted.push_back ({ beat, h.slot, h.velocity });
            }
            else
                kept.push_back (h);
        // Moved onto others: theirs replaced.
        for (const auto& s : shifted)
            std::erase_if (kept, [&s] (const song::Hit& h) { return h.slot == s.slot && sameBeat (h.beat, s.beat); });
        kept.insert (kept.end(), shifted.begin(), shifted.end());
        blk.hits = std::move (kept);
        song::tidyHits (blk);
        for (const auto& s : shifted)
            moved.push_back ({ s.slot, s.beat });
    });
    selection = std::move (moved);
    repaint();
}

void BlockEditor::deleteSelected()
{
    if (selection.empty())
        return;
    edit ([this] (song::Block& blk) { std::erase_if (blk.hits, [this] (const song::Hit& h) { return isSelected (h); }); });
    selection.clear();
}

void BlockEditor::copySelected()
{
    const auto* b = block();
    if (b == nullptr || selection.empty())
        return;
    song::Block clip = *b;
    clip.hits = getSelection();
    clipboard = clip;
}

void BlockEditor::paste()
{
    if (! clipboard.has_value() || block() == nullptr)
        return;
    const auto clip = *clipboard;
    std::vector<Key> pasted;
    edit ([&] (song::Block& blk)
    {
        for (const auto& h : clip.hits)
            if (h.beat < blk.loopLength - 1.0e-9)
            {
                song::addHit (blk, h);
                pasted.push_back ({ h.slot, h.beat });
            }
    });
    selection = std::move (pasted);
    repaint();
}

void BlockEditor::stretch (grooves::TempoFactor factor)
{
    const auto* b = block();
    if (b == nullptr)
        return;
    const auto notes = getSelection();
    if (notes.empty() || notes.size() == b->hits.size())
    {
        // The whole block: its length too (the blocks after it move along), as the strip's CHANGE TEMPO.
        auto s = processor.getSong();
        song::changeTempo (s.blocks[(size_t) blockIndex], factor);
        s.resize (blockIndex, s.blocks[(size_t) blockIndex].length);
        processor.setSong (std::move (s));
        selection.clear();
        pxPerBeat = 0.0;
        refresh();
        return;
    }
    // The chosen notes from the first of them, the rest left where they are (any pushed past the groove's end: gone).
    const auto scale = 1.0 / grooves::factorOf (factor);
    const auto from = notes.front().beat;
    std::vector<Key> moved;
    edit ([&] (song::Block& blk)
    {
        std::vector<song::Hit> kept, shifted;
        for (const auto& h : blk.hits)
            if (isSelected (h))
            {
                const auto beat = from + (h.beat - from) * scale;
                if (beat < blk.loopLength - 1.0e-9)
                    shifted.push_back ({ beat, h.slot, h.velocity });
            }
            else
                kept.push_back (h);
        kept.insert (kept.end(), shifted.begin(), shifted.end());
        blk.hits = std::move (kept);
        song::tidyHits (blk);
        for (const auto& s : shifted)
            moved.push_back ({ s.slot, s.beat });
    });
    selection = std::move (moved);
    repaint();
}

//==============================================================================
void BlockEditor::capture()
{
    const auto* b = block();
    if (b == nullptr)
        return;
    baseBlock = *b;
    baseNotes.clear();
    for (size_t i = 0; i < b->hits.size(); ++i)
        if (selection.empty() ? rowOf (b->hits[i].slot) >= 0 : isSelected (b->hits[i]))
            baseNotes.push_back (i);
}

void BlockEditor::applySliders (bool asUndoStep)
{
    if (! baseBlock.has_value())
        capture();
    if (! baseBlock.has_value() || blockIndex < 0)
        return;
    auto blk = *baseBlock;
    const auto r = resolutionBeats();
    const auto q = sliderValues[(size_t) Slider::quantize] / 100.0;
    const auto sw = sliderValues[(size_t) Slider::swing] / 100.0;
    const auto rt = sliderValues[(size_t) Slider::randomize] / 100.0;
    const auto vel = sliderValues[(size_t) Slider::velocity] / 100.0;
    const auto dyn = sliderValues[(size_t) Slider::dynamics] / 100.0;
    const auto rv = sliderValues[(size_t) Slider::randomVelocity] / 100.0;
    const auto sl = sliderValues[(size_t) Slider::slope] / 100.0;
    // The chosen notes' loudness on average, and where they start and end (for DYNAMICS and SLOPE).
    auto mean = 0.0, first = 1.0e9, last = -1.0e9;
    for (const auto i : baseNotes)
    {
        mean += blk.hits[i].velocity;
        first = juce::jmin (first, blk.hits[i].beat);
        last = juce::jmax (last, blk.hits[i].beat);
    }
    mean /= juce::jmax ((size_t) 1, baseNotes.size());
    std::vector<song::Hit> changed;
    for (const auto i : baseNotes)
    {
        const auto& h = blk.hits[i];
        // Timing: towards the grid, every second step swung (100: a third of the step later: the triplet swing of
        // two even steps), loosened at random.
        const auto onGrid = std::round (h.beat / r) * r;
        auto beat = h.beat + (onGrid - h.beat) * q;
        if ((llabs (std::llround (onGrid / r)) % 2) == 1)
            beat += sw * r / 3.0;
        beat += scatter (h, 1) * rt * r / 3.0;
        beat = std::fmod (beat, blk.loopLength);
        if (beat < 0.0)
            beat += blk.loopLength;
        if (beat > blk.loopLength - 1.0e-6)
            beat = 0.0;
        // Velocity: the spread around the average, the level, a random wobble, the slope across the notes.
        auto v = mean + (h.velocity - mean) * (1.0 + dyn);
        v *= std::pow (2.0, vel);
        v += scatter (h, 2) * rv * 0.25;
        if (last > first)
            v *= 1.0 + sl * ((h.beat - first) / (last - first) - 0.5) * 1.2;
        changed.push_back ({ beat, h.slot, (float) juce::jlimit (1.0 / 127.0, 1.0, v) });
    }
    std::vector<song::Hit> kept;
    for (size_t i = 0; i < blk.hits.size(); ++i)
        if (std::find (baseNotes.begin(), baseNotes.end(), i) == baseNotes.end())
            kept.push_back (blk.hits[i]);
    kept.insert (kept.end(), changed.begin(), changed.end());
    blk.hits = std::move (kept);
    song::tidyHits (blk);
    const auto hadSelection = ! selection.empty();
    selection.clear();
    if (hadSelection)
        for (const auto& c : changed)
            selection.push_back ({ c.slot, c.beat });
    auto s = processor.getSong();
    if (blockIndex >= (int) s.blocks.size())
        return;
    s.blocks[(size_t) blockIndex] = blk;
    lastMade = blk;
    if (asUndoStep)
    {
        if (! (s == processor.getSong()))
            processor.setSong (std::move (s));
    }
    else
        processor.previewSong (std::move (s));
    repaint();
}

void BlockEditor::setSliderValue (Slider which, double percent, bool asUndoStep)
{
    auto& s = sliders[(size_t) which];
    s.setValue (percent, juce::dontSendNotification);
    sliderValues[(size_t) which] = s.getValue();
    applySliders (asUndoStep);
}

double BlockEditor::getSliderValue (Slider which) const
{
    return sliderValues[(size_t) which];
}

void BlockEditor::syncSliderLabels()
{
    for (size_t k = 0; k < sliders.size(); ++k)
    {
        sliders[k].setValue (sliderValues[k], juce::dontSendNotification);
        const auto v = juce::roundToInt (sliderValues[k]);
        sliderLabels[k].setText (juce::String (sliderNames[k]) + " " + (v > 0 && k != (size_t) Slider::quantize && k != (size_t) Slider::randomize
                                                                            && k != (size_t) Slider::randomVelocity ? "+" : "")
                                     + juce::String (v) + "%", juce::dontSendNotification);
    }
}

//==============================================================================
void BlockEditor::setTool (Tool t)
{
    tool = t;
    pointerButton.setToggleState (t == Tool::pointer, juce::dontSendNotification);
    pencilButton.setToggleState (t == Tool::pencil, juce::dontSendNotification);
    setMouseCursor (t == Tool::pencil ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void BlockEditor::setSnap (bool on)
{
    snapOn = on;
    snapButton.setToggleState (on, juce::dontSendNotification);
}

void BlockEditor::setResolution (int index)
{
    resolutionChoice.setSelectedItemIndex (juce::jlimit (0, resolutionNames.size() - 1, index), juce::sendNotificationSync);
}

void BlockEditor::setTriplets (bool on)
{
    triplets = on;
    repaint();
}

void BlockEditor::setZoom (double pixelsPerBeat)
{
    const auto* b = block();
    if (b == nullptr || grid.getWidth() <= 0)
        return;
    const auto fit = grid.getWidth() / juce::jmax (0.25, b->loopLength);
    pxPerBeat = juce::jlimit (fit, fit * 32.0, pixelsPerBeat);
    zoom.setValue (std::log (pxPerBeat / fit) / std::log (32.0), juce::dontSendNotification);
    clampView();
    repaint();
}

void BlockEditor::setViewStart (double beat)
{
    viewBeat = beat;
    clampView();
    repaint();
}

void BlockEditor::clampView()
{
    const auto* b = block();
    if (b == nullptr || pxPerBeat <= 0.0)
        return;
    const auto shown = grid.getWidth() / pxPerBeat;
    viewBeat = juce::jlimit (0.0, juce::jmax (0.0, b->loopLength - shown), viewBeat);
    hScroll.setRangeLimits (0.0, b->loopLength, juce::dontSendNotification);
    hScroll.setCurrentRange (viewBeat, juce::jmin (shown, b->loopLength), juce::dontSendNotification);
    const auto visibleRows = juce::jmax (1, (int) (grid.getHeight() / rowHeight()));
    rowScroll = juce::jlimit (0, juce::jmax (0, (int) rows.size() - visibleRows), rowScroll);
    vScroll.setRangeLimits (0.0, (double) juce::jmax (1, (int) rows.size()), juce::dontSendNotification);
    vScroll.setCurrentRange ((double) rowScroll, (double) visibleRows, juce::dontSendNotification);
}

void BlockEditor::setAutoScroll (bool on)
{
    autoScroll = on;
    autoScrollButton.setToggleState (on, juce::dontSendNotification);
}

//==============================================================================
void BlockEditor::setExpanded (int piece, bool on)
{
    if (piece >= 0 && piece < synth::drums::numPieces)
        expanded[(size_t) piece] = on;
    refresh();
}

void BlockEditor::setAllExpanded (bool on)
{
    expanded.fill (on);
    refresh();
}

void BlockEditor::setShowAll (bool on)
{
    showAll = on;
    refresh();
}

void BlockEditor::moveRow (int piece, int toIndex)
{
    const auto it = std::find (order.begin(), order.end(), piece);
    if (it == order.end())
        return;
    order.erase (it);
    order.insert (order.begin() + juce::jlimit (0, (int) order.size(), toIndex), piece);
    refresh();
}

void BlockEditor::resetOrder()
{
    order.clear();
    for (int k = 0; k < synth::drums::numPieces; ++k)
        order.push_back (k);
    refresh();
}

void BlockEditor::setRowMute (int row, bool on)
{
    if (row >= 0 && row < (int) rows.size())
        muted[(size_t) rows[(size_t) row].piece] = on;
    updateHeard();
}

void BlockEditor::setRowSolo (int row, bool on)
{
    if (row >= 0 && row < (int) rows.size())
        soloed[(size_t) rows[(size_t) row].piece] = on;
    updateHeard();
}

bool BlockEditor::hasSoloMute() const noexcept
{
    return std::any_of (muted.begin(), muted.end(), [] (bool m) { return m; })
           || std::any_of (soloed.begin(), soloed.end(), [] (bool m) { return m; });
}

void BlockEditor::clearSoloMute()
{
    recallMuted = muted;
    recallSoloed = soloed;
    muted.fill (false);
    soloed.fill (false);
    updateHeard();
}

void BlockEditor::recallSoloMute()
{
    muted = recallMuted;
    soloed = recallSoloed;
    updateHeard();
}

void BlockEditor::updateHeard()
{
    // Soloed instruments only (any soloed), less the muted ones.
    if (! hasSoloMute())
        processor.setSongHeard (std::nullopt);
    else
    {
        const auto anySolo = std::any_of (soloed.begin(), soloed.end(), [] (bool s) { return s; });
        song::SlotMask heard = 0;
        for (int p = 0; p < synth::drums::numPieces; ++p)
            if ((! anySolo || soloed[(size_t) p]) && ! muted[(size_t) p])
                heard |= song::playstyle::slotsOfPiece (p);
        processor.setSongHeard (heard);
    }
    refresh();
}

//==============================================================================
juce::PopupMenu BlockEditor::selectMenu()
{
    juce::PopupMenu m;
    m.addItem ("Select All (Ctrl + A)", [this] { selectAll(); });
    m.addItem ("Select None", ! selection.empty(), false, [this] { selectNone(); });
    m.addItem ("Invert Selection (Shift + I)", [this] { invertSelection(); });
    juce::PopupMenu ex;
    ex.addItem ("On Beats", ! selection.empty(), false, [this] { exclude (true); });
    ex.addItem ("Off Beats", ! selection.empty(), false, [this] { exclude (false); });
    m.addSubMenu ("Exclude", ex);
    juce::PopupMenu values;
    for (const auto& [name, beats] : std::initializer_list<std::pair<const char*, double>> { { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 },
                                                                                            { "1/16", 0.25 }, { "1/32", 0.125 },
                                                                                            { "1/4 Triplet", 2.0 / 3.0 }, { "1/8 Triplet", 1.0 / 3.0 },
                                                                                            { "1/16 Triplet", 1.0 / 6.0 } })
        values.addItem (name, [this, b = beats] { selectNoteValue (b); });
    m.addSubMenu ("Note Values", values);
    juce::PopupMenu jumpTo;
    jumpTo.addItem ("Next Note (Right)", [this] { jump (1); });
    jumpTo.addItem ("Previous Note (Left)", [this] { jump (-1); });
    m.addSubMenu ("Jump To", jumpTo);
    return m;
}

juce::PopupMenu BlockEditor::optionsMenu()
{
    juce::PopupMenu m;
    juce::PopupMenu stretchMenu;
    for (const auto f : { grooves::TempoFactor::half, grooves::TempoFactor::twice, grooves::TempoFactor::twoThirds, grooves::TempoFactor::threeHalves })
        stretchMenu.addItem (grooves::tempoFactorNames[(int) f], [this, f] { stretch (f); });
    m.addSubMenu (selection.empty() ? "Stretch Notes (the whole block)" : "Stretch Notes (the chosen)", stretchMenu);
    m.addItem ("Preview Sounds", true, previewSounds, [this] { previewSounds = ! previewSounds; });
    return m;
}

juce::PopupMenu BlockEditor::rowsMenu()
{
    juce::PopupMenu m;
    m.addItem ("Show Unmapped / Turned-Off Drums", true, showAll, [this] { setShowAll (! showAll); });
    m.addItem ("Reset Order", [this] { resetOrder(); });
    m.addItem ("Collapse All", [this] { setAllExpanded (false); });
    m.addItem ("Expand All", [this] { setAllExpanded (true); });
    return m;
}

//==============================================================================
void BlockEditor::timerCallback()
{
    if (! isShowing())
        return;
    const auto* b = block();
    if (b == nullptr)
        return;
    // The playhead, where the song is in this block's groove (-1: not in it).
    const auto at = processor.getSongPosition();
    auto play = -1.0;
    if (at >= b->start && at < b->end() && b->loopLength > 0.0)
        play = std::fmod (at - b->start, b->loopLength);
    if (std::abs (play - shownPlayhead) > 1.0e-4)
    {
        const auto old = shownPlayhead;
        shownPlayhead = play;
        if (play >= 0.0 && autoScroll && pxPerBeat > 0.0)
        {
            const auto shown = grid.getWidth() / pxPerBeat;
            if (play < viewBeat || play > viewBeat + shown * 0.95)
            {
                setViewStart (play);
                return;
            }
        }
        for (const auto beat : { old, play })
            if (beat >= 0.0)
                repaint (juce::Rectangle<int> (juce::roundToInt (xOf (beat)) - 2, grid.getY() - rulerHeight, 5, lane.getBottom() - grid.getY() + rulerHeight));
    }
}

void BlockEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    const auto held = mods.isCtrlDown() && mods.isAltDown();
    if (held != pencilHeld)
    {
        pencilHeld = held;
        setMouseCursor (held || tool == Tool::pencil ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }
}

bool BlockEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const auto direction = code == juce::KeyPress::rightKey ? 1 : -1;
        if (mods.isAltDown())
            nudge (direction);
        else
            jump (direction);
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        selectNone();
        return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) code);
    if (mods.isShiftDown() && ! mods.isCommandDown() && c == 'i')
    {
        invertSelection();
        return true;
    }
    if (mods.isCommandDown())
    {
        if (c == 'a') { selectAll(); return true; }
        if (c == 'c') { copySelected(); return true; }
        if (c == 'x') { copySelected(); deleteSelected(); return true; }
        if (c == 'v') { paste(); return true; }
    }
    return false;
}

//==============================================================================
void BlockEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto* b = block();
    if (b == nullptr)
        return;
    grabKeyboardFocus();
    drag = Drag::none;
    dragStart = dragNow = e.position;
    const auto pencil = tool == Tool::pencil || (e.mods.isCtrlDown() && e.mods.isAltDown());

    // The time bar: right-click for straight or triplet lines.
    if (ruler.contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("Straight Grid Lines", true, ! triplets, [this] { setTriplets (false); });
            m.addItem ("Triplet Grid Lines", true, triplets, [this] { setTriplets (true); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
        return;
    }
    // The rows' names: the arrow folds an instrument out, M / S, else choose its notes (drag: reorder).
    if (names.contains (e.getPosition()))
    {
        const auto row = rowAt (e.position.y);
        if (row < 0)
            return;
        const auto area = rowNameArea (row);
        const auto& r = rows[(size_t) row];
        if (r.slot < 0 && pieces[(size_t) r.piece].count > 1 && e.position.x < (float) area.getX() + 18.0f)
        {
            setExpanded (r.piece, ! expanded[(size_t) r.piece]);
            return;
        }
        if (r.slot < 0 && e.position.x >= (float) area.getRight() - 40.0f)
        {
            if (e.position.x < (float) area.getRight() - 20.0f)
                setRowMute (row, ! muted[(size_t) r.piece]);
            else
                setRowSolo (row, ! soloed[(size_t) r.piece]);
            return;
        }
        drag = Drag::rowOrder;
        grabRow = row;
        return;
    }
    // The velocity lane.
    if (lane.contains (e.getPosition()))
    {
        velocityEdits.clear();
        drag = Drag::velocity;
        grabHit = pencil ? -1 : velocityHitAt (e.position);
        if (! pencil && grabHit < 0)
            drag = Drag::none;
        mouseDrag (e);
        return;
    }
    if (! grid.contains (e.getPosition()))
        return;
    const auto hit = hitAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            const auto h = b->hits[(size_t) hit];
            edit ([h] (song::Block& blk)
            {
                std::erase_if (blk.hits, [&h] (const song::Hit& x) { return x.slot == h.slot && sameBeat (x.beat, h.beat); });
            });
            return;
        }
        juce::PopupMenu m;
        m.addItem ("Cut", ! selection.empty(), false, [this] { copySelected(); deleteSelected(); });
        m.addItem ("Copy", ! selection.empty(), false, [this] { copySelected(); });
        m.addItem ("Paste", clipboard.has_value(), false, [this] { paste(); });
        m.addItem ("Delete", ! selection.empty(), false, [this] { deleteSelected(); });
        m.addSeparator();
        m.addItem ("Select All", [this] { selectAll(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    if (hit >= 0)
    {
        // On a note: choose it (SHIFT: add / take away) and drag the chosen (ALT: copies).
        const auto h = b->hits[(size_t) hit];
        if (e.mods.isShiftDown())
            select (h.slot, h.beat, true);
        else if (! isSelected (h))
            select (h.slot, h.beat, false);
        previewSlot (h.slot);
        if (isSelected (h))
        {
            drag = Drag::move;
            grabHit = hit;
            dragCopy = e.mods.isAltDown() && ! e.mods.isCtrlDown();
            moveBeats = 0.0;
            moveRows = 0;
        }
        return;
    }
    if (pencil)
    {
        // Notes drawn along the row, on the resolution.
        const auto row = rowAt (e.position.y);
        const auto slot = row >= 0 ? (rows[(size_t) row].slot >= 0 ? rows[(size_t) row].slot
                                                                   : (rows[(size_t) row].heading ? -1 : pieces[(size_t) rows[(size_t) row].piece].articulations[0].slot))
                                   : -1;
        if (slot < 0)
            return;
        drag = Drag::pencil;
        grabRow = row;
        drawn.clear();
        mouseDrag (e);
        previewSlot (slot);
        return;
    }
    if (! e.mods.isShiftDown())
        selectNone();
    drag = Drag::lasso;
}

void BlockEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto* b = block();
    if (b == nullptr)
        return;
    dragNow = e.position;
    switch (drag)
    {
        case Drag::lasso:
            repaint();
            break;
        case Drag::move:
        {
            if (grabHit < 0 || grabHit >= (int) b->hits.size())
                break;
            const auto& anchor = b->hits[(size_t) grabHit];
            const auto dx = (double) (e.position.x - dragStart.x) / pxPerBeat;
            moveBeats = snap (anchor.beat + dx) - anchor.beat;
            if (! snapOn)
                moveBeats = dx;
            moveRows = juce::roundToInt ((e.position.y - dragStart.y) / rowHeight());
            repaint();
            break;
        }
        case Drag::pencil:
        {
            const auto& r = rows[(size_t) grabRow];
            const auto slot = r.slot >= 0 ? r.slot : pieces[(size_t) r.piece].articulations[0].slot;
            const auto step = resolutionBeats();
            const auto from = beatAt (dragStart.x), to = beatAt (e.position.x);
            const auto place = [&] (double beat)
            {
                beat = snapOn ? std::floor (beat / step + 1.0e-9) * step : beat;
                if (beat < 0.0 || beat >= b->loopLength - 1.0e-9)
                    return;
                if (std::none_of (drawn.begin(), drawn.end(), [&] (const song::Hit& h) { return sameBeat (h.beat, beat); }))
                    drawn.push_back ({ beat, slot, 0.8f });
            };
            place (from);
            if (snapOn)
                for (auto beat = std::floor (juce::jmin (from, to) / step) * step; beat <= juce::jmax (from, to) + 1.0e-9; beat += step)
                    place (beat);
            else
                place (to);
            repaint();
            break;
        }
        case Drag::velocity:
        {
            const auto level = juce::jlimit (1.0f / 127.0f, 1.0f, ((float) lane.getBottom() - 3.0f - e.position.y) / (float) (lane.getHeight() - 8));
            if (grabHit >= 0)
            {
                // A stalk dragged: it (and the other chosen ones by as much) to that height.
                const auto& g = b->hits[(size_t) grabHit];
                const auto delta = level - g.velocity;
                velocityEdits.clear();
                const auto withOthers = isSelected (g) && selection.size() > 1;
                for (size_t i = 0; i < b->hits.size(); ++i)
                    if ((int) i == grabHit)
                        velocityEdits.push_back ({ i, level });
                    else if (withOthers && isSelected (b->hits[i]))
                        velocityEdits.push_back ({ i, juce::jlimit (1.0f / 127.0f, 1.0f, b->hits[i].velocity + delta) });
            }
            else
            {
                // The pencil across the lane: every stalk it passes to its height.
                for (size_t i = 0; i < b->hits.size(); ++i)
                    if (rowOf (b->hits[i].slot) >= 0 && std::abs (xOf (b->hits[i].beat) - e.position.x) <= 4.0f)
                    {
                        std::erase_if (velocityEdits, [i] (const auto& v) { return v.first == i; });
                        velocityEdits.push_back ({ i, level });
                    }
            }
            repaint();
            break;
        }
        case Drag::rowOrder:
        case Drag::scroll:
        case Drag::none:
            break;
    }
}

void BlockEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto* b = block();
    const auto was = drag;
    drag = Drag::none;
    if (b == nullptr)
        return;
    switch (was)
    {
        case Drag::lasso:
        {
            const auto box = juce::Rectangle<float> (dragStart, dragNow);
            if (box.getWidth() < 2.0f && box.getHeight() < 2.0f)
                break;
            for (const auto& h : b->hits)
                if (const auto cell = cellOf (h.slot, h.beat); ! cell.isEmpty() && box.intersects (cell) && ! isSelected (h))
                    selection.push_back ({ h.slot, h.beat });
            baseBlock.reset();
            sliderValues = {};
            syncSliderLabels();
            break;
        }
        case Drag::move:
        {
            if (std::abs (moveBeats) < 1.0e-9 && moveRows == 0)
                break;
            const auto by = moveBeats;
            const auto rowsBy = moveRows;
            const auto copy = dragCopy;
            std::vector<Key> moved;
            edit ([&] (song::Block& blk)
            {
                std::vector<song::Hit> kept, shifted;
                for (const auto& h : blk.hits)
                {
                    if (! isSelected (h))
                    {
                        kept.push_back (h);
                        continue;
                    }
                    if (copy)
                        kept.push_back (h);
                    auto beat = std::fmod (h.beat + by, blk.loopLength);
                    if (beat < 0.0)
                        beat += blk.loopLength;
                    if (beat > blk.loopLength - 1.0e-6)
                        beat = 0.0;
                    auto slot = h.slot;
                    if (rowsBy != 0)
                    {
                        const auto to = juce::jlimit (0, (int) rows.size() - 1, rowOf (h.slot) + rowsBy);
                        if (const auto s = slotForRow (to, h.slot); s >= 0)
                            slot = s;
                    }
                    shifted.push_back ({ beat, slot, h.velocity });
                }
                for (const auto& s : shifted)
                    std::erase_if (kept, [&s] (const song::Hit& h) { return h.slot == s.slot && sameBeat (h.beat, s.beat); });
                kept.insert (kept.end(), shifted.begin(), shifted.end());
                blk.hits = std::move (kept);
                song::tidyHits (blk);
                for (const auto& s : shifted)
                    moved.push_back ({ s.slot, s.beat });
            });
            selection = std::move (moved);
            break;
        }
        case Drag::pencil:
        {
            const auto notes = drawn;
            drawn.clear();
            if (notes.empty())
                break;
            edit ([&notes] (song::Block& blk)
            {
                for (const auto& h : notes)
                    song::addHit (blk, h);
            });
            selection.clear();
            for (const auto& h : notes)
                selection.push_back ({ h.slot, h.beat });
            break;
        }
        case Drag::velocity:
        {
            const auto changes = velocityEdits;
            velocityEdits.clear();
            if (changes.empty())
                break;
            edit ([&changes] (song::Block& blk)
            {
                for (const auto& [i, v] : changes)
                    if (i < blk.hits.size())
                        blk.hits[i].velocity = v;
            });
            break;
        }
        case Drag::rowOrder:
        {
            const auto target = rowAt (e.position.y);
            if (std::abs (e.position.y - dragStart.y) < 4.0f || target < 0 || target == grabRow)
            {
                selectRow (grabRow, e.mods.isShiftDown() || e.mods.isCommandDown());
                break;
            }
            // The instrument moved to where the target row's instrument is in the order.
            const auto piece = rows[(size_t) grabRow].piece;
            const auto onto = rows[(size_t) target].piece;
            if (piece != onto)
            {
                const auto to = (int) std::distance (order.begin(), std::find (order.begin(), order.end(), onto));
                moveRow (piece, to);
            }
            break;
        }
        case Drag::scroll:
        case Drag::none:
            break;
    }
    grabHit = -1;
    repaint();
}

void BlockEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // POINTER: a double-click on empty space adds a note there (on the resolution while SNAP is on).
    const auto* b = block();
    if (b == nullptr || ! grid.contains (e.getPosition()) || hitAt (e.position) >= 0 || tool != Tool::pointer)
        return;
    const auto row = rowAt (e.position.y);
    if (row < 0 || rows[(size_t) row].heading)
        return;
    const auto slot = rows[(size_t) row].slot >= 0 ? rows[(size_t) row].slot : pieces[(size_t) rows[(size_t) row].piece].articulations[0].slot;
    auto beat = snap (beatAt (e.position.x));
    if (beat >= b->loopLength - 1.0e-9)
        beat = snapOn ? std::floor ((b->loopLength - 1.0e-9) / resolutionBeats()) * resolutionBeats() : b->loopLength - 1.0e-3;
    beat = juce::jmax (0.0, beat);
    edit ([slot, beat] (song::Block& blk) { song::addHit (blk, { beat, slot, 0.8f }); });
    select (slot, beat, false);
    previewSlot (slot);
}

void BlockEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isAltDown())
    {
        // ZOOM about the mouse.
        const auto at = beatAt (e.position.x);
        setZoom (pxPerBeat * std::pow (2.0, wheel.deltaY * 2.0));
        setViewStart (at - (e.position.x - (float) grid.getX()) / pxPerBeat);
        return;
    }
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setViewStart (viewBeat - amount * 200.0 / juce::jmax (1.0, pxPerBeat));
        return;
    }
    rowScroll -= juce::roundToInt (wheel.deltaY * 8.0f);
    clampView();
    repaint();
}

//==============================================================================
void BlockEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto top = area.removeFromTop (headerHeight);
    closeButton.setBounds (top.removeFromRight (36).reduced (0, 4));
    top.removeFromRight (10);
    autoScrollButton.setBounds (top.removeFromRight (104).reduced (2, 4));
    zoom.setBounds (top.removeFromRight (120).reduced (2, 6));
    zoomLabel.setBounds (top.removeFromRight (42));
    top.removeFromRight (8);
    rowsButton.setBounds (top.removeFromRight (64).reduced (2, 4));
    optionsButton.setBounds (top.removeFromRight (80).reduced (2, 4));
    selectButton.setBounds (top.removeFromRight (70).reduced (2, 4));
    playStyleTab.setBounds (top.removeFromLeft (150).reduced (0, 4));
    gridTab.setBounds (top.removeFromLeft (124).reduced (4, 4));

    toolbar = area.removeFromTop (toolbarHeight);
    auto t = toolbar;
    pointerButton.setBounds (t.removeFromLeft (76).reduced (2, 3));
    pencilButton.setBounds (t.removeFromLeft (70).reduced (2, 3));
    t.removeFromLeft (10);
    snapButton.setBounds (t.removeFromLeft (56).reduced (2, 3));
    resolutionChoice.setBounds (t.removeFromLeft (110).reduced (2, 3));
    t.removeFromLeft (10);
    for (const auto k : { Slider::quantize, Slider::swing, Slider::randomize })
    {
        auto col = t.removeFromLeft (150);
        sliderLabels[(size_t) k].setBounds (col.removeFromTop (12));
        sliders[(size_t) k].setBounds (col.reduced (2, 0));
        if (k == Slider::quantize)
            maxButton.setBounds (t.removeFromLeft (44).reduced (2, 4));
        t.removeFromLeft (6);
    }
    nudgeLabel.setBounds (t.removeFromLeft (48));
    nudgeLeft.setBounds (t.removeFromLeft (28).reduced (2, 4));
    nudgeRight.setBounds (t.removeFromLeft (28).reduced (2, 4));
    t.removeFromLeft (10);
    soloMuteButton.setBounds (t.removeFromLeft (150).reduced (2, 4));

    hint.setBounds (area.removeFromBottom (hintHeight));
    area.removeFromBottom (4);
    auto laneRow = area.removeFromBottom (laneHeight);
    laneControls = laneRow.removeFromLeft (namesWidth);
    lane = laneRow.withTrimmedRight (scrollSize);
    {
        auto c = laneControls.reduced (6, 4);
        const auto h = c.getHeight() / 4;
        for (const auto k : { Slider::velocity, Slider::dynamics, Slider::randomVelocity, Slider::slope })
        {
            auto line = c.removeFromTop (h);
            sliderLabels[(size_t) k].setBounds (line.removeFromLeft (96));
            sliders[(size_t) k].setBounds (line);
        }
    }
    auto scrollRow = area.removeFromBottom (scrollSize);
    area.removeFromBottom (2);
    ruler = area.removeFromTop (rulerHeight).withTrimmedLeft (namesWidth).withTrimmedRight (scrollSize);
    names = area.removeFromLeft (namesWidth);
    vScroll.setBounds (area.removeFromRight (scrollSize));
    grid = area;
    hScroll.setBounds (scrollRow.withTrimmedLeft (namesWidth).withTrimmedRight (scrollSize));
    if (const auto* b = block(); b != nullptr && grid.getWidth() > 0)
    {
        const auto fit = grid.getWidth() / juce::jmax (0.25, b->loopLength);
        if (pxPerBeat < fit - 1.0e-9)
            pxPerBeat = fit;
        zoom.setValue (std::log (pxPerBeat / fit) / std::log (32.0), juce::dontSendNotification);
    }
    clampView();
}

void BlockEditor::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    const auto* b = block();
    if (b == nullptr)
        return;
    g.setColour (ParagonColours::accent);
    g.fillRect (gridTab.getX(), gridTab.getBottom() + 1, gridTab.getWidth(), 2);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const auto bar = barBeats();
    const auto bars = b->loopLength / bar;
    const auto times = b->length / b->loopLength;
    auto title = b->name + "   " + juce::String (bars, std::abs (bars - std::round (bars)) < 1.0e-6 ? 0 : 2)
               + (std::abs (bars - 1.0) < 1.0e-6 ? " bar" : " bars");
    if (times > 1.0 + 1.0e-6)
        title << ", played " << juce::String (times, std::abs (times - std::round (times)) < 1.0e-6 ? 0 : 2) << " times in the song";
    title << "   " << (int) selection.size() << " of " << (int) b->hits.size() << " notes chosen";
    g.drawText (title, gridTab.getRight() + 16, 8, selectButton.getX() - gridTab.getRight() - 24, headerHeight, juce::Justification::centredLeft, true);

    // The workspace and the lane.
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRect (grid);
    g.fillRect (lane);
    g.setColour (juce::Colour (0xff100c10));
    g.fillRect (laneControls);

    // The rows: stripes, the names (arrow, name, notes, M / S).
    std::array<int, synth::drums::numSlots> counts {};
    for (const auto& h : b->hits)
        if (h.slot >= 0 && h.slot < synth::drums::numSlots)
            ++counts[(size_t) h.slot];
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (names.getUnion (grid));
        for (int r = rowScroll; r < (int) rows.size(); ++r)
        {
            const auto y = rowTop (r);
            if (y > (float) grid.getBottom())
                break;
            const auto& row = rows[(size_t) r];
            const auto mask = row.heading ? song::playstyle::slotsOfPiece (row.piece) : slotsOfRow (r);
            auto n = 0;
            for (int s = 0; s < synth::drums::numSlots; ++s)
                if ((mask & song::slotBit (s)) != 0)
                    n += counts[(size_t) s];
            if (r % 2 == 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect ((float) grid.getX(), y, (float) grid.getWidth(), rowHeight());
            }
            if (row.heading)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRect ((float) grid.getX(), y, (float) grid.getWidth(), rowHeight());
            }
            auto area = rowNameArea (r).toFloat();
            const auto sub = row.slot >= 0;
            auto text = area.reduced (4.0f, 0.0f);
            if (! sub && pieces[(size_t) row.piece].count > 1)
            {
                juce::Path arrow;
                const auto c = juce::Point<float> (area.getX() + 9.0f, area.getCentreY());
                if (expanded[(size_t) row.piece])
                    arrow.addTriangle (c.x - 4.0f, c.y - 2.5f, c.x + 4.0f, c.y - 2.5f, c.x, c.y + 3.0f);
                else
                    arrow.addTriangle (c.x - 2.5f, c.y - 4.0f, c.x - 2.5f, c.y + 4.0f, c.x + 3.0f, c.y);
                g.setColour (ParagonColours::textDim);
                g.fillPath (arrow);
            }
            text.removeFromLeft (sub ? 30.0f : 16.0f);
            const auto heard = ! muted[(size_t) row.piece];
            g.setColour (sub ? ParagonColours::textDim : (heard ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.6f)));
            g.setFont (juce::FontOptions (sub ? 11.0f : 12.0f, sub ? juce::Font::plain : juce::Font::bold));
            const auto name = sub ? synth::drums::articulationName (row.slot) : pieceTitle (row.piece);
            g.drawText (name, text.withTrimmedRight (sub ? 30.0f : 70.0f), juce::Justification::centredLeft, true);
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (juce::String (n), text.withTrimmedRight (sub ? 0.0f : 44.0f).removeFromRight (26.0f), juce::Justification::centredRight, false);
            if (! sub)
            {
                // M and S.
                const auto mBox = juce::Rectangle<float> (area.getRight() - 38.0f, area.getY() + 3.0f, 16.0f, rowHeight() - 6.0f);
                const auto sBox = mBox.translated (19.0f, 0.0f);
                for (const auto& [box, on, label] : { std::tuple { mBox, muted[(size_t) row.piece], "M" }, std::tuple { sBox, soloed[(size_t) row.piece], "S" } })
                {
                    g.setColour (on ? ParagonColours::accent : juce::Colour (0xff1c171c));
                    g.fillRoundedRectangle (box, 2.0f);
                    g.setColour (on ? juce::Colours::black : ParagonColours::textDim);
                    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                    g.drawText (label, box, juce::Justification::centred, false);
                }
            }
            g.setColour (ParagonColours::panelEdge.withAlpha (0.5f));
            g.drawHorizontalLine (juce::roundToInt (y + rowHeight()) - 1, (float) names.getX(), (float) grid.getRight());
        }
    }

    // The grid's lines (straight or triplet, as fine as the zoom allows): bars strongest.
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (grid.getUnion (ruler).getUnion (lane));
        auto step = triplets ? 1.0 / 3.0 : 0.25;
        for (const auto d : triplets ? std::initializer_list<double> { 1.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 }
                                     : std::initializer_list<double> { 1.0, 0.5, 0.25, 0.125 })
            if (d * pxPerBeat >= 12.0)
                step = d;
        const auto firstBeat = std::floor (viewBeat / step) * step;
        for (auto beat = juce::jmax (0.0, firstBeat); beat < b->loopLength - 1.0e-9; beat += step)
        {
            const auto x = xOf (beat);
            if (x > (float) grid.getRight())
                break;
            const auto onBar = std::abs (std::remainder (beat, bar)) < 1.0e-6;
            const auto onBeat = std::abs (std::remainder (beat, 1.0)) < 1.0e-6;
            g.setColour (onBar ? ParagonColours::panelEdge.brighter (0.4f) : ParagonColours::panelEdge.withAlpha (onBeat ? 0.9f : 0.5f));
            g.fillRect (x, (float) grid.getY(), onBar ? 1.5f : 1.0f, (float) grid.getHeight());
            g.setColour (ParagonColours::panelEdge.withAlpha (onBar ? 0.8f : onBeat ? 0.4f : 0.15f));
            g.fillRect (x, (float) lane.getY(), 1.0f, (float) lane.getHeight());
            if (onBar || (onBeat && pxPerBeat > 36.0))
            {
                const auto barNumber = (int) std::floor (beat / bar + 1.0e-6);
                const auto inBar = (int) std::round (beat - barNumber * bar);
                g.setColour (onBar ? ParagonColours::text : ParagonColours::textDim);
                g.setFont (juce::FontOptions (10.0f, onBar ? juce::Font::bold : juce::Font::plain));
                g.drawText (onBar ? juce::String (barNumber + 1) : juce::String (barNumber + 1) + "." + juce::String (inBar + 1),
                            juce::roundToInt (x) + 3, ruler.getY(), 40, ruler.getHeight(), juce::Justification::centredLeft, false);
            }
        }
        if (triplets)
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("TRIPLET", ruler.removeFromRight (60), juce::Justification::centredRight, false);
        }
    }

    // The notes as diamonds, brighter the louder; the chosen ones ringed; while dragging, where they'll go.
    const auto diamond = [&g] (juce::Rectangle<float> r, juce::Colour fill, bool ringed)
    {
        juce::Path d;
        const auto c = r.getCentre();
        const auto h = r.getWidth() * 0.5f;
        d.startNewSubPath (c.x, c.y - h);
        d.lineTo (c.x + h, c.y);
        d.lineTo (c.x, c.y + h);
        d.lineTo (c.x - h, c.y);
        d.closeSubPath();
        g.setColour (fill);
        g.fillPath (d);
        if (ringed)
        {
            g.setColour (ParagonColours::text);
            g.strokePath (d, juce::PathStrokeType (1.5f));
        }
    };
    const auto noteColour = [] (float v) { return ParagonColours::accent.withMultipliedBrightness (0.35f + 0.65f * v).withAlpha (0.4f + 0.6f * v); };
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (grid);
        for (size_t i = 0; i < b->hits.size(); ++i)
        {
            const auto& h = b->hits[i];
            const auto cell = cellOf (h.slot, h.beat);
            if (cell.isEmpty())
                continue;
            const auto chosen = isSelected (h);
            const auto moving = drag == Drag::move && chosen;
            diamond (cell, noteColour (h.velocity).withMultipliedAlpha (moving && ! dragCopy ? 0.35f : 1.0f), chosen);
            if (moving)
            {
                auto beat = std::fmod (h.beat + moveBeats, b->loopLength);
                if (beat < 0.0)
                    beat += b->loopLength;
                const auto toRow = juce::jlimit (0, (int) rows.size() - 1, rowOf (h.slot) + moveRows);
                const auto c = juce::Point<float> (xOf (beat), rowTop (toRow) + rowHeight() * 0.5f);
                diamond (cell.withCentre (c), ParagonColours::glow, true);
            }
        }
        for (const auto& h : drawn)
            diamond (cellOf (h.slot, h.beat), ParagonColours::glow, true);
        if (drag == Drag::lasso)
        {
            const auto box = juce::Rectangle<float> (dragStart, dragNow);
            g.setColour (ParagonColours::accent.withAlpha (0.12f));
            g.fillRect (box);
            g.setColour (ParagonColours::accent);
            g.drawRect (box, 1.0f);
        }
    }

    // The velocity lane: a stalk per note (the chosen bright), as dragged while dragging.
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (lane);
        for (size_t i = 0; i < b->hits.size(); ++i)
        {
            const auto& h = b->hits[i];
            if (rowOf (h.slot) < 0)
                continue;
            auto v = h.velocity;
            for (const auto& [k, nv] : velocityEdits)
                if (k == i)
                    v = nv;
            const auto x = xOf (h.beat);
            const auto y = (float) lane.getBottom() - 3.0f - (float) (lane.getHeight() - 8) * v;
            const auto chosen = isSelected (h);
            g.setColour (chosen ? ParagonColours::accent : ParagonColours::accent.withAlpha (0.35f));
            g.fillRect (x - 1.0f, y, 2.0f, (float) lane.getBottom() - 3.0f - y);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }
        g.setColour (ParagonColours::textDim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("VELOCITY", lane.reduced (6, 3), juce::Justification::topLeft, false);
    }

    // The playhead.
    if (shownPlayhead >= 0.0)
    {
        const auto x = xOf (shownPlayhead);
        if (x >= (float) grid.getX() && x <= (float) grid.getRight())
        {
            g.setColour (ParagonColours::glow);
            g.fillRect (x - 0.5f, (float) ruler.getY(), 1.5f, (float) (lane.getBottom() - ruler.getY()));
        }
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (grid);
    g.drawRect (lane);
    if (rows.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.drawText ("No drums to show: ROWS > SHOW UNMAPPED / TURNED-OFF DRUMS.", grid, juce::Justification::centred, false);
    }
}
