#include "LedDisplay.h"

#include "ParagonLookAndFeel.h"

namespace
{
    // Which of the seven segments (a-g) light for each digit.
    //    a
    //  f   b
    //    g
    //  e   c
    //    d
    std::array<bool, 7> segmentsFor (juce::juce_wchar digit)
    {
        switch (digit)
        {
            case '0': return { 1, 1, 1, 1, 1, 1, 0 };
            case '1': return { 0, 1, 1, 0, 0, 0, 0 };
            case '2': return { 1, 1, 0, 1, 1, 0, 1 };
            case '3': return { 1, 1, 1, 1, 0, 0, 1 };
            case '4': return { 0, 1, 1, 0, 0, 1, 1 };
            case '5': return { 1, 0, 1, 1, 0, 1, 1 };
            case '6': return { 1, 0, 1, 1, 1, 1, 1 };
            case '7': return { 1, 1, 1, 0, 0, 0, 0 };
            case '8': return { 1, 1, 1, 1, 1, 1, 1 };
            case '9': return { 1, 1, 1, 1, 0, 1, 1 };
            case '-': return { 0, 0, 0, 0, 0, 0, 1 };
            default:  return { 0, 0, 0, 0, 0, 0, 0 };
        }
    }
}

LedDisplay::LedDisplay()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("The current rig. The dot lights when it has unsaved changes. Click to browse rigs.");
}

void LedDisplay::setContents (const Contents& newContents)
{
    const auto changed = newContents.number != contents.number || newContents.name != contents.name
                      || newContents.bank != contents.bank || newContents.modified != contents.modified
                      || newContents.bypassed != contents.bypassed || ! (newContents.tuner == contents.tuner);
    contents = newContents;
    if (changed)
        repaint();
}

void LedDisplay::drawDigit (juce::Graphics& g, juce::juce_wchar digit, juce::Rectangle<float> area, juce::Colour lit)
{
    // A classic seven-segment digit: each segment a long hexagon with
    // pointed ends and a small gap at the joints, leaning forwards like
    // the mockup's italic "001". Unlit segments are all but invisible, as
    // on a real LED display behind tinted glass.
    const auto w = area.getWidth(), h = area.getHeight();
    const auto t = w * 0.19f;            // segment thickness
    const auto gap = t * 0.18f;          // space at each joint
    const auto lean = h * 0.2f;          // how far the top leans right of the bottom
    const auto lit7 = segmentsFor (digit);

    // Upright cell (x 0..cellW, y 0..h); leaned with a shear afterwards.
    const auto cellW = w - lean;
    const auto half = t * 0.5f;

    const auto horizontal = [&] (float y)
    {
        juce::Path p;
        const auto x1 = half + gap, x2 = cellW - half - gap;
        p.startNewSubPath (x1, y);
        p.lineTo (x1 + half, y - half);
        p.lineTo (x2 - half, y - half);
        p.lineTo (x2, y);
        p.lineTo (x2 - half, y + half);
        p.lineTo (x1 + half, y + half);
        p.closeSubPath();
        return p;
    };
    const auto vertical = [&] (float x, float y1, float y2)
    {
        juce::Path p;
        y1 += gap;
        y2 -= gap;
        p.startNewSubPath (x, y1);
        p.lineTo (x + half, y1 + half);
        p.lineTo (x + half, y2 - half);
        p.lineTo (x, y2);
        p.lineTo (x - half, y2 - half);
        p.lineTo (x - half, y1 + half);
        p.closeSubPath();
        return p;
    };

    const auto top = half, middle = h * 0.5f, bottom = h - half;
    const auto left = half, right = cellW - half;
    const std::array<juce::Path, 7> segments {
        horizontal (top),                     // a
        vertical (right, top, middle),        // b
        vertical (right, middle, bottom),     // c
        horizontal (bottom),                  // d
        vertical (left, middle, bottom),      // e
        vertical (left, top, middle),         // f
        horizontal (middle),                  // g
    };

    const auto shear = juce::AffineTransform::shear (-lean / h, 0.0f).translated (area.getX() + lean, area.getY());

    juce::Path litPath, unlitPath;
    for (size_t i = 0; i < segments.size(); ++i)
        (lit7[i] ? litPath : unlitPath).addPath (segments[i], shear);

    g.setColour (lit.withAlpha (0.05f));
    g.fillPath (unlitPath);

    // Glow: the light bleeding into the glass round each lit segment.
    g.setColour (lit.withAlpha (0.12f));
    g.strokePath (litPath, juce::PathStrokeType (t * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (lit.withAlpha (0.22f));
    g.strokePath (litPath, juce::PathStrokeType (t * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (lit);
    g.fillPath (litPath);
    g.setColour (juce::Colour (0xffffc28a).withAlpha (0.5f));   // hotter centre line, like a lit LED segment
    g.strokePath (litPath, juce::PathStrokeType (t * 0.12f), juce::AffineTransform());
}

juce::Rectangle<float> LedDisplay::paintScreen (juce::Graphics& g, juce::Rectangle<float> bounds, float size)
{
    // A polished metal frame set into the face: chrome lit from above, with
    // a dark seam where it meets the black bezel inside it.
    juce::Path frame;
    frame.addRoundedRectangle (bounds.reduced (1.0f), 11.0f * size);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (9.0f * size), { 0, juce::roundToInt (3.0f * size) }).drawForPath (g, frame);
    {
        juce::ColourGradient chrome (juce::Colour (0xfffaf3f6), 0.0f, bounds.getY(), juce::Colour (0xff5e4f57), 0.0f, bounds.getBottom(), false);
        chrome.addColour (0.12, juce::Colour (0xffcbbac3));
        chrome.addColour (0.5, juce::Colour (0xff8f7d87));
        chrome.addColour (0.85, juce::Colour (0xffb7a4ae));
        g.setGradientFill (chrome);
        g.fillPath (frame);
    }
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.strokePath (frame, juce::PathStrokeType (1.0f));

    // Inside it, the bezel: thick glossy black plastic, with a fine highlight
    // along its top edge fading down the sides.
    const auto bezelArea = bounds.reduced (4.0f * size);
    juce::Path bezel;
    bezel.addRoundedRectangle (bezelArea, 8.0f * size);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b292c), bezelArea.getTopLeft(),
                                             juce::Colour (0xff070607), bezelArea.getBottomLeft(), false));
    g.fillPath (bezel);
    {
        juce::Path edge;
        edge.addRoundedRectangle (bezelArea.reduced (0.5f), 8.0f * size);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), 0.0f, bezelArea.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, bezelArea.getY() + bezelArea.getHeight() * 0.4f, false));
        g.strokePath (edge, juce::PathStrokeType (1.0f));
    }

    // Tinted glass: very dark, warmed by the orange light behind it, with a
    // faint reflection across the top.
    const auto glass = bounds.reduced (11.0f * size);
    const auto radius = 5.0f * size;
    juce::Path glassPath;
    glassPath.addRoundedRectangle (glass, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c0b06), glass.getX() + glass.getWidth() * 0.25f, glass.getCentreY(),
                                             juce::Colour (0xff070302), glass.getRight(), glass.getY(), true));
    g.fillPath (glassPath);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (glass, radius, 1.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                             juce::Colours::white.withAlpha (0.0f), glass.getX(), glass.getCentreY(), false));
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), radius);
    return glass;
}

void LedDisplay::paint (juce::Graphics& g)
{
    const auto glass = paintScreen (g, getLocalBounds().toFloat());

    auto content = glass.reduced (glass.getWidth() * 0.05f, glass.getHeight() * 0.18f);
    const auto colour = contents.bypassed ? ledOrange.withAlpha (0.45f) : ledOrange;

    if (contents.tuner.active)
    {
        paintTuner (g, content, ledOrange);
        paintGlassOverlay (g, glass);
        return;
    }

    // Big digits: the rig's preset number, about half the glass high, as in the mockup.
    const auto digits = contents.number > 0 ? juce::String (contents.number).paddedLeft ('0', 3) : juce::String ("---");
    auto digitsArea = content.removeFromLeft (glass.getWidth() * 0.33f);
    const auto digitHeight = glass.getHeight() * 0.48f;
    const auto digitWidth = digitHeight * 0.8f;
    auto digitRow = digitsArea.withSizeKeepingCentre (digitsArea.getWidth(), digitHeight);
    for (int i = 0; i < 3; ++i)
    {
        drawDigit (g, digits[i], digitRow.removeFromLeft (digitWidth), colour);
        digitRow.removeFromLeft (digitWidth * 0.12f);
    }

    content.removeFromLeft (glass.getWidth() * 0.05f);

    // Name, a rule, then the bank and preset line with the unsaved-changes dot.
    // Sized from the glass, as in the mockup: the name about a fifth of its
    // height, the bank and preset line a little smaller.
    auto nameArea = content.removeFromTop (content.getHeight() * 0.5f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (glass.getHeight() * 0.22f));
    g.drawFittedText (contents.bypassed ? juce::String ("BYPASSED") : contents.name.toUpperCase(),
                      nameArea.toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);

    g.setColour (colour.withAlpha (0.3f));
    g.fillRect (content.getX(), nameArea.getBottom() + 4.0f, content.getWidth(), 1.0f);

    auto infoArea = content.withTrimmedTop (9.0f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (glass.getHeight() * 0.17f));
    if (contents.bank.isEmpty())
    {
        g.drawText ("NOT SAVED", infoArea, juce::Justification::topLeft, false);
    }
    else
    {
        g.drawText ("BANK " + contents.bank, infoArea, juce::Justification::topLeft, false);
        g.drawText ("PRESET " + juce::String (contents.number), infoArea.withTrimmedLeft (infoArea.getWidth() * 0.42f),
                    juce::Justification::topLeft, false);
    }

    // Unsaved-changes dot: a red LED at the end of the line.
    const auto dot = juce::Rectangle<float> (5.5f, 5.5f).withCentre ({ infoArea.getRight() - 10.0f, infoArea.getY() + glass.getHeight() * 0.085f });
    if (contents.modified)
    {
        g.setColour (ParagonColours::glow.withAlpha (0.35f));
        g.fillEllipse (dot.expanded (2.5f));
    }
    g.setColour (contents.modified ? juce::Colour (0xffff6a5a) : ParagonColours::glow.withAlpha (0.12f));
    g.fillEllipse (dot);

    paintGlassOverlay (g, glass);
}

void LedDisplay::paintGlassOverlay (juce::Graphics& g, juce::Rectangle<float> glass, float size)
{
    // What sits in front of the readout: the glass itself.
    juce::Path glassPath;
    glassPath.addRoundedRectangle (glass, 5.0f * size);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (glassPath);

    // The display's fine pixel rows, faintly visible.
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (auto y = glass.getY() + 1.0f; y < glass.getBottom(); y += 2.0f)
        g.fillRect (glass.getX(), y, glass.getWidth(), 0.6f);

    // The bezel's shadow falling on the glass along the top.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, glass.getY(),
                                             juce::Colours::black.withAlpha (0.0f), 0.0f, glass.getY() + 9.0f * size, false));
    g.fillRect (glass.withHeight (9.0f * size));

    // A reflection in the glass: a soft diagonal band of light across the
    // upper left, and a fainter one beside it.
    const auto band = [&] (float start, float width, float alpha)
    {
        juce::Path streak;
        const auto x = glass.getX() + glass.getWidth() * start, w = glass.getWidth() * width, slant = glass.getHeight() * 0.6f;
        streak.addQuadrilateral (x + slant, glass.getY(), x + slant + w, glass.getY(),
                                 x + w, glass.getBottom(), x, glass.getBottom());
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (alpha), 0.0f, glass.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, glass.getBottom(), false));
        g.fillPath (streak);
    };
    band (0.04f, 0.13f, 0.09f);
    band (0.2f, 0.035f, 0.06f);
}

void LedDisplay::paintTuner (juce::Graphics& g, juce::Rectangle<float> content, juce::Colour colour) const
{
    const auto& tuner = contents.tuner;
    const auto inTune = tuner.hasNote && std::abs (tuner.cents) < 3.0;
    const juce::Colour inTuneColour { 0xfff4eef1 };   // pearl white (the family's colours only): bright against the red
    const auto noteColour = inTune ? inTuneColour : colour;

    // The note name, big, where the rig number usually is.
    auto noteArea = content.removeFromLeft (content.getHeight() * 1.45f);
    g.setColour (noteColour.withAlpha (tuner.hasNote ? 0.25f : 0.0f));
    g.setFont (juce::FontOptions (noteArea.getHeight() * 1.05f, juce::Font::bold));
    const auto note = tuner.hasNote ? tuner.note : juce::String ("-");
    const auto letterArea = noteArea.withTrimmedRight (noteArea.getWidth() * 0.3f);
    g.drawText (note, letterArea.translated (0.0f, 1.5f).expanded (2.0f), juce::Justification::centred, false);   // glow
    g.setColour (tuner.hasNote ? noteColour : colour.withAlpha (0.3f));
    g.drawText (note, letterArea, juce::Justification::centred, false);
    if (tuner.hasNote)
    {
        g.setFont (juce::FontOptions (noteArea.getHeight() * 0.32f, juce::Font::bold));
        g.drawText (juce::String (tuner.octave), noteArea.removeFromRight (noteArea.getWidth() * 0.3f).removeFromBottom (noteArea.getHeight() * 0.45f),
                    juce::Justification::centredLeft, false);
    }

    content.removeFromLeft (content.getHeight() * 0.25f);

    // Top line: TUNER, listening (it never changes the sound).
    auto topLine = content.removeFromTop (content.getHeight() * 0.3f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (topLine.getHeight() * 0.85f, juce::Font::bold));
    g.drawText ("TUNER", topLine, juce::Justification::centredLeft, false);
    g.setColour (colour.withAlpha (0.6f));
    g.drawText ("LISTENING", topLine, juce::Justification::centredRight, false);

    // The meter: a row of LED bars from -50 to +50 cents. The centre bar is
    // taller; the lit bar shows how far off the note is.
    auto meter = content.removeFromTop (content.getHeight() * 0.62f).reduced (0.0f, content.getHeight() * 0.08f);
    constexpr int numBars = 25;
    const auto barWidth = meter.getWidth() / (float) numBars;
    const auto litBar = tuner.hasNote ? juce::jlimit (0, numBars - 1, juce::roundToInt ((tuner.cents + 50.0) / 100.0 * (numBars - 1))) : -1;
    for (int i = 0; i < numBars; ++i)
    {
        const auto isCentre = i == numBars / 2;
        auto bar = juce::Rectangle<float> (meter.getX() + barWidth * (float) i, meter.getY(), barWidth, meter.getHeight())
                       .reduced (barWidth * 0.22f, isCentre ? 0.0f : meter.getHeight() * 0.18f);
        if (i == litBar)
        {
            const auto barColour = isCentre ? inTuneColour : colour;
            g.setColour (barColour.withAlpha (0.3f));
            g.fillRoundedRectangle (bar.expanded (2.5f), 2.0f);
            g.setColour (barColour);
        }
        else
        {
            g.setColour ((isCentre ? inTuneColour : colour).withAlpha (isCentre ? 0.3f : 0.1f));
        }
        g.fillRoundedRectangle (bar, 1.5f);
    }

    // Bottom line: how far off, in cents, and the frequency.
    g.setColour (colour);
    g.setFont (juce::FontOptions (content.getHeight() * 0.8f, juce::Font::bold));
    if (tuner.hasNote)
    {
        const auto cents = juce::roundToInt (tuner.cents);
        g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " CENTS", content, juce::Justification::centredLeft, false);
        g.drawText (juce::String (tuner.frequency, 1) + " HZ", content, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (colour.withAlpha (0.6f));
        g.drawText ("WAITING FOR A NOTE", content, juce::Justification::centredLeft, false);
    }
}

void LedDisplay::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClick)
        onClick();
}
