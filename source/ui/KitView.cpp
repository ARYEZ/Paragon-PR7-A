#include "ui/KitView.h"
#include "NoteNames.h"

#include "KitImages.h"
#include "dsp/synth/NoteMap.h"
#include "ui/Logo.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    // 0.43.0: the owner's 12-piece kit (the pieces their sample pack records: three rack toms, a floor tom, the hi-hat,
    // two crashes, the ride, a china, a splash).
    constexpr const char* pieceNames[KitView::numPieces] = { "kick", "snare", "tom1", "tom2", "tom3", "floor", "hihat",
                                                              "crash", "crash2", "ride", "china", "splash" };

    // Each slot's piece (synth::drums::slotDefaults' order): BD, RIM, SD, CLP, SD2, LT, CH, MT, PH, HT, OH, CY,
    // RD, RB, CR2, CHN, SPL, HH, TMB, SHK, then 0.17.0's RE (the ride's edge), HO1 and HO3 (the hat's openness), then
    // 0.33.0's T4, HSH (the hat), the chokes (their cymbals), then 0.36.0's C3-C6, CFX, CH2, T5. 0.37.0: the rack toms
    // HT, MT, LT left to right, the floor tom T4; the clap, tambourine and shaker aren't in the picture. 0.43.0: nor
    // are C3-C6, CFX, CH2 and T5 (they play their stand-ins: the owner's pack has none of them).
    constexpr int slotPieces[] = { 0, 1, 1, -1, 1, 4, 6, 3, 6, 2, 6, 7, 9, 9, 8, 10, 11, 6, -1, -1, 9, 6, 6,
                                   5, 6, 7, 8, 10, 11,
                                   -1, -1, -1, -1, -1, -1, -1 };
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

namespace
{
    // 0.46.0: each piece cut from the kit by the click map (its pixels, the rest clear) and its outline (the edge of
    // those pixels, 3 of the kit's pixels wide), in the kit's pixels, for EDIT PLAY STYLE's highlight. Made once.
    struct PieceCut
    {
        juce::Rectangle<int> box;
        juce::Image cut, outline;
    };
    const std::array<PieceCut, KitView::numPieces>& pieceCuts()
    {
        static const auto made = []
        {
            std::array<PieceCut, KitView::numPieces> cuts;
            const auto& pics = KitView::pictures();
            if (! pics.kit.isValid() || ! pics.zones.isValid() || pics.zones.getBounds() != pics.kit.getBounds())
                return cuts;
            const auto w = pics.zones.getWidth(), h = pics.zones.getHeight();
            std::vector<std::int8_t> owner ((size_t) (w * h), -1);
            {
                const juce::Image::BitmapData zones (pics.zones, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        owner[(size_t) (y * w + x)] = (std::int8_t) KitView::pieceOf ((int) zones.getPixelColour (x, y).getRed() - 1);
            }
            const auto at = [&] (int x, int y) { return x < 0 || y < 0 || x >= w || y >= h ? -1 : (int) owner[(size_t) (y * w + x)]; };
            for (int k = 0; k < KitView::numPieces; ++k)
            {
                int left = w, right = -1, top = h, bottom = -1;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (at (x, y) == k)
                        {
                            left = juce::jmin (left, x); right = juce::jmax (right, x);
                            top = juce::jmin (top, y); bottom = juce::jmax (bottom, y);
                        }
                if (right < left)
                    continue;
                constexpr int pad = 4;
                auto& c = cuts[(size_t) k];
                c.box = juce::Rectangle<int>::leftTopRightBottom (left - pad, top - pad, right + pad + 1, bottom + pad + 1)
                            .getIntersection ({ 0, 0, w, h });
                c.cut = juce::Image (juce::Image::ARGB, c.box.getWidth(), c.box.getHeight(), true);
                c.outline = juce::Image (juce::Image::ARGB, c.box.getWidth(), c.box.getHeight(), true);
                const juce::Image::BitmapData kit (pics.kit, juce::Image::BitmapData::readOnly);
                juce::Image::BitmapData cut (c.cut, juce::Image::BitmapData::writeOnly), line (c.outline, juce::Image::BitmapData::writeOnly);
                for (int y = c.box.getY(); y < c.box.getBottom(); ++y)
                    for (int x = c.box.getX(); x < c.box.getRight(); ++x)
                    {
                        const auto inside = at (x, y) == k;
                        if (inside)
                            cut.setPixelColour (x - c.box.getX(), y - c.box.getY(), kit.getPixelColour (x, y));
                        // The outline: outside the piece, within 3 pixels of it (so it rings the piece, never covers it).
                        if (! inside)
                        {
                            auto nearest = 99;
                            for (int dy = -3; dy <= 3; ++dy)
                                for (int dx = -3; dx <= 3; ++dx)
                                    if (at (x + dx, y + dy) == k)
                                        nearest = juce::jmin (nearest, dx * dx + dy * dy);
                            if (nearest <= 9)
                                line.setPixelColour (x - c.box.getX(), y - c.box.getY(),
                                                     juce::Colours::white.withAlpha (nearest <= 4 ? 1.0f : 0.5f));
                        }
                    }
            }
            return cuts;
        }();
        return made;
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

namespace
{
    namespace holo
    {
        // 0.60.0: THE GLITCH. Worked out from the time alone (the same moment always looks the same: tests, redraws).
        //   - every half second, one time in four, a hologram breaks up for 0.14 s: 1-3 strips of it jump sideways
        //     6-18% of its width (new strips every 35 ms), a faint copy of it shows beside it, and it flickers;
        //   - every 4 s a brighter band rolls down through it over 0.9 s;
        //   - each side on its own timing.
        inline std::uint32_t mix (std::uint32_t x) noexcept
        {
            x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
            return x;
        }
        struct Band { float top, height, shift; };   // fractions of the icon's height, shift of its width
        struct Glitch
        {
            int key = -1;              // which break-up frame (-1: none)
            std::array<Band, 3> bands {};
            int numBands = 0;
            float flicker = 1.0f;
            float ghost = 0.0f;        // a faint copy of the whole icon this far aside (a fraction of its width)
            int rollStep = -1;         // the rolling band's step (-1: none)
            float rollTop = 0.0f;      // its top, a fraction of the icon's height
        };
        inline Glitch glitchAt (int side, double t)
        {
            Glitch g;
            const auto slot = (std::int64_t) std::floor (t / 0.5);
            const auto h = mix ((std::uint32_t) (slot * 2 + side) * 2654435761u + 12345u);
            if (h % 4u == 0u)
            {
                const auto start = (double) slot * 0.5 + (double) ((h >> 8) % 1000u) / 1000.0 * 0.35;
                if (t >= start && t < start + 0.14)
                {
                    const auto frame = (int) ((t - start) / 0.035);
                    g.key = (int) ((slot & 0xffffff) * 8 + frame);
                    auto r = mix (h + (std::uint32_t) frame * 0x9e3779b9u);
                    g.numBands = 1 + (int) (r % 3u);
                    for (int b = 0; b < g.numBands; ++b)
                    {
                        r = mix (r + 0x68e31da4u);
                        const auto bandHeight = 0.05f + (float) (r % 100u) / 100.0f * 0.13f;
                        const auto top = (float) ((r >> 8) % 1000u) / 1000.0f * (1.0f - bandHeight);
                        const auto shift = (0.06f + (float) ((r >> 18) % 100u) / 100.0f * 0.12f) * ((r >> 30) & 1u ? 1.0f : -1.0f);
                        g.bands[(size_t) b] = { top, bandHeight, shift };
                    }
                    g.flicker = frame % 2 == 0 ? 0.55f : 1.0f;
                    g.ghost = (((r >> 7) & 1u) ? 1.0f : -1.0f) * (0.04f + (float) ((r >> 11) % 100u) / 100.0f * 0.05f);
                }
            }
            const auto roll = std::fmod (t + side * 1.7, 4.0) / 0.9;
            if (roll < 1.0)
            {
                g.rollStep = (int) (roll * 27.0);
                g.rollTop = (float) (-0.18 + 1.18 * ((double) g.rollStep / 27.0));
            }
            return g;
        }

    }
}

KitView::KitView (ParagonX1AudioProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
    setOpaque (true);   // it fills itself: what's behind it needn't be drawn when a piece glows (0.15.1)
}

// 0.15.1: the pictures shrunk to the size they're shown, in the screen's own pixels (Windows' display scaling too),
// once, and only copied after that: shrinking the kit (1896 pixels wide since 0.42.0) and each glow smoothly on every redraw cost
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
    prepareHolograms (toScreen);
}

juce::Rectangle<float> KitView::pictureArea() const
{
    const auto& kit = pictures().kit;
    if (! kit.isValid())
        return {};
    // On whole pixels, its sides multiples of 4 (0.42.0: the wide picture rarely fits a view exactly; half a pixel off,
    // or half a screen pixel too big at 125-200% display scaling, it would be drawn snapped or stretched, off its click
    // map).
    const auto placed = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                            .appliedTo (kit.getBounds().toFloat(), getLocalBounds().toFloat());
    const auto w = juce::jmax (4, (int) (placed.getWidth() / 4.0f) * 4), h = juce::jmax (4, (int) (placed.getHeight() / 4.0f) * 4);
    return juce::Rectangle<int> (w, h).withCentre (placed.getCentre().toInt()).toFloat();
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

void KitView::setHighlight (const Highlight& h)
{
    if (h == highlight)
        return;
    highlight = h;
    repaint();
}

std::optional<juce::Point<float>> KitView::pieceCentre (int piece) const
{
    if (piece < 0 || piece >= numPieces)
        return {};
    const auto& c = pieceCuts()[(size_t) piece];
    const auto area = pictureArea();
    const auto& kit = pictures().kit;
    if (c.box.isEmpty() || area.isEmpty() || ! kit.isValid())
        return {};
    const auto scale = area.getWidth() / (float) kit.getWidth();
    return area.getTopLeft() + c.box.toFloat().getCentre() * scale;
}

std::optional<juce::Point<float>> KitView::slotCentre (int slot) const
{
    // Each slot's part's middle (the average of its pixels in the click map), worked out once for all of them.
    static const auto centres = []
    {
        std::array<juce::Point<float>, synth::drums::numSlots> out {};
        const auto& zones = pictures().zones;
        if (! zones.isValid())
            return out;
        // (Only the top half of each part: a hi-hat's part runs down its stand, and the flag belongs on the hat.)
        std::array<double, synth::drums::numSlots> sx {}, sy {}, n {};
        std::array<int, synth::drums::numSlots> top {}, bottom {};
        top.fill (1 << 30);
        bottom.fill (-1);
        const juce::Image::BitmapData pixels (zones, juce::Image::BitmapData::readOnly);
        for (int pass = 0; pass < 2; ++pass)
            for (int y = 0; y < pixels.height; ++y)
                for (int x = 0; x < pixels.width; ++x)
                    if (const auto z = (int) pixels.getPixelColour (x, y).getRed() - 1; z >= 0 && z < synth::drums::numSlots)
                    {
                        if (pass == 0)
                        {
                            top[(size_t) z] = juce::jmin (top[(size_t) z], y);
                            bottom[(size_t) z] = juce::jmax (bottom[(size_t) z], y);
                        }
                        else if (y <= (top[(size_t) z] + bottom[(size_t) z]) / 2)
                        {
                            sx[(size_t) z] += x;
                            sy[(size_t) z] += y;
                            n[(size_t) z] += 1.0;
                        }
                    }
        for (size_t k = 0; k < out.size(); ++k)
            out[k] = n[k] > 0.0 ? juce::Point<float> ((float) (sx[k] / n[k]), (float) (sy[k] / n[k])) : juce::Point<float> (-1.0f, -1.0f);
        return out;
    }();
    const auto area = pictureArea();
    const auto& kit = pictures().kit;
    if (slot < 0 || slot >= synth::drums::numSlots || centres[(size_t) slot].x < 0.0f || area.isEmpty() || ! kit.isValid())
        return {};
    return area.getTopLeft() + centres[(size_t) slot] * (area.getWidth() / (float) kit.getWidth());
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

    // The holograms: brighter for a moment with each kick (half as bright again, fading over a third of a second), the scanlines a line further up
    // every quarter second, the glitch and the rolling band (0.60.0). Repainted only when something changes.
    if (highlight.on)
        return;
    auto kick = 0.0f;
    for (int slot = 0; slot < synth::drums::numSlots; ++slot)
        if (pieceOf (slot) == 0)
            if (const auto age = kit.getHitAge (slot); age >= 0.0f && age < 0.35f)
                kick = juce::jmax (kick, (1.0f - age / 0.35f) * (1.0f - age / 0.35f));
    const auto level = restLevel + 0.45f * kick;
    holoTime = fixedHoloTime >= 0.0 ? fixedHoloTime : juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto phase = (int) (std::fmod (holoTime * 4.0, 4.0));
    // (In steps of 0.06: about seven redraws a kick, not one every frame.)
    const auto brighter = std::abs (level - holoLevel) > 0.06f || (juce::exactlyEqual (level, restLevel) && ! juce::exactlyEqual (holoLevel, restLevel));
    if (brighter)
        holoLevel = level;
    const auto moved = phase != scanPhase;
    scanPhase = phase;
    for (int side = 0; side < numHolograms; ++side)
    {
        // 0.60.0: the glitch and the rolling band redraw the icon (and its strips' room to the sides) while they last.
        const auto glitch = holo::glitchAt (side, holoTime);
        const auto state = glitch.key * 64 + glitch.rollStep;
        const auto changed = state != shownGlitch[(size_t) side];
        shownGlitch[(size_t) side] = state;
        if (brighter)
            repaint (hologramPulseArea (side).expanded (hologramIconArea (side).getWidth() / 4, 0));
        else if (moved || changed)
            repaint (hologramIconArea (side).expanded (hologramIconArea (side).getWidth() / 4, 1));
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
    if (highlight.on)
    {
        // EDIT PLAY STYLE: the kit dimmed, the pieces the block plays drawn again over it, each outlined (the chosen ones
        // brighter, glowing): 0.49.0 drawn once into a picture at the screen's pixels (`highlightShown`), then only
        // copied, with the glows of hits on top. It was drawn afresh, the pieces resampled, on every repaint (each hit's
        // glow, every frame while the song plays): about 20 ms each time.
        if (highlightShown.isNull() || ! (highlightFor == highlight) || ! juce::exactlyEqual (highlightScale, scale)
            || highlightArea != area)
        {
            highlightFor = highlight;
            highlightScale = scale;
            highlightArea = area;
            highlightShown = shownAt.kit.createCopy();
            juce::Graphics h (highlightShown);
            const auto& cuts = pieceCuts();
            const auto toScreen = (float) highlightShown.getWidth() / (float) pictures().kit.getWidth();
            const auto boxOf = [&] (int k) { return cuts[(size_t) k].box.toFloat() * toScreen; };
            h.setColour (juce::Colours::black.withAlpha (0.72f));
            h.fillAll();
            h.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            for (int k = 0; k < numPieces; ++k)
            {
                const auto& c = cuts[(size_t) k];
                if (c.cut.isNull() || ! (highlight.used[(size_t) k] || highlight.chosen[(size_t) k]))
                    continue;
                h.setOpacity (highlight.used[(size_t) k] ? 1.0f : 0.55f);
                h.drawImage (c.cut, boxOf (k), juce::RectanglePlacement::stretchToFit);
                h.setOpacity (1.0f);
            }
            for (int k = 0; k < numPieces; ++k)
            {
                const auto& c = cuts[(size_t) k];
                const auto chosen = highlight.chosen[(size_t) k];
                if (c.outline.isNull() || (! chosen && ! highlight.used[(size_t) k]))
                    continue;
                if (chosen && shownAt.lit[(size_t) k].isValid())
                {
                    h.setOpacity (0.85f);
                    h.drawImageAt (shownAt.lit[(size_t) k], shownAt.litAt[(size_t) k].x, shownAt.litAt[(size_t) k].y);
                    h.setOpacity (1.0f);
                }
                h.setColour (chosen ? ParagonColours::accent.brighter (0.3f) : ParagonColours::accent.withAlpha (0.45f));
                h.drawImage (c.outline, boxOf (k), juce::RectanglePlacement::stretchToFit, true);
            }
        }
        g.drawImageTransformed (highlightShown, place ({}));
        for (int k = 0; k < numPieces; ++k)
            if (glow[(size_t) k] > 0.0f && shownAt.lit[(size_t) k].isValid())
            {
                g.setOpacity (juce::jmin (1.0f, glow[(size_t) k]));
                g.drawImageTransformed (shownAt.lit[(size_t) k], place (shownAt.litAt[(size_t) k]));
            }
        g.setOpacity (1.0f);
    }
    else
        g.drawImageTransformed (shownAt.kit, place ({}));
    for (int k = 0; ! highlight.on && k < numPieces; ++k)
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
    if (! highlight.on && hologramsShown)
        paintHolograms (g, place, scale);


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
    if (highlight.on || ! g.clipRegionIntersects (getLocalBounds().reduced (14, 10).removeFromBottom (18)))
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
    if (highlight.on)
    {
        const auto slot = slotAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu (slot, e.position);
        }
        else if (slot >= 0 && onChoose)
            onChoose (slot, e.mods);
        return;
    }
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

// ---------------------------------------------------------------------------------------------------------------------
// THE HOLOGRAMS (0.57.0). Placed in the kit's picture through the camera it was rendered with (kit3d.py's `panel`
// camera, fitted to the render's floor grid: 0.4 m squares), so the rings lie flat on the floor and the icons stand at
// the size they would at that distance: each a point on the empty floor left and right of the kit (render pixels
// (245, 380) and (1655, 385) on the floor), a ring 0.2 m round that, the icon 0.56 m tall with its foot 0.22 m above
// the floor (its top clear of the picture's faded top edge), facing the seat. Our own design, mock-up B as the owner chose.
namespace
{
    namespace holo
    {
        struct V3 { double x, y, z; };
        V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        V3 operator* (V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
        double dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        V3 unit (V3 a) { return a * (1.0 / std::sqrt (dot (a, a))); }

        constexpr double width = 1896.0, height = 744.0;   // the render's pixels
        struct Camera
        {
            V3 at { 0.0, 12.2, 9.741 };
            V3 forward, right, up;
            double focal = width * 134.39 / 36.0;   // lens 134.39 mm on a 36 mm sensor
            Camera()
            {
                forward = unit (V3 { 0.0, 0.2, 0.72 } - at);
                right = unit (cross (forward, { 0.0, 0.0, 1.0 }));
                up = cross (right, forward);
            }
            // A point -> render pixels, and its depth.
            juce::Point<double> project (V3 p, double* depth = nullptr) const
            {
                const auto d = p - at;
                const auto z = dot (d, forward);
                if (depth != nullptr)
                    *depth = z;
                return { width / 2 + focal * dot (d, right) / z, height / 2 - focal * dot (d, up) / z };
            }
            // A render pixel -> the floor point under it.
            V3 floorAt (double px, double py) const
            {
                const auto d = forward + right * ((px - width / 2) / focal) - up * ((py - height / 2) / focal);
                return at + d * (-at.z / d.z);
            }
        };
        const Camera& camera() { static const Camera c; return c; }

        // (0.60.0: the ring two thirds the size it was, 0.2 m; the beam's foot with it.)
        constexpr double ringRadius = 0.133, ringWidth = 0.013, iconHeight = 0.56, lift = 0.22, beamBase = 0.27;
        const juce::Point<double> floorPixel[KitView::numHolograms] = { { 245.0, 380.0 }, { 1655.0, 385.0 } };
        // Each side's colour: the kit's glow there (pink by the hi-hat, orange by the ride).
        const juce::Colour tint[KitView::numHolograms] = { juce::Colour (0xffff2d6e), juce::Colour (0xffff6a24) };
        constexpr double glowNear = 11.8, glowFar = 39.2;   // the glow's two blurs, in render pixels

        struct Place
        {
            V3 floor;
            juce::Point<double> ring, foot;   // render pixels
            double iconPx, beamTopPx, beamBottomPx;
            juce::Rectangle<double> icon, beam, ringBox, all;
        };
        Place placeOf (int side)
        {
            const auto& cam = camera();
            Place p;
            p.floor = cam.floorAt (floorPixel[side].x, floorPixel[side].y);
            double zb = 1.0, zt = 1.0;
            p.ring = cam.project (p.floor, &zb);
            p.foot = cam.project (p.floor + V3 { 0.0, 0.0, lift }, &zt);
            p.iconPx = cam.focal * iconHeight / zt;
            p.beamTopPx = cam.focal * iconHeight * 0.85 / zt;
            p.beamBottomPx = cam.focal * beamBase / zb;
            p.icon = { p.foot.x - p.iconPx / 2, p.foot.y - p.iconPx, p.iconPx, p.iconPx };
            const auto half = std::max (p.beamTopPx, p.beamBottomPx) / 2;
            p.beam = juce::Rectangle<double>::leftTopRightBottom (std::min (p.foot.x, p.ring.x) - half, p.foot.y,
                                                                  std::max (p.foot.x, p.ring.x) + half, p.ring.y);
            // (Points' corners, not unions of empty rectangles: JUCE's union ignores an empty one.)
            double left = 1.0e9, top = 1.0e9, right = -1.0e9, bottom = -1.0e9;
            for (int k = 0; k < 64; ++k)
            {
                const auto a = juce::MathConstants<double>::twoPi * k / 64;
                const auto q = cam.project (p.floor + V3 { std::cos (a), std::sin (a), 0.0 } * (ringRadius + 3 * ringWidth));
                left = std::min (left, q.x);
                right = std::max (right, q.x);
                top = std::min (top, q.y);
                bottom = std::max (bottom, q.y);
            }
            p.ringBox = juce::Rectangle<double>::leftTopRightBottom (left, top, right, bottom);
            p.all = p.icon.getUnion (p.beam).getUnion (p.ringBox).expanded (2.6 * glowFar)
                        .getIntersection ({ 0.0, 0.0, width, height });
            return p;
        }

        // The beam's light at a render pixel (0 to 1): a cone from the ring up to the icon's foot, brightest low down.
        double beamAt (const Place& p, double x, double y)
        {
            if (y < p.foot.y || y > p.ring.y)
                return 0.0;
            const auto frac = (y - p.foot.y) / std::max (1.0, p.ring.y - p.foot.y);   // 0 at the top, 1 at the ring
            const auto half = 0.5 * (p.beamTopPx * (1 - frac) + p.beamBottomPx * frac);
            const auto centre = p.foot.x * (1 - frac) + p.ring.x * frac;
            const auto across = juce::jlimit (0.0, 1.0, 1.0 - std::abs (x - centre) / std::max (1.0, half));
            // It fades out over its last fifth, into the ring's middle (cut off there it left a straight edge).
            const auto end = juce::jlimit (0.0, 1.0, (1.0 - frac) / 0.2);
            return std::pow (across, 0.8) * (0.25 + 0.75 * frac) * end * end * (3.0 - 2.0 * end) * 0.75;
        }

        // The ring's light at a render pixel: a soft line on the floor 0.2 m round the point, a faint fill inside.
        double ringAt (const Place& p, double x, double y)
        {
            const auto q = camera().floorAt (x, y);
            const auto d = std::hypot (q.x - p.floor.x, q.y - p.floor.y);
            const auto off = (d - ringRadius) / ringWidth;
            return (std::exp (-off * off) + 0.25 * juce::jlimit (0.0, 1.0, 1.0 - d / ringRadius)) * 0.7;
        }

        // RGB light, a float per channel.
        struct Light
        {
            int w = 0, h = 0;
            std::vector<float> rgb;
            Light (int width_, int height_) : w (width_), h (height_), rgb ((size_t) (width_ * height_ * 3), 0.0f) {}
            float* at (int x, int y) { return rgb.data() + ((size_t) y * (size_t) w + (size_t) x) * 3; }
        };

        // A gaussian blur (three box blurs each way), outside the area dark.
        Light blurred (const Light& in, double sigma)
        {
            Light out = in;
            const auto r = std::max (1, (int) std::lround ((std::sqrt (4.0 * sigma * sigma + 1.0) - 1.0) / 2.0));
            std::vector<float> line, sums;
            const auto pass = [&] (bool across)
            {
                const auto n = across ? out.w : out.h, lines = across ? out.h : out.w;
                line.resize ((size_t) n * 3);
                for (int l = 0; l < lines; ++l)
                {
                    const auto px = [&] (int i) { return across ? out.at (i, l) : out.at (l, i); };
                    for (int i = 0; i < n; ++i)
                        std::copy_n (px (i), 3, line.data() + (size_t) i * 3);
                    for (int c = 0; c < 3; ++c)
                    {
                        double sum = 0.0;
                        for (int i = 0; i <= std::min (r, n - 1); ++i)
                            sum += line[(size_t) i * 3 + (size_t) c];
                        for (int i = 0; i < n; ++i)
                        {
                            px (i)[c] = (float) (sum / (2 * r + 1));
                            if (i + r + 1 < n)
                                sum += line[(size_t) (i + r + 1) * 3 + (size_t) c];
                            if (i - r >= 0)
                                sum -= line[(size_t) (i - r) * 3 + (size_t) c];
                        }
                    }
                }
            };
            for (int k = 0; k < 3; ++k)
            {
                pass (true);
                pass (false);
            }
            return out;
        }

        // Light -> a premultiplied image to draw over the kit (its alpha the brightest channel: on the dark floor
        // almost exactly light added). Too bright, a colour is scaled down whole, so orange stays orange (clipping each
        // channel would turn it yellow).
        juce::Image imageOf (Light& light)
        {
            juce::Image image (juce::Image::ARGB, std::max (1, light.w), std::max (1, light.h), true);
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < light.h; ++y)
                for (int x = 0; x < light.w; ++x)
                {
                    const auto* c = light.at (x, y);
                    auto top = std::max ({ c[0], c[1], c[2] });
                    const auto k = top > 1.0f ? 1.0f / top : 1.0f;
                    top = std::min (1.0f, top);
                    const auto byte = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) std::lround (v * 255.0f)); };
                    auto* p = reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y));
                    p->setARGB (byte (top), byte (c[0] * k), byte (c[1] * k), byte (c[2] * k));
                }
            return image;
        }

        // The part of an image with anything in it.
        juce::Rectangle<int> visibleArea (const juce::Image& image)
        {
            const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
            int left = image.getWidth(), top = image.getHeight(), right = 0, bottom = 0;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    if (reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y))->getAlpha() > 0)
                    {
                        left = std::min (left, x);
                        right = std::max (right, x + 1);
                        top = std::min (top, y);
                        bottom = std::max (bottom, y + 1);
                    }
            return right > left ? juce::Rectangle<int>::leftTopRightBottom (left, top, right, bottom) : juce::Rectangle<int> (1, 1);
        }

        // The icon's colours pulled towards the side's (mock-up B: 55% its own, 45% the tint at its brightness), and
        // `gain` (they're drawn into an image and can't be brighter than white there).
        juce::Colour tinted (juce::Colour c, juce::Colour t, float k, float gain)
        {
            const auto r = c.getFloatRed() * k, g = c.getFloatGreen() * k, b = c.getFloatBlue() * k;
            const auto top = std::max ({ r, g, b });
            return juce::Colour::fromFloatRGBA ((r * 0.55f + top * t.getFloatRed() * 0.45f) / gain,
                                                (g * 0.55f + top * t.getFloatGreen() * 0.45f) / gain,
                                                (b * 0.55f + top * t.getFloatBlue() * 0.45f) / gain, 1.0f);
        }
    }
}

void KitView::prepareHolograms (float toScreen)
{
    const auto red = juce::Colour (0xffff2a2e), orange = juce::Colour (0xffff6a24), white = juce::Colour (0xfffff0ec);
    constexpr float gain = 1.25f;   // the icon drawn at 1 / gain, its light brought back up after
    constexpr int coarse = 4;       // the glow worked out on a grid of 4 x 4 screen pixels (it's soft), then smoothed up
    for (int side = 0; side < numHolograms; ++side)
    {
        const auto p = holo::placeOf (side);
        const auto tint = holo::tint[side];
        const auto toPixels = [toScreen] (juce::Rectangle<double> r) { return (r * (double) toScreen).getSmallestIntegerContainer(); };

        // The icon and the beam, in screen pixels.
        const auto iconBox = toPixels (p.icon.getUnion (p.beam));
        juce::Image drawn (juce::Image::ARGB, std::max (1, iconBox.getWidth()), std::max (1, iconBox.getHeight()), true);
        {
            juce::Graphics g (drawn);
            Logo::drawIcon (g, (p.icon * (double) toScreen).toFloat().translated ((float) -iconBox.getX(), (float) -iconBox.getY()),
                            { holo::tinted (red, tint, 1.1f, gain), holo::tinted (orange, tint, 1.1f, gain),
                              holo::tinted (white, tint, 0.95f, gain), holo::tinted (red, tint, 1.0f, gain) });
        }
        holo::Light icon (drawn.getWidth(), drawn.getHeight());
        {
            const juce::Image::BitmapData data (drawn, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < icon.h; ++y)
                for (int x = 0; x < icon.w; ++x)
                {
                    const auto c = reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y));
                    const auto rx = (iconBox.getX() + x + 0.5) / toScreen, ry = (iconBox.getY() + y + 0.5) / toScreen;
                    const auto beam = (float) holo::beamAt (p, rx, ry);
                    auto* out = icon.at (x, y);
                    out[0] = c->getRed() / 255.0f * gain * 1.05f + beam * tint.getFloatRed();
                    out[1] = c->getGreen() / 255.0f * gain * 1.05f + beam * tint.getFloatGreen();
                    out[2] = c->getBlue() / 255.0f * gain * 1.05f + beam * tint.getFloatBlue();
                }
        }

        // The ring.
        const auto ringBox = toPixels (p.ringBox);
        holo::Light ring (std::max (1, ringBox.getWidth()), std::max (1, ringBox.getHeight()));
        for (int y = 0; y < ring.h; ++y)
            for (int x = 0; x < ring.w; ++x)
            {
                const auto level = (float) holo::ringAt (p, (ringBox.getX() + x + 0.5) / toScreen, (ringBox.getY() + y + 0.5) / toScreen);
                auto* r = ring.at (x, y);
                r[0] = level * tint.getFloatRed();
                r[1] = level * tint.getFloatGreen();
                r[2] = level * tint.getFloatBlue();
            }

        // The glow of both (the icon at its scanlines' mean, 0.8): averaged onto the coarse grid, blurred there, then
        // smoothed back up to screen pixels.
        const auto box = toPixels (p.all);
        const auto cw = (box.getWidth() + coarse - 1) / coarse, ch = (box.getHeight() + coarse - 1) / coarse;
        holo::Light source (cw, ch);
        const auto gather = [&] (holo::Light& light, juce::Rectangle<int> at, float k)
        {
            for (int y = 0; y < light.h; ++y)
                for (int x = 0; x < light.w; ++x)
                {
                    const auto gx = (at.getX() + x - box.getX()) / coarse, gy = (at.getY() + y - box.getY()) / coarse;
                    if (gx < 0 || gy < 0 || gx >= cw || gy >= ch)
                        continue;
                    for (int c = 0; c < 3; ++c)
                        source.at (gx, gy)[c] += k * light.at (x, y)[c] / (float) (coarse * coarse);
                }
        };
        gather (icon, iconBox, 0.8f);
        gather (ring, ringBox, 1.0f);
        const auto nearGlow = holo::blurred (source, holo::glowNear * toScreen / coarse);
        const auto farGlow = holo::blurred (source, holo::glowFar * toScreen / coarse);
        holo::Light glowLight (box.getWidth(), box.getHeight());
        for (int y = 0; y < glowLight.h; ++y)
        {
            const auto fy = juce::jlimit (0.0f, (float) (ch - 1), (y + 0.5f) / coarse - 0.5f);
            const auto y0 = (int) fy, y1 = std::min (y0 + 1, ch - 1);
            const auto ty = fy - (float) y0;
            for (int x = 0; x < glowLight.w; ++x)
            {
                const auto fx = juce::jlimit (0.0f, (float) (cw - 1), (x + 0.5f) / coarse - 0.5f);
                const auto x0 = (int) fx, x1 = std::min (x0 + 1, cw - 1);
                const auto tx = fx - (float) x0;
                for (int c = 0; c < 3; ++c)
                {
                    const auto at = [&] (int gx, int gy)
                    {
                        return 0.9f * nearGlow.rgb[((size_t) gy * (size_t) cw + (size_t) gx) * 3 + (size_t) c]
                               + 0.5f * farGlow.rgb[((size_t) gy * (size_t) cw + (size_t) gx) * 3 + (size_t) c];
                    };
                    glowLight.at (x, y)[c] = (at (x0, y0) * (1 - tx) + at (x1, y0) * tx) * (1 - ty)
                                        + (at (x0, y1) * (1 - tx) + at (x1, y1) * tx) * ty;
                }
            }
        }

        auto& h = holograms[(size_t) side];
        h.glow = holo::imageOf (glowLight);
        // Only as big as it shows (its far edges are darker than a step of 255: no need to draw them).
        const auto shows = holo::visibleArea (h.glow);
        h.glow = h.glow.getClippedImage (shows);
        h.glowAt = box.getPosition() + shows.getPosition();
        h.ring = holo::imageOf (ring);
        h.ringAt = ringBox.getPosition();
        h.icon = holo::imageOf (icon);
        h.iconAt = iconBox.getPosition();
    }
}

juce::Rectangle<int> KitView::hologramArea (int side) const
{
    const auto area = pictureArea();
    if (area.isEmpty() || side < 0 || side >= numHolograms)
        return {};
    const auto k = area.getWidth() / holo::width;
    return (holo::placeOf (side).all * (double) k).toFloat().translated (area.getX(), area.getY()).getSmallestIntegerContainer().expanded (1);
}

bool KitView::isGlitching (int side) const { return holo::glitchAt (side, holoTime).key >= 0; }
bool KitView::isRolling (int side) const { return holo::glitchAt (side, holoTime).rollStep >= 0; }

juce::Rectangle<int> KitView::hologramPulseArea (int side) const
{
    const auto area = pictureArea();
    if (area.isEmpty() || side < 0 || side >= numHolograms)
        return {};
    const auto p = holo::placeOf (side);
    const auto k = area.getWidth() / holo::width;
    return (p.icon.getUnion (p.beam).getUnion (p.ringBox) * (double) k).toFloat().translated (area.getX(), area.getY())
        .getSmallestIntegerContainer().expanded (2);
}

juce::Rectangle<int> KitView::hologramIconArea (int side) const
{
    const auto area = pictureArea();
    if (area.isEmpty() || side < 0 || side >= numHolograms)
        return {};
    const auto k = area.getWidth() / holo::width;
    return (holo::placeOf (side).icon * (double) k).toFloat().translated (area.getX(), area.getY()).getSmallestIntegerContainer();
}

void KitView::paintHolograms (juce::Graphics& g, std::function<juce::AffineTransform (juce::Point<int>)> place, float scale)
{
    for (const auto& h : holograms)
    {
        if (! h.glow.isValid() || ! h.ring.isValid() || ! h.icon.isValid())
            continue;
        // Brighter than the pictures hold on a kick: drawn once more at what's over 1.
        const auto draw = [&] (const juce::Image& image, juce::Point<int> at, float level)
        {
            for (auto left = level; left > 0.005f; left -= 1.0f)
            {
                g.setOpacity (juce::jmin (1.0f, left));
                g.drawImageTransformed (image, place (at));
            }
        };
        draw (h.glow, h.glowAt, restLevel);   // (always as bright: a kick redraws only what it brightens)
        draw (h.ring, h.ringAt, holoLevel);
        // The icon (0.60.0, made bolder: "a glitched hologram"): at 25%, every other pair of lines again at 75% (the
        // scanlines, two on, two off, a line further up every quarter second), the rolling band 30% more (45% more
        // again on its scanlines); while it breaks up, its strips drawn shifted sideways, a faint copy of it aside,
        // the whole flickering.
        const auto side = (int) (&h - holograms.data());
        const auto glitch = holo::glitchAt (side, holoTime);
        const auto level = holoLevel * glitch.flicker;
        const auto iconArea = juce::Rectangle<float> (shownAt.area.getX() + (float) h.iconAt.x / scale, shownAt.area.getY() + (float) h.iconAt.y / scale,
                                                      (float) h.icon.getWidth() / scale, (float) h.icon.getHeight() / scale);
        const auto box = iconArea.getSmallestIntegerContainer();
        const auto iconTall = (float) hologramIconArea (side).getHeight();   // the icon itself (the image also holds the beam)
        const auto iconTop = (float) hologramIconArea (side).getY();
        const auto iconWide = (float) hologramIconArea (side).getWidth();
        const auto bandRect = [&] (const holo::Band& b)
        {
            return juce::Rectangle<int> (box.getX() - 1, (int) std::floor (iconTop + b.top * iconTall), box.getWidth() + 2,
                                         juce::jmax (1, (int) std::ceil (b.height * iconTall)));
        };
        const auto pass = [&] (float lvl, const juce::RectangleList<int>* clip)
        {
            {
                juce::Graphics::ScopedSaveState saved (g);
                if (clip == nullptr || g.reduceClipRegion (*clip))
                {
                    for (int b = 0; b < glitch.numBands; ++b)
                        g.excludeClipRegion (bandRect (glitch.bands[(size_t) b]));
                    draw (h.icon, h.iconAt, lvl);
                }
            }
            for (int b = 0; b < glitch.numBands; ++b)
            {
                juce::Graphics::ScopedSaveState saved (g);
                if ((clip == nullptr || g.reduceClipRegion (*clip)) && g.reduceClipRegion (bandRect (glitch.bands[(size_t) b])))
                    draw (h.icon, h.iconAt + juce::Point<int> (juce::roundToInt (glitch.bands[(size_t) b].shift * iconWide * scale), 0), lvl);
            }
        };
        pass (level * 0.25f, nullptr);
        juce::RectangleList<int> lines;
        for (int y = box.getY(); y < box.getBottom(); ++y)
            if ((((y + scanPhase) % 4) + 4) % 4 < 2)
                lines.addWithoutMerging ({ box.getX() - 8, y, box.getWidth() + 16, 1 });
        pass (level * 0.75f, &lines);
        if (glitch.rollStep >= 0)
        {
            const auto rollTop = (int) std::floor (iconTop + glitch.rollTop * iconTall);
            juce::RectangleList<int> roll;
            roll.add ({ box.getX() - 8, rollTop, box.getWidth() + 16, juce::roundToInt (0.18f * iconTall) });
            pass (level * 0.3f, &roll);
            roll.clipTo (lines);   // (brighter still on its scanlines: a sweep, not a block)
            pass (level * 0.45f, &roll);
        }
        if (! juce::exactlyEqual (glitch.ghost, 0.0f))
            draw (h.icon, h.iconAt + juce::Point<int> (juce::roundToInt (glitch.ghost * iconWide * scale), 0), level * 0.22f);
    }
    g.setOpacity (1.0f);
}
