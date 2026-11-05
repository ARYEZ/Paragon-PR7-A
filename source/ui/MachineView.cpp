#include "MachineView.h"

#include "Parameters.h"
#include "ui/Finish.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"

namespace
{
    using synth::drumseq::Note;
    using synth::drumseq::Pattern;

    // The pads' labels, by slot (our own short names: the piece and, for a second articulation, which).
    const char* const padNames[] = {
        "KICK", "SIDESTICK", "SNARE", "CLAP", "RIMSHOT", "TOM 3", "CL HAT", "TOM 2", "PEDAL HAT", "TOM 1", "OPEN HAT", "CRASH",
        "RIDE", "BELL", "CRASH 2", "CHINA", "SPLASH", "HAT OPEN 2", "TAMBOURINE", "SHAKER", "RIDE EDGE", "HAT OPEN 1",
        "HAT OPEN 3", "TOM 4", "SHOULDER", "CR 1 CHOKE", "CR 2 CHOKE", "CHN CHOKE", "SPL CHOKE", "CRASH 3", "CRASH 4",
        "CRASH 5", "CRASH 6", "FX CRASH", "CHINA 2", "TOM 5",
    };
    static_assert (std::size (padNames) == (size_t) ParamIDs::numDrums);

    // The key names, in `keys`' order.
    const char* const keyNames[] = { "PLAY", "STOP", "REC", "TAP", "A", "B", "C", "D", "1-18", "19-36", "MUTE", "SOLO",
                                     "KIT", "MACHINE", "DRUM", "SEQ", "SONG", "COPY", "CLEAR", "FILL", "<", ">", "FINISH..." };
    namespace K
    {
    enum Key { play, stop, rec, tapKey, patternA, patternB, patternC, patternD, bank1, bank2, mute, solo,
               viewKit, viewMachine, viewDrum, viewSeq, song, copy, clear, fill, pagePrev, pageNext, finish, numKeys };
    }
    static_assert (std::size (keyNames) == (size_t) K::numKeys);
}

//==============================================================================
// A drum's LEVEL fader: PR7-K's wheel slot (a chrome lip, a dark well), a neon line up it as high as the level, a neon
// cap at the level. Drag to move, double-click: back to 0 dB (its default).
class MachineView::Fader final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Fader() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

    void bind (juce::RangedAudioParameter* p)
    {
        attachment.reset();
        parameter = p;
        if (p == nullptr)
            return;
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
        setTooltip (p->getName (64) + " (drag; double-click: back to its default)");
    }
    float proportion() const { return parameter != nullptr ? parameter->getValue() : 0.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto s = (float) getWidth() / 66.0f;   // its own units: 66 wide
        const auto slot = juce::Rectangle<float> (15.0f * s, 14.0f * s, 36.0f * s, (float) getHeight() - 28.0f * s);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (slot.translated (0.0f, 2.0f), 7.0f * s);
        // The slot's lip: chrome, or (pearl black, white + neon) the neon, lit along the top like chrome (0.53.0).
        if (Finish::neonRings())
        {
            g.setGradientFill (Finish::neonGradient (slot.getX(), slot.getRight()));
            g.fillRoundedRectangle (slot, 7.0f * s);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.4f), slot.getX(), slot.getY(),
                                                     juce::Colours::black.withAlpha (0.45f), slot.getX(), slot.getBottom(), false));
        }
        else
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9dde3), slot.getX(), slot.getY(), juce::Colour (0xff3e3238), slot.getX(), slot.getBottom(), false));
        g.fillRoundedRectangle (slot, 7.0f * s);
        const auto well = slot.reduced (3.0f * s);
        g.setColour (juce::Colour (0xff050405));
        g.fillRoundedRectangle (well, 5.0f * s);

        const auto travel = well.reduced (0.0f, 12.0f * s);
        g.setColour (Finish::inkDim().withAlpha (0.6f));
        for (int m = 0; m <= 10; ++m)
        {
            const auto y = travel.getBottom() - travel.getHeight() * (float) m / 10.0f;
            const auto len = (m % 5 == 0 ? 8.0f : 4.0f) * s;
            g.fillRect (juce::Rectangle<float> (slot.getX() - len - 4.0f * s, y, len, juce::jmax (1.0f, s)));
        }

        const auto capY = travel.getBottom() - travel.getHeight() * proportion();
        const auto lit = juce::Rectangle<float> (well.getCentreX() - 1.2f * s, capY, 2.4f * s, juce::jmax (0.0f, well.getBottom() - capY - 3.0f * s));
        g.setColour (ParagonColours::accent.withAlpha (0.25f));
        g.fillRect (lit.expanded (2.5f * s, 0.0f));
        g.setColour (ParagonColours::accent);
        g.fillRect (lit);

        const auto cap = juce::Rectangle<float> (slot.getWidth() + 16.0f * s, 26.0f * s).withCentre ({ slot.getCentreX(), capY });
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::jmax (1, (int) (7 * s)), { 0, (int) (3 * s) }).drawForRectangle (g, cap.toNearestInt());
        ParagonLookAndFeel::drawPianoKey (g, cap, 3.5f * s, 2.2f * s, false, isMouseOverOrDragging());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 10.0f * s, 1.8f * s));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (attachment == nullptr)
            return;
        attachment->beginGesture();
        startValue = proportion();
        startY = e.position.y;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (attachment == nullptr)
            return;
        const auto s = (float) getWidth() / 66.0f;
        const auto travel = juce::jmax (20.0f, (float) getHeight() - 58.0f * s);
        const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
        const auto v = juce::jlimit (0.0f, 1.0f, startValue - (e.position.y - startY) / travel * fine);
        attachment->setValueAsPartOfGesture (parameter->convertFrom0to1 (v));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (attachment != nullptr)
            attachment->endGesture();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture (parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startValue = 0.0f, startY = 0.0f;
};

//==============================================================================
// A drum's pad: a big black key in a bezel (chrome, or the neon on pearl black and white + neon). Clicked, it chooses the drum and plays it; it lights up as the drum
// sounds (from anything: the beat, the DAW, a groove), and its stripe is lit while it's the chosen drum.
class MachineView::Pad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onHit, onMenu;

    void setLight (float newLight)
    {
        if (std::abs (newLight - light) < 0.02f && (newLight > 0.0f) == (light > 0.0f))
            return;
        light = newLight;
        repaint();
    }
    float getLight() const noexcept { return light; }
    void setChosen (bool c)
    {
        if (c != chosen)
        {
            chosen = c;
            repaint();
        }
    }
    bool isChosen() const noexcept { return chosen; }

    void paint (juce::Graphics& g) override
    {
        const auto s = (float) getWidth() / 110.0f;   // its own units: 110 wide (the pad's 78 and room for the glow)
        const auto area = getLocalBounds().toFloat().reduced (16.0f * s);
        // (0.53.1: no glow outside the bezel any more: its stepped rings read as a halo round the neon bezel. The key
        // lights up inside and its stripe is lit instead.)
        // The chrome bezel and the key, drawn once for each look (pressed, hovered) at this size and scale (0.40.0: they
        // were most of a glowing pad's 30 Hz redraw).
        const auto look = (pressed ? 1 : 0) + (isMouseOver() ? 2 : 0);
        juce::Rectangle<float> well;
        keyCache[(size_t) look].draw (g, getLocalBounds(), [&] (juce::Graphics& kg)
        {
            const auto w = ParagonLookAndFeel::drawChromeBezel (kg, area, 9.0f * s);
            ParagonLookAndFeel::drawPianoKey (kg, w.reduced (1.5f * s), 7.0f * s, 4.5f * s, look & 1, (look & 2) != 0);
        });
        well = area.reduced (4.5f);   // (the bezel's well, exactly as drawChromeBezel works it out)
        if (light > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.2f * light));
            g.fillRoundedRectangle (well.reduced (7.0f * s), 6.0f * s);
        }
        const auto stripe = juce::Rectangle<float> (area.getWidth() * 0.5f, 4.0f * s).withCentre ({ area.getCentreX(), area.getY() + 14.0f * s });
        if (chosen || light > 0.3f)
            ParagonLookAndFeel::drawNeonBar (g, stripe, true);
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (stripe, 2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        pressed = true;
        repaint();
        if (onHit)
            onHit();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        pressed = false;
        repaint();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    float light = 0.0f;
    bool chosen = false, pressed = false;
    std::array<PaintCache, 4> keyCache;
};

//==============================================================================
MachineView::MachineView (ParagonX1AudioProcessor& p) : processor (p)
{
    setOpaque (true);
    auto& state = processor.getParameters();
    const auto knob = [this] (juce::Slider& k)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        k.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (k);
    };
    const auto attach = [&state] (juce::Slider& k, const juce::String& id)
    {
        auto a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, k);
        if (auto* parameter = state.getParameter (id))
            k.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        return a;
    };
    for (auto* k : { &tempo, &swing, &master })
        knob (*k);
    fixedAttachments.push_back (attach (tempo, ParamIDs::machineTempo));
    fixedAttachments.push_back (attach (swing, ParamIDs::shuffle));
    fixedAttachments.push_back (attach (master, ParamIDs::outputGain));
    tempo.setTooltip ("TEMPO: the beat's tempo when PLAY runs it on its own (while the DAW plays, the DAW's tempo leads). "
                      "Double-click: 120 BPM.");
    swing.setTooltip ("SWING (the sequencer's SHUFFLE): every second step later, up to half a step (the screen shows it as "
                      "a drum machine's swing, 50% straight to 75%).");
    master.setTooltip ("MASTER: the plugin's OUTPUT.");

    for (int c = 0; c < numColumns; ++c)
    {
        tunes[(size_t) c] = std::make_unique<juce::Slider>();
        decays[(size_t) c] = std::make_unique<juce::Slider>();
        knob (*tunes[(size_t) c]);
        knob (*decays[(size_t) c]);
        faders[(size_t) c] = std::make_unique<Fader>();
        addAndMakeVisible (*faders[(size_t) c]);
        pads[(size_t) c] = std::make_unique<Pad>();
        pads[(size_t) c]->onHit = [this, c]
        {
            const auto slot = slotOfColumn (c);
            if (onChoose)
                onChoose (slot);
            processor.previewDrum (slot);
        };
        pads[(size_t) c]->onMenu = [this, c] { showPadMenu (c); };
        addAndMakeVisible (*pads[(size_t) c]);
    }

    for (int k = 0; k < K::numKeys; ++k)
    {
        auto key = std::make_unique<juce::TextButton> (keyNames[k]);
        key->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        addAndMakeVisible (*key);
        keys.push_back (std::move (key));
    }
    const auto on = [this] (K::Key k, std::function<void()> f, const juce::String& tip)
    {
        keys[(size_t) k]->onClick = std::move (f);
        keys[(size_t) k]->setTooltip (tip);
    };
    on (K::play, [this] { play(); }, "PLAY: the beat from its first step (the DRUM SEQUENCER on, following the song), at TEMPO. "
                                  "While the DAW plays, the beat follows the DAW.");
    on (K::stop, [this] { stop(); }, "STOP: stops PLAY (and ends a REC take).");
    on (K::rec, [this] { toggleRecord(); }, "REC: while the beat runs, the drums you play (pads, the DAW, a MIDI controller) "
                                         "are written into the pattern on their nearest steps. Off again (or STOP): one undo step.");
    on (K::tapKey, [this] { tap (juce::Time::getMillisecondCounterHiRes()); }, "TAP: click in time to set TEMPO.");
    for (int pattern = 0; pattern < 4; ++pattern)
        on ((K::Key) (K::patternA + pattern), [this, pattern] { choosePattern (pattern); },
            "Pattern " + synth::drumseq::patternNames[pattern] + " (after COPY: the pattern playing copied here first)");
    on (K::bank1, [this] { setBank (0); }, "Drums 1-18 on the columns");
    on (K::bank2, [this] { setBank (1); }, "Drums 19-36 on the columns");
    on (K::mute, [this]
        {
            const auto id = ParamIDs::drum (chosenSlot(), "Mute");
            setParameter (id, processor.getParameters().getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f);
        }, "MUTE the chosen drum");
    on (K::solo, [this]
        {
            const auto id = ParamIDs::drum (chosenSlot(), "Solo");
            setParameter (id, processor.getParameters().getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f);
        }, "SOLO the chosen drum");
    on (K::viewKit, [this] { if (onView) onView (OtherView::kit); }, "The KIT VIEW");
    on (K::viewMachine, [this] { refreshKeys(); }, "THE DRUM MACHINE (this view)");
    on (K::viewDrum, [this] { if (onView) onView (OtherView::drum); }, "The chosen drum's sound (the DRUM view)");
    on (K::viewSeq, [this] { if (onView) onView (OtherView::sequencer); }, "The DRUM SEQUENCER's grid");
    on (K::song, [this]
        {
            const auto isSong = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::dseqTrigger)->load()) == 0;
            setParameter (ParamIDs::dseqTrigger, isSong ? 1.0f : 0.0f);
        }, "SONG (lit): the beat follows the song (the DAW, or PLAY). Off: KEY, the four keys under the kit start the patterns.");
    on (K::copy, [this] { armCopy (! copyArmed); }, "COPY, then a pattern key: the pattern playing copied there");
    keys[(size_t) K::clear]->onClick = [this] { clearChosen (juce::ModifierKeys::currentModifiers.isShiftDown()); };
    keys[(size_t) K::clear]->setTooltip ("CLEAR: the chosen drum's notes in the pattern (SHIFT: the whole pattern). One undo step.");
    on (K::fill, [this] { fillChosen(); }, "FILL: the chosen drum on every step shown");
    on (K::pagePrev, [this] { setStepPage (stepPage - 1); }, "The pattern's steps before these");
    on (K::pageNext, [this] { setStepPage (stepPage + 1); }, "The pattern's steps after these");
    on (K::finish, [this] { showFinishMenu(); }, "FINISH: the machine's own finish (pearl black or pearl white + neon: black keys and knobs in neon pink-to-orange rings; pearl white: chrome rings)");

    for (int s = 0; s < numSteps; ++s)
    {
        stepKeys[(size_t) s] = std::make_unique<juce::TextButton>();
        stepKeys[(size_t) s]->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        stepKeys[(size_t) s]->onClick = [this, s] { toggleStep (s); };
        addAndMakeVisible (*stepKeys[(size_t) s]);
    }

    bindColumns();
    startTimerHz (30);
}

MachineView::~MachineView() = default;

int MachineView::slotOfColumn (int column) const noexcept
{
    return processor.getMachinePad (bank * numColumns + juce::jlimit (0, numColumns - 1, column));
}

void MachineView::assignPad (int column, int slot)
{
    processor.setMachinePad (bank * numColumns + juce::jlimit (0, numColumns - 1, column), slot);
    update();   // the columns rebound
}

void MachineView::showPadMenu (int column)
{
    const auto current = slotOfColumn (column);
    juce::PopupMenu menu;
    menu.addSectionHeader ("THE DRUM ON THIS PAD (IT SWAPS WITH ITS OWN PAD)");
    for (int slot = 0; slot < ParamIDs::numDrums; ++slot)
    {
        const auto place = [this, slot]
        {
            for (int i = 0; i < ParamIDs::numDrums; ++i)
                if (processor.getMachinePad (i) == slot)
                    return i;
            return -1;
        }();
        const auto where = place >= 0 ? (place < numColumns ? "  (bank 1-18)" : "  (bank 19-36)") : "";
        menu.addItem (slot + 1, nameOf (slot) + where, true, slot == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Every pad back in the kit's order");
    juce::Component::SafePointer<MachineView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pads[(size_t) column].get()), [safe, column] (int chosen)
    {
        if (safe == nullptr || chosen == 0)
            return;
        if (chosen == 1000)
        {
            safe->processor.resetMachinePads();
            safe->update();
        }
        else
            safe->assignPad (column, chosen - 1);
    });
}

void MachineView::mouseDown (const juce::MouseEvent& e)
{
    // A drum's name: right-click to put another drum on its pad.
    if (e.mods.isPopupMenu() && namesArea.contains (e.getPosition()))
        for (int c = 0; c < numColumns; ++c)
            if (at (column (c) - 8, 778, 102, 16).contains (e.position))
                showPadMenu (c);
}

juce::String MachineView::nameOf (int slot)
{
    return slot >= 0 && slot < (int) std::size (padNames) ? padNames[slot] : "";
}

void MachineView::showFinishMenu()
{
    juce::PopupMenu menu;
    for (int t = 0; t < Finish::names.size(); ++t)
        menu.addItem (t + 1, Finish::names[t].toUpperCase(), true, (int) Finish::getMachine() == t);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keys[(size_t) K::finish].get()),
                        [safe = juce::Component::SafePointer<MachineView> (this)] (int id)
                        {
                            if (id > 0 && safe != nullptr)
                                safe->setFinish ((Finish::Type) (id - 1));
                        });
}

void MachineView::setFinish (Finish::Type type, bool remember)
{
    Finish::setMachine (type, remember);
    repaint();
}

juce::TextButton& MachineView::getKey (const juce::String& name)
{
    for (int k = 0; k < K::numKeys; ++k)
        if (name == keyNames[k])
            return *keys[(size_t) k];
    jassertfalse;
    return *keys.front();
}

juce::Component& MachineView::getPad (int c) { return *pads[(size_t) c]; }
juce::Component& MachineView::getFader (int c) { return *faders[(size_t) c]; }

void MachineView::setParameter (const juce::String& id, float value)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void MachineView::bindColumns()
{
    auto& state = processor.getParameters();
    columnAttachments.clear();
    for (int c = 0; c < numColumns; ++c)
    {
        const auto slot = slotOfColumn (c);
        const auto name = nameOf (slot);
        const auto bindKnob = [&] (juce::Slider& k, const juce::String& id, const juce::String& what)
        {
            columnAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, k));
            if (auto* parameter = state.getParameter (id))
                k.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
            k.setTooltip (name + " " + what);
        };
        bindKnob (*tunes[(size_t) c], ParamIDs::drum (slot, "Pitch"), "TUNE: its pitch, in semitones (the DRUM view's PITCH)");
        bindKnob (*decays[(size_t) c], ParamIDs::drum (slot, "Decay"), "DECAY: how long it rings");
        faders[(size_t) c]->bind (state.getParameter (ParamIDs::drum (slot, "Level")));
        pads[(size_t) c]->setTooltip (name + ": click to choose it and hear it (its steps below). Right-click: put another drum "
                                      "on this pad.");
    }
}

void MachineView::setBank (int newBank)
{
    newBank = juce::jlimit (0, 1, newBank);
    if (newBank == bank)
        return;
    bank = newBank;
    bindColumns();
    faceCache.invalidate();
    for (auto& pad : pads)
        pad->setLight (0.0f);
    shownChosen = -1;
    update();
    repaint();
}

int MachineView::shownPattern() const
{
    return juce::jlimit (0, synth::drumseq::numPatterns - 1,
                         juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::dseqPattern)->load()));
}

int MachineView::chosenSlot() const
{
    return juce::jlimit (0, ParamIDs::numDrums - 1, getChosen ? getChosen() : 0);
}

int MachineView::numStepPages() const
{
    const auto length = juce::roundToInt (processor.getDrumSeqSettings().lengthSteps());
    return juce::jmax (1, (length + numSteps - 1) / numSteps);
}

void MachineView::setStepPage (int page)
{
    page = juce::jlimit (0, numStepPages() - 1, page);
    if (page != stepPage)
    {
        stepPage = page;
        update();
        repaint (stepLabels);
    }
}

bool MachineView::isStepOn (int step) const
{
    const auto at = stepPage * numSteps + step;
    const auto slot = chosenSlot();
    for (const auto& n : processor.getModMatrix().drumPatterns[(size_t) shownPattern()].notes)
        if (n.row == slot && std::abs (n.start - at) < 0.5)
            return true;
    return false;
}

void MachineView::editPattern (const std::function<void (Pattern&)>& edit)
{
    auto m = processor.getModMatrix();
    auto& pattern = m.drumPatterns[(size_t) shownPattern()];
    const auto before = pattern;
    edit (pattern);
    if (pattern == before)
        return;
    processor.setModMatrix (m);
    processor.modMatrixEditFinished();   // one undo step
    update();
}

void MachineView::toggleStep (int step)
{
    const auto at = stepPage * numSteps + step;
    if (at >= juce::roundToInt (processor.getDrumSeqSettings().lengthSteps()))
        return;
    const auto slot = chosenSlot();
    const auto wasOn = isStepOn (step);
    editPattern ([&] (Pattern& p)
                 {
                     if (wasOn)
                         p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(),
                                                        [&] (const Note& n) { return n.row == slot && std::abs (n.start - at) < 0.5; }),
                                        p.notes.end());
                     else if ((int) p.notes.size() < synth::drumseq::maxNotes)
                     {
                         Note n;
                         n.row = slot;
                         n.start = at;
                         p.notes.push_back (n);
                     }
                 });
    if (! wasOn)
        processor.previewDrum (slot);
}

void MachineView::clearChosen (bool wholePattern)
{
    const auto slot = chosenSlot();
    editPattern ([&] (Pattern& p)
                 {
                     if (wholePattern)
                         p.notes.clear();
                     else
                         p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return n.row == slot; }),
                                        p.notes.end());
                 });
}

void MachineView::fillChosen()
{
    const auto slot = chosenSlot();
    const auto length = juce::roundToInt (processor.getDrumSeqSettings().lengthSteps());
    editPattern ([&] (Pattern& p)
                 {
                     for (int s = 0; s < numSteps; ++s)
                     {
                         const auto at = stepPage * numSteps + s;
                         if (at >= length || (int) p.notes.size() >= synth::drumseq::maxNotes)
                             break;
                         const auto there = std::any_of (p.notes.begin(), p.notes.end(),
                                                         [&] (const Note& n) { return n.row == slot && std::abs (n.start - at) < 0.5; });
                         if (! there)
                         {
                             Note n;
                             n.row = slot;
                             n.start = at;
                             p.notes.push_back (n);
                         }
                     }
                 });
}

void MachineView::play()
{
    processor.setMachinePlaying (true);
    update();
}

void MachineView::stop()
{
    processor.setMachinePlaying (false);
    update();
}

void MachineView::toggleRecord()
{
    processor.setMachineRecording (! processor.isMachineRecording());
    update();
}

void MachineView::tap (double nowMs)
{
    if (! taps.empty() && nowMs - taps.back() > 2000.0)
        taps.clear();   // a pause: a new tempo
    taps.push_back (nowMs);
    if (taps.size() > 5)
        taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const auto beat = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        if (beat > 0.0)
            setParameter (ParamIDs::machineTempo, (float) juce::jlimit (40.0, 300.0, 60000.0 / beat));
    }
}

void MachineView::armCopy (bool armed)
{
    copyArmed = armed;
    refreshKeys();
}

void MachineView::choosePattern (int pattern)
{
    pattern = juce::jlimit (0, synth::drumseq::numPatterns - 1, pattern);
    if (copyArmed)
    {
        copyArmed = false;
        if (const auto from = shownPattern(); from != pattern)
        {
            auto m = processor.getModMatrix();
            m.drumPatterns[(size_t) pattern] = m.drumPatterns[(size_t) from];
            processor.setModMatrix (m);
            processor.modMatrixEditFinished();
        }
    }
    setParameter (ParamIDs::dseqPattern, (float) pattern);
    update();
}

double MachineView::stepsPerBar() const
{
    const auto beatsPerBar = processor.getHostNumerator() * 4.0 / juce::jmax (1, processor.getHostDenominator());
    return juce::jmax (1.0, beatsPerBar / processor.getDrumSeqSettings().beatsPerStep());
}

juce::String MachineView::getScreenText() const
{
    auto& state = processor.getParameters();
    const auto& kit = processor.getDrumKit();
    const auto position = kit.getSequencerPosition();
    const auto running = position >= 0.0;
    juce::String where;
    if (running)
    {
        const auto perBar = stepsPerBar();
        where = "BAR " + juce::String ((int) std::floor (position / perBar) + 1) + " . STEP "
              + juce::String ((int) std::floor (std::fmod (position, perBar)) + 1);
    }
    else
        where = "STOPPED";
    if (processor.isMachineRecording())
        where = "REC  " + where;
    const auto daw = processor.isDawPlaying();
    const auto bpm = daw ? processor.getHostBpm() : (double) state.getRawParameterValue (ParamIDs::machineTempo)->load();
    const auto swingPercent = 50 + juce::roundToInt (25.0f * state.getRawParameterValue (ParamIDs::shuffle)->load());
    return "PATTERN " + synth::drumseq::patternNames[shownPattern()] + "|" + juce::String (processor.getHostNumerator()) + "/"
         + juce::String (processor.getHostDenominator()) + "|" + where + "|" + processor.getCurrentRig().name.toUpperCase() + "|"
         + juce::String (bpm, 1) + "|" + (daw ? "BPM  DAW" : "BPM") + "|SWING " + juce::String (swingPercent) + "%";
}

void MachineView::refreshKeys()
{
    auto& state = processor.getParameters();
    const auto value = [&state] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    const auto running = processor.getDrumKit().getSequencerPosition() >= 0.0;
    const auto set = [this] (K::Key k, bool lit) { keys[(size_t) k]->setToggleState (lit, juce::dontSendNotification); };
    set (K::play, processor.isMachinePlaying() || (running && processor.isDawPlaying()));
    set (K::stop, false);
    set (K::rec, processor.isMachineRecording());
    for (int pattern = 0; pattern < 4; ++pattern)
        set ((K::Key) (K::patternA + pattern), shownPattern() == pattern);
    set (K::bank1, bank == 0);
    set (K::bank2, bank == 1);
    const auto slot = chosenSlot();
    set (K::mute, value (ParamIDs::drum (slot, "Mute")) > 0.5f);
    set (K::solo, value (ParamIDs::drum (slot, "Solo")) > 0.5f);
    set (K::viewMachine, true);
    set (K::song, juce::roundToInt (value (ParamIDs::dseqTrigger)) == 0);
    set (K::copy, copyArmed);
    keys[(size_t) K::pagePrev]->setEnabled (stepPage > 0);
    keys[(size_t) K::pageNext]->setEnabled (stepPage + 1 < numStepPages());
}

void MachineView::update()
{
    // The pads' drums (0.39.0): rebound when they change (a pad picked, a project loaded).
    {
        std::array<int, numColumns> slots {};
        for (int c = 0; c < numColumns; ++c)
            slots[(size_t) c] = slotOfColumn (c);
        if (slots != shownSlots)
        {
            shownSlots = slots;
            bindColumns();
            shownChosen = -1;
            repaint (namesArea);
        }
    }
    if (processor.isMachineRecording())
        processor.mergeMachineNotes();
    const auto& kit = processor.getDrumKit();

    // The pads glow as their drums sound; the chosen one's stripe.
    const auto chosen = chosenSlot();
    for (int c = 0; c < numColumns; ++c)
    {
        const auto age = kit.getHitAge (slotOfColumn (c));
        pads[(size_t) c]->setLight (age >= 0.0f ? (1.0f - age) * (1.0f - age) : 0.0f);
        pads[(size_t) c]->setChosen (slotOfColumn (c) == chosen);
    }
    if (chosen != shownChosen)
    {
        shownChosen = chosen;
        repaint (namesArea);
        repaint (stepLabels);
    }

    // The steps: the page the beat is on while it runs (of the pattern shown).
    const auto position = kit.getSequencerPosition();
    const auto length = juce::roundToInt (processor.getDrumSeqSettings().lengthSteps());
    auto playStep = -1;
    if (position >= 0.0 && kit.getSequencerPattern() == shownPattern())
    {
        playStep = juce::jlimit (0, juce::jmax (0, length - 1), (int) std::floor (position));
        stepPage = playStep / numSteps;
    }
    stepPage = juce::jlimit (0, numStepPages() - 1, stepPage);
    for (int s = 0; s < numSteps; ++s)
    {
        auto& key = *stepKeys[(size_t) s];
        key.setEnabled (stepPage * numSteps + s < length);
        key.setToggleState (isStepOn (s), juce::dontSendNotification);
    }
    const auto playOnPage = playStep >= 0 && playStep / numSteps == stepPage ? playStep % numSteps : -1;
    const auto labelState = playOnPage + 100 * stepPage + 10000 * numStepPages();
    if (labelState != shownPlayStep)
    {
        shownPlayStep = labelState;
        repaint (stepLabels);
    }

    refreshKeys();

    if (const auto text = getScreenText(); text != shownScreen)
    {
        shownScreen = text;
        repaint (screenArea);
    }

    // MASTER's meter: OUTPUT's peaks (its own column is hidden behind the machine), falling 24 dB a second.
    const auto peakDb = juce::Decibels::gainToDecibels (processor.getOutputMeter().takePeak(), -60.0f);
    meterLevel = juce::jmax ((peakDb + 60.0f) / 66.0f, meterLevel - 24.0f / 66.0f / 30.0f, 0.0f);
    if (std::abs (meterLevel - shownMeter) > 0.004f)
    {
        shownMeter = meterLevel;
        repaint (meterArea);
    }
}

void MachineView::timerCallback()
{
    if (isShowing())
        update();
}

//==============================================================================
juce::Rectangle<float> MachineView::at (float x, float y, float w, float h) const
{
    return { origin.x + x * scale, origin.y + y * scale, w * scale, h * scale };
}

void MachineView::resized()
{
    scale = juce::jmin ((float) getWidth() / refWidth, (float) getHeight() / refHeight);
    origin = { ((float) getWidth() - refWidth * scale) * 0.5f, ((float) getHeight() - refHeight * scale) * 0.5f };
    faceCache.invalidate();
    const auto place = [this] (juce::Component& c, float x, float y, float w, float h) { c.setBounds (at (x, y, w, h).toNearestInt()); };

    place (tempo, 1170, 40, 96, 96);
    place (swing, 1290, 40, 96, 96);
    for (int i = 0; i < 4; ++i)
        place (*keys[(size_t) (K::play + i)], 1160.0f + (float) i * 64.0f, 176, 58, 34);
    place (master, 1460, 40, 110, 110);
    for (int i = 0; i < 4; ++i)
        place (*keys[(size_t) (K::viewKit + i)], 1654, 74.0f + (float) i * 40.0f, 196, 32);

    for (int c = 0; c < numColumns; ++c)
    {
        place (*tunes[(size_t) c], column (c) + 14, 280, 56, 56);
        place (*decays[(size_t) c], column (c) + 14, 366, 56, 56);
        place (*faders[(size_t) c], column (c) + 10, 432, 66, 254);
        place (*pads[(size_t) c], column (c) - 12, 678, 110, 110);
    }
    place (*keys[K::bank1], 1654, 290, 94, 34);
    place (*keys[K::bank2], 1756, 290, 94, 34);
    place (*keys[K::mute], 1654, 370, 94, 34);
    place (*keys[K::solo], 1756, 370, 94, 34);
    for (int i = 0; i < 4; ++i)
        place (*keys[(size_t) (K::patternA + i)], 1654.0f + (float) (i % 2) * 102.0f, 500.0f + (float) (i / 2) * 50.0f, 94, 40);
    for (int i = 0; i < 4; ++i)
        place (*keys[(size_t) (K::song + i)], 1654.0f + (float) (i % 2) * 102.0f, 660.0f + (float) (i / 2) * 46.0f, 94, 36);
    for (int s = 0; s < numSteps; ++s)
        place (*stepKeys[(size_t) s], stepX (s), 860, 80, 50);
    place (*keys[K::pagePrev], 1654, 866, 94, 38);
    place (*keys[K::pageNext], 1756, 866, 94, 38);
    place (*keys[K::finish], 1654, 768, 196, 32);
    // The lettering kept clear of each key's LED (paintOverChildren: centred 12 units in, 3 wide, its glow 4 more).
    for (auto& key : keys)
        key->getProperties().set (ParagonLookAndFeel::ledClearanceProperty, 19.0 * scale);

    screenArea = at (480, 34, 640, 190).toNearestInt();
    meterArea = at (1580, 38, 28, 178).toNearestInt();
    stepLabels = at (30, 816, 1590, 130).toNearestInt().withTrimmedTop (0);
    namesArea = at (30, 776, 1590, 20).toNearestInt();
}

void MachineView::stamp (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float size,
                         juce::Colour colour) const
{
    g.setFont (juce::FontOptions (size * scale, juce::Font::bold));
    ParagonLookAndFeel::drawStampedText (g, text, r, j, colour);
}

void MachineView::paintFace (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    if (! grain.isValid() || grain.getWidth() != getWidth() || grain.getHeight() != getHeight())
        grain = Finish::makeSatinGrain (juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), 7);
    const auto chassis = at (0, 0, refWidth, refHeight).reduced (5.0f);
    const auto plate = chassis.reduced (9.0f).withTrimmedBottom (5.0f);
    PedalBody::paintUnit (g, chassis, plate, [this] (juce::Graphics& gg) { gg.drawImage (grain, getLocalBounds().toFloat()); },
                          { 20.0f, 12.0f, 7.0f, 0.4f });
    PedalBody::paintBadge (g, at (44, 40, 380, 120), "ADVANCED DRUM COMPUTER");

    const auto ink = Finish::ink();
    stamp (g, "TEMPO", at (1170, 140, 96, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "SWING", at (1290, 140, 96, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "MASTER", at (1460, 154, 110, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "VIEW", at (1654, 48, 196, 16), juce::Justification::centred, 12.0f, ink);

    g.setColour (Finish::printedRed().withAlpha (0.5f));
    g.fillRect (at (34, 240, 1832, 1.4f));
    stamp (g, bank == 0 ? "DRUMS 1-18" : "DRUMS 19-36", at (44, 248, 400, 18), juce::Justification::centredLeft, 13.0f, ink);
    stamp (g, "TUNE", at (20, 301, 44, 14), juce::Justification::centredLeft, 10.0f, ink);
    stamp (g, "DECAY", at (20, 387, 44, 14), juce::Justification::centredLeft, 10.0f, ink);
    stamp (g, "LEVEL", at (20, 552, 44, 14), juce::Justification::centredLeft, 10.0f, ink);
    stamp (g, "BANK", at (1654, 268, 196, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "CHOSEN DRUM", at (1654, 348, 196, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "PATTERN", at (1654, 478, 196, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "SEQUENCE", at (1654, 638, 196, 16), juce::Justification::centred, 12.0f, ink);
    stamp (g, "STEPS", at (1654, 844, 196, 16), juce::Justification::centred, 12.0f, ink);
    g.setColour (Finish::inkDim().withAlpha (0.35f));
    g.fillRect (at (1628, 260, 1.0f, 700));

    g.setColour (Finish::printedRed().withAlpha (0.5f));
    g.fillRect (at (34, 812, 1580, 1.4f));
    g.setColour (Finish::inkDim().withAlpha (0.4f));
    for (int q = 1; q < 4; ++q)
        g.fillRect (at (stepX (q * 4) - 12.0f, 860, 1.0f, 50));
}

void MachineView::paint (juce::Graphics& g)
{
    // Its own finish (pearl black by default: black keys and knobs in neon rings), whatever finish the rest of the plugin is in (the face and every
    // control on it: cleared again in paintOverChildren, after they've drawn).
    Finish::paintAs (Finish::getMachine());   // (0.43.0: its own finish, chosen with FINISH)
    if (shownFinish != Finish::getMachine())
    {
        shownFinish = Finish::getMachine();
        faceCache.invalidate();
        grain = {};
    }
    faceCache.draw (g, getLocalBounds(), [this] (juce::Graphics& fg) { paintFace (fg); });

    // What changes: the screen, the meter, the drums' names (the chosen one red), the steps' line and numbers. Each
    // only when the area being redrawn reaches it (0.40.0: a pad's glow or the meter redrew the screen 30 times a second).
    const auto reaches = [&g] (juce::Rectangle<int> area) { return g.clipRegionIntersects (area); };
    if (reaches (screenArea))
        paintScreen (g, at (480, 34, 640, 190));
    if (reaches (meterArea))
        paintMeter (g, at (1586, 44, 16, 166));

    const auto chosen = chosenSlot();
    if (reaches (namesArea))
        for (int c = 0; c < numColumns; ++c)
            if (const auto where = at (column (c) - 8, 778, 102, 16); reaches (where.getSmallestIntegerContainer()))
            {
                const auto slot = slotOfColumn (c);
                stamp (g, nameOf (slot), where, juce::Justification::centred, 10.5f, slot == chosen ? Finish::printedRed() : Finish::ink());
            }
    if (! reaches (stepLabels))
        return;

    const auto pages = numStepPages();
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    auto title = "STEPS" + dot + nameOf (chosen) + dot + synth::seq::divisionNames[processor.getDrumSeqSettings().division];
    if (pages > 1)
        title << dot << juce::String (stepPage + 1) << " OF " << juce::String (pages);
    stamp (g, title, at (44, 822, 900, 18), juce::Justification::centredLeft, 13.0f, Finish::ink());

    const auto position = processor.getDrumKit().getSequencerPosition();
    const auto playStep = position >= 0.0 && processor.getDrumKit().getSequencerPattern() == shownPattern() ? (int) std::floor (position) : -1;
    for (int s = 0; s < numSteps; ++s)
    {
        const auto step = stepPage * numSteps + s;
        const auto playing = step == playStep;
        stamp (g, juce::String (step + 1), at (stepX (s), 914, 80, 16), juce::Justification::centred, 11.0f,
               playing ? Finish::printedRed() : Finish::inkDim());
        if (playing)
            ParagonLookAndFeel::drawNeonBar (g, at (stepX (s) + 28, 934, 24, 4), true);
    }
}

void MachineView::paintOverChildren (juce::Graphics& g)
{
    // Each labelled key's LED, at its left end: lit while it's on (on the neon keys the lettering alone barely shows it).
    for (const auto& key : keys)
    {
        const auto r = key->getBounds().toFloat();
        if (r.isEmpty() || ! key->isVisible())
            continue;
        const auto led = juce::Rectangle<float> (3.0f * scale, r.getHeight() * 0.42f).withCentre ({ r.getX() + 12.0f * scale, r.getCentreY() });
        if (key->getToggleState())
            ParagonLookAndFeel::drawNeonBar (g, led, true);
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (led, 1.5f * scale);
        }
    }
    Finish::paintAs (std::nullopt);
}

void MachineView::paintMeter (juce::Graphics& g, juce::Rectangle<float> area) const
{
    constexpr int segments = 14;
    const auto h = area.getHeight() / (float) segments;
    const auto litAt = [&] (int i) { return (float) i / (float) segments < meterLevel - 1.0e-4f; };
    // One soft glow behind the lit segments together (0.53.3: each segment's own glow, wider than the gaps between
    // them, piled up into a red cloud round the whole meter).
    int lit = 0;
    while (lit < segments && litAt (lit))
        ++lit;
    if (lit > 0)
    {
        const auto column = juce::Rectangle<float> (area.getX(), area.getBottom() - (float) lit * h, area.getWidth(), (float) lit * h);
        g.setColour (juce::Colour (0xffff3a2a).withAlpha (0.16f));
        g.fillRoundedRectangle (column.expanded (h * 0.35f, h * 0.2f), h * 0.5f);
    }
    for (int i = 0; i < segments; ++i)
    {
        const auto r = juce::Rectangle<float> (area.getX(), area.getBottom() - (float) (i + 1) * h, area.getWidth(), h).reduced (0.0f, h * 0.18f);
        ParagonLookAndFeel::drawNeonBar (g, r, litAt (i), false);
    }
}

void MachineView::paintScreen (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto parts = juce::StringArray::fromTokens (shownScreen.isEmpty() ? getScreenText() : shownScreen, "|", {});
    const auto size = juce::jlimit (0.8f, 1.6f, area.getHeight() / 120.0f);
    const auto glass = LedDisplay::paintScreen (g, area, size);
    const auto orange = LedDisplay::ledOrange;
    auto content = glass.reduced (glass.getWidth() * 0.04f, glass.getHeight() * 0.1f);

    auto top = content.removeFromTop (content.getHeight() * 0.2f);
    g.setColour (orange);
    g.setFont (juce::FontOptions (top.getHeight() * 0.85f, juce::Font::bold));
    g.drawText (parts[0] + juce::String::fromUTF8 ("  \xc2\xb7  ") + parts[1], top, juce::Justification::centredLeft, false);
    g.drawText (parts[2], top, juce::Justification::centredRight, false);

    auto nameArea = content.removeFromTop (content.getHeight() * 0.55f);
    auto nameFont = juce::FontOptions (nameArea.getHeight() * 0.72f, juce::Font::bold);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (nameFont, parts[3]);
    if (nameWidth > nameArea.getWidth())
        nameFont = nameFont.withHeight (nameFont.getHeight() * nameArea.getWidth() / nameWidth);
    g.setFont (nameFont);
    g.setColour (orange.withAlpha (0.18f));
    for (const auto offset : { -1.5f, 1.5f })
        g.drawText (parts[3], nameArea.translated (offset, 0.0f), juce::Justification::centredLeft, false);
    g.setColour (orange);
    g.drawText (parts[3], nameArea, juce::Justification::centredLeft, false);

    // The tempo in the screen's seven-segment digits.
    const auto digitH = content.getHeight() * 0.9f, digitW = digitH * 0.58f;
    auto x = content.getX();
    for (const auto c : parts[4])
    {
        if (c == '.')
        {
            const auto d = digitH * 0.13f;
            g.setColour (orange);
            g.fillEllipse (x, content.getY() + digitH - d, d, d);
            x += d + digitH * 0.12f;
            continue;
        }
        LedDisplay::drawDigit (g, c, { x, content.getY(), digitW, digitH }, orange);
        x += digitW + digitH * 0.12f;
    }
    g.setColour (orange);
    g.setFont (juce::FontOptions (content.getHeight() * 0.45f, juce::Font::bold));
    g.drawText (parts[5], juce::Rectangle<float> (x + 6.0f, content.getY(), 160.0f, digitH), juce::Justification::centredLeft, false);
    g.drawText (parts[6], content, juce::Justification::centredRight, false);

    LedDisplay::paintGlassOverlay (g, glass, size);
}
