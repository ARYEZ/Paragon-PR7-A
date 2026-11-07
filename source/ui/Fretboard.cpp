#include "Fretboard.h"

#include "ui/ParagonLookAndFeel.h"

namespace
{
    // The fret wires as on a real neck (each 2^(1/12) shorter than the last), the 24th at the neck's right end.
    float neckFraction (int fret) { return (1.0f - std::pow (2.0f, -(float) fret / 12.0f)) / 0.75f; }
    constexpr int labelWidth = 64, openWidth = 34;
}

Fretboard::Fretboard (ParagonX1AudioProcessor& p) : processor (p)
{
    frets.fill (-1);
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

juce::Colour Fretboard::stringColour (int string)
{
    // PR7-D's Tron glow by position: pink (ff2d6e), red (ff2a2e), orange (ff6a24).
    const auto t = (float) string / (float) (guitar::numStrings - 1);
    const juce::Colour pink { 0xffff2d6e }, red { 0xffff2a2e }, orange { 0xffff6a24 };
    return t < 0.5f ? pink.interpolatedWith (red, t * 2.0f) : red.interpolatedWith (orange, (t - 0.5f) * 2.0f);
}

juce::Rectangle<float> Fretboard::neck() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 10.0f).withTrimmedLeft ((float) (labelWidth + openWidth)).withTrimmedRight (6.0f);
}

float Fretboard::fretX (int fret) const
{
    const auto n = neck();
    return n.getX() + n.getWidth() * neckFraction (fret);
}

float Fretboard::stringY (int string) const
{
    const auto n = neck();
    const auto row = (guitar::numStrings - 1 - string);   // high E on top
    return n.getY() + n.getHeight() * ((float) row + 0.5f) / (float) guitar::numStrings;
}

juce::Point<float> Fretboard::noteCentre (int string, int fret) const
{
    const auto x = fret == 0 ? neck().getX() - openWidth * 0.5f : 0.5f * (fretX (fret - 1) + fretX (fret));
    return { x, stringY (string) };
}

void Fretboard::refresh()
{
    const auto& g = processor.getGuitar();
    auto changed = false;
    for (int s = 0; s < guitar::numStrings; ++s)
    {
        const auto sounding = g.getSounding (s);
        if (sounding.fret != frets[(size_t) s] || std::abs (sounding.level - levels[(size_t) s]) > 0.02f || sounding.muted != muted[(size_t) s])
        {
            frets[(size_t) s] = sounding.fret;
            levels[(size_t) s] = sounding.level;
            muted[(size_t) s] = sounding.muted;
            changed = true;
        }
    }
    const auto settings = processor.getGuitarSettings();
    const auto h = g.getHand(), f = g.getForcedString (settings);
    if (h != hand || f != forced)
    {
        hand = h;
        forced = f;
        changed = true;
    }
    if (changed)
        repaint();
}

void Fretboard::paint (juce::Graphics& g)
{
    const auto n = neck();
    // The neck: near black, a little lighter than the panel; the hand's reach shaded.
    g.setColour (juce::Colour (0xff120d10));
    g.fillRoundedRectangle (n, 3.0f);
    const auto span = guitar::handSpan (processor.getGuitarSettings().fretMode);
    const auto reach = juce::Rectangle<float>::leftTopRightBottom (fretX (hand - 1), n.getY(), fretX (juce::jmin (guitar::numFrets, hand + span - 1)), n.getBottom());
    g.setColour (ParagonColours::accent.withAlpha (0.07f));
    g.fillRect (reach);
    g.setColour (ParagonColours::accent.withAlpha (0.35f));
    g.drawRect (reach, 1.0f);

    // Inlays, and the fret numbers under them.
    g.setFont (juce::FontOptions (11.0f));
    for (int f = 1; f <= guitar::numFrets; ++f)
    {
        const auto x = 0.5f * (fretX (f - 1) + fretX (f));
        if (f % 12 == 0)
        {
            g.setColour (ParagonColours::panelEdge.brighter (0.4f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, n.getY() + n.getHeight() * 0.3f }));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, n.getY() + n.getHeight() * 0.7f }));
        }
        else if (f % 12 == 3 || f % 12 == 5 || f % 12 == 7 || f % 12 == 9)
        {
            g.setColour (ParagonColours::panelEdge.brighter (0.4f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, n.getCentreY() }));
        }
    }
    // Frets, the nut.
    for (int f = 1; f <= guitar::numFrets; ++f)
    {
        g.setColour (ParagonColours::metalDark.withAlpha (0.45f));
        g.drawVerticalLine (juce::roundToInt (fretX (f)), n.getY(), n.getBottom());
    }
    g.setColour (ParagonColours::metalMid);
    g.fillRect (juce::Rectangle<float> (n.getX() - 3.0f, n.getY(), 4.0f, n.getHeight()));

    // Strings (lower strings thicker), each named on the left; a forced string lit.
    for (int s = 0; s < guitar::numStrings; ++s)
    {
        const auto y = stringY (s);
        const auto colour = stringColour (s);
        const auto thickness = 2.6f - 0.25f * (float) s;
        const auto isForced = s == forced;
        g.setColour ((isForced ? colour : ParagonColours::metalDark).withAlpha (isForced ? 0.95f : 0.55f));
        g.fillRect (juce::Rectangle<float> (n.getX() - openWidth, y - thickness * 0.5f, n.getWidth() + openWidth, thickness));
        g.setColour (isForced ? colour : ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (guitar::playerNumber (s)) + " " + guitar::stringName (s),
                    juce::Rectangle<float> (8.0f, y - 9.0f, (float) labelWidth - 8.0f, 18.0f), juce::Justification::centredLeft);
    }

    // The notes sounding: a dot on each, in its string's colour, brighter the louder, with a soft glow.
    for (int s = 0; s < guitar::numStrings; ++s)
        if (const auto f = frets[(size_t) s]; f >= 0 && f <= guitar::numFrets)
        {
            const auto centre = noteCentre (s, f);
            const auto colour = stringColour (s);
            const auto level = juce::jlimit (0.35f, 1.0f, 0.35f + levels[(size_t) s]);
            const auto r = juce::jmin (9.0f, neck().getHeight() / (float) guitar::numStrings * 0.42f);
            g.setColour (colour.withAlpha (0.25f * level));
            g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (centre));
            g.setColour (colour.withMultipliedBrightness (0.6f + 0.4f * level));
            if (muted[(size_t) s])
            {
                // A palm mute: a ring (the string damped), the fret number in the string's colour.
                g.setColour (ParagonColours::background);
                g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
                g.setColour (colour.withMultipliedBrightness (0.6f + 0.4f * level));
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre).reduced (1.0f), 2.0f);
            }
            else
            {
                g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
                g.setColour (ParagonColours::background);
            }
            g.setFont (juce::FontOptions (r * 1.15f, juce::Font::bold));
            g.drawText (juce::String (f), juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), juce::Justification::centred);
        }
}
