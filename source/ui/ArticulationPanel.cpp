#include "ui/ArticulationPanel.h"
#include "NoteNames.h"

#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int titleHeight = 26, rowHeight = 22, pad = 10, powerWidth = 16, noteWidth = 34, levelWidth = 58;
    constexpr int pedalHeight = 64, tailHeight = 44;
    constexpr float minDb = -48.0f, maxDb = 12.0f;

    juce::String noteName (int note) { return note >= 0 ? notenames::name (note) : juce::String ("-"); }
    juce::String upper (const char* text) { return juce::String (text).toUpperCase(); }
}

ArticulationPanel::ArticulationPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("The chosen drum's articulations: the ways it's played, each its own sound. Click a name to choose it and "
                "hear it, ALL for every one; the dot switches one off (its notes then play the nearest one that's on); "
                "drag a level.");
}

void ArticulationPanel::showSlot (int slot, const std::vector<int>& chosenSlots)
{
    const auto p = synth::drums::pieceOfSlot (slot);
    if (p < 0)
        return;
    if (p != piece || slot != slotShown || chosenSlots != chosen)
    {
        piece = p;
        slotShown = slot;
        chosen = chosenSlots;
        repaint();
    }
}

bool ArticulationPanel::isHat() const noexcept { return juce::String (synth::drums::pieces[(size_t) piece].name) == "HI-HAT"; }

bool ArticulationPanel::isCymbal() const noexcept
{
    // Every piece whose articulations are all cymbals (the model or what it's for): the crashes, ride, china, splash.
    const juce::String name (synth::drums::pieces[(size_t) piece].name);
    return name.startsWith ("CRASH") || name == "RIDE" || name == "CHINA" || name == "SPLASH";
}

int ArticulationPanel::wantedHeight() const
{
    const auto& pc = synth::drums::pieces[(size_t) piece];
    auto h = titleHeight + (pc.count > 1 ? rowHeight : 0) + pc.count * rowHeight + pad;
    if (isHat())
        h += pedalHeight;
    else if (isCymbal())
        h += tailHeight;
    return h;
}

juce::Rectangle<int> ArticulationPanel::allArea() const
{
    return { pad, titleHeight, getWidth() - 2 * pad, synth::drums::pieces[(size_t) piece].count > 1 ? rowHeight : 0 };
}

std::vector<ArticulationPanel::Row> ArticulationPanel::layoutRows() const
{
    std::vector<Row> rows;
    const auto& pc = synth::drums::pieces[(size_t) piece];
    auto y = allArea().getBottom();
    for (int a = 0; a < pc.count; ++a, y += rowHeight)
    {
        Row r;
        r.slot = pc.articulations[(size_t) a].slot;
        r.area = { pad, y, getWidth() - 2 * pad, rowHeight };
        auto inner = r.area.reduced (4, 0);
        r.power = inner.removeFromLeft (powerWidth);
        r.level = inner.removeFromRight (levelWidth).reduced (0, 7);
        inner.removeFromRight (noteWidth);
        r.name = inner;
        rows.push_back (r);
    }
    return rows;
}

juce::Rectangle<int> ArticulationPanel::extraArea() const
{
    const auto rows = layoutRows();
    const auto top = rows.empty() ? titleHeight : rows.back().area.getBottom();
    return { pad, top + 4, getWidth() - 2 * pad, isHat() ? pedalHeight - 4 : isCymbal() ? tailHeight - 4 : 0 };
}

juce::Rectangle<int> ArticulationPanel::pedalBar() const
{
    return extraArea().withTrimmedTop (18).withHeight (14).reduced (4, 0);
}

std::array<juce::Rectangle<int>, 3> ArticulationPanel::tailChoices() const
{
    auto area = extraArea().withTrimmedTop (18).withHeight (18).reduced (4, 0);
    const auto w = area.getWidth() / 3;
    return { area.removeFromLeft (w).reduced (2, 0), area.removeFromLeft (w).reduced (2, 0), area.reduced (2, 0) };
}

float ArticulationPanel::paramValue (const juce::String& id) const
{
    if (auto* p = processor.getParameters().getParameter (id))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void ArticulationPanel::setParam (const juce::String& id, float value)
{
    if (auto* p = processor.getParameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

int ArticulationPanel::pedalStep (int i) const
{
    return juce::roundToInt (paramValue (ParamIDs::drumsPedalSteps[i]));
}

void ArticulationPanel::toggleOn (int slot)
{
    const auto id = ParamIDs::drum (slot, "On");
    setParam (id, paramValue (id) > 0.5f ? 0.0f : 1.0f);
    repaint();
}

void ArticulationPanel::clickName (int slot)
{
    if (onChoose)
        onChoose (slot);
    processor.previewDrum (slot);
}

void ArticulationPanel::clickAll()
{
    std::vector<int> slots;
    const auto& pc = synth::drums::pieces[(size_t) piece];
    for (int a = 0; a < pc.count; ++a)
        slots.push_back (pc.articulations[(size_t) a].slot);
    if (onChooseAll)
        onChooseAll (slots);
}

void ArticulationPanel::setMuteTail (synth::drums::MuteTail tail)
{
    const auto& pc = synth::drums::pieces[(size_t) piece];
    for (int a = 0; a < pc.count; ++a)
        setParam (ParamIDs::drum (pc.articulations[(size_t) a].slot, "MuteTail"), (float) tail);
    repaint();
}

void ArticulationPanel::setPedalStep (int step, int value)
{
    // Kept in order: each step's mark stays between its neighbours (closed highest).
    const auto above = step > 0 ? pedalStep (step - 1) - 1 : 127;
    const auto below = step < 3 ? pedalStep (step + 1) + 1 : 1;
    setParam (ParamIDs::drumsPedalSteps[step], (float) juce::jlimit (below, juce::jmax (below, above), value));
    repaint();
}

juce::StringArray ArticulationPanel::describeRows() const
{
    juce::StringArray lines;
    const auto kit = processor.getKitSettings();
    const auto& pc = synth::drums::pieces[(size_t) piece];
    for (int a = 0; a < pc.count; ++a)
    {
        const auto slot = pc.articulations[(size_t) a].slot;
        auto line = juce::String (pc.articulations[(size_t) a].name) + " " + noteName (processor.homeNote (slot));
        if (kit.slots[(size_t) slot].active)
            line << " ON";
        else
        {
            line << " off";
            if (const auto as = kit.playsAs (slot); as >= 0)
                line << " -> " << synth::drums::articulationName (as);
        }
        lines.add (line);
    }
    return lines;
}

void ArticulationPanel::updateLights()
{
    const auto& kit = processor.getDrumKit();
    const auto& pc = synth::drums::pieces[(size_t) piece];
    auto changed = false;
    for (int a = 0; a < pc.count; ++a)
    {
        const auto age = kit.getHitAge (pc.articulations[(size_t) a].slot);
        const auto light = age >= 0.0f ? juce::jmax (0.0f, 1.0f - age / 0.3f) : 0.0f;
        if (std::abs (light - lights[(size_t) a]) > 0.02f)
        {
            lights[(size_t) a] = light;
            changed = true;
        }
    }
    if (isHat() && kit.getPedal() != pedal)
    {
        pedal = kit.getPedal();
        changed = true;
    }
    if (changed)
        repaint();
}

void ArticulationPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ParagonColours::background.withAlpha (0.86f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto& pc = synth::drums::pieces[(size_t) piece];
    const auto kit = processor.getKitSettings();
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);
    g.drawText (pc.name, juce::Rectangle<int> (pad + 4, 0, getWidth() - 2 * pad, titleHeight), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText ("ARTICULATIONS", juce::Rectangle<int> (pad, 0, getWidth() - 2 * pad - 4, titleHeight), juce::Justification::centredRight, false);

    // A stand-in's name: just the articulation's within this piece, else the piece's too ("CRASH 1").
    const auto labelOf = [&pc, this] (int slot)
    {
        if (synth::drums::pieceOfSlot (slot) == piece)
            for (int a = 0; a < pc.count; ++a)
                if (pc.articulations[(size_t) a].slot == slot)
                    return upper (pc.articulations[(size_t) a].name);
        return synth::drums::articulationName (slot);
    };
    const auto isChosen = [this] (int slot) { return std::find (chosen.begin(), chosen.end(), slot) != chosen.end(); };
    auto allChosen = pc.count > 1;
    for (int a = 0; a < pc.count; ++a)
        allChosen = allChosen && isChosen (pc.articulations[(size_t) a].slot);

    if (pc.count > 1)
    {
        const auto area = allArea();
        if (allChosen)
        {
            g.setColour (ParagonColours::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
        }
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (allChosen ? ParagonColours::accent : ParagonColours::text);
        g.drawText ("ALL", area.withTrimmedLeft (4 + powerWidth), juce::Justification::centredLeft, false);
    }

    const auto rows = layoutRows();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        const auto& s = kit.slots[(size_t) r.slot];
        const auto on = s.active;
        // Chosen: tinted; playing: a pearl light that fades.
        if (! allChosen && isChosen (r.slot))
        {
            g.setColour (ParagonColours::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (r.area.toFloat(), 3.0f);
        }
        if (lights[i] > 0.0f)
        {
            g.setColour (ParagonColours::text.withAlpha (0.18f * lights[i]));
            g.fillRoundedRectangle (r.area.toFloat(), 3.0f);
        }
        // ON.
        const auto dot = r.power.toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (on ? ParagonColours::meterRed : ParagonColours::panelEdge);
        g.fillEllipse (dot);
        g.setColour (on ? ParagonColours::meterRed.brighter (0.3f) : ParagonColours::textDim.withAlpha (0.5f));
        g.drawEllipse (dot, 1.0f);
        // The name, and its stand-in while it's off.
        g.setFont (juce::FontOptions (11.0f, isChosen (r.slot) ? juce::Font::bold : juce::Font::plain));
        g.setColour (on ? (isChosen (r.slot) ? ParagonColours::accent : ParagonColours::text) : ParagonColours::textDim);
        auto name = upper (pc.articulations[i].name);
        if (! on)
        {
            const auto as = kit.playsAs (r.slot);
            name << "  " << (as >= 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + labelOf (as) : juce::String ("SILENT"));
        }
        g.drawText (name, r.name, juce::Justification::centredLeft, true);
        // Its note in the map.
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        g.drawText (noteName (processor.homeNote (r.slot)), r.name.getRight(), r.area.getY(), noteWidth, rowHeight, juce::Justification::centredLeft, false);
        // LEVEL.
        const auto db = paramValue (ParamIDs::drum (r.slot, "Level"));
        const auto bar = r.level.toFloat();
        g.setColour (ParagonColours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        const auto share = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        g.setColour ((on ? ParagonColours::accent : ParagonColours::textDim).withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * share), 2.0f);
        const auto unity = bar.getX() + bar.getWidth() * (0.0f - minDb) / (maxDb - minDb);
        g.setColour (ParagonColours::text.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (unity), bar.getY() - 2.0f, bar.getBottom() + 2.0f);
    }

    const auto extra = extraArea();
    if (isHat())
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("PEDAL (CC 4)", extra.withHeight (16).withTrimmedLeft (4), juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        g.drawText ("OPEN  <  >  CLOSED", extra.withHeight (16).withTrimmedRight (4), juce::Justification::centredRight, false);
        // The bar: open on the left, closed on the right; each step's span, the pedal's position over it.
        const auto bar = pedalBar().toFloat();
        const auto xOf = [&bar] (float cc) { return bar.getX() + bar.getWidth() * cc / 127.0f; };
        g.setColour (ParagonColours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (ParagonColours::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (bar.withRight (xOf ((float) pedal)), 2.0f);
        const char* labels[] = { "C", "1", "2", "3", "4" };
        auto right = bar.getRight();
        for (int i = 0; i < 4; ++i)
        {
            const auto x = xOf ((float) pedalStep (i));
            g.setColour (ParagonColours::text);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f));
            g.setColour (ParagonColours::textDim);
            g.drawText (labels[i], juce::Rectangle<float> (x, bar.getBottom() + 2.0f, right - x, 12.0f), juce::Justification::centred, false);
            right = x;
        }
        g.drawText (labels[4], juce::Rectangle<float> (bar.getX(), bar.getBottom() + 2.0f, right - bar.getX(), 12.0f), juce::Justification::centred, false);
    }
    else if (isCymbal())
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText ("MUTE TAIL (GRAB IT)", extra.withHeight (16).withTrimmedLeft (4), juce::Justification::centredLeft, false);
        const auto current = juce::roundToInt (paramValue (ParamIDs::drum (slotShown, "MuteTail")));
        const auto choices = tailChoices();
        for (int i = 0; i < 3; ++i)
        {
            const auto lit = current == i;
            g.setColour (lit ? ParagonColours::accent.withAlpha (0.25f) : ParagonColours::panelEdge.withAlpha (0.6f));
            g.fillRoundedRectangle (choices[(size_t) i].toFloat(), 3.0f);
            g.setColour (lit ? ParagonColours::accent : ParagonColours::textDim);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (synth::drums::muteTailNames[i].toUpperCase(), choices[(size_t) i], juce::Justification::centred, false);
        }
    }
}

void ArticulationPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto at = e.getPosition();
    if (allArea().contains (at))
    {
        clickAll();
        return;
    }
    for (const auto& r : layoutRows())
        if (r.area.contains (at))
        {
            if (r.power.expanded (2).contains (at))
                toggleOn (r.slot);
            else if (r.level.expanded (0, 7).contains (at))
            {
                dragLevelSlot = r.slot;
                dragging = processor.getParameters().getParameter (ParamIDs::drum (r.slot, "Level"));
                if (dragging != nullptr)
                    dragging->beginChangeGesture();
                mouseDrag (e);
            }
            else
                clickName (r.slot);
            return;
        }
    if (isHat() && pedalBar().expanded (4, 8).contains (at))
    {
        // The nearest step's mark.
        const auto bar = pedalBar().toFloat();
        auto best = 1.0e9f;
        for (int i = 0; i < 4; ++i)
            if (const auto d = std::abs (bar.getX() + bar.getWidth() * (float) pedalStep (i) / 127.0f - (float) at.x); d < best)
            {
                best = d;
                dragStep = i;
            }
        dragging = processor.getParameters().getParameter (ParamIDs::drumsPedalSteps[dragStep]);
        if (dragging != nullptr)
            dragging->beginChangeGesture();
        return;
    }
    if (isCymbal())
    {
        const auto choices = tailChoices();
        for (int i = 0; i < 3; ++i)
            if (choices[(size_t) i].contains (at))
                setMuteTail ((synth::drums::MuteTail) i);
    }
}

void ArticulationPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == nullptr)
        return;
    if (dragLevelSlot >= 0)
    {
        for (const auto& r : layoutRows())
            if (r.slot == dragLevelSlot)
            {
                const auto share = juce::jlimit (0.0f, 1.0f, (float) (e.x - r.level.getX()) / (float) juce::jmax (1, r.level.getWidth()));
                dragging->setValueNotifyingHost (dragging->convertTo0to1 (minDb + share * (maxDb - minDb)));
            }
    }
    else if (dragStep >= 0)
    {
        const auto bar = pedalBar();
        const auto cc = juce::roundToInt (127.0f * (float) (e.x - bar.getX()) / (float) juce::jmax (1, bar.getWidth()));
        const auto above = dragStep > 0 ? pedalStep (dragStep - 1) - 1 : 127;
        const auto below = dragStep < 3 ? pedalStep (dragStep + 1) + 1 : 1;
        dragging->setValueNotifyingHost (dragging->convertTo0to1 ((float) juce::jlimit (below, juce::jmax (below, above), cc)));
    }
    repaint();
}

void ArticulationPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging != nullptr)
        dragging->endChangeGesture();
    dragging = nullptr;
    dragLevelSlot = dragStep = -1;
}

void ArticulationPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (const auto& r : layoutRows())
        if (r.level.expanded (0, 7).contains (e.getPosition()))
        {
            setParam (ParamIDs::drum (r.slot, "Level"), 0.0f);
            repaint();
        }
}
