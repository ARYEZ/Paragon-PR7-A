#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// PR7-I 0.5.0: the GUITAR page's main controls as one of the family's rack units (the owner: "use the rack images from
// the PR7-G line ... so they look in line with the rest of the family"), drawn with PR7-G's own parts: the pearl black
// chassis with the red light round it (PedalBody::paintUnit), the stamped lettering and printed red lines, the orange LED
// screen, the big knobs with their values on small screens, the small keys with neon stripes, a neon status bar. Always
// pearl black (PR7-G's Obsidian), whatever the finish, as it sits on the dark guitar page.
//
// Purely the unit's face: the controls on it (GuitarView's knobs and keys) are its children, laid out in the areas it
// gives. Left to right: the screen (the articulation latched, the last note) over the KEY MAP and VARIATION keys; a
// printed line; the knobs, two rows of four; a printed line; OUTPUT (a meter screen, LEVEL, OUTPUT).
class GuitarRack final : public juce::Component
{
public:
    GuitarRack();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    struct Screen
    {
        juce::String caption, articulation, key, line1, line2;
        bool operator== (const Screen&) const = default;
    };
    void setScreen (const Screen&);
    void setOn (bool);
    void setMeterDb (float);   // the output's level, for the meter screen (dB)

    // Where the controls go, in the unit's pixels.
    juce::Rectangle<int> knobCell (int row, int column) const;
    juce::Rectangle<int> keyCell (int index) const;             // 0 SPLIT, 1 FACTORY, 2 VARIATION
    juce::Rectangle<int> sideKnobCell (int index) const;        // 0 LEVEL, 1 OUTPUT
    juce::Rectangle<int> screenArea() const;
    juce::Rectangle<int> meterArea() const;

    static constexpr int knobColumns = 4, knobRows = 2;

private:
    juce::Rectangle<float> face() const;
    juce::Rectangle<float> content() const;
    juce::Rectangle<float> keysArea() const;
    juce::Rectangle<float> knobsArea() const;
    juce::Rectangle<float> sideArea() const;

    juce::Image satin;
    Screen screen;
    bool on = true;
    float meterDb = -100.0f;
};
