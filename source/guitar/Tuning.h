#pragma once

#include <juce_core/juce_core.h>

#include <array>

// THE GUITAR (PR7-I 0.2.0): the owner's 7-string in drop A. Strings are numbered from 0, the low A, up to 6, the high E
// (a guitarist calls the high E string 1 and the low A string 7: `playerNumber`). Frets 0 (open) to 24.
namespace guitar
{
    inline constexpr int numStrings = 7;
    inline constexpr int numFrets = 24;   // the highest fret; 25 positions with the open string
    inline constexpr std::array<int, numStrings> openNotes { 33, 40, 45, 50, 55, 59, 64 };   // A1 E2 A2 D3 G3 B3 E4
    inline constexpr int lowestNote = 33, highestNote = 64 + numFrets;                        // A1 to E6

    inline int noteOf (int string, int fret) noexcept { return openNotes[(size_t) string] + fret; }
    inline int playerNumber (int string) noexcept { return numStrings - string; }   // 0 (low A) -> 7
    inline juce::String stringName (int string)
    {
        static const char* names[] { "A", "E", "A", "D", "G", "B", "E" };
        return juce::String (names[juce::jlimit (0, numStrings - 1, string)]);
    }
    // "STRING 7 (LOW A)", as the render list's rows and files name a string.
    inline juce::String stringLabel (int string)
    {
        const auto n = juce::String (playerNumber (string));
        if (string == 0) return "STRING " + n + " (LOW A)";
        if (string == numStrings - 1) return "STRING " + n + " (HIGH E)";
        return "STRING " + n + " (" + stringName (string) + ")";
    }
    inline double frequencyOf (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    // How a string is played. Only SUSTAIN in 0.2.0; MUTE (palm mute) comes in 0.3.0. Append only (saved by index).
    enum class Articulation { sustain, mute };
    inline constexpr int numArticulations = 2;
    inline juce::String articulationName (Articulation a) { return a == Articulation::mute ? "Mute" : "Sustain"; }

    enum class Stroke { down, up };
}
