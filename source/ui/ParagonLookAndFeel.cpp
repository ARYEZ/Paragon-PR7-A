#include "ParagonLookAndFeel.h"

#include "Finish.h"

#include <ParagonFonts.h>

ParagonLookAndFeel::ParagonLookAndFeel()
    : regular (juce::Typeface::createSystemTypefaceFor (ParagonFonts::InterRegular_ttf, ParagonFonts::InterRegular_ttfSize)),
      semiBold (juce::Typeface::createSystemTypefaceFor (ParagonFonts::InterSemiBold_ttf, ParagonFonts::InterSemiBold_ttfSize)),
      blackItalic (juce::Typeface::createSystemTypefaceFor (ParagonFonts::InterDisplayBlackItalic_ttf,
                                                           ParagonFonts::InterDisplayBlackItalic_ttfSize))
{
    // The family's colours under everything JUCE draws by itself (PR7-D 0.11.0): the dark scheme's blue "fill" and
    // "highlight" (slider thumbs and tracks, toggles, selected text, scroll bars...) as orange; onyx and pearl for the rest.
    setColourScheme ({ ParagonColours::background, juce::Colour (0xff151217), juce::Colour (0xff050405), ParagonColours::panelEdge,
                       ParagonColours::text, ParagonColours::accent, juce::Colours::white, ParagonColours::accentDim, juce::Colours::white });
    setColour (juce::ResizableWindow::backgroundColourId, ParagonColours::background);
    setColour (juce::Label::textColourId, ParagonColours::text);
    setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, ParagonColours::accentDim);
    setColour (juce::TextEditor::textColourId, ParagonColours::accent);
    setColour (juce::CaretComponent::caretColourId, ParagonColours::accent);
    setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    setColour (juce::ToggleButton::textColourId, ParagonColours::text);
    setColour (juce::TooltipWindow::backgroundColourId, ParagonColours::panel);
    setColour (juce::TooltipWindow::textColourId, ParagonColours::text);
    setColour (juce::TooltipWindow::outlineColourId, ParagonColours::panelEdge);

    // Menus: black with white text, as on PR7-C (the highlight is drawn, below).
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff050405));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffe0103c));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

juce::ColourGradient ParagonLookAndFeel::menuHighlight (juce::Rectangle<float> area)
{
    // Red, through hot pink, to orange, left to right.
    juce::ColourGradient gradient (juce::Colour (0xffe0103c), area.getX(), 0.0f, juce::Colour (0xffff7a1a), area.getRight(), 0.0f, false);
    gradient.addColour (0.5, juce::Colour (0xffff2f7a));
    return gradient;
}

void ParagonLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (bounds, 1.0f);
}

void ParagonLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (ParagonColours::panelEdge);
        g.fillRect (area.reduced (6, 0).withSizeKeepingCentre (area.getWidth() - 12, 1));
        return;
    }

    auto r = area.reduced (2, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setGradientFill (menuHighlight (r));
        g.fillRoundedRectangle (r, 3.0f);
    }

    const auto colour = ! isActive ? juce::Colours::white.withAlpha (0.35f)
                                   : (textColour != nullptr && ! isHighlighted ? *textColour : juce::Colours::white);
    g.setColour (colour);
    auto font = getPopupMenuFont();
    const auto maxFontHeight = r.getHeight() / 1.3f;
    if (font.getHeight() > maxFontHeight)
        font.setHeight (maxFontHeight);
    g.setFont (font);

    auto textArea = r.reduced (8.0f, 0.0f);
    auto tickArea = textArea.removeFromLeft (font.getHeight() * 0.9f);
    if (icon != nullptr)
        icon->drawWithin (g, tickArea.reduced (2.0f), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    else if (isTicked)
    {
        // A tick, in white.
        const auto tick = tickArea.withSizeKeepingCentre (tickArea.getWidth() * 0.7f, tickArea.getWidth() * 0.7f);
        juce::Path path;
        path.startNewSubPath (tick.getX(), tick.getCentreY());
        path.lineTo (tick.getX() + tick.getWidth() * 0.38f, tick.getBottom());
        path.lineTo (tick.getRight(), tick.getY());
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (hasSubMenu)
    {
        const auto arrowH = 0.6f * font.getAscent();
        const auto x = textArea.removeFromRight (arrowH).getX();
        const auto halfH = textArea.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (x, halfH - arrowH * 0.5f);
        arrow.lineTo (x + arrowH * 0.6f, halfH);
        arrow.lineTo (x, halfH + arrowH * 0.5f);
        g.strokePath (arrow, juce::PathStrokeType (1.6f));
    }

    textArea.removeFromRight (3.0f);
    g.drawFittedText (text, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        auto small = font;
        small.setHeight (small.getHeight() * 0.75f);
        g.setFont (small);
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

void ParagonLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto ledSize = juce::jmin (12.0f, bounds.getHeight() - 4.0f);
    const auto led = bounds.removeFromLeft (bounds.getHeight()).withSizeKeepingCentre (ledSize, ledSize);
    const auto on = button.getToggleState();

    if (on)
    {
        // Soft glow behind a lit LED.
        g.setColour (ParagonColours::accent.withAlpha (0.25f));
        g.fillEllipse (led.expanded (3.0f));
    }

    g.setColour (on ? ParagonColours::accent : ParagonColours::accentDim.darker (0.6f));
    g.fillEllipse (led);
    g.setColour (highlighted ? ParagonColours::text : juce::Colours::black);
    g.drawEllipse (led, 1.0f);

    if (button.getButtonText().isNotEmpty())
    {
        // The button's own text colour, so the same switch works on the dark
        // panels (light text) and printed on the silver pedal (dark text).
        const auto textColour = button.findColour (juce::ToggleButton::textColourId);
        g.setColour (on ? textColour : textColour.withMultipliedAlpha (0.6f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (2.0f), juce::Justification::centredLeft, true);
    }
}

void ParagonLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool highlighted, bool down)
{
    if (button.getProperties().contains (plainTextProperty))
    {
        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.08f : 0.04f));
            g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4.0f);
        }
        return;
    }

    if (button.getProperties()[outlinedButtonProperty])
    {
        auto box = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = button.getToggleState();
        g.setColour (juce::Colours::white.withAlpha (down ? 0.35f : highlighted ? 0.75f : 0.55f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawRoundedRectangle (box.translated (0.0f, 1.0f), 3.0f, 1.0f);
        g.setColour (on ? ParagonColours::glow : ParagonColours::ink.withAlpha (0.35f));
        g.drawRoundedRectangle (box, 3.0f, on ? 1.5f : 1.0f);
        return;
    }

    if (button.getProperties()[pedalButtonProperty])
    {
        // A small piano-black key, in a recessed surround on the metal: a dark
        // well round the key, a lit lower lip, and a rim, chrome or (pearl black,
        // white + neon) the neon, pink to red to orange across.
        const auto surround = button.getLocalBounds().toFloat().reduced (0.5f);
        if (Finish::neonRings())
        {
            g.setGradientFill (Finish::neonGradient (surround.getX(), surround.getRight()));
            g.fillRoundedRectangle (surround, 5.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, surround.getY(),
                                                     juce::Colours::white.withAlpha (0.25f), 0.0f, surround.getBottom(), false));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6e5f67), 0.0f, surround.getY(),
                                                     juce::Colour (0xffe9dde3), 0.0f, surround.getBottom(), false));
        }
        g.fillRoundedRectangle (surround, 5.0f);
        g.setColour (juce::Colour (0xff1a1216));
        g.fillRoundedRectangle (surround.reduced (1.5f), 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (surround, 5.0f, 0.8f);
        drawPianoKey (g, surround.reduced (2.2f, 2.0f).withTrimmedBottom (0.6f), 3.0f, 2.2f, down, highlighted);
        return;
    }

    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = juce::Colour (0xff1c1c20);
    if (down)
        fill = ParagonColours::accentDim;
    else if (highlighted)
        fill = fill.brighter (0.1f);

    g.setColour (button.isEnabled() ? fill : fill.withAlpha (0.4f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void ParagonLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    if (const auto* size = button.getProperties().getVarPointer (plainTextProperty))
    {
        g.setFont (juce::FontOptions ((float) *size));
        g.setColour (button.findColour (juce::TextButton::textColourOffId));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        return;
    }

    if (button.getProperties()[outlinedButtonProperty])
    {
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (button.getToggleState() ? ParagonColours::glow.darker (0.1f) : ParagonColours::ink);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1);
        return;
    }

    if (! button.getProperties()[pedalButtonProperty])
    {
        LookAndFeel_V4::drawButtonText (g, button, highlighted, down);
        return;
    }

    auto area = button.getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    const auto on = button.getToggleState();

    if (button.getButtonText().isEmpty())
    {
        // Just the LED stripe, like the mockup's EXIT / MODE / BYPASS caps.
        const auto stripe = area.withSizeKeepingCentre (area.getWidth() * 0.6f, 4.0f).translated (0.0f, down ? 0.5f : -0.5f);
        drawNeonBar (g, stripe, on);
        return;
    }

    const juce::Font font (juce::FontOptions (juce::jmin (12.0f, area.getHeight() * 0.7f), juce::Font::bold));
    g.setFont (font);
    // Clear of an LED at the left (0.53.2: on THE DRUM MACHINE's narrow PLAY key the LED sat on the "P"): centred
    // lettering that would reach it is centred in the room right of it instead (squeezed a little if it must be).
    if (const auto clearance = (float) (double) button.getProperties()[ledClearanceProperty]; clearance > 0.0f)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
        if (area.getCentreX() - width * 0.5f < clearance)
            area.setLeft (juce::jmin (clearance, area.getRight() - 1.0f));
    }
    // Light lettering on the black key (lit: the accent orange).
    g.setColour (button.isEnabled() ? (on ? ParagonColours::accent : ParagonColours::text) : ParagonColours::textDim);
    g.drawFittedText (button.getButtonText(), area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void ParagonLookAndFeel::drawPedalKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle)
{
    // The mockup's big knobs, drawn as a real object seen slightly from
    // above: a black cylinder with a spun-metal top, standing in a thin neon
    // ring that's open at the front, with red light pooling on the metal and
    // reflecting up the cylinder's sides. Proportions measured off the mockup.
    using MathConstants = juce::MathConstants<float>;
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto box = bounds.withSizeKeepingCentre (size, size);
    const auto cx = box.getCentreX();
    const auto topY = box.getY() + size * 0.37f;     // centre of the top face
    const auto baseY = box.getY() + size * 0.58f;    // where the cylinder meets the metal
    const auto rx = size * 0.24f, ry = size * 0.19f;
    const auto ringRx = size * 0.29f, ringRy = size * 0.26f;
    const auto ringY = baseY - size * 0.03f;         // the ring's centre, a little above the base
    const juce::Colour neon { 0xffff2e3a }, neonCore { 0xffffd6d6 };
    // On pearl black and pearl white + neon the ring carries the neon's
    // pink to red to orange across, like the keys' rings; on pearl white
    // it's the plain neon red.
    const auto gradientRing = Finish::neonRings();

    // 1. Light pooling on the metal round the ring (kept tight, so it reads
    //    as a glow rather than a wash of pink), and the knob's contact shadow.
    {
        juce::ColourGradient pool (neon.withAlpha (0.35f), cx, baseY, neon.withAlpha (0.0f), cx + ringRx * 1.25f, baseY, true);
        pool.addColour (0.75, neon.withAlpha (0.18f));
        g.setGradientFill (pool);
        g.fillEllipse (juce::Rectangle<float> (ringRx * 2.5f, ringRy * 2.5f).withCentre ({ cx, baseY }));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), cx, baseY + ry * 0.2f,
                                                 juce::Colours::black.withAlpha (0.0f), cx + rx * 1.25f, baseY + ry * 0.2f, true));
        g.fillEllipse (juce::Rectangle<float> (rx * 2.5f, ry * 2.1f).withCentre ({ cx, baseY + ry * 0.25f }));
    }

    // The neon ring as arcs (JUCE angles: clockwise from 12 o'clock). It's
    // open at the front (6 o'clock), like the mockup's.
    constexpr auto gap = 0.62f;   // half the opening, in radians
    const auto ringArc = [&] (float from, float to)
    {
        juce::Path arc;
        arc.addCentredArc (cx, ringY, ringRx, ringRy, 0.0f, from, to, true);
        return arc;
    };
    const auto ringFill = [&] (float alpha)
    {
        if (! gradientRing)
        {
            g.setColour (neon.withAlpha (alpha));
            return;
        }
        // The keys' gradient, brightened: the ring is a light, the rims are metal.
        auto gradient = Finish::neonGradient (cx - ringRx, cx + ringRx);
        for (int i = 0; i < gradient.getNumColours(); ++i)
            gradient.setColour (i, gradient.getColour (i).brighter (0.35f).withAlpha (alpha));
        g.setGradientFill (gradient);
    };
    const auto strokeNeon = [&] (const juce::Path& arc)
    {
        ringFill (0.18f);
        g.strokePath (arc, juce::PathStrokeType (size * 0.075f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ringFill (0.45f);
        g.strokePath (arc, juce::PathStrokeType (size * 0.032f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ringFill (1.0f);
        g.strokePath (arc, juce::PathStrokeType (size * 0.016f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (neonCore);
        g.strokePath (arc, juce::PathStrokeType (size * 0.006f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // 2. The back of the ring (the knob hides most of it).
    strokeNeon (ringArc (MathConstants::pi * 1.5f, MathConstants::pi * 2.5f));

    // A chrome collar where the knob meets the face, like the nut and washer
    // a real pot is mounted with: lit along its back, dark at the front.
    {
        const auto collar = juce::Rectangle<float> (rx * 2.3f, ry * 2.3f).withCentre ({ cx, baseY });
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6eef2), collar.getX(), collar.getY(),
                                                 juce::Colour (0xff3e3238), collar.getRight(), collar.getBottom(), false));
        g.fillEllipse (collar);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (collar, 0.8f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawEllipse (collar.reduced (1.2f), 0.6f);
    }

    // 3. The cylinder: glossy black, with the red glow reflected up its
    //    sides and a faint highlight where it faces the light.
    juce::Path side;
    side.startNewSubPath (cx - rx, topY);
    side.lineTo (cx - rx, baseY);
    side.addCentredArc (cx, baseY, rx, ry, 0.0f, MathConstants::pi * 1.5f, MathConstants::pi * 0.5f);
    side.lineTo (cx + rx, topY);
    side.closeSubPath();
    {
        juce::ColourGradient wall (juce::Colour (0xff030304), cx - rx, 0.0f, juce::Colour (0xff030304), cx + rx, 0.0f, false);
        wall.addColour (0.3, juce::Colour (0xff1b1a1e));
        wall.addColour (0.55, juce::Colour (0xff0c0c0e));
        g.setGradientFill (wall);
        g.fillPath (side);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (side);

        // Knurled grip: fine vertical ridges round the cylinder, closer
        // together towards its sides as they turn away, each with a lit
        // edge and a dark one.
        constexpr int ridges = 36;
        for (int i = 1; i < ridges; ++i)
        {
            const auto theta = MathConstants::pi * (float) i / (float) ridges;
            const auto x = cx - rx * std::cos (theta);
            const auto facing = std::sin (theta);
            g.setColour (juce::Colours::white.withAlpha (0.11f * facing));
            g.fillRect (juce::Rectangle<float> (x, topY, 0.7f, (baseY + ry) - topY));
            g.setColour (juce::Colours::black.withAlpha (0.5f * facing));
            g.fillRect (juce::Rectangle<float> (x + 0.7f, topY, 0.7f, (baseY + ry) - topY));
        }

        for (auto x : { cx - rx * 0.78f, cx + rx * 0.82f, cx - rx * 0.35f })
        {
            const auto strong = std::abs (x - cx) > rx * 0.5f;
            if (! strong)
            {
                g.setGradientFill (juce::ColourGradient (neon.withAlpha (0.0f), 0.0f, topY, neon.withAlpha (0.14f), 0.0f, baseY + ry, false));
                g.fillRect (juce::Rectangle<float> (size * 0.015f, (baseY + ry) - topY).withCentre ({ x, (topY + baseY + ry) * 0.5f }));
                continue;
            }
            const auto streak = juce::Rectangle<float> (size * (strong ? 0.035f : 0.02f), (baseY + ry) - topY).withCentre ({ x, (topY + baseY + ry) * 0.5f });
            g.setGradientFill (juce::ColourGradient (neon.withAlpha (0.0f), 0.0f, topY,
                                                     neon.withAlpha (strong ? 0.75f : 0.3f), 0.0f, baseY + ry, false));
            g.fillRect (streak);
        }
    }

    // 4. The top: spun metal. Light catches it in two broad opposite fans
    //    (anisotropic reflection), drawn as thin wedges whose brightness
    //    follows the angle, plus fine concentric machining rings.
    const auto top = juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre ({ cx, topY });
    juce::Path topFace;
    topFace.addEllipse (top);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (topFace);
        g.setColour (juce::Colour (0xff09090b));
        g.fillPath (topFace);

        constexpr int wedges = 144;
        for (int i = 0; i < wedges; ++i)
        {
            const auto a0 = (float) i / (float) wedges * MathConstants::twoPi;
            const auto a1 = (float) (i + 1) / (float) wedges * MathConstants::twoPi;
            const auto fan = std::pow (0.5f + 0.5f * std::cos (2.0f * (a0 + 0.55f)), 3.0f);
            juce::Path wedge;
            wedge.startNewSubPath (cx, topY);
            wedge.lineTo (cx + std::sin (a0) * rx * 1.1f, topY - std::cos (a0) * ry * 1.1f);
            wedge.lineTo (cx + std::sin (a1) * rx * 1.1f, topY - std::cos (a1) * ry * 1.1f);
            wedge.closeSubPath();
            g.setColour (juce::Colour (0xff6a6970).withAlpha (0.06f + 0.7f * fan));
            g.fillPath (wedge);
        }

        g.setColour (juce::Colours::white.withAlpha (0.012f));
        for (int ring = 1; ring <= 10; ++ring)
            g.drawEllipse (top.withSizeKeepingCentre (top.getWidth() * (float) ring / 10.0f, top.getHeight() * (float) ring / 10.0f), 0.8f);

        // Darker towards the rim, so the top reads as slightly domed.
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), cx, topY,
                                                 juce::Colours::black.withAlpha (0.5f), cx + rx, topY, true));
        g.fillPath (topFace);

        // The pointer: a fine engraved line, turning with the value.
        const auto at = [&] (float fraction)
        {
            return juce::Point<float> (cx + std::sin (angle) * rx * fraction, topY - std::cos (angle) * ry * fraction);
        };
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine ({ at (0.4f).translated (0.0f, 1.0f), at (0.9f).translated (0.0f, 1.0f) }, size * 0.02f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine ({ at (0.4f), at (0.9f) }, size * 0.013f);
    }

    // The top's chamfered rim: a crisp bright edge along the back, where it
    // faces the light, and a fine bright ring just inside it.
    {
        juce::Path backEdge;
        backEdge.addCentredArc (cx, topY, rx - 0.5f, ry - 0.5f, 0.0f, -MathConstants::pi * 0.5f, MathConstants::pi * 0.5f, true);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (backEdge, juce::PathStrokeType (1.1f));
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (top.reduced (rx * 0.12f, ry * 0.12f), 0.7f);
    }
    g.setColour (juce::Colours::black);
    {
        juce::Path frontEdge;
        frontEdge.addCentredArc (cx, topY, rx, ry, 0.0f, MathConstants::pi * 0.55f, MathConstants::pi * 1.45f, true);
        g.strokePath (frontEdge, juce::PathStrokeType (1.2f));
    }

    // 5. The front of the ring passes in front of the cylinder, with small
    //    hooks curling outwards at the opening and marks at the sides.
    const auto leftFront = ringArc (MathConstants::pi + gap, MathConstants::pi * 1.5f);
    const auto rightFront = ringArc (MathConstants::pi * 0.5f, MathConstants::pi - gap);
    strokeNeon (leftFront);
    strokeNeon (rightFront);

    const auto pointOnRing = [&] (float a, float scale)
    {
        return juce::Point<float> (cx + std::sin (a) * ringRx * scale, ringY - std::cos (a) * ringRy * scale);
    };
    for (auto end : { MathConstants::pi - gap, MathConstants::pi + gap })
    {
        juce::Path hook;
        hook.startNewSubPath (pointOnRing (end, 1.0f));
        hook.lineTo (pointOnRing (end + (end < MathConstants::pi ? -0.08f : 0.08f), 1.12f));
        strokeNeon (hook);
    }
    for (auto a : { MathConstants::halfPi, MathConstants::pi * 1.5f })
    {
        const auto mark = juce::Rectangle<float> (size * 0.018f, size * 0.018f).withCentre (pointOnRing (a, 1.1f));
        g.setColour (neon.withAlpha (0.35f));
        g.fillEllipse (mark.expanded (size * 0.012f));
        g.setColour (neonCore);
        g.fillEllipse (mark);
    }
}

void ParagonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float startAngle, float endAngle,
                                           juce::Slider& slider)
{
    if (slider.getProperties()[pedalKnobProperty])
    {
        drawPedalKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), startAngle + sliderPos * (endAngle - startAngle));
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto arcRadius = radius - 3.0f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Track and value arc, like the orange ring on the mockup's knobs.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (ParagonColours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // A two-way knob (its range runs from -x to +x: pan, an envelope's amount,
    // a curve) lights from the top, its centre, so its value reads at a glance.
    const auto twoWay = slider.getMinimum() < 0.0 && juce::approximatelyEqual (-slider.getMinimum(), slider.getMaximum());
    const auto from = twoWay ? startAngle + (endAngle - startAngle) * (float) slider.valueToProportionOfLength (0.0) : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (ParagonColours::accent);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body.
    const auto knobRadius = arcRadius - 6.0f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a40), knob.getTopLeft(),
                                             juce::Colour (0xff101012), knob.getBottomRight(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::black);
    g.drawEllipse (knob, 1.0f);

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobRadius + 3.0f, 3.0f, knobRadius * 0.45f, 1.5f);
    g.setColour (ParagonColours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void ParagonLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical && slider.getProperties()[eqFaderProperty])
    {
        const auto centreX = (float) x + (float) width * 0.5f;
        const auto top = (float) y, bottom = (float) (y + height);   // already inset by the thumb radius
        const auto middle = (top + bottom) * 0.5f;
        const auto colour = slider.findColour (juce::Slider::trackColourId);

        // Groove, then the lit part from 0 dB to the cap.
        g.setColour (juce::Colour (0xff050406));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, bottom - top).withCentre ({ centreX, middle }), 2.0f);
        g.setColour (ParagonColours::panelEdge.brighter (0.2f));
        g.drawRoundedRectangle (juce::Rectangle<float> (4.0f, bottom - top).withCentre ({ centreX, middle }), 2.0f, 0.8f);
        const auto lit = juce::Rectangle<float>::leftTopRightBottom (centreX - 1.5f, juce::jmin (sliderPos, middle), centreX + 1.5f, juce::jmax (sliderPos, middle));
        g.setColour (colour.withAlpha (0.3f));
        g.fillRoundedRectangle (lit.expanded (2.5f, 0.0f), 2.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (lit, 1.5f);

        // The cap: a small piano-black key with a lit line across it.
        const auto cap = juce::Rectangle<float> (juce::jmin (26.0f, (float) width - 4.0f), 14.0f).withCentre ({ centreX, sliderPos });
        drawPianoKey (g, cap, 3.0f, 2.0f, slider.isMouseButtonDown(), slider.isMouseOver());
        g.setColour (colour);
        g.fillRect (cap.reduced (5.0f, 0.0f).withSizeKeepingCentre (cap.getWidth() - 10.0f, 1.6f).translated (0.0f, -0.5f));
        return;
    }

    if (style == juce::Slider::LinearBar && slider.getProperties()[bipolarBarProperty])
    {
        // Lit from the middle (0) to the value, either way.
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto middle = area.getX() + area.getWidth() * (float) slider.valueToProportionOfLength (0.0);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (middle, sliderPos), area.getY(), juce::jmax (middle, sliderPos), area.getBottom()));
        g.setColour (ParagonColours::panelEdge.brighter (0.3f));
        g.drawVerticalLine (juce::roundToInt (middle), area.getY(), area.getBottom());
        return;
    }

    if (style != juce::Slider::TwoValueHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto centreY = (float) y + (float) height * 0.5f;
    const auto left = (float) x + 8.0f, right = (float) (x + width) - 8.0f;

    g.setColour (ParagonColours::panelEdge);
    g.fillRoundedRectangle (left, centreY - 2.0f, right - left, 4.0f, 2.0f);
    g.setColour (ParagonColours::accent);
    g.fillRoundedRectangle (minSliderPos, centreY - 2.0f, maxSliderPos - minSliderPos, 4.0f, 2.0f);

    for (auto position : { minSliderPos, maxSliderPos })
    {
        const auto thumb = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ position, centreY });
        g.setColour (ParagonColours::accent.withAlpha (0.25f));
        g.fillEllipse (thumb.expanded (4.0f));
        g.setColour (ParagonColours::accent);
        g.fillEllipse (thumb);
        g.setColour (ParagonColours::background);
        g.fillEllipse (thumb.reduced (4.5f));
    }
}

juce::Typeface::Ptr ParagonLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() != juce::Font::getDefaultSansSerifFontName())
        return LookAndFeel_V4::getTypefaceForFont (font);

    if (font.isBold() && font.isItalic())
        return blackItalic;
    return font.isBold() ? semiBold : regular;
}

SharedParagonLookAndFeel::SharedParagonLookAndFeel()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    juce::Typeface::clearTypefaceCache();   // fonts looked up before now used the system font
}

SharedParagonLookAndFeel::~SharedParagonLookAndFeel()
{
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lookAndFeel)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    juce::Typeface::clearTypefaceCache();
}

void ParagonLookAndFeel::drawPianoKey (juce::Graphics& g, juce::Rectangle<float> area, float radius, float bevel,
                                       bool pressed, bool highlighted)
{
    if (pressed)
        area = area.translated (0.0f, 1.0f);

    juce::Path outline;
    outline.addRoundedRectangle (area, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (pressed ? 0.45f : 0.6f), juce::roundToInt (bevel * 3.0f),
                      { 0, juce::roundToInt (bevel * (pressed ? 0.6f : 1.4f)) }).drawForPath (g, outline);

    const auto top = area.withTrimmedLeft (bevel).withTrimmedRight (bevel)
                         .withTrimmedTop (bevel * 0.7f).withTrimmedBottom (bevel * (pressed ? 1.1f : 1.6f));
    const auto topRadius = juce::jmax (1.0f, radius - bevel * 0.5f);

    // The chamfer: lit along the top, mid grey down the sides, near black
    // along the front (the edge facing you).
    juce::ColourGradient chamfer (juce::Colour (0xff57565c), 0.0f, area.getY(), juce::Colour (0xff020203), 0.0f, area.getBottom(), false);
    chamfer.addColour (0.18, juce::Colour (0xff2a292e));
    chamfer.addColour (0.7, juce::Colour (0xff111113));
    g.setGradientFill (chamfer);
    g.fillPath (outline);

    // Red glow reflected in the side chamfers near the bottom, as in the mockup.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (outline);
        for (auto x : { area.getX(), area.getRight() - bevel })
        {
            g.setGradientFill (juce::ColourGradient (ParagonColours::glow.withAlpha (0.0f), 0.0f, area.getCentreY(),
                                                     ParagonColours::glow.withAlpha (0.75f), 0.0f, area.getBottom(), false));
            g.fillRect (juce::Rectangle<float> (x, area.getY(), bevel, area.getHeight()));
        }
    }

    // The top: deep glossy black, with a soft reflection of the room across
    // its upper half and a crisp highlight where it meets the top chamfer.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1b20), 0.0f, top.getY(), juce::Colour (0xff060607), 0.0f, top.getBottom(), false));
    g.fillRoundedRectangle (top, topRadius);
    {
        juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.13f), 0.0f, top.getY(),
                                    juce::Colours::white.withAlpha (0.0f), 0.0f, top.getY() + top.getHeight() * 0.55f, false);
        g.setGradientFill (gloss);
        g.fillRoundedRectangle (top.reduced (top.getWidth() * 0.03f, 1.0f).withHeight (top.getHeight() * 0.55f), topRadius);
    }
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawHorizontalLine (juce::roundToInt (top.getY()), top.getX() + topRadius, top.getRight() - topRadius);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (top, topRadius, 0.8f);

    g.setColour (highlighted ? ParagonColours::glow.withAlpha (0.8f) : juce::Colours::black);
    g.drawRoundedRectangle (area, radius, 1.0f);
}

void ParagonLookAndFeel::drawNeonBar (juce::Graphics& g, juce::Rectangle<float> bar, bool lit, bool glow)
{
    // Its thickness is the short side: a bar standing upright (THE DRUM MACHINE's key LEDs) glows as much as a lying one
    // (0.53.1: the glow was sized by the height, so an upright LED had a halo as wide as the key was tall).
    const auto thick = juce::jmin (bar.getWidth(), bar.getHeight());
    const auto upright = bar.getHeight() > bar.getWidth();
    const auto radius = thick * 0.5f;
    const juce::Colour neon { 0xffff3a2a };
    if (! lit)
    {
        g.setColour (neon.withAlpha (0.2f));
        g.fillRoundedRectangle (bar.expanded (1.5f), radius + 1.5f);
        g.setColour (juce::Colour (0xffcf3324));
        g.fillRoundedRectangle (bar, radius);
        return;
    }

    // Glow round the tube, the red-orange outline, then the white-hot core.
    if (glow)
    {
    g.setColour (neon.withAlpha (0.18f));
    const auto along = thick * 1.6f, across = thick * 1.3f;
    g.fillRoundedRectangle (upright ? bar.expanded (across, along) : bar.expanded (along, across), radius * 3.0f);
    g.setColour (neon.withAlpha (0.4f));
    g.fillRoundedRectangle (bar.expanded (thick * 0.6f), radius * 1.6f);
    }
    g.setColour (neon);
    g.fillRoundedRectangle (bar.expanded (1.0f), radius + 1.0f);
    g.setColour (juce::Colour (0xfffff2ee));
    const auto coreAlong = thick * 0.18f, coreAcross = thick * 0.22f;
    g.fillRoundedRectangle (upright ? bar.reduced (coreAcross, coreAlong) : bar.reduced (coreAlong, coreAcross), radius * 0.6f);
}

juce::Rectangle<float> ParagonLookAndFeel::drawChromeBezel (juce::Graphics& g, juce::Rectangle<float> bezel, float radius)
{
    juce::Path outline;
    outline.addRoundedRectangle (bezel, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 4 }).drawForPath (g, outline);

    // Chrome: lit along the top, darker towards the front, with the bright
    // and dark bands of its curved sides.
    if (Finish::neonRings())
    {
        // Pearl black and white + neon: the neon, pink to red to orange
        // across, lit along the top and darker towards the front like chrome.
        g.setGradientFill (Finish::neonGradient (bezel.getX(), bezel.getRight()));
        g.fillPath (outline);
        juce::ColourGradient light (juce::Colours::white.withAlpha (0.45f), 0.0f, bezel.getY(), juce::Colours::black.withAlpha (0.45f), 0.0f, bezel.getBottom(), false);
        light.addColour (0.12, juce::Colours::white.withAlpha (0.12f));
        light.addColour (0.6, juce::Colours::transparentBlack);
        g.setGradientFill (light);
    }
    else
    {
        juce::ColourGradient chrome (juce::Colour (0xfffbf5f8), 0.0f, bezel.getY(), juce::Colour (0xff4a3c43), 0.0f, bezel.getBottom(), false);
        chrome.addColour (0.1, juce::Colour (0xffd6c6ce));
        chrome.addColour (0.55, juce::Colour (0xff998792));
        chrome.addColour (0.82, juce::Colour (0xffc2b0b9));
        g.setGradientFill (chrome);
    }
    g.fillPath (outline);
    juce::ColourGradient sides (juce::Colours::white.withAlpha (0.35f), bezel.getX(), 0.0f,
                                juce::Colours::white.withAlpha (0.35f), bezel.getRight(), 0.0f, false);
    sides.addColour (0.04, juce::Colours::black.withAlpha (0.12f));
    sides.addColour (0.5, juce::Colours::white.withAlpha (0.0f));
    sides.addColour (0.96, juce::Colours::black.withAlpha (0.12f));
    g.setGradientFill (sides);
    g.fillPath (outline);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    // The well the key sits down in, its lower edge catching a little light.
    const auto well = bezel.reduced (4.5f);
    const auto wellRadius = juce::jmax (1.0f, radius - 3.5f);
    g.setColour (juce::Colour (0xff120b0e));
    g.fillRoundedRectangle (well, wellRadius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, well.getCentreY(),
                                             juce::Colours::white.withAlpha (0.25f), 0.0f, well.getBottom(), false));
    g.drawRoundedRectangle (well, wellRadius, 1.0f);
    return well;
}

void ParagonLookAndFeel::drawStampedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                          juce::Justification justification, juce::Colour colour)
{
    g.setColour (Finish::stampEdge());
    g.drawText (text, area.translated (0.0f, 1.0f), justification, false);
    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void ParagonLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    LookAndFeel_V4::positionComboBoxText (box, label);
    label.setMinimumHorizontalScale (0.6f);
}

int ParagonLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getProperties()[eqFaderProperty])
        return eqFaderThumbRadius;
    return LookAndFeel_V4::getSliderThumbRadius (slider);
}
