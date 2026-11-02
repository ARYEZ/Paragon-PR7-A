#include "LevelMeter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ParagonLookAndFeel.h"

namespace
{
    constexpr int refreshHz = 30;
    constexpr float fallDbPerSecond = 24.0f;   // how fast the meter drops after a peak
    constexpr float scaleMarksDb[] = { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f };
}

LevelMeter::LevelMeter (LevelMeterSource& sourceToUse) : source (sourceToUse)
{
    startTimerHz (refreshHz);
}

float LevelMeter::dbToProportion (float db) const noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

void LevelMeter::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto peakDb = juce::Decibels::gainToDecibels (source.takePeak(), minDb);
    const auto fallenDb = displayedDb - fallDbPerSecond / (float) refreshHz;
    const auto newDb = juce::jmax (peakDb, fallenDb, minDb);

    if (! juce::approximatelyEqual (newDb, displayedDb))
    {
        displayedDb = newDb;
        // 0.15.1: only the column of dots (with their glow) changes, not the dB scale beside it.
        repaint (getLocalBounds().reduced (0, 6).removeFromRight (6).expanded (3, 2));
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    // As in the mockup: the dB scale on the left and a slim column of small
    // red LED dots beside it, unlit dots just visible.
    auto bounds = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
    const auto column = bounds.removeFromRight (6.0f);
    bounds.removeFromRight (8.0f);
    const auto scaleArea = bounds;

    g.setFont (juce::FontOptions (10.5f));
    for (auto markDb : scaleMarksDb)
    {
        const auto y = column.getBottom() - dbToProportion (markDb) * column.getHeight();
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) markDb), scaleArea.withHeight (12.0f).withCentre ({ scaleArea.getCentreX(), y }),
                    juce::Justification::centredRight, false);
    }

    const auto pitch = 6.0f;   // one dot every 6 px
    const auto numDots = (int) (column.getHeight() / pitch);
    const auto litProportion = dbToProportion (displayedDb);
    const auto zeroProportion = dbToProportion (0.0f);

    for (int i = 0; i < numDots; ++i)
    {
        const auto proportion = (float) (i + 1) / (float) numDots;
        const auto y = column.getBottom() - (float) (i + 1) * pitch;
        const auto dot = juce::Rectangle<float> (column.getX(), y + 1.5f, column.getWidth(), pitch - 2.5f);
        const auto colour = proportion > zeroProportion ? juce::Colour (0xffffa53b) : ParagonColours::meterRed;   // over 0 dB: amber warning
        const auto lit = proportion <= litProportion + 1.0e-4f;

        if (lit)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillRoundedRectangle (dot.expanded (2.0f, 1.0f), 2.0f);
        }
        g.setColour (lit ? colour : colour.withAlpha (0.2f));
        g.fillRoundedRectangle (dot, 1.0f);
    }
}
