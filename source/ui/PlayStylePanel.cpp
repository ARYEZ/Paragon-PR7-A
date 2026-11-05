#include "ui/PlayStylePanel.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    using synth::drums::pieces;
    constexpr int headerHeight = 40, sideWidth = 300, chipsHeight = 34;

    juce::String pieceTitle (int piece)
    {
        return piece >= 0 && piece < synth::drums::numPieces ? juce::String (pieces[(size_t) piece].name) : juce::String();
    }

    juce::String articulationTitle (int slot)
    {
        const auto piece = synth::drums::pieceOfSlot (slot);
        if (piece < 0)
            return {};
        return pieces[(size_t) piece].count > 1 ? pieceTitle (piece) + " " + synth::drums::articulationName (slot).toUpperCase()
                                                : pieceTitle (piece);
    }

    float velocityFactor (double v) noexcept { return (float) std::pow (2.0, juce::jlimit (-1.0, 1.0, v)); }
}

//==============================================================================
// A flag on the kit: POWER HAND or OPENING HIT. Drag it onto an instrument; its arrow opens the articulation menu.
class PlayStylePanel::Tag final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    Tag (PlayStylePanel& p, bool power) : panel (p), isPower (power)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (isPower ? "POWER HAND: the groove's leading instrument. Click it: the instrument it's on and its articulations, "
                              "then the kit's other drums and cymbals A to Z; pick one and its notes move there (merged with what "
                              "that one plays). You can still drag it onto an instrument."
                            : "OPENING HIT: one hit at the block's start (OPENING HIT ON / OFF). Click it: the instrument it's on "
                              "and its articulations, then the others A to Z. You can still drag it onto an instrument.");
    }

    void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto colour = isPower ? ParagonColours::accent : juce::Colour (0xffff3d7a);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (lit ? colour : colour.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 4.0f, 1.6f);
        auto arrow = r.removeFromRight (18.0f);
        g.drawVerticalLine ((int) arrow.getX(), r.getY() + 3.0f, r.getBottom() - 3.0f);
        juce::Path p;
        const auto c = arrow.getCentre();
        p.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
        g.fillPath (p);
        g.setColour (lit ? ParagonColours::text : ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (isPower ? "POWER HAND" : "OPENING HIT", r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        moved = false;
        if (e.x >= getWidth() - 19 || e.mods.isPopupMenu())
        {
            panel.showArticulations (isPower, this);
            return;
        }
        dragger.startDraggingComponent (this, e);
        toFront (false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x >= (float) getWidth() - 19.0f || e.getDistanceFromDragStart() < 3)
            return;
        moved = true;
        dragger.dragComponent (this, e, nullptr);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! moved)
        {
            if (e.mouseDownPosition.x < (float) getWidth() - 19.0f && ! e.mods.isPopupMenu())
                panel.showArticulations (isPower, this);   // a click: its list (the arrow's opened it already)
            return;
        }
        moved = false;
        // Where its pointed end is: the flag's left edge, half way down.
        const auto at = getBounds().getPosition().translated (2, getHeight() / 2);
        panel.dropTag (isPower, at);
    }

private:
    PlayStylePanel& panel;
    bool isPower, lit = true, moved = false;
    juce::ComponentDragger dragger;
};

//==============================================================================
PlayStylePanel::PlayStylePanel (ParagonX1AudioProcessor& p) : processor (p), kit (p)
{
    powerTag = std::make_unique<Tag> (*this, true);
    openingTag = std::make_unique<Tag> (*this, false);
    kit.onChoose = [this] (int slot, juce::ModifierKeys mods)
    {
        choose (synth::drums::pieceOfSlot (slot), mods.isShiftDown() || mods.isCommandDown());
    };
    kit.onMenu = [this] (int slot, juce::Point<float>)
    {
        const auto piece = synth::drums::pieceOfSlot (slot);
        if (piece < 0)
            return;
        if (std::find (chosen.begin(), chosen.end(), piece) == chosen.end())
            choose (piece);
        instrumentMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kit).withMousePosition());
    };
    addAndMakeVisible (kit);
    addAndMakeVisible (*powerTag);
    addAndMakeVisible (*openingTag);

    playStyleTab.setToggleState (true, juce::dontSendNotification);
    playStyleTab.setTooltip ("EDIT PLAY STYLE: change how the block's drums play, instrument by instrument.");
    gridTab.setTooltip ("The same block in the GRID EDITOR, note by note.");
    gridTab.onClick = [this]
    {
        if (onGridEditor != nullptr && ! blocks.empty())
        {
            const auto b = blocks.front();
            close();
            onGridEditor (b);
        }
    };
    closeButton.setTooltip ("Close EDIT PLAY STYLE (the song plays on).");
    closeButton.onClick = [this] { close(); };
    selectButton.setTooltip ("Every instrument (the ones the block plays marked), and groups of them.");
    selectButton.onClick = [this] { selectMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&selectButton)); };

    for (auto* knob : { &amount, &velocity })
    {
        knob->setRange (-100.0, 100.0, 1.0);
        knob->setValue (0.0, juce::dontSendNotification);
        knob->setDoubleClickReturnValue (true, 0.0, juce::ModifierKeys::commandModifier);   // CTRL / CMD-click: reset
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob->onDragStart = [this] { dragging = true; };
        knob->onDragEnd = [this] { dragging = false; applyKnobs (true); };
        knob->onValueChange = [this] { applyKnobs (! dragging); };
    }
    amount.setTooltip ("AMOUNT: fewer hits (left) or more (right) for the chosen instruments, where a drummer would leave them "
                       "out or add them. CTRL / CMD-click: back to the groove's own.");
    velocity.setTooltip ("VELOCITY: the chosen instruments softer (left, to half) or louder (right, to twice), their balance kept.");
    amountReset.setTooltip ("Reset AMOUNT");
    amountReset.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Reset Amount", [this] { resetAmount(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&amountReset));
    };
    velocityReset.setTooltip ("Reset VELOCITY");
    velocityReset.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Reset Velocity", [this] { setVelocity (0.0); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&velocityReset));
    };
    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Only the chosen instruments heard while the song plays.");
    soloButton.onClick = [this] { setSolo (soloButton.getToggleState()); };
    openingButton.setClickingTogglesState (true);
    openingButton.setTooltip ("OPENING HIT: one hit at the block's start, on the instrument its flag is on.");
    openingButton.onClick = [this] { setOpeningOn (openingButton.getToggleState()); };

    for (auto* l : { &amountLabel, &velocityLabel })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centred);
    }
    for (auto* l : { &totalLabel, &velocityValue })
    {
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::text);
        l->setJustificationType (juce::Justification::centred);
    }
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, ParagonColours::text);
    chosenLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    chosenLabel.setColour (juce::Label::textColourId, ParagonColours::accent);
    chosenLabel.setJustificationType (juce::Justification::topLeft);
    chosenLabel.setMinimumHorizontalScale (1.0f);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, ParagonColours::textDim);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setText ("Click an instrument to choose it (SHIFT: more). Right-click it: cut, copy, paste or remove its MIDI, quantize "
                  "it, its articulation. Drag the POWER HAND or OPENING HIT flag onto another instrument. SHIFT-click blocks in "
                  "the song strip to change them together.", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &playStyleTab, &gridTab, &closeButton, &selectButton, &amount, &velocity,
                                                             &amountReset, &velocityReset, &soloButton, &openingButton, &amountLabel,
                                                             &velocityLabel, &totalLabel, &velocityValue, &title, &chosenLabel, &hint })
        addAndMakeVisible (c);

    // The instruments with no piece in the kit's picture get a button under it.
    for (int k = 0; k < synth::drums::numPieces; ++k)
        if (KitView::pieceOf (pieces[(size_t) k].articulations[0].slot) < 0)
            chipPieces.push_back (k);
    processor.songBroadcaster.addChangeListener (this);
}

PlayStylePanel::~PlayStylePanel()
{
    processor.songBroadcaster.removeChangeListener (this);
    if (solo)
        processor.setSongSolo (0);
}

//==============================================================================
void PlayStylePanel::open (std::vector<int> b)
{
    blocks.clear();
    chosen.clear();
    setBlocks (std::move (b));
    setVisible (true);
    toFront (false);
    startTimerHz (30);
}

void PlayStylePanel::setBlocks (std::vector<int> b)
{
    const auto& s = processor.getSong();
    std::erase_if (b, [&s] (int i) { return i < 0 || i >= (int) s.blocks.size(); });
    std::sort (b.begin(), b.end());
    b.erase (std::unique (b.begin(), b.end()), b.end());
    const auto changed = b != blocks;
    blocks = std::move (b);
    // New blocks: every instrument they play chosen (the whole kit, as the window opens).
    if (changed || chosen.empty())
        chosen = usedPieces();
    refresh();
}

void PlayStylePanel::close()
{
    stopTimer();
    setSolo (false);
    setVisible (false);
    if (onHighlightChanged != nullptr)
        onHighlightChanged (0);
    if (onClose != nullptr)
        onClose();
}

void PlayStylePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! isVisible())
        return;
    // Blocks gone (undo, a delete): only those still there.
    const auto& s = processor.getSong();
    auto still = blocks;
    std::erase_if (still, [&s] (int i) { return i >= (int) s.blocks.size(); });
    if (still.empty())
    {
        close();
        return;
    }
    if (still != blocks)
        setBlocks (still);
    else
        refresh();
}

void PlayStylePanel::timerCallback()
{
    if (isShowing())
        kit.updateLights();
}

//==============================================================================
std::vector<int> PlayStylePanel::usedPieces() const
{
    std::vector<int> out;
    const auto& s = processor.getSong();
    for (const auto b : blocks)
        if (b >= 0 && b < (int) s.blocks.size())
            for (const auto p : song::playstyle::piecesIn (s.blocks[(size_t) b]))
                if (std::find (out.begin(), out.end(), p) == out.end())
                    out.push_back (p);
    std::sort (out.begin(), out.end());
    return out;
}

void PlayStylePanel::choose (int piece, bool add)
{
    if (piece < 0 || piece >= synth::drums::numPieces)
        return;
    if (! add)
        chosen = { piece };
    else if (const auto it = std::find (chosen.begin(), chosen.end(), piece); it != chosen.end())
    {
        if (chosen.size() > 1)
            chosen.erase (it);
    }
    else
    {
        chosen.push_back (piece);
        std::sort (chosen.begin(), chosen.end());
    }
    if (solo)
        setSolo (true);
    refresh();
}

void PlayStylePanel::chooseGroup (song::SlotMask slots)
{
    std::vector<int> picked;
    for (int k = 0; k < synth::drums::numPieces; ++k)
        if ((song::playstyle::slotsOfPiece (k) & slots) != 0)
            picked.push_back (k);
    if (picked.empty())
        return;
    chosen = std::move (picked);
    if (solo)
        setSolo (true);
    refresh();
}

juce::PopupMenu PlayStylePanel::selectMenu()
{
    juce::PopupMenu m;
    const auto& s = processor.getSong();
    song::SlotMask playing = 0;
    for (const auto b : blocks)
        if (b < (int) s.blocks.size())
            playing |= song::playstyle::playingDrums (s.blocks[(size_t) b]);
    m.addItem ("All Playing Drums", playing != 0, false, [this, playing] { chooseGroup (playing); });
    m.addItem ("All Toms", [this] { chooseGroup (song::playstyle::allToms()); });
    m.addItem ("All Crashes", [this] { chooseGroup (song::playstyle::allCrashes()); });
    m.addSeparator();
    const auto used = usedPieces();
    for (int k = 0; k < synth::drums::numPieces; ++k)
    {
        const auto plays = std::find (used.begin(), used.end(), k) != used.end();
        juce::PopupMenu::Item item ((plays ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0  ")) : juce::String ("     ")) + pieceTitle (k));
        item.isTicked = chosen.size() == 1 && chosen.front() == k;
        if (plays)
            item.colour = ParagonColours::accent;
        item.action = [this, k] { choose (k); };
        m.addItem (std::move (item));
    }
    return m;
}

//==============================================================================
PlayStylePanel::Item& PlayStylePanel::itemFor (const song::Song& s, int block, int piece)
{
    const auto now = song::playstyle::notesOf (s.blocks[(size_t) block], piece);
    for (auto& it : items)
        if (it.block == block && it.piece == piece)
        {
            if (it.made != now)   // changed since (another editor, undo): worked out from what's there now
            {
                it.source = it.made = now;
                it.amount = it.velocity = 0.0;
            }
            return it;
        }
    items.push_back ({ block, piece, now, now, 0.0, 0.0 });
    return items.back();
}

std::vector<song::Hit> PlayStylePanel::notesFor (const song::Song& s, const Item& it) const
{
    const auto& b = s.blocks[(size_t) it.block];
    auto notes = song::playstyle::withAmount (it.source, it.piece, it.amount, b, s.beatsPerBarAt (b.start));
    const auto factor = velocityFactor (it.velocity);
    if (! juce::exactlyEqual (factor, 1.0f))
        for (auto& h : notes)
            h.velocity = juce::jlimit (1.0f / 127.0f, 1.0f, h.velocity * factor);
    return notes;
}

void PlayStylePanel::applyKnobs (bool asUndoStep)
{
    auto s = processor.getSong();
    auto changed = false;
    for (const auto b : blocks)
    {
        if (b >= (int) s.blocks.size())
            continue;
        for (const auto piece : chosen)
        {
            auto& it = itemFor (s, b, piece);
            it.amount = amount.getValue() / 100.0;
            it.velocity = velocity.getValue() / 100.0;
            song::playstyle::setNotes (s.blocks[(size_t) b], piece, notesFor (s, it));
            const auto now = song::playstyle::notesOf (s.blocks[(size_t) b], piece);
            changed = changed || now != it.made;
            it.made = now;
        }
    }
    if (asUndoStep)
    {
        if (! (s == processor.getSong()) || changed)
            processor.setSong (std::move (s));
    }
    else if (changed)
        processor.previewSong (std::move (s));
    refresh();
}

void PlayStylePanel::setAmount (double a)
{
    amount.setValue (juce::jlimit (-1.0, 1.0, a) * 100.0, juce::dontSendNotification);
    applyKnobs (true);
}

double PlayStylePanel::getAmount() const { return amount.getValue() / 100.0; }

void PlayStylePanel::resetAmount() { setAmount (0.0); }

void PlayStylePanel::setVelocity (double v)
{
    velocity.setValue (juce::jlimit (-1.0, 1.0, v) * 100.0, juce::dontSendNotification);
    applyKnobs (true);
}

void PlayStylePanel::setSolo (bool on)
{
    solo = on;
    soloButton.setToggleState (on, juce::dontSendNotification);
    song::SlotMask mask = 0;
    if (on)
        for (const auto p : chosen)
            mask |= song::playstyle::slotsOfPiece (p);
    processor.setSongSolo (mask);
}

int PlayStylePanel::getTotalHits() const
{
    const auto& s = processor.getSong();
    int n = 0;
    for (const auto b : blocks)
        if (b < (int) s.blocks.size())
            for (const auto p : chosen)
                n += (int) song::playstyle::notesOf (s.blocks[(size_t) b], p).size();
    return n;
}

//==============================================================================
void PlayStylePanel::edit (const std::function<void (song::Song&)>& f)
{
    auto s = processor.getSong();
    f (s);
    if (! (s == processor.getSong()))
        processor.setSong (std::move (s));
    refresh();
}

void PlayStylePanel::movePowerHand (int toPiece, int toSlot)
{
    edit ([this, toPiece, toSlot] (song::Song& s)
    {
        for (const auto b : blocks)
            if (b < (int) s.blocks.size())
                song::playstyle::movePowerHand (s.blocks[(size_t) b], toPiece, toSlot);
    });
}

void PlayStylePanel::setPowerHandArticulation (int slot)
{
    edit ([slot, this] (song::Song& s)
    {
        for (const auto b : blocks)
            if (b < (int) s.blocks.size())
            {
                auto& block = s.blocks[(size_t) b];
                const auto piece = song::playstyle::powerHandPiece (block);
                if (piece >= 0 && piece == synth::drums::pieceOfSlot (slot))
                {
                    song::playstyle::setArticulation (block, piece, slot);
                    block.powerHand = slot;
                }
            }
    });
}

void PlayStylePanel::setOpeningOn (bool on)
{
    edit ([this, on] (song::Song& s)
    {
        for (const auto b : blocks)
            if (b < (int) s.blocks.size())
                s.blocks[(size_t) b].openingOn = on;
    });
}

void PlayStylePanel::moveOpeningHit (int slot)
{
    if (slot < 0 || slot >= synth::drums::numSlots)
        return;
    edit ([this, slot] (song::Song& s)
    {
        for (const auto b : blocks)
            if (b < (int) s.blocks.size())
            {
                s.blocks[(size_t) b].openingSlot = slot;
                s.blocks[(size_t) b].openingOn = true;   // moved: it plays (ON / OFF turns it off again)
            }
    });
}

void PlayStylePanel::copyMidi()
{
    // The chosen instruments' notes in the first block (one time round its groove).
    const auto& s = processor.getSong();
    if (blocks.empty() || blocks.front() >= (int) s.blocks.size())
        return;
    const auto& b = s.blocks[(size_t) blocks.front()];
    clipboard.clear();
    for (const auto p : chosen)
        clipboard.push_back ({ p, song::playstyle::notesOf (b, p), b.loopLength });
}

void PlayStylePanel::removeMidi()
{
    // AMOUNT all the way down: the notes kept as its source, so turning it back up brings them back.
    auto s = processor.getSong();
    for (const auto b : blocks)
        if (b < (int) s.blocks.size())
            for (const auto p : chosen)
            {
                auto& it = itemFor (s, b, p);
                it.amount = -1.0;
                song::playstyle::setNotes (s.blocks[(size_t) b], p, {});
                it.made = {};
            }
    processor.setSong (std::move (s));
    refresh();
}

void PlayStylePanel::cutMidi()
{
    copyMidi();
    removeMidi();
}

void PlayStylePanel::pasteMidi (int fromPiece)
{
    const auto clip = std::find_if (clipboard.begin(), clipboard.end(), [fromPiece] (const Clip& c) { return c.piece == fromPiece; });
    if (clip == clipboard.end())
        return;
    const auto from = *clip;
    edit ([this, from] (song::Song& s)
    {
        for (const auto b : blocks)
        {
            if (b >= (int) s.blocks.size())
                continue;
            auto& block = s.blocks[(size_t) b];
            for (const auto to : chosen)
            {
                // Each copied note on the same articulation here (its place in the list), else the first; the copy
                // repeated, or cut, to this block's groove.
                std::vector<song::Hit> notes;
                const auto& target = pieces[(size_t) to];
                for (auto repeat = 0.0; repeat < block.loopLength - 1.0e-9 && from.length > 0.0; repeat += from.length)
                    for (const auto& h : from.notes)
                    {
                        auto index = 0;
                        const auto& source = pieces[(size_t) from.piece];
                        for (int a = 0; a < source.count; ++a)
                            if (source.articulations[(size_t) a].slot == h.slot)
                                index = a;
                        const auto slot = target.articulations[(size_t) (index < target.count ? index : 0)].slot;
                        if (repeat + h.beat < block.loopLength - 1.0e-9)
                            notes.push_back ({ repeat + h.beat, slot, h.velocity });
                    }
                song::playstyle::setNotes (block, to, notes);
            }
        }
    });
}

void PlayStylePanel::quantize (int choice)
{
    const auto grid = grooves::gridOf (choice);
    auto s = processor.getSong();
    for (const auto b : blocks)
        if (b < (int) s.blocks.size())
            for (const auto p : chosen)
            {
                // The source quantized too, so AMOUNT goes on from the quantized notes.
                auto& it = itemFor (s, b, p);
                auto temp = s.blocks[(size_t) b];
                temp.hits = it.source;
                song::quantize (temp, grid, song::playstyle::slotsOfPiece (p));
                it.source = temp.hits;
                song::playstyle::setNotes (s.blocks[(size_t) b], p, notesFor (s, it));
                it.made = song::playstyle::notesOf (s.blocks[(size_t) b], p);
            }
    processor.setSong (std::move (s));
    refresh();
}

void PlayStylePanel::setArticulation (int slot)
{
    if (chosen.size() != 1 || synth::drums::pieceOfSlot (slot) != chosen.front())
        return;
    const auto p = chosen.front();
    auto s = processor.getSong();
    for (const auto b : blocks)
        if (b < (int) s.blocks.size())
        {
            auto& it = itemFor (s, b, p);
            auto temp = s.blocks[(size_t) b];
            temp.hits = it.source;
            song::playstyle::setArticulation (temp, p, slot);
            it.source = temp.hits;
            song::playstyle::setNotes (s.blocks[(size_t) b], p, notesFor (s, it));
            it.made = song::playstyle::notesOf (s.blocks[(size_t) b], p);
        }
    processor.setSong (std::move (s));
    refresh();
}

juce::PopupMenu PlayStylePanel::instrumentMenu()
{
    juce::PopupMenu m;
    const auto any = getTotalHits() > 0;
    m.addItem ("Cut MIDI", any, false, [this] { cutMidi(); });
    m.addItem ("Copy MIDI", any, false, [this] { copyMidi(); });
    juce::PopupMenu paste;
    for (const auto& c : clipboard)
        paste.addItem (pieceTitle (c.piece) + " (" + juce::String ((int) c.notes.size()) + ")", [this, p = c.piece] { pasteMidi (p); });
    m.addSubMenu ("Paste MIDI", paste, ! clipboard.empty());
    m.addItem ("Remove MIDI", any, false, [this] { removeMidi(); });
    juce::PopupMenu grid;
    for (int k = 0; k < grooves::quantizeNames.size(); ++k)
        grid.addItem (grooves::quantizeNames[k], [this, k] { quantize (k); });
    m.addSubMenu ("Quantize", grid, any);
    juce::PopupMenu artic;
    if (chosen.size() == 1)
    {
        const auto& piece = pieces[(size_t) chosen.front()];
        for (int a = 0; a < piece.count; ++a)
        {
            const auto slot = piece.articulations[(size_t) a].slot;
            artic.addItem (piece.articulations[(size_t) a].name, [this, slot] { setArticulation (slot); });
        }
    }
    m.addSubMenu ("Articulation", artic, chosen.size() == 1 && pieces[(size_t) chosen.front()].count > 1 && any);
    return m;
}

juce::PopupMenu PlayStylePanel::tagMenu (bool power)
{
    // The flag's list (the owner: dragging it over the kit picture caught on the wrong pieces): the instrument it's on
    // first, ticked, with its articulations; then every other drum and cymbal the kit has (its main articulation On),
    // A to Z. The kick (a foot), the clap, tambourine and shaker left out.
    juce::PopupMenu m;
    const auto& s = processor.getSong();
    if (blocks.empty() || blocks.front() >= (int) s.blocks.size())
        return m;
    const auto& b = s.blocks[(size_t) blocks.front()];
    const auto slot = power ? song::playstyle::powerHandSlot (b) : b.openingSlot;
    const auto piece = synth::drums::pieceOfSlot (slot);
    m.addSectionHeader (power ? "POWER HAND" : "OPENING HIT");
    if (piece >= 0)
    {
        m.addItem (pieceTitle (piece), true, true, [] {});
        const auto& pc = pieces[(size_t) piece];
        for (int a = 0; a < pc.count && pc.count > 1; ++a)
        {
            const auto to = pc.articulations[(size_t) a].slot;
            if (power && to == synth::drums::notemap::PH)
                continue;   // the pedal is the foot, not a hand
            m.addItem (juce::String (juce::CharPointer_UTF8 ("\xc2\xb7  ")) + pc.articulations[(size_t) a].name, true, to == slot, [this, power, to]
            {
                if (power)
                    setPowerHandArticulation (to);
                else
                    moveOpeningHit (to);
                placeTags();
            });
        }
    }
    m.addSeparator();
    const auto kit = processor.getKitSettings();
    std::vector<int> others;
    for (int p = 0; p < synth::drums::numPieces; ++p)
    {
        const juce::String name (pieces[(size_t) p].name);
        if (p == piece || name == "KICK" || name == "CLAP" || name == "TAMBOURINE" || name == "SHAKER")
            continue;
        const auto main = pieces[(size_t) p].articulations[0].slot;
        if (main < 0 || main >= synth::drums::numSlots || ! kit.slots[(size_t) main].active)
            continue;
        others.push_back (p);
    }
    std::sort (others.begin(), others.end(), [] (int a, int b2) { return pieceTitle (a).compareNatural (pieceTitle (b2)) < 0; });
    for (const auto p : others)
        m.addItem (pieceTitle (p), [this, power, p]
        {
            if (power)
                movePowerHand (p, -1);
            else
                moveOpeningHit (pieces[(size_t) p].articulations[0].slot);
            placeTags();
        });
    return m;
}

void PlayStylePanel::showArticulations (bool power, juce::Component* target)
{
    tagMenu (power).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void PlayStylePanel::dropTag (bool power, juce::Point<int> at)
{
    auto slot = -1;
    if (kit.getBounds().contains (at))
        slot = kit.slotAt ((at - kit.getPosition()).toFloat());
    if (slot < 0)
        if (const auto piece = pieceAt (at); piece >= 0)
            slot = pieces[(size_t) piece].articulations[0].slot;
    if (slot >= 0)
    {
        if (power)
            movePowerHand (synth::drums::pieceOfSlot (slot), slot == synth::drums::notemap::PH ? -1 : slot);
        else
            moveOpeningHit (slot);
    }
    placeTags();
}

//==============================================================================
juce::Rectangle<int> PlayStylePanel::chipOf (int piece) const
{
    const auto it = std::find (chipPieces.begin(), chipPieces.end(), piece);
    if (it == chipPieces.end() || chipsArea.isEmpty())
        return {};
    const auto n = (int) chipPieces.size();
    const auto w = chipsArea.getWidth() / juce::jmax (1, n);
    return { chipsArea.getX() + w * (int) std::distance (chipPieces.begin(), it), chipsArea.getY(), w, chipsArea.getHeight() };
}

int PlayStylePanel::pieceAt (juce::Point<int> at) const
{
    for (const auto p : chipPieces)
        if (chipOf (p).contains (at))
            return p;
    if (kit.getBounds().contains (at))
        return synth::drums::pieceOfSlot (kit.slotAt ((at - kit.getPosition()).toFloat()));
    return -1;
}

std::optional<juce::Point<int>> PlayStylePanel::tagPointFor (int slot) const
{
    if (const auto c = kit.slotCentre (slot))
        return (*c).roundToInt() + kit.getPosition();
    // An articulation with no part of its own in the picture (the hat's openings): on its instrument's main one.
    if (const auto piece = synth::drums::pieceOfSlot (slot); piece >= 0)
        if (const auto c = kit.slotCentre (pieces[(size_t) piece].articulations[0].slot))
            return (*c).roundToInt() + kit.getPosition();
    if (const auto kitPiece = KitView::pieceOf (slot); kitPiece >= 0)
        if (const auto c = kit.pieceCentre (kitPiece))
            return (*c).roundToInt() + kit.getPosition();
    if (const auto chip = chipOf (synth::drums::pieceOfSlot (slot)); ! chip.isEmpty())
        return chip.getCentre().withY (chip.getY() - 14);
    return {};
}

juce::Rectangle<int> PlayStylePanel::getPowerHandTag() const { return powerTag->getBounds(); }
juce::Rectangle<int> PlayStylePanel::getOpeningTag() const { return openingTag->getBounds(); }

void PlayStylePanel::placeTags()
{
    const auto& s = processor.getSong();
    const song::Block* b = ! blocks.empty() && blocks.front() < (int) s.blocks.size() ? &s.blocks[(size_t) blocks.front()] : nullptr;
    const auto powerSlot = b != nullptr ? song::playstyle::powerHandSlot (*b) : -1;
    const auto power = powerSlot >= 0 ? tagPointFor (powerSlot) : std::nullopt;
    powerTag->setVisible (power.has_value());
    if (power)
        powerTag->setBounds (juce::Rectangle<int> (power->x, power->y - 11, 118, 22).constrainedWithin (getLocalBounds()));
    const auto opening = b != nullptr ? tagPointFor (b->openingSlot) : std::nullopt;
    openingTag->setVisible (opening.has_value());
    if (opening)
    {
        auto r = juce::Rectangle<int> (opening->x, opening->y - 11, 118, 22);
        if (power && powerTag->getBounds().intersects (r))
            r.setY (powerTag->getBottom() + 3);
        openingTag->setBounds (r.constrainedWithin (getLocalBounds()));
        openingTag->setLit (b->openingOn);
    }
}

juce::String PlayStylePanel::chosenText() const
{
    if (chosen.empty())
        return "Nothing chosen";
    juce::StringArray names;
    for (const auto p : chosen)
        names.add (pieceTitle (p));
    if (names.size() > 4)
        return juce::String (names.size()) + " INSTRUMENTS CHOSEN";
    return names.joinIntoString (", ");
}

void PlayStylePanel::refresh()
{
    const auto& s = processor.getSong();
    // The kit: the played instruments lit, the chosen ones outlined.
    KitView::Highlight h;
    h.on = true;
    for (const auto p : usedPieces())
        if (const auto k = KitView::pieceOf (pieces[(size_t) p].articulations[0].slot); k >= 0)
            h.used[(size_t) k] = true;
    song::SlotMask mask = 0;
    for (const auto p : chosen)
    {
        mask |= song::playstyle::slotsOfPiece (p);
        if (const auto k = KitView::pieceOf (pieces[(size_t) p].articulations[0].slot); k >= 0)
            h.chosen[(size_t) k] = true;
    }
    kit.setHighlight (h);
    if (onHighlightChanged != nullptr)
        onHighlightChanged (mask);

    // The knobs as the first chosen instrument's of the first block were left (without changing anything).
    if (! dragging && ! blocks.empty() && ! chosen.empty() && blocks.front() < (int) s.blocks.size())
    {
        const auto& b = s.blocks[(size_t) blocks.front()];
        const auto now = song::playstyle::notesOf (b, chosen.front());
        auto a = 0.0, v = 0.0;
        for (const auto& it : items)
            if (it.block == blocks.front() && it.piece == chosen.front() && it.made == now)
            {
                a = it.amount;
                v = it.velocity;
            }
        amount.setValue (a * 100.0, juce::dontSendNotification);
        velocity.setValue (v * 100.0, juce::dontSendNotification);
    }
    totalLabel.setText ("TOTAL HITS: " + juce::String (getTotalHits()), juce::dontSendNotification);
    const auto v = velocity.getValue();
    velocityValue.setText ((v >= 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + "%", juce::dontSendNotification);
    chosenLabel.setText (chosenText(), juce::dontSendNotification);

    // The title: the block (and how many more).
    juce::String t = "EDIT PLAY STYLE";
    if (! blocks.empty() && blocks.front() < (int) s.blocks.size())
    {
        t << "   " << s.blocks[(size_t) blocks.front()].name;
        if (blocks.size() > 1)
            t << "  + " << (int) blocks.size() - 1 << (blocks.size() == 2 ? " MORE BLOCK" : " MORE BLOCKS");
    }
    title.setText (t, juce::dontSendNotification);
    const auto opening = ! blocks.empty() && blocks.front() < (int) s.blocks.size() && s.blocks[(size_t) blocks.front()].openingOn;
    openingButton.setToggleState (opening, juce::dontSendNotification);
    openingButton.setButtonText (opening ? "OPENING HIT: ON" : "OPENING HIT: OFF");
    placeTags();
    repaint (chipsArea.expanded (2));
}

//==============================================================================
void PlayStylePanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto top = area.removeFromTop (headerHeight);
    closeButton.setBounds (top.removeFromRight (36).reduced (0, 6));
    playStyleTab.setBounds (top.removeFromLeft (150).reduced (0, 6));
    gridTab.setBounds (top.removeFromLeft (130).reduced (4, 6));
    title.setBounds (top.reduced (16, 0));
    area.removeFromTop (4);
    side = area.removeFromRight (sideWidth);
    area.removeFromRight (12);
    chipsArea = area.removeFromBottom (chipsHeight).reduced (0, 4);
    kitArea = area;
    kit.setBounds (kitArea);

    auto r = side.reduced (8, 4);
    selectButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    chosenLabel.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    auto knobs = r.removeFromTop (170);
    for (auto [knob, label, value, reset] : { std::tuple { &amount, &amountLabel, &totalLabel, &amountReset },
                                              std::tuple { &velocity, &velocityLabel, &velocityValue, &velocityReset } })
    {
        auto col = knobs.removeFromLeft (r.getWidth() / 2);
        label->setBounds (col.removeFromTop (18));
        auto knobRow = col.removeFromTop (110);
        reset->setBounds (knobRow.removeFromRight (22).withSizeKeepingCentre (22, 22));
        knob->setBounds (knobRow.reduced (4));
        value->setBounds (col.removeFromTop (22));
    }
    r.removeFromTop (8);
    soloButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    openingButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (12);
    hint.setBounds (r.removeFromTop (120));
    placeTags();
}

void PlayStylePanel::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::accent);
    g.fillRect (playStyleTab.getX(), playStyleTab.getBottom() + 2, playStyleTab.getWidth(), 2);
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (side.toFloat(), 6.0f);

    // The instruments with no piece in the picture: lit while the block plays them, outlined while chosen.
    const auto used = usedPieces();
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto p : chipPieces)
    {
        const auto r = chipOf (p).reduced (2, 0).toFloat();
        const auto plays = std::find (used.begin(), used.end(), p) != used.end();
        const auto isChosen = std::find (chosen.begin(), chosen.end(), p) != chosen.end();
        g.setColour (plays ? juce::Colour (0xff2a1418) : juce::Colour (0xff141113));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (isChosen ? ParagonColours::accent.brighter (0.3f) : (plays ? ParagonColours::accent.withAlpha (0.45f) : ParagonColours::textDim.withAlpha (0.3f)));
        g.drawRoundedRectangle (r, 4.0f, isChosen ? 2.0f : 1.0f);
        g.setColour (plays || isChosen ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.6f));
        g.drawText (pieceTitle (p), r, juce::Justification::centred, true);
    }
}

void PlayStylePanel::mouseDown (const juce::MouseEvent& e)
{
    const auto piece = pieceAt (e.getPosition());
    if (piece < 0 || ! chipsArea.contains (e.getPosition()))
        return;
    if (e.mods.isPopupMenu())
    {
        if (std::find (chosen.begin(), chosen.end(), piece) == chosen.end())
            choose (piece);
        instrumentMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    choose (piece, e.mods.isShiftDown() || e.mods.isCommandDown());
}
