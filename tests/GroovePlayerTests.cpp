// THE GROOVE PLAYER's preview (PR7-D 0.4.0): a groove played through the kit, its hits on the samples its tempo puts
// them (the song's, or its own, times the factor), looping or once, its velocities scaled, its notes through the
// folder's map; and the file dragged out.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

#include <set>
#include "ui/GroovesPage.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;

    struct Plugin
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonGroovePlayerTests");
        ParagonX1AudioProcessor p;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (p);   // (0.59.0: ROOT's keys, as these tests play them)
            // The kick's and closed hat's slots play a short, steady burst (20 ms, full from its first sample): each hit
            // shows where it starts.
            const auto burst = testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate);
            testkit::useSample (p, BD, burst);
            testkit::useSample (p, CH, burst);
        }
        ~Plugin() { folder.deleteRecursively(); }

        void preview (bool songTempo, grooves::TempoFactor factor, float velocity = 1.0f)
        {
            auto s = p.getGrooveLibrary().getPreview();
            s.songTempo = songTempo;
            s.factor = factor;
            s.velocity = velocity;
            p.getGrooveLibrary().setPreview (s);
            p.applyGroovePreview();
        }

        std::vector<float> play (double seconds)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer none;
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
            {
                buffer.clear();
                p.processBlock (buffer, none);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };

    // A groove: `notes` (beat, note) at `bpm`, `bars` 4/4 bars.
    grooves::Groove groove (std::vector<std::pair<double, int>> notes, double bpm = 90.0, int bars = 1, float velocity = 1.0f)
    {
        grooves::Groove g;
        g.name = "Test";
        g.file = juce::File ("/grooves/Test.mid");
        g.bpm = g.bpmKnown ? bpm : bpm;
        g.bars = bars;
        g.lengthBeats = bars * 4.0;
        for (const auto& [beat, note] : notes)
            g.hits.push_back ({ beat, note, velocity });
        return g;
    }

    // Where each burst starts (a sample after at least 5 ms of silence).
    std::vector<int> onsets (const std::vector<float>& x)
    {
        std::vector<int> result;
        int quiet = 1 << 20;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            if (std::abs (x[(size_t) i]) > 1.0e-6f)
            {
                if (quiet >= 240)
                    result.push_back (i);
                quiet = 0;
            }
            else
                ++quiet;
        }
        return result;
    }

    float peak (const std::vector<float>& x)
    {
        auto p = 0.0f;
        for (const auto v : x)
            p = std::max (p, std::abs (v));
        return p;
    }
}

TEST_CASE ("GROOVE PREVIEW: looping at the song's tempo, each hit on its sample", "[grooves][groove-player]")
{
    Plugin t;
    t.preview (true, grooves::TempoFactor::normal);
    // Kick on 1, 2 and 3.5 of a one-bar groove; the song at 120 (the host's default here): a beat is 24000 samples.
    t.p.playGroove (groove ({ { 0.0, 36 }, { 1.0, 36 }, { 2.5, 36 } }), 0, true);
    const auto out = t.play (4.5);
    // Rounded down to the sample, as the sequencer's: 2.5 beats is 60000 exactly.
    CHECK (onsets (out) == std::vector<int> { 0, 24000, 60000, 96000, 120000, 156000, 192000, 216000 });
    CHECK (t.p.getGroovePosition() >= 0.0);
}

TEST_CASE ("GROOVE PREVIEW: its own tempo, and the factors", "[grooves][groove-player]")
{
    const auto firstGap = [] (bool songTempo, grooves::TempoFactor factor)
    {
        Plugin t;
        t.preview (songTempo, factor);
        t.p.playGroove (groove ({ { 0.0, 36 }, { 1.0, 36 } }, 90.0), 0, true);
        const auto o = onsets (t.play (1.5));
        REQUIRE (o.size() >= 2);
        return o[1] - o[0];
    };
    CHECK (firstGap (true, grooves::TempoFactor::normal) == 24000);       // 120: the song's
    CHECK (firstGap (false, grooves::TempoFactor::normal) == 32000);      // 90: its own
    CHECK (firstGap (true, grooves::TempoFactor::twice) == 12000);
    CHECK (firstGap (true, grooves::TempoFactor::half) == 48000);
    CHECK (firstGap (true, grooves::TempoFactor::twoThirds) == 36000);
    CHECK (firstGap (true, grooves::TempoFactor::threeHalves) == 16000);
}

TEST_CASE ("GROOVE PREVIEW: once through (with a closing crash on the next bar line), then it stops", "[grooves][groove-player]")
{
    Plugin t;
    t.preview (true, grooves::TempoFactor::twice);   // a bar a second
    t.p.playGroove (groove ({ { 0.0, 36 }, { 3.0, 36 }, { 4.0, 36 } }), 0, false);
    const auto out = t.play (3.0);
    CHECK (onsets (out) == std::vector<int> { 0, 36000, 48000 });   // no second time round
    CHECK (t.p.getGroovePosition() < 0.0);
}

TEST_CASE ("GROOVE PREVIEW: the velocity scale, and STOP", "[grooves][groove-player]")
{
    const auto level = [] (float velocity)
    {
        Plugin t;
        t.preview (true, grooves::TempoFactor::normal, velocity);
        t.p.playGroove (groove ({ { 0.0, 36 } }), 0, true);
        return peak (t.play (0.1));
    };
    const auto full = level (1.0f), half = level (0.5f);
    INFO ("full " << full << ", half " << half);
    // Half the velocity: 12 dB down at the kit's VELOCITY 100% (as a key played half as hard).
    CHECK_THAT (juce::Decibels::gainToDecibels (half / full), WithinAbs (-12.04, 0.3));

    Plugin t;
    t.p.playGroove (groove ({ { 0.0, 36 }, { 2.0, 36 } }), 0, true);
    t.play (0.5);
    t.p.stopGroove();
    CHECK (onsets (t.play (2.0)).empty());
    CHECK (t.p.getGroovePosition() < 0.0);
}

TEST_CASE ("GROOVE PREVIEW: notes through the folder's map: 22 is the hats' closed edge in a groove library's files, nothing in GM's", "[grooves][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack"), gm = t.folder.getChildFile ("GM Pack");
    pack.createDirectory();
    gm.createDirectory();
    auto& library = t.p.getGrooveLibrary();
    REQUIRE (library.addFolder (pack));
    REQUIRE (library.addFolder (gm));
    library.setLayout (1, Layout::generalMidi);
    t.p.playGroove (groove ({ { 0.0, 22 } }), 0, false);
    t.p.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, blockSize), *std::make_unique<juce::MidiBuffer>());
    CHECK (t.p.getDrumKit().isSounding (HSH));   // 0.58.0: the closed edge (our SHOULDER)
    Plugin u;   // (the same temporary folder, made afresh)
    gm.createDirectory();
    REQUIRE (u.p.getGrooveLibrary().addFolder (gm));
    u.p.getGrooveLibrary().setLayout (0, Layout::generalMidi);
    u.p.playGroove (groove ({ { 0.0, 22 }, { 0.0, 42 } }), 0, false);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    u.p.processBlock (buffer, none);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        CHECK (u.p.getDrumKit().isSounding (k) == (k == CH));   // 42 the hat, 22 nothing
}

TEST_CASE ("GROOVE DRAG: the file on this kit's notes, or as written; velocity and factor in it", "[grooves][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    pack.createDirectory();
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    const auto g = groove ({ { 0.0, 36 }, { 1.0, 42 }, { 2.0, 53 }, { 3.0, 55 } });
    const auto notesIn = [&t, &g]
    {
        const auto file = t.p.grooveDragFile (g, 0);
        REQUIRE (file.existsAsFile());
        const auto read = grooves::read (file, generalMidi());
        REQUIRE (read.has_value());
        return read;
    };
    // The kit on ROOT (C2): each drum on its key there: kick 36, closed hat 42, ride bell 49, splash 52.
    auto r = notesIn();
    REQUIRE (r->hits.size() == 4);
    CHECK (r->hits[0].note == 36);
    CHECK (r->hits[1].note == 36 + CH);
    CHECK (r->hits[2].note == 36 + RB);
    CHECK (r->hits[3].note == 36 + SPL);
    // The kit on the groove libraries' map, as the folder: as written (every note kept as it is).
    testkit::set (t.p, ParamIDs::drumsMap, (float) Layout::grooveLibrary);
    r = notesIn();
    CHECK (r->hits[1].note == 42);
    CHECK (r->hits[3].note == 55);
    // 2x and half velocity: the notes twice as close, half as hard.
    testkit::set (t.p, ParamIDs::drumsMap, (float) Layout::root);
    t.preview (true, grooves::TempoFactor::twice, 0.5f);
    r = notesIn();
    CHECK_THAT (r->hits[1].beat, WithinAbs (0.5, 1e-9));
    CHECK_THAT (r->hits[3].beat, WithinAbs (1.5, 1e-9));
    CHECK_THAT (r->hits[0].velocity * 127.0f, WithinAbs (64.0, 0.6));
    // FOR THIS KIT off: as written, whatever the kit's map.
    auto s = t.p.getGrooveLibrary().getPreview();
    s.forThisKit = false;
    t.p.getGrooveLibrary().setPreview (s);
    CHECK (notesIn()->hits[1].note == 42);
}

TEST_CASE ("GROOVES page: the browser's columns lead to the folder chosen, which filters the list; back and forward retrace it", "[grooves][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    for (const auto* path : { "Rock 96 BPM/Verse 01.mid", "Rock 96 BPM/Fill 01.mid", "Funk/Verse 01.mid", "Ballads/Slow 68 BPM/Verse 01.mid" })
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
        track.addEvent (juce::MidiMessage::noteOff (10, 36).withTimeStamp (60));
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        const auto f = pack.getChildFile (path);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    }
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    CHECK (page.getResults().size() == 4);
    const auto labels = [] (const std::vector<GroovesPage::BrowserRow>& rows)
    {
        juce::StringArray out;
        for (const auto& r : rows)
            out.add ((r.heading ? "# " : "") + r.label);
        return out;
    };

    // The first column: USER MIDI (PR7-D's own) and the linked folder.
    auto columns = page.getBrowserColumns();
    REQUIRE (columns.size() == 1);
    CHECK (labels (columns[0]) == juce::StringArray { "# USER LIBRARIES", "User MIDI", "# LINKED FOLDERS", "Pack" });

    // The pack: its folders in the next column, two levels to it (those without folders under the pack's name, a
    // folder with folders as a heading over them).
    page.chooseFolder (pack);
    columns = page.getBrowserColumns();
    REQUIRE (columns.size() == 2);
    CHECK (labels (columns[1]) == juce::StringArray { "# Pack", "Funk", "Rock 96 BPM", "# Ballads", "Slow 68 BPM" });
    CHECK (page.getResults().size() == 4);
    page.chooseFolder (pack.getChildFile ("Rock 96 BPM"));
    CHECK (page.getResults().size() == 2);
    CHECK (page.getChips().contains ("FOLDER: Rock 96 BPM"));
    page.refresh();   // rebuilt: the same folder chosen
    CHECK (page.getFolder() == pack.getChildFile ("Rock 96 BPM"));
    CHECK (page.getResults().size() == 2);
    // A folder two levels down: chosen in the pack's column, under its heading.
    page.chooseFolder (pack.getChildFile ("Ballads/Slow 68 BPM"));
    CHECK (page.getResults().size() == 1);
    CHECK (page.getBrowserColumns().size() == 2);

    // Back and forward.
    CHECK (page.goBack());
    CHECK (page.getFolder() == pack.getChildFile ("Rock 96 BPM"));
    CHECK (page.goBack());
    CHECK (page.getFolder() == pack);
    CHECK (page.goForward());
    CHECK (page.getFolder() == pack.getChildFile ("Rock 96 BPM"));

    page.setSearch ("fill");
    CHECK (page.getResults().size() == 1);
    page.playRow (0, true);
    CHECK (t.p.getGrooveFile().getFileName() == "Fill 01.mid");

    // SHOW IN BROWSER: from everywhere, back to its folder, the groove chosen.
    page.clearFilters();
    CHECK (page.getFolder() == juce::File());
    CHECK (page.getResults().size() == 4);
    int slow = -1;
    for (int r = 0; r < (int) page.getResults().size(); ++r)
        if (page.getResults()[(size_t) r].family == "Slow 68 BPM")
            slow = r;
    REQUIRE (slow >= 0);
    page.showInBrowser (slow);
    CHECK (page.getFolder() == pack.getChildFile ("Ballads/Slow 68 BPM"));
    CHECK (page.getTable().getSelectedRow() == 0);

    // The window closing stops it.
    {
        GroovesPage another (t.p);
        another.refresh();
        another.playRow (0, true);
        CHECK (t.p.getGrooveFile() != juce::File());
    }
    CHECK (t.p.getGrooveFile() == juce::File());
}

TEST_CASE ("GROOVES page: USER MIDI takes MIDI files dropped on it; a folder can be left out of searches", "[grooves][groove-player][ui]")
{
    Plugin t;
    auto& library = t.p.getGrooveLibrary();
    const auto user = library.getUserFolder();
    REQUIRE (user >= 0);
    const auto userFolder = library.getFolders()[(size_t) user].folder;
    CHECK (userFolder == t.folder.getChildFile ("User MIDI"));
    CHECK (userFolder.isDirectory());

    // A pack linked after it: USER MIDI stays last; it can't be removed.
    const auto pack = t.folder.getChildFile ("Pack");
    pack.createDirectory();
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
    track.addEvent (juce::MidiMessage::noteOff (10, 36).withTimeStamp (60));
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (track);
    const auto outside = t.folder.getChildFile ("Elsewhere/Mine 01.mid");
    outside.getParentDirectory().createDirectory();
    for (const auto& f : { pack.getChildFile ("Beat.mid"), outside })
    {
        juce::FileOutputStream out (f);
        out.setPosition (0);
        out.truncate();
        midi.writeTo (out);
    }
    REQUIRE (library.addFolder (pack));
    CHECK (library.getFolders().back().user);
    library.removeFolder (library.getUserFolder());
    CHECK (library.getUserFolder() >= 0);
    library.scanNow();
    CHECK (library.getEntries().size() == 1);

    // Dropped onto it: copied in, read; out of step until then.
    CHECK_FALSE (library.needsSync (library.getUserFolder()));
    outside.copyFileTo (userFolder.getChildFile ("Copied by hand.mid"));
    CHECK (library.needsSync (library.getUserFolder()));   // (a file put there from outside: the orange dot)
    library.scanNow();
    CHECK_FALSE (library.needsSync (library.getUserFolder()));
    CHECK (library.addToUserFolder ({ outside.getFullPathName() }, userFolder) == 1);
    library.scanNow();
    CHECK (library.getEntries().size() == 3);
    CHECK (userFolder.getChildFile ("Mine 01.mid").existsAsFile());

    // LEAVE OUT OF SEARCHES: its grooves listed only when browsing inside it.
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    CHECK (page.getResults().size() == 3);
    library.setExcluded (library.getUserFolder(), true);
    page.refresh();
    CHECK (page.getResults().size() == 1);
    page.chooseFolder (userFolder);
    CHECK (page.getResults().size() == 2);
}

TEST_CASE ("GROOVES page: chips for every active filter, suggestions as you type, search with tags", "[grooves][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto write = [&pack] (const juce::String& path, std::vector<std::pair<double, int>> notes)
    {
        juce::MidiMessageSequence track;
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        const auto f = pack.getChildFile (path);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    write ("Rock/Verse 01.mid", { { 0.0, 36 }, { 0.5, 42 }, { 1.0, 38 }, { 1.5, 42 } });
    write ("Rock/Fill 01.mid", { { 0.0, 36 }, { 2.0, 38 }, { 3.0, 49 } });
    write ("Shuffle/Verse 02.mid", { { 0.0, 36 }, { 2.0 / 3.0, 42 }, { 1.0, 38 }, { 5.0 / 3.0, 42 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    using grooves::Column;

    // Chips: the folder, each filter (excluded ones NOT ...), the search; a click on one takes it away.
    page.chooseFolder (pack.getChildFile ("Rock"));
    page.toggleFilter (Column::section, "Verse", false);
    page.toggleFilter (Column::type, "Swing", true);
    CHECK (page.getChips() == juce::StringArray { "FOLDER: Rock", "SECTION: Verse", "NOT TYPE: Swing" });
    CHECK (page.getResults().size() == 1);
    page.removeChip (1);
    CHECK (page.getChips() == juce::StringArray { "FOLDER: Rock", "NOT TYPE: Swing" });
    CHECK (page.getResults().size() == 2);
    page.removeChip (0);
    CHECK (page.getFolder() == juce::File());
    // Values none of the grooves here has stay listed, greyed (counted 0).
    page.toggleFilter (Column::type, "Swing", true);
    page.chooseFolder (pack.getChildFile ("Rock"));
    CHECK (page.getColumnValues (Column::type).contains ("Swing (0)"));
    page.clearFilters();
    CHECK (page.getChips().isEmpty());

    // The preview changed from as played: a chip to reset it.
    page.setOriginalTempo (true);
    auto pr = t.p.getGrooveLibrary().getPreview();
    CHECK_FALSE (pr.songTempo);
    pr.factor = grooves::TempoFactor::twice;
    t.p.getGrooveLibrary().setPreview (pr);
    page.refresh();
    REQUIRE (page.getChips().size() == 1);
    CHECK (page.getChips()[0].startsWith ("TEMPO 2x"));
    page.removeChip (0);
    CHECK (t.p.getGrooveLibrary().getPreview().factor == grooves::TempoFactor::normal);
    CHECK (page.getChips().isEmpty());

    // Suggestions: filter values first, then grooves' names; one taken is a filter (and a chip).
    page.setSearch ("fil");
    CHECK (page.getResults().size() == 1);
    const auto suggested = page.getSuggestions();
    REQUIRE (suggested.size() >= 3);
    CHECK (suggested.contains ("TYPE: Fill"));
    CHECK (suggested.contains ("SECTION: Fill"));
    CHECK (suggested.contains ("Fill 01"));
    page.acceptSuggestion (suggested.indexOf ("TYPE: Fill"));
    CHECK (page.getChips() == juce::StringArray { "TYPE: Fill" });
    CHECK (page.getResults().size() == 1);
    page.clearFilters();

    // SEARCH WITH TAGS: a groove's values (TYPE, TIME, POWER HAND...) as the filters.
    int shuffle = -1;
    for (int r = 0; r < (int) page.getResults().size(); ++r)
        if (page.getResults()[(size_t) r].family == "Shuffle")
            shuffle = r;
    REQUIRE (shuffle >= 0);
    page.searchWithTags (shuffle);
    CHECK (page.getFilters().include[(size_t) Column::type] == juce::StringArray { "Swing", "Beat" });
    CHECK (page.getFilters().include[(size_t) Column::time] == juce::StringArray { "4/4" });
    REQUIRE (page.getResults().size() == 1);
    CHECK (page.getResults()[0].family == "Shuffle");

    // A filter column hidden and shown again; at least one stays.
    page.showFilterColumn (Column::genre, false);
    CHECK_FALSE (page.isFilterColumnShown (Column::genre));
    page.showFilterColumn (Column::genre, true);
    CHECK (page.isFilterColumnShown (Column::genre));
}

namespace
{
    struct Song final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (true);
            return info;
        }
    };
}

TEST_CASE ("GROOVE INTO THE SEQUENCER: a pattern with the groove's notes, played with the song, hits on the samples the groove has them", "[grooves][groove-player][to-pattern]")
{
    Plugin t;
    // A one-bar groove with an off-grid hit (a laid-back snare: 1.06 beats), soft hats, and a note no drum has (4).
    auto g = groove ({ { 0.0, 36 }, { 1.06, 36 }, { 2.5, 42 }, { 3.0, 4 } }, 90.0, 1, 0.8f);
    auto& library = t.p.getGrooveLibrary();
    const auto pack = t.folder.getChildFile ("Pack");
    pack.createDirectory();
    REQUIRE (library.addFolder (pack));
    const auto r = t.p.grooveToPattern (g, 0, 1);   // pattern B
    CHECK (r.placed == 3);
    CHECK (r.noDrum == 1);
    CHECK (r.pastEnd == 0);
    CHECK (r.lengthBeats == 4);

    const auto& made = t.p.getModMatrix().drumPatterns[1];
    REQUIRE (made.notes.size() == 3);
    CHECK (made.notes[0].row == BD);
    CHECK (made.notes[2].row == CH);   // 42: the closed hat, through the folder's map
    CHECK_THAT (made.notes[1].start, WithinAbs (1.06 * 4.0, 1e-9));   // in sixteenths, off the grid as played
    CHECK_THAT (made.notes[0].velocity, WithinAbs (0.8, 1e-6));
    const auto value = [&t] (const char* id) { return t.p.getParameters().getRawParameterValue (id)->load(); };
    CHECK (value (ParamIDs::dseqOn) > 0.5f);
    CHECK (juce::roundToInt (value (ParamIDs::dseqPattern)) == 1);
    CHECK (juce::roundToInt (value (ParamIDs::dseqLength)) == 4);

    // Played with the song at 120: the kick at 0 and at 1.06 beats (25440 samples), each bar.
    Song song;
    t.p.setPlayHead (&song);
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    for (int b = 0; b < (int) (2.2 * sampleRate) / blockSize; ++b)
    {
        buffer.clear();
        t.p.processBlock (buffer, none);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        song.ppq += blockSize / sampleRate * 2.0;
    }
    t.p.setPlayHead (nullptr);
    const auto o = onsets (out);
    REQUIRE (o.size() >= 4);
    CHECK (o[0] == 0);
    CHECK (std::abs (o[1] - 25440) <= 1);
    CHECK (std::abs (o[2] - 60000) <= 1);   // the hat (2.5 beats)
    CHECK (std::abs (o[3] - 96000) <= 1);   // the next bar
}

TEST_CASE ("GROOVE INTO THE SEQUENCER: a second groove into the same pattern is its own undo step", "[grooves][groove-player][to-pattern]")
{
    Plugin t;
    t.p.grooveToPattern (groove ({ { 0.0, 36 }, { 1.0, 38 } }), 0, 0);
    t.p.flushUndoStep();
    const auto first = t.p.getModMatrix().drumPatterns[0];
    // The same LENGTH, pattern and ON: only the notes change.
    t.p.grooveToPattern (groove ({ { 0.0, 36 }, { 2.0, 38 }, { 3.0, 38 } }), 0, 0);
    t.p.flushUndoStep();
    REQUIRE (t.p.getModMatrix().drumPatterns[0].notes.size() == 3);
    t.p.undo();
    CHECK (t.p.getModMatrix().drumPatterns[0] == first);   // back one step, not two
}

TEST_CASE ("GROOVE INTO THE SEQUENCER: the factor and velocity scale; longer than 16 beats: the first 16", "[grooves][groove-player][to-pattern]")
{
    Plugin t;
    t.preview (true, grooves::TempoFactor::twice, 0.5f);
    const auto r = t.p.grooveToPattern (groove ({ { 0.0, 36 }, { 3.0, 36 } }, 90.0, 2), 0, 0);   // 8 beats at 2x: 4
    CHECK (r.lengthBeats == 4);
    const auto& a = t.p.getModMatrix().drumPatterns[0];
    REQUIRE (a.notes.size() == 2);
    CHECK_THAT (a.notes[1].start, WithinAbs (1.5 * 4.0, 1e-9));
    CHECK_THAT (a.notes[0].velocity, WithinAbs (0.5, 1e-6));

    t.preview (true, grooves::TempoFactor::normal, 1.0f);
    std::vector<std::pair<double, int>> long8bars;
    for (int b = 0; b < 32; ++b)
        long8bars.push_back ({ b * 1.0, 36 });
    const auto l = t.p.grooveToPattern (groove (long8bars, 90.0, 8), 0, 2);
    CHECK (l.lengthBeats == 16);
    CHECK (l.placed == 16);
    CHECK (l.pastEnd == 16);
    CHECK (t.p.getModMatrix().drumPatterns[2].notes.size() == 16);   // beats 0 to 15

    // UNDO takes it back: pattern C empty again, LENGTH as it was before.
    t.p.flushUndoStep();
    t.p.undo();
    CHECK (t.p.getModMatrix().drumPatterns[2].notes.empty());
    CHECK (juce::roundToInt (t.p.getParameters().getRawParameterValue (ParamIDs::dseqLength)->load()) == 4);
}

TEST_CASE ("GROOVES page 0.5: the filter columns list what the grooves have; choosing, excluding, CLEAR; TO SEQUENCER", "[grooves][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto write = [&] (const char* path, std::vector<std::pair<double, int>> notes, double bpm)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        const auto f = pack.getChildFile (path);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    write ("Rock/Verse 01.mid", { { 0.0, 36 }, { 0.5, 42 }, { 1.0, 38 }, { 1.5, 42 } }, 96.0);
    write ("Rock/Fill 01.mid", { { 0.0, 36 }, { 2.0, 38 }, { 3.0, 49 } }, 96.0);
    write ("Shuffle/Verse 02.mid", { { 0.0, 36 }, { 2.0 / 3.0, 42 }, { 1.0, 38 }, { 5.0 / 3.0, 42 } }, 130.0);
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    REQUIRE (page.getResults().size() == 3);
    using grooves::Column;
    CHECK (page.getColumnValues (Column::section) == juce::StringArray { "Verse (2)", "Fill (1)" });
    CHECK (page.getColumnValues (Column::type) == juce::StringArray { "Straight (2)", "Swing (1)", "Beat (2)", "Fill (1)" });
    CHECK (page.getColumnValues (Column::tempo) == juce::StringArray { "80-99 (2)", "120-139 (1)" });

    page.toggleFilter (Column::section, "Verse", false);
    CHECK (page.getResults().size() == 2);
    page.toggleFilter (Column::type, "Swing", true);   // excluded
    REQUIRE (page.getResults().size() == 1);
    CHECK (page.getResults()[0].name == "Verse 01");
    page.toggleFilter (Column::type, "Swing", true);   // let go
    CHECK (page.getResults().size() == 2);
    page.clearFilters();
    CHECK (page.getResults().size() == 3);
    CHECK (page.getFilters().isEmpty());

    // TO SEQUENCER: nothing chosen, nothing happens; the fill chosen, pattern D is it.
    page.toPattern (3);
    CHECK (t.p.getModMatrix().drumPatterns[3].notes.empty());
    page.getTable().selectRow (0);   // sorted by folder and name: Rock / Fill 01 first
    page.toPattern (3);
    CHECK (t.p.getModMatrix().drumPatterns[3].notes.size() == 3);
}

TEST_CASE ("TAP TO FIND: the metronome clicks on each beat while tapping (higher on the bar), and not after", "[tap][groove-player]")
{
    Plugin t;
    t.p.startTapping (120.0, 8.0, 4.0);
    const auto out = t.play (2.2);
    const auto o = onsets (out);
    // Beats every 24000 samples at 120. (The click is a sine from zero, so it doesn't pop: its first sound is the
    // sample after the beat's.)
    REQUIRE (o.size() >= 5);
    CHECK (std::vector<int> (o.begin(), o.begin() + 5) == std::vector<int> { 1, 24001, 48001, 72001, 96001 });
    CHECK_THAT (peak (out), WithinAbs (ParagonX1AudioProcessor::metronomeLevel, 0.03));
    // The bar's first click higher: count its rises in its first 5 ms against the next beat's.
    const auto rises = [&out] (int from)
    {
        auto n = 0;
        for (int i = from + 1; i < from + 240; ++i)
            n += out[(size_t) i - 1] < 0.0f && out[(size_t) i] >= 0.0f;
        return n;
    };
    CHECK (rises (0) > rises (24000));
    CHECK (rises (96000) > rises (72000));   // bar 2's first
    t.p.stopTapping();
    CHECK (peak (t.play (1.0)) < 1.0e-6f);
    CHECK (t.p.getTapBeat() < 0.0);
}

TEST_CASE ("TAP TO FIND: notes played are taps where they fall in the loop, by drum group; the pads' too", "[tap][groove-player]")
{
    Plugin t;
    t.p.startTapping (120.0, 8.0, 4.0);
    juce::AudioBuffer<float> buffer (2, blockSize);
    // Blocks of 256: a kick note (36) at 30000 samples (1.25 beats), a hat (42 under ROOT: F#2, the closed hat) at
    // 200000 (8.33 beats: round the 8-beat loop to 0.33).
    const std::pair<int, int> notes[] = { { 30000, 36 }, { 200000, 42 } };
    for (int start = 0; start < 210000; start += blockSize)
    {
        juce::MidiBuffer midi;
        for (const auto& [at, note] : notes)
            if (at >= start && at < start + blockSize)
                midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), at - start);
        buffer.clear();
        t.p.processBlock (buffer, midi);
    }
    std::vector<ParagonX1AudioProcessor::Tap> taps;
    REQUIRE (t.p.popTaps (taps) == 2);
    CHECK (taps[0].lane == (int) grooves::Lane::kick);
    CHECK_THAT (taps[0].beat, WithinAbs (1.25, 1e-9));
    CHECK (taps[1].lane == (int) grooves::Lane::hats);
    CHECK_THAT (taps[1].beat, WithinAbs (200000.0 / 24000.0 - 8.0, 1e-9));

    // A pad: recorded at the next block's start, its drum played.
    t.p.tapPad ((int) grooves::Lane::snare);
    buffer.clear();
    juce::MidiBuffer none;
    t.p.processBlock (buffer, none);
    taps.clear();
    REQUIRE (t.p.popTaps (taps) == 1);
    CHECK (taps[0].lane == (int) grooves::Lane::snare);
    CHECK (t.p.getDrumKit().isSounding (SD));
    // Not tapping: nothing recorded.
    t.p.stopTapping();
    t.p.tapPad (0);
    t.p.processBlock (buffer, none);
    taps.clear();
    CHECK (t.p.popTaps (taps) == 0);
}

TEST_CASE ("TAP TO FIND in the library: grooves sorted by how close they are to the tap", "[tap][grooves]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto write = [&] (const char* path, std::vector<std::pair<double, int>> notes)
    {
        juce::MidiMessageSequence track;
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        const auto f = pack.getChildFile (path);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    // Rock: kick 1 and 3, snare 2 and 4. Push: kick 1 and the "and" of 2. Four: kick on every beat.
    write ("Rock.mid", { { 0, 36 }, { 2, 36 }, { 1, 38 }, { 3, 38 }, { 4, 36 }, { 6, 36 }, { 5, 38 }, { 7, 38 } });
    write ("Push.mid", { { 0, 36 }, { 1.5, 36 }, { 1, 38 }, { 3, 38 }, { 4, 36 }, { 5.5, 36 }, { 5, 38 }, { 7, 38 } });
    write ("Four.mid", { { 0, 36 }, { 1, 36 }, { 2, 36 }, { 3, 36 }, { 1, 38 }, { 3, 38 }, { 4, 36 }, { 5, 36 }, { 6, 36 }, { 7, 36 }, { 5, 38 }, { 7, 38 } });
    auto& library = t.p.getGrooveLibrary();
    REQUIRE (library.addFolder (pack));
    library.scanNow();
    grooves::Library::TapSearch tap;
    tap.grid = 0.5;
    tap.rhythm.lengthBeats = 8.0;
    tap.rhythm.beats[(size_t) grooves::Lane::kick] = { 0.0, 1.5, 4.0, 5.5 };
    tap.rhythm.beats[(size_t) grooves::Lane::snare] = { 1.0, 3.0, 5.0, 7.0 };
    const auto found = library.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4, {}, &tap);
    REQUIRE (found.size() == 3);
    CHECK (found[0].name == "Push");
    CHECK_THAT (found[0].match, WithinAbs (1.0, 1e-6));
    CHECK (found[1].match < 1.0f);
    CHECK (found[1].match >= found[2].match);
    // The rhythm comes back from the index too.
    grooves::Library again (t.folder.getChildFile ("Grooves.xml"), t.folder.getChildFile ("Groove Index.xml"));
    again.loadIndex();
    CHECK (again.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4, {}, &tap)[0].name == "Push");
}

TEST_CASE ("GROOVES 0.33.1: a scan lists grooves as it reads them, not only when it ends", "[grooves][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Big Pack");
    constexpr int count = 400;
    for (int i = 0; i < count; ++i)
    {
        juce::MidiMessageSequence track;
        for (int beat = 0; beat < 4; ++beat)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, beat % 2 == 0 ? 36 : 38, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, beat % 2 == 0 ? 36 : 38).withTimeStamp (beat * 480 + 60));
        }
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        const auto f = pack.getChildFile ("Folk Metal").getChildFile ("130bpm - Galloping " + juce::String (i) + ".mid");
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    }
    auto& library = t.p.getGrooveLibrary();
    grooves::Library::showFreshEveryMs = 0.0;
    REQUIRE (library.addFolder (pack));
    library.scan();
    // While it scans, some are listed already (the whole of them only at the end, before this change).
    auto seenPartly = false;
    const auto until = juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (library.isScanning() && juce::Time::getMillisecondCounterHiRes() < until)
    {
        const auto n = library.getNumEntries();
        seenPartly = seenPartly || (n > 0 && n < count);
        juce::Thread::sleep (1);
    }
    grooves::Library::showFreshEveryMs = 500.0;
    CHECK (seenPartly);
    REQUIRE (library.getNumEntries() == count);
    // Each listed once, with its values worked out, in name order.
    const auto found = library.query ({}, {}, false, grooves::Sort::name, 120.0, 4, 4, {});
    REQUIRE (found.size() == (size_t) count);
    CHECK (found[0].name == "130bpm - Galloping 0");
    CHECK (found[1].name == "130bpm - Galloping 1");
    CHECK (found.back().name == "130bpm - Galloping 399");
    std::set<juce::String> paths;
    for (const auto& e : found)
    {
        paths.insert (e.file.getFullPathName());
        REQUIRE (e.cached != nullptr);
        CHECK (e.cached->values[(size_t) grooves::Column::tempo] == juce::StringArray { "120-139" });
    }
    CHECK (paths.size() == (size_t) count);
}

TEST_CASE ("GROOVES 0.33.2: a folder opened with new files is read first; already checked this session, it's still found", "[grooves][groove-player]")
{
    Plugin t;
    const auto root = t.folder.getChildFile ("User MIDI");
    const auto write = [] (const juce::File& f)
    {
        juce::MidiMessageSequence track;
        for (int beat = 0; beat < 8; ++beat)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, beat % 2 == 0 ? 36 : 38, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, beat % 2 == 0 ? 36 : 38).withTimeStamp (beat * 480 + 60));
        }
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    write (root.getChildFile ("A Pack/Groove.mid"));
    const auto settings = t.folder.getChildFile ("Test Grooves.xml"), index = t.folder.getChildFile ("Test Groove Index.xml");
    {
        grooves::Library first (settings, index);
        REQUIRE (first.addFolder (root));
        while (first.isScanning())
            juce::Thread::sleep (2);
        REQUIRE (first.getNumEntries() == 1);
    }
    // Copied in while the DAW is open: many in a pack the walk comes to first, a few in the folder being looked at.
    for (int i = 0; i < 1500; ++i)
        write (root.getChildFile ("A Pack/Fills/Fill " + juce::String (i) + ".mid"));
    const auto folk = root.getChildFile ("Loudnoise/Fast Grooves/Folk Metal");
    for (int i = 1; i <= 5; ++i)
        write (folk.getChildFile ("130bpm - Galloping Folk Skank 0" + juce::String (i) + ".mid"));

    // Another PR7-D in the same DAW session: these folders were checked already, so opening the page only lists.
    grooves::Library library (settings, index);
    library.scan (true);
    while (library.isScanning())
        juce::Thread::sleep (2);
    CHECK (library.getNumEntries() == 1);
    CHECK (library.getNumLooked() == 0);
    // Opening Folk Metal: it has files the list hasn't, so it's scanned, that folder first.
    CHECK (library.hasUnlisted (folk));
    CHECK_FALSE (library.hasUnlisted (root.getChildFile ("Nothing here")));
    grooves::Library::showFreshEveryMs = 0.0;
    library.setFocus (folk);
    library.scan();
    auto readWhenFolkCame = -1;
    const auto until = juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (juce::Time::getMillisecondCounterHiRes() < until)
    {
        const auto read = library.getNumScanned();
        if (library.query (folk, {}, false, grooves::Sort::name, 120.0, 4, 4).size() == 5)
        {
            readWhenFolkCame = read;
            break;
        }
        juce::Thread::sleep (1);
    }
    while (library.isScanning())
        juce::Thread::sleep (2);
    grooves::Library::showFreshEveryMs = 500.0;
    REQUIRE (readWhenFolkCame >= 0);
    CHECK (readWhenFolkCame < 300);   // Folk Metal's 5 came in before the 1,500 fills were read (a little slack: polling)
    CHECK (library.getNumEntries() == 1 + 1500 + 5);
    CHECK (library.getNumScanned() == 1505);   // each read once
    CHECK (library.getNumLooked() >= 1506);
    CHECK_FALSE (library.hasUnlisted (folk));
    CHECK_FALSE (library.hasUnlisted (root));
}

TEST_CASE ("TAP TO FIND page: open taps with a loop; FIND sorts by MATCH; FIND SIMILAR; a MIDI file dropped; CLEAR TAP", "[tap][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto write = [&] (const juce::File& f, std::vector<std::pair<double, int>> notes)
    {
        juce::MidiMessageSequence track;
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    write (pack.getChildFile ("A Rock.mid"), { { 0, 36 }, { 2, 36 }, { 1, 38 }, { 3, 38 }, { 4, 36 }, { 6, 36 }, { 5, 38 }, { 7, 38 } });
    write (pack.getChildFile ("B Push.mid"), { { 0, 36 }, { 1.5, 36 }, { 1, 38 }, { 3, 38 }, { 4, 36 }, { 5.5, 36 }, { 5, 38 }, { 7, 38 } });
    write (pack.getChildFile ("C Half.mid"), { { 0, 36 }, { 2, 38 }, { 4, 36 }, { 6, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    REQUIRE (page.getResults().size() == 3);
    CHECK (page.getResults()[0].match < 0.0f);   // no search: no MATCH

    page.openTapPanel();
    CHECK (t.p.isTapping());
    CHECK (page.getTapPanel().isVisible());
    auto& panel = page.getTapPanel();
    for (const auto b : { -0.04, 1.5, 4.0, 5.5 }) panel.addTap ((int) grooves::Lane::kick, b);   // beat 1 a hair early
    CHECK_THAT (panel.getRhythm().beats[(size_t) grooves::Lane::kick].back(), WithinAbs (7.96, 1e-9));
    // 0.31.0: the same drum in the same place again (the next time round) replaces the first: still four kicks, the
    // later one kept; and the taps may all be 40 ms late (the interface's latency).
    panel.addTap ((int) grooves::Lane::kick, 1.55);
    REQUIRE (panel.getRhythm().beats[(size_t) grooves::Lane::kick].size() == 4);
    CHECK_THAT (panel.getRhythm().beats[(size_t) grooves::Lane::kick].front(), WithinAbs (1.55, 1e-9));   // (in time order)
    CHECK (panel.getRhythm().slackBeats > 0.0);
    for (const auto b : { 1.0, 3.0, 5.0, 7.0 }) panel.addTap ((int) grooves::Lane::snare, b);
    panel.getFindButton().onClick();   // as clicking FIND
    CHECK_FALSE (t.p.isTapping());   // FIND closes it
    REQUIRE (page.isTapSearching());
    REQUIRE (page.getResults().size() == 3);
    CHECK (page.getResults()[0].name == "B Push");
    CHECK_THAT (page.getResults()[0].match, WithinAbs (1.0, 1e-6));

    // FIND SIMILAR on the rock beat: it first.
    const auto rock = std::find_if (page.getResults().begin(), page.getResults().end(), [] (const auto& e) { return e.name == "A Rock"; });
    page.findSimilar ((int) std::distance (page.getResults().begin(), rock));
    CHECK (page.getResults()[0].name == "A Rock");

    // A MIDI file dropped: the half-time one's rhythm.
    const auto dropped = t.folder.getChildFile ("Elsewhere/half time.mid");
    write (dropped, { { 0, 36 }, { 2, 38 }, { 4, 36 }, { 6, 38 } });
    CHECK (page.isInterestedInFileDrag ({ dropped.getFullPathName() }));
    CHECK_FALSE (page.isInterestedInFileDrag ({ t.folder.getChildFile ("x.txt").getFullPathName() }));
    CHECK (page.isInterestedInFileDrag ({ t.folder.getChildFile ("x.wav").getFullPathName() }));   // 0.32.0: a riff
    REQUIRE (page.findLikeFile (dropped));   // (FIND SIMILAR's drop box; 0.55.0: dropped on the page, it's a riff)
    CHECK (page.getResults()[0].name == "C Half");

    page.clearTap();
    CHECK_FALSE (page.isTapSearching());
    CHECK (page.getResults()[0].name == "A Rock");   // by name again
    CHECK (page.getResults()[0].match < 0.0f);

    // Closing the window while tapping stops the loop.
    page.openTapPanel();
    CHECK (t.p.isTapping());
}

TEST_CASE ("GROOVE PARTS: every drum in a part; the preview plays only the parts chosen", "[parts][groove-player]")
{
    using grooves::DrumPart;
    CHECK (grooves::drumPartOf (BD) == (int) DrumPart::kick);
    CHECK (grooves::drumPartOf (RIM) == (int) DrumPart::snare);
    CHECK (grooves::drumPartOf (HH) == (int) DrumPart::hats);
    CHECK (grooves::drumPartOf (PH) == (int) DrumPart::pedal);
    CHECK (grooves::drumPartOf (RB) == (int) DrumPart::ride);
    CHECK (grooves::drumPartOf (SPL) == (int) DrumPart::crash);
    CHECK (grooves::drumPartOf (CLP) == (int) DrumPart::percussion);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        CHECK (grooves::drumPartOf (k) >= 0);   // none left out

    Plugin t;
    t.p.playGroove (groove ({ { 0.0, 36 }, { 0.0, 38 }, { 0.0, 42 } }), 0, false, 1u << (int) DrumPart::hats);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    t.p.processBlock (buffer, none);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        CHECK (t.p.getDrumKit().isSounding (k) == (k == CH));
}

TEST_CASE ("GROOVE PARTS: dragged out, only the parts chosen", "[parts][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    pack.createDirectory();
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    const auto g = groove ({ { 0.0, 36 }, { 1.0, 38 }, { 0.5, 42 }, { 1.5, 60 }, { 2.0, 51 } });   // (0.58.0: 60 the open hat; 46 is OPEN 2)
    const auto file = t.p.grooveDragFile (g, 0, (1u << (int) grooves::DrumPart::hats) | (1u << (int) grooves::DrumPart::ride));
    CHECK (file.getFileNameWithoutExtension().endsWith ("HI-HAT RIDE"));
    const auto read = grooves::read (file, synth::drums::notemap::grooveLibrary());
    REQUIRE (read.has_value());
    REQUIRE (read->hits.size() == 3);   // closed hat, open hat, ride (on this kit's notes under ROOT)
    CHECK (read->hits[0].note == 36 + CH);
    CHECK (read->hits[1].note == 36 + OH);
    CHECK (read->hits[2].note == 36 + RD);
}

TEST_CASE ("GROOVE PARTS: laid into a pattern: those drums replaced, repeated to LENGTH, the rest kept; undo", "[parts][groove-player]")
{
    Plugin t;
    // Pattern B from a kick-snare-hats groove (8 beats).
    t.p.grooveToPattern (groove ({ { 0.0, 36 }, { 1.0, 38 }, { 0.0, 42 }, { 2.0, 42 }, { 4.0, 36 }, { 5.0, 38 }, { 4.0, 42 } }, 90.0, 2), 0, 1);
    t.p.flushUndoStep();
    const auto before = t.p.getModMatrix().drumPatterns[1];
    REQUIRE (before.notes.size() == 7);
    // Another groove's hats (one bar of sixteenths, soft) laid on: repeated across the 8 beats.
    std::vector<std::pair<double, int>> sixteenths;
    for (int s = 0; s < 16; ++s)
        sixteenths.push_back ({ s * 0.25, 42 });
    sixteenths.push_back ({ 0.0, 36 });   // its kick: not a hat, not laid
    auto hats = groove (sixteenths, 90.0, 1, 0.6f);
    const auto r = t.p.layerParts (hats, 0, 1u << (int) grooves::DrumPart::hats, 1);
    CHECK (r.placed == 32);
    const auto& after = t.p.getModMatrix().drumPatterns[1];
    auto kicks = 0, snares = 0, hatNotes = 0;
    for (const auto& n : after.notes)
    {
        kicks += n.row == BD;
        snares += n.row == SD;
        hatNotes += n.row == CH;
        if (n.row == CH)
            CHECK_THAT (n.velocity, WithinAbs (0.6, 1e-6));
    }
    CHECK (kicks == 2);      // the pattern's own, kept
    CHECK (snares == 2);
    CHECK (hatNotes == 32);  // the old hats gone, the new ones twice round
    const auto lastHat = std::max_element (after.notes.begin(), after.notes.end(), [] (const auto& a, const auto& b)
    {
        return (a.row == CH ? a.start : -1.0) < (b.row == CH ? b.start : -1.0);
    });
    CHECK_THAT (lastHat->start, WithinAbs (31.0, 1e-9));   // 7.75 beats, in sixteenths
    CHECK (juce::roundToInt (t.p.getParameters().getRawParameterValue (ParamIDs::dseqLength)->load()) == 8);   // LENGTH kept

    t.p.flushUndoStep();
    t.p.undo();
    INFO ("after undo: " << t.p.getModMatrix().drumPatterns[1].notes.size() << " notes");
    CHECK (t.p.getModMatrix().drumPatterns[1] == before);
}

TEST_CASE ("GROOVE PARTS page: the chosen groove's parts counted; ticking; the preview with them; LAYER ONTO", "[parts][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    {
        juce::MidiMessageSequence track;
        for (const auto& [beat, note] : std::vector<std::pair<double, int>> { { 0, 36 }, { 2, 36 }, { 1, 38 }, { 3, 38 }, { 0, 42 }, { 1, 42 }, { 2, 42 }, { 3, 60 } })   // (0.58.0: 60 the open hat)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        pack.createDirectory();
        juce::FileOutputStream out (pack.getChildFile ("Beat.mid"));
        file.writeTo (out);
    }
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    page.getTable().selectRow (0);
    using grooves::DrumPart;
    const auto counts = page.getPartCounts();
    CHECK (counts[(size_t) DrumPart::kick] == 2);
    CHECK (counts[(size_t) DrumPart::snare] == 2);
    CHECK (counts[(size_t) DrumPart::hats] == 4);   // closed and open
    CHECK (counts[(size_t) DrumPart::ride] == 0);

    CHECK (page.getDrumParts() == grooves::allParts);
    page.toggleDrumPart ((int) DrumPart::hats);    // from ALL: just the hats
    CHECK (page.getDrumParts() == 1u << (int) DrumPart::hats);
    page.toggleDrumPart ((int) DrumPart::kick);    // and the kick
    CHECK (page.getDrumParts() == ((1u << (int) DrumPart::hats) | (1u << (int) DrumPart::kick)));
    page.toggleDrumPart ((int) DrumPart::kick);
    page.toggleDrumPart ((int) DrumPart::hats);    // none left: back to ALL
    CHECK (page.getDrumParts() == grooves::allParts);

    // The preview with only the hats; ticking the snare while it plays: heard at once.
    page.toggleDrumPart ((int) DrumPart::hats);
    page.playRow (0, true);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    t.p.processBlock (buffer, none);
    CHECK (t.p.getDrumKit().isSounding (CH));
    CHECK_FALSE (t.p.getDrumKit().isSounding (BD));
    page.toggleDrumPart ((int) DrumPart::snare);
    t.p.prepareToPlay (sampleRate, blockSize);   // the kit quiet: what plays now is the new preview
    for (int b = 0; b < 200; ++b)
        t.p.processBlock (buffer, none);   // past beat 2 (the snare's at 1)
    CHECK (t.p.getDrumKit().isSounding (SD));

    // LAYER ONTO C: the hats and snare only.
    page.layerOnto (2);
    const auto& c = t.p.getModMatrix().drumPatterns[2];
    auto rows = std::set<int>();
    for (const auto& n : c.notes)
        rows.insert (n.row);
    CHECK (rows == std::set<int> { SD, CH, OH });
}

//==============================================================================
// RIFF MATCH (0.32.0)
TEST_CASE ("RIFF MATCH: the riff is heard with the preview, from its start, looping its part, at its own tempo and rate", "[riff][groove-player]")
{
    Plugin t;
    // A riff at 44.1 kHz (not the plugin's rate): silence, a click where its part starts.
    const auto rate = 44100.0;
    auto audio = std::make_shared<juce::AudioBuffer<float>> (1, (int) (3.0 * rate));
    audio->clear();
    const auto start = 0.5, loop = 0.8;   // 2 beats at 150 BPM
    audio->setSample (0, (int) (start * rate), 1.0f);
    t.p.setRiff (audio, rate, start, loop, 150.0);
    t.p.setRiffLevel (1.0f);
    const auto peakIn = [] (const std::vector<float>& x, int from, int to)
    {
        auto best = from;
        for (int i = from; i < juce::jmin (to, (int) x.size()); ++i)
            if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) best]))
                best = i;
        return best;
    };
    // Nothing previewing: not heard.
    auto out = t.play (0.3);
    CHECK (std::abs (out[(size_t) peakIn (out, 0, (int) out.size())]) < 1.0e-6f);

    // A preview (a note no drum plays: silent drums): the click at its start, then every 0.8 s, to the sample.
    t.p.playGroove (groove ({ { 0.0, 0 } }, 90.0), 0, true);
    out = t.play (2.0);
    for (int k = 0; k < 3; ++k)
    {
        const auto expected = (int) std::lround (k * loop * sampleRate);
        const auto at = peakIn (out, juce::jmax (0, expected - 200), expected + 200);
        INFO ("loop " << k << ": click at " << at << ", expected " << expected);
        CHECK (std::abs (at - expected) <= 1);
        CHECK (std::abs (out[(size_t) at]) > 0.4f);
    }
    // The preview plays at the riff's tempo (150 BPM), not the groove's or the song's: 2 s is 5 beats (4-beat loop: 1).
    CHECK_THAT (t.p.getGroovePosition(), WithinAbs (std::fmod (std::ceil (2.0 * sampleRate / blockSize) * blockSize / sampleRate * 2.5, 4.0), 1e-6));

    // LISTEN off: the drums alone again (and their own tempo).
    t.p.setRiffListening (false);
    t.p.playGroove (groove ({ { 0.0, 0 } }, 90.0), 0, true);
    out = t.play (1.0);
    CHECK (std::abs (out[(size_t) peakIn (out, 0, (int) out.size())]) < 1.0e-6f);
    t.p.setRiffListening (true);
    // Put away: gone.
    t.p.clearRiff();
    t.p.playGroove (groove ({ { 0.0, 0 } }, 90.0), 0, true);
    out = t.play (1.0);
    CHECK (std::abs (out[(size_t) peakIn (out, 0, (int) out.size())]) < 1.0e-6f);
    CHECK_FALSE (t.p.hasRiff());
}

TEST_CASE ("RIFF MATCH page: a riff dropped finds the groove whose kick follows it; its chip; put away", "[riff][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto write = [&] (const juce::File& f, std::vector<std::pair<double, int>> notes, int channel = 10, double bpm = 0.0)
    {
        juce::MidiMessageSequence track;
        if (bpm > 0.0)
            track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100).withTimeStamp (beat * 480));
            track.addEvent (juce::MidiMessage::noteOff (channel, note).withTimeStamp (beat * 480 + 100));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        track.updateMatchedPairs();
        file.addTrack (track);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    };
    // Three grooves: their kicks differ, their snares the same backbeat.
    write (pack.getChildFile ("A Rock.mid"), { { 0, 36 }, { 2, 36 }, { 4, 36 }, { 6, 36 }, { 1, 38 }, { 3, 38 }, { 5, 38 }, { 7, 38 } });
    write (pack.getChildFile ("B Push.mid"), { { 0, 36 }, { 0.75, 36 }, { 1.5, 36 }, { 2.5, 36 }, { 4, 36 }, { 4.75, 36 }, { 5.5, 36 }, { 6.5, 36 },
                                               { 1, 38 }, { 3, 38 }, { 5, 38 }, { 7, 38 } });
    write (pack.getChildFile ("C Floor.mid"), { { 0, 36 }, { 1, 36 }, { 2, 36 }, { 3, 36 }, { 4, 36 }, { 5, 36 }, { 6, 36 }, { 7, 36 },
                                                { 1, 38 }, { 3, 38 }, { 5, 38 }, { 7, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    REQUIRE (page.getResults().size() == 3);

    // A bass riff (MIDI, at 110 BPM) on B's kick, with passing notes between.
    const auto riffFile = t.folder.getChildFile ("Elsewhere/my bass riff.mid");
    write (riffFile, { { 0, 40 }, { 0.75, 40 }, { 1.5, 43 }, { 2.5, 45 }, { 3.25, 45 }, { 4, 40 }, { 4.75, 40 }, { 5.5, 43 }, { 6.5, 47 }, { 7.25, 45 } },
           1, 110.0);
    CHECK (page.isInterestedInFileDrag ({ t.folder.getChildFile ("riff.wav").getFullPathName() }));
    REQUIRE (page.loadRiff (riffFile, true));
    CHECK (page.getRiffPanel().isVisible());
    CHECK (t.p.hasRiff());
    CHECK_THAT (page.getRiffPanel().getRiff().bpm, WithinAbs (110.0, 1e-6));
    page.getRiffPanel().getFindButton().onClick();   // as clicking FIND
    REQUIRE (page.isRiffSearching());
    CHECK_FALSE (page.getRiffPanel().isVisible());
    REQUIRE (page.getResults().size() == 3);
    CHECK (page.getResults()[0].name == "B Push");
    CHECK (page.getResults()[0].match > page.getResults()[1].match);
    CHECK (page.getChips().contains ("FITS RIFF: my bass riff"));

    // Two bars from its second bar on: still B (the riff repeats). Changing the part searches again at once.
    page.getRiffPanel().setStartSeconds (4.0 * 60.0 / 110.0);
    CHECK (page.getResults()[0].name == "B Push");

    // The cross: the riff put away, its search gone, nothing heard.
    page.putRiffAway();
    CHECK_FALSE (page.isRiffSearching());
    CHECK_FALSE (t.p.hasRiff());
    CHECK_FALSE (page.getRiffPanel().hasRiff());
    CHECK (page.getResults()[0].match < 0.0f);
    // A file that isn't one: refused.
    CHECK_FALSE (page.loadRiff (t.folder.getChildFile ("notes.txt"), true));

    // 0.55.0: the MIDI riff dragged onto the page (as from the DAW), not onto RIFF MATCH's box: a riff, not a groove
    // to search like (read there and then here: in the plugin, on its own thread).
    CHECK (page.isInterestedInFileDrag ({ riffFile.getFullPathName() }));
    page.waitForDroppedRiffs = true;
    page.filesDropped ({ riffFile.getFullPathName() }, page.getWidth() / 2, page.getHeight() - 40);
    REQUIRE (page.getRiffPanel().hasRiff());
    CHECK (page.getRiffPanel().isVisible());
    CHECK (t.p.hasRiff());
    CHECK_THAT (page.getRiffPanel().getRiff().bpm, WithinAbs (110.0, 1e-6));
    CHECK_FALSE (page.isTapSearching());   // not searched as a drum groove
}
