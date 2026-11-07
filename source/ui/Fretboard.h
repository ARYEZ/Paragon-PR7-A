#pragma once

#include "PluginProcessor.h"

// THE FRETBOARD (PR7-I 0.2.0): the neck in the strip under the pages, lighting the string and fret each note is played
// on, as the owner's reference guitar instrument shows them. High E on top (as tab is written), the nut on the left,
// frets spaced as on a real neck; the hand's reach shaded, a forced string lit. Each string its colour from PR7-D's
// "Tron" kit: pink on the low A, through red, to orange on the high E. A plain first version: 0.3.0 gives it the
// Tron look.
class Fretboard final : public juce::Component,
                        private juce::Timer
{
public:
    explicit Fretboard (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void refresh();   // from the guitar now (the timer's; tests)

    static juce::Colour stringColour (int string);
    float fretX (int fret) const;                      // the fret wire's x (0: the nut)
    juce::Point<float> noteCentre (int string, int fret) const;
    int shownFret (int string) const noexcept { return frets[(size_t) string]; }

private:
    void timerCallback() override { refresh(); }
    juce::Rectangle<float> neck() const;
    float stringY (int string) const;
    ParagonX1AudioProcessor& processor;
    std::array<int, guitar::numStrings> frets {};
    std::array<float, guitar::numStrings> levels {};
    int hand = 1, forced = -1;
};
