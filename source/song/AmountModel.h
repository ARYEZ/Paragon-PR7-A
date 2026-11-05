// EDIT PLAY STYLE's AMOUNT, learned (PR7-D 0.54.0). The manual's AMOUNT "calculates the probabilities of where a real
// drummer would add or remove hits", from "tens of thousands of real drum performances". Ours learns them from the
// user's own groove library (the GROOVES tab's MIDI packs and groove databases: real drummers' playing), not from
// Toontrack's data. Our own design:
//   - every groove in the library, cut into bars, each drum group's hits (grooves::Lane: kick, snare, hi-hat, ride,
//     toms, crash) as bits, one per twelfth of a beat (sixteenths and triplets both land on one); kept apart by bar
//     length, straight or swung, and fill or not (a fill learns from fills);
//   - asked about one drum group in one bar of a block, it finds the library's bars most like it: for ADDING, bars
//     that play (nearly) everything this bar plays on that drum and more; for REMOVING, bars that play only part of
//     it; both weighted by how alike the other drums are too (a kick learns from bars whose snare and hats match);
//   - the 40 closest bars vote: how often each place in the bar is played there, blended with how often it's played
//     in the whole library (so a few close bars don't decide alone);
//   - with too little alike (an odd meter, a groove nothing in the library resembles), it says so, and the rules
//     (0.46.0's) decide instead.
// Rhythms come from the groove index (TAP TO FIND's: the first 32 beats of each groove, to a twelfth of a beat).
#pragma once

#include "grooves/GrooveLibrary.h"
#include "song/Song.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace song::playstyle
{
    // One bar: each drum group's hits, a bit per twelfth of a beat from the bar line.
    struct BarPattern
    {
        std::array<std::uint64_t, grooves::numLanes> lanes {};
        bool operator< (const BarPattern& o) const noexcept { return lanes < o.lanes; }
        bool operator== (const BarPattern& o) const noexcept = default;
        bool isEmpty() const noexcept
        {
            return std::all_of (lanes.begin(), lanes.end(), [] (std::uint64_t l) { return l == 0; });
        }
    };

    class AmountModel
    {
    public:
        static constexpr int stepsPerBeat = 12;
        static constexpr int maxSteps = 64;      // bars up to 5 beats long (longer: the rules)
        static constexpr int neighbours = 40;    // the closest bars that vote
        static constexpr int enoughBars = 30;    // fewer bars of a kind: a neighbouring kind's (another feel...)

        // Everything learned again from these grooves (their rhythms; entries without one are skipped).
        void learn (const std::vector<grooves::Entry>&);
        // One groove's bars added (tests); `finish` after adding, before asking.
        void add (const grooves::Rhythm&, bool swung, bool fill);
        void finish();
        size_t numBars() const noexcept { return total; }

        struct Query
        {
            int lane = 0;              // the drum group asked about
            int steps = 48;            // the bar's length in twelfths of a beat (4/4: 48)
            bool swung = false, fill = false;
            bool adding = true;        // adding (bars with more) or removing (bars with less)
            BarPattern bar;            // the bar as it plays: the asked group's hits and the other groups'
        };
        // For each place in the bar (steps of it), how likely a drummer plays the group there, 0 to 1, among the bars
        // like this one. Empty: not enough alike was learned (use the rules).
        std::vector<double> likelihood (const Query&) const;

    private:
        struct Bucket
        {
            std::vector<BarPattern> bars;
            std::array<std::array<float, maxSteps>, grooves::numLanes> prior {};   // how often each place is played
        };
        static int keyOf (int steps, bool swung, bool fill) noexcept { return steps * 4 + (swung ? 2 : 0) + (fill ? 1 : 0); }
        const Bucket* bucketFor (int steps, bool swung, bool fill) const;
        std::map<int, Bucket> buckets;
        size_t total = 0;
    };

    // A block's bar `bar` as a BarPattern: `notes` (the asked instrument's) in `lane`, the block's other hits (not
    // `excluded`'s slots, nor other instruments of `lane`'s group) in theirs.
    BarPattern barOf (const std::vector<Hit>& notes, int lane, const Block&, SlotMask excluded, int bar, double beatsPerBar);
}
