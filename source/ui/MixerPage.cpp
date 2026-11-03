#include "ui/MixerPage.h"

#include "MixerEffects.h"

#include "dsp/synth/Instrument.h"
#include "ui/DockPanels.h"
#include "ui/DrumSeqPage.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int propertiesWidth = 360, rowHeight = 26, stripWidth = 82, headerHeight = 44;
    using Attachment = juce::AudioProcessorValueTreeState;
    using namespace synth;

    // The buses' colour: golden orange (the reference's buses are yellow; ours stay in the family's range).
    const juce::Colour busColour = ParagonColours::drive;

    void styleToggle (juce::TextButton& b, const juce::String& tip)
    {
        DrumSeqPanel::style (b);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
    }

    // Strips: the named mic channels, the buses, then (0.23.0) the user channels.
    bool isBus (int strip) noexcept { return mixer::isBusStrip (strip); }
    bool isUser (int strip) noexcept { return mixer::isUserChannel (mixer::channelOfStrip (strip)); }
    juce::String stripName (const ParagonX1AudioProcessor& p, int strip)
    {
        return isBus (strip) ? mixer::busNames[strip - mixer::numChannels] : p.getMixerChannelName (mixer::channelOfStrip (strip));
    }
    juce::String stripId (int strip, const char* what)
    {
        return isBus (strip) ? ParamIDs::mixBus (strip - mixer::numChannels, what) : ParamIDs::mix (mixer::channelOfStrip (strip), what);
    }
}

//==============================================================================
// A channel's or a bus's strip.
class MixerPage::Strip final : public juce::Component
{
public:
    Strip (MixerPage& p, int s) : page (p), strip (s)
    {
        auto& state = page.processor.getParameters();
        const auto id = [s] (const char* what) { return stripId (s, what); };
        if (! isBus (strip))
        {
            styleToggle (bleedOn, "BLEED on: the other drums heard through this mic. Off: only the drums it's meant for.");
            bleedOn.setButtonText ("BLEED");
            bleed.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            bleed.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            bleed.setTooltip ("BLEED level: every other drum in this mic, together (their balance is in PROPERTIES).");
            addAndMakeVisible (bleedOn);
            addAndMakeVisible (bleed);
            attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Bleed"), bleed));
            buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("BleedOn"), bleedOn));
            bleed.setDoubleClickReturnValue (true, 0.0);
        }
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            auto& b = effects[(size_t) k];
            DrumSeqPanel::style (b);
            b.setTooltip ("EFFECT " + juce::String (k + 1) + ": EQ, COMPRESSOR, GATE, TRANSIENT, DRIVE, LO-FI or MOD, in order from the "
                          "top, before the pan and fader. Click to choose; its settings are in PROPERTIES > EFFECTS.");
            b.onClick = [this, k] { page.showEffectMenu (strip, k, effects[(size_t) k]); };
            addAndMakeVisible (b);
        }
        for (int k = 0; k < mixer::numSendSlots; ++k)
        {
            auto& b = sends[(size_t) k];
            DrumSeqPanel::style (b);
            b.setTooltip ("SEND " + juce::String (k + 1) + ": a copy of this " + (isBus (strip) ? "bus" : "channel")
                          + " into a bus, an FX bus (the FX page's insert buses, with their effects), the DELAY or the REVERB. "
                            "Click to choose; its levels and pans are in PROPERTIES > SENDS.");
            b.onClick = [this, k] { page.showSendMenu (strip, k, sends[(size_t) k]); };
            addAndMakeVisible (b);
        }
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setTooltip ("PAN: left to right (a balance).");
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        fader.setTooltip (isBus (strip) ? "The bus's level." : "The channel's level.");
        styleToggle (mute, isBus (strip) ? "MUTE this bus." : "MUTE this channel (its sends too, unless MENU... > POST SEND).");
        mute.setButtonText ("M");
        styleToggle (solo, isBus (strip) ? "SOLO: only the soloed buses heard (the channels aren't touched)."
                                         : "SOLO: only the soloed channels heard (the buses aren't touched).");
        solo.setButtonText ("S");
        styleToggle (invert, "POLARITY: the waveform flipped (to line a mic up with another).");
        invert.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        DrumSeqPanel::style (output);
        output.setTooltip (isBus (strip) ? "Where the bus goes: MAIN, OUT 2 to 16 (switch it on in your DAW; until then MAIN), a later "
                                           "bus, or an FX bus. The outputs stay with the project, the buses with presets."
                                         : "Where the channel goes: MAIN, OUT 2 to 16 (switch it on in your DAW; until then MAIN), a "
                                           "bus, or an FX bus. The outputs stay with the project, the buses with presets.");
        output.onClick = [this] { page.showOutputMenu (strip, output); };
        for (juce::Component* comp : { (juce::Component*) &pan, (juce::Component*) &fader, (juce::Component*) &mute,
                                       (juce::Component*) &solo, (juce::Component*) &invert, (juce::Component*) &output })
            addAndMakeVisible (comp);
        attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Pan"), pan));
        attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Level"), fader));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Mute"), mute));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Solo"), solo));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Phase"), invert));
        fader.setDoubleClickReturnValue (true, 0.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        refresh();
    }

    void setMeter (float peak)
    {
        // Falls back over about a third of a second.
        const auto shown = juce::jmax (peak, meter * 0.8f);
        if (std::abs (shown - meter) > 1.0e-4f)
        {
            meter = shown;
            repaint (meterArea());
        }
    }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void setInUse (bool u) { if (u != used) { used = u; setAlpha (used ? 1.0f : 0.45f); } }

    // The OUTPUT and SEND buttons' words (the timer: they follow undo, presets and the DAW).
    void refresh()
    {
        if (const auto text = page.describeOutput (strip); text != output.getButtonText())
            output.setButtonText (text);
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            auto& b = effects[(size_t) k];
            const auto& fx = page.processor.getModMatrix().effect (strip, k);
            if (const auto text = page.describeEffect (strip, k); text != b.getButtonText())
                b.setButtonText (text);
            b.setToggleState (fx.isLoaded() && fx.on, juce::dontSendNotification);
            b.setAlpha (fx.isLoaded() ? (fx.on ? 1.0f : 0.75f) : 0.6f);
        }
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            auto& b = effects[(size_t) k];
            DrumSeqPanel::style (b);
            b.setTooltip ("EFFECT " + juce::String (k + 1) + ": EQ, COMPRESSOR, GATE, TRANSIENT, DRIVE, LO-FI or MOD, in order from the "
                          "top, before the pan and fader. Click to choose; its settings are in PROPERTIES > EFFECTS.");
            b.onClick = [this, k] { page.showEffectMenu (strip, k, effects[(size_t) k]); };
            addAndMakeVisible (b);
        }
        for (int k = 0; k < mixer::numSendSlots; ++k)
        {
            auto& b = sends[(size_t) k];
            const auto on = page.processor.getModMatrix().send (strip, k).isOn();
            if (const auto text = page.describeSend (strip, k); text != b.getButtonText())
                b.setButtonText (text);
            b.setToggleState (on, juce::dontSendNotification);
            b.setAlpha (on ? 1.0f : 0.6f);
        }
    }

    juce::Rectangle<int> meterArea() const { return fader.getBounds().withWidth (6).translated (fader.getWidth() - 4, 0).withTrimmedBottom (20); }

    void resized() override
    {
        auto a = getLocalBounds().reduced (4, 6);
        if (! isBus (strip))
        {
            bleedOn.setBounds (a.removeFromTop (22));
            a.removeFromTop (2);
            bleed.setBounds (a.removeFromTop (36).withSizeKeepingCentre (36, 36));
        }
        else
        {
            a.removeFromTop (60);   // where a channel's BLEED is: the bus's label is drawn there
        }
        a.removeFromTop (4);
        for (auto& b : effects)
        {
            b.setBounds (a.removeFromTop (18));
            a.removeFromTop (2);
        }
        a.removeFromTop (4);
        for (auto& b : sends)
        {
            b.setBounds (a.removeFromTop (18));
            a.removeFromTop (2);
        }
        a.removeFromTop (2);
        pan.setBounds (a.removeFromTop (36).withSizeKeepingCentre (36, 36));
        a.removeFromTop (4);
        a.removeFromBottom (24);   // the name
        output.setBounds (a.removeFromBottom (22));
        a.removeFromBottom (4);
        auto buttonsRow = a.removeFromBottom (22);
        const auto w = buttonsRow.getWidth() / 3;
        mute.setBounds (buttonsRow.removeFromLeft (w).reduced (1, 0));
        solo.setBounds (buttonsRow.removeFromLeft (w).reduced (1, 0));
        invert.setBounds (buttonsRow.reduced (1, 0));
        a.removeFromBottom (4);
        fader.setBounds (a);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto colour = isBus (strip) ? busColour : ParagonColours::accent;
        g.setColour (selected ? colour.withAlpha (0.12f) : isBus (strip) ? juce::Colour (0xff120d0a) : juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
        g.setColour (selected ? colour : isBus (strip) ? busColour.withAlpha (0.35f) : ParagonColours::panelEdge);
        g.drawRoundedRectangle (r.reduced (1.5f), 4.0f, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (selected || isBus (strip) ? colour : ParagonColours::text);
        g.drawText (stripName (page.processor, strip), getLocalBounds().removeFromBottom (28), juce::Justification::centred, false);
        if (isBus (strip))
        {
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.setColour (busColour.withAlpha (0.55f));
            g.drawText (juce::String (strip - mixer::numChannels + 1), getLocalBounds().reduced (4, 6).removeFromTop (60),
                        juce::Justification::centred, false);
        }
        // The meter beside the fader.
        const auto m = meterArea().toFloat();
        g.setColour (ParagonColours::panelEdge);
        g.fillRect (m);
        const auto db = juce::Decibels::gainToDecibels (meter, -60.0f);
        const auto h = m.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour (db > -1.0f ? ParagonColours::meterRed : colour);
        g.fillRect (m.withTop (m.getBottom() - h));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y > getHeight() - 30 || (isBus (strip) && e.y < 66))
            page.selectStrip (strip);
        // A user channel's name: right-click to rename it (0.23.0).
        if (isUser (strip) && e.y > getHeight() - 30 && e.mods.isPopupMenu())
        {
            auto* window = new juce::AlertWindow ("RENAME CHANNEL", "A name for this channel:", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", page.processor.getMixerChannelName (mixer::channelOfStrip (strip)));
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Strip> safe (this);
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
            {
                if (safe != nullptr && result == 1)
                    safe->page.processor.renameUserChannel (mixer::channelOfStrip (safe->strip), window->getTextEditorContents ("name"));
            }), true);
        }
    }

    int getStrip() const noexcept { return strip; }

private:
    MixerPage& page;
    int strip;
    juce::TextButton bleedOn, mute, solo, invert, output;
    std::array<juce::TextButton, mixer::numSendSlots> sends;
    std::array<juce::TextButton, mixer::numFxSlots> effects;
    juce::Slider bleed, pan, fader;
    std::vector<std::unique_ptr<Attachment::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<Attachment::ButtonAttachment>> buttons;
    float meter = 0.0f;
    bool selected = false, used = true;
};

//==============================================================================
// A piece's row in PROPERTIES: on, its name, its level in this channel, polarity.
class MixerPage::PieceRow final : public juce::Component
{
public:
    PieceRow (MixerPage& p, int pc) : page (p), piece (pc)
    {
        styleToggle (on, "On: this drum heard through the channel. Off: not at all.");
        on.setButtonText ("ON");
        styleToggle (invert, "Polarity: this drum's sound in this mic flipped.");
        invert.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        level.setRange (-60.0, 12.0, 0.1);
        level.setTextValueSuffix (" dB");
        level.setDoubleClickReturnValue (true, 0.0);
        level.setTooltip ("This drum's level in this channel (-60: off).");
        for (juce::Component* c : { (juce::Component*) &on, (juce::Component*) &invert, (juce::Component*) &level })
            addAndMakeVisible (c);
        const auto commit = [this] (bool final)
        {
            page.setBleed (piece, (float) level.getValue(), invert.getToggleState(), on.getToggleState());
            if (final)
                page.processor.modMatrixEditFinished();
        };
        on.onClick = [commit] { commit (true); };
        invert.onClick = [commit] { commit (true); };
        level.onValueChange = [this, commit] { if (! refreshing) commit (false); };
        level.onDragEnd = [commit] { commit (true); };
    }

    void refresh (int channel, bool direct, bool hasAudio)
    {
        refreshing = true;
        const auto& cell = page.processor.getModMatrix().bleed (channel, piece);
        on.setToggleState (cell.on, juce::dontSendNotification);
        invert.setToggleState (cell.invert, juce::dontSendNotification);
        level.setValue (cell.gainDb, juce::dontSendNotification);
        refreshing = false;
        isDirect = direct;
        audio = hasAudio;
        setAlpha (hasAudio ? 1.0f : 0.5f);
        repaint();
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (0, 2);
        on.setBounds (a.removeFromLeft (38));
        a.removeFromLeft (110);   // the name
        invert.setBounds (a.removeFromRight (26));
        a.removeFromRight (4);
        level.setBounds (a);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (10.5f, isDirect ? juce::Font::bold : juce::Font::plain));
        g.setColour (isDirect ? ParagonColours::accent : ParagonColours::text);
        g.drawText (synth::drums::pieces[(size_t) piece].name, getLocalBounds().withTrimmedLeft (44).withWidth (106), juce::Justification::centredLeft, true);
    }

    bool isDirect = false;

private:
    MixerPage& page;
    int piece;
    juce::TextButton on, invert;
    juce::Slider level;
    bool refreshing = false, audio = true;
};

//==============================================================================
// A send's editor in PROPERTIES > SENDS (the chosen strip's send `slot`): its target, POST / PRE FADER, LINK; the
// DIRECT sound's on, level and pan; the BLEED's (mic channels only: a bus's sound is all direct).
class MixerPage::SendEditor final : public juce::Component
{
public:
    static constexpr int height = 104, busHeight = 70;   // a bus's: no BLEED row

    SendEditor (MixerPage& p, int k) : page (p), slot (k)
    {
        for (int t = 0; t < mixer::sendTargetNames.size(); ++t)
            target.addItem (mixer::sendTargetNames[t], t + 1);
        target.setTooltip ("Where this send goes: a bus, an FX bus (the FX page's insert buses, with their effects), the DELAY or "
                           "the REVERB (while it's on). A bus sends only into a later bus.");
        mode.addItemList (mixer::sendModeNames, 1);
        mode.setTooltip ("POST FADER: after the channel's fader (it follows the fader and the mute). PRE FADER: before it (with "
                         "MENU... > POST SEND, it goes on while the channel is muted).");
        styleToggle (link, "LINK: DIRECT and BLEED levels move together.");
        link.setButtonText ("LINK");
        styleToggle (directOn, "The drums this mic is meant for, sent (on / off).");
        directOn.setButtonText ("ON");
        styleToggle (bleedOn, "The other drums in this mic (bleed), sent (on / off): off to compress a bus hard on the close sound alone.");
        bleedOn.setButtonText ("ON");
        for (auto* l : { &directLevel, &bleedLevel })
        {
            l->setSliderStyle (juce::Slider::LinearHorizontal);
            l->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
            l->setRange (-60.0, 12.0, 0.1);
            l->setTextValueSuffix (" dB");
            l->setDoubleClickReturnValue (true, 0.0);
        }
        directLevel.setTooltip ("The level sent (-60: none).");
        bleedLevel.setTooltip ("The bleed's level sent (-60: none).");
        for (auto* pan : { &directPan, &bleedPan })
        {
            pan->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan->setRange (-1.0, 1.0, 0.01);
            pan->setDoubleClickReturnValue (true, 0.0);
            pan->setTooltip ("The send's own pan (not the channel's).");
        }
        for (juce::Component* c : { (juce::Component*) &target, (juce::Component*) &mode, (juce::Component*) &link,
                                    (juce::Component*) &directOn, (juce::Component*) &directLevel, (juce::Component*) &directPan,
                                    (juce::Component*) &bleedOn, (juce::Component*) &bleedLevel, (juce::Component*) &bleedPan })
            addAndMakeVisible (c);
        target.onChange = [this] { if (! refreshing) commit (true); };
        mode.onChange = [this] { if (! refreshing) commit (true); };
        for (auto* b : { &link, &directOn, &bleedOn })
            b->onClick = [this] { commit (true); };
        // LINK: one level moved, the other by as much.
        directLevel.onValueChange = [this]
        {
            if (refreshing)
                return;
            if (link.getToggleState() && ! isBus (strip))
                follow (bleedLevel, directLevel.getValue() - lastDirect);
            commit (false);
        };
        bleedLevel.onValueChange = [this]
        {
            if (refreshing)
                return;
            if (link.getToggleState())
                follow (directLevel, bleedLevel.getValue() - lastBleed);
            commit (false);
        };
        for (auto* s : { &directPan, &bleedPan })
            s->onValueChange = [this] { if (! refreshing) commit (false); };
        for (auto* s : { &directLevel, &bleedLevel, &directPan, &bleedPan })
            s->onDragEnd = [this] { commit (true); };
    }

    void refresh (int forStrip)
    {
        strip = forStrip;
        const auto& d = page.processor.getModMatrix().send (strip, slot);
        refreshing = true;
        target.setSelectedId (d.target + 1, juce::dontSendNotification);
        for (int b = 0; b < mixer::numBuses; ++b)
            target.setItemEnabled (b + 2, mixer::canFeedBus (strip, b));
        mode.setSelectedId ((int) d.mode + 1, juce::dontSendNotification);
        link.setToggleState (d.link, juce::dontSendNotification);
        directOn.setToggleState (d.directOn, juce::dontSendNotification);
        bleedOn.setToggleState (d.bleedOn, juce::dontSendNotification);
        directLevel.setValue (d.directDb, juce::dontSendNotification);
        bleedLevel.setValue (d.bleedDb, juce::dontSendNotification);
        directPan.setValue (d.directPan, juce::dontSendNotification);
        bleedPan.setValue (d.bleedPan, juce::dontSendNotification);
        lastDirect = d.directDb;
        lastBleed = d.bleedDb;
        refreshing = false;
        const auto bus = isBus (strip);
        for (auto* c : { (juce::Component*) &link, (juce::Component*) &bleedOn, (juce::Component*) &bleedLevel, (juce::Component*) &bleedPan })
            c->setVisible (! bus);
        for (auto* c : { (juce::Component*) &mode, (juce::Component*) &link, (juce::Component*) &directOn, (juce::Component*) &directLevel,
                         (juce::Component*) &directPan, (juce::Component*) &bleedOn, (juce::Component*) &bleedLevel, (juce::Component*) &bleedPan })
            c->setAlpha (d.isOn() ? 1.0f : 0.4f);
        repaint();
    }

    void resized() override
    {
        auto a = getLocalBounds().withTrimmedTop (4);
        auto top = a.removeFromTop (24);
        top.removeFromLeft (62);
        target.setBounds (top.removeFromLeft (104));
        top.removeFromLeft (6);
        mode.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (6);
        link.setBounds (top.removeFromLeft (50));
        for (auto [on, level, pan] : { std::tuple { &directOn, &directLevel, &directPan }, std::tuple { &bleedOn, &bleedLevel, &bleedPan } })
        {
            a.removeFromTop (6);
            auto row = a.removeFromTop (28);
            row.removeFromLeft (62);
            on->setBounds (row.removeFromLeft (38).withSizeKeepingCentre (38, 22));
            pan->setBounds (row.removeFromRight (28));
            row.removeFromRight (4);
            level->setBounds (row.withTrimmedLeft (4));
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().withTrimmedTop (4);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("SEND " + juce::String (slot + 1), a.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        a.removeFromTop (6);
        g.drawText (isBus (strip) ? "LEVEL" : "DIRECT", a.removeFromTop (28), juce::Justification::centredLeft, false);
        a.removeFromTop (6);
        if (! isBus (strip))
            g.drawText ("BLEED", a.removeFromTop (28), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

private:
    void follow (juce::Slider& other, double delta)
    {
        refreshing = true;
        other.setValue (juce::jlimit (-60.0, 12.0, other.getValue() + delta), juce::dontSendNotification);
        refreshing = false;
    }

    void commit (bool final)
    {
        mixer::Send d;
        d.target = juce::jmax (0, target.getSelectedId() - 1);
        d.mode = (mixer::SendMode) juce::jmax (0, mode.getSelectedId() - 1);
        d.link = link.getToggleState();
        d.directOn = directOn.getToggleState();
        d.bleedOn = bleedOn.getToggleState();
        d.directDb = (float) directLevel.getValue();
        d.bleedDb = (float) bleedLevel.getValue();
        d.directPan = (float) directPan.getValue();
        d.bleedPan = (float) bleedPan.getValue();
        lastDirect = d.directDb;
        lastBleed = d.bleedDb;
        page.setSend (slot, d, final);
    }

    MixerPage& page;
    int slot, strip = 0;
    juce::ComboBox target, mode;
    juce::TextButton link, directOn, bleedOn;
    juce::Slider directLevel, bleedLevel, directPan, bleedPan;
    double lastDirect = 0.0, lastBleed = 0.0;
    bool refreshing = false;
};

//==============================================================================
// PROPERTIES > EFFECTS: the chosen strip's four slots (ON, the effect, its gain reduction) and the chosen slot's
// settings: OUTPUT and a knob (or a menu) for each of its type's values.
class MixerPage::EffectEditor final : public juce::Component
{
public:
    explicit EffectEditor (MixerPage& p) : page (p)
    {
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            auto& row = rows[(size_t) k];
            styleToggle (row.on, "ON: the effect working. Off: bypassed, its settings kept.");
            row.on.setButtonText ("ON");
            row.on.onClick = [this, k]
            {
                page.chooseEffectSlot (k);
                auto fx = page.processor.getModMatrix().effect (strip, k);
                fx.on = rows[(size_t) k].on.getToggleState();
                page.setEffect (k, fx);
            };
            for (int t = 0; t < mixerfx::typeNames.size(); ++t)
                row.type.addItem (t == 0 ? juce::String ("(empty)") : mixerfx::typeNames[t], t + 1);
            row.type.setTooltip ("The effect in this slot (a new one starts at its defaults).");
            row.type.onChange = [this, k]
            {
                if (refreshing)
                    return;
                page.chooseEffectSlot (k);
                page.setEffectType (strip, k, rows[(size_t) k].type.getSelectedId() - 1);
            };
            addAndMakeVisible (row.on);
            addAndMakeVisible (row.type);
        }
        output.setSliderStyle (juce::Slider::LinearHorizontal);
        output.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
        output.setRange (-24.0, 12.0, 0.1);
        output.setTextValueSuffix (" dB");
        output.setDoubleClickReturnValue (true, 0.0);
        output.setTooltip ("OUTPUT: the effect's level after it.");
        output.onValueChange = [this] { if (! refreshing) commit (false); };
        output.onDragEnd = [this] { commit (true); };
        addAndMakeVisible (output);
    }

    void refresh()
    {
        strip = page.getSelectedStrip();
        chosen = page.getChosenEffectSlot();
        const auto& matrix = page.processor.getModMatrix();
        refreshing = true;
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto& fx = matrix.effect (strip, k);
            auto& row = rows[(size_t) k];
            row.type.setSelectedId (fx.type + 1, juce::dontSendNotification);
            row.on.setToggleState (fx.on, juce::dontSendNotification);
            row.on.setEnabled (fx.isLoaded());
        }
        const auto& fx = matrix.effect (strip, chosen);
        if (fx.type != builtFor)
            rebuild (fx.type);
        const auto& params = mixerfx::paramsOf (fx.type);
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (knobs[i] != nullptr)
                knobs[i]->setValue (fx.values[i], juce::dontSendNotification);
            if (menus[i] != nullptr)
                menus[i]->setSelectedId (juce::roundToInt (fx.values[i]) + 1, juce::dontSendNotification);
        }
        output.setValue (fx.outputDb, juce::dontSendNotification);
        output.setVisible (fx.isLoaded());
        refreshing = false;
        resized();
        repaint();
    }

    // The gain reductions (the timer).
    void showReductions()
    {
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto db = page.processor.getMixerEffectReduction (strip, k);
            if (std::abs (db - reductions[(size_t) k]) > 0.05f)
            {
                reductions[(size_t) k] = db;
                repaint (rowArea (k));
            }
        }
    }

    void resized() override
    {
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            auto r = rowArea (k).reduced (0, 2);
            r.removeFromLeft (24);
            rows[(size_t) k].on.setBounds (r.removeFromLeft (38));
            r.removeFromLeft (6);
            rows[(size_t) k].type.setBounds (r.removeFromLeft (140));
        }
        auto a = settingsArea();
        a.removeFromTop (24);
        output.setBounds (a.removeFromTop (24).withTrimmedLeft (70));
        a.removeFromTop (8);
        const auto columns = (size_t) mixerfx::columnsFor (builtFor);
        const auto w = a.getWidth() / (int) columns;
        const auto& params = mixerfx::paramsOf (builtFor);
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto cell = juce::Rectangle<int> (a.getX() + (int) (i % columns) * w, a.getY() + (int) (i / columns) * cellHeight, w, cellHeight).reduced (3, 0);
            const auto body = cell.withTrimmedTop (14);
            if (knobs[i] != nullptr)
                knobs[i]->setBounds (body.withHeight (cellHeight - 18));
            if (menus[i] != nullptr)
                menus[i]->setBounds (body.withHeight (22).translated (0, 16));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& matrix = page.processor.getModMatrix();
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto r = rowArea (k);
            if (k == chosen)
            {
                g.setColour (ParagonColours::accent.withAlpha (0.1f));
                g.fillRoundedRectangle (r.toFloat(), 3.0f);
            }
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (k == chosen ? ParagonColours::accent : ParagonColours::textDim);
            g.drawText (juce::String (k + 1), r.withWidth (20), juce::Justification::centred, false);
            const auto type = matrix.effect (strip, k).type;
            if (const auto db = reductions[(size_t) k]; (type == mixerfx::compressor || type == mixerfx::gate) && db > 0.05f)
            {
                g.setFont (juce::FontOptions (10.0f));
                g.setColour (ParagonColours::accent);
                g.drawText ("-" + juce::String (db, 1) + " dB", r.withTrimmedLeft (214), juce::Justification::centredLeft, false);
            }
        }
        auto a = settingsArea();
        const auto& fx = matrix.effect (strip, chosen);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("EFFECT " + juce::String (chosen + 1) + ": " + (fx.isLoaded() ? mixerfx::typeNames[fx.type].toUpperCase() : juce::String ("EMPTY")),
                    a.removeFromTop (24), juce::Justification::centredLeft, false);
        if (! fx.isLoaded())
        {
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (ParagonColours::textDim);
            g.drawFittedText ("Choose an effect for this slot above, or click a slot on the strip. Effects work in order from the top, "
                              "before the pan and fader.", a.removeFromTop (40), juce::Justification::topLeft, 3);
            return;
        }
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        g.drawText ("OUTPUT", a.removeFromTop (24), juce::Justification::centredLeft, false);
        a.removeFromTop (8);
        const auto columns = (size_t) mixerfx::columnsFor (fx.type);
        const auto w = a.getWidth() / (int) columns;
        const auto& params = mixerfx::paramsOf (fx.type);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto cell = juce::Rectangle<int> (a.getX() + (int) (i % columns) * w, a.getY() + (int) (i / columns) * cellHeight, w, 14);
            g.drawText (params[i].name, cell, juce::Justification::centred, false);
            // 0.35.0: a menu too narrow for its choice (SATURATION's styles): the whole name under it.
            if (params[i].choices.size() > 0 && w < 110)
            {
                const auto text = mixerfx::valueText (fx.type, (int) i, fx.values[i]);
                if (text.length() > 3)
                {
                    juce::Graphics::ScopedSaveState keep (g);
                    g.setFont (juce::FontOptions (10.0f));
                    g.setColour (ParagonColours::accent);
                    g.drawFittedText (text, cell.translated (0, 58).withHeight (26).reduced (2, 0), juce::Justification::centredTop, 2);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int k = 0; k < mixer::numFxSlots; ++k)
            if (rowArea (k).contains (e.getPosition()))
                page.chooseEffectSlot (k);
    }

private:
    static constexpr int rowHeight = 28, cellHeight = 92;
    juce::Rectangle<int> rowArea (int k) const { return { 0, k * rowHeight, getWidth(), rowHeight }; }
    juce::Rectangle<int> settingsArea() const { return getLocalBounds().withTrimmedTop (mixer::numFxSlots * rowHeight + 12); }

    void rebuild (int type)
    {
        builtFor = type;
        for (auto& k : knobs)
            k.reset();
        for (auto& m : menus)
            m.reset();
        const auto& params = mixerfx::paramsOf (type);
        for (size_t i = 0; i < params.size() && i < knobs.size(); ++i)
        {
            const auto& param = params[i];
            if (! param.choices.isEmpty())
            {
                auto menu = std::make_unique<juce::ComboBox>();
                menu->addItemList (param.choices, 1);
                menu->onChange = [this] { if (! refreshing) { commit (true); repaint(); } };
                addAndMakeVisible (*menu);
                menus[i] = std::move (menu);
                continue;
            }
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
            knob->setRange (param.min, param.max, 0.0);
            if (param.log)
                knob->setSkewFactorFromMidPoint (std::sqrt (param.min * param.max));
            knob->setDoubleClickReturnValue (true, param.def);
            knob->textFromValueFunction = [t = type, i] (double v) { return mixerfx::valueText (t, (int) i, (float) v); };
            knob->valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("-0123456789.").getDoubleValue(); };
            knob->onValueChange = [this] { if (! refreshing) commit (false); };
            knob->onDragEnd = [this] { commit (true); };
            knob->updateText();
            addAndMakeVisible (*knob);
            knobs[i] = std::move (knob);
        }
    }

    void commit (bool final)
    {
        auto fx = page.processor.getModMatrix().effect (strip, chosen);
        const auto& params = mixerfx::paramsOf (fx.type);
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (knobs[i] != nullptr)
                fx.values[i] = (float) knobs[i]->getValue();
            if (menus[i] != nullptr)
                fx.values[i] = (float) juce::jmax (0, menus[i]->getSelectedId() - 1);
        }
        fx.outputDb = (float) output.getValue();
        page.setEffect (chosen, fx, final);
    }

    struct Row { juce::TextButton on; juce::ComboBox type; };
    MixerPage& page;
    std::array<Row, mixer::numFxSlots> rows;
    std::array<std::unique_ptr<juce::Slider>, mixer::maxFxValues> knobs;
    std::array<std::unique_ptr<juce::ComboBox>, mixer::maxFxValues> menus;
    std::array<float, mixer::numFxSlots> reductions {};
    juce::Slider output;
    int strip = 0, chosen = 0, builtFor = -1;
    bool refreshing = false;
};

//==============================================================================
// ROUTE INSTRUMENT MICROPHONES (0.23.0, from the reference's 9.4.1; our own look and words): over the strips, three
// columns: 1. the drums (pieces) with multi-mic sounds, 2. the chosen one's mics (close ones first, then bleed),
// 3. each mic's channel: AUTO (its own), NONE, any mic channel, or a NEW CHANNEL of its own.
class MixerPage::MicRouter final : public juce::Component
{
public:
    explicit MicRouter (MixerPage& p) : page (p)
    {
        DrumSeqPanel::style (close);
        close.setButtonText ("CLOSE");
        close.onClick = [this] { page.openRouter (false); };
        addAndMakeVisible (close);
    }

    // The pieces with multi-mic sounds, and each one's mics (the channels their names sorted them into).
    static std::map<int, juce::Array<int>> micsByPiece (ParagonX1AudioProcessor& processor)
    {
        std::map<int, juce::Array<int>> found;
        for (int k = 0; k < drums::numSlots; ++k)
            if (const auto instrument = processor.getDrumInstrument (k); instrument != nullptr && instrument->hasMics())
                if (const auto piece = drums::pieceOfSlot (k); piece >= 0)
                    for (const auto c : instrument->micChannels())
                        found[piece].addIfNotAlreadyThere (c >= 0 ? c : drums::homeChannel (k));
        // 0.26.0: the drums heard through SIMULATED MICS, and their mics.
        if (auto* on = processor.getParameters().getRawParameterValue (ParamIDs::drumsMicSim); on != nullptr && on->load() > 0.5f)
            for (int pc = 0; pc < drums::numPieces; ++pc)
                if (auto* miked = processor.getParameters().getRawParameterValue (ParamIDs::drum (drums::pieces[(size_t) pc].articulations[0].slot, "Mics"));
                    miked != nullptr && miked->load() > 0.5f)
                    for (const auto c : synth::micsim::channelsFor (pc))
                        found[pc].addIfNotAlreadyThere (c);
        for (auto& [piece, mics] : found)
        {
            // Close mics first (as the reference lists them), then the rest, each in channel order.
            const auto slot = drums::pieces[(size_t) piece].articulations[0].slot;
            std::sort (mics.begin(), mics.end(), [slot] (int a, int b)
            {
                const auto da = drums::isDirect (slot, a) && ! mixer::hearsWholeKit (a), db = drums::isDirect (slot, b) && ! mixer::hearsWholeKit (b);
                return da != db ? da : a < b;
            });
        }
        return found;
    }

    void refresh()
    {
        const auto found = micsByPiece (page.processor);
        if (! found.empty() && found.find (piece) == found.end())
            piece = found.begin()->first;
        pieceButtons.clear();
        for (const auto& [pc, mics] : found)
        {
            auto b = std::make_unique<juce::TextButton> (juce::String (drums::pieces[(size_t) pc].name));
            DrumSeqPanel::style (*b);
            b->setClickingTogglesState (false);
            b->setToggleState (pc == piece, juce::dontSendNotification);
            b->onClick = [this, pc = pc] { page.chooseRouterPiece (pc); };
            addAndMakeVisible (*b);
            pieceButtons.push_back (std::move (b));
        }
        rows.clear();
        if (const auto it = found.find (piece); it != found.end())
            for (const auto mic : it->second)
            {
                auto row = std::make_unique<Row>();
                row->mic = mic;
                const auto slot = drums::pieces[(size_t) piece].articulations[0].slot;
                row->close = drums::isDirect (slot, mic) && ! mixer::hearsWholeKit (mic);
                fill (row->target, mic);
                row->target.onChange = [this, mic, box = &row->target]
                {
                    if (refreshing)
                        return;
                    const auto id = box->getSelectedId();
                    if (id == createId)
                        page.processor.createUserChannel (piece, mic);
                    else
                        page.processor.setMicRoute (piece, mic, id == autoId ? (int) mixer::routeAuto : id == noneId ? (int) mixer::routeNone : id - channelId);
                    juce::Component::SafePointer<MicRouter> safe (this);
                    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->page.routingChanged(); });
                };
                addAndMakeVisible (row->target);
                rows.push_back (std::move (row));
            }
        resized();
        repaint();
    }

    int getPiece() const noexcept { return piece; }
    void setPiece (int p) { piece = p; refresh(); }
    juce::Array<int> getMics() const
    {
        juce::Array<int> mics;
        for (const auto& r : rows)
            mics.add (r->mic);
        return mics;
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (18, 0);
        auto header = a.removeFromTop (44);
        close.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 26));
        a.removeFromTop (24);   // the columns' headings
        auto left = a.removeFromLeft (200);
        for (auto& b : pieceButtons)
        {
            b->setBounds (left.removeFromTop (26));
            left.removeFromTop (4);
        }
        a.removeFromLeft (30);
        auto y = a.getY() + 18;   // under the first group's heading
        auto previousClose = true;
        for (auto& r : rows)
        {
            if (previousClose && ! r->close && &r != &rows.front())
                y += 18;   // the BLEED MICS heading
            previousClose = r->close;
            r->target.setBounds (a.getX() + 210, y, 230, 24);
            y += 30;
        }
    }

    void paint (juce::Graphics& g) override
    {
        DockStyle::drawPanel (g, getLocalBounds().toFloat());
        auto a = getLocalBounds().reduced (18, 0);
        auto header = a.removeFromTop (44);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (header.toFloat().withWidth (300.0f)));
        g.drawText ("ROUTE INSTRUMENT MICROPHONES", header, juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (ParagonColours::textDim);
        g.drawFittedText ("Each of a drum's mics plays into its own channel (AUTO), another, a NEW CHANNEL of its own (e.g. the ride's overhead "
                          "mics apart from the rest of the kit), or none.",
                          header.withTrimmedLeft (300).withTrimmedRight (100), juce::Justification::centredLeft, 2);
        auto headings = a.removeFromTop (24);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("1. DRUM", headings.withWidth (200), juce::Justification::centredLeft, false);
        g.drawText ("2. ITS MICS", headings.withTrimmedLeft (230).withWidth (200), juce::Justification::centredLeft, false);
        g.drawText ("3. CHANNEL", headings.withTrimmedLeft (440).withWidth (230), juce::Justification::centredLeft, false);
        if (pieceButtons.empty())
        {
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (ParagonColours::textDim);
            g.drawText ("No multi-mic drums loaded (DRUMS > LOAD...), and SIMULATED MICS off.", a.withHeight (30), juce::Justification::centredLeft, false);
            return;
        }
        a.removeFromLeft (230);
        auto previousClose = true;
        auto y = a.getY() + 18;
        if (! rows.empty())
        {
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.setColour (ParagonColours::textDim);
            g.drawText (rows.front()->close ? "CLOSE MICS" : "BLEED MICS", juce::Rectangle<int> (a.getX(), y - 14, 200, 12), juce::Justification::centredLeft, false);
        }
        for (const auto& r : rows)
        {
            if (previousClose && ! r->close && &r != &rows.front())
            {
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.setColour (ParagonColours::textDim);
                g.drawText ("BLEED MICS", juce::Rectangle<int> (a.getX(), y + 4, 200, 12), juce::Justification::centredLeft, false);
                y += 18;
            }
            previousClose = r->close;
            g.setFont (juce::FontOptions (11.0f, r->close ? juce::Font::bold : juce::Font::plain));
            g.setColour (r->close ? ParagonColours::accent : ParagonColours::text);
            g.drawText (mixer::channelNames[r->mic], juce::Rectangle<int> (a.getX(), y, 200, 24), juce::Justification::centredLeft, false);
            y += 30;
        }
    }

private:
    static constexpr int autoId = 1, noneId = 2, channelId = 100, createId = 1000;

    void fill (juce::ComboBox& box, int mic)
    {
        refreshing = true;
        box.clear (juce::dontSendNotification);
        box.addItem ("Auto: " + mixer::channelNames[mic], autoId);
        box.addItem ("None", noneId);
        box.addSeparator();
        for (int c = 0; c < mixer::numChannels; ++c)
            box.addItem (mixer::channelNames[c], channelId + c);
        const auto users = page.processor.getUserChannelsInUse();
        for (const auto c : users)
            box.addItem (page.processor.getMixerChannelName (c) + " (yours)", channelId + c);
        box.addSeparator();
        box.addItem ("Create New Channel", createId);
        box.setItemEnabled (createId, users.size() < mixer::numUserChannels);
        const auto to = page.processor.getMicRoute (piece, mic);
        box.setSelectedId (to == mixer::routeAuto ? autoId : to == mixer::routeNone ? noneId : channelId + to, juce::dontSendNotification);
        refreshing = false;
    }

    struct Row { int mic = 0; bool close = false; juce::ComboBox target; };
    MixerPage& page;
    juce::TextButton close;
    std::vector<std::unique_ptr<juce::TextButton>> pieceButtons;
    std::vector<std::unique_ptr<Row>> rows;
    int piece = -1;
    bool refreshing = false;
};

//==============================================================================
// SIMULATED MICS AND ROOM (0.26.0): the switch, the room, and which drums go through the mics.
class MixerPage::MicSimPanel final : public juce::Component
{
public:
    explicit MicSimPanel (MixerPage& p) : page (p)
    {
        auto& apvts = page.processor.getParameters();
        DrumSeqPanel::style (close);
        close.setButtonText ("CLOSE");
        close.onClick = [this] { page.openMicSim (false); };
        addAndMakeVisible (close);
        styleToggle (onButton, "SIMULATED MICS: the kit's synth drums and single samples heard through close mics, overheads, a room "
                               "pair and a far ambience pair, each in its own mixer channel (our own model of a miked kit).");
        onButton.setButtonText ("SIMULATED MICS");
        addAndMakeVisible (onButton);
        onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamIDs::drumsMicSim, onButton);
        room.addItemList (synth::micsim::roomNames, 1);
        room.setTooltip ("ROOM: the space the room and ambience mics hear (our own rooms: their size, how long and how bright they ring).");
        addAndMakeVisible (room);
        roomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, ParamIDs::drumsRoom, room);
        for (auto* k : { &decay, &tone })
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            addAndMakeVisible (*k);
        }
        decay.setTooltip ("DECAY: the room's ring, shorter or longer than its own.");
        tone.setTooltip ("TONE: the room dark (left) or bright (right).");
        decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::drumsRoomDecay, decay);
        toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::drumsRoomTone, tone);
        for (int pc = 0; pc < drums::numPieces; ++pc)
        {
            auto b = std::make_unique<juce::TextButton> (juce::String (drums::pieces[(size_t) pc].name));
            styleToggle (*b, "Whether this drum is heard through the simulated mics (off: it plays its own way, its OUTPUT, INSERT and sends).");
            b->onClick = [this, pc, button = b.get()] { page.setPieceMiked (pc, button->getToggleState()); };
            addAndMakeVisible (*b);
            pieceButtons.push_back (std::move (b));
        }
        refresh();
    }

    void refresh()
    {
        for (int pc = 0; pc < drums::numPieces; ++pc)
            pieceButtons[(size_t) pc]->setToggleState (page.isPieceMiked (pc), juce::dontSendNotification);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (18, 0);
        auto header = a.removeFromTop (44);
        close.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 26));
        a.removeFromTop (8);
        auto controls = a.removeFromTop (92);
        onButton.setBounds (controls.removeFromLeft (150).withSizeKeepingCentre (150, 28));
        controls.removeFromLeft (24);
        room.setBounds (controls.removeFromLeft (150).withSizeKeepingCentre (150, 26).translated (0, 8));
        controls.removeFromLeft (20);
        decay.setBounds (controls.removeFromLeft (80).withTrimmedTop (14));
        controls.removeFromLeft (8);
        tone.setBounds (controls.removeFromLeft (80).withTrimmedTop (14));
        a.removeFromTop (36);   // DRUMS THROUGH THE MICS
        const auto columns = juce::jmax (1, a.getWidth() / 140);
        for (size_t i = 0; i < pieceButtons.size(); ++i)
            pieceButtons[i]->setBounds (a.getX() + (int) (i % (size_t) columns) * 140, a.getY() + (int) (i / (size_t) columns) * 32, 132, 26);
    }

    void paint (juce::Graphics& g) override
    {
        DockStyle::drawPanel (g, getLocalBounds().toFloat());
        auto a = getLocalBounds().reduced (18, 0);
        auto header = a.removeFromTop (44);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (header.toFloat().withWidth (260.0f)));
        g.drawText ("SIMULATED MICS AND ROOM", header, juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (ParagonColours::textDim);
        g.drawFittedText ("The synth drums heard as a recorded kit: close mics, overheads, room and ambience, each its own channel "
                          "below, the other drums bleeding into each close mic. Our own model, not a real room's.",
                          header.withTrimmedLeft (270).withTrimmedRight (100), juce::Justification::centredLeft, 2);
        a.removeFromTop (8);
        auto labels = a.removeFromTop (92);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        labels.removeFromLeft (174);
        g.drawText ("ROOM", labels.removeFromLeft (150).withHeight (20).translated (0, 16), juce::Justification::centredLeft, false);
        labels.removeFromLeft (20);
        g.drawText ("DECAY", labels.removeFromLeft (80).withHeight (14), juce::Justification::centred, false);
        labels.removeFromLeft (8);
        g.drawText ("TONE", labels.removeFromLeft (80).withHeight (14), juce::Justification::centred, false);
        g.drawText ("DRUMS THROUGH THE MICS", a.removeFromTop (30).withTrimmedTop (10), juce::Justification::centredLeft, false);
    }

private:
    MixerPage& page;
    juce::TextButton close, onButton;
    juce::ComboBox room;
    juce::Slider decay, tone;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roomAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment, toneAttachment;
    std::vector<std::unique_ptr<juce::TextButton>> pieceButtons;
};

//==============================================================================
MixerPage::MixerPage (ParagonX1AudioProcessor& p) : processor (p)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    for (int s = 0; s < mixer::numStrips; ++s)
    {
        strips.push_back (std::make_unique<Strip> (*this, s));
        holder.addAndMakeVisible (*strips.back());
    }
    for (int pc = 0; pc < mixer::numPieces; ++pc)
    {
        rows.push_back (std::make_unique<PieceRow> (*this, pc));
        addChildComponent (*rows.back());
    }
    for (int k = 0; k < mixer::numSendSlots; ++k)
    {
        sendEditors.push_back (std::make_unique<SendEditor> (*this, k));
        addChildComponent (*sendEditors.back());
    }
    effectEditor = std::make_unique<EffectEditor> (*this);
    addChildComponent (*effectEditor);
    router = std::make_unique<MicRouter> (*this);
    addChildComponent (*router);
    micSimPanel = std::make_unique<MicSimPanel> (*this);
    addChildComponent (*micSimPanel);
    DrumSeqPanel::style (micSimButton);
    micSimButton.setTooltip ("SIMULATED MICS AND ROOM: the synth drums through close mics, overheads, room and ambience, in a room of "
                             "your choice.");
    micSimButton.onClick = [this] { openMicSim (! isMicSimOpen()); };
    addAndMakeVisible (micSimButton);
    DrumSeqPanel::style (routeButton);
    routeButton.setTooltip ("ROUTE INSTRUMENT MICROPHONES: each drum's mics to a channel of your choice, a new channel of their own, or none.");
    routeButton.onClick = [this] { openRouter (! isRouterOpen()); };
    addAndMakeVisible (routeButton);
    for (auto* s : { &offset, &release })
    {
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        addChildComponent (*s);
    }
    offset.setTooltip ("TIME OFFSET: the channel later, up to 10 ms (to line overheads or room up with the close mics). On a "
                       "close mic it moves the hit off its beat.");
    release.setTooltip ("RELEASE: each hit in this channel faded after this long (OFF: as recorded). Shorter room tails, or a "
                        "gated sound.");
    styleToggle (micsButton, "Show or hide the mic channels.");
    styleToggle (busesButton, "Show or hide the buses.");
    micsButton.setToggleState (true, juce::dontSendNotification);
    busesButton.setToggleState (true, juce::dontSendNotification);
    micsButton.onClick = [this] { showGroups (micsButton.getToggleState(), busesButton.getToggleState()); };
    busesButton.onClick = [this] { showGroups (micsButton.getToggleState(), busesButton.getToggleState()); };
    DrumSeqPanel::style (menuButton);
    menuButton.setTooltip ("The mixer's menu: MUTE MODE, STEREO REVERSE, STEREO or MULTICHANNEL OUTPUTS.");
    menuButton.onClick = [this] { showMixerMenu(); };
    for (auto* b : { &bleedTab, &sendsTab, &effectsTab })
    {
        DrumSeqPanel::style (*b);
        b->setClickingTogglesState (false);
    }
    bleedTab.setTooltip ("The channel's bleed from each drum, TIME OFFSET and RELEASE.");
    sendsTab.setTooltip ("The channel's two sends: where, how loud, their pans.");
    bleedTab.onClick = [this] { showTab (Tab::bleed); };
    sendsTab.onClick = [this] { showTab (Tab::sends); };
    effectsTab.setTooltip ("The four effect slots: EQ, COMPRESSOR, GATE, TRANSIENT, DRIVE, LO-FI, MOD.");
    effectsTab.onClick = [this] { showTab (Tab::effects); };
    for (auto* c : { (juce::Component*) &micsButton, (juce::Component*) &busesButton, (juce::Component*) &menuButton,
                     (juce::Component*) &bleedTab, (juce::Component*) &sendsTab, (juce::Component*) &effectsTab })
        addAndMakeVisible (c);
    selectStrip (selected);
    startTimerHz (30);
}

MixerPage::~MixerPage() { stopTimer(); }

juce::Array<int> MixerPage::getChannelsInUse() const
{
    // Each loaded multi-mic drum's mics, through ROUTE INSTRUMENT MICROPHONES (0.23.0).
    juce::Array<int> channels;
    for (int k = 0; k < drums::numSlots; ++k)
        if (const auto instrument = processor.getDrumInstrument (k); instrument != nullptr && instrument->hasMics())
            for (const auto c : instrument->micChannels())
            {
                const auto source = c >= 0 ? c : drums::homeChannel (k);
                const auto piece = drums::pieceOfSlot (k);
                const auto to = piece >= 0 ? processor.getMicRoute (piece, source) : (int) mixer::routeAuto;
                if (to == mixer::routeAuto)
                    channels.addIfNotAlreadyThere (source);
                else if (to >= 0)
                    channels.addIfNotAlreadyThere (to);
            }
    // 0.29.0: the channels the drums' layers play into.
    for (int k = 0; k < drums::numSlots; ++k)
        if (processor.getLayerRef (k).isNotEmpty())
            if (auto* out = processor.getParameters().getRawParameterValue (ParamIDs::drum (k, "LayerOut")); out != nullptr && out->load() > 0.5f)
                channels.addIfNotAlreadyThere (juce::roundToInt (out->load()) - 1);
    // 0.26.0: SIMULATED MICS: each miked drum's mics, through the routes too.
    if (auto* on = processor.getParameters().getRawParameterValue (ParamIDs::drumsMicSim); on != nullptr && on->load() > 0.5f)
        for (int pc = 0; pc < drums::numPieces; ++pc)
            if (isPieceMiked (pc))
                for (const auto c : synth::micsim::channelsFor (pc))
                {
                    const auto to = processor.getMicRoute (pc, c);
                    if (to == mixer::routeAuto)
                        channels.addIfNotAlreadyThere (c);
                    else if (to >= 0)
                        channels.addIfNotAlreadyThere (to);
                }
    channels.sort();
    return channels;
}

void MixerPage::selectStrip (int strip)
{
    selected = juce::jlimit (0, mixer::numStrips - 1, strip);
    for (auto& s : strips)
        s->setSelected (s->getStrip() == selected);
    if (const auto channel = mixer::channelOfStrip (selected); channel >= 0)
    {
        offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), ParamIDs::mix (channel, "Offset"), offset);
        releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), ParamIDs::mix (channel, "Release"), release);
    }
    refreshProperties();
    resized();
    repaint();
}

void MixerPage::showTab (Tab t)
{
    tab = t;
    resized();
    repaint();
}

void MixerPage::showGroups (bool mics, bool buses)
{
    micsButton.setToggleState (mics, juce::dontSendNotification);
    busesButton.setToggleState (buses, juce::dontSendNotification);
    if (! isStripShown (selected))
        if (const auto first = std::find_if (strips.begin(), strips.end(), [this] (const auto& s) { return isStripShown (s->getStrip()); }); first != strips.end())
            selectStrip ((*first)->getStrip());
    resized();
    repaint();
}

bool MixerPage::isStripShown (int strip) const
{
    if (isBus (strip))
        return busesButton.getToggleState();
    // A user channel only once a mic is routed to it.
    return micsButton.getToggleState() && (! isUser (strip) || processor.getUserChannelsInUse().contains (mixer::channelOfStrip (strip)));
}

void MixerPage::scrollToStrip (int strip)
{
    if (const auto& s = strips[(size_t) juce::jlimit (0, mixer::numStrips - 1, strip)]; s->isVisible())
        viewport.setViewPosition (s->getX(), 0);
}

void MixerPage::setBleed (int piece, float gainDb, bool invert, bool on)
{
    if (selected >= mixer::numChannels)   // a bus or a user channel (its bleed cells are its mics' own channels')
        return;
    auto m = processor.getModMatrix();
    m.bleed (selected, juce::jlimit (0, mixer::numPieces - 1, piece)) = { juce::jlimit (-60.0f, 12.0f, gainDb), invert, on };
    processor.setModMatrix (m);
}

void MixerPage::setSend (int slot, const mixer::Send& send, bool final)
{
    slot = juce::jlimit (0, mixer::numSendSlots - 1, slot);
    auto m = processor.getModMatrix();
    auto d = send;
    if (const auto b = mixer::busOfTarget (d.target); b >= 0 && ! mixer::canFeedBus (selected, b))
        d.target = 0;
    m.send (selected, slot) = d;
    processor.setModMatrix (m);
    if (final)
        processor.modMatrixEditFinished();
    strips[(size_t) selected]->refresh();
}

void MixerPage::setSendTarget (int strip, int slot, int target)
{
    strip = juce::jlimit (0, mixer::numStrips - 1, strip);
    slot = juce::jlimit (0, mixer::numSendSlots - 1, slot);
    target = juce::jlimit (0, mixer::sendTargetNames.size() - 1, target);
    if (const auto b = mixer::busOfTarget (target); b >= 0 && ! mixer::canFeedBus (strip, b))
        return;
    auto m = processor.getModMatrix();
    auto& d = m.send (strip, slot);
    d = target == 0 ? mixer::Send {} : d;   // OFF clears it (levels and pans back to 0)
    d.target = target;
    processor.setModMatrix (m);
    processor.modMatrixEditFinished();
    strips[(size_t) strip]->refresh();
    if (strip == selected)
        refreshProperties();
}

juce::String MixerPage::describeOutput (int strip) const
{
    const auto route = processor.getMixerRoute (strip);
    if (const auto b = mixer::busOfTarget (route); b >= 0 && mixer::canFeedBus (strip, b))
        return mixer::routeNames[route];
    if (mixer::fxBusOfTarget (route) >= 0)
        return mixer::routeNames[route];
    return ParamIDs::outputNames[juce::jlimit (0, ParamIDs::numOutputs - 1, processor.getMixerOutput (strip))];
}

juce::String MixerPage::describeSend (int strip, int slot) const
{
    const auto& d = processor.getModMatrix().send (strip, slot);
    return d.isOn() ? mixer::sendTargetNames[d.target] : juce::String ("+ SEND");
}

void MixerPage::showOutputMenu (int strip, juce::Component& from)
{
    const auto route = processor.getMixerRoute (strip), output = processor.getMixerOutput (strip);
    const auto current = describeOutput (strip);
    juce::PopupMenu menu;
    menu.addSectionHeader ("OUTPUTS");
    for (int o = 0; o < ParamIDs::numOutputs; ++o)
        menu.addItem (1 + o, ParamIDs::outputNames[o], true, route == 0 && output == o && current == ParamIDs::outputNames[o]);
    menu.addSectionHeader ("BUSES");
    for (int b = 0; b < mixer::numBuses; ++b)
        menu.addItem (101 + b, mixer::routeNames[b + 1], mixer::canFeedBus (strip, b), route == b + 1);
    menu.addSectionHeader ("FX BUSES (THE FX PAGE'S INSERT BUSES)");
    for (int f = 0; f < mixer::numFxBuses; ++f)
        menu.addItem (201 + f, mixer::routeNames[mixer::firstFxRoute + f], true, route == mixer::firstFxRoute + f);
    juce::Component::SafePointer<MixerPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from), [safe, strip] (int chosen)
    {
        if (safe == nullptr || chosen == 0)
            return;
        if (chosen >= 201)
            safe->processor.setMixerRoute (strip, mixer::firstFxRoute + chosen - 201);
        else if (chosen >= 101)
            safe->processor.setMixerRoute (strip, chosen - 100);
        else
            safe->processor.setMixerOutput (strip, chosen - 1);
        safe->update();
    });
}

void MixerPage::showSendMenu (int strip, int slot, juce::Component& from)
{
    selectStrip (strip);
    showTab (Tab::sends);
    const auto current = processor.getModMatrix().send (strip, slot).target;
    juce::PopupMenu menu;
    menu.addItem (1000, "Off", current != 0, current == 0);
    menu.addSectionHeader ("BUSES");
    for (int b = 0; b < mixer::numBuses; ++b)
        menu.addItem (b + 1, mixer::sendTargetNames[b + 1], mixer::canFeedBus (strip, b), current == b + 1);
    menu.addSectionHeader ("FX BUSES (THE FX PAGE'S INSERT BUSES)");
    for (int f = 0; f < mixer::numFxBuses; ++f)
        menu.addItem (mixer::firstFxRoute + f, mixer::sendTargetNames[mixer::firstFxRoute + f], true, current == mixer::firstFxRoute + f);
    menu.addSectionHeader ("EFFECTS");
    menu.addItem (mixer::delayTarget, "Delay", true, current == mixer::delayTarget);
    menu.addItem (mixer::reverbTarget, "Reverb", true, current == mixer::reverbTarget);
    juce::Component::SafePointer<MixerPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from), [safe, strip, slot] (int chosen)
    {
        if (safe != nullptr && chosen != 0)
            safe->setSendTarget (strip, slot, chosen == 1000 ? 0 : chosen);
    });
}

void MixerPage::showMixerMenu()
{
    const auto postSend = processor.getParameters().getRawParameterValue (ParamIDs::mixMuteMode)->load() > 0.5f;
    juce::PopupMenu menu;
    menu.addSectionHeader ("CHANNEL MUTE");
    menu.addItem (preSend, "Before the sends (PRE SEND): a muted channel's sends are silent too", true, ! postSend);
    menu.addItem (MenuItem::postSend, "After the sends (POST SEND): its pre-fader sends go on", true, postSend);
    menu.addSeparator();
    menu.addItem (stereoReverse, "Stereo reverse: every pan to the other side");
    menu.addItem (stereoOutputs, "Stereo outputs: every channel and bus to MAIN");
    menu.addItem (multichannelOutputs, "Multichannel outputs: the channels to OUT 2-9 by instrument");
    juce::Component::SafePointer<MixerPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safe] (int chosen)
    {
        if (safe != nullptr && chosen != 0)
            safe->runMenuItem (chosen);
    });
}

void MixerPage::runMenuItem (int item)
{
    if (item == preSend || item == MenuItem::postSend)
    {
        if (auto* parameter = processor.getParameters().getParameter (ParamIDs::mixMuteMode))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (item == MenuItem::postSend ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }
        note = item == MenuItem::postSend ? "POST SEND: a muted channel's pre-fader sends go on." : "PRE SEND: a muted channel's sends are silent too.";
    }
    else if (item == stereoReverse)
    {
        processor.mixerStereoReverse();
        note = "Every pan moved to the other side.";
    }
    else if (item == stereoOutputs)
    {
        processor.applyMixerOutputs (false);
        note = "Every channel and bus to MAIN.";
    }
    else if (item == multichannelOutputs)
    {
        processor.applyMixerOutputs (true);
        note = "Kick OUT 2, snare 3, hi-hat 4, toms 5, cymbals 6, overheads 7, room 8, other 9: switch them on in your DAW "
               "(until then they play in MAIN).";
    }
    refreshProperties();
    update();
    repaint();
}

void MixerPage::setEffectType (int strip, int slot, int type)
{
    strip = juce::jlimit (0, mixer::numStrips - 1, strip);
    slot = juce::jlimit (0, mixer::numFxSlots - 1, slot);
    type = juce::jlimit (0, mixerfx::typeNames.size() - 1, type);
    auto m = processor.getModMatrix();
    if (m.effect (strip, slot).type == type)
        return;
    m.effect (strip, slot) = type == 0 ? mixer::FxSlot {} : mixerfx::defaultSlot (type);
    processor.setModMatrix (m);
    processor.modMatrixEditFinished();
    strips[(size_t) strip]->refresh();
    if (strip == selected)
        refreshProperties();
}

void MixerPage::setEffect (int slot, const mixer::FxSlot& fx, bool final)
{
    slot = juce::jlimit (0, mixer::numFxSlots - 1, slot);
    auto m = processor.getModMatrix();
    m.effect (selected, slot) = fx;
    processor.setModMatrix (m);
    if (final)
        processor.modMatrixEditFinished();
    shownEffects[(size_t) slot] = fx;   // its own edit: the editor already shows it
    strips[(size_t) selected]->refresh();
}

void MixerPage::chooseEffectSlot (int slot)
{
    chosenFx = juce::jlimit (0, mixer::numFxSlots - 1, slot);
    effectEditor->refresh();
}

void MixerPage::copyEffect (int strip, int slot)
{
    fxClipboard = processor.getModMatrix().effect (juce::jlimit (0, mixer::numStrips - 1, strip), juce::jlimit (0, mixer::numFxSlots - 1, slot));
}

void MixerPage::pasteEffect (int strip, int slot)
{
    if (! fxClipboard.has_value())
        return;
    strip = juce::jlimit (0, mixer::numStrips - 1, strip);
    slot = juce::jlimit (0, mixer::numFxSlots - 1, slot);
    auto m = processor.getModMatrix();
    m.effect (strip, slot) = *fxClipboard;
    processor.setModMatrix (m);
    processor.modMatrixEditFinished();
    strips[(size_t) strip]->refresh();
    if (strip == selected)
        refreshProperties();
}

juce::String MixerPage::describeEffect (int strip, int slot) const
{
    const auto& fx = processor.getModMatrix().effect (strip, slot);
    return fx.isLoaded() && fx.type < mixerfx::typeNames.size() ? mixerfx::typeNames[fx.type] : juce::String ("+ FX");
}

void MixerPage::showEffectMenu (int strip, int slot, juce::Component& from)
{
    selectStrip (strip);
    chooseEffectSlot (slot);
    showTab (Tab::effects);
    const auto& fx = processor.getModMatrix().effect (strip, slot);
    juce::PopupMenu menu;
    menu.addSectionHeader ("EFFECT " + juce::String (slot + 1));
    for (int t = 1; t < mixerfx::typeNames.size(); ++t)
        menu.addItem (t, mixerfx::typeNames[t], true, fx.type == t);
    menu.addSeparator();
    menu.addItem (100, "On", fx.isLoaded(), fx.isLoaded() && fx.on);
    menu.addItem (101, "Copy", fx.isLoaded());
    menu.addItem (102, "Paste", fxClipboard.has_value());
    menu.addItem (103, "Remove", fx.isLoaded());
    juce::Component::SafePointer<MixerPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from), [safe, strip, slot] (int chosen)
    {
        if (safe == nullptr || chosen == 0)
            return;
        if (chosen < 100)
            safe->setEffectType (strip, slot, chosen);
        else if (chosen == 100)
        {
            auto on = safe->processor.getModMatrix().effect (strip, slot);
            on.on = ! on.on;
            safe->selectStrip (strip);
            safe->setEffect (slot, on);
            safe->refreshProperties();
        }
        else if (chosen == 101)
            safe->copyEffect (strip, slot);
        else if (chosen == 102)
            safe->pasteEffect (strip, slot);
        else if (chosen == 103)
            safe->setEffectType (strip, slot, 0);
    });
}

void MixerPage::openMicSim (bool open)
{
    if (open)
        openRouter (false);
    micSimPanel->setVisible (open);
    micSimButton.setToggleState (open, juce::dontSendNotification);
    if (open)
    {
        micSimPanel->refresh();
        micSimPanel->toFront (false);
    }
}

bool MixerPage::isMicSimOpen() const { return micSimPanel->isVisible(); }

bool MixerPage::isPieceMiked (int piece) const
{
    const auto& pc = drums::pieces[(size_t) juce::jlimit (0, drums::numPieces - 1, piece)];
    auto* parameter = processor.getParameters().getRawParameterValue (ParamIDs::drum (pc.articulations[0].slot, "Mics"));
    return parameter != nullptr && parameter->load() > 0.5f;
}

void MixerPage::setPieceMiked (int piece, bool miked)
{
    // Every articulation of the drum (one undo step).
    const auto& pc = drums::pieces[(size_t) juce::jlimit (0, drums::numPieces - 1, piece)];
    for (int a = 0; a < pc.count; ++a)
        if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (pc.articulations[(size_t) a].slot, "Mics")))
            parameter->setValueNotifyingHost (miked ? 1.0f : 0.0f);
    processor.modMatrixEditFinished();   // one undo step
    micSimPanel->refresh();
    update();
}

void MixerPage::openRouter (bool open)
{
    if (open && micSimPanel != nullptr)
        openMicSim (false);
    router->setVisible (open);
    routeButton.setToggleState (open, juce::dontSendNotification);
    if (open)
    {
        router->refresh();
        router->toFront (false);
    }
}

bool MixerPage::isRouterOpen() const { return router->isVisible(); }
void MixerPage::chooseRouterPiece (int piece) { router->setPiece (piece); }
int MixerPage::getRouterPiece() const { return router->getPiece(); }
juce::Array<int> MixerPage::getRouterMics() const { return router->getMics(); }

void MixerPage::routingChanged()
{
    if (router->isVisible())
        router->refresh();
    resized();
    update();
    repaint();
}

void MixerPage::refreshProperties()
{
    for (auto& e : sendEditors)
        e->refresh (selected);
    for (int k = 0; k < mixer::numSendSlots; ++k)
        shownSends[(size_t) k] = processor.getModMatrix().send (selected, k);
    effectEditor->refresh();
    for (int k = 0; k < mixer::numFxSlots; ++k)
        shownEffects[(size_t) k] = processor.getModMatrix().effect (selected, k);
    if (selected >= mixer::numChannels)   // a bus, or a user channel (no bleed table of its own)
        return;
    // Which pieces have audio in this channel (a loaded kit's mic), and which are its own.
    std::array<bool, mixer::numPieces> has {};
    for (int k = 0; k < drums::numSlots; ++k)
        if (const auto instrument = processor.getDrumInstrument (k); instrument != nullptr && instrument->hasMics())
            for (const auto c : instrument->micChannels())
                if ((c >= 0 ? c : drums::homeChannel (k)) == selected)
                    if (const auto pc = drums::pieceOfSlot (k); pc >= 0)
                        has[(size_t) pc] = true;
    for (int pc = 0; pc < mixer::numPieces; ++pc)
    {
        const auto slot = drums::pieces[(size_t) pc].articulations[0].slot;
        rows[(size_t) pc]->refresh (selected, drums::isDirect (slot, selected), has[(size_t) pc]);
    }
}

void MixerPage::timerCallback()
{
    if (isShowing())
        update();
}

void MixerPage::update()
{
    for (auto& s : strips)
    {
        const auto strip = s->getStrip();
        s->setMeter (isBus (strip) ? processor.takeMixerBusPeak (strip - mixer::numChannels) : processor.takeMixerPeak (mixer::channelOfStrip (strip)));
        s->refresh();
    }
    // The sends changed elsewhere (undo, a preset): the editors show them.
    static_assert (mixer::numSendSlots == 2);
    if (const auto& m = processor.getModMatrix(); ! (m.send (selected, 0) == shownSends[0]) || ! (m.send (selected, 1) == shownSends[1]))
    {
        for (auto& e : sendEditors)
            e->refresh (selected);
        for (int k = 0; k < mixer::numSendSlots; ++k)
            shownSends[(size_t) k] = m.send (selected, k);
    }
    {
        auto changed = false;
        for (int k = 0; k < mixer::numFxSlots; ++k)
            changed = changed || ! (processor.getModMatrix().effect (selected, k) == shownEffects[(size_t) k]);
        if (changed)
        {
            effectEditor->refresh();
            for (int k = 0; k < mixer::numFxSlots; ++k)
                shownEffects[(size_t) k] = processor.getModMatrix().effect (selected, k);
        }
        effectEditor->showReductions();
    }
    // The user channels in use and their names (routes changed by undo, a preset, the panel).
    if (const auto users = processor.getUserChannelsInUse(); users != shownUsers || processor.getModMatrix().userChannelNames != shownUserNames)
    {
        shownUsers = users;
        shownUserNames = processor.getModMatrix().userChannelNames;
        if (! isStripShown (selected))
            selectStrip (0);
        resized();
        repaint();
        for (auto& st : strips)
            st->repaint();
        if (router->isVisible())
            router->refresh();
    }
    const auto used = getChannelsInUse();
    if (used != inUse)
    {
        inUse = used;
        for (auto& s : strips)
            if (! isBus (s->getStrip()))
                s->setInUse (inUse.contains (mixer::channelOfStrip (s->getStrip())));
        refreshProperties();
        repaint();
    }
}

void MixerPage::resized()
{
    auto a = getLocalBounds();
    propertiesArea = a.removeFromRight (propertiesWidth);
    a.removeFromRight (12);
    stripsArea = a;

    auto header = stripsArea.reduced (18, 0).withHeight (headerHeight).withSizeKeepingCentre (stripsArea.getWidth() - 36, 26);
    menuButton.setBounds (header.removeFromRight (78));
    header.removeFromRight (8);
    busesButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (4);
    micsButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (8);
    routeButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (8);
    micSimButton.setBounds (header.removeFromRight (100));
    router->setBounds (stripsArea);
    micSimPanel->setBounds (stripsArea);

    viewport.setBounds (stripsArea.reduced (14, 0).withTrimmedTop (headerHeight).withTrimmedBottom (4));
    int shown = 0;
    for (auto& s : strips)
        shown += isStripShown (s->getStrip()) ? 1 : 0;
    const auto fits = shown * stripWidth <= viewport.getWidth();
    const auto w = fits && shown > 0 ? viewport.getWidth() / shown : stripWidth;
    holder.setSize (juce::jmax (1, shown * w), viewport.getHeight() - (fits ? 0 : viewport.getScrollBarThickness() + 2));
    // Shown in order: the named channels, the user channels (after them, with the mics), the buses.
    std::vector<Strip*> order;
    for (auto& s : strips)
        if (s->getStrip() < mixer::numChannels)
            order.push_back (s.get());
    for (auto& s : strips)
        if (isUser (s->getStrip()))
            order.push_back (s.get());
    for (auto& s : strips)
        if (isBus (s->getStrip()))
            order.push_back (s.get());
    int x = 0;
    for (auto* s : order)
    {
        const auto visible = isStripShown (s->getStrip());
        s->setVisible (visible);
        if (visible)
        {
            s->setBounds (juce::Rectangle<int> (x, 0, w, holder.getHeight()).reduced (2, 0));
            x += w;
        }
    }

    // PROPERTIES: BLEED / SENDS tabs (a bus: its sends only).
    const auto bus = isBus (selected);
    if (bus && tab == Tab::bleed)
        tab = Tab::effects;
    auto p = propertiesArea.reduced (18, 0).withTrimmedTop (headerHeight);
    auto tabs = p.removeFromTop (24);
    bleedTab.setVisible (! bus);
    if (! bus)
    {
        bleedTab.setBounds (tabs.removeFromLeft (76));
        tabs.removeFromLeft (4);
    }
    effectsTab.setBounds (tabs.removeFromLeft (84));
    tabs.removeFromLeft (4);
    sendsTab.setBounds (tabs.removeFromLeft (76));
    bleedTab.setToggleState (tab == Tab::bleed, juce::dontSendNotification);
    sendsTab.setToggleState (tab == Tab::sends, juce::dontSendNotification);
    effectsTab.setToggleState (tab == Tab::effects, juce::dontSendNotification);
    p.removeFromTop (8);
    propertiesBody = p;
    effectEditor->setVisible (tab == Tab::effects);
    effectEditor->setBounds (p);

    const auto showingBleed = tab == Tab::bleed;
    const auto userChannel = isUser (selected);
    for (auto& r : rows)
        r->setVisible (showingBleed && ! userChannel);
    offset.setVisible (showingBleed);
    release.setVisible (showingBleed);
    for (auto& e : sendEditors)
        e->setVisible (tab == Tab::sends);
    if (showingBleed && userChannel)
    {
        // A user channel: its mics' bleed levels are their own channels'; TIME OFFSET and RELEASE are its own.
        p.removeFromTop (64);
        offset.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
        p.removeFromTop (6);
        release.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
    }
    else if (showingBleed)
    {
        // CLOSE MIC AUDIO first, then BLEED FROM INSTRUMENTS (the order rows are drawn in, by kind).
        std::vector<PieceRow*> direct, bleeding;
        for (auto& r : rows)
            (r->isDirect ? direct : bleeding).push_back (r.get());
        p.removeFromTop (18);
        for (auto* r : direct)
            r->setBounds (p.removeFromTop (rowHeight));
        p.removeFromTop (22);
        for (auto* r : bleeding)
            r->setBounds (p.removeFromTop (rowHeight));
        p.removeFromTop (14);
        offset.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
        p.removeFromTop (6);
        release.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
    }
    else if (tab == Tab::sends)
    {
        for (auto& e : sendEditors)
        {
            e->setBounds (p.removeFromTop (bus ? SendEditor::busHeight : SendEditor::height));
            p.removeFromTop (8);
        }
    }
}

void MixerPage::paint (juce::Graphics& g)
{
    for (const auto& area : { stripsArea, propertiesArea })
        DockStyle::drawPanel (g, area.toFloat());
    const auto title = [&g] (const juce::String& text, juce::Rectangle<int> area, juce::Colour colour = {})
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        if (colour.isTransparent())
            g.setGradientFill (ParagonLookAndFeel::menuHighlight (area.toFloat().withWidth (140.0f)));
        else
            g.setColour (colour);
        g.drawText (text, area, juce::Justification::centredLeft, false);
    };
    title ("MIXER", stripsArea.reduced (18, 0).withHeight (headerHeight));
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ParagonColours::textDim);
    const auto hint = note.isNotEmpty() ? note
                    : inUse.isEmpty() ? juce::String ("Load a multi-mic kit (DRUMS > LOAD...: an SFZ kit, or a folder with a folder per mic) to use "
                                                      "the mixer, or SIM MICS... to hear the synth drums through simulated mics.")
                                      : juce::String ("The mic channels (multi-mic drums, or SIM MICS' simulated ones), then the buses. Click a name for its properties.");
    g.drawFittedText (hint, stripsArea.reduced (18, 0).withHeight (headerHeight).withTrimmedLeft (90).withTrimmedRight (340),
                      juce::Justification::centredLeft, 2);

    const auto bus = isBus (selected);
    title ("PROPERTIES: " + stripName (processor, selected), propertiesArea.reduced (18, 0).withHeight (headerHeight), bus ? busColour : juce::Colour());
    auto p = propertiesBody;
    if (tab == Tab::effects)
        return;
    if (tab == Tab::sends)
    {
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (ParagonColours::textDim);
        p.removeFromTop (mixer::numSendSlots * ((bus ? SendEditor::busHeight : SendEditor::height) + 8) + 4);
        g.drawFittedText (bus ? "A bus sends into a later bus, an FX bus (the FX page's insert buses A-D, with their effects), the "
                                "DELAY or the REVERB."
                              : "DIRECT: the drums this mic is meant for; BLEED: the rest of the kit in it, sent apart (off to "
                                "compress a bus on the close sound alone). The DELAY and REVERB are on the FX page.",
                          p.removeFromTop (60), juce::Justification::topLeft, 4);
        return;
    }
    if (isUser (selected))
    {
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (ParagonColours::textDim);
        g.drawFittedText ("A channel of your own (ROUTE MICS...): the mics routed here keep the bleed levels of the channels they "
                          "came from (their BLEED FROM INSTRUMENTS); this channel's BLEED knob and button still apply.",
                          p.removeFromTop (58), juce::Justification::topLeft, 4);
        p.removeFromTop (6);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("TIME OFFSET", p.removeFromTop (24), juce::Justification::centredLeft, false);
        p.removeFromTop (6);
        g.drawText ("RELEASE", p.removeFromTop (24), juce::Justification::centredLeft, false);
        return;
    }
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (ParagonColours::text);
    int directCount = 0;
    for (auto& r : rows)
        directCount += r->isDirect ? 1 : 0;
    g.drawText (mixer::hearsWholeKit (selected) ? "THE WHOLE KIT (NO BLEED HERE)" : "CLOSE MIC AUDIO", p.removeFromTop (18), juce::Justification::centredLeft, false);
    p.removeFromTop (directCount * rowHeight);
    g.drawText ("BLEED FROM INSTRUMENTS", p.removeFromTop (22).withTrimmedTop (4), juce::Justification::centredLeft, false);
    p.removeFromTop ((mixer::numPieces - directCount) * rowHeight + 14);
    g.drawText ("TIME OFFSET", p.removeFromTop (24), juce::Justification::centredLeft, false);
    p.removeFromTop (6);
    g.drawText ("RELEASE", p.removeFromTop (24), juce::Justification::centredLeft, false);
}
