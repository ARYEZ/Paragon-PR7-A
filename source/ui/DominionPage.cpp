#include "DominionPage.h"

#include "DockPanels.h"
#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr double lowestHz = 20.0, highestHz = 20000.0, rangeDb = 24.0;

    bool isCut (dominion::Shape s) { return s == dominion::Shape::lowCut || s == dominion::Shape::highCut; }
    bool usesDepth (dominion::Shape s)
    {
        return s == dominion::Shape::bell || s == dominion::Shape::lowShelf || s == dominion::Shape::highShelf || s == dominion::Shape::tilt;
    }
    bool usesSlope (dominion::Shape s)
    {
        return isCut (s) || s == dominion::Shape::lowShelf || s == dominion::Shape::highShelf || s == dominion::Shape::bandPass;
    }

    juce::String frequencyText (double hz)
    {
        return hz < 1000.0 ? juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz" : juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }

    void styleKnob (juce::Slider& knob)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    }

    void styleLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setJustificationType (juce::Justification::centred);
    }

    void styleCombo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
        box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box.setColour (juce::ComboBox::textColourId, ParagonColours::text);
        box.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    }

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }
}

//==============================================================================
DominionDisplay::DominionDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("DOMINION graph");
    shown = processor.getDominionSettings();
    startTimerHz (30);
}

void DominionDisplay::refresh()
{
    shown = processor.getDominionSettings();
    if (selected >= 0 && ! shown.bands[(size_t) selected].used)
        select (-1);
    repaint();
}

void DominionDisplay::select (int band)
{
    if (band == selected)
        return;
    selected = band;
    if (onSelect)
        onSelect (band);
    repaint();
}

void DominionDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
}

float DominionDisplay::xForFrequency (double hz) const noexcept
{
    return plot.getX() + (float) (std::log (hz / lowestHz) / std::log (highestHz / lowestHz)) * plot.getWidth();
}

double DominionDisplay::frequencyForX (float x) const noexcept
{
    return juce::jlimit (lowestHz, highestHz, lowestHz * std::pow (highestHz / lowestHz, (double) ((x - plot.getX()) / plot.getWidth())));
}

// 0 dB a third of the way down: the depth curve rises above it, the cuts hang below it.
float DominionDisplay::yForDb (double db) const noexcept
{
    const auto zero = plot.getY() + plot.getHeight() * 0.36f;
    return zero - (float) (db / rangeDb) * plot.getHeight() * 0.36f;
}

double DominionDisplay::dbForY (float y) const noexcept
{
    const auto zero = plot.getY() + plot.getHeight() * 0.36f;
    return (double) ((zero - y) / (plot.getHeight() * 0.36f)) * rangeDb;
}

juce::Colour DominionDisplay::colourAt (double hz)
{
    const auto proportion = std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz);
    return ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f }).getColourAtPosition (proportion);
}

juce::Point<float> DominionDisplay::dotFor (const dominion::Band& b) const noexcept
{
    return { xForFrequency (b.frequency), yForDb (usesDepth (b.shape) ? juce::jlimit (-rangeDb, rangeDb, b.depthDb) : 0.0) };
}

int DominionDisplay::bandAt (juce::Point<float> position) const
{
    auto nearest = -1;
    auto best = 12.0f;
    for (int i = 0; i < dominion::maxBands; ++i)
        if (const auto& b = shown.bands[(size_t) i]; b.used)
            if (const auto d = dotFor (b).getDistanceFrom (position); d < best)
            {
                best = d;
                nearest = i;
            }
    return nearest;
}

juce::RangedAudioParameter& DominionDisplay::parameter (int band, const char* what) const
{
    return *processor.getParameters().getParameter (ParamIDs::domBand (band, what));
}

void DominionDisplay::setBand (int band, const char* what, float plainValue)
{
    auto& p = parameter (band, what);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    p.endChangeGesture();
}

int DominionDisplay::createBand (dominion::Shape shape, double frequency, double depthDb)
{
    for (int band = 0; band < dominion::maxBands; ++band)
    {
        if (parameter (band, "Used").getValue() > 0.5f)
            continue;
        // Everything set before "Used", so it comes in as it's meant.
        setBand (band, "Shape", (float) shape);
        setBand (band, "Freq", (float) frequency);
        setBand (band, "Depth", (float) depthDb);
        setBand (band, "Q", 1.0f);
        setBand (band, "Slope", (float) eq::defaultSlope);
        setBand (band, "Focus", 0.0f);
        setBand (band, "On", 1.0f);
        setBand (band, "Used", 1.0f);
        shown = processor.getDominionSettings();
        return band;
    }
    return -1;
}

void DominionDisplay::removeBand (int band)
{
    setBand (band, "Used", 0.0f);
    shown = processor.getDominionSettings();
    if (band == selected)
        select (-1);
}

void DominionDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    // Grid
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const auto x = xForFrequency (hz);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ParagonColours::textDim);
        g.drawText (hz < 1000.0 ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0)) + "k",
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }
    for (auto db = -36.0; db <= rangeDb + 0.1; db += 12.0)
    {
        const auto y = yForDb (db);
        if (y > plot.getBottom())
            continue;
        g.setColour (juce::Colours::white.withAlpha (std::abs (db) < 0.1 ? 0.18f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText ((db > 0.0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 32.0f, y - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (2.0f).toNearestInt());
    const auto& d = processor.getDominion();
    const auto zero = yForDb (0.0);

    // What it's cutting now, in the menus' red to pink to orange (left to
    // right across the graph): the right/side channel fainter, behind the left/mid.
    const auto channels = shown.stereo == dominion::StereoMode::midSide || shown.link < 99.5 ? 2 : 1;
    for (int c = channels - 1; c >= 0; --c)
    {
        juce::Path area;
        area.startNewSubPath (plot.getX(), zero);
        for (int k = 0; k < d.getNumFilters(); ++k)
            area.lineTo (xForFrequency (d.getFrequency (k)), yForDb (-(double) d.getReductionDb (c, k)));
        area.lineTo (plot.getRight(), zero);
        area.closeSubPath();
        auto fill = ParagonLookAndFeel::menuHighlight (plot);
        fill.multiplyOpacity (c == 0 ? 0.7f : 0.3f);
        g.setGradientFill (fill);
        g.fillPath (area);
        auto edge = ParagonLookAndFeel::menuHighlight (plot);
        edge.multiplyOpacity (c == 0 ? 1.0f : 0.5f);
        g.setGradientFill (edge);
        g.strokePath (area, juce::PathStrokeType (1.2f));
    }

    // The depth curve: left/mid white, right/side darker where it differs.
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    for (int c = 1; c >= 0; --c)
    {
        juce::Path path;
        for (auto x = plot.getX(); x <= plot.getRight() + 1.0f; x += 2.0f)
        {
            const auto y = yForDb (juce::jlimit (-rangeDb * 1.6, rangeDb * 1.1, dominion::depthCurveDb (shown, frequencyForX (x), c, sampleRate)));
            if (x <= plot.getX())
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (c == 0 ? juce::Colours::white : juce::Colours::white.withAlpha (0.35f));
        g.strokePath (path, juce::PathStrokeType (c == 0 ? 2.2f : 1.4f));
    }

    // The dots.
    for (int i = 0; i < dominion::maxBands; ++i)
    {
        const auto& b = shown.bands[(size_t) i];
        if (! b.used)
            continue;
        const auto dot = juce::Rectangle<float> (16.0f, 16.0f).withCentre (dotFor (b));
        const auto colour = colourAt (b.frequency);   // the gradient where it sits
        if (i == selected || i == hovered)
        {
            g.setColour (colour.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (5.0f));
        }
        if (b.on)
        {
            g.setColour (juce::Colours::white);
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawEllipse (dot.reduced (1.0f), 1.6f);
        }
        if (i == selected)
        {
            g.setColour (colour);
            g.drawEllipse (dot.expanded (1.5f), 2.0f);
        }
        g.setColour (b.on ? juce::Colour (0xff120d10) : juce::Colours::white);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (i + 1), dot, juce::Justification::centred, false);
    }

    // What's under the mouse.
    juce::String readout;
    juce::Point<float> anchor;
    if (const auto band = dragging >= 0 ? dragging : hovered; band >= 0)
    {
        const auto& b = shown.bands[(size_t) band];
        readout << (band + 1) << "  " << dominion::shapeNames[(int) b.shape] << "  " << frequencyText (b.frequency);
        if (usesDepth (b.shape))
            readout << "  " << (b.depthDb > 0.0 ? "+" : "") << juce::String (b.depthDb, 1) << " dB";
        if (! b.on)
            readout << "  (off)";
        anchor = dotFor (b).translated (0.0f, -24.0f);
    }
    else if (plot.contains (hoverPoint))
    {
        const auto hz = frequencyForX (hoverPoint.x);
        auto most = 0.0f;
        for (int k = 0; k < d.getNumFilters(); ++k)
            if (std::abs (std::log (d.getFrequency (k) / hz)) < std::log (2.0) / 24.0)
                most = std::max ({ most, d.getReductionDb (0, k), d.getReductionDb (1, k) });
        readout << frequencyText (hz) << "  cutting " << juce::String (most, 1) << " dB";
        anchor = { hoverPoint.x, plot.getBottom() - 14.0f };
    }
    if (readout.isNotEmpty())
    {
        const auto font = juce::FontOptions (11.0f, juce::Font::bold);
        auto box = juce::Rectangle<float> ((float) juce::GlyphArrangement::getStringWidthInt (font, readout) + 14.0f, 18.0f).withCentre (anchor);
        box = box.constrainedWithin (plot);
        g.setColour (juce::Colour (0xe0141016));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (ParagonColours::text);
        g.setFont (font);
        g.drawText (readout, box, juce::Justification::centred, false);
    }

    if (std::none_of (shown.bands.begin(), shown.bands.end(), [] (const dominion::Band& b) { return b.used; }))
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Double-click to add a band to the depth curve: raise it where DOMINION should work harder, lower it where it should leave alone.",
                    plot.withTrimmedTop (plot.getHeight() * 0.85f).withHeight (18.0f), juce::Justification::centred, false);
    }
}

void DominionDisplay::mouseMove (const juce::MouseEvent& event)
{
    hoverPoint = event.position;
    hovered = bandAt (event.position);
    repaint();
}

void DominionDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    hoverPoint = {};
    repaint();
}

void DominionDisplay::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto band = bandAt (event.position);
    if (band < 0)
    {
        if (! event.mods.isPopupMenu())
            select (-1);
        return;
    }
    select (band);
    if (event.mods.isPopupMenu())
    {
        showMenu (band);
        return;
    }
    const auto& b = shown.bands[(size_t) band];
    dragging = band;
    dragStartFrequency = b.frequency;
    dragStartDepth = b.depthDb;
    parameter (band, "Freq").beginChangeGesture();
    parameter (band, "Depth").beginChangeGesture();
}

void DominionDisplay::mouseDrag (const juce::MouseEvent& event)
{
    hoverPoint = event.position;
    if (dragging < 0)
        return;
    const auto fine = event.mods.isShiftDown() ? 0.1f : 1.0f;
    const auto dx = (float) event.getDistanceFromDragStartX() * fine, dy = (float) event.getDistanceFromDragStartY() * fine;
    auto& f = parameter (dragging, "Freq");
    f.setValueNotifyingHost (f.convertTo0to1 ((float) frequencyForX (xForFrequency (dragStartFrequency) + dx)));
    if (usesDepth (shown.bands[(size_t) dragging].shape))
    {
        auto& depth = parameter (dragging, "Depth");
        const auto db = dragStartDepth - (double) dy / (plot.getHeight() * 0.36) * rangeDb;
        depth.setValueNotifyingHost (depth.convertTo0to1 ((float) juce::jlimit (-dominion::maxDepthDb, dominion::maxDepthDb, db)));
    }
    shown = processor.getDominionSettings();
    repaint();
}

void DominionDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    parameter (dragging, "Freq").endChangeGesture();
    parameter (dragging, "Depth").endChangeGesture();
    dragging = -1;
}

void DominionDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (const auto band = bandAt (event.position); band >= 0)
    {
        // A dot: on or off (as in soothe).
        setBand (band, "On", shown.bands[(size_t) band].on ? 0.0f : 1.0f);
        shown = processor.getDominionSettings();
        return;
    }
    if (! plot.contains (event.position))
        return;
    const auto hz = frequencyForX (event.position.x);
    const auto shape = hz < 40.0 ? dominion::Shape::lowCut : hz > 12000.0 ? dominion::Shape::highCut : dominion::Shape::bell;
    const auto depthDb = shape == dominion::Shape::bell ? juce::jlimit (-dominion::maxDepthDb, dominion::maxDepthDb, dbForY (event.position.y)) : 0.0;
    if (const auto band = createBand (shape, hz, depthDb); band >= 0)
        select (band);
}

void DominionDisplay::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto band = hovered >= 0 ? hovered : selected;
    if (band < 0 || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    const auto& b = shown.bands[(size_t) band];
    if (isCut (b.shape))
        setBand (band, "Slope", (float) juce::jlimit (0, eq::numSlopes - 1, b.slope + (wheel.deltaY > 0.0f ? 1 : -1)));
    else
        setBand (band, "Q", (float) juce::jlimit (0.1, 10.0, b.q * std::exp ((double) wheel.deltaY * (event.mods.isShiftDown() ? 0.25 : 1.5))));
    shown = processor.getDominionSettings();
    repaint();
}

bool DominionDisplay::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected >= 0)
    {
        removeBand (selected);
        return true;
    }
    return false;
}

void DominionDisplay::showMenu (int band)
{
    const auto& b = shown.bands[(size_t) band];
    juce::PopupMenu menu, shapes;
    for (int i = 0; i < dominion::shapeNames.size(); ++i)
        shapes.addItem (100 + i, dominion::shapeNames[i], true, (int) b.shape == i);
    menu.addSectionHeader ("Band " + juce::String (band + 1));
    menu.addSubMenu ("Shape", shapes);
    menu.addSeparator();
    menu.addItem (1, b.on ? "Switch off" : "Switch on");
    menu.addItem (2, "Delete");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, band] (int chosen)
    {
        if (chosen >= 100)
            setBand (band, "Shape", (float) (chosen - 100));
        else if (chosen == 1)
            setBand (band, "On", shown.bands[(size_t) band].on ? 0.0f : 1.0f);
        else if (chosen == 2)
            removeBand (band);
        shown = processor.getDominionSettings();
    });
}

//==============================================================================
DominionPage::Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : label ({}, text), attachment (state, id, slider)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

namespace
{
    // A menu styled and filled with a choice parameter's choices, ready to attach.
    juce::ComboBox& filled (juce::ComboBox& box, juce::AudioProcessorValueTreeState& state, const char* id)
    {
        styleCombo (box);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            box.addItemList (choice->choices, 1);
        return box;
    }
}

DominionPage::Choice::Choice (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : label ({}, text), attachment (state, id, filled (box, state, id))
{
    styleLabel (label);
    box.setTooltip (tooltip);
}

DominionPage::DominionPage (ParagonX1AudioProcessor& p)
    : processor (p),
      display (p),
      onAttachment (p.getParameters(), ParamIDs::domOn, onButton),
      deltaAttachment (p.getParameters(), ParamIDs::domDelta, delta),
      mode (p.getParameters(), ParamIDs::domMode, "MODE",
            "SOFT: finds what sticks out above the sound around it, at any level (resonances, harshness, build-ups). "
            "HARD: works on what's loud, so it depends on the level: firmer, and a sound-design tool when pushed"),
      stereo (p.getParameters(), ParamIDs::domStereo, "STEREO",
              "L/R works on left and right, M/S on the middle and the sides of the stereo picture"),
      depth (p.getParameters(), ParamIDs::domDepth, "DEPTH", "How much it takes away: more, deeper cuts"),
      detail (p.getParameters(), ParamIDs::domDetail, "DETAIL",
              "How narrow its cuts are: low for broad build-ups (boom, mud), high for narrow resonances (ringing, whistle)"),
      attack (p.getParameters(), ParamIDs::domAttack, "ATTACK", "How quickly it acts: higher lets more of each attack through"),
      release (p.getParameters(), ParamIDs::domRelease, "RELEASE", "How quickly it lets go: higher is steadier, lower more open"),
      link (p.getParameters(), ParamIDs::domLink, "LINK", "100%: both channels listened to together and cut alike. 0%: each on its own"),
      focus (p.getParameters(), ParamIDs::domFocus, "FOCUS", "More on one channel than the other: down for left (or mid), up for right (or side)"),
      detailLow (p.getParameters(), ParamIDs::domDetailLow, "DETAIL", "DETAIL in the lows (below about 500 Hz): up narrower, down broader"),
      detailHigh (p.getParameters(), ParamIDs::domDetailHigh, "DETAIL", "DETAIL in the highs (above about 2 kHz): up narrower, down broader"),
      attackLow (p.getParameters(), ParamIDs::domAttackLow, "ATTACK", "ATTACK in the lows: up slower (more punch through), down faster"),
      attackHigh (p.getParameters(), ParamIDs::domAttackHigh, "ATTACK", "ATTACK in the highs: up slower, down faster"),
      releaseLow (p.getParameters(), ParamIDs::domReleaseLow, "RELEASE", "RELEASE in the lows: up slower, down faster"),
      releaseHigh (p.getParameters(), ParamIDs::domReleaseHigh, "RELEASE", "RELEASE in the highs: up slower, down faster"),
      maxCut (p.getParameters(), ParamIDs::domMaxCut, "MAX CUT", "The most it may cut anywhere (at the top: no limit)"),
      wetTrim (p.getParameters(), ParamIDs::domWetTrim, "WET TRIM", "The level of the processed sound, before MIX"),
      mix (p.getParameters(), ParamIDs::domMix, "MIX", "Processed against the original: 100% all processed"),
      out (p.getParameters(), ParamIDs::domOut, "OUT", "The final level, after MIX"),
      preset (PresetButton::Hooks {
          "DOMINION",
          [] () -> const std::vector<StompPreset>& { return DominionPresets::factory(); },
          [&p] { return p.getDominionPresets().user(); },
          [&p] { return p.getDominionPresetName(); },
          [&p] { return p.isDominionPresetModified(); },
          [&p] (const StompPreset& chosen) { p.loadDominionPreset (chosen); },
          [&p] (const juce::String& name)
          {
              const auto saved = DominionPresets::capture (p.getParameters(), name);
              if (! p.getDominionPresets().save (saved))
                  return false;
              p.rememberDominionPreset (saved);
              return true;
          },
          [&p] (const StompPreset& gone)
          {
              if (! p.getDominionPresets().remove (gone))
                  return false;
              p.rememberDominionPreset (DominionPresets::capture (p.getParameters(), "Untitled"));
              return true;
          },
          [&p] { return p.getDominionPresets().getFolder(); } })
{
    addAndMakeVisible (display);
    display.onSelect = [this] (int selected) { showBand (selected); };

    onButton.setTooltip ("Switch DOMINION on or off (the same as its POWER switch in the rack)");
    delta.setTooltip ("Hear only what DOMINION takes away");
    styleButton (back);
    back.onClick = [this] { if (onClose) onClose(); };
    styleLabel (presetLabel);
    addAndMakeVisible (preset);
    addAndMakeVisible (presetLabel);

    for (auto* component : std::initializer_list<juce::Component*> { &onButton, &delta, &back, &mode.box, &mode.label, &stereo.box, &stereo.label })
        addAndMakeVisible (component);
    for (auto* knob : { &depth, &detail, &attack, &release, &link, &focus, &detailLow, &detailHigh, &attackLow, &attackHigh,
                        &releaseLow, &releaseHigh, &maxCut, &wetTrim, &mix, &out })
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }

    // The selected band.
    styleCombo (shape);
    shape.addItemList (dominion::shapeNames, 1);
    styleCombo (slope);
    slope.addItemList (eq::slopeNames, 1);
    for (auto* knob : { &frequency, &bandDepth, &q, &bandFocus })
        styleKnob (*knob);
    frequency.setDoubleClickReturnValue (true, 1000.0);
    bandDepth.setDoubleClickReturnValue (true, 0.0);
    q.setDoubleClickReturnValue (true, 1.0);
    bandFocus.setDoubleClickReturnValue (true, 0.0);
    for (auto* label : { &shapeLabel, &slopeLabel, &frequencyLabel, &depthLabel, &qLabel, &focusLabel })
        styleLabel (*label);
    for (auto* button : { &listen, &previous, &next, &remove })
        styleButton (*button);
    listen.setTooltip ("Hold to hear only what DOMINION takes away in this band's part of the spectrum");
    listen.onStateChange = [this] { processor.setDominionListen (listen.isDown() && band >= 0 ? band : -1); };
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    remove.onClick = [this] { if (band >= 0) display.removeBand (band); };
    bandFocus.setTooltip ("This band's part of the curve more on one channel: down for left (or mid), up for right (or side)");
    for (auto* component : bandComponents())
        addChildComponent (component);

    showBand (-1);
    startTimerHz (10);
}

DominionPage::~DominionPage()
{
    processor.setDominionListen (-1);
}

std::vector<juce::Component*> DominionPage::bandComponents()
{
    return { &bandOn, &listen, &previous, &next, &remove, &shape, &slope, &frequency, &bandDepth, &q, &bandFocus,
             &shapeLabel, &slopeLabel, &frequencyLabel, &depthLabel, &qLabel, &focusLabel };
}

void DominionPage::showBand (int newBand)
{
    band = newBand;
    bandOnAttachment.reset();
    shapeAttachment.reset();
    slopeAttachment.reset();
    frequencyAttachment.reset();
    depthAttachment.reset();
    qAttachment.reset();
    focusAttachment.reset();
    processor.setDominionListen (-1);
    for (auto* component : bandComponents())
        component->setVisible (band >= 0);
    if (band >= 0)
    {
        auto& state = processor.getParameters();
        const auto parameter = [&] (const char* what) -> juce::RangedAudioParameter& { return *state.getParameter (ParamIDs::domBand (band, what)); };
        bandOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter ("On"), bandOn);
        shapeAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter ("Shape"), shape);
        slopeAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter ("Slope"), slope);
        frequencyAttachment = std::make_unique<juce::SliderParameterAttachment> (parameter ("Freq"), frequency);
        depthAttachment = std::make_unique<juce::SliderParameterAttachment> (parameter ("Depth"), bandDepth);
        qAttachment = std::make_unique<juce::SliderParameterAttachment> (parameter ("Q"), q);
        focusAttachment = std::make_unique<juce::SliderParameterAttachment> (parameter ("Focus"), bandFocus);
        updateControls();
    }
    repaint();
}

void DominionPage::step (int direction)
{
    const auto settings = processor.getDominionSettings();
    std::vector<int> order;
    for (int i = 0; i < dominion::maxBands; ++i)
        if (settings.bands[(size_t) i].used)
            order.push_back (i);
    if (order.empty())
        return;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return settings.bands[(size_t) a].frequency < settings.bands[(size_t) b].frequency; });
    const auto at = std::find (order.begin(), order.end(), band);
    const auto index = at == order.end() ? 0 : (int) std::distance (order.begin(), at) + direction;
    display.select (order[(size_t) ((index + (int) order.size()) % (int) order.size())]);
}

void DominionPage::updateControls()
{
    preset.refresh();   // its name, with * once its settings have been changed

    if (band < 0)
        return;
    const auto b = processor.getDominionSettings().bands[(size_t) band];
    bandDepth.setEnabled (usesDepth (b.shape));
    slope.setEnabled (usesSlope (b.shape));
    q.setEnabled (! isCut (b.shape));
    repaint (controlsArea.withWidth (140));   // the band's dot follows its frequency's colour
}

void DominionPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (92);
    area.removeFromTop (8);
    sidePanel = area.removeFromRight (300);
    area.removeFromRight (8);
    controlsArea = area.removeFromBottom (104);
    area.removeFromBottom (8);
    display.setBounds (area);

    const auto place = [] (Knob& knob, juce::Rectangle<int> column)
    {
        knob.label.setBounds (column.removeFromTop (14));
        knob.slider.setBounds (column);
    };

    // Top bar: the name, ON, MODE, the four main knobs; BACK on the right.
    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (150);   // the name, drawn
    onButton.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    bar.removeFromLeft (14);
    auto modeColumn = bar.removeFromLeft (110);
    mode.label.setBounds (modeColumn.removeFromTop (14));
    mode.box.setBounds (modeColumn.withSizeKeepingCentre (100, 28));
    bar.removeFromLeft (18);
    for (auto* knob : { &depth, &detail, &attack, &release })
    {
        place (*knob, bar.removeFromLeft (92));
        bar.removeFromLeft (10);
    }
    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));
    bar.removeFromRight (24);
    auto presetColumn = bar.removeFromRight (220);
    presetLabel.setBounds (presetColumn.removeFromTop (14));
    preset.setBounds (presetColumn.withSizeKeepingCentre (220, 30));

    // The side panel: STEREO, TILT, SCALE, then DELTA, MIX and OUT.
    auto side = sidePanel.reduced (12, 10);
    // (PR7-K's page is 46 px shorter than PR7-E's: TILT and SCALE give up some of their height so MIX and OUT fit.)
    stereoArea = side.removeFromTop (118);
    tiltArea = side.removeFromTop (200);
    scaleArea = side.removeFromTop (104);
    outputArea = side;

    auto stereoRow = stereoArea.withTrimmedTop (22);
    auto stereoColumn = stereoRow.removeFromLeft (92);
    stereo.label.setBounds (stereoColumn.removeFromTop (14));
    stereo.box.setBounds (stereoColumn.withSizeKeepingCentre (84, 28));
    place (link, stereoRow.removeFromLeft (92));
    place (focus, stereoRow.removeFromLeft (92));

    auto tilts = tiltArea.withTrimmedTop (22);
    const auto rowHeight = tilts.getHeight() / 2;
    auto lows = tilts.removeFromTop (rowHeight), highs = tilts;
    lows.removeFromLeft (30);
    highs.removeFromLeft (30);
    for (const auto& [low, high] : std::initializer_list<std::pair<Knob*, Knob*>> { { &detailLow, &detailHigh }, { &attackLow, &attackHigh },
                                                                                   { &releaseLow, &releaseHigh } })
    {
        place (*low, lows.removeFromLeft (82));
        place (*high, highs.removeFromLeft (82));
    }

    auto scaleRow = scaleArea.withTrimmedTop (22);
    place (maxCut, scaleRow.removeFromLeft (92));
    place (wetTrim, scaleRow.removeFromLeft (92));

    auto outRow = outputArea.withTrimmedTop (22);
    delta.setBounds (outRow.removeFromLeft (92).withSizeKeepingCentre (84, 28));
    place (mix, outRow.removeFromLeft (92));
    place (out, outRow.removeFromLeft (92));

    // The selected band.
    auto controls = controlsArea.reduced (14, 8);
    controls.removeFromLeft (110);   // its name, drawn
    auto arrows = controls.removeFromLeft (64);
    previous.setBounds (arrows.removeFromLeft (30).withSizeKeepingCentre (30, 26));
    next.setBounds (arrows.removeFromRight (30).withSizeKeepingCentre (30, 26));
    controls.removeFromLeft (10);
    bandOn.setBounds (controls.removeFromLeft (56).withSizeKeepingCentre (56, 26));
    controls.removeFromLeft (8);
    const auto labelled = [&] (juce::Label& label, juce::Component& component, int width, bool knob)
    {
        auto column = controls.removeFromLeft (width);
        label.setBounds (column.removeFromTop (14));
        component.setBounds (knob ? column : column.withSizeKeepingCentre (width - 8, 26));
        controls.removeFromLeft (6);
    };
    labelled (shapeLabel, shape, 124, false);
    labelled (frequencyLabel, frequency, 86, true);
    labelled (depthLabel, bandDepth, 86, true);
    labelled (qLabel, q, 86, true);
    labelled (slopeLabel, slope, 112, false);
    labelled (focusLabel, bandFocus, 86, true);
    remove.setBounds (controls.removeFromRight (86).withSizeKeepingCentre (86, 28));
    controls.removeFromRight (8);
    listen.setBounds (controls.removeFromRight (86).withSizeKeepingCentre (86, 28));
}

void DominionPage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, sidePanel, controlsArea })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient, across its letters.
    auto title = topBar.reduced (14, 8).removeFromLeft (150);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "DOMINION");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("DOMINION", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Resonance suppression", title, juce::Justification::topLeft, false);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (const auto& [area, name] : std::initializer_list<std::pair<juce::Rectangle<int>, const char*>> {
             { stereoArea, "STEREO" }, { tiltArea, "TILT" }, { scaleArea, "SCALE" }, { outputArea, "OUTPUT" } })
    {
        g.setColour (ParagonColours::text);
        g.drawText (name, area.withHeight (18), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (area.getY() + 19, (float) area.getX(), (float) area.getRight());
    }
    // The tilt grid's rows and columns.
    auto tilts = tiltArea.withTrimmedTop (22);
    const auto rowHeight = tilts.getHeight() / 2;
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("LO", tilts.withWidth (28).withHeight (rowHeight), juce::Justification::centred, false);
    g.drawText ("HI", tilts.withWidth (28).withTrimmedTop (rowHeight), juce::Justification::centred, false);

    auto controls = controlsArea.reduced (14, 8);
    auto name = controls.removeFromLeft (110);
    if (band < 0)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Click a dot to change its band, or double-click the graph to add one (8 at most).", controlsArea,
                    juce::Justification::centred, false);
        return;
    }
    g.setColour (DominionDisplay::colourAt (processor.getDominionSettings().bands[(size_t) band].frequency));
    g.fillEllipse (name.removeFromLeft (20).toFloat().withSizeKeepingCentre (13.0f, 13.0f));
    name.removeFromLeft (6);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (band + 1), name, juce::Justification::centredLeft, false);
}
