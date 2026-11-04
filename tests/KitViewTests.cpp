// THE KIT VIEW (0.14.0): its pictures, where clicks land, a click choosing and playing a drum, and each piece's glow
// following its hits.
#include "TestUtils.h"

#include "PluginProcessor.h"
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
