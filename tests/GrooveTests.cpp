// THE GROOVE PLAYER (PR7-D 0.4.0): reading a MIDI groove and what it tells (tempo, time signature, bars, power hand,
// intensity); the file dragged out. The grooves here are made in the test, as a groove pack's files are written.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grooves/Groove.h"
#include "grooves/GrooveLibrary.h"

using Catch::Matchers::WithinAbs;

namespace
{
    using namespace synth::drums::notemap;

    struct Note { double beat; int note; int velocity = 100; };

    // A MIDI file as groove packs write them: 480 ticks a beat, tempo and time signature on the first track (or none).
    juce::MemoryBlock midiFile (const std::vector<Note>& notes, double bpm = 0.0, int num = 0, int den = 0, double endBeat = -1.0,
                                int ticks = 480)
    {
        juce::MidiMessageSequence track;
        if (bpm > 0.0)
            track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
        if (num > 0)
            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (num, den));
        for (const auto& n : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, n.note, (juce::uint8) n.velocity).withTimeStamp (n.beat * ticks));
            track.addEvent (juce::MidiMessage::noteOff (10, n.note).withTimeStamp (n.beat * ticks + ticks / 4));
        }
        if (endBeat >= 0.0)
        {
            auto end = juce::MidiMessage::endOfTrack();
            end.setTimeStamp (endBeat * ticks);
            track.addEvent (end);
        }
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticks);
        file.addTrack (track);
        juce::MemoryOutputStream out;
        file.writeTo (out);
        return out.getMemoryBlock();
    }

    std::optional<grooves::Groove> readBlock (const juce::MemoryBlock& block, const juce::String& path = "/grooves/Rock/Verse 01.mid",
                                              const Map& layout = grooveLibrary())
    {
        juce::MemoryInputStream in (block, false);
        return grooves::read (in, juce::File (path), layout);
    }

    // A 2-bar rock beat: kick on 1 and 3, snare on 2 and 4, closed hat (42) on the eighths.
    std::vector<Note> rockBeat (int hat = 42, int bars = 2)
    {
        std::vector<Note> notes;
        for (int bar = 0; bar < bars; ++bar)
        {
            const auto b = bar * 4.0;
            notes.push_back ({ b, 36 });
            notes.push_back ({ b + 2.0, 36 });
            notes.push_back ({ b + 1.0, 38 });
            notes.push_back ({ b + 3.0, 38 });
            for (int e = 0; e < 8; ++e)
                notes.push_back ({ b + e * 0.5, hat, 80 });
        }
        return notes;
    }
}

TEST_CASE ("GROOVES: a groove's notes, tempo, time signature and bars from its file", "[grooves]")
{
    const auto g = readBlock (midiFile (rockBeat(), 96.0, 4, 4));
    REQUIRE (g.has_value());
    CHECK (g->name == "Verse 01");
    CHECK_THAT (g->bpm, WithinAbs (96.0, 0.01));
    CHECK (g->bpmKnown);
    CHECK (g->signature() == "4/4");
    CHECK (g->bars == 2);
    CHECK_THAT (g->lengthBeats, WithinAbs (8.0, 1e-9));
    REQUIRE (g->hits.size() == 24);
    CHECK_THAT (g->hits.front().beat, WithinAbs (0.0, 1e-9));
    CHECK (std::is_sorted (g->hits.begin(), g->hits.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; }));
    // A snare on beat 2 exactly (tick 480).
    const auto snare = std::find_if (g->hits.begin(), g->hits.end(), [] (const auto& h) { return h.note == 38; });
    CHECK_THAT (snare->beat, WithinAbs (1.0, 1e-9));
    CHECK_THAT (snare->velocity, WithinAbs (100.0 / 127.0, 1e-6));
}

TEST_CASE ("GROOVES: bars count from where hits start; a closing crash on the next bar line ends the last bar", "[grooves]")
{
    auto fill = rockBeat (42, 1);
    fill.push_back ({ 4.0, 49 });   // the crash on the next downbeat
    CHECK (readBlock (midiFile (fill, 120.0, 4, 4))->bars == 1);
    fill.push_back ({ 4.5, 42 });   // a hit past it: a second bar
    CHECK (readBlock (midiFile (fill, 120.0, 4, 4))->bars == 2);
    // A groove that ends in a rest: the track's end marks its length.
    CHECK (readBlock (midiFile (rockBeat (42, 1), 120.0, 4, 4, 16.0))->bars == 4);
    // 6/8: three quarter notes a bar.
    std::vector<Note> sixEight;
    for (int e = 0; e < 12; ++e)
        sixEight.push_back ({ e * 0.5, 42 });
    const auto g = readBlock (midiFile (sixEight, 120.0, 6, 8));
    CHECK (g->signature() == "6/8");
    CHECK_THAT (g->beatsPerBar(), WithinAbs (3.0, 1e-9));
    CHECK (g->bars == 2);
}

TEST_CASE ("GROOVES: no tempo in the file: the one in its name or its folder's; else 120 and not known", "[grooves]")
{
    const auto plain = midiFile (rockBeat());
    CHECK_THAT (readBlock (plain, "/grooves/Funky 16ths 102 BPM/Verse 01.mid")->bpm, WithinAbs (102.0, 1e-9));
    CHECK_THAT (readBlock (plain, "/grooves/Pack/Fill 140bpm.mid")->bpm, WithinAbs (140.0, 1e-9));
    const auto none = readBlock (plain, "/grooves/Pack/Verse.mid");
    CHECK_THAT (none->bpm, WithinAbs (120.0, 1e-9));
    CHECK_FALSE (none->bpmKnown);
    CHECK (none->signature() == "4/4");   // no time signature: 4/4
    // The file's own tempo wins over the name's.
    CHECK_THAT (readBlock (midiFile (rockBeat(), 88.0), "/grooves/Funky 16ths 102 BPM/Verse 01.mid")->bpm, WithinAbs (88.0, 0.01));

    CHECK (grooves::tempoInName ("Laid Back Ballad 55 BPM") == 55.0);
    CHECK (grooves::tempoInName ("Bpm Verse 2") == 0.0);
    CHECK (grooves::tempoInName ("9000 BPM") == 0.0);
}

TEST_CASE ("GROOVES: the power hand is what the riding hand plays most, through the folder's layout", "[grooves]")
{
    using grooves::PowerHand;
    CHECK (readBlock (midiFile (rockBeat (42)))->hand == PowerHand::closedHat);
    CHECK (readBlock (midiFile (rockBeat (22)))->hand == PowerHand::closedHat);   // the layout's closed edge (the shoulder)
    CHECK (readBlock (midiFile (rockBeat (46)))->hand == PowerHand::openHat);
    CHECK (readBlock (midiFile (rockBeat (51)))->hand == PowerHand::ride);
    CHECK (readBlock (midiFile (rockBeat (49)))->hand == PowerHand::crash);
    CHECK (readBlock (midiFile (rockBeat (54)), "/x/Verse.mid", generalMidi())->hand == PowerHand::percussion);   // GM's tambourine
    // Through General MIDI, 22 is nothing: the hand isn't known.
    CHECK (readBlock (midiFile (rockBeat (22)), "/x/Verse.mid", generalMidi())->hand == PowerHand::none);
    // Kick and snare only: none.
    CHECK (readBlock (midiFile ({ { 0.0, 36 }, { 1.0, 38 } }))->hand == PowerHand::none);
}

TEST_CASE ("GROOVES: intensity rises with how hard and how busy", "[grooves]")
{
    auto soft = rockBeat();
    for (auto& n : soft)
        n.velocity = 40;
    auto busy = rockBeat();
    for (int s = 0; s < 32; ++s)
        busy.push_back ({ s * 0.25 + 0.125, 42, 80 });   // 32nds on the hat
    const auto a = readBlock (midiFile (soft))->intensity;
    const auto b = readBlock (midiFile (rockBeat()))->intensity;
    const auto c = readBlock (midiFile (busy))->intensity;
    INFO ("soft " << a << ", plain " << b << ", busy " << c);
    CHECK (a < b);
    CHECK (b < c);
    CHECK (c <= 1.0f);
}

TEST_CASE ("GROOVES: files that aren't grooves are left out", "[grooves]")
{
    CHECK_FALSE (readBlock (midiFile ({}, 120.0, 4, 4)).has_value());   // no notes
    juce::MemoryBlock junk ("not a midi file", 15);
    CHECK_FALSE (readBlock (junk).has_value());
}

TEST_CASE ("GROOVES: the dragged file: the tempo it's given, velocities scaled, notes as noteFor says", "[grooves]")
{
    const auto g = readBlock (midiFile (rockBeat(), 96.0, 4, 4));
    const auto file = grooves::toMidi (*g, 144.0, 0.5f, [] (int note) { return note == 38 ? -1 : note + 1; });
    juce::MemoryOutputStream out;
    REQUIRE (file.writeTo (out));
    const auto back = readBlock (out.getMemoryBlock());
    REQUIRE (back.has_value());
    CHECK_THAT (back->bpm, WithinAbs (144.0, 0.01));
    CHECK (back->bars == 2);
    CHECK (back->hits.size() == 20);   // the snares left out
    for (const auto& h : back->hits)
    {
        CHECK ((h.note == 37 || h.note == 43));
        const auto original = h.note == 37 ? 100 : 80;
        CHECK_THAT (h.velocity * 127.0f, WithinAbs (std::round (original * 0.5), 0.6));
    }
    CHECK_THAT (back->hits[1].beat, WithinAbs (0.0, 1e-9));   // the same beats: only the tempo changed
    CHECK_THAT (back->hits.back().beat, WithinAbs (7.5, 1e-9));
}

namespace
{
    // A folder of grooves as a pack lays them out: Pack / Rock 96 BPM / {Verse 01, Fill 01}, Pack / Ballads / 6-8 /
    // Verse 01, Pack / Ride Beat; a text file and an empty MIDI file that aren't grooves.
    struct TempGrooves
    {
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonGrooveTests");
        juce::File pack = root.getChildFile ("Pack"), settings = root.getChildFile ("Grooves.xml"), index = root.getChildFile ("Groove Index.xml");
        TempGrooves()
        {
            root.deleteRecursively();
            write ("Rock 96 BPM/Verse 01.mid", midiFile (rockBeat()));
            write ("Rock 96 BPM/Fill 01.mid", midiFile (rockBeat (42, 1)));
            std::vector<Note> sixEight;
            for (int e = 0; e < 12; ++e)
                sixEight.push_back ({ e * 0.5, 42 });
            write ("Ballads/6-8/Verse 01.mid", midiFile (sixEight, 60.0, 6, 8));
            write ("Ride Beat.mid", midiFile (rockBeat (51), 140.0, 4, 4));
            write ("notes.txt", juce::MemoryBlock ("hello", 5));
            write ("Empty.mid", midiFile ({}, 120.0, 4, 4));
        }
        ~TempGrooves() { root.deleteRecursively(); }
        void write (const juce::String& path, const juce::MemoryBlock& data)
        {
            const auto f = pack.getChildFile (path);
            f.getParentDirectory().createDirectory();
            REQUIRE (f.replaceWithData (data.getData(), data.getSize()));
        }
    };

    juce::StringArray names (const std::vector<grooves::Entry>& entries)
    {
        juce::StringArray result;
        for (const auto& e : entries)
            result.add (e.family + "/" + e.name);
        return result;
    }
}

TEST_CASE ("GROOVE LIBRARY: a linked folder's MIDI grooves, read with their tags; other files left out", "[grooves]")
{
    TempGrooves t;
    grooves::Library library (t.settings, t.index);
    REQUIRE (library.addFolder (t.pack));
    CHECK_FALSE (library.addFolder (t.pack));                          // once
    CHECK_FALSE (library.addFolder (t.pack.getChildFile ("Ballads")));  // inside it: listed already
    library.scanNow();
    const auto all = library.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4);
    CHECK (names (all) == juce::StringArray { "6-8/Verse 01", "Pack/Ride Beat", "Rock 96 BPM/Fill 01", "Rock 96 BPM/Verse 01" });
    const auto verse = std::find_if (all.begin(), all.end(), [] (const auto& e) { return e.family == "Rock 96 BPM" && e.name == "Verse 01"; });
    CHECK_THAT (verse->bpm, WithinAbs (96.0, 1e-9));   // from its folder's name
    CHECK (verse->bars == 2);
    CHECK (verse->hand == grooves::PowerHand::closedHat);
    CHECK (all[0].signature() == "6/8");
    CHECK (all[1].hand == grooves::PowerHand::ride);
}

TEST_CASE ("GROOVE LIBRARY: a folder as a filter, with or without its subfolders; words; favourites; sorting", "[grooves]")
{
    TempGrooves t;
    grooves::Library library (t.settings, t.index);
    library.addFolder (t.pack);
    library.scanNow();
    const auto q = [&] (const juce::File& within, const juce::String& text = {}, bool favourites = false,
                        grooves::Sort sort = grooves::Sort::name, double bpm = 120.0)
    {
        return names (library.query (within, text, favourites, sort, bpm, 4, 4));
    };
    CHECK (q (t.pack.getChildFile ("Ballads")) == juce::StringArray { "6-8/Verse 01" });   // and its subfolder's
    library.setSubfolders (false);
    CHECK (q (t.pack.getChildFile ("Ballads")).isEmpty());                                // only its own files
    CHECK (q (t.pack) == juce::StringArray { "Pack/Ride Beat" });
    library.setSubfolders (true);

    CHECK (q ({}, "verse") == juce::StringArray { "6-8/Verse 01", "Rock 96 BPM/Verse 01" });
    CHECK (q ({}, "ROCK verse") == juce::StringArray { "Rock 96 BPM/Verse 01" });   // every word, in the name or folder
    CHECK (q ({}, "jazz").isEmpty());

    library.setFavourite (t.pack.getChildFile ("Ride Beat.mid"), true);
    CHECK (q ({}, {}, true) == juce::StringArray { "Pack/Ride Beat" });

    // Nearest the song's tempo first: 140 (the ride beat), then 96, 96, 60.
    CHECK (q ({}, {}, false, grooves::Sort::songTempo, 150.0)[0] == "Pack/Ride Beat");
    CHECK (q ({}, {}, false, grooves::Sort::songTempo, 50.0)[0] == "6-8/Verse 01");
    // The song's time signature first (6/8 here).
    CHECK (names (library.query ({}, {}, false, grooves::Sort::songSignature, 120.0, 6, 8))[0] == "6-8/Verse 01");
}

TEST_CASE ("GROOVE LIBRARY: folders, maps, favourites and the preview's settings are kept; what was read is remembered", "[grooves]")
{
    TempGrooves t;
    {
        grooves::Library library (t.settings, t.index);
        library.addFolder (t.pack);
        library.setLayout (0, synth::drums::notemap::Layout::generalMidi);
        library.setFavourite (t.pack.getChildFile ("Ride Beat.mid"), true);
        auto p = library.getPreview();
        p.factor = grooves::TempoFactor::twoThirds;
        p.velocity = 0.5f;
        p.songTempo = false;
        library.setPreview (p);
        library.scanNow();
    }
    grooves::Library again (t.settings, t.index);
    REQUIRE (again.getFolders().size() == 1);
    CHECK (again.getFolders()[0].folder == t.pack);
    CHECK (again.getFolders()[0].layout == synth::drums::notemap::Layout::generalMidi);
    CHECK (again.isFavourite (t.pack.getChildFile ("Ride Beat.mid")));
    CHECK (again.getPreview().factor == grooves::TempoFactor::twoThirds);
    CHECK_THAT (again.getPreview().velocity, WithinAbs (0.5, 1e-6));
    CHECK_FALSE (again.getPreview().songTempo);
    CHECK (again.getNumEntries() == 0);   // nothing read when made (most PR7-Ds never open GROOVES)...
    again.loadIndex();
    CHECK (again.getNumEntries() == 4);   // ...listed from the index on opening, before any scan

    // A file changed on disk (its size or date) is read again; the others come from the index. (Dates count to the
    // second on some systems: a real edit comes later than a minute after the last scan.)
    t.write ("Ride Beat.mid", midiFile (rockBeat (51), 70.0, 4, 4));
    t.pack.getChildFile ("Ride Beat.mid").setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::minutes (1));
    again.scanNow();
    const auto all = again.query ({}, "ride", false, grooves::Sort::name, 120.0, 4, 4);
    REQUIRE (all.size() == 1);
    CHECK_THAT (all[0].bpm, WithinAbs (70.0, 0.01));

    again.removeFolder (0);
    again.scanNow();
    CHECK (again.getNumEntries() == 0);
}

TEST_CASE ("GROOVE LIBRARY: the scan runs in the background", "[grooves]")
{
    TempGrooves t;
    grooves::Library library (t.settings, t.index);
    library.addFolder (t.pack);   // starts one
    for (int i = 0; i < 500 && library.isScanning(); ++i)
        juce::Thread::sleep (10);
    CHECK_FALSE (library.isScanning());
    CHECK (library.getNumEntries() == 4);
}

TEST_CASE ("GROOVE LIBRARY 0.15.2: a second PR7-D in the same session lists what was read and doesn't check the folders again", "[grooves]")
{
    // Every PR7-D added to a song has its own library; before 0.15.2 each one checked every file again when its
    // GROOVES page opened (10,000 files in the owner's folders).
    TempGrooves t;
    const auto waitFor = [] (grooves::Library& l)
    {
        for (int i = 0; i < 500 && l.isScanning(); ++i)
            juce::Thread::sleep (10);
    };
    {
        grooves::Library first (t.settings, t.index);
        first.addFolder (t.pack);
        waitFor (first);
        first.scanNow();   // checked: nothing new, so nothing read again
        CHECK (first.getNumScanned() == 0);
        CHECK (first.getNumEntries() == 4);
    }

    // A groove changes on disk after that check.
    t.write ("Ride Beat.mid", midiFile (rockBeat (51), 70.0, 4, 4));
    t.pack.getChildFile ("Ride Beat.mid").setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::minutes (1));

    // The next PR7-D's page opening: listed from what was read (in the background), no check this session.
    grooves::Library second (t.settings, t.index);
    CHECK (second.getNumEntries() == 0);
    second.scan (true);
    waitFor (second);
    CHECK (second.getNumEntries() == 4);
    CHECK (second.getNumScanned() == 0);
    const auto ride = [&second]
    {
        const auto found = second.query ({}, "ride", false, grooves::Sort::name, 120.0, 4, 4);
        return found.empty() ? 0.0 : found[0].bpm;
    };
    CHECK_THAT (ride(), ! WithinAbs (70.0, 0.01));   // the change not seen yet...
    second.scan (true);
    waitFor (second);
    CHECK (second.getNumScanned() == 0);             // ...and not looked for again

    // REFRESH: the folders checked; only the changed file read.
    second.scan();
    waitFor (second);
    CHECK (second.getNumScanned() == 1);
    CHECK_THAT (ride(), WithinAbs (70.0, 0.01));

    // Folders changed: a check is needed again (a third PR7-D with the folder's map changed checks it).
    second.setLayout (0, synth::drums::notemap::Layout::generalMidi);
    waitFor (second);
    grooves::Library third (t.settings, t.index);
    third.scan (true);
    waitFor (third);
    CHECK (third.getNumEntries() == 4);
}

TEST_CASE ("GROOVE LIBRARY 0.15.2: a check that finds nothing new leaves the index as it was", "[grooves]")
{
    TempGrooves t;
    grooves::Library library (t.settings, t.index);
    library.addFolder (t.pack);
    for (int i = 0; i < 500 && library.isScanning(); ++i)
        juce::Thread::sleep (10);
    REQUIRE (t.index.existsAsFile());
    const auto earlier = juce::Time::getCurrentTime() - juce::RelativeTime::hours (1);
    t.index.setLastModificationTime (earlier);
    library.scanNow();
    CHECK (library.getNumScanned() == 0);
    CHECK (std::abs (t.index.getLastModificationTime().toMilliseconds() - earlier.toMilliseconds()) < 2000);   // not rewritten
    // A file removed: written again, without it.
    t.pack.getChildFile ("Ride Beat.mid").deleteFile();
    library.scanNow();
    CHECK (library.getNumEntries() == 3);
    CHECK (t.index.getLastModificationTime() > earlier + juce::RelativeTime::minutes (30));
}

TEST_CASE ("GROOVES 0.5: FEEL from where the off-beat hits fall; TYPE from the names", "[grooves][filters]")
{
    // Straight eighths and sixteenths: straight. A shuffle (the hat on the beat and two thirds of it): swung.
    CHECK (readBlock (midiFile (rockBeat()))->feel == grooves::Feel::straight);
    std::vector<Note> shuffle { { 0.0, 36 }, { 1.0, 38 } };
    for (int b = 0; b < 4; ++b)
    {
        shuffle.push_back ({ b * 1.0, 42 });
        shuffle.push_back ({ b + 2.0 / 3.0, 42 });
    }
    CHECK (readBlock (midiFile (shuffle))->feel == grooves::Feel::swung);
    // A beat with one stray triplet note stays straight.
    auto almost = rockBeat();
    almost.push_back ({ 1.0 / 3.0, 38 });
    CHECK (readBlock (midiFile (almost))->feel == grooves::Feel::straight);

    using grooves::Part;
    CHECK (grooves::partIn ("Verse 02") == Part::verse);
    CHECK (grooves::partIn ("Pre-Chorus 01") == Part::preChorus);   // not CHORUS
    CHECK (grooves::partIn ("pre_chorus") == Part::preChorus);
    CHECK (grooves::partIn ("Chorus") == Part::chorus);
    CHECK (grooves::partIn ("Fills") == Part::fill);
    CHECK (grooves::partIn ("Outro") == Part::ending);
    CHECK (grooves::partIn ("Count-In 2 bars") == Part::countIn);
    CHECK (grooves::partIn ("Country Shuffle") == Part::beat);       // "Country" isn't a count-in
    CHECK (grooves::partIn ("Groove 3") == Part::beat);
    // 0.8.1, from a check against real groove packs' labels: FILL before the song's sections; BREAK a fill as a word of
    // its own (a number after it or not), not inside another.
    CHECK (grooves::partIn ("Alien Introfill 2") == Part::fill);
    CHECK (grooves::partIn ("Verse Fill 02") == Part::fill);
    CHECK (grooves::partIn ("Pre-Chorus Fill") == Part::fill);
    CHECK (grooves::partIn ("909 Break 2") == Part::fill);
    CHECK (grooves::partIn ("606 Clean Break") == Part::fill);
    CHECK (grooves::partIn ("Break3") == Part::fill);
    CHECK (grooves::partIn ("Breakdown 1") == Part::beat);
    CHECK (grooves::partIn ("Breakbeat 120") == Part::beat);
    CHECK (grooves::partIn ("Breaks") == Part::beat);
    CHECK (grooves::partIn ("Jailbreak") == Part::beat);
    CHECK (grooves::partIn ("Intro 01") == Part::intro);
    // From the file's name, else its folder's, else the one above.
    CHECK (grooves::partOf (juce::File ("/p/Rock/Fills/Var 1.mid")) == Part::fill);
    CHECK (grooves::partOf (juce::File ("/p/Rock/Bridge/Fast/Var 1.mid")) == Part::bridge);
    CHECK (grooves::partOf (juce::File ("/p/Rock/Intro 01.mid")) == Part::intro);
}

TEST_CASE ("GROOVE LIBRARY 0.5: filter columns: any of the values chosen in a column, none excluded, all columns together", "[grooves][filters]")
{
    TempGrooves t;
    // Plus a swung fill at 150, and a verse with no tempo.
    std::vector<Note> shuffle;
    for (int b = 0; b < 4; ++b)
    {
        shuffle.push_back ({ b * 1.0, 42 });
        shuffle.push_back ({ b + 2.0 / 3.0, 42 });
    }
    t.write ("Swing/Fill 02.mid", midiFile (shuffle, 150.0, 4, 4));
    t.write ("Loose/Verse 03.mid", midiFile (rockBeat()));
    grooves::Library library (t.settings, t.index);
    library.addFolder (t.pack);
    library.scanNow();
    using grooves::Column;
    const auto q = [&] (const grooves::Filters& f)
    {
        return names (library.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4, f));
    };
    const auto all = library.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4);
    REQUIRE (all.size() == 6);
    const auto value = [&] (const juce::String& family, const juce::String& name, Column c)
    {
        for (const auto& e : all)
            if (e.family == family && e.name == name)
                return grooves::valueOf (e, c);
        return juce::String ("?");
    };
    // SECTION: from the names (as TYPE was before 0.16.0).
    CHECK (value ("Rock 96 BPM", "Verse 01", Column::section) == "Verse");
    CHECK (value ("Rock 96 BPM", "Fill 01", Column::section) == "Fill");
    CHECK (value ("Pack", "Ride Beat", Column::section) == "Beat");
    // TYPE (0.16.0), a MIDI file's: how it's felt and what it is, both.
    CHECK (value ("Swing", "Fill 02", Column::type) == "Swing, Fill");
    CHECK (value ("Rock 96 BPM", "Verse 01", Column::type) == "Straight, Beat");
    CHECK (value ("Rock 96 BPM", "Verse 01", Column::genre) == "");   // only groove databases have genres
    CHECK (value ("6-8", "Verse 01", Column::time) == "6/8");
    CHECK (value ("Pack", "Ride Beat", Column::hand) == "Ride");
    CHECK (value ("Rock 96 BPM", "Verse 01", Column::tempo) == "80-99");
    CHECK (value ("Pack", "Ride Beat", Column::tempo) == "140-159");
    CHECK (value ("6-8", "Verse 01", Column::tempo) == "Below 80");
    CHECK (value ("Loose", "Verse 03", Column::tempo) == "Unknown");

    grooves::Filters f;
    f.include[(size_t) Column::section] = { "Fill" };
    CHECK (q (f) == juce::StringArray { "Rock 96 BPM/Fill 01", "Swing/Fill 02" });
    f.include[(size_t) Column::section].add ("Verse");   // any of them
    CHECK (q (f).size() == 5);
    f.exclude[(size_t) Column::time] = { "6/8" };        // none excluded
    CHECK (q (f).size() == 4);
    f.include[(size_t) Column::type] = { "Swing" };      // and every column
    CHECK (q (f) == juce::StringArray { "Swing/Fill 02" });
    // TYPE: every value chosen there (its values say different things).
    grooves::Filters t2;
    t2.include[(size_t) Column::type] = { "Straight", "Fill" };
    CHECK (q (t2) == juce::StringArray { "Rock 96 BPM/Fill 01" });
    CHECK_FALSE (f.isEmpty());
    CHECK (grooves::Filters().isEmpty());
}

TEST_CASE ("GROOVES 0.30.0: the values worked out once when listed are the ones worked out each time", "[grooves]")
{
    using namespace grooves;
    std::vector<Entry> list;
    const char* names[] = { "Verse 2", "verse 10", "Chorus", "Fill 1", "Ending", "Intro" };
    for (int i = 0; i < 6; ++i)
    {
        Entry e;
        e.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Pack/Song " + juce::String (i % 2) + "/" + names[i] + ".mid");
        e.name = names[i];
        e.family = "Song " + juce::String (i % 2);
        e.bpm = 70.0 + 30.0 * i;
        e.bpmKnown = i != 3;
        e.hand = (PowerHand) (i % 3);
        e.feel = i % 2 == 0 ? Feel::straight : Feel::swung;
        e.part = partOf (e.file);
        if (i == 2)
            e.tags = { "Genre:Pop/Rock", "Type:Half Time", "Power Hand:Ride Crash" };
        list.push_back (e);
    }
    auto cached = list;
    cacheEntries (cached);
    Filters filters;
    filters.include[(size_t) Column::section] = { "Verse", "Chorus" };
    filters.exclude[(size_t) Column::tempo] = { "Below 80" };
    for (size_t i = 0; i < list.size(); ++i)
    {
        REQUIRE (cached[i].cached != nullptr);
        for (int c = 0; c < numColumns; ++c)
        {
            juce::StringArray scratch;
            CHECK (valuesIn (cached[i], (Column) c, scratch) == valuesOf (list[i], (Column) c));
            CHECK (valueOf (cached[i], (Column) c) == valueOf (list[i], (Column) c));
        }
        CHECK (filters.passes (cached[i]) == filters.passes (list[i]));
    }
    // The name order: family, then name, numbers in order ("verse 10" after "Verse 2").
    std::vector<juce::String> byRank (list.size());
    for (const auto& e : cached)
        byRank[(size_t) e.nameRank] = e.family + "/" + e.name;
    CHECK (byRank == std::vector<juce::String> { "Song 0/Chorus", "Song 0/Ending", "Song 0/Verse 2", "Song 1/Fill 1", "Song 1/Intro", "Song 1/verse 10" });
}
