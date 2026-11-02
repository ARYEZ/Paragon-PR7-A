// TAP TO FIND (PR7-D 0.6.0, plan stage d part 3): a groove's rhythm, drum group by drum group, and how close one rhythm
// is to another. Our own design, inspired by the tap-a-rhythm search of commercial drum software: the player taps a
// rhythm over a loop (or drops a MIDI file, or picks a groove), and every groove gets a match from 0 to 1.
#pragma once

#include "Groove.h"

#include <memory>

namespace grooves
{
    // The drum groups tapped and compared (the pads). Others (pedal hat, clap, percussion) aren't compared.
    enum class Lane { kick, snare, hats, ride, toms, crash };
    inline constexpr int numLanes = 6;
    inline const juce::StringArray laneNames { "KICK", "SNARE", "HI-HAT", "RIDE", "TOMS", "CRASH" };
    int laneOf (int slot) noexcept;       // the kit's slot's group, or -1
    int slotOfLane (int lane) noexcept;   // the drum a pad plays: kick, snare, closed hat, ride, mid tom, cymbal

    // Where each group's hits are, in beats from the start; the rhythm loops every lengthBeats.
    struct Rhythm
    {
        double lengthBeats = 8.0, beatsPerBar = 4.0;
        std::array<std::vector<double>, numLanes> beats;   // each in time order
        // 0.31.0: each hit's velocity (0 to 1), beside `beats`; empty: all full (an index from before 0.31.0, taps).
        std::array<std::vector<float>, numLanes> velocities;
        // 0.31.0, a query only (not kept): how far its hits may all be late (taps: the interface's latency), in beats.
        double slackBeats = 0.0;

        float velocityOf (int lane, size_t hit) const noexcept
        {
            const auto& v = velocities[(size_t) lane];
            return hit < v.size() ? v[hit] : 1.0f;
        }
        void add (int lane, double beat, float velocity = 1.0f);   // in time order (velocities kept alongside)

        bool isEmpty() const noexcept
        {
            return std::all_of (beats.begin(), beats.end(), [] (const auto& b) { return b.empty(); });
        }
        int numHits() const noexcept
        {
            auto n = 0;
            for (const auto& b : beats)
                n += (int) b.size();
            return n;
        }

        // As text for the index: "length;perBar;lane:twelfths,twelfths...|lane:...": beats to a twelfth of a beat
        // (sixteenths and triplets alike land on it), at most the first `maxBeats` (32: eight bars of 4/4). 0.31.0: a
        // hit softer than full has "~velocity" (1-126) after it.
        static constexpr double maxBeats = 32.0;
        juce::String encode() const;
        static Rhythm decode (const juce::String&);
    };

    // A groove's rhythm through `layout` (which drum each note is), its first maxBeats.
    Rhythm rhythmOf (const Groove&, const synth::drums::notemap::Map& layout);
    // The first `lengthBeats` of a groove as a tap: a MIDI file dropped, a groove to find more like.
    Rhythm firstBars (const Rhythm&, double lengthBeats);
    // What a dropped file or a groove searches with (0.31.0): its first four bars (all of it when shorter).
    inline constexpr int searchBars = 4;
    Rhythm searchRhythm (const Rhythm&);
    // Taps' latency allowance: 40 ms at `bpm`, in beats.
    inline double tapSlackBeats (double bpm) noexcept { return 0.040 * bpm / 60.0; }

    // How close `groove` is to the tapped `query`, 0 to 1 (0.31.0, measured on two real groove packs):
    // - hits are compared where they were played: a groove's hit within half a `gridBeats` step (QUANTIZE) of a tapped
    //   one counts as found, the nearest pairs first, one to one (before 0.31.0 both were rounded into grid cells: a
    //   sixteenth sat on the edge between two eighths);
    // - only the groups tapped count, each by how many hits it has; a group scores 2 x found / (tapped + found + the
    //   groove's other hits x 1/4, its ghost notes (under 55% of the group's loudest) x 1/10): taps are a sketch;
    // - HI-HAT and RIDE stand in for each other when the groove has only one of them there;
    // - the groove (looped) is tried from each of its bars, the best kept; the query's `slackBeats` lets all its hits
    //   be up to that much late (tried in five steps);
    // - a groove with a different bar length (3/4 against 4/4) counts 3/4 as much.
    double match (const Rhythm& query, const Rhythm& groove, double gridBeats);

    // QUANTIZE's grids.
    inline const juce::StringArray quantizeNames { "1/4", "1/8", "1/16", "1/8 Triplet", "1/16 Triplet" };
    inline double gridOf (int quantize) noexcept
    {
        constexpr double grids[] = { 1.0, 0.5, 0.25, 1.0 / 3.0, 1.0 / 6.0 };
        return grids[juce::jlimit (0, 4, quantize)];
    }
}
