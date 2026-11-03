#include "ProgressBox.h"

#include "ui/Logo.h"
#include "ui/ParagonLookAndFeel.h"

ProgressBox::ProgressBox()
{
    setSize (460, 150);
    setInterceptsMouseClicks (true, true);   // over the window: nothing under it is clicked meanwhile
    cancelKey.setWantsKeyboardFocus (false);
    cancelKey.onClick = [this]
    {
        cancelKey.setEnabled (false);
        cancelKey.setButtonText ("CANCELLING");
        if (job.cancel)
            job.cancel();
    };
    addChildComponent (cancelKey);
}

void ProgressBox::show (Job newJob)
{
    job = std::move (newJob);
    progress = 0.0;
    cancelKey.setVisible (job.cancel != nullptr);
    cancelKey.setEnabled (true);
    cancelKey.setButtonText ("CANCEL");
    setVisible (true);
    toFront (false);
    startTimerHz (30);
    update();
}

void ProgressBox::update()
{
    if (job.progress == nullptr)
        return;
    const auto now = job.progress();
    if (now < 0.0)
    {
        // Done (or stopped): gone.
        stopTimer();
        job = {};
        setVisible (false);
        if (onFinished)
            onFinished();
        return;
    }
    if (std::abs (now - progress) > 1.0e-4)
    {
        progress = juce::jlimit (0.0, 1.0, now);
        repaint();
    }
}

void ProgressBox::resized()
{
    cancelKey.setBounds (getWidth() - 22 - 110, getHeight() - 18 - 26, 110, 26);
}

void ProgressBox::paintBar (juce::Graphics& g, juce::Rectangle<float> area, double value)
{
    // PR7-S's horizontal meter: the glass, then segments every 4 px, lit orange to pink-red up to the value, the rest
    // just showing; a little glow round the lit ones.
    const auto lit = (float) juce::jlimit (0.0, 1.0, value);
    g.setColour (juce::Colour (0xff050405));
    g.fillRect (area);
    const auto count = juce::jmax (1, (int) (area.getWidth() / 4.0f));
    const auto step = area.getWidth() / (float) count;
    for (int i = 0; i < count; ++i)
    {
        const auto proportion = ((float) i + 0.5f) / (float) count;
        const auto colour = juce::Colour (0xffff7a1a).interpolatedWith (juce::Colour (0xffff1f5a), proportion);
        const auto on = proportion <= lit;
        const auto segment = juce::Rectangle<float> (area.getX() + (float) i * step + 0.6f, area.getY(), step - 1.2f, area.getHeight());
        if (on)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillRect (segment.expanded (0.6f, 1.0f));
        }
        g.setColour (on ? colour : colour.withAlpha (0.1f));
        g.fillRect (segment);
    }
}

void ProgressBox::paint (juce::Graphics& g)
{
    // PR7-S's question boxes' look: black pearl, a red glow along the edge.
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff0c0a0d));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::glow.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), 7.0f, 3.0f);
    g.setColour (ParagonColours::glow);
    g.drawRoundedRectangle (bounds, 8.0f, 1.2f);

    auto area = bounds.reduced (22.0f, 18.0f);
    auto top = area.removeFromTop (34.0f);
    Logo::drawMark (g, top.removeFromLeft (34.0f).reduced (2.0f), juce::Justification::centredLeft,
                    { juce::Colours::white, ParagonColours::glow, {}, {}, 1.0f, true });
    top.removeFromLeft (12.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (job.what, top, juce::Justification::centredLeft, true);

    area.removeFromTop (14.0f);
    auto barRow = area.removeFromTop (26.0f);
    auto percent = barRow.removeFromRight (64.0f);
    barRow.removeFromRight (10.0f);
    paintBar (g, barRow, progress);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (progress * 100.0)) + "%", percent, juce::Justification::centredRight, false);
    area.removeFromTop (10.0f);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    if (cancelKey.isVisible())
        area.removeFromRight (130.0f);
    g.drawText ("The plugin plays on meanwhile.", area, juce::Justification::centredLeft, true);
}
