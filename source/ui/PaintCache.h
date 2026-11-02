#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 0.30.0: a component's unchanging background (gradients, frames, its name and logo) drawn once into a picture at the
// screen's own pixel size, then copied on every repaint. A component under others is repainted with every one of them
// (a meter, a playing row, a hovered button), so its drawing ran at 30 times a second for no change. Drawn again when
// the size or the screen's scale changes, or `invalidate` says it changed.
class PaintCache
{
public:
    // `opaque`: the drawing covers every pixel (copied as is, not blended).
    explicit PaintCache (bool opaque = false) : format (opaque ? juce::Image::RGB : juce::Image::ARGB) {}

    template <typename Paint>
    void draw (juce::Graphics& g, juce::Rectangle<int> area, Paint&& paint)
    {
        const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
        if (area.isEmpty() || scale <= 0.0f)
            return;
        if (! image.isValid() || scale != shownScale || area != shownArea)
        {
            const auto w = juce::roundToInt ((float) area.getWidth() * scale), h = juce::roundToInt ((float) area.getHeight() * scale);
            image = juce::Image (format, juce::jmax (1, w), juce::jmax (1, h), true);
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            ig.setOrigin (-area.getPosition());
            paint (ig);
            shownScale = scale;
            shownArea = area;
        }
        // Copied pixel for pixel: the context's own scale cancels ours, and the corner sits on a whole screen pixel.
        const auto origin = (area.getTopLeft().toFloat() * scale).roundToInt();
        g.drawImageTransformed (image, juce::AffineTransform::translation ((float) origin.x, (float) origin.y).scaled (1.0f / scale));
    }
    void invalidate() noexcept { image = {}; }

private:
    juce::Image::PixelFormat format;
    juce::Image image;
    float shownScale = 0.0f;
    juce::Rectangle<int> shownArea;
};
