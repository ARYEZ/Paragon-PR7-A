#include "GuitarRack.h"

#include "ui/Finish.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"

namespace
{
    // As PR7-G's rack units (Faceplate.cpp): the chassis, the room round it for its light, the screens' size.
    const PedalBody::Shape unitShape { 16.0f, 10.0f, 5.0f, 0.3f };
    constexpr float glowRoom = 7.0f;
    constexpr float screenSize = 0.45f;
    constexpr float sectionGap = 28.0f;    // between the sections, a printed red line in its middle
    constexpr float displayWidth = 250.0f, sideWidth = 196.0f, headerHeight = 30.0f;
    const auto orange = LedDisplay::ledOrange;

    juce::FontOptions font (float height, int style = juce::Font::bold) { return juce::FontOptions (height, style); }

    void stamped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                  juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (font (height));
        ParagonLookAndFeel::drawStampedText (g, text, area, justification, Finish::ink());
    }
}

GuitarRack::GuitarRack()
{
    setOpaque (false);
}

void GuitarRack::resized()
{
    satin = Finish::makeSatinGrain (juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), 1234);
}

void GuitarRack::setScreen (const Screen& s)
{
    if (s == screen)
        return;
    screen = s;
    repaint (screenArea());
}

void GuitarRack::setOn (bool shouldBeOn)
{
    if (shouldBeOn == on)
        return;
    on = shouldBeOn;
    repaint();
}

void GuitarRack::setMeterDb (float db)
{
    if (std::abs (db - meterDb) < 0.5f)
        return;
    meterDb = db;
    repaint (meterArea().expanded (4));
}

//==============================================================================
juce::Rectangle<float> GuitarRack::face() const
{
    return getLocalBounds().toFloat().reduced (glowRoom).withTrimmedBottom (unitShape.frontEdge).reduced (9.0f);
}

juce::Rectangle<float> GuitarRack::content() const { return face().reduced (12.0f, 8.0f); }

juce::Rectangle<float> GuitarRack::knobsArea() const
{
    auto area = content().withTrimmedTop (headerHeight + 6.0f);
    area.removeFromLeft (displayWidth + sectionGap);
    area.removeFromRight (sideWidth + sectionGap);
    return area;
}

juce::Rectangle<float> GuitarRack::sideArea() const
{
    return content().withTrimmedTop (headerHeight + 6.0f).removeFromRight (sideWidth);
}

juce::Rectangle<int> GuitarRack::screenArea() const
{
    return content().withTrimmedTop (headerHeight + 6.0f).withWidth (displayWidth).withHeight (136.0f).toNearestInt();
}

juce::Rectangle<float> GuitarRack::keysArea() const
{
    auto area = content().withTrimmedTop (headerHeight + 6.0f).withWidth (displayWidth);
    area.removeFromTop (136.0f + 14.0f);
    return area;
}

juce::Rectangle<int> GuitarRack::keyCell (int index) const
{
    auto area = keysArea().withTrimmedTop (14.0f);   // KEY MAP printed over the first two
    const auto width = area.getWidth() / 3.0f;
    return area.withX (area.getX() + width * (float) index).withWidth (width).withTrimmedBottom (12.0f)   // its name, stamped under it
               .withSizeKeepingCentre (54.0f, 28.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::knobCell (int row, int column) const
{
    const auto area = knobsArea();
    const auto width = area.getWidth() / (float) knobColumns, height = area.getHeight() / (float) knobRows;
    return juce::Rectangle<float> (area.getX() + width * (float) column, area.getY() + height * (float) row, width, height).reduced (2.0f, 5.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::meterArea() const
{
    return sideArea().withWidth (58.0f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::sideKnobCell (int index) const
{
    auto area = sideArea().withTrimmedLeft (58.0f + 10.0f);
    const auto height = area.getHeight() / 2.0f;
    return area.withY (area.getY() + height * (float) index).withHeight (height).reduced (2.0f, 2.0f).toNearestInt();
}

//==============================================================================
void GuitarRack::paint (juce::Graphics& g)
{
    // The unit, and everything on it (its knobs and keys too, till paintOverChildren), in pearl black.
    Finish::paintAs (Finish::Type::pearlBlack);

    const auto chassis = getLocalBounds().toFloat().reduced (glowRoom);
    PedalBody::paintUnit (g, chassis, face(), [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);

    // The name top left (as PR7-G's rack units: the block's name, its kind printed in red), the status bar top right.
    auto header = content().removeFromTop (headerHeight);
    {
        auto name = header.withWidth (displayWidth);
        g.setFont (font (19.0f, juce::Font::bold | juce::Font::italic));
        ParagonLookAndFeel::drawStampedText (g, "PERFORMANCE", name.removeFromTop (20.0f), juce::Justification::bottomLeft, Finish::ink());
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        g.drawText (juce::String::fromUTF8 ("PR7-I SERIES  \xc2\xb7  PLAYING AND TONE"), name, juce::Justification::centredLeft, false);
    }
    ParagonLookAndFeel::drawNeonBar (g, header.removeFromRight (34.0f).withSizeKeepingCentre (30.0f, 6.0f).translated (0.0f, -6.0f), on);

    // The printed lines between the sections.
    const auto printedLine = [&] (float x)
    {
        g.setColour (Finish::printedRed().withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, face().getY() + 14.0f, 1.5f, face().getHeight() - 28.0f));
    };
    printedLine (knobsArea().getX() - sectionGap * 0.5f);
    printedLine (knobsArea().getRight() + sectionGap * 0.5f);

    // The screen: what's latched, and the last note.
    {
        const auto glass = LedDisplay::paintScreen (g, screenArea().toFloat(), screenSize);
        auto text = glass.reduced (12.0f, 9.0f);
        auto captionRow = text.removeFromTop (11.0f);
        g.setColour (orange.withAlpha (0.6f));
        g.setFont (font (8.5f));
        g.drawText (screen.caption, captionRow, juce::Justification::centredLeft, false);
        text.removeFromTop (6.0f);
        auto big = text.removeFromTop (30.0f);
        g.setColour (orange);
        g.setFont (font (13.0f));
        g.drawText (screen.key, big.removeFromRight (40.0f), juce::Justification::centredRight, false);
        g.setFont (font (23.0f));
        g.drawFittedText (screen.articulation, big.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        text.removeFromTop (6.0f);
        g.setColour (orange.withAlpha (0.25f));
        g.fillRect (text.removeFromTop (1.0f));
        text.removeFromTop (8.0f);
        g.setColour (orange);
        g.setFont (font (13.0f));
        g.drawText (screen.line1, text.removeFromTop (18.0f), juce::Justification::centredLeft, false);
        g.setColour (orange.withAlpha (0.65f));
        g.setFont (font (9.5f));
        g.drawFittedText (screen.line2, text.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        LedDisplay::paintGlassOverlay (g, glass, screenSize);
    }

    // The keys' names stamped under them; KEY MAP printed over the first two, with a red bracket.
    for (int i = 0; i < 3; ++i)
    {
        const auto key = keyCell (i).toFloat();
        stamped (g, i == 0 ? "SPLIT" : i == 1 ? "FACTORY" : "VARIATION", key.translated (0.0f, key.getHeight() + 2.0f).withHeight (12.0f).expanded (20.0f, 0.0f), 8.0f);
    }
    {
        const auto a = keyCell (0).toFloat(), b = keyCell (1).toFloat();
        const auto y = a.getY() - 8.0f;
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        const auto label = juce::Rectangle<float> (a.getCentreX(), y - 6.0f, b.getCentreX() - a.getCentreX(), 12.0f);
        g.drawText ("KEY MAP", label, juce::Justification::centred, false);
        const auto gap = 26.0f;
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (label.getCentreX() + gap, y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, 1.2f, 5.0f));
        g.fillRect (juce::Rectangle<float> (b.getCentreX() - 1.2f, y, 1.2f, 5.0f));
    }

    // OUTPUT: its name over the side, and the meter screen (a ladder of LED bars, -60 to +6 dB).
    stamped (g, "OUTPUT", sideArea().withHeight (14.0f).translated (0.0f, -headerHeight * 0.5f - 8.0f), 9.0f, juce::Justification::centredLeft);
    {
        const auto glass = LedDisplay::paintScreen (g, meterArea().toFloat(), screenSize);
        auto bars = glass.reduced (10.0f, 10.0f);
        constexpr int count = 22;
        const auto pitch = bars.getHeight() / (float) count;
        const auto lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (meterDb + 60.0f) / 66.0f) * (float) count);
        for (int i = 0; i < count; ++i)
        {
            const auto bar = juce::Rectangle<float> (bars.getX(), bars.getBottom() - pitch * (float) (i + 1), bars.getWidth(), pitch).reduced (0.0f, pitch * 0.22f);
            const auto colour = i >= count - 2 ? juce::Colour (0xffffb070) : orange;   // over 0 dB: paler
            if (i < lit)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.fillRoundedRectangle (bar.expanded (2.0f), 2.0f);
                g.setColour (colour);
            }
            else
            {
                g.setColour (colour.withAlpha (0.1f));
            }
            g.fillRoundedRectangle (bar, 1.5f);
        }
        LedDisplay::paintGlassOverlay (g, glass, screenSize);
    }
}

void GuitarRack::paintOverChildren (juce::Graphics&)
{
    Finish::paintAs (std::nullopt);
}
