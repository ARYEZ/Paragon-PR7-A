#pragma once

#include "PluginProcessor.h"
#include "ui/GuitarRack.h"
#include "ui/SynthControls.h"

// THE GUITAR page (PR7-I 0.4.0): the owner's approved mock-up. Our own 3D guitar (resources/guitar: a 7-string with a
// superstrat body traced off the owner's photo, in PR7-D's "Tron" look) fills the window under the header, laid out as
// the owner's reference guitar instrument's Main tab, mirrored as the owner asked (the headstock right, the low A on
// top): frets 0 to 24 across the window, the body's horns at the left. On it, live:
//   - each note on its string and fret: a solid dot sustained, a ring palm-muted, the string lit from it to the bridge;
//   - the hand's reach (a lit window and a bracket: HAND 1-4 AUTO), the capo (drag it from the nut along the neck;
//     double-click it off), a forced string;
// and the controls floating round it where the reference has its own (mirrored): TONE BITE ATTACK TRANSPOSE VEL > VOL
// RELEASE; POLY WINDOW, DOUBLE TRACKING (a later release), VARIATION; OUTPUT (LEVEL, OUTPUT, the meter) and the KEY
// MAP; the PICKING / FRETTING / PERFORMANCE menus, FORCE STRING and HAND POSITION, CAPO HAND SPAN HAND RESET MONO; and
// NOW PLAYING (what the last note did: our addition, where the reference has a picture of picks).
class GuitarView final : public juce::Component,
                         private juce::Timer
{
public:
    explicit GuitarView (ParagonX1AudioProcessor&);
    ~GuitarView() override;

    // The canvas row the view starts at (it fills the 1600 x 900 canvas under the header).
    static constexpr int top = 62;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void refresh();   // from the guitar now (the timer's; tests, snapshots)

    // Where things are, in the view's pixels (tests).
    juce::Point<float> noteCentre (int string, int fret) const;
    float fretX (int fret) const;                 // the fret wire (0: the nut)
    int fretAt (float x) const;                   // the fret space under x (0: between the 1st fret and the nut)
    int shownFret (int string) const noexcept { return frets[(size_t) string]; }
    bool shownMuted (int string) const noexcept { return muted[(size_t) string]; }
    juce::String getNowPlayingText() const { return rack.getScreen().line1 + "\n" + rack.getScreen().line2; }   // the rack's screen
    GuitarRack& getRack() noexcept { return rack; }
    static juce::Image picture();                  // the guitar, its colours kept in the family's range

    // A small box showing a parameter's value (CAPO, HAND SPAN...): drag up or down to change it, click an on/off
    // one, double-click for the default.
    class ValueBox;

private:
    void timerCallback() override { refresh(); }
    void paintNeck (juce::Graphics&);
    void showRackPage();   // the rack's page's controls shown, the other's hidden
    float stringY (int string, float x) const;
    float boardEdge (bool upper, float x) const;
    juce::Rectangle<int> neckArea() const;
    void setCapo (int fret);

    ParagonX1AudioProcessor& processor;
    ParamControl tone, bite, attack, transpose, velToVol, release, polyWindow, variation, level, output, pickMode, fretMode;
    GuitarRack rack;   // 0.5.0: the main controls on a rack unit, as PR7-G's
    juce::TextButton variationOn, splitMap, factoryMap;   // the rack's small keys (their stripes lit while on)
    ParamControl forceString, handPosition;   // the rack's FRETTING page
    juce::ComboBox performance;
    std::unique_ptr<juce::ParameterAttachment> keyMapAttachment;
    std::vector<std::unique_ptr<ValueBox>> boxes;
    float meterDb = -100.0f;

    std::array<int, guitar::numStrings> frets {};
    std::array<float, guitar::numStrings> levels {};
    std::array<bool, guitar::numStrings> muted {};
    int hand = 1, span = 4, forced = -1, capo = 0;
    juce::uint32 nowCount = 0;
    guitar::Guitar::NowPlaying now;
    int latchedKey = guitar::keyswitch::c1;
    bool draggingCapo = false;
    float lastVariation = 0.5f;
};

class GuitarView::ValueBox final : public juce::Component,
                                   public juce::SettableTooltipClient
{
public:
    ValueBox (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label, const juce::String& tooltip);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getValueText() const;
    void step (int by);   // tests: as dragging up by that many steps

private:
    juce::RangedAudioParameter& parameter;
    juce::String label;
    juce::ParameterAttachment attachment;
    float value = 0.0f, dragStart = 0.0f;
    bool isSwitch = false, dragged = false;
};
