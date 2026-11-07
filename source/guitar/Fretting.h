#pragma once

#include "guitar/Tuning.h"

#include <vector>

// FRETTING (PR7-I 0.2.0): which string and fret a note is played on, as a guitarist would choose. Our own design and
// code, working the way the owner's reference guitar instrument describes its engine: a virtual HAND on the neck (notes
// go where the hand already is), strings already sounding avoided, FORCE STRING, and a FRETTING MODE.
//
//   - THE HAND covers `handSpan` frets from its fret (1 = frets 1 to 4); open strings are always in reach.
//   - Each string that can play the note costs: the frets the hand would have to stretch or move outside its span (open
//     strings: none, while the hand is low on the neck), and much more if the string is already sounding (a guitarist
//     would have to stop it). The cheapest wins; ties go to the lower string (the djent riff on the open low A).
//   - NATURAL: the hand follows the notes (AUTO), four frets wide. POLYPHONIC: five wide, and chords spread over as
//     many strings as they have notes.
//   - FORCE STRING: that string if it can play the note at all.
namespace guitar
{
    enum class FretMode { natural, polyphonic };   // append only

    struct Position
    {
        int string = -1, fret = -1;
        bool valid() const noexcept { return string >= 0; }
        bool operator== (const Position& o) const noexcept { return string == o.string && fret == o.fret; }
    };

    struct FretSettings
    {
        FretMode mode = FretMode::natural;
        int forceString = -1;   // -1: none
    };

    int handSpan (FretMode) noexcept;

    // The cheapest place for `note` (invalid: no string reaches it). `busy`: strings sounding now (or already given to
    // a note of the same chord); `hand`: the hand's fret (1 to 21).
    Position choose (int note, const std::array<bool, numStrings>& busy, int hand, const FretSettings&) noexcept;

    // Notes struck together (a chord): placed lowest first, each on a string the others don't take where it can. With an
    // AUTO hand (`handFollows`), each note placed moves the hand before the next, as a player's hand goes to the lowest
    // note's fret first: drop A's D5 (D2 A2 D3) is a barre at the 5th fret, not the open A and D strings.
    // `places` gets one Position per note, in the notes' order. Nothing allocated (the audio thread calls it).
    inline constexpr int maxChord = 16;
    void chooseChord (const int* notes, int count, std::array<bool, numStrings> busy, int hand, const FretSettings&, Position* places,
                      bool handFollows = true) noexcept;
    std::vector<Position> chooseChord (const std::vector<int>& notes, std::array<bool, numStrings> busy, int hand, const FretSettings&,
                                       bool handFollows = true);

    // AUTO hand: where the hand goes after playing at `fret` (it moves just enough to reach it; open strings don't move it).
    int followHand (int hand, int fret, FretMode) noexcept;
}
