#pragma once

#include "PluginProcessor.h"
#include "dsp/synth/Drums.h"

// THE KIT VIEW (0.14.0): the kit drawn from the drummer's seat, on DRUMS. It's a 3D scene of our own design in the
// family's colours (black drums lit by their own glowing rings, pink on the hi-hat's side through red to orange on
// the ride's), rendered once into pictures that are built into the plugin:
//   - the kit as it sits (KitImages::kit_jpg);
//   - each of its 12 pieces glowing brighter, cut to the area it changes (lit-<piece>.png, with an alpha that fades
//     where the glow fades, so a piece fades in over the kit with no seam), and where each cut goes (kitlayout.txt);
//   - a click map (kitzones.png): each clickable part in its own value, the slot it plays + 1 (0: nothing). The snare's
//     head is SD, its hoop SD2 (the rimshot), its shell RIM (the sidestick); the ride's bow RD, its bell RB; the
//     hi-hat CH, its pedal PH. The clap and the shaker have no piece.
// Click a piece to choose its drum and hear it (as its row does; SHIFT / CTRL choose several, as on the rows). Each
// piece glows brighter as it plays, fading with its hit over its first second; the chosen drum's piece glows a little.
class KitView final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit KitView (ParagonX1AudioProcessor&);

    // A click on a slot's part: DrumPage chooses it (and the view plays it).
    std::function<void (int slot, juce::ModifierKeys)> onChoose;

    static constexpr int numPieces = 12;   // (12 until 0.37.0, 19 until 0.43.0)
    static const char* pieceName (int piece) noexcept;   // "kick", "snare", ... (the pictures' names)
    static int pieceOf (int slot) noexcept;              // the piece a slot glows on, -1 for none (CLP, SHK)

    // The pictures, loaded once: the kit, the click map, and each piece's glow with where it goes (in the kit's
    // pixels). Empty images if they failed to load (the view then draws nothing).
    struct Pictures
    {
        juce::Image kit, zones;
        std::array<juce::Image, numPieces> lit;
        std::array<juce::Rectangle<int>, numPieces> litArea;
    };
    static const Pictures& pictures();

    juce::Rectangle<float> pictureArea() const;            // where the kit is drawn in the view (it keeps its shape)
    int slotAt (juce::Point<float>) const;                 // the slot a click there plays, or -1
    std::optional<juce::Point<float>> pointFor (int slot) const;   // somewhere a click chooses `slot` (tests)
    void click (juce::Point<float>, juce::ModifierKeys);   // what a mouse click does

    void setSelected (int slot);

    // EDIT PLAY STYLE (0.46.0): the kit as that window shows it: the pieces a block plays lit and outlined, the rest
    // dimmed, the chosen ones outlined brighter and glowing. Off: the kit as on DRUMS. With it on, a click chooses
    // (onChoose) without playing the drum, a right-click asks for the menu (onMenu), and the caption line isn't drawn.
    struct Highlight
    {
        bool on = false;
        std::array<bool, numPieces> used {}, chosen {};
        bool operator== (const Highlight&) const = default;
    };
    void setHighlight (const Highlight&);
    std::function<void (int slot, juce::Point<float>)> onMenu;
    std::optional<juce::Point<float>> pieceCentre (int piece) const;   // the middle of a piece's drawn area
    std::optional<juce::Point<float>> slotCentre (int slot) const;     // the middle of a slot's part (EDIT PLAY STYLE's flags)
    void updateLights();                     // each piece's glow from the kit's hits (DrumPage's timer)

    // THE HOLOGRAMS (0.57.0): the PR7-D icon floating over a glowing ring on the floor at each side of the kit (pink on
    // the hi-hat's side, orange on the ride's), a beam of light from the ring up to it, scanlines drifting slowly up,
    // all brighter for a moment with each kick. Not drawn in EDIT PLAY STYLE's view; clicks go through them.
    static constexpr int numHolograms = 2;
    juce::Rectangle<int> hologramArea (int side) const;   // where one is drawn, its glow included (the view's pixels)
    juce::Rectangle<int> hologramIconArea (int side) const;   // its icon
    juce::Rectangle<int> hologramPulseArea (int side) const;  // what a kick brightens: the icon, its beam and ring
    static constexpr float restLevel = 0.9f;
    float getHologramLevel() const noexcept { return holoLevel; }   // restLevel, up to 1.35 just after a kick
    int getScanPhase() const noexcept { return scanPhase; }
    void setHologramsShown (bool shown) { hologramsShown = shown; repaint(); }   // (tests: the kit alone)
    // 0.60.0: the holograms' clock fixed at `seconds` (tests and snapshots: the glitch and the rolling band at a known
    // moment); < 0 back to the real clock. Takes effect at the next updateLights.
    void setHologramTime (double seconds) { fixedHoloTime = seconds; }
    bool isGlitching (int side) const;   // breaking up now (its strips shifted)
    bool isRolling (int side) const;     // the bright band rolling through it now
    float getGlow (int piece) const noexcept { return glow[(size_t) piece]; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String describe (int slot) const;
    juce::Rectangle<int> areaOf (int piece) const;   // where a piece's glow is drawn in the view
    void prepare (float scale);                       // the pictures at the size shown (0.15.1)

    struct Shown
    {
        float scale = 0.0f;
        juce::Rectangle<float> area;
        juce::Image kit;
        std::array<juce::Image, numPieces> lit;
        std::array<juce::Point<int>, numPieces> litAt {};   // in screen pixels from the picture's corner
    };
    Shown shownAt;

    // Each hologram at the size shown, in screen pixels from the picture's corner: `glow` round it all (always drawn
    // as bright), `ring` on the floor, `icon` its icon and beam (drawn at 70%, then again at 30% on every third line:
    // the scanlines); the ring and the icon brighter on a kick.
    struct Hologram
    {
        juce::Image glow, ring, icon;
        juce::Point<int> glowAt, ringAt, iconAt;
    };
    std::array<Hologram, numHolograms> holograms;
    void prepareHolograms (float toScreen);
    void paintHolograms (juce::Graphics&, std::function<juce::AffineTransform (juce::Point<int>)> place, float scale);
    float holoLevel = restLevel;
    int scanPhase = 0;
    double holoTime = 0.0, fixedHoloTime = -1.0;
    std::array<int, numHolograms> shownGlitch { -2, -2 };
    bool hologramsShown = true;

    ParagonX1AudioProcessor& processor;
    Highlight highlight;
    // 0.49.0: the highlighted kit as last drawn (for that highlight, scale and area).
    juce::Image highlightShown;
    Highlight highlightFor;
    float highlightScale = 0.0f;
    juce::Rectangle<float> highlightArea;
    std::array<float, numPieces> glow {};
    int selected = 0, hovered = -1;
};
