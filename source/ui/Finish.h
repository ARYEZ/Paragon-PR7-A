#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// The unit's finish: Obsidian (a pearl black unit, the default) or Aurora
// (pearl white, 10% less bright: auroraDimming). On both the keys and knobs
// are black, in neon rings (pink to red to orange): the small keys'
// surrounds, the knobs' rings, the bezels. Only the unit itself changes (in
// PR7-I the rack across the top of the GUITAR page: its chassis, the
// lettering printed on it); the screens, the neon, the guitar and the dark
// pages round it stay as they are. (0.6.4 and before had Pearl White with
// chrome rings, Pearl Black and Pearl White + Neon, but no way to choose:
// the rack was always pearl black.)
//
// It's a preference of the person, not part of a project or a preset: kept in
// a small settings file in the user's application data folder, so every
// PR7-I opened afterwards, in any project, uses the same finish. Every open
// window repaints when it changes.
namespace Finish
{
    // Saved by number: only ever add to the end. retiredPearlWhite (0) is
    // only ever read from an old settings file, and opens as Obsidian;
    // obsidian (1) was Pearl Black, aurora (2) Pearl White + Neon.
    enum class Type { retiredPearlWhite, obsidian, aurora };

    Type get();
    // While set, everything drawn is in this finish instead: set in a component's paint, cleared in its
    // paintOverChildren, so its children (keys, knobs) draw in it too. Message thread only. (Unused since 0.7.0,
    // when the rack began to follow the finish; kept for a part that must keep one finish.)
    void paintAs (std::optional<Type>);
    // `remember` saves it to the settings file (off for tests and snapshots).
    void set (Type, bool remember = true);
    inline bool isBlack() { return get() == Type::obsidian; }            // the chassis colour

    // Told whenever the finish changes, to repaint.
    juce::ChangeBroadcaster& changes();

    // Colours that follow the finish.
    juce::Colour ink();          // lettering printed on the unit: near black on white, pearl white on black
    juce::Colour inkDim();
    juce::Colour stampEdge();    // the fine edge under stamped lettering: a light catch on white, a shadow on black
    juce::Colour printedRed();   // the red lettering and lines (a touch brighter on black, so it reads as clearly)

    // The satin grain laid over pearl surfaces: a fine speckle with no
    // direction to it (so no brushed lines) and the odd bright mica fleck.
    // On black the flecks show more, as they do in a real black pearl paint.
    juce::Image makeSatinGrain (int width, int height, int seed);

    // The neon of the rings round the keys and knobs: hot pink at the
    // left through red to orange at the right (the reference swatch's
    // colours, 25% darker), across `left` to `right`. Lighting is drawn over it.
    juce::ColourGradient neonGradient (float left, float right);

    // AURORA's pearl is darkened by this much (the owner: full white was "too
    // bright, almost disorienting"): every pixel of the pearl surfaces, their
    // sheen and highlights included, is drawn at 90% of its old value, so pure
    // white becomes 230/255. The lettering, the neon, the keys and the screens on
    // it keep their own colours. 0.24.2 took 25% off, which looked grey; 0.24.3
    // settled on 10% (the owner's choice, from mock-ups of 10, 15 and 25%).
    inline constexpr float auroraDimming = 0.10f;

    // Darkens a pearl surface just drawn, inside `area`, by auroraDimming on
    // Aurora (black laid over it: each colour times 1 - auroraDimming); nothing on Obsidian.
    void dimPearl (juce::Graphics&, const juce::Path& area);
    // A colour of Aurora's pearl as drawn (times 1 - auroraDimming); unchanged on Obsidian.
    juce::Colour pearl (juce::Colour);

    // Picks the colour for the current finish.
    inline juce::Colour pick (juce::uint32 onWhite, juce::uint32 onBlack) { return juce::Colour (isBlack() ? onBlack : onWhite); }
}
