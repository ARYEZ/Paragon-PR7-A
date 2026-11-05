#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours taken from the mockup: near-black panels with an
// orange accent.
namespace ParagonColours
{
    inline const juce::Colour background { 0xff060407 };   // measured off the mockup: near black, faintly purple
    inline const juce::Colour panel      { 0xff0c0a0d };
    inline const juce::Colour panelEdge  { 0xff27222a };
    inline const juce::Colour accent     { 0xffff7a1a };
    inline const juce::Colour accentDim  { 0xff5a2a0a };
    inline const juce::Colour meterRed   { 0xffff3b2f };
    inline const juce::Colour text       { 0xffe8e8ea };
    inline const juce::Colour textDim    { 0xff8a8a92 };

    // The silver pedal unit, and dark "ink" for text printed on it.
    inline const juce::Colour metalLight { 0xfff1edef };
    inline const juce::Colour metalMid   { 0xffd5cfd2 };
    inline const juce::Colour metalDark  { 0xffa59ea2 };
    inline const juce::Colour ink        { 0xff26252a };
    inline const juce::Colour inkDim     { 0xff6b6569 };
    inline const juce::Colour glow       { 0xffff4a3a };   // the red-orange edge light

    // Each block's colour in the signal chain, as in the mockup.
    // PR7-D 0.11.0: the family's colours only (the red / pink / orange range, pearl white and onyx): the effects' own
    // colours moved into it (none of the out-of-range ones were drawn here; kept in range so none creeps in later).
    inline const juce::Colour comp       { 0xffff6f91 };   // rose
    inline const juce::Colour drive      { 0xffffb22e };
    inline const juce::Colour amp        { 0xffff4b3e };
    inline const juce::Colour cab        { 0xffff5d6c };
    inline const juce::Colour modulation { 0xffff4fa8 };   // magenta-pink
    inline const juce::Colour delay      { 0xffff8a5c };   // coral
    inline const juce::Colour reverb     { 0xffffb3c8 };   // pale pink
    inline const juce::Colour eq         { 0xffff5a2c };
    inline const juce::Colour filterPitch { 0xffff9a2e };   // orange
    inline const juce::Colour glitch     { 0xffff3d8b };   // hot pink
    inline const juce::Colour pitch      { 0xfff4eef1 };   // pearl white
    inline const juce::Colour loFi       { 0xffffc29a };   // peach   // the plugin's neon orange-red
}

class ParagonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ParagonLookAndFeel();

    // Set this property on a slider to draw it as one of the big black
    // knobs on the pedal face (RIG LEVEL, PRESENCE) instead of the
    // small ringed knob.
    static constexpr const char* pedalKnobProperty = "pedalKnob";

    // Set this on a TextButton to draw it as one of the pedal's small black
    // buttons with an LED stripe (SAVE, BYPASS...). The stripe lights when
    // the button's toggle state is on.
    static constexpr const char* pedalButtonProperty = "pedalButton";

    // Set this on a TextButton to draw it as the mockup's light outlined
    // buttons printed on the metal (VOLUME, WAH / FILTER, MORPH / FX). Its
    // outline and text turn red when its toggle state is on.
    static constexpr const char* outlinedButtonProperty = "outlinedButton";

    // Set this (to a font size) on a TextButton to draw just its text, no
    // box: the header's rig name and tempo, which sit in framed groups.
    static constexpr const char* plainTextProperty = "plainText";

    // Set this on a vertical slider to draw it as a graphic-EQ fader: a thin
    // groove lit from its centre (0 dB) to the cap, and a small black cap.
    static constexpr const char* eqFaderProperty = "eqFader";
    // A LinearBar slider whose range runs either side of 0 (a mod amount): lit from the centre to its value.
    static inline const juce::Identifier bipolarBarProperty { "paragonBipolarBar" };
    static constexpr int eqFaderThumbRadius = 7;

    // Every font in the plugin is Inter, built in: semibold for bold text,
    // and the heavy display italic for bold italic (the PARAGON badge).
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // Toggle buttons are drawn as the mockup's red/orange status LEDs.
    // EQ faders: travel inset by half the cap's height, so the cap's centre
    // reaches exactly the top and bottom of the scale.
    int getSliderThumbRadius (juce::Slider&) override;
    // Drop-down lists: a long choice ("Multi-Tap", "1/8 triplet") squeezes to
    // fit a narrow box rather than being cut off with "...".
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // Menus (every drop-down, right-click menu and the SAVE dialog's): black,
    // white text, the item under the mouse lit with a red to pink to orange
    // gradient (as PR7-C's menus are black and white).
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    static juce::ColourGradient menuHighlight (juce::Rectangle<float> area);

    // The two-thumb MIN / MAX slider: a thin track with round glowing thumbs.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

private:
    juce::Typeface::Ptr regular, semiBold, blackItalic;

public:
    static void drawPedalKnob (juce::Graphics&, juce::Rectangle<float> bounds, float angle);   // (public: tests draw one)

    // The mockup's piano-black keys (footswitches, slot blocks, small
    // buttons): a glossy black top set in chamfered edges, the top bevel
    // catching the light, the side bevels reflecting the red glow, and a
    // soft shadow on the metal. `bevel` is the chamfer's width. Black on
    // every finish (0.53.0: the neon finishes' colour is in the rings).
    static void drawPianoKey (juce::Graphics&, juce::Rectangle<float> area, float radius, float bevel,
                              bool pressed, bool highlighted);

    // The mockup's LED bars: when lit, a neon tube (white-hot core inside a
    // red-orange outline, with a soft glow); when off, a dimmer solid red.
    static void drawNeonBar (juce::Graphics&, juce::Rectangle<float> bar, bool lit);
    // A raised chrome bezel on the metal with a dark well inside it, the way a
    // real unit's footswitches and big keys are mounted. Returns the well,
    // for the key to be drawn in.
    static juce::Rectangle<float> drawChromeBezel (juce::Graphics&, juce::Rectangle<float> bezel, float radius);
    // Text stamped into the metal, with a fine edge underneath (a light
    // catch on pearl white, a shadow on pearl black: Finish::stampEdge).
    static void drawStampedText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                                 juce::Justification, juce::Colour colour);
};

// JUCE finds fonts through the *default* look-and-feel, so while any plugin
// window is open ours is made the default. Shared by every open window, and
// the default is restored when the last one closes. (The default is per
// plugin binary, so other plugins in the DAW aren't affected.)
struct SharedParagonLookAndFeel
{
    SharedParagonLookAndFeel();
    ~SharedParagonLookAndFeel();

    ParagonLookAndFeel lookAndFeel;
};
