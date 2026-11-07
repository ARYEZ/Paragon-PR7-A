// THE KIT VIEW (0.14.0): its pictures, where clicks land, a click choosing and playing a drum, and each piece's glow
// following its hits.
#include "TestUtils.h"

#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/DrumPage.h"
#include "ui/KitView.h"

namespace
{
    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-kitview-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        explicit Plugin (const juce::File& presets)
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
            processor.useRigFolder (presets);
        }
        void run (double seconds, juce::MidiBuffer midi = {})
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                processor.processBlock (buffer, midi);
                midi.clear();
            }
        }
    };

    // The slots with a part on the kit (the clap and the shaker have none).
    // 0.17.0: the ride's edge (20) a ring round its bow; the hat's openness steps are chosen in the articulations.
    // 0.37.0: the owner's bigger kit: tom 4 and 5 (the floor toms), crashes 3 to 6, the FX crash, china 2; no tambourine.
    // 0.43.0: the owner's 12-piece kit: crashes 3 to 6, the FX crash, china 2 and tom 5 left the picture.
    const std::vector<int> onTheKit { 0, 1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 20, 23 };

    // A pixel's hue in degrees, and whether it's coloured enough to count (the family's rule is about colours, not
    // the near-black onyx or pearl greys).
    bool coloured (juce::Colour c) { return c.getSaturation() > 0.3f && c.getBrightness() > 0.15f && c.getAlpha() > 128; }
    float hueOf (juce::Colour c) { return c.getHue() * 360.0f; }
}

TEST_CASE ("KIT VIEW: its pictures are built in and fit together", "[kitview]")
{
    const auto& pics = KitView::pictures();
    REQUIRE (pics.kit.isValid());
    REQUIRE (pics.zones.isValid());
    CHECK (pics.kit.getBounds() == pics.zones.getBounds());
    CHECK (pics.kit.getWidth() >= 1200);   // sharp at the editor's largest size
    for (int k = 0; k < KitView::numPieces; ++k)
    {
        INFO (KitView::pieceName (k));
        REQUIRE (pics.lit[(size_t) k].isValid());
        CHECK (pics.lit[(size_t) k].getBounds().getSmallestIntegerContainer() == pics.litArea[(size_t) k].withZeroOrigin());
        CHECK (pics.kit.getBounds().contains (pics.litArea[(size_t) k]));
        CHECK (pics.lit[(size_t) k].hasAlphaChannel());
    }
}

TEST_CASE ("KIT VIEW: each drum on the kit has its part, the clap, tambourine and shaker none", "[kitview]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    KitView view (p.processor);
    view.setSize (848, 612);

    for (int slot = 0; slot < synth::drums::numSlots; ++slot)
    {
        INFO (synth::drums::slotDefaults[(size_t) slot].key);
        const auto at = view.pointFor (slot);
        const auto expected = std::find (onTheKit.begin(), onTheKit.end(), slot) != onTheKit.end();
        CHECK (at.has_value() == expected);
        if (at.has_value())
        {
            CHECK (view.slotAt (*at) == slot);
            CHECK (view.pictureArea().contains (*at));
            // The piece it glows on is the one it's drawn on.
            CHECK (KitView::pieceOf (slot) >= 0);
        }
    }
    // Articulations on one piece: the snare's head, hoop and shell; the ride's bow and bell; the hats and their pedal.
    CHECK (KitView::pieceOf (2) == KitView::pieceOf (4));
    CHECK (KitView::pieceOf (2) == KitView::pieceOf (1));
    CHECK (KitView::pieceOf (12) == KitView::pieceOf (13));
    CHECK (KitView::pieceOf (6) == KitView::pieceOf (8));
    CHECK (KitView::pieceOf (3) == -1);
    CHECK (KitView::pieceOf (19) == -1);
    CHECK (KitView::pieceOf (18) == -1);   // (0.37.0: no tambourine in the bigger kit)
    // 0.37.0: each new piece its own; 0.43.0: the ones the owner's pack doesn't record left the picture.
    CHECK (KitView::pieceOf (23) != KitView::pieceOf (5));
    for (const auto gone : { 29, 30, 31, 32, 33, 34, 35 })
        CHECK (KitView::pieceOf (gone) == -1);
    CHECK (KitView::pieceOf (25) == KitView::pieceOf (11));   // a choke on its cymbal
    // The empty floor in the corners chooses nothing.
    CHECK (view.slotAt ({ 3.0f, 3.0f }) == -1);
    CHECK (view.slotAt ({ 845.0f, 609.0f }) == -1);
}

TEST_CASE ("KIT VIEW: a click on the snare's head chooses SD and plays it", "[kitview]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    CHECK (page.getView() == DrumPage::View::kit);   // the page opens on it
    auto& view = page.getKitView();
    REQUIRE (view.isVisible());

    const auto snare = view.pointFor (2);
    REQUIRE (snare.has_value());
    CHECK (p.processor.getDrumKit().getHitAge (2) < 0.0f);
    view.click (*snare, {});
    CHECK (page.getSelected() == 2);
    CHECK (page.getChosen() == std::vector<int> { 2 });
    p.run (0.05);
    CHECK (p.processor.getDrumKit().getHitAge (2) >= 0.0f);   // it played

    // CTRL adds the ride's bell, as on the rows.
    const auto bell = view.pointFor (13);
    REQUIRE (bell.has_value());
    view.click (*bell, juce::ModifierKeys::ctrlModifier);
    CHECK (page.getChosen() == std::vector<int> { 2, 13 });

    // A click on nothing changes nothing.
    view.click ({ 2.0f, 2.0f }, {});
    CHECK (page.getChosen() == std::vector<int> { 2, 13 });
}

TEST_CASE ("KIT VIEW: a piece glows as its drum plays and fades with the hit", "[kitview]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    auto& view = page.getKitView();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 15, 1.0f), 0);   // CHN, the china (ROOT + 15)
    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 4, 1.0f), 0);    // SD2: the snare's piece
    p.run ((double) blockSize / sampleRate, midi);
    page.updateLights();
    const auto china = KitView::pieceOf (15), snare = KitView::pieceOf (4);
    CHECK (view.getGlow (china) > 0.9f);
    CHECK (view.getGlow (snare) > 0.9f);
    for (int k = 0; k < KitView::numPieces; ++k)
        if (k != china && k != snare)
            CHECK (view.getGlow (k) == 0.0f);

    p.run (0.5);
    page.updateLights();
    CHECK_THAT (view.getGlow (china), Catch::Matchers::WithinAbs (0.25, 0.05));   // (1 - age)^2, half a second in
    p.run (0.6);
    page.updateLights();
    CHECK (view.getGlow (china) == 0.0f);
}

TEST_CASE ("KIT VIEW: its pictures keep to the family's colours", "[kitview][colours]")
{
    const auto& pics = KitView::pictures();
    const auto check = [] (const juce::Image& image, const char* what)
    {
        int counted = 0, outside = 0;
        float worst = 0.0f;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                if (const auto c = image.getPixelAt (x, y); coloured (c))
                {
                    ++counted;
                    const auto h = hueOf (c);
                    if (h > 42.0f && h < 318.0f)
                    {
                        ++outside;
                        worst = std::abs (h - 180.0f) > std::abs (worst - 180.0f) ? worst : h;
                    }
                }
        INFO (what << ": " << outside << " of " << counted << " coloured pixels outside, e.g. hue " << worst);
        CHECK (counted > 0);
        CHECK (outside == 0);
    };
    check (pics.kit, "the kit");
    for (int k = 0; k < KitView::numPieces; ++k)
        check (pics.lit[(size_t) k], KitView::pieceName (k));
}

TEST_CASE ("KIT VIEW: drawn from its pictures shrunk once, it looks as drawn directly, at any display scale", "[kitview]")
{
    // 0.15.1: the pictures are shrunk to the screen's pixels once and copied after that (shrinking them on every
    // redraw was most of the window's CPU while drums played). The view must still show them where and as they were.
    TempFolder temp;
    Plugin p (temp.folder);
    KitView view (p.processor);
    view.setSize (848, 612);
    view.setSelected (3);   // the clap: no piece glows
    view.setHologramsShown (false);   // (0.57.0: drawn over the picture; tested on their own)
    CHECK (view.isOpaque());
    const auto& pics = KitView::pictures();
    for (const auto scale : { 1.0f, 1.5f, 2.0f })
    {
        INFO ("scale " << scale);
        const auto w = juce::roundToInt (848 * scale), h = juce::roundToInt (612 * scale);
        const auto render = [&] (std::function<void (juce::Graphics&)> draw)
        {
            juce::Image image (juce::Image::RGB, w, h, true);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            draw (g);
            return image;
        };
        const auto drawn = render ([&] (juce::Graphics& g) { view.paintEntireComponent (g, false); });
        const auto directAt = [&] (float shift)
        {
            return render ([&] (juce::Graphics& g)
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (pics.kit, view.pictureArea().translated (shift, 0.0f));
            });
        };
        const auto direct = directAt (0.0f), shifted = directAt (1.0f / scale);   // one screen pixel to the right
        // Inside the edges' fade and above the label line, the mean difference per pixel (red + green + blue).
        const auto inner = (view.pictureArea().reduced (60.0f) * scale).getSmallestIntegerContainer();
        const auto meanDifference = [&inner] (const juce::Image& a, const juce::Image& b)
        {
            double total = 0.0;
            int count = 0;
            for (int y = inner.getY(); y < inner.getBottom(); y += 2)
                for (int x = inner.getX(); x < inner.getRight(); x += 2)
                {
                    const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
                    total += std::abs ((int) p.getRed() - (int) q.getRed()) + std::abs ((int) p.getGreen() - (int) q.getGreen())
                             + std::abs ((int) p.getBlue() - (int) q.getBlue());
                    ++count;
                }
            return total / count;
        };
        // Smoothing differs a little from computer to computer (most when enlarging, at 2x); a picture a pixel out of
        // place differs far more. Ours must be much nearer the direct drawing than a one-pixel shift is.
        const auto ours = meanDifference (drawn, direct), oneOff = meanDifference (shifted, direct);
        INFO ("ours " << ours << ", a pixel out " << oneOff);
        CHECK (ours < 2.5);
        CHECK (ours < oneOff / 3.0);
    }
}

// THE HOLOGRAMS (0.57.0): the PR7-D icon floating over a ring on the empty floor at each side of the kit.
TEST_CASE ("KIT VIEW 0.57.0: the holograms stand on the empty floor beside the kit, clicks going through", "[kitview][hologram]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    auto& view = page.getKitView();
    const auto picture = view.pictureArea().toNearestInt();
    for (int side = 0; side < KitView::numHolograms; ++side)
    {
        INFO ("side " << side);
        const auto icon = view.hologramIconArea (side), all = view.hologramArea (side);
        CHECK (! icon.isEmpty());
        CHECK (all.contains (icon));
        CHECK (picture.expanded (2).contains (all));
        CHECK (icon.getY() > picture.getY() + 30);   // below the picture's faded top edge
        CHECK (icon.getHeight() > picture.getHeight() / 4);
        // Nothing to click under the icon or the ring: a click there chooses nothing.
        for (int y = icon.getY(); y < icon.getBottom(); y += 4)
            for (int x = icon.getX(); x < icon.getRight(); x += 4)
                CHECK (view.slotAt ({ (float) x, (float) y }) == -1);
    }
    // The first on the hi-hat's side (the picture's left), the other on the ride's.
    CHECK (view.hologramIconArea (0).getRight() < picture.getCentreX() - picture.getWidth() / 4);
    CHECK (view.hologramIconArea (1).getX() > picture.getCentreX() + picture.getWidth() / 4);
    CHECK (! view.hologramArea (0).intersects (view.hologramArea (1)));
}

TEST_CASE ("KIT VIEW 0.57.0: the holograms light up with each kick and settle again", "[kitview][hologram]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    auto& view = page.getKitView();
    page.updateLights();
    CHECK (view.getHologramLevel() == KitView::restLevel);

    // A snare: no flash.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, 1.0f), 0);
    p.run ((double) blockSize / sampleRate, midi);
    page.updateLights();
    CHECK (view.getHologramLevel() == KitView::restLevel);

    // A kick: half as bright again, fading over a third of a second.
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    p.run ((double) blockSize / sampleRate, midi);
    page.updateLights();
    CHECK (view.getHologramLevel() > KitView::restLevel + 0.4f);
    p.run (0.15);
    page.updateLights();
    const auto fading = view.getHologramLevel();
    CHECK (fading > KitView::restLevel + 0.02f);
    CHECK (fading < KitView::restLevel + 0.3f);
    p.run (0.25);
    page.updateLights();
    CHECK (view.getHologramLevel() == KitView::restLevel);
}

TEST_CASE ("KIT VIEW 0.57.0: the holograms are drawn in the family's colours, brighter on a kick, scanlines moving", "[kitview][hologram][colours]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    auto& view = page.getKitView();
    view.setHologramTime (1.0);   // (0.60.0: a moment with no glitch or rolling band)
    page.updateLights();
    const auto draw = [&view] (bool shown)
    {
        view.setHologramsShown (shown);
        return view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    };
    const auto without = draw (false), with = draw (true);
    const auto brightness = [] (const juce::Image& image, juce::Rectangle<int> area)
    {
        double total = 0.0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                total += c.getRed() + c.getGreen() + c.getBlue();
            }
        return total / (3.0 * area.getWidth() * area.getHeight());
    };
    for (int side = 0; side < KitView::numHolograms; ++side)
    {
        INFO ("side " << side);
        const auto icon = view.hologramIconArea (side);
        CHECK (brightness (with, icon) > brightness (without, icon) + 20.0);
        // Every pixel it colours is the family's (pink on the left, orange on the right: hue 318 through 0 to 42).
        int counted = 0, outside = 0;
        for (int y = icon.getY(); y < icon.getBottom(); ++y)
            for (int x = icon.getX(); x < icon.getRight(); ++x)
                if (const auto c = with.getPixelAt (x, y); coloured (c))
                {
                    ++counted;
                    if (const auto h = hueOf (c); h > 42.0f && h < 318.0f)
                        ++outside;
                }
        CHECK (counted > 200);
        CHECK (outside == 0);
        const auto meanHue = [&]
        {
            double hue = 0.0;
            int n = 0;
            for (int y = icon.getY(); y < icon.getBottom(); ++y)
                for (int x = icon.getX(); x < icon.getRight(); ++x)
                    if (const auto c = with.getPixelAt (x, y); coloured (c))
                    {
                        auto h = hueOf (c);
                        hue += h > 180.0f ? h - 360.0f : h;
                        ++n;
                    }
            return hue / juce::jmax (1, n);
        };
        if (side == 0)
            CHECK (meanHue() < 0.0);   // pinker
        else
            CHECK (meanHue() > 5.0);   // oranger
    }

    // A kick: brighter.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    p.run ((double) blockSize / sampleRate, midi);
    page.updateLights();
    const auto kicked = draw (true);
    // The light it adds over the kit (the glow round it stays as bright; the icon, beam and ring brighten).
    const auto area = view.hologramIconArea (1);
    const auto restLight = brightness (with, area) - brightness (without, area);
    const auto kickLight = brightness (kicked, area) - brightness (without, area);
    INFO ("light at rest " << restLight << ", on a kick " << kickLight);
    CHECK (kickLight > restLight * 1.05);   // (less than the level: its brightest parts are near white already, 0.60.0 more so)

    // The scanlines move a line every quarter second (0.60.0: at moments with no glitch or rolling band).
    p.run (0.5);
    const auto calm = [&view] (double t)
    {
        view.setHologramTime (t);
        view.updateLights();
        return ! view.isGlitching (0) && ! view.isRolling (0) && ! view.isGlitching (1) && ! view.isRolling (1);
    };
    auto t = 1.0;
    while (! (calm (t) && calm (t + 0.25)))
        t += 0.05;
    calm (t);
    const auto phase = view.getScanPhase();
    const auto before = draw (true);
    calm (t + 0.25);
    CHECK (view.getScanPhase() != phase);
    const auto after = draw (true);
    const auto icon = view.hologramIconArea (0);
    CHECK (std::abs (brightness (before, icon) - brightness (after, icon)) < 2.0);   // as bright, moved
    int changed = 0;
    for (int y = icon.getY(); y < icon.getBottom(); ++y)
        changed += before.getPixelAt (icon.getCentreX(), y) != after.getPixelAt (icon.getCentreX(), y) ? 1 : 0;
    CHECK (changed > 10);
}

TEST_CASE ("KIT VIEW 0.60.0: the holograms glitch now and then: strips shifted, about every two seconds, each side on its own", "[kitview][hologram]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    auto& view = page.getKitView();
    // Over a minute: break-ups (0.14 s each) about one every 2 s on each side; the rolling band every 4 s for 0.9 s.
    std::array<int, 2> breakUps {}, rolling {};
    std::array<bool, 2> was {};
    int together = 0;
    for (int f = 0; f < 60 * 100; ++f)
    {
        view.setHologramTime (f / 100.0);
        view.updateLights();
        for (int side = 0; side < 2; ++side)
        {
            const auto now = view.isGlitching (side);
            breakUps[(size_t) side] += now && ! was[(size_t) side] ? 1 : 0;
            was[(size_t) side] = now;
            rolling[(size_t) side] += view.isRolling (side) ? 1 : 0;
        }
        together += view.isGlitching (0) && view.isGlitching (1) ? 1 : 0;
    }
    for (int side = 0; side < 2; ++side)
    {
        INFO ("side " << side);
        CHECK (breakUps[(size_t) side] >= 15);
        CHECK (breakUps[(size_t) side] <= 45);
        CHECK_THAT ((double) rolling[(size_t) side], Catch::Matchers::WithinAbs (60 * 100 * 0.9 / 4.0, 100.0));
    }
    CHECK (together < 60);   // (their own timings: seldom both at once)

    // A break-up shifts strips of the icon: it looks different from a calm moment's at the same scanline step.
    const auto drawAt = [&view] (double t)
    {
        view.setHologramTime (t);
        view.updateLights();
        return view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    };
    auto t = 0.0;
    while (! view.isGlitching (0) || view.isRolling (0))
    {
        t += 0.01;
        view.setHologramTime (t);
        view.updateLights();
    }
    const auto glitched = drawAt (t);
    const auto phase = view.getScanPhase();
    auto u = t + 1.0;   // a calm moment on the same scanline step
    for (;; u += 0.01)
    {
        view.setHologramTime (u);
        view.updateLights();
        if (! view.isGlitching (0) && ! view.isRolling (0) && view.getScanPhase() == phase
            && std::abs (view.getHologramLevel() - KitView::restLevel) < 1.0e-6f)
            break;
    }
    const auto calm = drawAt (u);
    const auto icon = view.hologramIconArea (0);
    int differ = 0;
    for (int y = icon.getY(); y < icon.getBottom(); ++y)
        for (int x = icon.getX(); x < icon.getRight(); ++x)
            differ += glitched.getPixelAt (x, y) != calm.getPixelAt (x, y) ? 1 : 0;
    CHECK (differ > icon.getWidth() * icon.getHeight() / 50);
}
