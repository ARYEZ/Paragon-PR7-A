#include "ui/MidiInPanel.h"
#include "NoteNames.h"

#include "dsp/synth/NoteMap.h"
#include "ui/DrumSeqPage.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int tabHeight = 26, monitorLines = 9, lineHeight = 15;
    juce::String noteName (int n) { return notenames::name (n); }
    juce::String arrow() { return juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")); }

    juce::String drumName (int slot)
    {
        if (slot == synth::drums::notemap::PEDAL)
            return "HI-HAT (PEDAL)";
        if (slot < 0 || slot >= synth::drums::numSlots)
            return "-";
        return juce::String (synth::drums::slotDefaults[(size_t) slot].key) + " " + synth::drums::articulationName (slot);
    }

    // THE HI-HAT's starting points (ours): where each step begins, closed first.
    struct StepPreset { const char* name; std::array<int, 4> steps; };
    constexpr StepPreset stepPresets[] = {
        { "Default", { 102, 77, 51, 26 } },
        { "Closes Sooner (pedals that stop short of 127)", { 90, 68, 45, 23 } },
        { "Opens Sooner", { 116, 100, 80, 50 } },
    };
}

MidiInPanel::MidiInPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    for (auto* b : { &mappingTab, &velocityTab, &hihatTab })
    {
        DrumSeqPanel::style (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (965);
        addAndMakeVisible (*b);
    }
    mappingTab.setToggleState (true, juce::dontSendNotification);
    mappingTab.onClick = [this] { showTab (Tab::mapping); };
    velocityTab.onClick = [this] { showTab (Tab::velocity); };
    hihatTab.onClick = [this] { showTab (Tab::hihat); };
    mappingTab.setTooltip ("The chosen note: its drum, LEARN, and CC ZONES for a pad that senses where it's hit.");
    velocityTab.setTooltip ("How hard each incoming note plays: a curve for your pads' or keys' feel (grooves aren't changed by it).");
    hihatTab.setTooltip ("Your hi-hat pedal (CC 4): where each openness step begins, and CALIBRATE.");

    // MAPPING.
    drumBox.addItem ("-", 1);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        drumBox.addItem (drumName (k), k + 2);
    drumBox.addItem (drumName (synth::drums::notemap::PEDAL), synth::drums::notemap::PEDAL + 2);
    drumBox.setTooltip ("The drum the chosen note plays (changing it makes the map CUSTOM).");
    drumBox.onChange = [this]
    {
        const auto slot = drumBox.getSelectedId() - 2;
        if (slot != processor.getNoteMap()[(size_t) note])
            processor.mapNote (note, slot);
    };
    learn.setTooltip ("LEARN: hit a pad (or key). Its note then plays the chosen note's drum.");
    learn.setClickingTogglesState (true);
    learn.onClick = [this] { learning = learn.getToggleState(); repaint(); };
    learnController.setTooltip ("Hit the pad: the controller it sends with its note (where it was hit) becomes the zones' controller.");
    learnController.setClickingTogglesState (true);
    learnController.onClick = [this] { learningController = learnController.getToggleState(); };
    for (auto* b : { &learn, &learnController })
        DrumSeqPanel::style (*b);
    zoneCount.addItemList ({ "Off", "2 zones", "3 zones", "4 zones" }, 1);
    zoneCount.setTooltip ("CC ZONES: a pad that senses where it's hit (centre, edge, rim...) sends a controller with each note; each "
                          "range of that controller plays its own drum. Off: the note plays its drum, wherever it's hit.");
    zoneCount.onChange = [this] { setZoneCount (zoneCount.getSelectedId() <= 1 ? 0 : zoneCount.getSelectedId()); };
    for (int cc = 0; cc < 128; ++cc)
        zoneController.addItem ("CC " + juce::String (cc), cc + 1);
    zoneController.setTooltip ("The controller the pad sends where it's hit (many pads: CC 16).");
    zoneController.onChange = [this]
    {
        auto in = processor.getMidiIn();
        if (auto* z = in.zonesFor (note))
        {
            z->cc = (std::int8_t) (zoneController.getSelectedId() - 1);
            processor.setMidiIn (in);
        }
    };
    for (size_t z = 0; z < zoneSlots.size(); ++z)
    {
        auto& box = zoneSlots[z];
        box.addItem ("As mapped", 1);
        for (int k = 0; k < synth::drums::numSlots; ++k)
            box.addItem (drumName (k), k + 2);
        box.setTooltip ("What this zone plays (zone 1: the controller's lowest values).");
        box.onChange = [this, z] { setZoneSlot ((int) z, zoneSlots[z].getSelectedId() - 2); };
    }
    for (juce::Component* c : { (juce::Component*) &drumBox, (juce::Component*) &learn, (juce::Component*) &zoneCount,
                                (juce::Component*) &zoneController, (juce::Component*) &learnController })
        addChildComponent (c);
    for (auto& box : zoneSlots)
        addChildComponent (box);

    // VELOCITY.
    curveEditor.getCurve = [this] { return processor.getMidiIn().curve; };
    curveEditor.onChange = [this] (const synth::drums::VelocityCurve& c, bool)
    {
        auto in = processor.getMidiIn();
        in.curve = c;
        processor.setMidiIn (in);
        softButton.setToggleState (c.soft, juce::dontSendNotification);
        gateButton.setToggleState (c.gate, juce::dontSendNotification);
    };
    curveEditor.setLabel ("EVERY INCOMING NOTE");
    addChildComponent (curveEditor);
    for (auto* b : { &softButton, &gateButton })
    {
        DrumSeqPanel::style (*b);
        b->setClickingTogglesState (true);
        addChildComponent (*b);
    }
    softButton.onClick = [this] { auto c = processor.getMidiIn().curve; c.soft = softButton.getToggleState(); curveEditor.onChange (c, true); curveEditor.repaint(); };
    gateButton.onClick = [this] { auto c = processor.getMidiIn().curve; c.gate = gateButton.getToggleState(); curveEditor.onChange (c, true); curveEditor.repaint(); };
    softButton.setTooltip ("SOFT: the curve rounded between its points.");
    gateButton.setTooltip ("GATE: notes softer than the left end don't play (off: they play at its height).");
    DrumSeqPanel::style (straight);
    straight.setTooltip ("A straight line: every note as hard as it comes.");
    straight.onClick = [this] { curveEditor.onChange ({}, true); curveEditor.repaint(); };
    addChildComponent (straight);

    // HI-HAT.
    for (auto* b : { &stepsMenu, &calibrate })
    {
        DrumSeqPanel::style (*b);
        addChildComponent (*b);
    }
    stepsMenu.setTooltip ("Ready-made steps to start from.");
    stepsMenu.onClick = [this]
    {
        juce::PopupMenu menu;
        for (const auto& preset : stepPresets)
            menu.addItem (preset.name, [this, steps = preset.steps]
            {
                for (int i = 0; i < 4; ++i)
                    if (auto* param = processor.getParameters().getParameter (ParamIDs::drumsPedalSteps[i]))
                    {
                        param->beginChangeGesture();
                        param->setValueNotifyingHost (param->convertTo0to1 ((float) steps[(size_t) i]));
                        param->endChangeGesture();
                    }
                repaint();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stepsMenu));
    };
    calibrate.setTooltip ("CALIBRATE: press the pedal right down and let it right up, a few times, then press DONE. The steps then "
                          "spread over the range your pedal really sends (some never reach 127, so the hat never fully closed).");
    calibrate.onClick = [this] { calibrating ? finishCalibration() : startCalibration(); };

    // THE MIDI MONITOR.
    DrumSeqPanel::style (clearMonitor);
    clearMonitor.onClick = [this] { lines.clear(); repaint(); };
    addAndMakeVisible (clearMonitor);

    showTab (Tab::mapping);
    startTimerHz (30);
}

MidiInPanel::~MidiInPanel() { stopTimer(); }

void MidiInPanel::setNote (int n)
{
    n = juce::jlimit (0, 127, n);
    if (n == note)
        return;
    note = n;
    refreshMapping();
    repaint();
}

void MidiInPanel::showTab (Tab t)
{
    tab = t;
    (t == Tab::mapping ? mappingTab : t == Tab::velocity ? velocityTab : hihatTab).setToggleState (true, juce::dontSendNotification);
    const auto mapping = t == Tab::mapping, velocity = t == Tab::velocity, hihat = t == Tab::hihat;
    for (juce::Component* c : { (juce::Component*) &drumBox, (juce::Component*) &learn, (juce::Component*) &zoneCount })
        c->setVisible (mapping);
    curveEditor.setVisible (velocity);
    for (auto* b : { &softButton, &gateButton, &straight })
        b->setVisible (velocity);
    stepsMenu.setVisible (hihat);
    calibrate.setVisible (hihat);
    if (velocity)
    {
        softButton.setToggleState (processor.getMidiIn().curve.soft, juce::dontSendNotification);
        gateButton.setToggleState (processor.getMidiIn().curve.gate, juce::dontSendNotification);
    }
    refreshMapping();
    resized();
    repaint();
}

void MidiInPanel::refreshMapping()
{
    const auto mapping = tab == Tab::mapping;
    drumBox.setSelectedId (processor.getNoteMap()[(size_t) note] + 2, juce::dontSendNotification);
    const auto* zones = [this]() -> const synth::drums::KitSettings::CcZones*
    {
        for (const auto& z : processor.getMidiIn().zones)
            if (z.note == note && z.count >= 2)
                return &z;
        return nullptr;
    }();
    const auto count = zones != nullptr ? (int) zones->count : 0;
    zoneCount.setSelectedId (count >= 2 ? count : 1, juce::dontSendNotification);
    zoneController.setVisible (mapping && count >= 2);
    learnController.setVisible (mapping && count >= 2);
    if (zones != nullptr)
        zoneController.setSelectedId (zones->cc + 1, juce::dontSendNotification);
    for (int z = 0; z < 4; ++z)
    {
        zoneSlots[(size_t) z].setVisible (mapping && z < count);
        if (zones != nullptr)
            zoneSlots[(size_t) z].setSelectedId (zones->slots[(size_t) z] + 2, juce::dontSendNotification);
    }
}

void MidiInPanel::startLearn()
{
    learning = true;
    learn.setToggleState (true, juce::dontSendNotification);
    repaint();
}

void MidiInPanel::startLearnController()
{
    learningController = true;
    learnController.setToggleState (true, juce::dontSendNotification);
}

void MidiInPanel::setZoneCount (int count)
{
    auto in = processor.getMidiIn();
    if (auto* z = in.zonesFor (note))
    {
        z->count = (std::int8_t) (count >= 2 ? juce::jmin (4, count) : 0);
        // The controller's range shared out evenly to start with.
        for (int i = 0; i < 3; ++i)
            z->bounds[(size_t) i] = (std::uint8_t) juce::jlimit (1, 127, juce::roundToInt (128.0 * (i + 1) / juce::jmax (2, count)));
        if (z->count == 0)
            *z = {};
        processor.setMidiIn (in);
    }
    refreshMapping();
    resized();
    repaint();
}

void MidiInPanel::setZoneSlot (int zone, int slot)
{
    auto in = processor.getMidiIn();
    if (auto* z = in.zonesFor (note); z != nullptr && zone >= 0 && zone < 4)
    {
        z->slots[(size_t) zone] = (std::int8_t) juce::jlimit (-1, synth::drums::numSlots - 1, slot);
        processor.setMidiIn (in);
    }
    refreshMapping();
}

void MidiInPanel::setZoneBound (int index, int value)
{
    auto in = processor.getMidiIn();
    if (auto* z = in.zonesFor (note); z != nullptr && index >= 0 && index < z->count - 1)
    {
        // Kept rising: each between its neighbours.
        const auto lo = index > 0 ? z->bounds[(size_t) index - 1] + 1 : 1;
        const auto hi = index < z->count - 2 ? z->bounds[(size_t) index + 1] - 1 : 127;
        z->bounds[(size_t) index] = (std::uint8_t) juce::jlimit (lo, juce::jmax (lo, hi), value);
        processor.setMidiIn (in);
    }
    repaint();
}

void MidiInPanel::startCalibration()
{
    calibrating = true;
    calibrationLow = 127;
    calibrationHigh = 0;
    calibrate.setButtonText ("DONE");
    repaint();
}

void MidiInPanel::finishCalibration()
{
    calibrating = false;
    calibrate.setButtonText ("CALIBRATE");
    // At least a fifth of the range moved: the steps over what the pedal sent, closed in its top fifth.
    if (calibrationHigh - calibrationLow >= 25)
    {
        const auto range = (float) (calibrationHigh - calibrationLow);
        for (int i = 0; i < 4; ++i)
            setStep (i, juce::roundToInt ((float) calibrationLow + range * (0.8f - 0.2f * (float) i)));
    }
    repaint();
}

void MidiInPanel::setStep (int index, int value)
{
    if (auto* param = processor.getParameters().getParameter (ParamIDs::drumsPedalSteps[index]))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) juce::jlimit (0, 127, value)));
        param->endChangeGesture();
    }
}

juce::String MidiInPanel::describe (const synth::drums::Kit::MidiEvent& e) const
{
    using Type = synth::drums::Kit::MidiEvent::Type;
    const auto ch = "ch " + juce::String (e.channel) + "  ";
    switch (e.type)
    {
        case Type::controller:
            return ch + "CC " + juce::String (e.number) + "  = " + juce::String (e.value) + (e.number == 4 ? "  (hi-hat pedal)" : "");
        case Type::aftertouch:
            return ch + "AFTERTOUCH " + juce::String (e.number) + " " + noteName (e.number) + "  " + juce::String (e.value);
        case Type::noteOff:
            return ch + "OFF " + juce::String (e.number) + " " + noteName (e.number);
        case Type::noteOn:
            return ch + "NOTE " + juce::String (e.number) + " " + noteName (e.number) + arrow() + (e.slot >= 0 ? drumName (e.slot) : juce::String ("nothing"))
                   + "   vel " + juce::String (e.value) + (e.valueOut < 0 ? arrow() + "gated" : e.valueOut != e.value ? arrow() + juce::String (e.valueOut) : juce::String());
    }
    return {};
}

juce::StringArray MidiInPanel::getMonitorLines() const
{
    juce::StringArray result;
    for (const auto& l : lines)
        result.add (l.text);
    return result;
}

void MidiInPanel::pollMidi()
{
    using Type = synth::drums::Kit::MidiEvent::Type;
    std::array<synth::drums::Kit::MidiEvent, 128> events;
    auto changed = false;
    for (;;)
    {
        const auto n = processor.popMidiEvents (events.data(), (int) events.size());
        if (n <= 0)
            break;
        for (int i = 0; i < n; ++i)
        {
            const auto& e = events[(size_t) i];
            lines.push_front ({ e, describe (e) });
            changed = true;
            if (e.type == Type::controller)
            {
                controllerValues[e.number] = e.value;
                if (learningController)
                {
                    learningController = false;
                    learnController.setToggleState (false, juce::dontSendNotification);
                    zoneController.setSelectedId (e.number + 1, juce::sendNotificationSync);
                }
                if (calibrating && e.number == 4)
                {
                    calibrationLow = juce::jmin (calibrationLow, (int) e.value);
                    calibrationHigh = juce::jmax (calibrationHigh, (int) e.value);
                }
            }
            if (e.type == Type::noteOn)
            {
                curveEditor.showVelocity (e.value);
                if (learning)
                {
                    // That pad's note now plays the chosen note's drum; it becomes the chosen note.
                    learning = false;
                    learn.setToggleState (false, juce::dontSendNotification);
                    processor.mapNote (e.number, processor.getNoteMap()[(size_t) note]);
                    setNote (e.number);
                    if (onChooseNote)
                        onChooseNote (e.number);
                }
            }
        }
    }
    while (lines.size() > 60)
        lines.pop_back();
    if (changed)
        repaint();
}

void MidiInPanel::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    pollMidi();
    if (tab == Tab::hihat || tab == Tab::mapping)
        repaint (tab == Tab::hihat ? pedalBar().expanded (4, 20) : zoneBar().expanded (4, 20));
}

juce::Rectangle<int> MidiInPanel::monitorArea() const
{
    return getLocalBounds().removeFromBottom (24 + monitorLines * lineHeight + 8);
}

juce::Rectangle<int> MidiInPanel::zoneBar() const
{
    return { 4, zoneCount.getBottom() + 26, getWidth() - 8, 14 };
}

juce::Rectangle<int> MidiInPanel::pedalBar() const
{
    return { 4, tabHeight + 52, getWidth() - 8, 14 };
}

void MidiInPanel::resized()
{
    auto area = getLocalBounds();
    auto tabs = area.removeFromTop (tabHeight);
    const auto w = tabs.getWidth() / 3;
    mappingTab.setBounds (tabs.removeFromLeft (w).reduced (2, 0));
    velocityTab.setBounds (tabs.removeFromLeft (w).reduced (2, 0));
    hihatTab.setBounds (tabs.reduced (2, 0));
    area.removeFromBottom (monitorArea().getHeight());
    const auto top = area.withTrimmedTop (8);
    clearMonitor.setBounds (monitorArea().removeFromTop (22).removeFromRight (70));

    // MAPPING.
    {
        auto m = top.withTrimmedTop (22);
        auto row = m.removeFromTop (26);
        drumBox.setBounds (row.removeFromLeft (row.getWidth() - 80));
        row.removeFromLeft (6);
        learn.setBounds (row);
        m.removeFromTop (30);
        row = m.removeFromTop (24);
        zoneCount.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (6);
        zoneController.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        learnController.setBounds (row.removeFromLeft (80));
        m.removeFromTop (50);   // the zones' bar (painted)
        const auto half = m.getWidth() / 2;
        for (int z = 0; z < 4; ++z)
            zoneSlots[(size_t) z].setBounds (m.getX() + (z % 2) * half, m.getY() + (z / 2) * 28, half - 4, 24);
    }
    // VELOCITY.
    {
        auto v = top;
        auto buttons = v.removeFromBottom (26);
        const auto bw = buttons.getWidth() / 3;
        softButton.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
        gateButton.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
        straight.setBounds (buttons.reduced (2, 0));
        v.removeFromBottom (6);
        curveEditor.setBounds (v);
    }
    // HI-HAT.
    {
        auto h = top.withTrimmedTop (100);
        stepsMenu.setBounds (h.removeFromTop (26).withWidth (juce::jmin (200, h.getWidth())));
        h.removeFromTop (8);
        calibrate.setBounds (h.removeFromTop (26).withWidth (juce::jmin (200, h.getWidth())));
    }
}

void MidiInPanel::paint (juce::Graphics& g)
{
    const auto small = [&g] (const juce::String& text, juce::Rectangle<int> r, juce::Colour c = ParagonColours::textDim, bool bold = false)
    {
        g.setColour (c);
        g.setFont (juce::FontOptions (10.5f, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (text, r, juce::Justification::centredLeft, true);
    };
    const auto top = getLocalBounds().withTrimmedTop (tabHeight + 8);
    if (tab == Tab::mapping)
    {
        small ("NOTE " + juce::String (note) + "  " + noteName (note) + " PLAYS", top.withHeight (18), ParagonColours::text, true);
        if (learning)
            small ("HIT A PAD: ITS NOTE WILL PLAY THIS", top.withTrimmedTop (50).withHeight (16), ParagonColours::accent, true);
        small ("CC ZONES", top.withTrimmedTop (64).withHeight (16), ParagonColours::text, true);
        // The zones over the controller's range, its last value live.
        const auto* zones = [this]() -> const synth::drums::KitSettings::CcZones*
        {
            for (const auto& z : processor.getMidiIn().zones)
                if (z.note == note && z.count >= 2)
                    return &z;
            return nullptr;
        }();
        if (zones != nullptr)
        {
            const auto bar = zoneBar().toFloat();
            const auto xOf = [&bar] (float v) { return bar.getX() + bar.getWidth() * v / 127.0f; };
            g.setColour (ParagonColours::panelEdge);
            g.fillRoundedRectangle (bar, 2.0f);
            const auto value = controllerValues[(size_t) juce::jlimit (0, 127, (int) zones->cc)];
            g.setColour (ParagonColours::accent.withAlpha (0.5f));
            g.fillRoundedRectangle (bar.withRight (xOf ((float) value)), 2.0f);
            auto left = bar.getX();
            for (int i = 0; i < zones->count; ++i)
            {
                const auto right = i < zones->count - 1 ? xOf ((float) zones->bounds[(size_t) i]) : bar.getRight();
                if (i < zones->count - 1)
                {
                    g.setColour (ParagonColours::text);
                    g.fillRect (juce::Rectangle<float> (right - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f));
                }
                small (juce::String (i + 1), juce::Rectangle<float> (left, bar.getBottom() + 2.0f, right - left, 12.0f).toNearestInt()
                                                  .withSizeKeepingCentre (12, 12));
                left = right;
            }
            small ("ZONE 1 LOW " + arrow() + " HIGH", zoneBar().translated (0, -16).withHeight (14));
        }
    }
    else if (tab == Tab::hihat)
    {
        small ("PEDAL (CC 4)", top.withHeight (16), ParagonColours::text, true);
        small ("OPEN  <  >  CLOSED", top.withHeight (16).withTrimmedLeft (top.getWidth() - 110));
        const auto bar = pedalBar().toFloat();
        const auto xOf = [&bar] (float cc) { return bar.getX() + bar.getWidth() * cc / 127.0f; };
        const auto pedal = processor.getDrumKit().getPedal();
        g.setColour (ParagonColours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (ParagonColours::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (bar.withRight (xOf ((float) pedal)), 2.0f);
        if (calibrating && calibrationHigh >= calibrationLow)
        {
            g.setColour (ParagonColours::text.withAlpha (0.25f));
            g.fillRect (bar.withLeft (xOf ((float) calibrationLow)).withRight (xOf ((float) calibrationHigh)).expanded (0.0f, 4.0f));
        }
        const char* labels[] = { "C", "1", "2", "3", "4" };
        auto right = bar.getRight();
        for (int i = 0; i < 4; ++i)
        {
            const auto x = xOf ((float) juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drumsPedalSteps[i])->load()));
            g.setColour (ParagonColours::text);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f));
            small (labels[i], juce::Rectangle<float> (x, bar.getBottom() + 2.0f, right - x, 12.0f).toNearestInt().withSizeKeepingCentre (10, 12));
            right = x;
        }
        small (labels[4], juce::Rectangle<float> (bar.getX(), bar.getBottom() + 2.0f, right - bar.getX(), 12.0f).toNearestInt().withSizeKeepingCentre (10, 12));
        small ("PEDAL NOW: " + juce::String (pedal) + (calibrating ? "   SEEN: " + juce::String (calibrationLow) + " TO "
                                                                         + juce::String (calibrationHigh) : juce::String()),
               top.withTrimmedTop (80).withHeight (14), ParagonColours::text);
        if (calibrating)
        {
            g.setColour (ParagonColours::accent);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawFittedText ("PRESS THE PEDAL RIGHT DOWN AND LET IT RIGHT UP, A FEW TIMES. THEN PRESS DONE.",
                              top.withTrimmedTop (166).withHeight (30), juce::Justification::topLeft, 2);
        }
    }

    // THE MIDI MONITOR.
    auto m = monitorArea();
    g.setColour (ParagonColours::panel);
    g.fillRoundedRectangle (m.toFloat(), 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (m.toFloat().reduced (0.5f), 4.0f, 1.0f);
    auto title = m.removeFromTop (22).reduced (8, 0);
    small ("MIDI MONITOR", title, ParagonColours::text, true);
    m.reduce (8, 2);
    if (lines.empty())
        small ("Play your pads or keys: what comes in shows here.", m.removeFromTop (lineHeight));
    for (size_t i = 0; i < lines.size() && i < (size_t) monitorLines; ++i)
    {
        const auto& l = lines[i];
        small (l.text, m.removeFromTop (lineHeight), i == 0 ? ParagonColours::text : ParagonColours::textDim);
    }
}

int MidiInPanel::zoneIndexForDrag (int x) const
{
    for (const auto& z : processor.getMidiIn().zones)
        if (z.note == note && z.count >= 2)
        {
            const auto bar = zoneBar();
            auto best = -1;
            auto bestDistance = 12;
            for (int i = 0; i < z.count - 1; ++i)
                if (const auto d = std::abs (bar.getX() + bar.getWidth() * z.bounds[(size_t) i] / 127 - x); d < bestDistance)
                {
                    best = i;
                    bestDistance = d;
                }
            return best;
        }
    return -1;
}

void MidiInPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto at = e.getPosition();
    if (tab == Tab::mapping && zoneBar().expanded (0, 8).contains (at))
    {
        dragZone = zoneIndexForDrag (at.x);
        return;
    }
    if (tab == Tab::hihat && pedalBar().expanded (0, 8).contains (at))
    {
        const auto bar = pedalBar();
        auto best = 1000;
        for (int i = 0; i < 4; ++i)
        {
            const auto x = bar.getX() + juce::roundToInt ((float) bar.getWidth()
                               * processor.getParameters().getRawParameterValue (ParamIDs::drumsPedalSteps[i])->load() / 127.0f);
            if (std::abs (x - at.x) < best)
            {
                best = std::abs (x - at.x);
                dragStep = i;
            }
        }
        draggingStep = processor.getParameters().getParameter (ParamIDs::drumsPedalSteps[dragStep]);
        if (draggingStep != nullptr)
            draggingStep->beginChangeGesture();
        return;
    }
    // A note in the monitor: chosen.
    auto m = monitorArea().withTrimmedTop (24).reduced (8, 2);
    if (m.contains (at))
    {
        const auto index = (size_t) ((at.y - m.getY()) / lineHeight);
        if (index < lines.size() && lines[index].event.type != synth::drums::Kit::MidiEvent::Type::controller)
        {
            setNote (lines[index].event.number);
            if (onChooseNote)
                onChooseNote (note);
        }
    }
}

void MidiInPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragZone >= 0)
    {
        const auto bar = zoneBar();
        setZoneBound (dragZone, juce::roundToInt (127.0f * (float) (e.x - bar.getX()) / (float) juce::jmax (1, bar.getWidth())));
    }
    else if (dragStep >= 0 && draggingStep != nullptr)
    {
        const auto bar = pedalBar();
        const auto cc = juce::roundToInt (127.0f * (float) (e.x - bar.getX()) / (float) juce::jmax (1, bar.getWidth()));
        const auto step = [this] (int i) { return juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drumsPedalSteps[i])->load()); };
        const auto above = dragStep > 0 ? step (dragStep - 1) - 1 : 127;
        const auto below = dragStep < 3 ? step (dragStep + 1) + 1 : 1;
        draggingStep->setValueNotifyingHost (draggingStep->convertTo0to1 ((float) juce::jlimit (below, juce::jmax (below, above), cc)));
        repaint();
    }
}

void MidiInPanel::mouseUp (const juce::MouseEvent&)
{
    if (draggingStep != nullptr)
        draggingStep->endChangeGesture();
    draggingStep = nullptr;
    dragZone = dragStep = -1;
}
