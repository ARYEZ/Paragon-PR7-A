#include "ModFxPages.h"

#include "dsp/effects/LoFi.h"
#include "dsp/effects/Modulation.h"
#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    using Type = effects::Modulation::Type;

    // Two things moving in turn: drawn as the shape and its opposite.
    bool drawsTwo (Type type) { return type == Type::dimension || type == Type::harmonicTremolo || type == Type::autoPan; }

    juce::String hzText (float hz) { return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0) + " kHz"; }
}

//==============================================================================
SweepDisplay::SweepDisplay (std::function<effects::Modulation::Settings()> settings, std::function<float()> phase)
    : settingsNow (std::move (settings)), phaseNow (std::move (phase))
{
    setTooltip ("What the LFO moves, from now (left) over its next two cycles, at DEPTH: the shapes the effect plays. The pedals' "
                "own quirks (the Vortex's uneven clock, the Uni-Vibe's slow lamp, the rotor's inertia) shape the sound beyond this line.");
    startTimerHz (30);
}

juce::String SweepDisplay::movingName (Type type)
{
    switch (type)
    {
        case Type::chorus:
        case Type::carbonite:        return "CHIP CLOCK";
        case Type::flanger:          return "DELAY";
        case Type::phaser:           return "PHASE STAGES";
        case Type::tremolo:          return "VOLUME";
        case Type::dimension:        return "VOICE A / VOICE B";
        case Type::uniVibe:          return "LAMP";
        case Type::rotary:           return "HORN";
        case Type::vibrato:          return "PITCH";
        case Type::harmonicTremolo:  return "HIGHS / LOWS";
        case Type::autoPan:          return "RIGHT / LEFT";
        case Type::doubler:          return "TAKES' TIMING";
    }
    return {};
}

float SweepDisplay::swing (const effects::Modulation::Settings& settings) noexcept
{
    if (settings.type == Type::phaser)
        return 1.0f;   // Hadron has only SPEED: it always sweeps its full range
    if (settings.type == Type::rotary)
        return 0.35f + 0.65f * settings.depth;   // (the rotor's intensity)
    return settings.depth;
}

void SweepDisplay::timerCallback()
{
    if (isShowing())
        repaint();
}

void SweepDisplay::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto settings = settingsNow();
    const auto area = getLocalBounds().toFloat().reduced (28.0f, 22.0f).withTrimmedBottom (14.0f);
    const auto rate = juce::jmax (0.01f, settings.rateHz);
    const auto seconds = 2.0f / rate;

    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (int k = 0; k <= 4; ++k)
    {
        const auto x = area.getX() + area.getWidth() * (float) k / 4.0f;
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim);
        const auto t = seconds * (float) k / 4.0f;
        g.drawText (k == 0 ? juce::String ("NOW") : juce::String (t, t < 1.0f ? 2 : 1) + " s",
                    juce::Rectangle<float> (x - 30.0f, area.getBottom() + 2.0f, 60.0f, 14.0f), juce::Justification::centred, false);
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

    const auto swing = SweepDisplay::swing (settings);
    const auto phase = phaseNow();
    // Razor's DEPTH is how far down the volume dips: full volume stays at the top.
    const auto valueAt = [&] (float phase, float sign)
    {
        const auto shape = sign * effects::Modulation::sweepShape (settings.type, phase, settings);
        return settings.type == Type::tremolo ? 1.0f - settings.depth + settings.depth * shape : swing * shape;
    };
    const auto path = [&] (float sign)
    {
        juce::Path line;
        constexpr int points = 400;
        for (int i = 0; i <= points; ++i)
        {
            const auto t = (float) i / (float) points;   // 0 to 1 across: two cycles
            const auto x = area.getX() + area.getWidth() * t;
            const auto y = area.getCentreY() - area.getHeight() * 0.46f * valueAt (phase + 2.0f * t, sign);
            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }
        return line;
    };
    if (drawsTwo (settings.type))
    {
        g.setColour (ParagonColours::textDim);
        g.strokePath (path (-1.0f), juce::PathStrokeType (1.6f));
    }
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
    g.strokePath (path (1.0f), juce::PathStrokeType (2.4f));
    const auto nowY = area.getCentreY() - area.getHeight() * 0.46f * valueAt (phase, 1.0f);
    g.setColour (settings.on ? ParagonColours::accent : ParagonColours::textDim);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ area.getX(), nowY }));

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);
    g.drawText (settings.on ? movingName (settings.type) : movingName (settings.type) + " (OFF)", area.reduced (10.0f, 4.0f),
                juce::Justification::topLeft, false);
    g.drawText (juce::String (settings.rateHz, settings.rateHz < 1.0f ? 2 : 1) + " Hz", area.reduced (10.0f, 4.0f), juce::Justification::topRight, false);
}

//==============================================================================
LoFiDisplay::LoFiDisplay (ParagonX1AudioProcessor& p, std::function<effects::LoFi::Settings()> settings)
    : processor (p), settingsNow (std::move (settings))
{
    setTooltip ("What LO-FI lets through at this AMOUNT: its filters' response, 20 Hz to 20 kHz (the saturation, hiss and wobble aren't "
                "drawn). For the bitcrusher, a sine as it comes out: the levels its bits leave, each held for its samples.");
    startTimerHz (10);
}

void LoFiDisplay::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto settings = settingsNow();
    if ((int) settings.type != shownType || settings.amount != shownAmount)
    {
        shownType = (int) settings.type;
        shownAmount = settings.amount;
        repaint();
    }
}

void LoFiDisplay::paint (juce::Graphics& g)
{
    using LoFiType = effects::LoFi::Type;
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto settings = settingsNow();
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto area = getLocalBounds().toFloat().reduced (28.0f, 22.0f).withTrimmedBottom (14.0f).withTrimmedLeft (18.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);

    if (settings.type == LoFiType::bitcrusher)
    {
        // Two cycles of a sine as the bitcrusher plays it: rounded to its levels (2^(bits - 1) each side), each held.
        const auto bits = effects::LoFi::crusherBits (settings.amount);
        const auto steps = std::exp2 (bits - 1.0f);
        const auto hold = effects::LoFi::crusherHold (settings.amount, sampleRate);
        const auto cycle = juce::jlimit (16, 2000, 24 * hold);   // samples per cycle drawn: wide enough to see the holding
        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        juce::Path smooth, crushed;
        auto held = 0.0f;
        for (int i = 0; i <= 2 * cycle; ++i)
        {
            const auto x = area.getX() + area.getWidth() * (float) i / (float) (2 * cycle);
            const auto value = 0.9f * std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) cycle);
            if (i % hold == 0)
                held = std::round (value * steps) / steps;
            const auto ySmooth = area.getCentreY() - area.getHeight() * 0.48f * value;
            const auto yHeld = area.getCentreY() - area.getHeight() * 0.48f * held;
            if (i == 0)
            {
                smooth.startNewSubPath (x, ySmooth);
                crushed.startNewSubPath (x, yHeld);
            }
            else
            {
                smooth.lineTo (x, ySmooth);
                crushed.lineTo (x, crushed.getCurrentPosition().y);   // a step: across, then up or down
                crushed.lineTo (x, yHeld);
            }
        }
        g.setColour (ParagonColours::textDim);
        g.strokePath (smooth, juce::PathStrokeType (1.2f));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.strokePath (crushed, juce::PathStrokeType (2.2f));
        g.setColour (ParagonColours::accent);
        g.drawText (juce::String (bits, 1) + " BITS, EACH SAMPLE HELD x" + juce::String (hold) + " ("
                        + hzText ((float) sampleRate / (float) hold) + ")",
                    area.reduced (10.0f, 4.0f), juce::Justification::topRight, false);
        return;
    }

    // The filters' response: two 12 dB/octave stages each way (JUCE's state-variable filters at their default Q), and a
    // speaker's resonance (a band-pass added at its gain, Q 1 to 2.5 by type: drawn at Q 1.5).
    const auto band = effects::LoFi::bandFor (settings.type, settings.amount, sampleRate);
    constexpr float q = 0.70710678f;
    const auto magnitude = [&] (float hz)
    {
        const auto hp = [&] (float fc) { const auto r = hz / fc; return r * r / std::sqrt ((1.0f - r * r) * (1.0f - r * r) + r * r / (q * q)); };
        const auto lp = [&] (float fc) { const auto r = hz / fc; return 1.0f / std::sqrt ((1.0f - r * r) * (1.0f - r * r) + r * r / (q * q)); };
        auto m = hp (band.lowHz) * hp (band.lowHz) * lp (band.highHz) * lp (band.highHz);
        if (band.bumpGain > 0.0f)
        {
            const auto r = hz / band.bumpHz;
            const auto bandPass = (r / 1.5f) / std::sqrt ((1.0f - r * r) * (1.0f - r * r) + r * r / (1.5f * 1.5f));
            m *= 1.0f + band.bumpGain * bandPass;
        }
        return m;
    };
    const auto xAt = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    constexpr float topDb = 12.0f, bottomDb = -48.0f;
    const auto yAt = [&] (float db) { return area.getY() + area.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (const auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xAt (hz)), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim);
        g.drawText (hz < 1000.0f ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0f)) + "k",
                    juce::Rectangle<float> (xAt (hz) - 20.0f, area.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    for (const auto db : { 0.0f, -12.0f, -24.0f, -36.0f })
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (db == 0.0f ? 1.0f : 0.45f));
        g.drawHorizontalLine (juce::roundToInt (yAt (db)), area.getX(), area.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (area.getX() - 40.0f, yAt (db) - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);
    }
    juce::Path curve, fill;
    for (int i = 0; i <= 300; ++i)
    {
        const auto hz = 20.0f * std::pow (1000.0f, (float) i / 300.0f);
        const auto point = juce::Point<float> (xAt (hz), yAt (juce::Decibels::gainToDecibels (magnitude (hz), -100.0f)));
        if (i == 0)
        {
            curve.startNewSubPath (point);
            fill.startNewSubPath (point.x, area.getBottom());
        }
        else
            curve.lineTo (point);
        fill.lineTo (point);
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
    g.setOpacity (0.35f);
    g.fillPath (fill);
    g.setOpacity (1.0f);
    g.strokePath (curve, juce::PathStrokeType (2.2f));
    g.setColour (ParagonColours::accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (settings.type == LoFiType::cassette ? "TAPE: TOP END FROM " + hzText (band.highHz)
                                                    : hzText (band.lowHz) + " TO " + hzText (band.highHz) + ", BUMP AT " + hzText (band.bumpHz),
                area.reduced (10.0f, 4.0f), juce::Justification::topRight, false);
}

//==============================================================================
ModFxPage::ModFxPage (ParagonX1AudioProcessor& p, Kind k, int b) : processor (p), kind (k), bus (b)
{
    auto& state = p.getParameters();
    const auto make = [&state] (auto& list, const juce::String& id, const juce::String& label, const juce::String& tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, id, label, tip));
    };
    // The whole sound's IDs, or the bus's ("lofiOn" -> "ins1LofiOn").
    const auto lofi = [b] (const char* what) { return b < 0 ? juce::String ("lofi") + what : ParamIDs::ins (b, (juce::String ("Lofi") + what).toRawUTF8()); };
    if (kind == Kind::loFi)
    {
        display = std::make_unique<LoFiDisplay> (p, [&p, b] { return b < 0 ? p.getLoFiSettings() : p.getInsertLoFiSettings (b); });
        make (top, lofi ("On"), "ON", "LO-FI on or off.");
        make (top, lofi ("Type"), "TYPE", "TELEPHONE a phone line, only the voice band; AM RADIO a small radio speaker, nasal; MEGAPHONE narrow, "
                                     "honky and driven hard; CASSETTE wow, flutter, a dull top and hiss; BITCRUSHER fewer bits and a lower "
                                     "sample rate. Each side through its own (PR7-G's speakers are mono).");
        make (controls, lofi ("Amount"), "AMOUNT", "How strong: narrower, dirtier, wobblier, crunchier.");
        make (controls, lofi ("Noise"), "NOISE", "The line hiss, tape hiss or static that sells it.");
        make (controls, lofi ("Mix"), "MIX", "How much of the lo-fi sound against the clean one.");
        make (controls, lofi ("Level"), "LEVEL", "The level after it.");
    }
    else
    {
        modIds = b >= 0 ? ParamIDs::insModIds (b) : kind == Kind::mod ? ParamIDs::modIds : ParamIDs::mod2Ids;
        const auto& ids = modIds;
        const auto which = kind == Kind::mod ? 0 : 1;
        display = std::make_unique<SweepDisplay> ([&p, which, b] { return b >= 0 ? p.getInsertModSettings (b) : p.getModSettings (which); },
                                                  [&p, which, b] { return b >= 0 ? p.getInsertModSweepPhase (b) : p.getModSweepPhase (which); });
        make (top, ids.on, "ON", "On or off.");
        make (top, ids.type, "TYPE", "VORTEX a late-'80s stereo chorus pedal; AEON a BF-2 flanger; HADRON a Phase 90 (SCRIPT or BLOCK); RAZOR a "
                                     "TR-2 tremolo with its WAVE; PRISM a Dimension-style dual chorus; NEBULA a Uni-Vibe; ROTARY a Leslie; "
                                     "VIBRATO pitch alone; HARMONIC TREMOLO lows and highs in turn; AUTO-PAN side to side; DOUBLER two extra "
                                     "takes; CARBONITE a CE-2 chorus, darker. Modelled on those, not copies.");
        make (controls, ids.sync, "SYNC", "The LFO in notes at the song's tempo (OFF: at RATE).");
        make (controls, ids.rate, "RATE", "How fast the LFO sweeps (Hadron: its SPEED; Rotary: the horn's speed).");
        make (controls, ids.depth, "DEPTH", "How far it sweeps.");
        make (controls, ids.mix, "MIX", "The effect against the sound (the Vortex: its E.LEVEL, the chorus added on top).");
        make (controls, ids.manual, "MANUAL", "AEON's MANUAL: where the sweep sits (13 ms to 1 ms; at full DEPTH the LFO alone sweeps).");
        make (controls, ids.res, "RES", "AEON's RES: how much rings back, for the metallic peaks.");
        make (controls, ids.hadron, "CIRCUIT", "Hadron's SCRIPT (the original) or BLOCK (with its feedback: a stronger sweep).");
        make (controls, ids.wave, "WAVE", "Razor's WAVE: a triangle (0) to nearly a square (10), a smooth pulse to a chop.");
        make (controls, ids.eq, "EQ", "The Vortex's EQ on its chorus alone: darker or brighter.");
        make (controls, ids.stereo, "STEREO", "The Vortex's STEREO: the chorus between the speakers (added on the left, taken away on the "
                                              "right; in mono it folds back to the dry sound). Off: the chorus on both sides.");
    }
    for (auto* list : { &top, &controls })
        for (auto& c : *list)
            addAndMakeVisible (*c);
    addAndMakeVisible (*display);
    back.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (back);
    timerCallback();
    startTimerHz (10);
}

bool ModFxPage::shows (const ParamControl& control) const
{
    if (kind == Kind::loFi)
        return true;
    // As PR7-G's MOD panel: RATE while not synced, each type's own controls, DEPTH and MIX where they do something.
    const auto& ids = modIds;
    const auto settings = bus >= 0 ? processor.getInsertModSettings (bus) : processor.getModSettings (kind == Kind::mod ? 0 : 1);
    const auto& id = control.getParameterId();
    const auto type = settings.type;
    if (id == ids.rate)    return processor.getParameters().getRawParameterValue (ids.sync)->load() < 0.5f;
    if (id == ids.depth)   return type != Type::phaser;
    if (id == ids.mix)     return effects::Modulation::usesMix (type);
    if (id == ids.manual || id == ids.res) return type == Type::flanger;
    if (id == ids.hadron)  return type == Type::phaser;
    if (id == ids.wave)    return type == Type::tremolo;
    if (id == ids.eq || id == ids.stereo) return type == Type::chorus;
    return true;
}

void ModFxPage::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    juce::String key;
    for (auto& c : controls)
        key << (shows (*c) ? "1" : "0");
    if (key != layoutKey)
    {
        layoutKey = key;
        resized();
    }
}

std::vector<ParamControl*> ModFxPage::getControls()
{
    std::vector<ParamControl*> result;
    for (auto* list : { &top, &controls })
        for (auto& c : *list)
            result.push_back (c.get());
    return result;
}

std::vector<ParamControl*> ModFxPage::getShownControls()
{
    std::vector<ParamControl*> result;
    for (auto* c : getControls())
        if (c->isVisible())
            result.push_back (c);
    return result;
}

void ModFxPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (juce::Rectangle<float> (24.0f, 18.0f, 120.0f, 24.0f)));
    g.drawText (kind == Kind::mod ? "MOD" : kind == Kind::mod2 ? "MOD 2" : "LO-FI", juce::Rectangle<int> (24, 14, 160, 26),
                juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    const auto busName = bus >= 0 ? ParamIDs::insertBusNames[bus + 1] : juce::String();
    g.drawText (bus >= 0 ? (kind == Kind::loFi ? "Bus " + busName + ": after MOD" : "Bus " + busName + ": after GLITCH")
                         : kind == Kind::loFi ? "Insert 3: after MOD 2" : kind == Kind::mod ? "Insert 1: after the sends" : "Insert 2: after MOD",
                juce::Rectangle<int> (24, 38, 200, 16), juce::Justification::centredLeft, false);
}

void ModFxPage::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto bar = area.removeFromTop (54);
    bar.removeFromLeft (190);
    back.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 30));
    bar.removeFromRight (20);
    top[0]->setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (10);
    top[1]->setBounds (bar.removeFromLeft (200));
    area.removeFromTop (8);

    auto controlRow = area.removeFromBottom (ParamControl::knobHeight);
    area.removeFromBottom (10);
    display->setBounds (area);
    std::vector<juce::Component*> row;
    for (auto& c : controls)
    {
        c->setVisible (shows (*c));
        if (c->isVisible())
            row.push_back (c.get());
    }
    SynthPanel::layoutRow (controlRow, row, 110);
}
