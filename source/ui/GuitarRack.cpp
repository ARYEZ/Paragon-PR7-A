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
    constexpr float badgeWidth = 190.0f, displayWidth = 250.0f, sideWidth = 196.0f;
    constexpr float titleHeight = 24.0f;   // the page's name, over it
    constexpr float screenHeight = 136.0f;
    const auto orange = LedDisplay::ledOrange;

    juce::FontOptions font (float height, int style = juce::Font::bold) { return juce::FontOptions (height, style); }

    void stamped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                  juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (font (height));
        ParagonLookAndFeel::drawStampedText (g, text, area, justification, Finish::ink());
    }

    // A red bracket printed over two keys with a word in its middle (as PR7-G's HOLD TO TUNER).
    void bracket (juce::Graphics& g, juce::Rectangle<float> a, juce::Rectangle<float> b, const juce::String& word)
    {
        const auto y = a.getY() - 8.0f;
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        const auto label = juce::Rectangle<float> (a.getCentreX(), y - 6.0f, b.getCentreX() - a.getCentreX(), 12.0f);
        g.drawText (word, label, juce::Justification::centred, false);
        const auto gap = juce::GlyphArrangement::getStringWidth (font (8.0f), word) * 0.5f + 6.0f;
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (label.getCentreX() + gap, y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, 1.2f, 5.0f));
        g.fillRect (juce::Rectangle<float> (b.getCentreX() - 1.2f, y, 1.2f, 5.0f));
    }
}

GuitarRack::GuitarRack()
{
    setOpaque (false);
    for (auto* key : { &playingKey, &frettingKey })
    {
        key->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);   // a small key, its stripe lit on its page
        addAndMakeVisible (*key);
    }
    playingKey.setComponentID ("rackPlaying");
    frettingKey.setComponentID ("rackFretting");
    playingKey.setTooltip ("PLAYING: the tone and how notes play (TONE ... VARIATION)");
    frettingKey.setTooltip ("FRETTING: picking, fretting, performance, force string, hand, capo, mono");
    playingKey.onClick = [this] { setPage (Page::playing); };
    frettingKey.onClick = [this] { setPage (Page::fretting); };
    setPage (Page::playing);
}

void GuitarRack::resized()
{
    satin = Finish::makeSatinGrain (juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), 1234);
    playingKey.setBounds (pageKeyCell (0));
    frettingKey.setBounds (pageKeyCell (1));
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

void GuitarRack::setPage (Page newPage)
{
    playingKey.setToggleState (newPage == Page::playing, juce::dontSendNotification);
    frettingKey.setToggleState (newPage == Page::fretting, juce::dontSendNotification);
    if (newPage == page)
        return;
    page = newPage;
    repaint();
    if (onPageChanged != nullptr)
        onPageChanged();
}

//==============================================================================
juce::Rectangle<float> GuitarRack::face() const
{
    return getLocalBounds().toFloat().reduced (glowRoom).withTrimmedBottom (unitShape.frontEdge).reduced (9.0f);
}

juce::Rectangle<float> GuitarRack::content() const { return face().reduced (12.0f, 8.0f); }
juce::Rectangle<float> GuitarRack::badgeColumn() const { return content().withWidth (badgeWidth); }
juce::Rectangle<float> GuitarRack::displayColumn() const { return content().withTrimmedLeft (badgeWidth + sectionGap).withWidth (displayWidth); }
juce::Rectangle<float> GuitarRack::sideColumn() const { return content().removeFromRight (sideWidth); }

juce::Rectangle<float> GuitarRack::pageArea() const
{
    auto area = content();
    area.removeFromLeft (badgeWidth + sectionGap + displayWidth + sectionGap);
    area.removeFromRight (sideWidth + sectionGap);
    area.removeFromTop (titleHeight + 6.0f);
    return area;
}

juce::Rectangle<int> GuitarRack::pageKeyCell (int index) const
{
    auto area = badgeColumn();
    area = area.removeFromBottom (60.0f).withTrimmedBottom (14.0f);   // names stamped under them
    const auto width = area.getWidth() / 2.0f;
    return area.withX (area.getX() + width * (float) index).withWidth (width).withSizeKeepingCentre (54.0f, 28.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::screenArea() const { return displayColumn().withHeight (screenHeight).toNearestInt(); }

juce::Rectangle<int> GuitarRack::keyCell (int index) const
{
    auto area = displayColumn().withTrimmedTop (screenHeight + 14.0f + 14.0f);   // KEY MAP printed over the first two
    const auto width = area.getWidth() / 3.0f;
    return area.withX (area.getX() + width * (float) index).withWidth (width).withTrimmedBottom (12.0f)
               .withSizeKeepingCentre (54.0f, 28.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::knobCell (int index) const
{
    const auto area = pageArea();
    const auto width = area.getWidth() / (float) numKnobs;
    const auto height = juce::jmin (area.getHeight(), 136.0f);
    return juce::Rectangle<float> (area.getX() + width * (float) index, area.getCentreY() - height * 0.5f, width, height)
        .reduced (4.0f, 0.0f).toNearestInt();
}

// FRETTING: the menus in a column on the left, the two knobs, the four value screens in two columns on the right.
juce::Rectangle<int> GuitarRack::menuCell (int index) const
{
    const auto area = pageArea().withWidth (236.0f);
    const auto row = area.getHeight() / 3.0f;
    return juce::Rectangle<float> (area.getX(), area.getY() + row * (float) index + 16.0f, area.getWidth(), 30.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::frettingKnobCell (int index) const
{
    const auto area = pageArea().withTrimmedLeft (236.0f + 36.0f).withWidth (240.0f);
    const auto height = juce::jmin (area.getHeight(), 136.0f);
    return juce::Rectangle<float> (area.getX() + 120.0f * (float) index, area.getCentreY() - height * 0.5f, 120.0f, height)
        .reduced (6.0f, 0.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::boxCell (int index) const
{
    const auto area = pageArea().withTrimmedLeft (236.0f + 36.0f + 240.0f + 36.0f);
    const auto width = area.getWidth() / 2.0f, row = area.getHeight() / 2.0f;
    return juce::Rectangle<float> (area.getX() + width * (float) (index % 2), area.getY() + row * (float) (index / 2) + 26.0f, width, 36.0f)
        .reduced (8.0f, 0.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::meterArea() const
{
    return sideColumn().withTrimmedTop (titleHeight + 6.0f).withWidth (58.0f).toNearestInt();
}

juce::Rectangle<int> GuitarRack::sideKnobCell (int index) const
{
    auto area = sideColumn().withTrimmedTop (titleHeight + 6.0f).withTrimmedLeft (58.0f + 10.0f);
    const auto height = area.getHeight() / 2.0f;
    return area.withY (area.getY() + height * (float) index).withHeight (height).reduced (2.0f, 1.0f).toNearestInt();
}

//==============================================================================
void GuitarRack::paint (juce::Graphics& g)
{
    // The unit, and everything on it (its knobs and keys too, till paintOverChildren), in pearl black.
    Finish::paintAs (Finish::Type::pearlBlack);

    const auto chassis = getLocalBounds().toFloat().reduced (glowRoom);
    PedalBody::paintUnit (g, chassis, face(), [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);

    // The badge, as on PR7-G's unit: PARAGON over the PR7-I mark, what it is printed in red.
    PedalBody::paintBadge (g, badgeColumn().withHeight (88.0f).translated (2.0f, 2.0f), "GUITAR INSTRUMENT");
    {
        const auto a = pageKeyCell (0).toFloat(), b = pageKeyCell (1).toFloat();
        bracket (g, a, b, "PAGE");
        stamped (g, "PLAYING", a.translated (0.0f, a.getHeight() + 2.0f).withHeight (12.0f).expanded (24.0f, 0.0f), 8.0f);
        stamped (g, "FRETTING", b.translated (0.0f, b.getHeight() + 2.0f).withHeight (12.0f).expanded (24.0f, 0.0f), 8.0f);
    }

    // The printed lines between the sections.
    const auto printedLine = [&] (float x)
    {
        g.setColour (Finish::printedRed().withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, face().getY() + 14.0f, 1.5f, face().getHeight() - 28.0f));
    };
    printedLine (displayColumn().getX() - sectionGap * 0.5f);
    printedLine (displayColumn().getRight() + sectionGap * 0.5f);
    printedLine (sideColumn().getX() - sectionGap * 0.5f);

    paintScreen (g);

    // The keys' names stamped under them; KEY MAP printed over the first two.
    for (int i = 0; i < 3; ++i)
    {
        const auto key = keyCell (i).toFloat();
        stamped (g, i == 0 ? "SPLIT" : i == 1 ? "FACTORY" : "VARIATION", key.translated (0.0f, key.getHeight() + 2.0f).withHeight (12.0f).expanded (20.0f, 0.0f), 8.0f);
    }
    bracket (g, keyCell (0).toFloat(), keyCell (1).toFloat(), "KEY MAP");

    // The page's name, over it.
    auto title = pageArea().withY (content().getY()).withHeight (titleHeight);
    g.setFont (font (15.0f, juce::Font::bold | juce::Font::italic));
    ParagonLookAndFeel::drawStampedText (g, page == Page::playing ? "PLAYING" : "FRETTING", title.removeFromLeft (120.0f),
                                         juce::Justification::centredLeft, Finish::ink());
    g.setColour (Finish::printedRed());
    g.setFont (font (8.0f));
    g.drawText (page == Page::playing ? "TONE AND HOW EACH NOTE PLAYS" : "PICKING, FRETTING AND THE HAND", title, juce::Justification::centredLeft, false);

    // FRETTING: the menus' and value boxes' names and screens (the menus and boxes themselves sit on them).
    if (page == Page::fretting)
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto cell = menuCell (i).toFloat();
            stamped (g, menuNames[i], cell.withHeight (12.0f).translated (0.0f, -15.0f), 8.0f, juce::Justification::centredLeft);
            LedDisplay::paintScreen (g, cell, screenSize);
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = boxCell (i).toFloat();
            stamped (g, boxNames[i], cell.withHeight (12.0f).translated (0.0f, -15.0f), 8.0f, juce::Justification::centredLeft);
        }
    }

    // OUTPUT: its name, the status bar (off while bypassed), the meter screen.
    auto sideTitle = sideColumn().withHeight (titleHeight);
    ParagonLookAndFeel::drawNeonBar (g, sideTitle.removeFromRight (34.0f).withSizeKeepingCentre (30.0f, 6.0f), on);
    g.setFont (font (15.0f, juce::Font::bold | juce::Font::italic));
    ParagonLookAndFeel::drawStampedText (g, "OUTPUT", sideTitle, juce::Justification::centredLeft, Finish::ink());
    paintMeter (g);
}

void GuitarRack::paintScreen (juce::Graphics& g)
{
    const auto glass = LedDisplay::paintScreen (g, screenArea().toFloat(), screenSize);
    auto text = glass.reduced (12.0f, 9.0f);
    auto captionRow = text.removeFromTop (11.0f);
    g.setColour (orange.withAlpha (0.6f));
    g.setFont (font (8.5f));
    g.drawText (screen.caption, captionRow, juce::Justification::centredLeft, false);
    text.removeFromTop (5.0f);
    auto big = text.removeFromTop (30.0f);
    g.setColour (orange);
    g.setFont (font (13.0f));
    g.drawText (screen.key, big.removeFromRight (40.0f), juce::Justification::centredRight, false);
    g.setFont (font (23.0f));
    g.drawFittedText (screen.articulation, big.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    text.removeFromTop (5.0f);
    g.setColour (orange.withAlpha (0.25f));
    g.fillRect (text.removeFromTop (1.0f));
    text.removeFromTop (7.0f);
    g.setColour (orange);
    g.setFont (font (13.0f));
    g.drawText (screen.line1, text.removeFromTop (18.0f), juce::Justification::centredLeft, false);
    g.setColour (orange.withAlpha (0.65f));
    g.setFont (font (9.5f));
    g.drawFittedText (screen.line2, text.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    LedDisplay::paintGlassOverlay (g, glass, screenSize);
}

void GuitarRack::paintMeter (juce::Graphics& g)
{
    // A ladder of LED bars, -60 to +6 dB, the top two (over 0 dB) paler.
    const auto glass = LedDisplay::paintScreen (g, meterArea().toFloat(), screenSize);
    auto bars = glass.reduced (10.0f, 10.0f);
    constexpr int count = 20;
    const auto pitch = bars.getHeight() / (float) count;
    const auto lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (meterDb + 60.0f) / 66.0f) * (float) count);
    for (int i = 0; i < count; ++i)
    {
        const auto bar = juce::Rectangle<float> (bars.getX(), bars.getBottom() - pitch * (float) (i + 1), bars.getWidth(), pitch).reduced (0.0f, pitch * 0.22f);
        const auto colour = i >= count - 2 ? juce::Colour (0xffffb070) : orange;
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

void GuitarRack::paintOverChildren (juce::Graphics& g)
{
    // FRETTING: the glass over the menus' screens (their text sits behind it, as on any of the unit's screens).
    if (page == Page::fretting)
        for (int i = 0; i < 3; ++i)
            LedDisplay::paintGlassOverlay (g, menuCell (i).toFloat().reduced (3.0f), screenSize);
    Finish::paintAs (std::nullopt);
}
