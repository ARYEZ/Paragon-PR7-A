// TAP TO FIND (PR7-D 0.6.0): a groove's rhythm by drum group, and how close a tapped rhythm is to a groove's.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grooves/Rhythm.h"

using Catch::Matchers::WithinAbs;

namespace
{
    using grooves::Lane;
    using grooves::Rhythm;

    Rhythm rhythm (std::initializer_list<std::pair<Lane, std::vector<double>>> lanes, double length = 8.0, double perBar = 4.0)
    {
        Rhythm r;
        r.lengthBeats = length;
        r.beatsPerBar = perBar;
        for (const auto& [lane, beats] : lanes)
            r.beats[(size_t) lane] = beats;
        return r;
    }

    std::vector<double> every (double step, double length, double offset = 0.0)
    {
        std::vector<double> b;
        for (auto t = offset; t < length - 1e-9; t += step)
            b.push_back (t);
        return b;
    }

    // A 2-bar rock beat: kick on 1 and 3 (and the "and" of 3 in bar 2), snare on 2 and 4, eighth hats.
    Rhythm rock (Lane hand = Lane::hats)
    {
        return rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } }, { hand, every (0.5, 8.0) } });
    }
    constexpr double eighth = 0.5;
}

TEST_CASE ("TAP: the same rhythm matches fully; only the groups tapped count", "[tap]")
{
    CHECK_THAT (grooves::match (rock(), rock(), eighth), WithinAbs (1.0, 1e-9));
    // Kick and snare tapped: the groove's hats don't matter.
    const auto kickSnare = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (kickSnare, rock(), eighth), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("TAP: closer rhythms score higher", "[tap]")
{
    const auto tapped = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    // One kick different (no 6.5): close. Kicks on every beat: further. Snare on 1 and 3 instead: far.
    const auto oneOff = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto fourFloor = rhythm ({ { Lane::kick, every (1.0, 8.0) }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto swapped = rhythm ({ { Lane::kick, { 1.0, 3.0, 5.0, 7.0 } }, { Lane::snare, { 0.0, 2.0, 4.0, 6.0 } } });
    const auto a = grooves::match (tapped, oneOff, eighth), b = grooves::match (tapped, fourFloor, eighth), c = grooves::match (tapped, swapped, eighth);
    INFO ("one off " << a << ", four on the floor " << b << ", swapped " << c);
    CHECK (a > b);
    CHECK (b > c);
    CHECK (a > 0.9);
    CHECK (c < 0.2);
}

TEST_CASE ("TAP: QUANTIZE: taps a little off land on the grid", "[tap]")
{
    // Tapped late and early by up to a sixteenth's half: on the 1/8 grid, the same.
    const auto loose = rhythm ({ { Lane::kick, { 0.06, 1.97, 4.1, 5.92, 6.55 } }, { Lane::snare, { 1.08, 2.93, 5.0, 7.1 } } });
    CHECK_THAT (grooves::match (loose, rock(), eighth), WithinAbs (1.0, 1e-9));
    // A tap a hair early for beat 1 (at the loop's end) counts on beat 1.
    const auto early = rhythm ({ { Lane::kick, { 7.95, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (early, rock(), eighth), WithinAbs (1.0, 1e-9));
    // On the 1/16 grid an off-by-a-sixteenth tap is a miss.
    const auto offSixteenth = rhythm ({ { Lane::kick, { 0.25, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK (grooves::match (offSixteenth, rock(), 0.25) < 1.0);
}

TEST_CASE ("TAP: HI-HAT and RIDE stand in for each other (the riding hand)", "[tap]")
{
    const auto hats = rhythm ({ { Lane::hats, every (0.5, 8.0) } });
    CHECK_THAT (grooves::match (hats, rock (Lane::ride), eighth), WithinAbs (1.0, 1e-9));
    const auto ride = rhythm ({ { Lane::ride, every (0.5, 8.0) } });
    CHECK_THAT (grooves::match (ride, rock (Lane::hats), eighth), WithinAbs (1.0, 1e-9));
    // A groove with both: each is itself.
    auto both = rock (Lane::hats);
    both.beats[(size_t) Lane::ride] = every (1.0, 8.0);
    CHECK (grooves::match (ride, both, eighth) < 0.8);
}

TEST_CASE ("TAP: a groove is tried from each of its bars, and loops when shorter", "[tap]")
{
    // A 4-bar groove: bars 1-2 just quarter kicks; bars 3-4 the rock beat. The tap finds bars 3-4.
    auto four = rhythm ({ { Lane::kick, every (1.0, 8.0) } }, 16.0);
    const auto beat = rock();   // held: a loop over part of a temporary would read it after it's gone
    for (const auto b : beat.beats[(size_t) Lane::kick]) four.beats[(size_t) Lane::kick].push_back (8.0 + b);
    for (const auto b : beat.beats[(size_t) Lane::snare]) four.beats[(size_t) Lane::snare].push_back (8.0 + b);
    std::sort (four.beats[(size_t) Lane::kick].begin(), four.beats[(size_t) Lane::kick].end());
    const auto kickSnare = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (kickSnare, four, eighth), WithinAbs (1.0, 1e-9));
    // A one-bar groove, looped, against a two-bar tap of the same bar twice.
    const auto oneBar = rhythm ({ { Lane::kick, { 0.0, 2.0 } }, { Lane::snare, { 1.0, 3.0 } } }, 4.0);
    const auto twice = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (twice, oneBar, eighth), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("TAP: nothing tapped, or nothing in the groove: 0", "[tap]")
{
    CHECK (grooves::match (Rhythm(), rock(), eighth) == 0.0);
    CHECK (grooves::match (rock(), Rhythm(), eighth) == 0.0);
    const auto toms = rhythm ({ { Lane::toms, { 0.0, 1.0 } } });
    CHECK (grooves::match (toms, rock(), eighth) == 0.0);   // the groove has no toms
}

TEST_CASE ("TAP: a groove's rhythm through its map, kept as text to the twelfth of a beat", "[tap]")
{
    grooves::Groove g;
    g.lengthBeats = 8.0;
    g.hits = { { 0.0, 36, 1.0f }, { 1.0, 38, 1.0f }, { 1.0 / 3.0, 22, 1.0f }, { 2.0, 51, 1.0f }, { 3.0, 49, 1.0f },
               { 3.5, 44, 1.0f }, { 40.0, 36, 1.0f } };
    const auto r = grooves::rhythmOf (g, synth::drums::notemap::grooveLibrary());
    CHECK (r.beats[(size_t) Lane::kick] == std::vector<double> { 0.0 });   // the one past 32 beats left out
    CHECK (r.beats[(size_t) Lane::snare] == std::vector<double> { 1.0 });
    CHECK (r.beats[(size_t) Lane::hats].size() == 1);                        // 22: the hats (closed edge); 44 (pedal) not a lane
    CHECK (r.beats[(size_t) Lane::ride] == std::vector<double> { 2.0 });
    CHECK (r.beats[(size_t) Lane::crash] == std::vector<double> { 3.0 });
    const auto back = Rhythm::decode (r.encode());
    CHECK_THAT (back.beats[(size_t) Lane::hats][0], WithinAbs (1.0 / 3.0, 1e-9));
    CHECK_THAT (back.lengthBeats, WithinAbs (8.0, 1e-9));
    CHECK (back.numHits() == r.numHits());
    CHECK (grooves::firstBars (r, 2.0).numHits() == 3);   // kick, triplet hat, snare
}

//==============================================================================
// 0.31.0: the fixes the audit on two real groove packs called for.
namespace
{
    Rhythm withVelocities (Rhythm r, Lane lane, std::vector<float> v)
    {
        r.velocities[(size_t) lane] = std::move (v);
        return r;
    }
}

TEST_CASE ("TAP 0.31.0: at 1/8, a groove's sixteenths no longer sit on a knife edge", "[tap]")
{
    // A kick on the "a" of beat 1 (0.75): halfway between two eighths. A tap a hair either side of it must score the same.
    const auto groove = rhythm ({ { Lane::kick, { 0.0, 0.75, 2.0, 4.0, 4.75, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto early = rhythm ({ { Lane::kick, { 0.0, 0.73, 2.0, 4.0, 4.73, 6.0 } } });
    const auto late = rhythm ({ { Lane::kick, { 0.0, 0.77, 2.0, 4.0, 4.77, 6.0 } } });
    CHECK_THAT (grooves::match (early, groove, eighth), WithinAbs (grooves::match (late, groove, eighth), 1e-9));
    CHECK (grooves::match (late, groove, eighth) > 0.6);
}

TEST_CASE ("TAP 0.31.0: hits not tapped count lightly, ghost notes least", "[tap]")
{
    // Tapped: the backbeat only. A groove with the backbeat and soft ghost notes between beats is a better answer than
    // one with loud extra snares, and both are good answers (the tap is a sketch).
    const auto tapped = rhythm ({ { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    std::vector<double> ghosted { 1.0, 1.75, 2.5, 3.0, 3.75, 5.0, 5.75, 6.5, 7.0, 7.75 };
    std::vector<float> soft { 1.0f, 0.3f, 0.3f, 1.0f, 0.3f, 1.0f, 0.3f, 0.3f, 1.0f, 0.3f };
    const auto ghosts = withVelocities (rhythm ({ { Lane::snare, ghosted } }), Lane::snare, soft);
    const auto loud = rhythm ({ { Lane::snare, ghosted } });
    const auto ghostScore = grooves::match (tapped, ghosts, eighth), loudScore = grooves::match (tapped, loud, eighth);
    INFO ("ghosts " << ghostScore << ", loud extras " << loudScore);
    CHECK (ghostScore > loudScore);
    CHECK (ghostScore > 0.85);        // 2 x 4 / (4 + 4 + 6 x 0.1) = 0.93
    CHECK (loudScore > 0.7);          // 2 x 4 / (4 + 4 + 6 x 0.25) = 0.84 (0.57 before 0.31.0)
    CHECK_THAT (grooves::match (tapped, rhythm ({ { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } }), eighth), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("TAP 0.31.0: taps all a little late (the interface's latency) still match", "[tap]")
{
    // At 200 BPM, 39 ms late is 0.13 of a beat: on 1/16 every tap rounds into the next sixteenth. With the allowance
    // (40 ms) they're found; without it, not.
    auto late = rhythm ({ { Lane::kick, { 0.13, 2.13, 4.13, 6.13 } }, { Lane::snare, { 1.13, 3.13, 5.13, 7.13 } } });
    const auto groove = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto without = grooves::match (late, groove, 0.25);
    late.slackBeats = grooves::tapSlackBeats (200.0);
    CHECK_THAT (late.slackBeats, WithinAbs (0.04 * 200.0 / 60.0, 1e-9));
    const auto with = grooves::match (late, groove, 0.25);
    INFO ("without " << without << ", with " << with);
    CHECK_THAT (with, WithinAbs (1.0, 1e-9));
    CHECK (without < 0.1);
    // Early taps aren't moved later: the allowance is for lateness only.
    auto early = rhythm ({ { Lane::kick, { 7.87, 1.87, 3.87, 5.87 } } });
    early.slackBeats = grooves::tapSlackBeats (200.0);
    CHECK (grooves::match (early, rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } } }), 0.25) < 0.1);
}

TEST_CASE ("TAP 0.31.0: the clap is a snare; another bar length counts less; velocities kept", "[tap]")
{
    using namespace synth::drums::notemap;
    CHECK (grooves::laneOf (CLP) == (int) Lane::snare);
    CHECK (grooves::laneOf (PH) == -1);   // the pedal hat is a foot, not the riding hand

    // The same hits in 3/4: 3/4 as much (a 4/4 groove with them first).
    const auto tapped = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } } });
    const auto fourFour = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } } });
    const auto threeFour = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } } }, 6.0, 3.0);
    CHECK_THAT (grooves::match (tapped, fourFour, eighth), WithinAbs (1.0, 1e-9));
    CHECK (grooves::match (tapped, threeFour, eighth) <= 0.75 + 1e-9);

    // The index keeps velocities (softer than full only), and reads an index from before 0.31.0 as all full.
    auto r = withVelocities (rhythm ({ { Lane::snare, { 1.0, 1.75, 3.0 } } }), Lane::snare, { 1.0f, 0.25f, 0.8f });
    const auto text = r.encode();
    CHECK (text.contains ("21~32"));
    const auto back = Rhythm::decode (text);
    REQUIRE (back.beats[(size_t) Lane::snare].size() == 3);
    CHECK_THAT (back.velocityOf ((int) Lane::snare, 1), WithinAbs (32.0 / 127.0, 1e-6));
    CHECK_THAT (back.velocityOf ((int) Lane::snare, 0), WithinAbs (1.0, 1e-6));
    const auto old = Rhythm::decode ("8.0000;4.0000;1:12,21,36");
    CHECK_THAT (old.velocityOf ((int) Lane::snare, 1), WithinAbs (1.0, 1e-6));

    // A dropped file or a groove searches with up to four bars.
    const auto eight = rhythm ({ { Lane::kick, every (1.0, 32.0) } }, 32.0);
    const auto search = grooves::searchRhythm (eight);
    CHECK_THAT (search.lengthBeats, WithinAbs (16.0, 1e-9));
    CHECK (search.beats[(size_t) Lane::kick].size() == 16);
    CHECK_THAT (grooves::searchRhythm (rock()).lengthBeats, WithinAbs (8.0, 1e-9));   // shorter: all of it
}
