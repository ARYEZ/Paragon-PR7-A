#include "ui/KitView.h"
#include "NoteNames.h"

#include "KitImages.h"
#include "dsp/synth/NoteMap.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr const char* pieceNames[KitView::numPieces] = { "kick", "snare", "tom1", "tom2", "floor", "hihat",
                                                              "crash", "crash2", "ride", "china", "splash", "tambourine" };

    // Each slot's piece (synth::drums::slotDefaults' order): BD, RIM, SD, CLP, SD2, LT, CH, MT, PH, HT, OH, CY,
    // RD, RB, CR2, CHN, SPL, HH, TMB, SHK, then 0.17.0's RE (the ride's edge), HO1 and HO3 (the hat's openness), then
    // 0.33.0's T4 (no tom 4 in the picture), HSH (the hat), the chokes (their cymbals).
    constexpr int slotPieces[] = { 0, 1, 1, -1, 1, 4, 5, 3, 5, 2, 5, 6, 8, 8, 7, 9, 10, 5, 11, -1, 8, 5, 5,
                                   -1, 5, 6, 7, 9, 10 };
    static_assert (std::size (slotPieces) == synth::drums::numSlots);

    // 0.17.0: the ride's edge, a ring round the outside of its bow in the click map (the renders have bow and bell
    // only): bow pixels further out than 0.84 of the way from its middle to its rim, as the ellipse the cymbal makes
    // from the seat.
    void addRideEdge (juce::Image& zones)
    {
        using namespace synth::drums::notemap;
        if (! zones.isValid())
            return;
        juce::Image::BitmapData pixels (zones, juce::Image::BitmapData::readWrite);
        const auto zoneAt = [&pixels] (int x, int y) { return (int) pixels.getPixelColour (x, y).getRed() - 1; };
        int left = pixels.width, right = -1, top = pixels.height, bottom = -1;
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                if (const auto z = zoneAt (x, y); z == RD || z == RB)
                {
                    left = juce::jmin (left, x);
                    right = juce::jmax (right, x);
                    top = juce::jmin (top, y);
                    bottom = juce::jmax (bottom, y);
                }
        if (right <= left || bottom <= top)
            return;
        const auto cx = 0.5 * (left + right), cy = 0.5 * (top + bottom);
        const auto rx = 0.5 * (right - left + 1), ry = 0.5 * (bottom - top + 1);
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                if (zoneAt (x, y) == RD)
                {
                    const auto dx = (x - cx) / rx, dy = (y - cy) / ry;
                    if (dx * dx + dy * dy > 0.84 * 0.84)
                    {
                        const auto c = pixels.getPixelColour (x, y);
                        pixels.setPixelColour (x, y, juce::Colour ((juce::uint8) (RE + 1), c.getGreen(), c.getBlue(), c.getAlpha()));
                    }
                }
    }

    constexpr float chosenGlow = 0.3f, hoverGlow = 0.18f;

    // The family's colours only: any hue outside 318 to 42 degrees (through red) turned to the nearer end, its
    // saturation and brightness kept. The renders were made that way; this undoes what JPEG moves (a few degrees, and
    // ringing beside bright glints), once, as the pictures load.
    void familyHues (juce::Image& image)
    {
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const auto hue = c.getHue() * 360.0f;
                if (c.getSaturation() <= 0.0f || hue <= 42.0f || hue >= 318.0f)
                    continue;
                const auto to = hue < 180.0f ? 40.0f : 320.0f;
                pixels.setPixelColour (x, y, juce::Colour::fromHSV (to / 360.0f, c.getSaturation(), c.getBrightness(), c.getFloatAlpha()));
            }
    }

    juce::Image load (const char* name)
    {
        int size = 0;
        if (const auto* data = KitImages::getNamedResource (name, size))
            if (auto image = juce::ImageFileFormat::loadFrom (data, (size_t) size); image.isValid())
            {
                image = image.convertedToFormat (image.hasAlphaChannel() ? juce::Image::ARGB : juce::Image::RGB);
                familyHues (image);
                return image;
            }
        return {};
    }
}

const char* KitView::pieceName (int piece) noexcept
{
    return piece >= 0 && piece < numPieces ? pieceNames[piece] : "";
}

int KitView::pieceOf (int slot) noexcept
{
    return slot >= 0 && slot < synth::drums::numSlots ? slotPieces[slot] : -1;
}

const KitView::Pictures& KitView::pictures()
{
    static const Pictures loaded = []
    {
        Pictures p;
        p.kit = load ("kit_jpg");
        p.zones = load ("kitzones_png");
        addRideEdge (p.zones);
        // kitlayout.txt: a line per piece, "<name> <x> <y> <width> <height>", where its glow goes in the kit's pixels.
        int size = 0;
        const auto* text = KitImages::getNamedResource ("kitlayout_txt", size);
        const auto lines = juce::StringArray::fromLines (text != nullptr ? juce::String::fromUTF8 (text, size) : juce::String());
        for (const auto& line : lines)
        {
            const auto words = juce::StringArray::fromTokens (line, false);
            if (words.size() != 5)
                continue;
            for (int k = 0; k < numPieces; ++k)
                if (words[0] == pieceNames[k])
                {
                    p.litArea[(size_t) k] = { words[1].getIntValue(), words[2].getIntValue(), words[3].getIntValue(), words[4].getIntValue() };
                    p.lit[(size_t) k] = load (("lit" + juce::String (pieceNames[k]) + "_png").toRawUTF8());
                }
        }
        return p;
    }();
    return loaded;
}

KitView::KitView (ParagonX1AudioProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
    setOpaque (true);   // it fills itself: what's behind it needn't be drawn when a piece glows (0.15.1)
}

// 0.15.1: the pictures shrunk to the size they're shown, in the screen's own pixels (Windows' display scaling too),
// once, and only copied after that: shrinking the 1280-pixel kit and each glow smoothly on every redraw cost
// milliseconds a frame, 30 frames a second per glowing piece.
void KitView::prepare (float scale)
{
    const auto& pics = pictures();
    const auto area = pictureArea();
    const auto size = (area * scale).getSmallestIntegerContainer();
    if (! pics.kit.isValid() || size.isEmpty() || (juce::exactlyEqual (scale, shownAt.scale) && area == shownAt.area))
        return;
    shownAt.scale = scale;
    shownAt.area = area;
    const auto toScreen = (float) size.getWidth() / (float) pics.kit.getWidth();   // kit pixels -> screen pixels
    shownAt.kit = juce::Image (juce::Image::RGB, size.getWidth(), size.getHeight(), false);
    {
        juce::Graphics g (shownAt.kit);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (pics.kit, shownAt.kit.getBounds().toFloat());
    }
    for (int k = 0; k < numPieces; ++k)
    {
        const auto& lit = pics.lit[(size_t) k];
        if (! lit.isValid())
            continue;
        const auto exact = pics.litArea[(size_t) k].toFloat() * toScreen;
        const auto whole = exact.getSmallestIntegerContainer();
        auto& image = shownAt.lit[(size_t) k];
        image = juce::Image (juce::Image::ARGB, juce::jmax (1, whole.getWidth()), juce::jmax (1, whole.getHeight()), true);
        juce::Graphics g (image);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (lit, exact.translated ((float) -whole.getX(), (float) -whole.getY()));
        shownAt.litAt[(size_t) k] = whole.getPosition();
    }
}

juce::Rectangle<float> KitView::pictureArea() const
{
    const auto& kit = pictures().kit;
    if (! kit.isValid())
        return {};
    return juce::RectanglePlacement (juce::RectanglePlacement::centred)
        .appliedTo (kit.getBounds().toFloat(), getLocalBounds().toFloat());
}

int KitView::slotAt (juce::Point<float> at) const
{
    const auto& zones = pictures().zones;
    const auto area = pictureArea();
    if (! zones.isValid() || area.isEmpty() || ! area.contains (at))
        return -1;
    const auto scale = (float) zones.getWidth() / area.getWidth();
    const auto x = (int) ((at.x - area.getX()) * scale), y = (int) ((at.y - area.getY()) * scale);
    const auto zoneAt = [&zones] (int px, int py)
    {
        if (px < 0 || py < 0 || px >= zones.getWidth() || py >= zones.getHeight())
            return -1;
        return (int) zones.getPixelAt (px, py).getRed() - 1;
    };
    if (const auto exact = zoneAt (x, y); exact >= 0)
        return exact;
    // Thin parts (a hoop, a pedal) are easy to miss: the nearest part within 4 of the kit's pixels.
    auto best = -1;
    auto bestDistance = 1000;
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
            if (const auto z = zoneAt (x + dx, y + dy); z >= 0 && dx * dx + dy * dy < bestDistance)
            {
                best = z;
                bestDistance = dx * dx + dy * dy;
            }
    return best;
}

std::optional<juce::Point<float>> KitView::pointFor (int slot) const
{
    const auto& zones = pictures().zones;
    const auto area = pictureArea();
    if (! zones.isValid() || area.isEmpty())
        return {};
    // The middle of the slot's part: the zone pixel nearest the average of its pixels.
    double sx = 0.0, sy = 0.0;
    int count = 0;
    for (int y = 0; y < zones.getHeight(); ++y)
        for (int x = 0; x < zones.getWidth(); ++x)
            if ((int) zones.getPixelAt (x, y).getRed() - 1 == slot)
            {
                sx += x;
                sy += y;
                ++count;
            }
    if (count == 0)
        return {};
    const juce::Point<double> mean (sx / count, sy / count);
    juce::Point<int> best;
    auto bestDistance = 1.0e12;
    for (int y = 0; y < zones.getHeight(); ++y)
        for (int x = 0; x < zones.getWidth(); ++x)
            if ((int) zones.getPixelAt (x, y).getRed() - 1 == slot)
                if (const auto d = mean.getDistanceSquaredFrom ({ (double) x, (double) y }); d < bestDistance)
                {
                    bestDistance = d;
                    best = { x, y };
                }
    const auto scale = area.getWidth() / (float) zones.getWidth();
    return juce::Point<float> (area.getX() + ((float) best.x + 0.5f) * scale, area.getY() + ((float) best.y + 0.5f) * scale);
}

void KitView::click (juce::Point<float> at, juce::ModifierKeys mods)
{
    const auto slot = slotAt (at);
    if (slot < 0)
        return;
    if (onChoose)
        onChoose (slot, mods);
    processor.previewDrum (slot);
}

void KitView::setSelected (int slot)
{
    if (slot != selected)
    {
        selected = slot;
        repaint();
    }
}

void KitView::updateLights()
{
    const auto& kit = processor.getDrumKit();
    std::array<float, numPieces> now {};
    for (int slot = 0; slot < synth::drums::numSlots; ++slot)
    {
        const auto piece = pieceOf (slot);
        const auto age = kit.getHitAge (slot);
        if (piece >= 0 && age >= 0.0f && kit.isSounding (slot))
            now[(size_t) piece] = juce::jmax (now[(size_t) piece], (1.0f - age) * (1.0f - age));
    }
    for (int k = 0; k < numPieces; ++k)
        if (std::abs (now[(size_t) k] - glow[(size_t) k]) > 0.02f || (now[(size_t) k] <= 0.0f) != (glow[(size_t) k] <= 0.0f))
        {
            glow[(size_t) k] = now[(size_t) k];
            repaint (areaOf (k));   // only that piece
        }
}

juce::Rectangle<int> KitView::areaOf (int piece) const
{
    const auto& pics = pictures();
    const auto area = pictureArea();
    if (! pics.kit.isValid() || area.isEmpty())
        return {};
    const auto scale = area.getWidth() / (float) pics.kit.getWidth();
    const auto at = pics.litArea[(size_t) piece].toFloat();
    return juce::Rectangle<float> (area.getX() + at.getX() * scale, area.getY() + at.getY() * scale, at.getWidth() * scale, at.getHeight() * scale)
        .getSmallestIntegerContainer().expanded (1);
}

void KitView::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    prepare (scale);
    if (! shownAt.kit.isValid())
        return;
    const auto area = shownAt.area;

    // Screen pixels -> the view: copied, not resampled. The context's own scale cancels this one, and the picture's
    // corner sits on a whole screen pixel (between two, every pixel would be smoothed again).
    const auto origin = (area.getTopLeft() * scale).roundToInt();
    const auto place = [&] (juce::Point<int> at)
    {
        return juce::AffineTransform::translation ((float) (origin.x + at.x), (float) (origin.y + at.y)).scaled (1.0f / scale);
    };
    g.drawImageTransformed (shownAt.kit, place ({}));
    for (int k = 0; k < numPieces; ++k)
    {
        auto level = glow[(size_t) k];
        if (pieceOf (selected) == k)
            level = juce::jmax (level, chosenGlow);
        if (pieceOf (hovered) == k)
            level = juce::jmax (level, hoverGlow);
        const auto& lit = shownAt.lit[(size_t) k];
        if (level <= 0.0f || ! lit.isValid())
            continue;
        g.setOpacity (juce::jmin (1.0f, level));
        g.drawImageTransformed (lit, place (shownAt.litAt[(size_t) k]));
    }
    g.setOpacity (1.0f);

    // The picture's edges fade into the panel, so it sits in it without a frame.
    const auto fade = juce::jmin (40.0f, area.getHeight() * 0.08f);
    const auto clear = ParagonColours::background.withAlpha (0.0f);
    const auto edge = [&g, clear] (juce::Rectangle<float> band, juce::Point<float> solid, juce::Point<float> gone)
    {
        g.setGradientFill (juce::ColourGradient (ParagonColours::background, solid, clear, gone, false));
        g.fillRect (band);
    };
    edge (area.withHeight (fade), area.getTopLeft(), area.getTopLeft().translated (0.0f, fade));
    edge (area.withTrimmedTop (area.getHeight() - fade), area.getBottomLeft(), area.getBottomLeft().translated (0.0f, -fade));
    edge (area.withWidth (fade), area.getTopLeft(), area.getTopLeft().translated (fade, 0.0f));
    edge (area.withTrimmedLeft (area.getWidth() - fade), area.getTopRight(), area.getTopRight().translated (-fade, 0.0f));

    // What's under the mouse (or chosen), at the bottom left (only when that line is being drawn).
    if (! g.clipRegionIntersects (getLocalBounds().reduced (14, 10).removeFromBottom (18)))
        return;
    const auto shown = hovered >= 0 ? hovered : selected;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (ParagonColours::text.withAlpha (0.9f));
    g.drawText (describe (shown), getLocalBounds().reduced (14, 10).removeFromBottom (18), juce::Justification::bottomLeft, false);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText ("CLICK A DRUM: CHOOSE IT AND HEAR IT", getLocalBounds().reduced (14, 10).removeFromBottom (18), juce::Justification::bottomRight, false);
}

juce::String KitView::describe (int slot) const
{
    if (slot < 0 || slot >= synth::drums::numSlots)
        return {};
    const auto home = processor.homeNote (slot);
    // 0.17.0: the piece and articulation ("RIDE EDGE"), and if it's switched off, what plays for it.
    const auto kit = processor.getKitSettings();
    const auto playsAs = kit.playsAs (slot);
    return juce::String (synth::drums::slotDefaults[(size_t) slot].key) + "  " + synth::drums::articulationName (slot)
           + (playsAs != slot ? (playsAs >= 0 ? "  (OFF: " + synth::drums::articulationName (playsAs) + " PLAYS)" : juce::String ("  (OFF)")) : juce::String())
           + (home >= 0 ? "  " + notenames::name (home) : juce::String());
}

void KitView::mouseDown (const juce::MouseEvent& e)
{
    click (e.position, e.mods);
}

void KitView::mouseMove (const juce::MouseEvent& e)
{
    const auto slot = slotAt (e.position);
    if (slot != hovered)
    {
        hovered = slot;
        setMouseCursor (slot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        setTooltip (slot >= 0 ? describe (slot) : juce::String());
        repaint();
    }
}

void KitView::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}
