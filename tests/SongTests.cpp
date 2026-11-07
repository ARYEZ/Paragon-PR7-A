// THE SONG TRACK (PR7-D 0.8.0): blocks placed by our rules (a drop inside a block goes to its nearer edge and the rest
// move along, lengthening pushes, shortening leaves a gap), each block's groove repeated to fill it, played with the DAW's
// song position (each hit on its sample) or with PLAY, MUTE, CHANGE TEMPO, undo, kept in the project, EXPORT MIDI, and
// the strip's drops and edits.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

#include "ui/GroovesPage.h"
#include "ui/SongTrack.h"
#include "ui/BlockEditor.h"
#include "ui/SongCreator.h"
#include "ui/MainView.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;

    // A block of `bars` 4/4 bars with `hits` (beat, slot).
    song::Block block (const juce::String& name, int bars, std::vector<std::pair<double, int>> hits = {})
    {
        song::Block b;
        b.name = name;
        b.length = b.loopLength = bars * 4.0;
        for (const auto& [beat, slot] : hits)
            b.hits.push_back ({ beat, slot, 1.0f });
        return b;
    }

    std::vector<double> starts (const song::Song& s)
    {
        std::vector<double> out;
        for (const auto& b : s.blocks)
            out.push_back (b.start);
        return out;
    }

    juce::StringArray names (const song::Song& s)
    {
        juce::StringArray out;
        for (const auto& b : s.blocks)
            out.add (b.name);
        return out;
    }

    struct Host final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    struct Plugin
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonSongTests");
        ParagonX1AudioProcessor p;
        Host host;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (p);   // (0.59.0: ROOT's keys, as these tests play them)
            // The kick and snare play a short steady burst (20 ms, full from its first sample): each hit shows where it starts.
            const auto burst = testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate);
            testkit::useSample (p, BD, burst);
            testkit::useSample (p, SD, burst);
            p.setPlayHead (&host);
        }
        ~Plugin()
        {
            p.setPlayHead (nullptr);
            folder.deleteRecursively();
        }

        // `seconds` of the plugin's output; the DAW's position moves on with it while it plays.
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
                if (host.playing)
                    host.ppq += blockSize / sampleRate * host.bpm / 60.0;
            }
            return out;
        }
    };

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

    void writeMidi (const juce::File& f, std::vector<std::pair<double, int>> notes, int bars = 1)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480.0));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480.0 + 60.0));
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (bars * 4.0 * 480.0);
        track.addEvent (end);
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    }
}

TEST_CASE ("SONG: placing blocks: after the last, inside one (to its nearer edge, the rest moved along), past the end (a gap)", "[song]")
{
    song::Song s;
    s.place (block ("A", 1), s.endBeat());
    s.place (block ("B", 1), s.endBeat());
    s.place (block ("C", 2), s.endBeat());
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0 });

    // Dropped on A's first half: in front of A, everything after moved along a bar.
    CHECK (s.place (block ("D", 1), 1.0) == 0);
    CHECK (names (s).joinIntoString (",") == "D,A,B,C");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0 });
    // On B's second half (beat 11): after B.
    CHECK (s.place (block ("E", 1), 11.0) == 3);
    CHECK (names (s).joinIntoString (",") == "D,A,B,E,C");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0, 16.0 });
    // Past the end (C ends at 24): to the nearest bar line (beat 29 -> 28), a gap before it.
    CHECK (s.place (block ("F", 1), 29.0) == 5);
    CHECK (s.blocks.back().start == 28.0);
    CHECK (s.endBeat() == 32.0);
}

TEST_CASE ("SONG: editing: longer pushes the blocks after it, shorter leaves a gap; move, duplicate, remove; whole bars", "[song]")
{
    song::Song s;
    for (const auto* n : { "A", "B", "C" })
        s.place (block (n, 1), s.endBeat());
    s.resize (0, 8.6);   // to whole bars: 2
    CHECK (s.blocks[0].length == 8.0);
    CHECK (starts (s) == std::vector<double> { 0.0, 8.0, 12.0 });
    s.resize (0, 4.0);   // shorter: B and C stay
    CHECK (starts (s) == std::vector<double> { 0.0, 8.0, 12.0 });
    s.resize (0, 0.5);   // at least a bar
    CHECK (s.blocks[0].length == 4.0);

    // C moved to the front: A and B move along.
    CHECK (s.move (2, 0.0) == 0);
    CHECK (names (s).joinIntoString (",") == "C,A,B");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0 });
    // Duplicated: the copy right after it, the rest along.
    CHECK (s.duplicate (1) == 2);
    CHECK (names (s).joinIntoString (",") == "C,A,A,B");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0 });
    s.remove (0);
    CHECK (names (s).joinIntoString (",") == "A,A,B");
    CHECK (starts (s) == std::vector<double> { 4.0, 8.0, 12.0 });   // removing leaves a gap: nothing moves

    // 3/4: three beats a bar.
    song::Song waltz;
    waltz.numerator = 3;
    CHECK (waltz.beatsPerBar() == 3.0);
    CHECK (waltz.snap (4.0) == 3.0);
    CHECK (waltz.snap (5.0) == 6.0);
}

TEST_CASE ("SONG: each block's groove repeated to fill it, cut at its end; MUTE leaves it out; CHANGE TEMPO", "[song]")
{
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 2.0, SD } }), 0.0);
    s.resize (0, 12.0);   // three times round
    s.place (block ("B", 1, { { 1.0, BD } }), s.endBeat());
    auto e = s.events();
    REQUIRE (e.size() == 7);
    for (int i = 0; i < 6; ++i)
        CHECK (e[(size_t) i].beat == i * 2.0);
    CHECK (e[6].beat == 13.0);
    CHECK (e[6].slot == BD);

    // Cut part way through the groove: the hits past its end aren't played.
    s.blocks[0].length = 6.0;
    e = s.events();
    CHECK (e.size() == 3 + 1);
    s.blocks[0].muted = true;
    e = s.events();
    REQUIRE (e.size() == 1);
    CHECK (e[0].beat == 13.0);

    // 2x: twice as fast, half as long; 1/2x: the other way.
    auto b = block ("C", 1, { { 0.0, BD }, { 3.0, SD } });
    song::changeTempo (b, grooves::TempoFactor::twice);
    CHECK (b.loopLength == 2.0);
    CHECK (b.length == 2.0);
    CHECK (b.hits[1].beat == 1.5);
    song::changeTempo (b, grooves::TempoFactor::half);
    song::changeTempo (b, grooves::TempoFactor::half);
    CHECK (b.loopLength == 8.0);
    CHECK (b.hits[1].beat == 6.0);
}

TEST_CASE ("SONG: a groove as a block (through the folder's map, the factor and velocity, its SONG PART); XML; MIDI", "[song]")
{
    grooves::Groove g;
    g.name = "Chorus 01";
    g.file = juce::File ("/grooves/Rock/Chorus 01.mid");
    g.lengthBeats = 4.0;
    g.hits = { { 0.0, 36, 1.0f }, { 1.0, 42, 0.5f }, { 2.0, 4, 1.0f } };   // 42: closed hat in the groove library map; 4: no drum
    const auto b = song::blockFrom (g, grooveLibrary(), grooves::TempoFactor::twice, 0.8f);
    CHECK (b.name == "Chorus 01");
    CHECK (b.part == grooves::Part::chorus);
    CHECK (b.loopLength == 2.0);
    REQUIRE (b.hits.size() == 2);
    CHECK (b.hits[0].slot == BD);
    CHECK (b.hits[1].slot == CH);
    CHECK (b.hits[1].beat == 0.5);
    CHECK_THAT (b.hits[1].velocity, WithinAbs (0.4, 1e-6));

    song::Song s;
    s.numerator = 6;
    s.denominator = 8;
    s.place (b, 0.0);
    s.place (block ("Fill", 1, { { 3.5, SD } }), s.endBeat());
    s.blocks[1].muted = true;
    s.blocks[1].part = grooves::Part::fill;
    const auto back = song::Song::fromXml (*s.toXml());
    CHECK (back == s);

    // MIDI: each drum on its note, at 960 ticks a beat, the tempo and signature.
    s.blocks[1].muted = false;
    const auto midi = s.toMidi (100.0, [] (int slot) { return slot == BD ? 36 : slot == SD ? 38 : -1; });   // the hat left out
    const auto* track = midi.getTrack (0);
    REQUIRE (track != nullptr);
    std::vector<std::pair<double, int>> notes;
    auto tempo = 0.0;
    auto num = 0;
    for (const auto* e : *track)
    {
        if (e->message.isNoteOn())
            notes.emplace_back (e->message.getTimeStamp(), e->message.getNoteNumber());
        if (e->message.isTempoMetaEvent())
            tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote();
        if (e->message.isTimeSignatureMetaEvent())
        {
            int d = 0;
            e->message.getTimeSignatureInfo (num, d);
        }
    }
    CHECK_THAT (tempo, WithinAbs (100.0, 0.01));
    CHECK (num == 6);
    // The fill: after the first block's bar of 6/8 (3 beats, the 2-beat groove made a bar long), its snare at 3.5.
    CHECK (s.blocks[1].start == 3.0);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0] == std::make_pair (0.0, 36));
    CHECK (notes[1] == std::make_pair ((3.0 + 3.5) * 960.0, 38));
}

TEST_CASE ("SONG playing with the DAW: each hit on its sample (bar 3 at 120 BPM: 4 s in), from wherever the DAW starts", "[song][song-player]")
{
    Plugin t;
    song::Song s;
    s.place (block ("Silence", 2), 0.0);
    s.place (block ("Beat", 1, { { 0.0, BD }, { 1.5, SD } }), s.endBeat());
    REQUIRE (s.blocks[1].start == 8.0);
    t.p.setSong (s);
    auto o = onsets (t.play (5.0));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 192000);           // beat 8 at 120 BPM: 4 s
    CHECK (o[1] == 192000 + 36000);   // 1.5 beats later
    CHECK_THAT (t.p.getSongPosition(), WithinAbs (t.host.ppq, 0.02));

    // The DAW started part way in (beat 7.5) at 90 BPM: the kick half a beat in (0.5 x 60 / 90 s: 16000 samples).
    t.host.ppq = 7.5;
    t.host.bpm = 90.0;
    o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 16000);

    // MUTE: nothing.
    s.blocks[1].muted = true;
    t.p.setSong (s);
    t.host.ppq = 7.5;
    CHECK (onsets (t.play (1.5)).empty());
}

TEST_CASE ("SONG PLAY with the DAW stopped: from the place chosen, at the DAW's tempo, stopping at the end", "[song][song-player]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    s.place (block ("B", 1, { { 0.0, SD }, { 2.0, BD } }), s.endBeat());
    t.p.setSong (s);
    CHECK (onsets (t.play (0.5)).empty());   // the DAW stopped, PLAY not pressed: quiet
    CHECK (t.p.getSongPosition() < 0.0);

    t.p.playSong (4.0);   // from bar 2
    CHECK (t.p.isSongPlaying());
    const auto o = onsets (t.play (3.0));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 0);
    CHECK (o[1] == 48000);   // 2 beats at 120
    CHECK (! t.p.isSongPlaying());   // past the end (beat 8 = 2 s): stopped
    CHECK (t.p.getSongPosition() < 0.0);

    // STOP part way.
    t.p.playSong (0.0);
    t.play (0.2);
    t.p.stopSong();
    CHECK (onsets (t.play (2.0)).empty());

    // The DAW starting ends PLAY: it leads from then on, and stopping it doesn't bring PLAY back.
    t.p.playSong (0.0);
    t.host.playing = true;
    t.host.ppq = 100.0;
    t.play (0.1);
    CHECK (! t.p.isSongPlaying());
    t.host.playing = false;
    CHECK (onsets (t.play (2.0)).empty());
}

TEST_CASE ("SONG: an edit is an undo step; kept in the project (not in presets); EXPORT MIDI on the kit's notes", "[song][song-player]")
{
    Plugin t;
    t.p.flushUndoStep();
    CHECK (t.p.getSong().isEmpty());
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 1.0, SD } }), 0.0);
    t.p.setSong (s);
    t.p.flushUndoStep();
    auto longer = s;
    longer.resize (0, 8.0);
    t.p.setSong (longer);
    t.p.flushUndoStep();
    t.p.undo();
    CHECK (t.p.getSong() == s);
    t.p.undo();
    CHECK (t.p.getSong().isEmpty());
    t.p.redo();
    t.p.redo();
    CHECK (t.p.getSong() == longer);

    // The project: saved and back in another instance; a project without a song: none.
    juce::MemoryBlock state;
    t.p.getStateInformation (state);
    {
        ParagonX1AudioProcessor other;
        CHECK (other.getSong().isEmpty());
        other.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (other.getSong() == longer);
        ParagonX1AudioProcessor fresh;
        juce::MemoryBlock empty;
        fresh.getStateInformation (empty);
        other.setStateInformation (empty.getData(), (int) empty.getSize());
        CHECK (other.getSong().isEmpty());
    }
    // Not in presets: a preset loaded keeps the song.
    t.p.loadRig (*t.p.getRigLibrary().findFactory ("Room Kit"));
    CHECK (t.p.getSong() == longer);

    // EXPORT MIDI: the kick and snare on their home notes, twice round (8 beats).
    const auto midi = t.p.songToMidi();
    std::vector<int> notes;
    for (const auto* e : *midi.getTrack (0))
        if (e->message.isNoteOn())
            notes.push_back (e->message.getNoteNumber());
    CHECK (notes == std::vector<int> { t.p.homeNote (BD), t.p.homeNote (SD), t.p.homeNote (BD), t.p.homeNote (SD) });
    CHECK (t.p.songDragFile().existsAsFile());
}

TEST_CASE ("SONG strip: a MIDI file dropped is a block; DELETE; the menu's DUPLICATE, MUTE, CHANGE TEMPO; GROOVES' ADD TO SONG", "[song][ui]")
{
    Plugin t;
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    const auto file = t.folder.getChildFile ("Dropped/Verse 02.mid");
    writeMidi (file, { { 0.0, t.p.homeNote (BD) }, { 2.0, t.p.homeNote (SD) } });
    REQUIRE (track.isInterestedInFileDrag ({ file.getFullPathName() }));
    CHECK (! track.isInterestedInFileDrag ({ "/somewhere/kick.wav" }));
    track.filesDropped ({ file.getFullPathName(), file.getFullPathName() }, (int) track.xOf (4.0) + 2, 80);
    const auto& s = t.p.getSong();
    REQUIRE (s.blocks.size() == 2);   // the second after the first
    CHECK (s.blocks[0].start == 4.0);
    CHECK (s.blocks[1].start == 8.0);
    CHECK (s.blocks[0].name == "Verse 02");
    CHECK (s.blocks[0].part == grooves::Part::verse);
    REQUIRE (s.blocks[0].hits.size() == 2);
    CHECK (s.blocks[0].hits[1].slot == SD);

    // DELETE removes the chosen block.
    track.choose (0);
    CHECK (track.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (t.p.getSong().blocks.size() == 1);
    CHECK (t.p.getSong().blocks[0].start == 8.0);

    // The menu: what it has, and what each does.
    auto menu = track.menuFor (0);
    juce::StringArray items;
    std::map<juce::String, std::function<void()>> actions;
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
    {
        items.add (it.getItem().text);
        actions[it.getItem().text] = it.getItem().action;
    }
    for (const auto* item : { "Duplicate", "Remove", "Mute", "Song Part", "Chorus", "Change Tempo", "2x" })
        CHECK (items.contains (item));
    actions["Duplicate"]();
    REQUIRE (t.p.getSong().blocks.size() == 2);
    CHECK (t.p.getSong().blocks[1].start == 12.0);
    actions["Mute"]();
    CHECK (t.p.getSong().blocks[0].muted);
    actions["Chorus"]();
    CHECK (t.p.getSong().blocks[0].part == grooves::Part::chorus);
    actions["1/2x"]();   // twice as long: 2 bars, the copy moved along
    CHECK (t.p.getSong().blocks[0].length == 8.0);
    CHECK (t.p.getSong().blocks[1].start == 16.0);

    // GROOVES: right-click ADD TO SONG puts the groove after the last block.
    const auto pack = t.folder.getChildFile ("Pack");
    writeMidi (pack.getChildFile ("Fill 01.mid"), { { 3.0, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    REQUIRE (page.getResults().size() == 1);
    auto shown = false;
    page.onAddedToSong = [&shown] { shown = true; };
    CHECK (page.addRowToSong (0));
    CHECK (shown);
    REQUIRE (t.p.getSong().blocks.size() == 3);
    CHECK (t.p.getSong().blocks[2].name == "Fill 01");
    CHECK (t.p.getSong().blocks[2].start == 20.0);
    CHECK (t.p.getSong().blocks[2].part == grooves::Part::fill);
}

//==============================================================================
// THE SONG TRACK part 2 (0.9.0): editing inside a block.
namespace
{
    std::vector<std::pair<double, int>> notesOf (const song::Block& b)
    {
        std::vector<std::pair<double, int>> out;
        for (const auto& h : b.hits)
            out.emplace_back (h.beat, h.slot);
        return out;
    }
    song::SlotMask bit (int slot) { return song::slotBit (slot); }
}

TEST_CASE ("SONG 0.9: split at a bar (each half its own notes), merge with the next (a gap kept as rests)", "[song][song-edit]")
{
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 2.0, SD } }), 0.0);
    s.resize (0, 12.0);   // three bars, the one-bar groove three times round
    s.place (block ("B", 1, { { 1.0, CH } }), 16.0);   // a bar's gap before it
    REQUIRE (starts (s) == std::vector<double> { 0.0, 16.0 });

    // At beat 5.2: the nearest bar line inside it is beat 4.
    CHECK (s.split (0, 5.2) == 1);
    REQUIRE (names (s).joinIntoString (",") == "A,A,B");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 16.0 });
    CHECK (s.blocks[0].length == 4.0);
    CHECK (s.blocks[1].length == 8.0);
    CHECK (s.blocks[1].loopLength == 8.0);   // the repeats written out
    CHECK (notesOf (s.blocks[1]) == std::vector<std::pair<double, int>> { { 0.0, BD }, { 2.0, SD }, { 4.0, BD }, { 6.0, SD } });
    CHECK (s.split (0, 0.4) == -1);   // no bar line inside a one-bar block
    // The song plays the same as before.
    auto e = s.events();
    REQUIRE (e.size() == 7);
    CHECK (e[4].beat == 8.0);

    // The second half merged with B: its 4-beat gap kept as rests, B's hat at 16 + 1 from the song's start.
    CHECK (s.mergeWithNext (1) == 1);
    REQUIRE (s.blocks.size() == 2);
    CHECK (s.blocks[1].start == 4.0);
    CHECK (s.blocks[1].length == 16.0);
    CHECK (notesOf (s.blocks[1]).back() == std::make_pair (13.0, (int) CH));
    CHECK (s.events().size() == 7);
    CHECK (s.mergeWithNext (1) == -1);   // nothing after it
}

TEST_CASE ("SONG 0.9: remove drums, paste drums (repeated to fill), quantize, velocity, move a drum, add and remove a note", "[song][song-edit]")
{
    // A two-bar groove: kick, a late snare, hats.
    auto b = block ("Verse", 2, { { 0.0, BD }, { 1.06, SD }, { 3.0, SD }, { 4.0, BD }, { 5.0, SD } });
    for (int e = 0; e < 16; ++e)
        b.hits.push_back ({ e * 0.5, CH, e % 2 == 0 ? 0.8f : 0.4f });
    std::stable_sort (b.hits.begin(), b.hits.end(), [] (const song::Hit& x, const song::Hit& y) { return x.beat < y.beat; });
    CHECK (song::slotsIn (b) == (bit (BD) | bit (SD) | bit (CH)));
    CHECK (song::slotsOf (1u << (int) grooves::DrumPart::hats) == (bit (CH) | bit (OH) | bit (HH) | bit (HO1) | bit (HO3) | bit (HSH)));

    auto removed = b;
    song::removeDrums (removed, song::slotsOf (1u << (int) grooves::DrumPart::hats));
    CHECK (song::slotsIn (removed) == (bit (BD) | bit (SD)));
    CHECK (removed.hits.size() == 5);

    // A one-bar ride part pasted on the hats' place... the hats stay (other drums), the ride repeats to fill 2 bars.
    const auto ride = block ("Ride", 1, { { 0.0, RD }, { 1.0, RD }, { 2.0, RD }, { 3.0, RD }, { 0.0, BD } });
    auto pasted = b;
    song::pasteDrums (pasted, ride, bit (RD));
    auto rides = 0;
    for (const auto& h : pasted.hits)
        rides += h.slot == RD;
    CHECK (rides == 8);
    CHECK (pasted.hits.size() == b.hits.size() + 8);   // its kick not pasted (not asked for), nothing else changed
    // Pasted onto the kick: the block's own kicks replaced by the clip's (one a bar).
    song::pasteDrums (pasted, ride, bit (BD));
    auto kicks = std::vector<double>();
    for (const auto& h : pasted.hits)
        if (h.slot == BD)
            kicks.push_back (h.beat);
    CHECK (kicks == std::vector<double> { 0.0, 4.0 });

    // QUANTIZE 1/4 on the snare only: 1.06 -> 1; the hats' off-beats untouched.
    auto q = b;
    song::quantize (q, 1.0, bit (SD));
    CHECK (notesOf (q)[2] == std::make_pair (0.5, (int) CH));
    auto snares = std::vector<double>();
    for (const auto& h : q.hits)
        if (h.slot == SD)
            snares.push_back (h.beat);
    CHECK (snares == std::vector<double> { 1.0, 3.0, 5.0 });
    // A note pushed to the groove's end goes round to its start; two on one drum and beat become one, the louder.
    auto wrap = block ("W", 1, { { 0.0, BD }, { 3.9, BD } });
    wrap.hits[0].velocity = 0.3f;
    song::quantize (wrap, 1.0);
    REQUIRE (wrap.hits.size() == 1);
    CHECK (wrap.hits[0].beat == 0.0);
    CHECK (wrap.hits[0].velocity == 1.0f);

    // VELOCITY: the hats at half, their balance kept (0.8 / 0.4 -> 0.4 / 0.2); others untouched; never past full or to 0.
    auto v = b;
    song::scaleVelocity (v, 0.5f, bit (CH));
    CHECK_THAT (v.hits[1].velocity, WithinAbs (0.4, 1e-6));   // the hat on 0 (after the kick)
    CHECK_THAT (v.hits[2].velocity, WithinAbs (0.2, 1e-6));
    CHECK (v.hits[0].velocity == 1.0f);
    song::scaleVelocity (v, 10.0f);
    CHECK (v.hits[1].velocity == 1.0f);
    song::scaleVelocity (v, 0.0f);
    CHECK_THAT (v.hits[1].velocity, WithinAbs (1.0 / 127.0, 1e-6));

    // MOVE TO: the hats on the ride; then the ride on the kick: merged, one hit where both had one (the louder).
    auto m = b;
    song::moveDrum (m, CH, RD);
    CHECK (song::slotsIn (m) == (bit (BD) | bit (SD) | bit (RD)));
    song::moveDrum (m, RD, BD);
    auto onZero = 0;
    for (const auto& h : m.hits)
        onZero += h.slot == BD && h.beat == 0.0;
    CHECK (onZero == 1);

    // A note added (one on that drum and beat replaced), removed by the nearest within reach.
    auto n = b;
    song::addHit (n, { 7.75, BD, 0.5f });
    song::addHit (n, { 7.75, BD, 0.9f });
    CHECK (n.hits.back() .velocity == 0.9f);
    song::addHit (n, { 8.0, BD, 0.5f });   // past the groove: not added
    CHECK (n.hits.size() == b.hits.size() + 1);
    CHECK (! song::removeHitNear (n, SD, 2.0, 0.1));
    CHECK (song::removeHitNear (n, SD, 1.1, 0.1));   // the late snare
    CHECK (n.hits.size() == b.hits.size());
}

TEST_CASE ("SONG 0.9 heard: QUANTIZE puts a late snare on its sample; VELOCITY 50% plays as the notes at half velocity", "[song][song-edit][song-player]")
{
    Plugin t;
    song::Song s;
    s.place (block ("Late", 1, { { 1.06, SD } }), 0.0);
    t.p.setSong (s);
    t.host.ppq = 0.0;
    auto o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 25440);   // 1.06 beats at 120
    song::quantize (s.blocks[0], 0.25);
    t.p.setSong (s);
    t.host.ppq = 0.0;
    o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 24000);   // on beat 2

    // A loud snare scaled to 50% against the same note written at half: the same sound, sample for sample.
    const auto render = [&t] (float velocity, float scale)
    {
        song::Song one;
        auto b = block ("V", 1, { { 0.0, SD } });
        b.hits[0].velocity = velocity;
        song::scaleVelocity (b, scale);
        one.place (b, 0.0);
        t.p.setSong (one);
        t.host.ppq = 0.0;
        return t.play (0.1);
    };
    const auto scaled = render (0.8f, 0.5f), written = render (0.4f, 1.0f), full = render (0.8f, 1.0f);
    CHECK (scaled == written);
    auto peak = [] (const std::vector<float>& x) { auto p = 0.0f; for (auto v : x) p = std::max (p, std::abs (v)); return p; };
    CHECK (peak (scaled) < peak (full) * 0.9f);
}

TEST_CASE ("SONG 0.9: the clipboard: a groove's ticked parts copied; the strip's menu pastes, removes, quantizes, splits, merges", "[song][song-edit][ui]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 2, { { 0.0, BD }, { 1.03, SD }, { 2.0, CH } }), 0.0);
    s.place (block ("B", 1, { { 0.0, BD } }), s.endBeat());
    t.p.setSong (s);
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    auto opened = -1;
    track.onEditBlock = [&opened] (int b) { opened = b; };
    t.p.flushUndoStep();
    const auto run = [&track, &t] (int block, const juce::String& path, double at = -1.0)
    {
        // The item at `path` ("Sub|Item" for a submenu's), run as clicking it does; false if not there or greyed out.
        auto menu = track.menuFor (block, at);
        const auto parts = juce::StringArray::fromTokens (path, "|", {});
        std::function<bool (juce::PopupMenu&, int)> find = [&] (juce::PopupMenu& m, int depth)
        {
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            {
                auto& item = it.getItem();
                if (item.text != parts[depth])
                    continue;
                if (depth + 1 < parts.size())
                    return item.subMenu != nullptr && find (*item.subMenu, depth + 1);
                if (! item.isEnabled || item.action == nullptr)
                    return false;
                item.action();
                t.p.flushUndoStep();   // each its own undo step (the editor's timer does this)
                return true;
            }
            return false;
        };
        return find (menu, 0);
    };

    // Nothing copied yet: PASTE greyed out.
    CHECK (! run (0, "Paste After"));
    CHECK (run (0, "Edit MIDI|Grid Editor..."));
    CHECK (opened == 0);

    // A groove from GROOVES with only HI-HAT ticked: the clipboard has only its hats.
    const auto pack = t.folder.getChildFile ("Pack");
    writeMidi (pack.getChildFile ("Verse 01.mid"), { { 0.0, 36 }, { 0.5, 42 }, { 1.5, 42 }, { 1.0, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    page.getTable().selectRow (0);
    page.toggleDrumPart ((int) grooves::DrumPart::hats);
    REQUIRE (page.copyRowForSong (0));
    REQUIRE (t.p.getSongClipboard().has_value());
    CHECK (song::slotsIn (*t.p.getSongClipboard()) == bit (CH));

    // PASTE DRUMS ONTO IT: HI-HAT: block A's hats replaced by the groove's (twice round, 2 bars).
    CHECK (run (0, "Paste Drums Onto It|HI-HAT (2)"));
    auto hats = 0;
    for (const auto& h : t.p.getSong().blocks[0].hits)
        hats += h.slot == CH;
    CHECK (hats == 4);
    // REMOVE DRUMS: KICK; QUANTIZE 1/4: the snare on 1; VELOCITY 50%.
    CHECK (run (0, "Remove Drums|KICK (1)"));
    CHECK ((song::slotsIn (t.p.getSong().blocks[0]) & bit (BD)) == 0);
    CHECK (run (0, "Quantize|1/4"));
    for (const auto& h : t.p.getSong().blocks[0].hits)
        if (h.slot == SD)
            CHECK (h.beat == 1.0);   // the late snare (1.03) on the beat
    CHECK (run (0, "Velocity|50%"));
    // SPLIT HERE at beat 4 (A is two bars), MERGE WITH NEXT back.
    CHECK (run (0, "Split Here", 4.2));
    REQUIRE (t.p.getSong().blocks.size() == 3);
    CHECK (t.p.getSong().blocks[1].start == 4.0);
    CHECK (run (0, "Merge With Next"));
    CHECK (t.p.getSong().blocks.size() == 2);
    // COPY a block, PASTE AFTER the first: the copy goes between, B moved along.
    CHECK (run (1, "Copy"));
    CHECK (run (0, "Paste After"));
    REQUIRE (t.p.getSong().blocks.size() == 3);
    CHECK (names (t.p.getSong()).joinIntoString (",") == "A,B,B");
    CHECK (starts (t.p.getSong()) == std::vector<double> { 0.0, 8.0, 12.0 });
    // The seven edits undone one by one: back to the song as it was; one more: before it was set.
    for (int i = 0; i < 7; ++i)
        t.p.undo();
    CHECK (t.p.getSong() == s);
    t.p.undo();
    CHECK (t.p.getSong().isEmpty());
}

//==============================================================================
// THE SONG TRACK part 3 (0.10.0): the song creator.
namespace
{
    grooves::Entry entryAt (const juce::File& f)
    {
        grooves::Entry e;
        e.file = f;
        e.name = f.getFileNameWithoutExtension();
        e.part = grooves::partOf (f);
        return e;
    }
    std::array<int, song::numSongParts> countsOf (std::initializer_list<std::pair<grooves::Part, int>> c)
    {
        std::array<int, song::numSongParts> out {};
        for (const auto& [p, n] : c)
            out[(size_t) p] = n;
        return out;
    }
}

TEST_CASE ("SONG CREATOR: structures as text; ours; a song's structure; which groove each section takes", "[song][creator]")
{
    using grooves::Part;
    const auto s = song::decode ("Intro 4, verse 8, Pre-Chorus 2, Nonsense 3, Fill 0, Chorus 8");
    REQUIRE (s.size() == 4);   // the unknown word and the empty fill left out
    CHECK (s[1] == song::Section { Part::verse, 8 });
    CHECK (s[2].part == Part::preChorus);
    CHECK (song::encode (s) == "Intro 4, Verse 8, Pre-Chorus 2, Chorus 8");
    CHECK (song::decode (song::encode (s)) == s);
    for (const auto& f : song::factoryStructures())
    {
        CHECK (! f.sections.empty());
        CHECK (! f.user);
        CHECK (f.totalBars() % 4 == 1);   // whole four-bar phrases and the ending's bar
    }
    CHECK (song::factoryStructures()[0].totalBars() == 37);

    song::Song song;
    song.place (block ("V", 2), 0.0);
    song.place (block ("C", 1), song.endBeat());
    song.blocks[1].part = Part::chorus;
    song.blocks[0].part = Part::verse;
    song.blocks[1].length = 5.0;   // a bar and a beat: one bar
    CHECK (song::structureOf (song) == std::vector<song::Section> { { Part::verse, 2 }, { Part::chorus, 1 } });

    // Stand-ins: a missing pre-chorus is a verse, a missing bridge a chorus, a missing ending a fill; a missing fill: none.
    const auto counts = countsOf ({ { Part::verse, 2 }, { Part::chorus, 1 }, { Part::fill, 1 } });
    CHECK (song::standInFor (Part::preChorus, counts) == (int) Part::verse);
    CHECK (song::standInFor (Part::bridge, counts) == (int) Part::chorus);
    CHECK (song::standInFor (Part::ending, counts) == (int) Part::fill);
    CHECK (song::standInFor (Part::intro, counts) == (int) Part::verse);
    CHECK (song::standInFor (Part::fill, countsOf ({ { Part::verse, 2 } })) == -1);
    // Each time a part comes back, its next groove, round and round.
    const auto picks = song::pick (song::decode ("Verse 8, Chorus 8, Verse 8, Chorus 8, Verse 8, Pre-Chorus 2"), counts);
    REQUIRE (picks.size() == 6);
    CHECK ((picks[0].part == (int) Part::verse && picks[0].index == 0));
    CHECK ((picks[2].part == (int) Part::verse && picks[2].index == 1));
    CHECK ((picks[4].part == (int) Part::verse && picks[4].index == 0));
    CHECK ((picks[3].part == (int) Part::chorus && picks[3].index == 0));
    CHECK ((picks[5].part == (int) Part::verse && picks[5].index == 1));   // the pre-chorus's stand-in: the verses' next
    CHECK (song::pick (song::decode ("Fill 1"), countsOf ({ { Part::verse, 1 } }))[0].part == -1);
}

TEST_CASE ("SONG CREATOR: the grooves round a source: its song folder, up a folder for fills kept apart, never past the linked folder", "[song][creator]")
{
    using grooves::Part;
    const juce::File root ("/packs/Mine");
    std::vector<grooves::Entry> all;
    for (const auto* p : { "Rock 96 BPM/Verse 02.mid", "Rock 96 BPM/Verse 01.mid", "Rock 96 BPM/Chorus 01.mid", "Rock 96 BPM/Fill 01.mid",
                           "Funk/Verse 01.mid", "Heavy/Grooves/Grooves 01.mid", "Heavy/Grooves/Grooves 02.mid", "Heavy/Fills/Fills 01.mid",
                           "Alone.mid" })
        all.push_back (entryAt (root.getChildFile (p)));

    auto c = song::variationsOf (root.getChildFile ("Rock 96 BPM/Verse 01.mid"), all, root);
    REQUIRE (c[(size_t) Part::verse].size() == 2);
    CHECK (c[(size_t) Part::verse][0].name == "Verse 01");   // the source first
    CHECK (c[(size_t) Part::chorus].size() == 1);
    CHECK (c[(size_t) Part::fill].size() == 1);
    CHECK (c[(size_t) Part::beat].empty());   // nothing from Funk

    // "Grooves" holds only beats: one folder up, its fills too (not Rock's or Funk's).
    c = song::variationsOf (root.getChildFile ("Heavy/Grooves/Grooves 02.mid"), all, root);
    REQUIRE (c[(size_t) Part::beat].size() == 2);
    CHECK (c[(size_t) Part::beat][0].name == "Grooves 02");
    CHECK (c[(size_t) Part::fill].size() == 1);
    CHECK (c[(size_t) Part::verse].empty());

    // At the linked folder itself: no further.
    c = song::variationsOf (root.getChildFile ("Alone.mid"), all, root);
    auto n = 0;
    for (const auto& col : c)
        n += (int) col.size();
    CHECK (n == (int) all.size());   // the linked folder holds them all; above it: nothing more
}

TEST_CASE ("SONG CREATOR: a song made from a structure (stand-ins, a missing fill playing on), heard on its samples; structures saved", "[song][creator][song-player]")
{
    using grooves::Part;
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    const auto kick = t.p.homeNote (BD), snare = t.p.homeNote (SD);
    writeMidi (pack.getChildFile ("Rock/Verse 01.mid"), { { 0.0, kick } });
    writeMidi (pack.getChildFile ("Rock/Verse 02.mid"), { { 2.0, kick } });
    writeMidi (pack.getChildFile ("Rock/Chorus 01.mid"), { { 1.0, snare } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    const auto columns = t.p.songCreatorColumns (pack.getChildFile ("Rock/Verse 02.mid"));
    REQUIRE (columns[(size_t) Part::verse].size() == 2);
    CHECK (columns[(size_t) Part::verse][0].name == "Verse 02");

    // No intro (a verse plays it, coloured as the intro), no fill (the verse before plays on), no ending (a chorus).
    const auto made = t.p.songFromStructure (song::decode ("Intro 1, Verse 2, Fill 1, Chorus 1, Verse 1, Ending 1"), columns);
    REQUIRE (made.blocks.size() == 5);
    CHECK (names (made).joinIntoString (",") == "Verse 02,Verse 01,Chorus 01,Verse 02,Chorus 01");
    CHECK (starts (made) == std::vector<double> { 0.0, 4.0, 16.0, 20.0, 24.0 });
    CHECK (made.blocks[0].part == Part::intro);
    CHECK (made.blocks[1].length == 12.0);   // its two bars and the fill's
    CHECK (made.blocks[4].part == Part::ending);
    CHECK (made.endBeat() == 28.0);

    // Heard: the chorus (beat 16) snare on beat 17: sample 408000 at 120 BPM.
    song::Song firstPart;
    firstPart.blocks.assign (made.blocks.begin(), made.blocks.begin() + 3);
    t.p.setSong (firstPart);
    t.host.ppq = 15.0;
    const auto o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 2 * 24000);   // two beats after beat 15

    // Your structures: saved (a name again: replaced), listed after ours, renamed, removed, in the presets folder.
    CHECK (t.p.saveSongStructure ("Mine", song::decode ("Verse 4, Chorus 4")));
    CHECK (t.p.saveSongStructure ("Mine", song::decode ("Verse 8, Chorus 8")));
    CHECK (t.p.songStructuresFile().existsAsFile());
    CHECK (t.p.songStructuresFile().getParentDirectory() == t.p.getRigLibrary().getFolder());
    auto all = t.p.getSongStructures();
    REQUIRE (all.size() == song::factoryStructures().size() + 1);
    CHECK (all.back().user);
    CHECK (all.back().totalBars() == 16);
    CHECK (t.p.renameSongStructure ("Mine", "Ours Now"));
    CHECK (t.p.getSongStructures().back().name == "Ours Now");
    CHECK (! t.p.renameSongStructure ("Nothing", "X"));
    CHECK (t.p.removeSongStructure ("Ours Now"));
    CHECK (t.p.getSongStructures().size() == song::factoryStructures().size());
    CHECK (! t.p.saveSongStructure ("  ", song::decode ("Verse 4")));
}

TEST_CASE ("SONG CREATOR panel: a source's columns, MAKE SONG / ADD TO END (undoable), the grooves' menu, SAVE SONG'S STRUCTURE", "[song][creator][ui]")
{
    using grooves::Part;
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    writeMidi (pack.getChildFile ("Rock/Verse 01.mid"), { { 0.0, 36 } });
    writeMidi (pack.getChildFile ("Rock/Chorus 01.mid"), { { 1.0, 38 } });
    writeMidi (pack.getChildFile ("Rock/Chorus 02.mid"), { { 3.0, 38 } });
    writeMidi (pack.getChildFile ("Rock/Fill 01.mid"), { { 3.5, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    t.p.flushUndoStep();

    SongCreator creator (t.p);
    creator.setBounds (0, 0, 1476, 672);
    auto chosen = -1;
    creator.chosenBlock = [&chosen] { return chosen; };
    creator.open();
    CHECK (creator.getStructures().size() == song::factoryStructures().size());
    creator.setSource (pack.getChildFile ("Rock/Verse 01.mid"));
    CHECK (creator.getColumns()[(size_t) Part::chorus].size() == 2);
    CHECK (! creator.cellOf ((int) Part::chorus, 1).isEmpty());

    // Clicking a groove plays it.
    creator.audition ((int) Part::chorus, 1);
    CHECK (t.p.getGrooveFile().getFileName() == "Chorus 02.mid");

    // MAKE SONG with ours (the first): 37 bars; ADD TO END: twice as long; undo: one then none.
    creator.chooseStructure (0);
    creator.makeSong (false);
    t.p.flushUndoStep();
    CHECK (t.p.getSong().endBeat() == 37 * 4.0);
    const auto once = t.p.getSong();
    creator.makeSong (true);
    t.p.flushUndoStep();
    CHECK (t.p.getSong().endBeat() == 2 * 37 * 4.0);
    t.p.undo();
    CHECK (t.p.getSong() == once);
    t.p.undo();
    CHECK (t.p.getSong().isEmpty());
    t.p.redo();

    // The grooves' menu: REPLACE ALL "Chorus" ON TRACK: every chorus block plays Chorus 02 now (their lengths kept).
    juce::StringArray items;
    auto menu = creator.grooveMenu ((int) Part::chorus, 1);
    std::map<juce::String, std::function<void()>> actions;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        items.add (it.getItem().text);
        actions[it.getItem().text] = it.getItem().action;
    }
    CHECK (items.contains ("Replace All \"Chorus\" on Track (2)"));
    CHECK (items.contains ("Add to Song"));
    CHECK (creator.replaceAll ((int) Part::chorus, 1) == 2);
    for (const auto& b : t.p.getSong().blocks)
        if (b.part == Part::chorus)
        {
            CHECK (b.name == "Chorus 02");
            CHECK (b.length == 32.0);
        }
    // REPLACE THE CHOSEN BLOCK (the intro, block 0) with the fill; with none chosen: nothing.
    CHECK (! creator.replaceChosenBlock ((int) Part::fill, 0));
    chosen = 0;
    CHECK (creator.replaceChosenBlock ((int) Part::fill, 0));
    CHECK (t.p.getSong().blocks[0].name == "Fill 01");
    CHECK (t.p.getSong().blocks[0].part == Part::intro);   // still the intro, its 4 bars
    CHECK (t.p.getSong().blocks[0].length == 16.0);
    // ADD TO SONG at a beat; COPY.
    const auto added = creator.addToSong ((int) Part::verse, 0);
    CHECK (added == (int) t.p.getSong().blocks.size() - 1);
    actions["Copy"]();
    REQUIRE (t.p.getSongClipboard().has_value());
    CHECK (t.p.getSongClipboard()->name == "Chorus 02");

    // SAVE SONG'S STRUCTURE: the song track's parts and lengths, chosen in the list after.
    CHECK (creator.saveStructure ("From My Song"));
    CHECK (creator.getStructures().back().name == "From My Song");
    CHECK (creator.getChosenStructure() == (int) creator.getStructures().size() - 1);
    CHECK (creator.getStructures().back().sections == song::structureOf (t.p.getSong()));
    CHECK (creator.structureMenu (creator.getChosenStructure()).getNumItems() == 5);   // header, make, add, rename, remove
    CHECK (creator.structureMenu (0).getNumItems() == 3);   // ours: no rename or remove
}

//==============================================================================
// THE SONG TRACK part 4a (0.11.0): tempo and time signature changes, MIDI import.
TEST_CASE ("SONG 0.11: time signature changes: bar lines, snapping, a change on a bar line, kept tidy; resize and split by them", "[song][song-time]")
{
    song::Song s;
    s.setMeter (16.0, 3, 4);   // from bar 5: 3/4
    CHECK (s.barLines (24.0) == std::vector<double> { 0.0, 4.0, 8.0, 12.0, 16.0, 19.0, 22.0, 25.0 });   // and the one after
    CHECK (s.snap (18.0) == 19.0);
    CHECK (s.snap (17.4) == 16.0);
    CHECK (s.barAt (20.0) == 5);
    CHECK (s.beatsPerBarAt (20.0) == 3.0);
    CHECK (s.meterAt (15.9).numerator == 4);
    s.setMeter (17.0, 6, 8);   // the nearest bar line is 16: that change replaced
    REQUIRE (s.meters.size() == 1);
    CHECK ((s.meters[0].beat == 16.0 && s.meters[0].numerator == 6 && s.meters[0].denominator == 8));
    s.setMeter (22.0, 6, 8);   // the same as before it: no change
    CHECK (s.meters.size() == 1);
    s.setMeter (0.0, 2, 4);    // the first bars
    CHECK ((s.numerator == 2 && s.denominator == 4));
    CHECK (s.meters[0].beat == 16.0);   // still on a bar line (2/4 bars: 16 is one)
    s.setMeter (0.0, 3, 4);    // now 16 isn't a bar line: the change moves to the nearest (15)
    REQUIRE (s.meters.size() == 1);
    CHECK (s.meters[0].beat == 15.0);
    s.removeMeter (15.0);
    CHECK (s.meters.empty());

    // A block resized and split on the song's bar lines.
    song::Song r;
    r.place (block ("A", 4), 0.0);   // 16 beats
    r.setMeter (8.0, 3, 4);           // bars: 0 4 8 11 14 17 ...
    r.resize (0, 12.4);               // the nearest bar line to 12.4: 11
    CHECK (r.blocks[0].length == 11.0);
    CHECK (r.split (0, 9.0) == 1);     // the nearest bar line: 8
    CHECK (r.blocks[1].start == 8.0);
    CHECK (r.blocks[1].length == 3.0);
}

TEST_CASE ("SONG 0.11: tempo changes: steady and gliding, beats to seconds and back; adding, moving, removing", "[song][song-time]")
{
    song::Song s;
    CHECK (s.bpmAt (3.0, 90.0) == 90.0);   // none: the DAW's
    CHECK_THAT (s.secondsAt (3.0, 90.0), WithinAbs (2.0, 1e-12));
    CHECK (s.addTempo (8.0, 60.0) == 0);
    CHECK (s.addTempo (0.0, 120.0) == 0);   // in order
    CHECK_THAT (s.secondsAt (8.0, 1.0), WithinAbs (4.0, 1e-9));    // 8 beats at 120
    CHECK_THAT (s.secondsAt (12.0, 1.0), WithinAbs (8.0, 1e-9));   // and 4 at 60
    CHECK_THAT (s.beatAtSeconds (8.0, 1.0), WithinAbs (12.0, 1e-9));
    CHECK_THAT (s.beatAtSeconds (3.0, 1.0), WithinAbs (6.0, 1e-9));

    // A glide from 60 to 120 over 4 beats: 4 ln 2 seconds (60 / k ln (b / a), k = 15 BPM a beat), half way 90 BPM.
    song::Song g;
    g.addTempo (0.0, 60.0);
    g.addTempo (4.0, 120.0);
    g.tempos[0].ramp = true;
    CHECK_THAT (g.bpmAt (2.0, 1.0), WithinAbs (90.0, 1e-9));
    CHECK_THAT (g.secondsAt (4.0, 1.0), WithinAbs (4.0 * std::log (2.0), 1e-9));
    CHECK_THAT (g.secondsAt (6.0, 1.0), WithinAbs (4.0 * std::log (2.0) + 1.0, 1e-9));
    for (const auto b : { 0.5, 1.7, 3.99, 4.0, 9.0 })
        CHECK_THAT (g.beatAtSeconds (g.secondsAt (b, 1.0), 1.0), WithinAbs (b, 1e-9));

    // Moved: kept between its neighbours; changed in place; removed.
    s.addTempo (16.0, 140.0);
    s.moveTempo (1, 40.0, 70.0);   // past its next one: stopped just short of it
    CHECK (s.tempos[1].beat < 16.0);
    CHECK (s.tempos[1].bpm == 70.0);
    CHECK (s.addTempo (0.0, 100.0) == 0);   // one on that beat: changed
    CHECK (s.tempos.size() == 3);
    CHECK (s.tempos[0].bpm == 100.0);
    s.removeTempo (1);
    CHECK (s.tempos.size() == 2);

    // Saved and read back; and in the MIDI file: the tempos (a glide as steps) and time signatures.
    g.setMeter (8.0, 3, 4);
    g.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    CHECK (song::Song::fromXml (*g.toXml()) == g);
    const auto midi = g.toMidi (100.0, [] (int) { return 36; });
    std::vector<std::pair<double, double>> tempos;
    std::vector<std::pair<double, int>> sigs;
    for (const auto* e : *midi.getTrack (0))
    {
        if (e->message.isTempoMetaEvent())
            tempos.emplace_back (e->message.getTimeStamp() / 960.0, 60.0 / e->message.getTempoSecondsPerQuarterNote());
        if (e->message.isTimeSignatureMetaEvent())
        {
            int n = 0, d = 0;
            e->message.getTimeSignatureInfo (n, d);
            sigs.emplace_back (e->message.getTimeStamp() / 960.0, n);
        }
    }
    REQUIRE (tempos.size() == 17);   // 16 steps a 16th apart, then 120
    CHECK (tempos.front().first == 0.0);
    CHECK_THAT (tempos.front().second, WithinAbs (60.0 + 15.0 * 0.125, 0.01));
    CHECK_THAT (tempos.back().second, WithinAbs (120.0, 0.01));
    CHECK (sigs == std::vector<std::pair<double, int>> { { 0.0, 4 }, { 8.0, 3 } });
}

TEST_CASE ("SONG 0.11 heard: PLAY through tempo changes (a step, a glide), each hit on its sample; the DAW's tempo leads while it plays", "[song][song-time][song-player]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("A", 2, { { 0.0, BD }, { 4.0, BD }, { 6.0, BD } }), 0.0);
    s.addTempo (0.0, 120.0);
    s.addTempo (4.0, 60.0);
    t.p.setSong (s);
    t.p.playSong (0.0);
    auto o = onsets (t.play (5.0));
    REQUIRE (o.size() == 3);
    CHECK (o[0] == 0);
    CHECK (o[1] == 96000);     // 4 beats at 120: 2 s
    CHECK (o[2] == 192000);    // and 2 at 60: 2 s more

    // A glide 60 -> 120 over the first 4 beats: the second kick at 4 ln 2 s.
    s.tempos[0] = { 0.0, 60.0, true };
    s.tempos[1] = { 4.0, 120.0, false };
    t.p.setSong (s);
    t.p.playSong (0.0);
    o = onsets (t.play (4.5));
    REQUIRE (o.size() >= 2);
    CHECK (std::abs (o[1] - (int) std::floor (4.0 * std::log (2.0) * sampleRate)) <= 1);
    // From bar 2 (beat 4: 120 from there): the kick on 6 one beat in.
    t.p.playSong (4.0);
    o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 0);
    REQUIRE (o.size() >= 2);
    CHECK (o[1] == 48000);   // two beats at 120 (6 - 4)

    // While the DAW plays: its tempo (120), not the song's: the kick on 4 at 2 s.
    t.p.stopSong();
    t.host.playing = true;
    t.host.ppq = 0.0;
    o = onsets (t.play (2.5));
    REQUIRE (o.size() >= 2);
    CHECK (o[1] == 96000);
}

namespace
{
    // A song's MIDI as a DAW would write it: tempo, time signatures, markers, notes.
    juce::MidiFile songFile (bool markers)
    {
        juce::MidiMessageSequence meta, notes;
        meta.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / 100.0)));
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (3, 4).withTimeStamp (16 * 480.0));
        meta.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / 140.0)).withTimeStamp (16 * 480.0));
        if (markers)
            for (const auto& [beat, text] : std::vector<std::pair<double, const char*>> { { 0.0, "Intro" }, { 8.0, "Verse 1" }, { 16.0, "Chorus" } })
                meta.addEvent (juce::MidiMessage::textMetaEvent (6, text).withTimeStamp (beat * 480.0));
        for (int b = 0; b < 24; ++b)
        {
            notes.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100).withTimeStamp (b * 480.0));
            notes.addEvent (juce::MidiMessage::noteOff (10, 36).withTimeStamp (b * 480.0 + 60.0));
        }
        notes.addEvent (juce::MidiMessage::noteOn (10, 99, (juce::uint8) 100).withTimeStamp (2 * 480.0));   // no drum
        juce::MidiFile f;
        f.setTicksPerQuarterNote (480);
        f.addTrack (meta);
        f.addTrack (notes);
        return f;
    }
}

TEST_CASE ("SONG 0.11: IMPORT MIDI FILE: a block a marker (named, coloured), its tempo and time signatures or not; after a song", "[song][song-time][import]")
{
    using grooves::Part;
    auto map = generalMidi();
    auto s = song::importMidi (songFile (true), map, "My Song", true);
    REQUIRE (s.blocks.size() == 3);
    CHECK (names (s).joinIntoString (",") == "Intro,Verse 1,Chorus");
    CHECK (s.blocks[0].part == Part::intro);
    CHECK (s.blocks[1].part == Part::verse);
    CHECK (s.blocks[2].part == Part::chorus);
    CHECK (starts (s) == std::vector<double> { 0.0, 8.0, 16.0 });
    CHECK (s.blocks[2].length == 9.0);   // its last kick on beat 23: to the next 3/4 bar line (25)
    CHECK (s.blocks[1].hits.size() == 8);
    CHECK (s.tempos.size() == 2);
    CHECK_THAT (s.tempos[1].bpm, WithinAbs (140.0, 0.01));
    REQUIRE (s.meters.size() == 1);
    CHECK ((s.meters[0].beat == 16.0 && s.meters[0].numerator == 3));

    // Without its tempo: the song's own (none), 4/4 throughout: the end on beat 24's bar line.
    s = song::importMidi (songFile (true), map, "My Song", false);
    CHECK (s.tempos.empty());
    CHECK (s.meters.empty());
    CHECK (s.endBeat() == 24.0);
    // No markers: one block, named for the file.
    s = song::importMidi (songFile (false), map, "My Song", false);
    REQUIRE (s.blocks.size() == 1);
    CHECK (s.blocks[0].name == "My Song");
    CHECK (s.blocks[0].hits.size() == 24);   // the note no drum plays left out
    // Timed in SMPTE frames: nothing.
    auto smpte = songFile (false);
    smpte.setSmpteTimeFormat (25, 40);
    CHECK (song::importMidi (smpte, map, "x", true).isEmpty());

    // Through the processor (the kit's map: ROOT here, 36 its kick if ROOT is C1... any note it plays): an empty song is
    // the file's; a second import goes after it, its tempo changes moved along with it.
    Plugin t;
    const auto file = t.folder.getChildFile ("Song.mid");
    file.getParentDirectory().createDirectory();
    {
        juce::FileOutputStream out (file);
        auto f = songFile (true);
        // On the kit's own kick note, whatever the map.
        juce::MidiFile mapped;
        mapped.setTicksPerQuarterNote (480);
        for (int tr = 0; tr < f.getNumTracks(); ++tr)
        {
            juce::MidiMessageSequence seq (*f.getTrack (tr));
            for (auto* e : seq)
                if (e->message.isNoteOnOrOff() && e->message.getNoteNumber() == 36)
                    e->message.setNoteNumber (t.p.homeNote (BD));
            mapped.addTrack (seq);
        }
        mapped.writeTo (out);
    }
    CHECK (t.p.importSongMidi (file, true) == 3);
    CHECK (t.p.getSong().endBeat() == 25.0);
    CHECK (t.p.getSong().tempos.size() == 2);
    CHECK (t.p.importSongMidi (file, true) == 3);
    const auto& both = t.p.getSong();
    REQUIRE (both.blocks.size() == 6);
    CHECK (both.blocks[3].start == 25.0);   // the next bar line after the first song
    CHECK (both.tempos.size() == 4);
    CHECK (both.tempos[2].beat == 25.0);
    CHECK (both.meterAt (26.0).numerator == 4);   // the second song's 4/4 from its start
    CHECK (both.meterAt (25.0 + 17.0).numerator == 3);
    CHECK (t.p.importSongMidi (t.folder.getChildFile ("none.mid"), true) == 0);
}

TEST_CASE ("SONG 0.11 strip: TEMPO view: double-click adds, drag moves, right-click ramps or removes; TIME view's menu; TRACK menu", "[song][song-time][ui]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 4, { { 0.0, BD } }), 0.0);
    t.p.setSong (s);
    t.p.flushUndoStep();
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    track.setView (SongTrack::View::tempo);
    CHECK (track.getView() == SongTrack::View::tempo);

    const auto mouse = [&track] (juce::Point<float> at, juce::Point<float> from, juce::ModifierKeys mods)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &track, &track, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 2, false);
    };
    const auto left = juce::ModifierKeys::leftButtonModifier;
    // A double-click at beat 8, at 100 BPM's height: a tempo change there.
    const auto at = track.pointOf ({ 8.0, 100.0 });
    track.mouseDoubleClick (mouse (at, at, left));
    REQUIRE (t.p.getSong().tempos.size() == 1);
    CHECK (t.p.getSong().tempos[0].beat == 8.0);
    CHECK (std::abs (t.p.getSong().tempos[0].bpm - 100.0) <= 1.0);
    t.p.flushUndoStep();
    // Dragged to beat 12 at 140 BPM.
    const auto to = track.pointOf ({ 12.0, 140.0 });
    track.mouseDown (mouse (at, at, left));
    track.mouseDrag (mouse (to, at, left));
    track.mouseUp (mouse (to, at, left));
    CHECK (t.p.getSong().tempos[0].beat == 12.0);
    CHECK (std::abs (t.p.getSong().tempos[0].bpm - 140.0) <= 1.0);
    t.p.flushUndoStep();
    // Another, then the first's menu: RAMP TO THE NEXT, then REMOVE.
    t.p.setSong ([&] { auto x = t.p.getSong(); x.addTempo (0.0, 90.0); return x; }());
    const auto run = [] (juce::PopupMenu menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text && it.getItem().isEnabled && it.getItem().action != nullptr)
            {
                it.getItem().action();
                return true;
            }
        return false;
    };
    CHECK (track.tempoAt (track.pointOf (t.p.getSong().tempos[0])) == 0);
    CHECK (run (track.tempoMenu (0), "Ramp to the Next"));
    CHECK (t.p.getSong().tempos[0].ramp);
    CHECK (run (track.tempoMenu (0), "120 BPM"));
    CHECK (t.p.getSong().tempos[0].bpm == 120.0);
    CHECK (run (track.tempoMenu (0), "Remove"));
    CHECK (t.p.getSong().tempos.size() == 1);

    // TIME: from bar 3, 3 beats a bar, then 8ths; removed.
    track.setView (SongTrack::View::time);
    CHECK (run (track.meterMenu (8.3), "3"));
    REQUIRE (t.p.getSong().meters.size() == 1);
    CHECK (t.p.getSong().meters[0].beat == 8.0);
    CHECK (run (track.meterMenu (8.0), "8 (eighth notes)"));
    CHECK (t.p.getSong().meterAt (9.0).denominator == 8);
    CHECK (run (track.meterMenu (8.0), "Remove This Change"));
    CHECK (t.p.getSong().meters.empty());

    // TRACK: removing all tempo changes.
    CHECK (run (track.trackMenu(), "Remove All Tempo Changes (the DAW's tempo)"));
    CHECK (t.p.getSong().tempos.empty());
    CHECK (! run (track.trackMenu(), "Remove All Time Signature Changes"));   // none: greyed out
}

//==============================================================================
// THE FAMILY's COLOURS (0.11.0, the owner: only the red / pink / orange range, pearl white and onyx).
#include "ui/ParagonLookAndFeel.h"

namespace
{
    bool inFamily (juce::Colour c)
    {
        // Greys, near black and near white (onyx, pearl) or a hue from pink (320) through red to orange (42).
        const auto hue = c.getHue() * 360.0f;
        return c.getSaturation() < 0.25f || c.getBrightness() < 0.15f || (c.getSaturation() < 0.12f && c.getBrightness() > 0.85f)
            || hue >= 318.0f || hue <= 42.0f;
    }
}

TEST_CASE ("COLOURS: the song parts' and the look-and-feel's (sliders, toggles, highlights) are the family's", "[song][colours][ui]")
{
    for (int k = 0; k < grooves::partNames.size(); ++k)
    {
        INFO (grooves::partNames[k]);
        CHECK (inFamily (song::colourOf ((grooves::Part) k)));
    }
    ParagonLookAndFeel look;
    for (const int id : { (int) juce::Slider::thumbColourId, (int) juce::Slider::trackColourId, (int) juce::Slider::rotarySliderFillColourId,
                           (int) juce::ToggleButton::tickColourId, (int) juce::TextEditor::highlightColourId, (int) juce::ScrollBar::thumbColourId,
                           (int) juce::ComboBox::arrowColourId, (int) juce::ListBox::backgroundColourId, (int) juce::TreeView::selectedItemBackgroundColourId })
    {
        INFO ("colour id " << juce::String::toHexString (id));
        CHECK (inFamily (look.findColour (id)));
    }
    for (const auto c : { ParagonColours::comp, ParagonColours::modulation, ParagonColours::delay, ParagonColours::reverb,
                          ParagonColours::filterPitch, ParagonColours::glitch, ParagonColours::pitch, ParagonColours::loFi })
        CHECK (inFamily (c));
}

//==============================================================================
// THE SONG TRACK part 4b (0.12.0): RECORD and BOUNCE.
namespace
{
    void setParam (ParagonX1AudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.getParameters().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // `seconds` of the plugin with `notes` (sample, note) played into it.
    void playNotes (ParagonX1AudioProcessor& p, Host& host, double seconds, std::vector<std::pair<int, int>> notes)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
        {
            juce::MidiBuffer midi;
            for (const auto& [at, note] : notes)
                if (at >= done && at < done + blockSize)
                    midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), at - done);
            buffer.clear();
            p.processBlock (buffer, midi);
            if (host.playing)
                host.ppq += blockSize / sampleRate * host.bpm / 60.0;
        }
    }

    std::vector<std::pair<double, int>> hitsOf (const song::Block& b)
    {
        std::vector<std::pair<double, int>> out;
        for (const auto& h : b.hits)
            out.emplace_back (std::round (h.beat * 1.0e6) / 1.0e6, h.slot);
        return out;
    }

    juce::AudioBuffer<float> readWav (const juce::File& f, double& rate, int& bits)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
        REQUIRE (reader != nullptr);
        rate = reader->sampleRate;
        bits = (int) reader->bitsPerSample;
        juce::AudioBuffer<float> out ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&out, 0, (int) reader->lengthInSamples, 0, true, true);
        return out;
    }
}

TEST_CASE ("SONG 0.12 RECORD with the DAW: notes at their beats, a take's block growing bar by bar, a new one after a gap; one undo step", "[song][record]")
{
    Plugin t;
    t.p.flushUndoStep();
    const auto kick = t.p.homeNote (BD), snare = t.p.homeNote (SD);
    // Not armed: nothing.
    playNotes (t.p, t.host, 0.5, { { 100, kick } });
    CHECK (t.p.mergeRecordedNotes() == 0);
    CHECK (t.p.getSong().isEmpty());

    t.host.ppq = 0.0;
    t.p.setSongRecording (true);
    CHECK (t.p.isSongRecording());
    // At 120: beat 1 (24000), beat 3 (72000), beat 5 (bar 2: the block grows), beat 9 (bar 3: no; bar 3 is next to bar 2:
    // grows again), a note the kit doesn't play (127).
    playNotes (t.p, t.host, 5.0, { { 24000, kick }, { 72000, snare }, { 120000, kick }, { 216000, snare }, { 30000, 127 } });
    CHECK (t.p.mergeRecordedNotes() == 4);
    const auto& s = t.p.getSong();
    REQUIRE (s.blocks.size() == 1);
    CHECK (s.blocks[0].name == "Recorded");
    CHECK (s.blocks[0].start == 0.0);
    CHECK (s.blocks[0].length == 12.0);
    CHECK (hitsOf (s.blocks[0]) == std::vector<std::pair<double, int>> { { 1.0, BD }, { 3.0, SD }, { 5.0, BD }, { 9.0, SD } });
    // After a gap (bar 6): a block of its own.
    t.host.ppq = 20.0;
    playNotes (t.p, t.host, 1.0, { { 12000, kick } });   // beat 20.5
    t.p.setSongRecording (false);
    REQUIRE (t.p.getSong().blocks.size() == 2);
    CHECK (t.p.getSong().blocks[1].start == 20.0);
    CHECK (hitsOf (t.p.getSong().blocks[1]) == std::vector<std::pair<double, int>> { { 0.5, BD } });
    // The take: one undo step.
    t.p.flushUndoStep();
    t.p.undo();
    CHECK (t.p.getSong().isEmpty());
}

TEST_CASE ("SONG 0.12 RECORD: on top of a repeating block (written out first), and with PLAY through a tempo change", "[song][record]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    s.resize (0, 12.0);   // the one-bar groove three times
    t.p.setSong (s);
    t.p.setSongRecording (true);
    t.host.ppq = 0.0;
    playNotes (t.p, t.host, 3.5, { { 144000, t.p.homeNote (SD) } });   // beat 6
    t.p.setSongRecording (false);
    const auto& a = t.p.getSong().blocks[0];
    CHECK (a.loopLength == 12.0);
    CHECK (hitsOf (a) == std::vector<std::pair<double, int>> { { 0.0, BD }, { 4.0, BD }, { 6.0, SD }, { 8.0, BD } });

    // PLAY (the DAW stopped) through 120 for 4 beats then 60: a note 2.5 s in is beat 4.5.
    song::Song timed;
    timed.place (block ("B", 2), 0.0);
    timed.addTempo (0.0, 120.0);
    timed.addTempo (4.0, 60.0);
    timed.countIn = false;   // (0.45.0: PLAY's take from its first beat, no count-in bar)
    t.p.setSong (timed);
    t.host.playing = false;
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    playNotes (t.p, t.host, 3.0, { { 120000, t.p.homeNote (BD) } });
    t.p.setSongRecording (false);
    REQUIRE (! t.p.getSong().blocks[0].hits.empty());
    CHECK_THAT (t.p.getSong().blocks[0].hits[0].beat, WithinAbs (4.5, 1e-6));
}

TEST_CASE ("SONG 0.12 BOUNCE: the song as WAV, each hit on its sample (the plugin's delay taken off), the gain, a file per output", "[song][bounce]")
{
    Plugin t;
    t.p.setPlayHead (nullptr);
    const auto folder = t.folder.getChildFile ("Bounced");
    // Empty: nothing.
    {
        ParagonX1AudioProcessor::SongBouncer empty (t.p, {}, folder);
        CHECK (empty.run().isEmpty());
        CHECK (empty.error.isNotEmpty());
    }
    song::Song s;
    s.place (block ("A", 2, { { 0.0, BD }, { 1.5, SD }, { 4.0, BD } }), 0.0);
    t.p.setSong (s);
    // The limiter's look-ahead on: the plugin is late by its latency; the bounce isn't.
    setParam (t.p, ParamIDs::limitOn, 1.0f);
    setParam (t.p, ParamIDs::mbLookahead, 1.0f);

    ParagonX1AudioProcessor::BounceOptions o;
    o.name = "Test Song";
    ParagonX1AudioProcessor::SongBouncer one (t.p, o, folder);
    const auto files = one.run();
    REQUIRE (files.size() == 1);
    CHECK (files[0].getFileName() == "Test Song - Main.wav");
    CHECK (one.progress.load() == 1.0f);
    double rate = 0.0;
    int bits = 0;
    const auto main = readWav (files[0], rate, bits);
    CHECK (rate == sampleRate);
    CHECK (bits == 24);
    CHECK (main.getNumSamples() >= 192000);   // the song's two bars (4 s at 120), then its tail
    std::vector<float> left (main.getReadPointer (0), main.getReadPointer (0) + main.getNumSamples());
    auto o1 = onsets (left);
    REQUIRE (o1.size() == 3);
    CHECK (std::abs (o1[0] - 0) <= 1);
    CHECK (std::abs (o1[1] - 36000) <= 1);   // 1.5 beats
    CHECK (std::abs (o1[2] - 96000) <= 1);   // beat 4
    CHECK (main.getMagnitude (0, main.getNumSamples() - 1000, 1000) < 1.0e-4f);   // it ends quiet

    // +6 dB: twice the level (the limiter's GAIN at 0 leaves the bursts under its ceiling... compared before it acts).
    setParam (t.p, ParamIDs::limitOn, 0.0f);
    ParagonX1AudioProcessor::SongBouncer flat (t.p, o, folder);
    const auto flatFile = flat.run();
    o.gainDb = 6.0f;
    o.bits = 32;
    o.name = "Louder";
    ParagonX1AudioProcessor::SongBouncer louder (t.p, o, folder);
    const auto loudFile = louder.run();
    REQUIRE ((flatFile.size() == 1 && loudFile.size() == 1));
    const auto a = readWav (flatFile[0], rate, bits);
    const auto b = readWav (loudFile[0], rate, bits);
    CHECK (bits == 32);
    CHECK_THAT (b.getMagnitude (0, 0, 2000) / a.getMagnitude (0, 0, 2000), WithinAbs (juce::Decibels::decibelsToGain (6.0f), 0.002));

    // A file per output: the snare on OUT 3: MAIN has the kicks only, OUT 3 the snare.
    setParam (t.p, ParamIDs::drum (SD, "Output"), 2.0f);
    o = {};
    o.perOutput = true;
    o.name = "Routed";
    ParagonX1AudioProcessor::SongBouncer routed (t.p, o, folder);
    const auto parts = routed.run();
    REQUIRE (parts.size() == 2);
    CHECK (parts[0].getFileName() == "Routed - Main.wav");
    CHECK (parts[1].getFileName() == "Routed - Out 3.wav");
    const auto m = readWav (parts[0], rate, bits);
    const auto three = readWav (parts[1], rate, bits);
    std::vector<float> ml (m.getReadPointer (0), m.getReadPointer (0) + m.getNumSamples());
    std::vector<float> tl (three.getReadPointer (0), three.getReadPointer (0) + three.getNumSamples());
    CHECK (onsets (ml) == std::vector<int> { 0, 96000 });
    REQUIRE (onsets (tl).size() == 1);
    CHECK (std::abs (onsets (tl)[0] - 36000) <= 1);
}

TEST_CASE ("SONG 0.12 strip: REC arms recording; the TRACK menu has BOUNCE (greyed with an empty song)", "[song][record][bounce][ui]")
{
    Plugin t;
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    const auto has = [&track] (const juce::String& text)
    {
        auto menu = track.trackMenu();
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text == text)
                return it.getItem().isEnabled;
        return false;
    };
    CHECK (! has ("Bounce to Audio..."));
    t.p.setSong ([] { song::Song s; s.place (block ("A", 1, { { 0.0, BD } }), 0.0); return s; }());
    CHECK (has ("Bounce to Audio..."));
    track.getRecordButton().setToggleState (true, juce::sendNotificationSync);
    CHECK (t.p.isSongRecording());
    track.getRecordButton().setToggleState (false, juce::sendNotificationSync);
    CHECK (! t.p.isSongRecording());
}

//==============================================================================
// FOLLOW DAW (0.12.0).
TEST_CASE ("SONG FOLLOW DAW: off, the DAW playing doesn't play the song; PLAY does (the DAW playing or not); saved; undone", "[song][follow]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 2.0, BD } }), 0.0);
    t.p.setSong (s);
    t.p.flushUndoStep();
    // On (as before): the DAW leads.
    t.host.ppq = 0.0;
    CHECK (onsets (t.play (1.5)) == std::vector<int> { 0, 48000 });

    // Off: the DAW plays, the song doesn't.
    s.followDaw = false;
    t.p.setSong (s);
    t.p.flushUndoStep();
    t.host.ppq = 0.0;
    CHECK (onsets (t.play (1.5)).empty());
    // PLAY, the DAW still playing (at ppq 37, nothing to do with the song): the song from bar 1, on its own clock.
    t.host.ppq = 37.0;
    t.p.playSong (0.0);
    CHECK (onsets (t.play (1.5)) == std::vector<int> { 0, 48000 });
    CHECK (t.p.isSongPlaying());   // the DAW playing didn't take over

    // Saved and read back (on is the default: not written).
    CHECK (! song::Song::fromXml (*t.p.getSong().toXml()).followDaw);
    CHECK (song::Song::fromXml (*song::Song().toXml()).followDaw);
    juce::MemoryBlock state;
    t.p.getStateInformation (state);
    ParagonX1AudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (! other.getSong().followDaw);
    // Undone: on again.
    t.p.undo();
    CHECK (t.p.getSong().followDaw);
}

TEST_CASE ("SONG FOLLOW DAW strip: the button switches it; EXPORT MIDI with it on reminds, with it off doesn't", "[song][follow][ui]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    t.p.setSong (s);
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    CHECK (track.getFollowButton().getToggleState());
    // A drag of EXPORT MIDI (its file made): the reminder.
    MidiExportButton* exportButton = nullptr;
    for (auto* c : track.getChildren())
        if (auto* b = dynamic_cast<MidiExportButton*> (c))
            exportButton = b;
    REQUIRE (exportButton != nullptr);
    CHECK (exportButton->makeDragFile().existsAsFile());
    CHECK (track.getStatusText().contains ("Turn FOLLOW DAW off"));
    // Off: the song says so; no reminder after another export.
    track.getFollowButton().setToggleState (false, juce::sendNotificationSync);
    CHECK (! t.p.getSong().followDaw);
    CHECK (track.getStatusText().contains ("only with PLAY"));
    t.folder.createDirectory();
    CHECK (exportButton->exportTo (t.folder.getChildFile ("song.mid")));
    CHECK (! track.getStatusText().contains ("Turn FOLLOW DAW off"));
}

//==============================================================================
// A GROOVE DRAGGED ONTO THE SEQUENCER's GRID (0.13.0).
TEST_CASE ("SEQUENCER DROP: a groove replaces the pattern shown; ticked parts or a part's row lay onto it; a creator groove; a file; undo", "[seq-drop][ui]")
{
    using grooves::DrumPart;
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    // A one-bar groove: kick on 1 and 3, snare on 2 and 4, eighth hats (the groove library map: 42 closed hat).
    std::vector<std::pair<double, int>> notes { { 0.0, 36 }, { 2.0, 36 }, { 1.0, 38 }, { 3.0, 38 } };
    for (int e = 0; e < 8; ++e)
        notes.push_back ({ e * 0.5, 42 });
    writeMidi (pack.getChildFile ("Rock/Verse 01.mid"), notes);
    writeMidi (pack.getChildFile ("Rock/Chorus 01.mid"), { { 0.0, 36 }, { 0.5, 36 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    t.p.flushUndoStep();

    MainView view (t.p);
    view.setSize (MainView::width, MainView::height);
    auto& page = view.getGroovesPage();
    page.refresh();
    REQUIRE (page.getResults().size() == 2);
    const auto verse = page.getResults()[0].name == "Verse 01" ? 0 : 1;
    auto& grid = view.getDrumPage().getSequencer().getGrid();
    const auto pattern = grid.getPattern();
    const auto rows = [&t, pattern] (int row)
    {
        std::vector<double> starts;
        for (const auto& n : t.p.getModMatrix().drumPatterns[(size_t) pattern].notes)
            if (n.row == row)
                starts.push_back (n.start);
        std::sort (starts.begin(), starts.end());
        return starts;
    };
    const auto drop = [&grid] (const juce::String& d)
    {
        juce::DragAndDropTarget::SourceDetails details (d, nullptr, { 10, 10 });
        REQUIRE (grid.isInterestedInDragSource (details));
        grid.itemDragEnter (details);
        const auto text = grid.getDropText();
        grid.itemDropped (details);
        CHECK (grid.getDropText().isEmpty());
        return text;
    };

    // The whole groove: the pattern shown is it (in 16ths: kicks 0 and 8, snares 4 and 12, hats every 2).
    CHECK (drop ("groove:" + juce::String (verse)).contains ("REPLACES PATTERN"));
    CHECK (rows (BD) == std::vector<double> { 0.0, 8.0 });
    CHECK (rows (SD) == std::vector<double> { 4.0, 12.0 });
    CHECK (rows (CH).size() == 8);
    t.p.flushUndoStep();
    const auto first = t.p.getModMatrix().drumPatterns[(size_t) pattern];

    // The chorus with KICK ticked: only its kicks laid onto the pattern (0 and 2), the snares and hats kept.
    page.getTable().selectRow (1 - verse);
    page.toggleDrumPart ((int) DrumPart::kick);
    CHECK (drop ("groove:" + juce::String (1 - verse)).contains ("KICK LAID ONTO"));
    CHECK (rows (BD) == std::vector<double> { 0.0, 2.0 });   // its kicks in place of the verse's (as LAYER ONTO)
    CHECK (rows (SD) == std::vector<double> { 4.0, 12.0 });
    t.p.flushUndoStep();
    // Undo: the verse as dropped.
    t.p.undo();
    CHECK (t.p.getModMatrix().drumPatterns[(size_t) pattern] == first);

    // A GROOVE PARTS row (HI-HAT) of the chosen groove (the verse): its hats laid on alone.
    page.toggleDrumPart (-1);   // ALL again
    page.getTable().selectRow (verse);
    t.p.grooveToPattern (*grooves::read (pack.getChildFile ("Rock/Chorus 01.mid"), t.p.grooveLayout (0)), 0, pattern);   // no hats
    CHECK (rows (CH).empty());
    CHECK (drop ("part:" + juce::String ((int) DrumPart::hats)).contains ("HI-HAT LAID ONTO"));
    CHECK (rows (BD) == std::vector<double> { 0.0, 2.0 });   // the chorus's kicks kept
    CHECK (rows (CH).size() == 8);   // its 8 hats over the one-bar pattern

    // A song creator groove: in place of the pattern.
    view.openSongCreator (pack.getChildFile ("Rock/Verse 01.mid"));
    CHECK (drop ("creator:" + juce::String ((int) grooves::Part::chorus) + ":0").contains ("REPLACES"));
    CHECK (rows (BD) == std::vector<double> { 0.0, 2.0 });
    CHECK (rows (CH).empty());

    // A MIDI file from the computer, through the kit's own map: the pattern.
    const auto file = t.folder.getChildFile ("Mine.mid");
    writeMidi (file, { { 1.0, t.p.homeNote (SD) }, { 3.0, t.p.homeNote (SD) } });
    REQUIRE (grid.isInterestedInFileDrag ({ file.getFullPathName() }));
    CHECK (! grid.isInterestedInFileDrag ({ "/x/kick.wav" }));
    grid.filesDropped ({ file.getFullPathName() }, 10, 10);
    CHECK (rows (SD) == std::vector<double> { 4.0, 12.0 });
    CHECK (rows (BD).empty());
    CHECK (! grid.isInterestedInDragSource ({ "something:else", nullptr, {} }));
}

TEST_CASE ("SEQUENCER DROP: a groove held over the DRUMS tab brings the sequencer in front; the drag over, the window settles there", "[seq-drop][ui]")
{
    Plugin t;
    MainView view (t.p);
    view.setSize (MainView::width, MainView::height);
    view.showPage (MainView::Page::grooves);
    const juce::DragAndDropTarget::SourceDetails over ("groove:0", nullptr, view.getDrumsTabBounds().getCentre());
    REQUIRE (view.isInterestedInDragSource (over));
    const auto now = juce::Time::getMillisecondCounterHiRes();
    view.itemDragMove (over);
    view.updateDragHover (now + 200.0, true);   // not yet
    CHECK (! view.isPeekingAtSequencer());
    view.updateDragHover (now + 600.0, true);   // half a second: in front, the GROOVES page still there under it
    CHECK (view.isPeekingAtSequencer());
    CHECK (view.getDrumPage().isVisible());
    CHECK (view.getGroovesPage().isVisible());
    CHECK (view.getDrumPage().getSequencer().isVisible());
    // Dropped (or let go anywhere): DRUMS, the GROOVES page hidden.
    view.updateDragHover (now + 700.0, false);
    CHECK (! view.isPeekingAtSequencer());
    CHECK (view.getPage() == MainView::Page::drums);
    CHECK (! view.getGroovesPage().isVisible());
    // Moved off the tab before the half second: nothing.
    view.showPage (MainView::Page::grooves);
    view.itemDragMove (over);
    view.itemDragMove ({ "groove:0", nullptr, { 5, 600 } });
    view.updateDragHover (now + 5000.0, true);
    CHECK (! view.isPeekingAtSequencer());
    CHECK (view.getPage() == MainView::Page::grooves);
}

//==============================================================================
// 0.43.0: A LONG SONG SCROLLS.
TEST_CASE ("SONG STRIP 0.43.0: a song longer than 32 bars scrolls; the wheel and the bar move along, Ctrl + wheel zooms, the view follows PLAY", "[song][ui]")
{
    Plugin t;
    song::Song s;
    s.place (block ("A", 20, { { 0.0, BD } }), 0.0);
    t.p.setSong (s);
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 136);
    const auto lane = track.getLane();
    // 20 bars: all of it shown (22 with the two after it), no scroll bar, as before.
    CHECK (track.totalBeats() == 88.0);
    CHECK (track.beatAt ((float) lane.getRight()) == 88.0);
    CHECK_FALSE (track.getScrollBar().isVisible());

    // 40 bars: 32 shown, the bar shows; a bar's x and beat agree after scrolling.
    s.place (block ("B", 20, { { 0.0, BD } }), 80.0);
    t.p.setSong (s);
    track.resized();
    CHECK (track.totalBeats() == 168.0);
    CHECK (track.beatAt ((float) lane.getRight()) == 128.0);
    CHECK (track.getScrollBar().isVisible());
    track.setViewStart (40.0);
    CHECK (track.getViewStart() == 40.0);
    CHECK (std::abs (track.beatAt ((float) lane.getX()) - 40.0) < 1.0e-6);
    CHECK (std::abs (track.xOf (100.0) - track.xOf (40.0) - (float) lane.getWidth() * 60.0f / 128.0f) < 1.0e-3f);
    CHECK (track.getScrollBar().getCurrentRangeStart() == 40.0);
    // No further than the end.
    track.setViewStart (1000.0);
    CHECK (track.getViewStart() == 40.0);
    // The wheel: back towards the start.
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 0.5f;
    const auto mouse = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), lane.getCentre().toFloat(), juce::ModifierKeys(),
                                         1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &track, &track, juce::Time::getCurrentTime(),
                                         lane.getCentre().toFloat(), juce::Time::getCurrentTime(), 1, false);
    track.mouseWheelMove (mouse, wheel);
    CHECK (track.getViewStart() < 40.0);
    // Zoomed in (as Ctrl + the wheel does): fewer bars shown.
    const auto before = track.getBarsShown();
    track.setBarsShown (8.0);
    CHECK (track.getBarsShown() == 8.0);
    CHECK (track.getBarsShown() < before);
    CHECK (std::abs ((track.beatAt ((float) lane.getRight()) - track.getViewStart()) - 32.0) < 1.0e-6);
    // Shorter again: the view kept inside the song.
    t.p.setSong (song::Song());
    CHECK (track.getViewStart() <= track.totalBeats());
}

//==============================================================================
// 0.44.0: THE LOOP AREA and THE CLICK.
TEST_CASE ("SONG LOOP 0.44.0: PLAY goes back to the loop's start on its very sample; off, it plays to the end and stops", "[song][song-player][loop]")
{
    Plugin t;
    t.host.playing = false;
    t.host.bpm = 110.0;   // a bar isn't a whole number of blocks: the wrap falls inside one
    song::Song s;
    s.place (block ("A", 1, { { 0.0, SD } }), 0.0);
    s.place (block ("B", 1, { { 0.0, BD }, { 2.0, BD } }), s.endBeat());
    s.place (block ("C", 1, { { 0.0, SD } }), s.endBeat());
    s.loopOn = true;
    s.loopStart = 4.0;
    s.loopEnd = 8.0;   // block B
    t.p.setSong (s);
    const auto beat = 60.0 / 110.0 * sampleRate;   // samples a beat
    t.p.playSong (4.0);
    const auto o = onsets (t.play (5.0));
    // B's kicks on beats 4 and 6, then again from the wrap (beat 8 -> 4), C's snare never reached.
    REQUIRE (o.size() >= 4);
    CHECK (o[0] == 0);
    CHECK (std::abs (o[1] - 2.0 * beat) <= 1.0);
    CHECK (std::abs (o[2] - 4.0 * beat) <= 1.0);
    CHECK (std::abs (o[3] - 6.0 * beat) <= 1.0);
    CHECK (t.p.isSongPlaying());   // past the song's... loop: still going
    CHECK (t.p.getSongPosition() >= 4.0);
    CHECK (t.p.getSongPosition() < 8.0);

    // Off: from B on to C's snare and the end, then stopped.
    s.loopOn = false;
    t.p.setSong (s);
    t.p.playSong (4.0);
    const auto plain = onsets (t.play (5.0));
    REQUIRE (plain.size() == 3);
    CHECK (std::abs (plain[2] - 4.0 * beat) <= 1.0);
    CHECK (! t.p.isSongPlaying());

    // A loop reaching past the song's end: PLAY carries on round it rather than stopping at the end.
    s.loopOn = true;
    s.loopStart = 8.0;
    s.loopEnd = 16.0;
    t.p.setSong (s);
    t.p.playSong (8.0);
    t.play (6.0);
    CHECK (t.p.isSongPlaying());

    // The loop's end exactly on a block's end (120 BPM: a bar is 375 blocks): it still goes round.
    t.host.bpm = 120.0;
    s.loopStart = 4.0;
    s.loopEnd = 8.0;
    t.p.setSong (s);
    t.p.playSong (4.0);
    const auto exact = onsets (t.play (4.5));
    REQUIRE (exact.size() == 5);
    CHECK (exact[2] == 96000);   // beat 8 -> 4: B's first kick again, on the very sample
    CHECK (exact[4] == 192000);
    s.loopStart = 8.0;
    s.loopEnd = 16.0;
    t.p.setSong (s);

    // Kept with the song (and undoable as an edit); a bounce plays the whole song, no loop.
    const auto back = song::Song::fromXml (*t.p.getSong().toXml());
    CHECK (back.loopOn);
    CHECK (back.loopStart == 8.0);
    CHECK (back.loopEnd == 16.0);
    CHECK (back == t.p.getSong());
    CHECK_FALSE (song::Song::fromXml (*song::Song().toXml()).loopOn);
}

TEST_CASE ("SONG CLICK 0.44.0: a click on every beat (higher on the bar's first) while recording, or always, or never", "[song][song-player][loop]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("Quiet", 2), 0.0);   // no hits: only the click sounds
    REQUIRE (s.click == song::Song::Click::recording);   // the default
    t.p.setSong (s);
    t.p.playSong (0.0);
    CHECK (onsets (t.play (1.5)).empty());   // not recording: no click

    // Recording: a click on each beat at 120 BPM (every 24000 samples), from PLAY's first sample.
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    const auto x = t.play (2.2);
    const auto o = onsets (x);
    REQUIRE (o.size() == 5);
    // (A click is a sine from 0: its beat's own sample is its silent first one, so it shows from the next.)
    for (size_t i = 0; i < o.size(); ++i)
        CHECK (o[i] - (int) i * 24000 == 1);
    // The bar's first beat higher (1600 Hz) than the others (1100 Hz): zero crossings in its first 10 ms.
    const auto crossings = [&x] (int from)
    {
        int n = 0;
        for (int i = from + 1; i < from + 480; ++i)
            n += (x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f);
        return n;
    };
    CHECK (std::abs (crossings (o[0]) - 32) <= 2);   // 1600 Hz x 10 ms x 2
    CHECK (std::abs (crossings (o[1]) - 22) <= 2);   // 1100 Hz
    CHECK (std::abs (crossings (o[4]) - 32) <= 2);   // bar 2's first
    t.p.setSongRecording (false);
    t.p.stopSong();

    // Always: without recording too; off: never.
    s.click = song::Song::Click::always;
    t.p.setSong (s);
    t.p.playSong (0.0);
    CHECK (onsets (t.play (1.2)).size() == 3);
    s.click = song::Song::Click::off;
    s.countIn = false;   // (0.45.0: a count-in clicks whatever CLICK is)
    t.p.setSong (s);
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    CHECK (onsets (t.play (1.2)).empty());
    t.p.setSongRecording (false);
    CHECK (song::Song::fromXml (*t.p.getSong().toXml()).click == song::Song::Click::off);
}

TEST_CASE ("SONG LOOP 0.44.0: a note played after the loop goes round is recorded at its place in the loop", "[song][song-player][loop]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    s.place (block ("B", 1, { { 0.0, BD } }), s.endBeat());
    s.loopOn = true;
    s.loopStart = 0.0;
    s.loopEnd = 4.0;
    s.click = song::Song::Click::off;
    s.countIn = false;   // (0.45.0: the take from PLAY's first sample)
    t.p.setSong (s);
    t.p.flushUndoStep();
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    // A snare 1.25 s after PLAY (beat 2.5 at 120 BPM)... then one at 2.25 s: past the wrap at 2 s, so beat 0.5.
    juce::AudioBuffer<float> buffer (2, blockSize);
    const auto note = [] (int at) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), at); return m; };
    for (int done = 0; done < 3 * (int) sampleRate; done += blockSize)
    {
        juce::MidiBuffer midi;
        for (const auto at : { 60000, 108000 })
            if (at >= done && at < done + blockSize)
                midi = note (at - done);
        buffer.clear();
        t.p.processBlock (buffer, midi);
    }
    t.p.setSongRecording (false);
    std::vector<double> snares;
    for (const auto& b : t.p.getSong().blocks)
        for (const auto& h : b.hits)
            if (h.slot == SD)
                snares.push_back (b.start + h.beat);
    std::sort (snares.begin(), snares.end());
    REQUIRE (snares.size() == 2);
    CHECK_THAT (snares[0], WithinAbs (0.5, 1.0e-3));
    CHECK_THAT (snares[1], WithinAbs (2.5, 1.0e-3));
}

//==============================================================================
// 0.45.0: A NOTE MOVED SIDEWAYS, THE COUNT-IN, THE GROOVES WINDOW.
TEST_CASE ("SONG 0.45.0: a note moved along its row wraps into the loop and replaces one already there", "[song][song-edit]")
{
    // The model: wrapped into the loop; a note already there is replaced.
    auto b = block ("B", 1, { { 0.0, BD }, { 3.0, BD } });
    CHECK (song::moveHit (b, BD, 3.0, 4.5));   // past the end: round to 0.5
    CHECK (notesOf (b) == std::vector<std::pair<double, int>> { { 0.0, BD }, { 0.5, BD } });
    CHECK (song::moveHit (b, BD, 0.5, 0.0));   // onto the other: one kick
    CHECK (b.hits.size() == 1);
    CHECK_FALSE (song::moveHit (b, SD, 0.0, 1.0));
}

TEST_CASE ("SONG 0.45.0 COUNT-IN: PLAY with REC armed clicks a bar first, then the song starts on the sample; nothing recorded meanwhile", "[song][song-player][loop]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("A", 2, { { 0.0, BD } }), 0.0);
    REQUIRE (s.countIn);   // on by default
    s.click = song::Song::Click::off;   // the count-in clicks whatever CLICK says
    t.p.setSong (s);
    t.p.flushUndoStep();
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    // A note during the count-in (sample 30000) and one half a beat into the song (96000 + 12000).
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> out;
    auto countingSeen = false;
    for (int done = 0; done < (int) (2.6 * sampleRate); done += blockSize)
    {
        juce::MidiBuffer midi;
        for (const auto at : { 30000, 108000 })
            if (at >= done && at < done + blockSize)
                midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), at - done);
        buffer.clear();
        t.p.processBlock (buffer, midi);
        countingSeen = countingSeen || t.p.isCountingIn();
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    CHECK (countingSeen);
    CHECK_FALSE (t.p.isCountingIn());
    const auto o = onsets (out);
    // The count-in's four clicks a beat apart (each a sine from 0: heard from its beat's next sample), the snare played
    // at 30000 (the kit plays it), then the song's kick on its first beat, a bar after PLAY, on the very sample.
    REQUIRE (o.size() >= 6);
    CHECK (o[0] == 1);
    CHECK (o[1] == 24001);
    CHECK (o[2] == 30000);
    CHECK (o[3] == 48001);
    CHECK (o[4] == 72001);
    CHECK (o[5] == 96000);
    t.p.setSongRecording (false);
    // Only the note played in the song recorded, at beat 0.5.
    std::vector<double> snares;
    for (const auto& b : t.p.getSong().blocks)
        for (const auto& h : b.hits)
            if (h.slot == SD)
                snares.push_back (b.start + h.beat);
    REQUIRE (snares.size() == 1);
    CHECK_THAT (snares[0], WithinAbs (0.5, 1.0e-3));

    // Off (or not recording): no wait.
    auto noWait = t.p.getSong();
    noWait.countIn = false;
    t.p.setSong (noWait);
    t.p.setSongRecording (true);
    t.p.playSong (0.0);
    const auto straight = onsets (t.play (0.3));
    REQUIRE_FALSE (straight.empty());
    CHECK (straight[0] == 0);
    t.p.setSongRecording (false);
    t.p.stopSong();
    t.p.playSong (0.0);   // not recording: no count-in either
    noWait.countIn = true;
    t.p.setSong (noWait);
    t.p.playSong (0.0);
    CHECK (onsets (t.play (0.3))[0] == 0);
    CHECK_FALSE (song::Song::fromXml (*noWait.toXml()).countIn == false);
    noWait.countIn = false;
    CHECK_FALSE (song::Song::fromXml (*noWait.toXml()).countIn);
}

TEST_CASE ("GROOVES 0.45.0: OWN WINDOW puts the page in a window of its own; BACK IN PLUGIN (or closing it) brings it back", "[song][ui]")
{
    Plugin t;
    MainView::groovesWindowOnDesktop = false;   // (this test machine's display has no window manager)
    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& page = view->getGroovesPage();
    view->showPage (MainView::Page::grooves);
    CHECK (page.getParentComponent() == view);
    CHECK (page.getWindowButton().getButtonText() == "OWN WINDOW");

    page.getWindowButton().onClick();   // (as a click: triggerClick would only post it)
    REQUIRE (view->areGroovesInOwnWindow());
    auto* window = view->getGroovesWindow();
    REQUIRE (window != nullptr);
    CHECK (page.getTopLevelComponent() == window);
    CHECK (page.isVisible());
    CHECK (page.getWidth() >= 1100);
    CHECK (page.getWindowButton().getButtonText() == "BACK IN PLUGIN");
    // The GROOVES tab says where it went; other pages leave the page alone (it stays up in its window).
    bool noteShown = false;
    for (auto* c : view->getChildren())
        if (auto* l = dynamic_cast<juce::Label*> (c); l != nullptr && l->isVisible() && l->getText().contains ("window of its own"))
            noteShown = true;
    CHECK (noteShown);
    view->showPage (MainView::Page::drums);
    CHECK (page.isVisible());

    // Back.
    view->setGroovesInOwnWindow (false);
    CHECK_FALSE (view->areGroovesInOwnWindow());
    CHECK (page.getParentComponent() == view);
    CHECK_FALSE (page.isVisible());   // on DRUMS now
    view->showPage (MainView::Page::grooves);
    CHECK (page.isVisible());
    CHECK (page.getBounds() == juce::Rectangle<int> (12, 68, 1476, 672));
    CHECK (page.getWindowButton().getButtonText() == "OWN WINDOW");
    // Out again, then the plugin's window closed with it out: no crash, the page let go of first.
    view->setGroovesInOwnWindow (true);
    editor.reset();
}
