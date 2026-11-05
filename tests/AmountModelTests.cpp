// EDIT PLAY STYLE's AMOUNT learned from the user's grooves (0.54.0): what the library's drummers add and leave out is
// what AMOUNT adds and takes out; the rules (0.46.0) only where too little alike was learned.
#include <catch2/catch_test_macros.hpp>

#include "PluginTestUtils.h"

#include "song/AmountModel.h"
#include "song/PlayStyle.h"

namespace
{
    using namespace synth::drums::notemap;
    namespace ps = song::playstyle;
    using grooves::Lane;
    constexpr int kick = 0, snare = 1, hats = 2;   // drums::pieces' order

    bool hasNoteAt (const std::vector<song::Hit>& notes, double beat)
    {
        return std::any_of (notes.begin(), notes.end(), [beat] (const song::Hit& h) { return std::abs (h.beat - beat) < 1.0e-6; });
    }

    // A two-bar groove of 4/4: hats on the eighths, snare on 2 and 4, the kick where asked (beats in each bar).
    grooves::Rhythm groove (const std::vector<double>& kicks, bool hatsOn = true)
    {
        grooves::Rhythm r;
        r.lengthBeats = 8.0;
        r.beatsPerBar = 4.0;
        for (int bar = 0; bar < 2; ++bar)
        {
            const auto at = bar * 4.0;
            for (const auto k : kicks)
                r.add ((int) Lane::kick, at + k);
            r.add ((int) Lane::snare, at + 1.0);
            r.add ((int) Lane::snare, at + 3.0);
            if (hatsOn)
                for (int e = 0; e < 8; ++e)
                    r.add ((int) Lane::hats, at + e * 0.5);
        }
        return r;
    }

    // A library whose drummers, playing this beat, put their extra kicks on the "a" of 2 and the "and" of 3 (1.75, 2.5):
    // places the rules rank low (sixteenths after the eighths).
    ps::AmountModel habitLibrary()
    {
        ps::AmountModel m;
        for (int i = 0; i < 30; ++i)
        {
            m.add (groove ({ 0.0, 2.0 }), false, false);
            m.add (groove ({ 0.0, 1.75, 2.0, 2.5 }), false, false);
        }
        // (and some other grooves, so the habit has to be found among them)
        for (int i = 0; i < 20; ++i)
        {
            m.add (groove ({ 0.0, 0.5, 2.0, 3.5 }, false), false, false);
            m.add (groove ({ 0.0, 1.0, 2.0, 3.0 }, false), false, false);
        }
        m.finish();
        return m;
    }

    song::Block blockOf (const std::vector<double>& kicks)
    {
        song::Block b;
        b.length = b.loopLength = 8.0;
        for (int bar = 0; bar < 2; ++bar)
        {
            const auto at = bar * 4.0;
            for (const auto k : kicks)
                b.hits.push_back ({ at + k, BD, 0.9f });
            b.hits.push_back ({ at + 1.0, SD, 0.8f });
            b.hits.push_back ({ at + 3.0, SD, 0.8f });
            for (int e = 0; e < 8; ++e)
                b.hits.push_back ({ at + e * 0.5, CH, 0.6f });
        }
        std::stable_sort (b.hits.begin(), b.hits.end(), [] (const song::Hit& x, const song::Hit& y) { return x.beat < y.beat; });
        return b;
    }
}

TEST_CASE ("AMOUNT 0.54.0: the library's bars, each groove's different bars once, kept by bar length", "[playstyle][amount]")
{
    ps::AmountModel m;
    m.add (groove ({ 0.0, 2.0 }), false, false);       // two identical bars: one
    CHECK (m.numBars() == 1);
    auto threeFour = groove ({ 0.0 });
    threeFour.beatsPerBar = 3.0;
    threeFour.lengthBeats = 6.0;
    m.add (threeFour, false, false);
    CHECK (m.numBars() == 3);                          // 6 beats of 3/4 cut differently: bars of their own
    auto sevenFour = groove ({ 0.0 });
    sevenFour.beatsPerBar = 7.0;                       // longer than 5 beats: not learned (the rules)
    m.add (sevenFour, false, false);
    CHECK (m.numBars() == 3);

    // Too little of a kind learned: no answer (the rules decide).
    m.finish();
    ps::AmountModel::Query q;
    q.lane = (int) Lane::kick;
    CHECK (m.likelihood (q).empty());
}

TEST_CASE ("AMOUNT 0.54.0: adding puts the kicks where the library's drummers add them, not where the rules would", "[playstyle][amount]")
{
    const auto model = habitLibrary();
    const auto b = blockOf ({ 0.0, 2.0 });
    const auto kicks = ps::notesOf (b, kick);
    REQUIRE (kicks.size() == 4);

    // Four more kicks (of 28 free sixteenths): on 1.75 and 2.5 of both bars, as the library plays it.
    const auto learned = ps::withAmount (kicks, kick, 4.0 / 28.0, b, 4.0, &model);
    REQUIRE (learned.size() == 8);
    for (const auto beat : { 0.0, 1.75, 2.0, 2.5, 4.0, 5.75, 6.0, 6.5 })
        CHECK (hasNoteAt (learned, beat));
    CHECK (ps::isLearned (kicks, kick, b, 4.0, &model));

    // The rules (no library) put them on eighths instead.
    const auto ruled = ps::withAmount (kicks, kick, 4.0 / 28.0, b, 4.0);
    REQUIRE (ruled.size() == 8);
    CHECK_FALSE (hasNoteAt (ruled, 1.75));
    CHECK_FALSE (ps::isLearned (kicks, kick, b, 4.0, nullptr));

    // Turning it further only adds (the first four stay): the knob moves smoothly through the same choices.
    const auto further = ps::withAmount (kicks, kick, 10.0 / 28.0, b, 4.0, &model);
    for (const auto& h : learned)
        CHECK (hasNoteAt (further, h.beat));
}

TEST_CASE ("AMOUNT 0.54.0: removing takes out what the library's drummers leave out", "[playstyle][amount]")
{
    const auto model = habitLibrary();
    const auto b = blockOf ({ 0.0, 1.75, 2.0, 2.5 });
    const auto kicks = ps::notesOf (b, kick);
    REQUIRE (kicks.size() == 8);
    // Half of them: the library's sparser version of this beat (1 and 3) stays.
    const auto half = ps::withAmount (kicks, kick, -0.5, b, 4.0, &model);
    REQUIRE (half.size() == 4);
    for (const auto beat : { 0.0, 2.0, 4.0, 6.0 })
        CHECK (hasNoteAt (half, beat));
}

TEST_CASE ("AMOUNT 0.54.0: with nothing alike learned, the rules decide as before", "[playstyle][amount]")
{
    const auto model = habitLibrary();
    // A 7/4 bar: nothing learned of that length.
    auto b = blockOf ({ 0.0, 2.0 });
    const auto kicks = ps::notesOf (b, kick);
    CHECK (ps::withAmount (kicks, kick, 0.3, b, 7.0, &model) == ps::withAmount (kicks, kick, 0.3, b, 7.0));
    CHECK_FALSE (ps::isLearned (kicks, kick, b, 7.0, &model));
    // An instrument no drum group of the library covers (the tambourine): the rules.
    const auto tambourine = synth::drums::pieceOfSlot (TMB);
    REQUIRE (tambourine >= 0);
    CHECK (ps::withAmount ({}, tambourine, 0.3, b, 4.0, &model) == ps::withAmount ({}, tambourine, 0.3, b, 4.0));
    // An empty library: the rules.
    ps::AmountModel empty;
    CHECK (ps::withAmount (kicks, kick, 0.3, b, 4.0, &empty) == ps::withAmount (kicks, kick, 0.3, b, 4.0));
}

TEST_CASE ("AMOUNT 0.54.0: the other drums count: a kick learns from bars whose snare and hats are like this one's", "[playstyle][amount]")
{
    // Two habits: with hats on the eighths drummers add the kick on 1.75; with no hats, on 3.5.
    ps::AmountModel m;
    for (int i = 0; i < 30; ++i)
    {
        m.add (groove ({ 0.0, 2.0 }), false, false);
        m.add (groove ({ 0.0, 1.75, 2.0 }), false, false);
        m.add (groove ({ 0.0, 2.0 }, false), false, false);
        m.add (groove ({ 0.0, 2.0, 3.5 }, false), false, false);
    }
    m.finish();
    const auto withHats = blockOf ({ 0.0, 2.0 });
    auto noHats = withHats;
    std::erase_if (noHats.hits, [] (const song::Hit& h) { return h.slot == CH; });
    const auto one = 2.0 / 28.0;   // one more kick a bar
    const auto a = ps::withAmount (ps::notesOf (withHats, kick), kick, one, withHats, 4.0, &m);
    const auto c = ps::withAmount (ps::notesOf (noHats, kick), kick, one, noHats, 4.0, &m);
    CHECK (hasNoteAt (a, 1.75));
    CHECK_FALSE (hasNoteAt (a, 3.5));
    CHECK (hasNoteAt (c, 3.5));
    CHECK_FALSE (hasNoteAt (c, 1.75));
}

TEST_CASE ("AMOUNT 0.54.0: an instrument the groove doesn't play gets what the library plays with the rest", "[playstyle][amount]")
{
    // Library: grooves like this beat with a crash on 1 of each bar.
    ps::AmountModel m;
    for (int i = 0; i < 40; ++i)
    {
        auto r = groove ({ 0.0, 2.0 });
        r.add ((int) Lane::crash, 0.0);
        r.add ((int) Lane::crash, 4.0);
        m.add (r, false, false);
    }
    m.finish();
    const auto b = blockOf ({ 0.0, 2.0 });
    const auto crash = synth::drums::pieceOfSlot (CY);
    const auto two = ps::withAmount ({}, crash, 2.0 / 32.0, b, 4.0, &m);
    REQUIRE (two.size() == 2);
    CHECK (hasNoteAt (two, 0.0));
    CHECK (hasNoteAt (two, 4.0));
}

TEST_CASE ("AMOUNT 0.54.0: learned from the library's grooves, again when they change", "[playstyle][amount]")
{
    std::vector<grooves::Entry> entries;
    for (int i = 0; i < 40; ++i)
    {
        grooves::Entry e;
        e.rhythm = std::make_shared<grooves::Rhythm> (groove ({ 0.0, (i % 2) != 0 ? 1.75 : 0.0, 2.0 }));
        entries.push_back (e);
    }
    grooves::Entry fill;
    fill.part = grooves::Part::fill;
    fill.rhythm = std::make_shared<grooves::Rhythm> (groove ({ 0.0, 0.25, 0.5 }));
    entries.push_back (fill);
    entries.push_back (grooves::Entry {});   // no rhythm: skipped
    ps::AmountModel m;
    m.learn (entries);
    CHECK (m.numBars() == 41);   // each groove's one different bar (the same bar in two grooves counts twice: a habit), the fill's
}
