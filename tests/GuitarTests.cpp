// THE GUITAR (PR7-I 0.2.0): fretting, the render list, importing a render, the library and the playing engine. The
// renders here are made up (tones at each take's pitch, where the list puts them, a little late as a DAW would), so
// the import's cutting and checking are measured exactly.
#include "TestUtils.h"

#include "guitar/Guitar.h"
#include "guitar/PolyWindow.h"
#include "guitar/RenderImport.h"
#include "guitar/RoundRobinCheck.h"
#include "guitar/NoiseLists.h"
#include "guitar/NoiseImport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <set>

using namespace guitar;

namespace
{
    std::array<bool, numStrings> none() { return {}; }

    // A made-up render of a list: each take a decaying tone at its pitch, its level by dynamic, starting `late` seconds
    // after where the list puts it, with a little pick noise just before.
    // Each round-robin its own recording (its own overtone: the (2 + round-robin)th harmonic); `copiesFrom` > 0: round-robins
    // from that one on copy the earlier ones (as an instrument with fewer round-robins than the list asks for gives).
    std::vector<float> fakeRender (Articulation a, int string, double rate, double late, int pitchShift = 0, double lengthScale = 1.0,
                                   int copiesFrom = 0)
    {
        std::vector<float> audio ((size_t) (renders::lengthSeconds (a, string) * rate * lengthScale), 0.0f);
        juce::Random random (7);
        for (const auto& take : renders::takes (a, string))
        {
            const auto hz = guitar::frequencyOf (take.note() + pitchShift);
            const auto start = (int) ((take.noteSeconds + late) * rate);
            const auto level = 0.2f + 0.3f * (float) take.layer;
            const auto rr = copiesFrom > 0 ? take.roundRobin % copiesFrom : take.roundRobin;
            const auto overtone = hz * (2.0 + rr);
            for (int i = -(int) (0.01 * rate); i < 0; ++i)
                if (start + i >= 0 && start + i < (int) audio.size())
                    audio[(size_t) (start + i)] += 0.004f * (random.nextFloat() - 0.5f);
            for (int i = 0; i < (int) (renders::spec (a).holdSeconds * rate); ++i)
                if (start + i < (int) audio.size())
                    audio[(size_t) (start + i)] += level * std::exp (-0.6f * (float) i / (float) rate)
                                                 * (float) (std::sin (juce::MathConstants<double>::twoPi * hz * i / rate)
                                                            + (overtone < 0.45 * rate ? 0.3 * std::sin (juce::MathConstants<double>::twoPi * overtone * i / rate) : 0.0));
        }
        return audio;
    }

    juce::File tempGuitar (const juce::String& name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PR7-I tests").getChildFile (name);
        f.deleteRecursively();
        f.createDirectory();
        return f;
    }

    // 0.6.0: a made-up render of a noise list, as the owner's will be (the notes' Volume down: only noises), `late`
    // seconds late. Releases: a decaying tone at the note's pitch from each note-off, `recordings` different ones in
    // turn (the rest copies); `everyNth`: only every nth hit has a noise (the unpitched list's now and then), each a
    // different burst of noise, `recordings` of them in turn; `notesHeard`: the notes too (a render made the wrong way).
    std::vector<float> fakeNoiseRender (noises::List list, int string, double rate, double late, int recordings, int everyNth = 1,
                                        bool notesHeard = false)
    {
        using namespace noises;
        std::vector<float> audio ((size_t) (lengthSeconds (list, string) * rate), 0.0f);
        auto n = 0;
        for (const auto& h : hits (list, string))
        {
            const auto which = (n / everyNth) % recordings;
            const auto heard = n % everyNth == 0;
            ++n;
            if (notesHeard)
                for (int i = 0; i < (int) (holdSeconds * rate); ++i)
                    if (const auto at = (size_t) ((h.noteSeconds + late) * rate) + (size_t) i; at < audio.size())
                        audio[at] += 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * guitar::frequencyOf (h.note) * i / rate);
            if (! heard)
                continue;
            const auto start = (list == List::fretNoise ? h.noteSeconds : h.offSeconds) + late;
            juce::Random random (100 + which);
            const auto hz = list == List::fretNoise || list == List::unpitched ? 0.0 : guitar::frequencyOf (h.note) * (1.0 + 0.002 * which);
            for (int i = 0; i < (int) (0.3 * rate); ++i)
            {
                const auto envelope = std::exp (-(double) i / (0.05 * rate));
                const auto tone = hz > 0.0 ? std::sin (juce::MathConstants<double>::twoPi * hz * i / rate + which) : random.nextDouble() * 2.0 - 1.0;
                if (const auto at = (size_t) (start * rate) + (size_t) i; at < audio.size())
                    audio[at] += (float) (0.03 * envelope * tone);
            }
        }
        return audio;
    }

    // A guitar of tones in memory: every string and fret, every dynamic and stroke, each take a tone at its pitch, its
    // level the dynamic's (0.25, 0.5, 1) times the stroke's (down 1, up 0.5), `seconds` long, all of it in memory.
    std::shared_ptr<Library> toneGuitar (double rate = 8000.0, double seconds = 3.0, bool withMutes = false)
    {
        auto library = std::make_shared<Library>();
        // PALM MUTES (the low three strings): 0.3 s each (gone well before a sustain), level 0.1 x (degree + 1) times
        // the stroke's (down 1, up 0.5).
        if (withMutes)
            for (int s = 0; s < 3; ++s)
                for (int f = 0; f <= numFrets; ++f)
                    for (int l = 0; l < renders::spec (Articulation::mute).layers; ++l)
                        for (const auto stroke : { Stroke::down, Stroke::up })
                            for (int rr = 0; rr < renders::spec (Articulation::mute).roundRobins; ++rr)
                            {
                                auto take = std::make_shared<Take>();
                                take->sampleRate = rate;
                                const auto level = 0.1f * (float) (l + 1) * (stroke == Stroke::down ? 1.0f : 0.5f);
                                const auto hz = guitar::frequencyOf (noteOf (s, f));
                                for (int i = 0; i < (int) (0.3 * rate); ++i)
                                    take->head.push_back (level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate + rr));
                                take->frames = (juce::int64) take->head.size();
                                library->add (Articulation::mute, s, f, l, stroke, rr, take);
                            }
        for (int s = 0; s < numStrings; ++s)
            for (int f = 0; f <= numFrets; ++f)
                for (int l = 0; l < renders::spec (Articulation::sustain).layers; ++l)
                    for (const auto stroke : { Stroke::down, Stroke::up })
                        for (int rr = 0; rr < renders::spec (Articulation::sustain).roundRobins; ++rr)
                    {
                        auto take = std::make_shared<Take>();
                        take->sampleRate = rate;
                        const auto level = (l == 0 ? 0.25f : l == 1 ? 0.5f : 1.0f) * (stroke == Stroke::down ? 1.0f : 0.5f);
                        const auto hz = guitar::frequencyOf (noteOf (s, f));
                        for (int i = 0; i < (int) (seconds * rate); ++i)
                            take->head.push_back (level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
                        take->frames = (juce::int64) take->head.size();
                        library->add (Articulation::sustain, s, f, l, stroke, rr, take);
                    }
        return library;
    }

    struct Player
    {
        Guitar guitar;
        Settings settings;
        std::shared_ptr<Library> library = toneGuitar();
        std::vector<float> left, right;
        explicit Player (bool withMutes = false)
        {
            if (withMutes)
                library = toneGuitar (8000.0, 3.0, true);
            guitar.prepare (sampleRate);
            settings.library = library.get();
            settings.velocityToVolume = 0.0f;
            settings.keyMap = KeyMap::factory;   // velocity as it comes (the split map has tests of its own)
        }
        // Plays `seconds`, `events` (message, at sample) in it, appending to left / right.
        void play (double seconds, std::vector<std::pair<juce::MidiMessage, int>> events = {})
        {
            const auto total = (int) (seconds * sampleRate);
            for (int done = 0; done < total; done += blockSize)
            {
                const auto n = std::min (blockSize, total - done);
                juce::MidiBuffer midi;
                for (const auto& [m, at] : events)
                    if (at >= done && at < done + n)
                        midi.addEvent (m, at - done);
                std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
                guitar.process (l.data(), r.data(), n, midi, 0, settings);
                left.insert (left.end(), l.begin(), l.end());
                right.insert (right.end(), r.begin(), r.end());
            }
        }
        static juce::MidiMessage on (int note, int velocity = 90) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity); }
        static juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }
    };
}

TEST_CASE ("FRETTING: open strings low on the neck, the hand's reach, busy strings, force string, chords", "[guitar][fretting]")
{
    const FretSettings natural;
    CHECK (choose (33, none(), 1, natural) == Position { 0, 0 });    // the low A, open
    CHECK (choose (45, none(), 1, natural) == Position { 2, 0 });    // A2: the open A string, not the 12th fret
    CHECK (choose (38, none(), 1, natural) == Position { 0, 5 });    // D2: only the low string has it
    CHECK (choose (52, none(), 1, natural) == Position { 3, 2 });    // E3 low on the neck: D string, 2nd fret
    CHECK (choose (52, none(), 7, natural) == Position { 2, 7 });    // ... with the hand at the 7th: A string, 7th fret
    CHECK_FALSE (choose (20, none(), 1, natural).valid());           // below the guitar

    auto busy = none();
    busy[2] = true;
    CHECK (choose (45, busy, 1, natural) == Position { 1, 5 });      // the A string is sounding: E string, 5th fret

    FretSettings forced;
    forced.forceString = 0;
    CHECK (choose (45, none(), 1, forced) == Position { 0, 12 });    // forced onto the low A
    forced.forceString = 6;
    CHECK (choose (45, none(), 1, forced) == Position { 2, 0 });     // the high E can't play it: as if not forced

    // Drop A's one-finger power chord at the 5th fret (D5), and the open A5: each note its own string.
    CHECK (chooseChord ({ 38, 45, 50 }, none(), 5, natural) == std::vector<Position> { { 0, 5 }, { 1, 5 }, { 2, 5 } });
    CHECK (chooseChord ({ 45, 33, 40 }, none(), 1, natural) == std::vector<Position> { { 2, 0 }, { 0, 0 }, { 1, 0 } });
    // ... and D5 struck with the hand low on the neck: the lowest note takes the hand to the 5th fret, so a barre there
    // (not the open A and D strings); with the hand held at the 1st fret (not AUTO), the open strings.
    CHECK (chooseChord ({ 38, 45, 50 }, none(), 1, natural) == std::vector<Position> { { 0, 5 }, { 1, 5 }, { 2, 5 } });
    CHECK (chooseChord ({ 38, 45, 50 }, none(), 1, natural, false) == std::vector<Position> { { 0, 5 }, { 2, 0 }, { 3, 0 } });

    // AUTO hand: moves just enough to reach a fret; open strings don't move it.
    CHECK (followHand (1, 7, FretMode::natural) == 4);
    CHECK (followHand (4, 2, FretMode::natural) == 2);
    CHECK (followHand (4, 0, FretMode::natural) == 4);
    CHECK (followHand (1, 7, FretMode::polyphonic) == 3);
}

TEST_CASE ("THE RENDER LIST: every fret, three dynamics, two round-robins of down and up; keyswitches first; one file per string", "[guitar][renders]")
{
    const auto list = renders::takes (Articulation::sustain, 0);
    REQUIRE (list.size() == 25 * 3 * 2 * 2);
    REQUIRE ((int) list.size() == Library::takesPerString (Articulation::sustain));
    CHECK_THAT (list.front().noteSeconds, WithinAbs (2.25, 1e-9));
    CHECK_THAT (list[1].noteSeconds - list[0].noteSeconds, WithinAbs (renders::spec (Articulation::sustain).slotSeconds, 1e-9));
    CHECK (list[0].stroke == Stroke::down);
    CHECK (list[1].stroke == Stroke::up);
    CHECK (list[2].stroke == Stroke::down);   // down, up, down, up: the two round-robins of each stroke
    CHECK (list[2].roundRobin == 1);
    CHECK (list[3].layer == 0);
    CHECK (list[4].layer == 1);
    CHECK (list.back().fret == 24);
    CHECK (list.back().note() == 33 + 24);
    for (const auto& t : list)
        CHECK (t.velocity() <= 119);   // sustain's velocities (above: the reference's rake and pinch harmonic)
    CHECK_THAT (renders::lengthSeconds (Articulation::sustain, 0), WithinAbs (2.0 + 300 * 5.5, 1e-9));
    CHECK (renders::fileName (Articulation::sustain, 0) == "PR7-I Sustain - String 7 (low A)");
    CHECK (renders::fileName (Articulation::sustain, 3) == "PR7-I Sustain - String 4 (D)");

    // The MIDI: the string forced (C-2 + string), legato off, then per take the articulation and stroke keys, the note.
    const auto midi = renders::toMidi (Articulation::sustain, 4);
    juce::MidiMessageSequence seq (*midi.getTrack (0));
    seq.updateMatchedPairs();
    const auto ticksPerSecond = 960.0 * renders::bpm / 60.0;
    auto notes = 0;
    auto forced = false, legatoOff = false;
    double firstNote = -1.0, firstStrokeKey = -1.0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (! m.isNoteOn())
            continue;
        const auto t = m.getTimeStamp() / ticksPerSecond;
        if (m.getNoteNumber() == renders::keys::forceStringLowA + 4) forced = true;
        if (m.getNoteNumber() == renders::keys::legatoOff) legatoOff = true;
        if (m.getNoteNumber() == renders::keys::pickDown && firstStrokeKey < 0.0) firstStrokeKey = t;
        if (m.getNoteNumber() >= lowestNote && m.getNoteNumber() <= highestNote)
        {
            if (firstNote < 0.0)
            {
                firstNote = t;
                CHECK (m.getNoteNumber() == 55);    // the G string open
                CHECK (m.getVelocity() == 45);
            }
            ++notes;
        }
    }
    CHECK (forced);
    CHECK (legatoOff);
    CHECK (notes == 300);
    CHECK_THAT (firstNote, WithinAbs (2.25, 0.001));
    CHECK (firstStrokeKey < firstNote);

    // A take's file name, and back.
    const auto name = renders::takeFileName (Articulation::sustain, list[7]);
    CHECK (name == "Sustain/s0_f00_l1_u_r1.wav");
    const auto id = renders::parseTakeFile (juce::File ("/x").getChildFile (name));
    REQUIRE (id);
    CHECK (id->fret == 0);
    CHECK (id->layer == 1);
    CHECK (id->stroke == Stroke::up);
    CHECK (id->roundRobin == 1);
    CHECK_FALSE (renders::parseTakeFile (juce::File ("/x/Other/s0_f01_l0_u_r0.wav")));
    CHECK_FALSE (renders::parseTakeFile (juce::File ("/x/Sustain/s0_f01_l0_u.wav")));
}

TEST_CASE ("PITCH: measured within a few cents, low A to the 24th fret", "[guitar][renders]")
{
    for (const auto rate : { 8000.0, 48000.0 })
        for (const auto note : { 33, 45, 64, 88 })
        {
            const auto hz = guitar::frequencyOf (note);
            std::vector<float> x ((size_t) (0.3 * rate));
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / rate)
                                + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * hz * (double) i / rate));
            const auto found = renders::pitchOf (x.data(), (int) x.size(), rate, hz * std::pow (2.0, 0.3 / 12.0));
            INFO (note << " at " << rate);
            CHECK (std::abs (1200.0 * std::log2 (found / hz)) < 5.0);
        }
}

TEST_CASE ("IMPORT A RENDER: the offset found, every take cut at its pick, in tune; the wrong file refused", "[guitar][renders]")
{
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("Import");
    const auto audio = fakeRender (Articulation::sustain, 0, rate, 0.023);
    std::atomic<float> progress { 0.0f };
    const auto report = renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 0, folder, &progress);
    INFO (report.summary());
    REQUIRE (report.ok());
    CHECK (report.written == 300);
    CHECK (report.copies == 0);
    CHECK (report.outOfTune.isEmpty());
    CHECK_THAT (report.offsetSeconds, WithinAbs (0.023, 0.001));
    CHECK (progress.load() == 1.0f);
    CHECK (report.summary() == "300 notes imported, all in tune");

    // A take's file: mono, the render's rate, its tone starting `preroll` in (the pick's noise kept before it).
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (folder.getChildFile ("Sustain/s0_f12_l2_d_r0.wav")));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 1);
    CHECK (reader->sampleRate == rate);
    juce::AudioBuffer<float> take (1, (int) reader->lengthInSamples);
    reader->read (&take, 0, take.getNumSamples(), 0, true, false);
    auto onset = -1;
    for (int i = 0; i < take.getNumSamples() && onset < 0; ++i)
        if (std::abs (take.getSample (0, i)) > 0.05f)
            onset = i;
    CHECK (std::abs (onset - (int) (renders::prerollSeconds * rate)) <= 3);
    CHECK_THAT ((double) take.getNumSamples() / rate, WithinAbs (renders::prerollSeconds + renders::spec (Articulation::sustain).holdSeconds + renders::tailSeconds, 0.001));

    // Another string's render dropped on this string: refused, nothing written.
    const auto wrongFolder = tempGuitar ("Wrong");
    const auto wrong = fakeRender (Articulation::sustain, 0, rate, 0.01, 5);
    const auto refused = renders::importAudio (wrong.data(), (int) wrong.size(), rate, Articulation::sustain, 0, wrongFolder);
    CHECK_FALSE (refused.ok());
    CHECK (refused.error.contains ("aren't the pitch"));
    CHECK_FALSE (wrongFolder.getChildFile ("Sustain").exists());

    // Not rendered to the end: refused.
    const auto shortRender = fakeRender (Articulation::sustain, 0, rate, 0.01, 0, 0.5);
    CHECK (renders::importAudio (shortRender.data(), (int) shortRender.size(), rate, Articulation::sustain, 0, wrongFolder).error.contains ("shorter"));

    // Silence: refused.
    std::vector<float> silence (audio.size(), 0.0f);
    CHECK (renders::importAudio (silence.data(), (int) silence.size(), rate, Articulation::sustain, 0, wrongFolder).error.contains ("No notes"));

    // THE LIBRARY: the folder loaded; the string's 150 takes; another string's notes played by stand-ins.
    const auto library = Library::load (folder);
    CHECK (library->count (Articulation::sustain, 0) == 300);
    CHECK (library->total() == 300);
    const auto exact = library->find (Articulation::sustain, 0, 5, 1, Stroke::up);
    REQUIRE (exact.take != nullptr);
    CHECK (exact.exact);
    CHECK (exact.take->file.getFileName() == "s0_f05_l1_u_r0.wav");
    CHECK (exact.take->stream != nullptr);   // longer than its head: the rest streams
    CHECK ((int) exact.take->head.size() == (int) std::ceil (Library::headSeconds * rate));
    const auto standIn = library->find (Articulation::sustain, 3, 2, 1, Stroke::down);   // E3 on the D string: the low A's 19th fret
    REQUIRE (standIn.take != nullptr);
    CHECK_FALSE (standIn.exact);
    CHECK (standIn.take->file.getFileName() == "s0_f19_l1_d_r0.wav");
    CHECK (Library::load (folder.getChildFile ("nothing"))->isEmpty());
    folder.getParentDirectory().deleteRecursively();
}

TEST_CASE ("THE ENGINE: notes on their strings at their pitch; one note per string; note-off damps; other strings ring", "[guitar][engine]")
{
    Player p;
    p.play (0.5, { { Player::on (45), 0 } });   // A2, the open A string
    CHECK (p.guitar.lastPosition() == Position { 2, 0 });
    CHECK_THAT (frequencyOf (p.left, 2000, 20000), WithinAbs (110.0, 0.2));
    CHECK (p.guitar.getSounding (2).fret == 0);

    // The low A, then D2 on the same string: the A stops (a guitarist re-picks the string); A2 on its string rings on.
    p.left.clear();
    p.play (0.6, { { Player::on (33), 0 }, { Player::on (38), (int) (0.2 * sampleRate) } });
    CHECK (p.guitar.lastPosition() == Position { 0, 5 });
    const auto from = (size_t) (0.25 * sampleRate);
    CHECK (p.guitar.getActiveVoices() == 2);                       // the low A's voice gone: A2 and D2 left
    CHECK (levelAt (p.left, 55.0, from, 9600) < 0.05);             // (what's left is the other two tones' spill)
    CHECK (levelAt (p.left, guitar::frequencyOf (38), from, 9600) > 0.2);
    CHECK (levelAt (p.left, 110.0, from, 9600) > 0.2);
    CHECK (p.guitar.getSounding (0).fret == 5);

    // NOTE-OFF: the string damped over RELEASE (80 ms), the others left ringing.
    p.left.clear();
    p.play (0.3, { { Player::off (38), 0 } });
    CHECK (rms (p.left, 6000, 14000) > 0.1);                     // the A string still
    CHECK (levelAt (p.left, guitar::frequencyOf (38), 6000, 8000) < 0.005);
    CHECK (p.guitar.getSounding (0).fret == -1);
    p.left.clear();
    p.play (0.2, { { Player::off (45), 0 } });
    CHECK (rms (p.left, 6000, 9600) < 1.0e-4);
    CHECK (p.guitar.getActiveVoices() == 0);
}

TEST_CASE ("THE ENGINE: alternate picking; DOWN / UP; velocity picks the dynamic; VEL > VOL", "[guitar][engine]")
{
    Player p;
    // ALTERNATE: down then up (the up takes are half as loud here), at the hard dynamic.
    p.play (0.2, { { Player::on (45, 120), 0 } });
    CHECK (p.guitar.lastStroke() == Stroke::down);
    const auto down = levelAt (p.left, 110.0, 2400, 4800);
    p.left.clear();
    p.play (0.2, { { Player::off (45), 0 }, { Player::on (45, 120), 1 } });
    CHECK (p.guitar.lastStroke() == Stroke::up);
    CHECK_THAT (levelAt (p.left, 110.0, 4800, 4800) / down, WithinAbs (0.5, 0.02));
    CHECK_THAT (down, WithinAbs (1.0, 0.02));

    // ALTERNATE's takes IN TURN (0.2.0's order, back in 0.2.2): down 1, up 1, down 2, up 2, again: the same recording no
    // sooner than every fourth note (0.2.1's random choice often played one every other note: a machine gun).
    {
        Player q;
        std::vector<const Take*> order;
        for (int i = 0; i < 16; ++i)
        {
            q.play (0.01, { { Player::off (45), 0 }, { Player::on (45, 120), 1 } });
            order.push_back (q.guitar.lastTake());
        }
        CHECK (std::set<const Take*> (order.begin(), order.begin() + 4).size() == 4);
        for (size_t i = 4; i < order.size(); ++i)
            CHECK (order[i] == order[i - 4]);
    }
    // DOWN: every stroke a down stroke, its two takes in turn; the soft and medium dynamics by velocity.
    p.settings.pick = PickMode::down;
    std::vector<const Take*> downs;
    for (int i = 0; i < 8; ++i)
    {
        p.play (0.01, { { Player::off (45), 0 }, { Player::on (45, 120), 1 } });
        downs.push_back (p.guitar.lastTake());
    }
    for (size_t i = 1; i < downs.size(); ++i)
        CHECK (downs[i] != downs[i - 1]);
    for (size_t i = 2; i < downs.size(); ++i)
        CHECK (downs[i] == downs[i - 2]);
    p.left.clear();
    p.play (0.2, { { Player::off (45), 0 }, { Player::on (45, 40), 1 } });
    CHECK (p.guitar.lastStroke() == Stroke::down);
    CHECK_THAT (levelAt (p.left, 110.0, 4800, 4800), WithinAbs (0.25, 0.01));
    p.left.clear();
    p.play (0.2, { { Player::off (45), 0 }, { Player::on (45, 90), 1 } });
    CHECK_THAT (levelAt (p.left, 110.0, 4800, 4800), WithinAbs (0.5, 0.01));

    // VEL > VOL: within a dynamic, softer than its velocity plays quieter (velocity 70 in the medium dynamic, made at 85).
    p.settings.velocityToVolume = 1.0f;
    p.left.clear();
    p.play (0.2, { { Player::off (45), 0 }, { Player::on (45, 70), 1 } });
    CHECK_THAT (levelAt (p.left, 110.0, 4800, 4800), WithinAbs (0.5 * std::pow (70.0 / 85.0, 1.5), 0.01));
    CHECK (layerFor (64) == 0);
    CHECK (layerFor (65) == 1);
    CHECK (layerFor (101) == 1);
    CHECK (layerFor (102) == 2);
}

TEST_CASE ("THE ENGINE: the reference's keyswitches (force string, hand position, picking); chords; the hand", "[guitar][engine]")
{
    Player p;
    // Force string C-2 (the low A): A2 at its 12th fret. G-2 turns it off.
    p.play (0.1, { { Player::on (0, 100), 0 }, { Player::on (45), 10 } });
    CHECK (p.guitar.lastPosition() == Position { 0, 12 });
    p.play (1.2, { { Player::off (45), 0 }, { Player::on (7, 100), 5 } });   // (a second's rest: the AUTO hand goes home)
    p.play (0.1, { { Player::on (45), 0 } });
    CHECK (p.guitar.lastPosition() == Position { 2, 0 });

    // Hand position A#-2 at velocity 7: E3 at the A string's 7th fret, not the D string's 2nd; B-2: AUTO again.
    p.play (0.1, { { Player::off (45), 0 }, { Player::on (10, 7), 5 }, { Player::on (52), 10 } });
    CHECK (p.guitar.lastPosition() == Position { 2, 7 });
    CHECK (p.guitar.getHand() == 7);
    p.play (1.2, { { Player::off (52), 0 }, { Player::on (11, 100), 5 } });   // and a second's rest: the hand goes home
    p.play (0.1, { { Player::on (52), 0 } });
    CHECK (p.guitar.lastPosition() == Position { 3, 2 });

    // Picking keyswitch C#7 (down): every stroke down; changing the page's setting wins again.
    p.play (0.1, { { Player::off (52), 0 }, { Player::on (109, 100), 2 }, { Player::on (45), 5 } });
    p.play (0.1, { { Player::off (45), 0 }, { Player::on (45), 5 } });
    CHECK (p.guitar.lastStroke() == Stroke::down);
    p.settings.pick = PickMode::up;
    p.play (0.1, { { Player::off (45), 0 }, { Player::on (45), 5 } });
    CHECK (p.guitar.lastStroke() == Stroke::up);

    // A chord struck together: each note its own string, all sounding.
    p.left.clear();
    p.play (0.3, { { Player::off (45), 0 }, { Player::on (33), 100 }, { Player::on (40), 100 }, { Player::on (45), 100 } });
    for (int s = 0; s < 3; ++s)
        CHECK (p.guitar.getSounding (s).fret == 0);
    CHECK (levelAt (p.left, 55.0, 4800, 9600) > 0.1);
    CHECK (levelAt (p.left, guitar::frequencyOf (40), 4800, 9600) > 0.1);
    CHECK (levelAt (p.left, 110.0, 4800, 9600) > 0.1);

    // TRANSPOSE: the notes played moved, keyswitches not.
    p.settings.transpose = 12;
    p.play (0.1, { { Player::on (33), 0 } });
    CHECK (noteOf (p.guitar.lastPosition().string, p.guitar.lastPosition().fret) == 45);
}

TEST_CASE ("THE ENGINE: a take read past its head from disk, as a whole note", "[guitar][engine]")
{
    // An imported take streams past its head: the note plays whole, at its pitch, from the file.
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("Stream");
    const auto audio = fakeRender (Articulation::sustain, 2, rate, 0.0);
    REQUIRE (renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 2, folder).ok());
    const auto library = Library::load (folder);
    Streamer streamer;
    Player p;
    p.settings.library = library.get();
    p.settings.streamer = &streamer;
    p.settings.waitForDisk = true;
    p.play (2.0, { { Player::on (45, 120), 0 } });   // the A string open (its renders are 8 kHz: played at 48 kHz)
    const auto atPitch = levelAt (p.left, 110.0, (size_t) (1.0 * sampleRate), (size_t) (0.9 * sampleRate));
    CHECK (atPitch > 0.2);                                                                                   // the A2, a second in
    CHECK (levelAt (p.left, 116.5, (size_t) (1.0 * sampleRate), (size_t) (0.9 * sampleRate)) < 0.1 * atPitch);   // not a semitone off
    CHECK (rms (p.left, (size_t) (1.5 * sampleRate), (size_t) (1.9 * sampleRate)) > 0.1);   // well past the 0.25 s head
    CHECK (streamer.getUnderruns() == 0);
    folder.getParentDirectory().deleteRecursively();
}

// PARAGON_RENDER_LISTS=<folder> ./GuitarCoreTests "[render-lists]": the seven strings' render lists written as MIDI files,
// and the round-robin check's.
TEST_CASE ("Write the render lists", "[.][render-lists]")
{
    const auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("PARAGON_RENDER_LISTS", "render-lists"));
    folder.createDirectory();
    for (const auto check : { renders::rrcheck::Check::roundRobin, renders::rrcheck::Check::palmMute, renders::rrcheck::Check::order })
    {
        const auto file = folder.getChildFile (renders::rrcheck::fileNameFor (check) + ".mid");
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (renders::rrcheck::toMidi (check).writeTo (out));
    }
    for (const auto a : { Articulation::sustain, Articulation::mute })
        for (int s = 0; s < numStrings; ++s)
        {
            const auto file = folder.getChildFile (renders::fileName (a, s) + ".mid");
            file.deleteFile();
            juce::FileOutputStream out (file);
            REQUIRE (out.openedOk());
            REQUIRE (renders::toMidi (a, s).writeTo (out));
        }
}

TEST_CASE ("THE NOISE LISTS 0.6.0: their layouts (fixed once sent), notes, keyswitches and times", "[guitar][noises]")
{
    using namespace noises;
    // How many hits each list has, and how long it is: the layouts sent to the owner (November 2026).
    CHECK (hits (List::pitchedRelease, 0).size() == 25 * 4);
    CHECK (hits (List::powerChordRelease).size() == 3 * 16 * 3);
    CHECK (hits (List::unpitched).size() == 7 * 4 * 12);
    CHECK (hits (List::fretNoise).size() == 32);
    CHECK_THAT (lengthSeconds (List::pitchedRelease, 0), Catch::Matchers::WithinAbs (2.0 + 100 * 1.25 + 1.0, 1e-9));
    CHECK_THAT (lengthSeconds (List::unpitched), Catch::Matchers::WithinAbs (2.0 + 336 * 1.25 + 1.0, 1e-9));

    // Each hit: its slot, the note held 0.5 s, and its release (or fret noise) clear of the next hit's keyswitches.
    for (const auto list : { List::pitchedRelease, List::powerChordRelease, List::unpitched, List::fretNoise })
    {
        const auto all = hits (list, 3);
        for (size_t i = 0; i < all.size(); ++i)
        {
            INFO ((int) list << " " << i);
            CHECK_THAT (all[i].noteSeconds, Catch::Matchers::WithinAbs (2.0 + (double) i * layout (list).slotSeconds + 0.25, 1e-9));
            if (i + 1 < all.size())
                CHECK (all[i + 1].noteSeconds - 0.25 - all[i].offSeconds >= 0.45);   // the ~0.3 s noise has room
        }
    }
    const auto pitched = hits (List::pitchedRelease, 3);
    CHECK (pitched.front().note == noteOf (3, 0));
    CHECK (pitched.back().note == noteOf (3, numFrets));
    CHECK (pitched[4].fret == 1);
    CHECK (hits (List::powerChordRelease).back().string == 2);
    CHECK (hits (List::powerChordRelease).back().fret == 15);
    CHECK (hits (List::fretNoise).front().note == 30);

    // The MIDI: every hit's note at its time and velocity; before it its force-string, articulation and down-stroke
    // keys (not for fret noise); legato off at the start.
    for (const auto list : { List::pitchedRelease, List::powerChordRelease, List::unpitched, List::fretNoise })
    {
        auto file = toMidi (list, 5);
        file.convertTimestampTicksToSeconds();
        const auto& track = *file.getTrack (0);
        std::vector<std::pair<double, int>> ons;
        for (int i = 0; i < track.getNumEvents(); ++i)
            if (const auto& m = track.getEventPointer (i)->message; m.isNoteOn())
                ons.push_back ({ m.getTimeStamp(), m.getNoteNumber() * 1000 + m.getVelocity() });
        const auto expect = hits (list, 5);
        auto found = 0;
        for (const auto& h : expect)
        {
            const auto v = list == List::fretNoise ? 127 : 85;
            for (const auto& [t, nv] : ons)
                if (std::abs (t - h.noteSeconds) < 1e-6 && nv == h.note * 1000 + v)
                    ++found;
            if (list != List::fretNoise)
            {
                const auto articulation = list == List::powerChordRelease ? 15 : 12;
                for (const auto k : { renders::keys::forceStringLowA + h.string, articulation })
                    CHECK (std::count_if (ons.begin(), ons.end(), [&] (const auto& e) { return std::abs (e.first - (h.noteSeconds - 0.25)) < 1e-6 && e.second / 1000 == k; }) == 1);
            }
        }
        CHECK (found == (int) expect.size());
        CHECK (std::count_if (ons.begin(), ons.end(), [] (const auto& e) { return e.second / 1000 == renders::keys::legatoOff; }) == 1);
    }
    CHECK (fileName (List::pitchedRelease, 0) == "PR7-I Release - String 7 (low A)");
    CHECK (fileName (List::fretNoise) == "PR7-I Fret Noise");
}

TEST_CASE ("THE NOISE IMPORT 0.6.0: releases cut at their note-offs, copies left out, renders made the wrong way refused", "[guitar][noises]")
{
    using namespace noises;
    constexpr double rate = 16000.0;

    // A string's releases, 12 ms late, two recordings taking turns: each fret's two kept, its two copies left out.
    {
        const auto folder = tempGuitar ("NoiseRelease");
        const auto audio = fakeNoiseRender (List::pitchedRelease, 3, rate, 0.012, 2);
        const auto report = importAudio (audio.data(), (int) audio.size(), rate, List::pitchedRelease, 3, folder);
        INFO (report.error);
        REQUIRE (report.ok());
        CHECK_THAT (report.offsetSeconds, Catch::Matchers::WithinAbs (0.012, 0.001));
        CHECK (report.written == 25 * 2);
        CHECK (report.copies == 25 * 2);
        CHECK (report.silent == 0);
        CHECK (folder.getChildFile ("Release/s3_f00_r0.wav").existsAsFile());
        CHECK (folder.getChildFile ("Release/s3_f24_r1.wav").existsAsFile());
        CHECK_FALSE (folder.getChildFile ("Release/s3_f00_r2.wav").existsAsFile());
        const auto library = Library::load (folder);
        CHECK (library->noiseCount (List::pitchedRelease, 3) == 50);
        CHECK (library->noiseCount (List::pitchedRelease, 2) == 0);
        CHECK (library->isEmpty());   // (no notes: the noises don't count as the guitar's takes)
        // Each saved release starts 5 ms before its noise, at the note's pitch.
        const auto found = library->release (false, 3, 7, 0);
        REQUIRE (found.take != nullptr);
        CHECK (found.pitchRatio == 1.0);
        const auto& head = found.take->head;
        CHECK (std::abs (head[(size_t) (0.003 * rate)]) < 1.0e-4f);
        CHECK (rms (head, (size_t) (0.006 * rate), (size_t) (0.05 * rate)) > 0.005);
        CHECK_THAT (frequencyOf (head, (size_t) (0.006 * rate), (size_t) (0.1 * rate)) * rate / sampleRate, Catch::Matchers::WithinRel (guitar::frequencyOf (noteOf (3, 7)), 0.03));   // (frequencyOf counts at 48 kHz)
        CHECK (library->release (false, 3, 7, 1).take != found.take);
        CHECK (library->release (false, 3, 7, 2).take == found.take);   // the two in turn
        CHECK (library->release (false, 2, 7, 0).take == nullptr);
        CHECK (library->release (true, 3, 7, 0).take == nullptr);

        // Imported again: the string's old releases replaced, not added to.
        REQUIRE (importAudio (audio.data(), (int) audio.size(), rate, List::pitchedRelease, 3, folder).ok());
        CHECK (Library::load (folder)->noiseCount (List::pitchedRelease, 3) == 50);
    }

    // The notes heard (the notes' Volume wasn't down): refused, nothing saved.
    {
        const auto folder = tempGuitar ("NoiseNotesHeard");
        const auto audio = fakeNoiseRender (List::pitchedRelease, 0, rate, 0.0, 2, 1, true);
        const auto report = importAudio (audio.data(), (int) audio.size(), rate, List::pitchedRelease, 0, folder);
        CHECK_FALSE (report.ok());
        CHECK (report.error.contains ("notes can be heard"));
        CHECK (Library::load (folder)->noiseTotal() == 0);
    }
    // No releases at all: refused.
    {
        const auto folder = tempGuitar ("NoiseSilent");
        std::vector<float> audio ((size_t) (lengthSeconds (List::pitchedRelease, 0) * rate), 0.0f);
        CHECK_FALSE (importAudio (audio.data(), (int) audio.size(), rate, List::pitchedRelease, 0, folder).ok());
    }

    // Unpitched: a noise on every 8th hit, 6 different ones in turn: the 6 kept, in the order they came.
    {
        const auto folder = tempGuitar ("NoiseUnpitched");
        const auto audio = fakeNoiseRender (List::unpitched, 0, rate, 0.0, 6, 8);
        const auto report = importAudio (audio.data(), (int) audio.size(), rate, List::unpitched, 0, folder);
        INFO (report.error);
        REQUIRE (report.ok());
        CHECK (report.written == 6);
        CHECK (report.silent == 336 - 42);
        CHECK (Library::load (folder)->noiseCount (List::unpitched) == 6);
        // ...but a noise on every hit means the pitched releases weren't turned down: refused.
        const auto wrong = fakeNoiseRender (List::unpitched, 0, rate, 0.0, 6, 1);
        CHECK (importAudio (wrong.data(), (int) wrong.size(), rate, List::unpitched, 0, tempGuitar ("NoiseUnpitchedWrong")).error.contains ("Pitched Release"));
    }

    // Fret noise: 13 recordings in turn over 32 hits: the 13 kept, numbered in turn (the first one played first).
    {
        const auto folder = tempGuitar ("NoiseFret");
        const auto audio = fakeNoiseRender (List::fretNoise, 0, rate, 0.004, 13);
        const auto report = importAudio (audio.data(), (int) audio.size(), rate, List::fretNoise, 0, folder);
        INFO (report.error);
        REQUIRE (report.ok());
        CHECK (report.written == 13);
        CHECK (report.copies == 19);
        const auto library = Library::load (folder);
        CHECK (library->noiseCount (List::fretNoise) == 13);
        CHECK (library->fretNoise (0) != library->fretNoise (1));
        CHECK (library->fretNoise (0) == library->fretNoise (13));
    }

    // Power-chord releases: by the root's string and fret.
    {
        const auto folder = tempGuitar ("NoisePower");
        const auto audio = fakeNoiseRender (List::powerChordRelease, 0, rate, 0.0, 1);
        const auto report = importAudio (audio.data(), (int) audio.size(), rate, List::powerChordRelease, 0, folder);
        INFO (report.error);
        REQUIRE (report.ok());
        CHECK (report.written == 48);
        const auto library = Library::load (folder);
        CHECK (library->release (true, 1, 5, 0).take != nullptr);
        CHECK (library->release (true, 1, 20, 0).pitchRatio > 1.0);   // past fret 15: the 15th's, played higher
        CHECK (library->release (true, 4, 5, 0).take == nullptr);     // roots on strings 7-5 only
    }
}

namespace
{
    // A noise in memory: a `hz` tone of `level`, `seconds` long, at 48 kHz.
    std::shared_ptr<Take> noiseTake (double hz, float level = 0.1f, double seconds = 0.1)
    {
        auto t = std::make_shared<Take>();
        t->sampleRate = sampleRate;
        for (int i = 0; i < (int) (seconds * sampleRate); ++i)
            t->head.push_back (level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        t->frames = (juce::int64) t->head.size();
        return t;
    }
}

TEST_CASE ("THE NOISES 0.6.0: a release at each note-off, by how long the note was held; power chords; fret noise; scrapes", "[guitar][engine][noises]")
{
    using noises::List;
    const auto release = noiseTake (3000.0);

    SECTION ("A note-off plays the note's release, from that very sample, at its level")
    {
        Player with, without;
        for (auto* p : { &with, &without })
        {
            p->library->addNoise (List::pitchedRelease, 0, 0, release);
            p->settings.releaseNoise = p == &with ? 1.0f : 0.0f;
            p->play (1.0, { { Player::on (33, 90), 0 }, { Player::off (33), (int) (0.5 * sampleRate) } });
        }
        std::vector<float> noise (with.left.size());
        for (size_t i = 0; i < noise.size(); ++i)
            noise[i] = with.left[i] - without.left[i];
        const auto off = (size_t) (0.5 * sampleRate);
        CHECK (rms (noise, 0, off) < 1.0e-6);                                   // nothing before the note-off
        const auto expected = 0.1 * std::pow (90.0 / 85.0, 0.36);               // a touch louder: velocity 90
        CHECK_THAT (levelAt (noise, 3000.0, off, 2400), Catch::Matchers::WithinRel (expected, 0.03));
        CHECK (std::abs (noise[off]) < 1.0e-6);
        CHECK (std::abs (noise[off + 3]) > 1.0e-3);                             // its first samples right there
        CHECK (rms (noise, off + (size_t) (0.11 * sampleRate), noise.size()) < 1.0e-6);   // played once (0.1 s)
        CHECK (with.guitar.lastNoise().take == release.get());
        CHECK (without.guitar.lastNoise().count == 0);
        // A fret with no release of its own borrows the string's nearest, at the note's pitch.
        Player b;
        b.library->addNoise (List::pitchedRelease, 0, 0, release);
        b.play (0.3, { { Player::on (38), 0 }, { Player::off (38), (int) (0.2 * sampleRate) } });   // low A, 5th fret
        CHECK_THAT (b.guitar.lastNoise().pitchRatio, Catch::Matchers::WithinRel (std::pow (2.0, 5.0 / 12.0), 1e-9));
        // No release on that string: none.
        Player n;
        n.library->addNoise (List::pitchedRelease, 3, 0, release);
        n.play (0.3, { { Player::on (33), 0 }, { Player::off (33), (int) (0.2 * sampleRate) } });
        CHECK (n.guitar.lastNoise().count == 0);
    }

    SECTION ("Held a second: full; 1.5 s: half; 2 s or more: none (the reference played none after 2 s)")
    {
        const auto gainAfter = [&] (double held)
        {
            Player p;
            p.library->addNoise (List::pitchedRelease, 0, 0, release);
            p.play (held + 0.2, { { Player::on (33, 85), 0 }, { Player::off (33), (int) (held * sampleRate) } });
            return p.guitar.lastNoise().count == 0 ? 0.0f : p.guitar.lastNoise().gain;
        };
        CHECK_THAT (gainAfter (0.1), Catch::Matchers::WithinAbs (1.0, 1e-3));
        CHECK_THAT (gainAfter (1.0), Catch::Matchers::WithinAbs (1.0, 1e-3));
        CHECK_THAT (gainAfter (1.5), Catch::Matchers::WithinAbs (0.5, 0.01));
        CHECK (gainAfter (2.5) == 0.0f);
    }

    SECTION ("No release for a palm mute, a note stopped by MONO, or with RELEASE NOISE at 0")
    {
        Player m;
        m.library->addNoise (List::pitchedRelease, 0, 0, release);
        m.play (0.4, { { Player::on (13), 0 }, { Player::on (33), 10 }, { Player::off (33), (int) (0.2 * sampleRate) } });   // C#-1: palm mute
        CHECK (m.guitar.lastNoise().count == 0);
        Player o;
        o.library->addNoise (List::pitchedRelease, 0, 0, release);
        o.settings.mono = true;
        o.play (0.4, { { Player::on (33), 0 }, { Player::on (45), (int) (0.1 * sampleRate) }, { Player::off (33), (int) (0.2 * sampleRate) } });
        CHECK (o.guitar.lastNoise().count == 0);   // (the low A was already stopped by the A string's note)
    }

    SECTION ("A power chord: one release of its own (by the root), else each string's")
    {
        const auto power = noiseTake (2000.0);
        Player p;
        p.library->addNoise (List::powerChordRelease, 0, 0, power);
        for (int s = 0; s < 3; ++s)
            p.library->addNoise (List::pitchedRelease, s, 0, release);
        p.play (0.4, { { Player::on (15), 0 }, { Player::on (33), 10 }, { Player::off (33), (int) (0.2 * sampleRate) } });   // D#-1: power chords
        CHECK (p.guitar.lastNoise().count == 1);
        CHECK (p.guitar.lastNoise().take == power.get());
        Player q;
        for (int s = 0; s < 3; ++s)
            q.library->addNoise (List::pitchedRelease, s, 0, release);
        q.play (0.4, { { Player::on (15), 0 }, { Player::on (33), 10 }, { Player::off (33), (int) (0.2 * sampleRate) } });
        CHECK (q.guitar.lastNoise().count == 3);   // the root, the fifth and the octave: each string's own
    }

    SECTION ("F#0: fret noise, its recordings in turn, by velocity; none at 0")
    {
        const auto a = noiseTake (1000.0), b = noiseTake (1500.0), c = noiseTake (2500.0);
        Player p;
        for (const auto& t : { a, b, c })
            p.library->addNoise (List::fretNoise, 0, 0, t);
        std::vector<const Take*> order;
        std::vector<float> gains;
        for (const auto velocity : { 127, 127, 127, 40 })
        {
            p.play (0.2, { { Player::on (30, velocity), 0 } });
            order.push_back (p.guitar.lastNoise().take);
            gains.push_back (p.guitar.lastNoise().gain);
        }
        CHECK (order == std::vector<const Take*> { a.get(), b.get(), c.get(), a.get() });
        CHECK_THAT (gains[0], Catch::Matchers::WithinAbs (1.0, 1e-6));
        CHECK_THAT (db (gains[3]), Catch::Matchers::WithinAbs (db (std::pow (40.0 / 127.0, 0.85)), 0.01));   // ~ -8.5 dB
        CHECK (p.guitar.getActiveVoices() == 0);   // (no note played)
        Player z;
        z.library->addNoise (List::fretNoise, 0, 0, a);
        z.settings.fretNoise = 0.0f;
        z.play (0.2, { { Player::on (30, 127), 0 } });
        CHECK (z.guitar.lastNoise().count == 0);
    }

    SECTION ("SCRAPES: an unpitched noise with a release now and then, the next in turn, never two releases running")
    {
        const auto u1 = noiseTake (700.0), u2 = noiseTake (900.0);
        Player p;
        p.library->addNoise (List::pitchedRelease, 0, 0, release);
        p.library->addNoise (List::unpitched, 0, 0, u1);
        p.library->addNoise (List::unpitched, 0, 0, u2);
        p.settings.scrapes = 1.0f;   // every time it may
        std::vector<const Take*> last;
        for (int i = 0; i < 4; ++i)
        {
            p.play (0.3, { { Player::on (33), 0 }, { Player::off (33), (int) (0.2 * sampleRate) } });
            last.push_back (p.guitar.lastNoise().take);
        }
        CHECK (last == std::vector<const Take*> { u1.get(), release.get(), u2.get(), release.get() });
        Player none;
        none.library->addNoise (List::pitchedRelease, 0, 0, release);
        none.library->addNoise (List::unpitched, 0, 0, u1);
        none.settings.scrapes = 0.0f;
        for (int i = 0; i < 6; ++i)
            none.play (0.3, { { Player::on (33), 0 }, { Player::off (33), (int) (0.2 * sampleRate) } });
        CHECK (none.guitar.lastNoise().take == release.get());
        CHECK (none.guitar.lastNoise().count == 6);
    }
}

// PARAGON_NOISE_LISTS=<folder> ./GuitarCoreTests "[noise-lists]": the noise lists written as MIDI files.
TEST_CASE ("Write the noise lists", "[.][noise-lists]")
{
    using namespace noises;
    const auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("PARAGON_NOISE_LISTS", "noise-lists"));
    folder.createDirectory();
    const auto write = [&] (List list, int string)
    {
        const auto file = folder.getChildFile (fileName (list, string) + ".mid");
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (toMidi (list, string).writeTo (out));
    };
    for (int s = 0; s < numStrings; ++s)
        write (List::pitchedRelease, s);
    for (const auto list : { List::powerChordRelease, List::unpitched, List::fretNoise })
        write (list, 0);
}

TEST_CASE ("THE ENGINE: streaming live (not waiting for the disk), the note whole and clean", "[guitar][engine]")
{
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("StreamLive");
    const auto audio = fakeRender (Articulation::sustain, 2, rate, 0.0);
    REQUIRE (renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 2, folder).ok());
    const auto library = Library::load (folder);
    Streamer streamer;
    Player p;
    p.settings.library = library.get();
    p.settings.streamer = &streamer;
    p.settings.waitForDisk = false;
    // In real time: a block's worth of time between blocks, as a host gives the disk.
    p.guitar.process (std::vector<float> (blockSize).data(), std::vector<float> (blockSize).data(), 0, {}, 0, p.settings);
    for (int b = 0; b < 375; ++b)
    {
        p.play ((double) blockSize / sampleRate, b == 0 ? std::vector<std::pair<juce::MidiMessage, int>> { { Player::on (45, 120), 0 } }
                                                        : std::vector<std::pair<juce::MidiMessage, int>> {});
        juce::Thread::sleep (5);
    }
    auto peak = 0.0f;
    for (const auto x : p.left)
        peak = std::max (peak, std::abs (x));
    CHECK (peak < 1.5f);
    const auto atPitch = levelAt (p.left, 110.0, (size_t) (1.0 * sampleRate), (size_t) (0.9 * sampleRate));
    CHECK (atPitch > 0.2);                                                                                   // the A2, a second in
    CHECK (levelAt (p.left, 116.5, (size_t) (1.0 * sampleRate), (size_t) (0.9 * sampleRate)) < 0.1 * atPitch);   // not a semitone off
    CHECK (rms (p.left, (size_t) (1.5 * sampleRate), (size_t) (1.9 * sampleRate)) > 0.1);
    INFO ("underruns " << streamer.getUnderruns());
    folder.getParentDirectory().deleteRecursively();
}

TEST_CASE ("IMPORT A RENDER 0.3.0: round-robins that are copies of earlier ones left out, and said", "[guitar][renders]")
{
    // An instrument with only one take per stroke, asked for two: round-robin 2 copies 1.
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("Copies");
    const auto audio = fakeRender (Articulation::sustain, 1, rate, 0.01, 0, 1.0, 1);
    const auto report = renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 1, folder);
    INFO (report.summary());
    REQUIRE (report.ok());
    CHECK (report.written == 150);
    CHECK (report.copies == 150);
    CHECK (report.fewestRoundRobins == 1);
    CHECK (report.summary() == "150 notes imported, all in tune, 150 copies of other takes left out (as few as 1 different per stroke)");
    CHECK (folder.getChildFile ("Sustain/s1_f03_l1_d_r0.wav").existsAsFile());
    CHECK_FALSE (folder.getChildFile ("Sustain/s1_f03_l1_d_r1.wav").exists());
    // The guitar then plays the one it has.
    const auto library = Library::load (folder);
    CHECK (library->find (Articulation::sustain, 1, 3, 1, Stroke::down, 1).take == library->find (Articulation::sustain, 1, 3, 1, Stroke::down, 0).take);

    // 0.6.2: re-importing a string clears its earlier takes first. Both round-robins imported, then a render with one
    // per stroke: the second round-robins (the first import's, now left out as copies) gone, not left to play as if new;
    // another string untouched.
    {
        const auto again = tempGuitar ("Reimport");
        const auto full = fakeRender (Articulation::sustain, 1, rate, 0.01);
        REQUIRE (renders::importAudio (full.data(), (int) full.size(), rate, Articulation::sustain, 1, again).written == 300);
        const auto other = fakeRender (Articulation::sustain, 2, rate, 0.01);
        REQUIRE (renders::importAudio (other.data(), (int) other.size(), rate, Articulation::sustain, 2, again).ok());
        CHECK (again.getChildFile ("Sustain/s1_f03_l1_d_r1.wav").existsAsFile());
        REQUIRE (renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 1, again).written == 150);
        CHECK_FALSE (again.getChildFile ("Sustain/s1_f03_l1_d_r1.wav").exists());
        const auto reloaded = Library::load (again);
        CHECK (reloaded->count (Articulation::sustain, 1) == 150);
        CHECK (reloaded->count (Articulation::sustain, 2) == 300);
    }
    folder.getParentDirectory().deleteRecursively();
}

TEST_CASE ("THE ROUND-ROBIN CHECK: its list; how many different takes each stroke has, and in what order", "[guitar][renders]")
{
    using namespace renders::rrcheck;
    const auto list = takes();
    REQUIRE (list.size() == 3 * 2 * repeats);
    CHECK_THAT (lengthSeconds() / 60.0, WithinAbs (4.03, 0.01));
    // Its MIDI: per take the string forced, the articulation, the stroke, then the note.
    const auto midi = toMidi();
    juce::MidiMessageSequence seq (*midi.getTrack (0));
    auto notes = 0, mutes = 0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (const auto& m = seq.getEventPointer (i)->message; m.isNoteOn())
        {
            notes += m.getNoteNumber() >= lowestNote && m.getNoteNumber() <= highestNote ? 1 : 0;
            mutes += m.getNoteNumber() == renders::keys::mute ? 1 : 0;
        }
    CHECK (notes == 96);
    CHECK (mutes == 32);

    // A made-up render: the first note's downs 4 recordings in turn, its ups 4 in turn; the palm mute's 3 in no fixed
    // order; the D string's 8 in turn. Each recording its own overtone (and so its own shape).
    constexpr double rate = 16000.0;
    std::vector<float> audio ((size_t) (lengthSeconds() * rate), 0.0f);
    juce::Random random (3);
    for (const auto& t : list)
    {
        const auto& g = groups()[(size_t) t.group];
        const auto recording = t.group == 0 ? t.repeat % 4 : t.group == 1 ? random.nextInt (3) : t.repeat % 8;
        const auto hz = guitar::frequencyOf (noteOf (g.string, g.fret));
        const auto start = (int) ((t.noteSeconds + 0.012) * rate);
        for (int i = 0; i < (int) (holdSeconds * rate); ++i)
            audio[(size_t) (start + i)] += 0.5f * std::exp (-2.0f * (float) i / (float) rate)
                                         * (float) (std::sin (juce::MathConstants<double>::twoPi * hz * i / rate)
                                                    + 0.4 * std::sin (juce::MathConstants<double>::twoPi * hz * (2.0 + recording) * i / rate + recording));
    }
    const auto report = analyse (audio.data(), (int) audio.size(), rate);
    INFO (report.summary());
    REQUIRE (report.error.isEmpty());
    REQUIRE (report.results.size() == 6);
    CHECK (report.results[0].different == 4);
    CHECK (report.results[0].inTurn);
    CHECK (report.results[0].describe().startsWith ("Sustain, low A open, hard, down: 4 different, in turn (1 2 3 4 1 2 3 4"));
    CHECK (report.results[1].stroke == Stroke::up);
    CHECK (report.results[2].different == 3);
    CHECK (report.results[4].different == 8);
    CHECK (report.results[4].inTurn);
    CHECK (report.fewest() == 3);
    std::vector<float> silence (audio.size(), 0.0f);
    CHECK (analyse (silence.data(), (int) silence.size(), rate).error.isNotEmpty());
}

TEST_CASE ("THE PALM MUTE CHECK 0.6.1: its list; how alike its copies and different takes are", "[guitar][renders]")
{
    using namespace renders::rrcheck;
    constexpr auto pm = Check::palmMute;
    const auto list = takes (pm);
    REQUIRE (list.size() == 5 * 2 * repeats);
    CHECK_THAT (lengthSeconds (pm), WithinAbs (2.0 + 160 * 1.6, 1e-9));
    CHECK (fileNameFor (pm) == "PR7-I Palm Mute Check");
    // Its MIDI: every note palm-muted (C#-1 before it), the high E's first.
    auto file = toMidi (pm);
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence seq (*file.getTrack (0));
    auto notes = 0, mutes = 0, highE = 0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (const auto& m = seq.getEventPointer (i)->message; m.isNoteOn())
        {
            notes += m.getNoteNumber() >= lowestNote && m.getNoteNumber() <= highestNote ? 1 : 0;
            mutes += m.getNoteNumber() == renders::keys::mute ? 1 : 0;
            highE += m.getNoteNumber() == renders::keys::forceStringLowA + 6 ? 1 : 0;
        }
    CHECK (notes == 160);
    CHECK (mutes == 160);
    CHECK (highE == 64);
    // 0.6.2, the order check: the high E open in the lists' order (down, up..., a picking key before each), the same
    // with ALTERNATE picking set once (no picking keys), the low A in the lists' order.
    {
        constexpr auto oc = Check::order;
        const auto orderTakes = takes (oc);
        REQUIRE (orderTakes.size() == 3 * 2 * repeats);
        CHECK (orderTakes[0].stroke == Stroke::down);
        CHECK (orderTakes[1].stroke == Stroke::up);
        CHECK (orderTakes[1].group == 0);
        CHECK_THAT (lengthSeconds (oc) / 60.0, WithinAbs ((2.0 + 96 * 1.6) / 60.0, 1e-9));
        CHECK (fileNameFor (oc) == "PR7-I Order Check");
        auto f = toMidi (oc);
        juce::MidiMessageSequence orderSeq (*f.getTrack (0));
        auto pickKeys = 0, alternateKeys = 0, played = 0;
        for (int i = 0; i < orderSeq.getNumEvents(); ++i)
            if (const auto& m = orderSeq.getEventPointer (i)->message; m.isNoteOn())
            {
                pickKeys += m.getNoteNumber() == renders::keys::pickDown || m.getNoteNumber() == renders::keys::pickUp ? 1 : 0;
                alternateKeys += m.getNoteNumber() == 110 ? 1 : 0;
                played += m.getNoteNumber() >= lowestNote && m.getNoteNumber() <= highestNote ? 1 : 0;
            }
        CHECK (played == 96);
        CHECK (pickKeys == 64);       // the two sections in the lists' order: one before each note
        CHECK (alternateKeys == 1);   // the ALTERNATE section: set once
    }

    // The round-robin check is just as it was (the owner's render of it still analyses the same).
    CHECK (takes().size() == 3 * 2 * repeats);
    CHECK_THAT (lengthSeconds() / 60.0, WithinAbs (4.03, 0.01));

    // A made-up render. The high E open's downs: 2 recordings in turn (the rest exact copies); its ups: 6. The B
    // string's downs: 2 recordings that differ very slightly (0.995 alike: merged as one by the 0.99 test, which its
    // copies' likeness gives away). The rest: 4 each.
    constexpr double rate = 16000.0;
    std::vector<float> audio ((size_t) (lengthSeconds (pm) * rate), 0.0f);
    for (const auto& t : list)
    {
        const auto& g = groups (pm)[(size_t) t.group];
        const auto recordings = t.group == 0 ? (t.stroke == Stroke::down ? 2 : 6) : 4;
        const auto recording = t.repeat % recordings;
        const auto nearCopy = t.group == 2 && t.stroke == Stroke::down;
        const auto hz = guitar::frequencyOf (noteOf (g.string, g.fret));
        const auto start = (int) ((t.noteSeconds + 0.01) * rate);
        for (int i = 0; i < (int) (holdFor (pm) * rate); ++i)
        {
            const auto w = juce::MathConstants<double>::twoPi * i / rate;
            const auto over = nearCopy ? 0.4 * std::sin (w * hz * 3.0) + (recording == 1 ? 0.05 * std::sin (w * hz * 5.0) : 0.0)
                                       : 0.4 * std::sin (w * hz * (2.0 + recording) + recording);
            audio[(size_t) (start + i)] += 0.5f * std::exp (-8.0f * (float) i / (float) rate) * (float) (std::sin (w * hz) + over);
        }
    }
    const auto report = analyse (audio.data(), (int) audio.size(), rate, pm);
    INFO (report.summary());
    REQUIRE (report.error.isEmpty());
    REQUIRE (report.results.size() == 10);
    CHECK (report.results[0].different == 2);
    CHECK (report.results[0].copiesAlike > 0.9999);                  // exact copies
    CHECK (report.results[0].closestDifferent < 0.99);
    CHECK (report.results[0].describe().contains ("copies alike 1.0000"));
    CHECK (report.results[1].different == 6);
    CHECK (report.results[4].different == 1);                        // the near copies merged...
    CHECK (report.results[4].copiesAlike < 0.999);                   // ...but not exact: said
    CHECK (report.results[4].copiesAlike > 0.99);
}

// PARAGON_PM_CHECK=<render of the palm mute check list> ./GuitarCoreTests "[pm-check]": what it says about the palm mutes
// (PARAGON_ORDER_CHECK=<render of the order check> instead: that one's).
TEST_CASE ("Analyse a render of the palm mute check list", "[.][pm-check]")
{
    const auto order = juce::SystemStats::getEnvironmentVariable ("PARAGON_ORDER_CHECK", "");
    const auto report = order.isNotEmpty() ? renders::rrcheck::analyseFile (juce::File (order), renders::rrcheck::Check::order)
                                           : renders::rrcheck::analyseFile (juce::File (juce::SystemStats::getEnvironmentVariable ("PARAGON_PM_CHECK", "")),
                                                                            renders::rrcheck::Check::palmMute);
    std::cout << report.summary() << std::endl;
}

// PARAGON_RR_CHECK=<render of the check list> ./GuitarCoreTests "[rr-check]": what it says about the instrument.
TEST_CASE ("Analyse a render of the round-robin check list", "[.][rr-check]")
{
    const auto report = renders::rrcheck::analyseFile (juce::File (juce::SystemStats::getEnvironmentVariable ("PARAGON_RR_CHECK", "")));
    std::cout << report.summary() << "\nFewest different takes of a stroke: " << report.fewest() << std::endl;
}

TEST_CASE ("THE ENGINE 0.3.0: VARIATION: each note a touch different in level, start and tuning; none at 0", "[guitar][engine]")
{
    const auto levels = [] (float variation)
    {
        Player p;
        p.settings.pick = PickMode::down;
        p.settings.variation = variation;
        std::vector<double> result;
        for (int i = 0; i < 12; ++i)
        {
            p.left.clear();
            p.play (0.25, { { Player::off (45), 0 }, { Player::on (45, 120), 1 } });
            result.push_back (levelAt (p.left, 110.0, 4800, 4800));   // (after the last note's 80 ms release)
        }
        return result;
    };
    // Off: every note the take's own level (1.0: the hard dynamic's down takes here).
    for (const auto l : levels (0.0f))
        CHECK_THAT (l, WithinAbs (1.0, 0.01));
    // Full: within about half a dB either way (borrowed takes matched to the note's level), and not all alike.
    const auto varied = levels (1.0f);
    auto lowest = 10.0, highest = 0.0;
    for (const auto l : varied)
    {
        CHECK (std::abs (db (l)) < 0.7);
        lowest = std::min (lowest, l);
        highest = std::max (highest, l);
    }
    CHECK (db (highest) - db (lowest) > 0.2);
}

TEST_CASE ("THE ENGINE 0.3.0: BORROWING: with VARIATION, the neighbouring dynamics' and frets' takes too, at its level and pitch", "[guitar][engine]")
{
    // A chug of 64 down strokes on the A string's 5th fret (D3), medium.
    const auto chug = [] (float variation, std::set<const Take*>& used, std::vector<double>& levels)
    {
        Player p;
        p.settings.pick = PickMode::down;
        p.settings.variation = variation;
        const Take* previous = nullptr;
        for (int i = 0; i < 64; ++i)
        {
            p.left.clear();
            p.play (0.2, { { Player::off (50), 0 }, { Player::on (50, 85), 1 } });
            CHECK (p.guitar.lastTake() != previous);   // never the same take twice running
            previous = p.guitar.lastTake();
            used.insert (previous);
            levels.push_back (levelAt (p.left, guitar::frequencyOf (50), 4800, 4800));   // (after the last note's release)
        }
    };
    // VARIATION 0: only its own two takes, in turn, at the medium dynamic's level (0.5 here).
    std::set<const Take*> own;
    std::vector<double> ownLevels;
    chug (0.0f, own, ownLevels);
    CHECK (own.size() == 2);
    for (const auto l : ownLevels)
        CHECK_THAT (l, WithinAbs (0.5, 0.01));
    // Full: now and then (about a third of the notes) the soft and hard dynamics' takes and the 4th and 6th frets' instead,
    // each at the note's own level and pitch (the borrowed dynamics, made at 0.25 and 1.0, matched to 0.5).
    std::set<const Take*> varied;
    std::vector<double> variedLevels;
    chug (1.0f, varied, variedLevels);
    CHECK (varied.size() >= 4);
    for (const auto l : variedLevels)
        CHECK (std::abs (db (l / 0.5)) < 0.7);
}

TEST_CASE ("RENDER LISTS 0.3.0: sustain's exactly as the owner rendered them; palm mute's with 8 round-robins and short slots", "[guitar][renders]")
{
    // SUSTAIN: unchanged (the owner's renders were made from these): 3 dynamics at 45 / 85 / 118, 2 round-robins of each
    // stroke, 4 s notes in 5.5 s slots, the C-1 keyswitch.
    const auto& sustain = renders::spec (Articulation::sustain);
    CHECK (sustain.layers == 3);
    CHECK (sustain.velocities[0] == 45);
    CHECK (sustain.velocities[1] == 85);
    CHECK (sustain.velocities[2] == 118);
    CHECK (sustain.roundRobins == 2);
    CHECK (sustain.holdSeconds == 4.0);
    CHECK (sustain.slotSeconds == 5.5);
    CHECK (sustain.key == 12);
    CHECK (renders::layerFor (Articulation::sustain, 64) == 0);
    CHECK (renders::layerFor (Articulation::sustain, 65) == 1);
    CHECK (renders::layerFor (Articulation::sustain, 102) == 2);

    // MUTE: its own file per string, 8 round-robins of each stroke, 1 s notes in 1.6 s slots, the factory C#-1.
    const auto& mute = renders::spec (Articulation::mute);
    const auto list = renders::takes (Articulation::mute, 0);
    CHECK ((int) list.size() == 25 * mute.layers * 2 * 8);
    CHECK ((int) list.size() == Library::takesPerString (Articulation::mute));
    CHECK (list[15].roundRobin == 7);
    CHECK (list[16].layer == 1);
    // 0.6.2: each note's 8 downs in a row, then its 8 ups (Jupiter plays all its palm mutes only when a stroke repeats).
    for (int i = 0; i < 16; ++i)
    {
        CHECK (list[(size_t) i].stroke == (i < 8 ? Stroke::down : Stroke::up));
        CHECK (list[(size_t) i].roundRobin == i % 8);
        CHECK (list[(size_t) i].layer == 0);
    }
    CHECK (list[16].stroke == Stroke::down);
    CHECK_THAT (list[1].noteSeconds - list[0].noteSeconds, WithinAbs (1.6, 1e-9));
    CHECK (renders::fileName (Articulation::mute, 0) == "PR7-I Mute v2 - String 7 (low A)");   // (0.6.2: the new order)
    const auto name = renders::takeFileName (Articulation::mute, list[15]);
    CHECK (name == "Mute/s0_f00_l0_u_r7.wav");
    const auto id = renders::parseTakeFile (juce::File ("/x").getChildFile (name));
    REQUIRE (id);
    CHECK (id->articulation == Articulation::mute);
    CHECK (id->roundRobin == 7);
    CHECK_FALSE (renders::parseTakeFile (juce::File ("/x/Sustain/s0_f00_l0_u_r7.wav")));   // sustain has two round-robins
    const auto midi = renders::toMidi (Articulation::mute, 0);
    juce::MidiMessageSequence seq (*midi.getTrack (0));
    seq.updateMatchedPairs();
    auto mutes = 0, notes = 0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (const auto* e = seq.getEventPointer (i); e->message.isNoteOn())
        {
            mutes += e->message.getNoteNumber() == renders::keys::mute ? 1 : 0;
            if (e->message.getNoteNumber() >= lowestNote && e->message.getNoteNumber() <= highestNote)
            {
                ++notes;
                CHECK_THAT ((e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) / (960.0 * 2.0), WithinAbs (1.0, 0.001));
            }
        }
    CHECK (mutes == (int) list.size());
    CHECK (notes == (int) list.size());

    // A palm-mute render imports into Mute/, its copies left out (here: only 3 of the 8 round-robins differ).
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("Mute");
    const auto audio = fakeRender (Articulation::mute, 2, rate, 0.004, 0, 1.0, 3);
    const auto report = renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::mute, 2, folder);
    INFO (report.summary());
    REQUIRE (report.ok());
    CHECK (report.written == 25 * mute.layers * 2 * 3);
    CHECK (report.fewestRoundRobins == 3);
    const auto library = Library::load (folder);
    CHECK (library->count (Articulation::mute, 2) == 25 * mute.layers * 2 * 3);
    CHECK (library->count (Articulation::sustain, 2) == 0);
    folder.getParentDirectory().deleteRecursively();
}

TEST_CASE ("ARTICULATIONS 0.3.0: the owner's SPLIT map (C-1 palm mute 1-59 / sustain 60-119, scaled), the FACTORY map", "[guitar][engine]")
{
    // The zones spread over each articulation's whole range (the owner's map has SCALING on).
    CHECK (playedFor (KeyMap::split, 12, 1).articulation == Articulation::mute);
    CHECK (playedFor (KeyMap::split, 12, 1).velocity == 1);
    CHECK (playedFor (KeyMap::split, 12, 59).velocity == 127);
    CHECK (playedFor (KeyMap::split, 12, 30).velocity == 64);
    CHECK (playedFor (KeyMap::split, 12, 60).articulation == Articulation::sustain);
    CHECK (playedFor (KeyMap::split, 12, 60).velocity == 1);
    CHECK (playedFor (KeyMap::split, 12, 119).velocity == 127);
    CHECK (playedFor (KeyMap::split, 12, 125).articulation == Articulation::sustain);   // the reference's rake: not rendered
    CHECK (playedFor (KeyMap::split, 13, 100).powerChord);
    CHECK_FALSE (playedFor (KeyMap::split, 12, 100).powerChord);
    CHECK (playedFor (KeyMap::factory, 13, 30).articulation == Articulation::mute);
    CHECK (playedFor (KeyMap::factory, 13, 30).velocity == 30);
    CHECK (playedFor (KeyMap::factory, 12, 30).articulation == Articulation::sustain);
    CHECK (playedFor (KeyMap::factory, 16, 30).powerChord);
    CHECK (playedFor (KeyMap::factory, 16, 30).articulation == Articulation::mute);
    CHECK (isArticulationKey (KeyMap::split, 12));
    CHECK_FALSE (isArticulationKey (KeyMap::split, 15));
    CHECK (isArticulationKey (KeyMap::factory, 16));

    // SPLIT, as a new instance starts (C-1 latched): velocity 30 a palm mute of the middle degree, 59 the least muted,
    // 1 the most; 60 the softest sustain, 119 the hardest. The palm mutes are gone in 0.3 s here, the sustains ring.
    Player p (true);
    p.settings.keyMap = KeyMap::split;
    p.settings.pick = PickMode::down;
    const auto play = [&p] (int velocity)
    {
        p.left.clear();
        p.play (0.5, { { Player::off (45), 0 }, { Player::on (45, velocity), 1 } });
        return std::pair { levelAt (p.left, 110.0, 4800, 4800), rms (p.left, (size_t) (0.4 * sampleRate), (size_t) (0.5 * sampleRate)) };
    };
    auto [mid, midTail] = play (30);
    CHECK_THAT (mid, WithinAbs (0.3, 0.01));
    CHECK (midTail < 1.0e-4);
    CHECK_THAT (play (59).first, WithinAbs (0.5, 0.01));
    CHECK_THAT (play (1).first, WithinAbs (0.1, 0.01));
    auto [soft, softTail] = play (60);
    CHECK_THAT (soft, WithinAbs (0.25, 0.01));
    CHECK (softTail > 0.1);
    CHECK_THAT (play (119).first, WithinAbs (1.0, 0.02));
    p.play (0.1, { { Player::off (45), 0 }, { Player::on (45, 30), 1 } });
    CHECK (p.guitar.getSounding (2).muted);

    // FACTORY: C#-1 (13) palm mute, velocity as it comes (30: the second degree); C-1 back to sustain.
    p.settings.keyMap = KeyMap::factory;
    p.play (0.05, { { Player::off (45), 0 }, { Player::on (13, 100), 1 } });
    CHECK_THAT (play (30).first, WithinAbs (0.2, 0.01));
    p.play (0.05, { { Player::off (45), 0 }, { Player::on (12, 100), 1 } });
    CHECK_THAT (play (30).first, WithinAbs (0.25, 0.01));
}

TEST_CASE ("PALM MUTE 0.3.0: its eight takes a stroke in turn; power chords as three strings; no palm mutes: sustain stands in", "[guitar][engine]")
{
    // Alternate picking, palm-muted (SPLIT, velocity 30): down 1, up 1 ... down 8, up 8, then again.
    Player p (true);
    p.settings.keyMap = KeyMap::split;
    std::vector<const Take*> order;
    for (int i = 0; i < 32; ++i)
    {
        p.play (0.01, { { Player::off (45), 0 }, { Player::on (45, 30), 1 } });
        order.push_back (p.guitar.lastTake());
    }
    CHECK (std::set<const Take*> (order.begin(), order.begin() + 16).size() == 16);
    for (size_t i = 16; i < order.size(); ++i)
        CHECK (order[i] == order[i - 16]);

    // A power chord (C#-1, SPLIT): D2 played as D5, a barre on the low three strings; its note-off stops all three.
    p.play (1.2, { { Player::off (45), 0 } });   // (a second's rest: the hand goes home)
    p.play (0.3, { { Player::on (13, 100), 1 }, { Player::on (38, 100), 2 } });
    for (int s = 0; s < 3; ++s)
    {
        CHECK (p.guitar.getSounding (s).fret == 5);
        CHECK_FALSE (p.guitar.getSounding (s).muted);
    }
    p.play (0.3, { { Player::off (38), 0 } });
    for (int s = 0; s < 3; ++s)
        CHECK (p.guitar.getSounding (s).fret == -1);
    // ... and palm-muted (velocity 30).
    p.play (0.1, { { Player::on (38, 30), 0 } });
    for (int s = 0; s < 3; ++s)
        CHECK (p.guitar.getSounding (s).muted);

    // A guitar with no palm mutes yet: velocity 30 plays its softest sustain takes instead (and rings).
    Player q;
    q.settings.keyMap = KeyMap::split;
    q.play (0.5, { { Player::on (45, 30), 0 } });
    CHECK_THAT (levelAt (q.left, 110.0, 4800, 4800), WithinAbs (0.25, 0.01));
    CHECK_FALSE (q.guitar.getSounding (2).muted);
    CHECK (q.guitar.getSounding (2).fret == 0);
}

TEST_CASE ("FRETTING 0.4.0: CAPO (nothing below it, its fret as open) and HAND SPAN", "[guitar][fretting]")
{
    FretSettings capo5 { FretMode::natural, -1, 5, 0 };
    CHECK (choose (45, none(), 1, {}) == Position { 2, 0 });       // A2: the open A string
    CHECK (choose (45, none(), 1, capo5) == Position { 1, 5 });    // capo 5: the E string at the capo, as open
    CHECK (choose (38, none(), 1, capo5) == Position { 0, 5 });    // D2: the low A at the capo
    CHECK_FALSE (choose (34, none(), 1, capo5).valid());           // A#1: below the capo on the only string that has it
    FretSettings forced { FretMode::natural, 2, 5, 0 };
    CHECK (choose (45, none(), 1, forced) == Position { 1, 5 });   // forcing the A string open: under the capo, not allowed

    CHECK (handSpan (FretMode::natural) == 4);
    CHECK (handSpan (FretMode::natural, 6) == 6);
    CHECK (followHand (1, 6, FretMode::natural) == 3);             // four frets: 3 to 6
    CHECK (followHand (1, 6, FretMode::natural, 6) == 1);          // six: 1 to 6 already reaches
    CHECK (followHand (8, 5, FretMode::natural, 0, 5) == 8);       // the capo's fret is open: the hand stays
}

TEST_CASE ("THE ENGINE 0.4.0: MONO, ECONOMY picking, ATTACK, HAND RESET, CAPO played, NOW PLAYING", "[guitar][engine]")
{
    SECTION ("MONO: a new note stops the others")
    {
        Player p;
        p.play (0.3, { { Player::on (33), 0 }, { Player::on (45), (int) (0.1 * sampleRate) } });
        CHECK (p.guitar.getActiveVoices() == 2);                   // both ring (two strings)
        Player m;
        m.settings.mono = true;
        m.play (0.3, { { Player::on (33), 0 }, { Player::on (45), (int) (0.1 * sampleRate) } });
        CHECK (m.guitar.getActiveVoices() == 1);
        CHECK (levelAt (m.left, 55.0, (size_t) (0.15 * sampleRate), 4800) < 0.01);   // the low A gone
        CHECK (levelAt (m.left, 110.0, (size_t) (0.15 * sampleRate), 4800) > 0.2);
        // A chord in MONO: only its highest note
        Player c;
        c.settings.mono = true;
        c.play (0.2, { { Player::on (33), 0 }, { Player::on (45), 0 } });
        CHECK (c.guitar.getActiveVoices() == 1);
        CHECK (c.guitar.lastPosition() == Position { 2, 0 });
    }
    SECTION ("ECONOMY: crossing strings the way the pick moves, the same stroke again")
    {
        const auto strokes = [] (PickMode mode)
        {
            Player p;
            p.settings.pick = mode;
            std::vector<Stroke> out;
            for (const auto note : { 33, 40, 45, 40, 33, 33 })   // low A, E, A strings open; back; the low A again
            {
                p.play (0.05, { { Player::on (note), 0 } });
                out.push_back (p.guitar.lastStroke());
            }
            return out;
        };
        const auto D = Stroke::down, U = Stroke::up;
        CHECK (strokes (PickMode::alternate) == std::vector<Stroke> { D, U, D, U, D, U });
        // down on to the next higher string: down again (x2); then up toward the low strings: up again; the same
        // string: alternate
        CHECK (strokes (PickMode::economy) == std::vector<Stroke> { D, D, D, U, U, D });
    }
    SECTION ("ATTACK: the first milliseconds louder, by velocity; none at 0")
    {
        const auto ratio = [] (float attack, int velocity)
        {
            Player p;
            p.library = toneGuitar (48000.0, 1.0);
            p.settings.library = p.library.get();
            p.settings.attack = attack;
            p.play (0.3, { { Player::on (45, velocity), 0 } });
            return rms (p.left, 0, 240) / rms (p.left, 9600, 14400);   // the first 5 ms against 200-300 ms
        };
        CHECK_THAT (ratio (0.0f, 127), WithinAbs (1.0, 0.1));
        CHECK (ratio (1.0f, 127) > 1.8);
        CHECK (ratio (1.0f, 40) < ratio (1.0f, 127));
    }
    SECTION ("HAND RESET: the AUTO hand home after that long with nothing held")
    {
        const auto handAfter = [] (float reset)
        {
            Player p;
            p.settings.handResetSeconds = reset;
            p.settings.forceString = 0;
            p.play (0.1, { { Player::on (45), 0 } });              // the low A at the 12th fret: the hand goes up
            p.settings.forceString = -1;
            p.play (0.5, { { Player::off (45), 0 } });
            return p.guitar.getHand();
        };
        CHECK (handAfter (0.2f) == 1);
        CHECK (handAfter (1.0f) > 5);
    }
    SECTION ("CAPO: notes played from it")
    {
        Player p;
        p.settings.capo = 5;
        p.play (0.3, { { Player::on (45), 0 } });
        CHECK (p.guitar.lastPosition() == Position { 1, 5 });
        CHECK_THAT (frequencyOf (p.left, 2000, 12000), WithinAbs (110.0, 0.3));
    }
    SECTION ("NOW PLAYING: what the last note did")
    {
        Player p (true);
        p.settings.keyMap = KeyMap::split;
        CHECK (p.guitar.getNowPlaying().note == -1);
        p.play (0.1, { { Player::on (12, 100), 0 }, { Player::on (33, 30), 10 } });   // C-1 latched; velocity 30: a palm mute
        auto n = p.guitar.getNowPlaying();
        CHECK (n.note == 33);
        CHECK (n.inputVelocity == 30);
        CHECK (n.articulation == Articulation::mute);
        CHECK (n.velocity == playedFor (KeyMap::split, 12, 30).velocity);
        CHECK (n.layer == renders::layerFor (Articulation::mute, n.velocity));
        CHECK (n.layers == 5);
        CHECK (n.roundRobins == 8);
        CHECK (n.stroke == Stroke::down);
        CHECK (n.string == 0);
        CHECK (n.fret == 0);
        const auto count = n.count;
        p.play (0.1, { { Player::on (45, 90), 0 } });
        n = p.guitar.getNowPlaying();
        CHECK (n.count == count + 1);
        CHECK (n.articulation == Articulation::sustain);
        CHECK (n.stroke == Stroke::up);
        CHECK (n.roundRobins == 2);
    }
}

TEST_CASE ("POLY WINDOW 0.4.0: everything a window late; notes within it one chord (keyswitches first); 0 passes straight through", "[guitar][engine]")
{
    PolyWindow w;
    w.prepare();
    const auto run = [&w] (std::vector<std::pair<juce::MidiMessage, int>> events, int windowSamples, int blocks = 8)
    {
        std::vector<std::tuple<int, int, bool>> out;   // (absolute sample, note, on)
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer in, result;
            for (const auto& [m, at] : events)
                if (at >= b * blockSize && at < (b + 1) * blockSize)
                    in.addEvent (m, at - b * blockSize);
            w.process (in, result, blockSize, windowSamples);
            for (const auto metadata : result)
            {
                const auto m = metadata.getMessage();
                out.emplace_back (b * blockSize + metadata.samplePosition, m.getNoteNumber(), m.isNoteOn());
            }
        }
        return out;
    };
    using E = std::tuple<int, int, bool>;
    // Window 0: as it came.
    w.reset();
    CHECK (run ({ { Player::on (45), 10 }, { Player::on (52), 100 }, { Player::off (45), 300 } }, 0)
           == std::vector<E> { { 10, 45, true }, { 100, 52, true }, { 300, 45, false } });
    // 5 ms (240 samples): the second note, 2 ms after the first, comes with it; the note-off a window late.
    w.reset();
    CHECK (run ({ { Player::on (45), 10 }, { Player::on (52), 106 }, { Player::off (45), 600 } }, 240)
           == std::vector<E> { { 250, 45, true }, { 250, 52, true }, { 840, 45, false } });
    // A keyswitch a hair after its note: pulled to before it.
    w.reset();
    CHECK (run ({ { Player::on (50), 0 }, { Player::on (13, 100), 20 } }, 240)
           == std::vector<E> { { 240, 13, true }, { 240, 50, true } });
    // Notes further apart than the window: each in its own place.
    w.reset();
    CHECK (run ({ { Player::on (45), 0 }, { Player::on (52), 400 } }, 240)
           == std::vector<E> { { 240, 45, true }, { 640, 52, true } });
    // A re-struck note isn't pulled before its own note-off.
    w.reset();
    CHECK (run ({ { Player::on (45), 0 }, { Player::off (45), 50 }, { Player::on (45), 100 } }, 240)
           == std::vector<E> { { 240, 45, true }, { 290, 45, false }, { 340, 45, true } });
}

TEST_CASE ("0.4.2: a note whose copies were left out plays the takes it has evenly in turn (not its first over and over)", "[guitar][engine]")
{
    // A palm-muted low A with three different down takes (and two up) kept, as an import with copies left out leaves it.
    auto library = std::make_shared<Library>();
    std::vector<const Take*> downs, ups;
    for (int l = 0; l < renders::spec (Articulation::mute).layers; ++l)
        for (const auto stroke : { Stroke::down, Stroke::up })
            for (int rr = 0; rr < (stroke == Stroke::down ? 3 : 2); ++rr)
            {
                auto take = std::make_shared<Take>();
                take->sampleRate = 8000.0;
                for (int i = 0; i < 2400; ++i)
                    take->head.push_back (0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * i / 8000.0 + rr));
                take->frames = (juce::int64) take->head.size();
                take->loudness = 0.1f;   // (so the library keeps this very take, not a measured copy)
                library->add (Articulation::mute, 0, 0, l, stroke, rr, take);
                if (l == 1)
                    (stroke == Stroke::down ? downs : ups).push_back (take.get());
            }
    const auto chug = [&] (PickMode pick, int notes)
    {
        Guitar g;
        g.prepare (sampleRate);
        Settings s;
        s.library = library.get();
        s.keyMap = KeyMap::factory;
        s.pick = pick;
        std::vector<const Take*> played;
        std::vector<float> l (blockSize), r (blockSize);
        juce::MidiBuffer ks;
        ks.addEvent (juce::MidiMessage::noteOn (1, 13, (juce::uint8) 100), 0);   // C#-1: palm mute (factory map)
        g.process (l.data(), r.data(), blockSize, ks, 0, s);
        for (int i = 0; i < notes; ++i)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 38), 0);   // the 2nd degree of muting
            g.process (l.data(), r.data(), blockSize, midi, 0, s);
            played.push_back (g.lastTake());
            // NOW PLAYING counts the takes there: "take 1 of 3" for a down, "of 2" for an up
            const auto now = g.getNowPlaying();
            CHECK (now.roundRobins == (now.stroke == Stroke::down ? 3 : 2));
            CHECK (now.roundRobin < now.roundRobins);
        }
        return played;
    };
    // All downs: the three takes in turn, each a third of the time, never twice running.
    const auto d = chug (PickMode::down, 12);
    for (size_t i = 0; i < d.size(); ++i)
    {
        INFO (i);
        CHECK (d[i] == downs[i % 3]);
    }
    // Alternate: downs 1 2 3, ups 1 2, each in turn.
    const auto a = chug (PickMode::alternate, 12);
    for (size_t i = 0; i < a.size(); ++i)
    {
        INFO (i);
        CHECK (a[i] == (i % 2 == 0 ? downs[(i / 2) % 3] : ups[(i / 2) % 2]));
    }
}

// THE A/B COMPARISON (after 0.4.2): the same riff rendered by the owner's guitar instrument (A) and by PR7-I (B),
// both dry, against the MIDI that played them, measured note by note: when each note's pick lands after its note-on,
// how loud, how far it has fallen 30 ms after its note-off (how it ends), and how often consecutive hits of the same
// note are the very same take (a machine gun). Run by hand:
//   PARAGON_AB_A=a.wav PARAGON_AB_B=b.wav PARAGON_AB_MIDI=riff.mid build/tests/GuitarCoreTests "[ab-compare]"
TEST_CASE ("Compare two renders of a riff", "[.][ab-compare]")
{
    const auto env = [] (const char* n) { return juce::SystemStats::getEnvironmentVariable (n, {}); };
    juce::MidiFile midi;
    {
        juce::FileInputStream in (juce::File (env ("PARAGON_AB_MIDI")));
        REQUIRE (in.openedOk());
        REQUIRE (midi.readFrom (in));
    }
    midi.convertTimestampTicksToSeconds();
    struct Note { double on = 0.0, off = -1.0; int key = 0, velocity = 0; };
    std::vector<Note> notes;
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* e : *midi.getTrack (t))
        {
            const auto& m = e->message;
            if (m.isNoteOn() && m.getNoteNumber() >= lowestNote && m.getNoteNumber() < keyswitch::pickUp)
                notes.push_back ({ m.getTimeStamp(), -1.0, m.getNoteNumber(), m.getVelocity() });
            else if (m.isNoteOff())
                for (auto it = notes.rbegin(); it != notes.rend(); ++it)
                    if (it->key == m.getNoteNumber() && it->off < 0.0)
                    {
                        it->off = m.getTimeStamp();
                        break;
                    }
        }
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.on < b.on; });
    REQUIRE (! notes.empty());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    struct Measured { double latencyMs = 0, peakDb = -100, endDb = 0; std::vector<float> signature; };
    const auto measure = [&] (const juce::String& path)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (path)));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        const auto rate = reader->sampleRate;
        std::vector<float> mono ((size_t) audio.getNumSamples());
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                mono[(size_t) i] += audio.getSample (c, i) / (float) audio.getNumChannels();
        const auto at = [&] (double s) { return juce::jlimit (0, (int) mono.size() - 1, (int) std::round (s * rate)); };
        const auto rmsDb = [&] (int from, int length)
        {
            double sum = 0.0;
            for (int i = from; i < juce::jmin ((int) mono.size(), from + length); ++i)
                sum += (double) mono[(size_t) i] * mono[(size_t) i];
            return juce::Decibels::gainToDecibels (std::sqrt (sum / juce::jmax (1, length)), -120.0);
        };
        std::vector<Measured> out;
        for (size_t n = 0; n < notes.size(); ++n)
        {
            const auto next = n + 1 < notes.size() ? notes[n + 1].on : notes[n].on + 2.0;
            const auto from = at (notes[n].on - 0.02), to = at (juce::jmin (next, notes[n].on + 1.5));
            auto peak = 0.0f;
            for (int i = from; i < to; ++i)
                peak = juce::jmax (peak, std::abs (mono[(size_t) i]));
            Measured m;
            if (peak <= 1.0e-4f)
            {
                out.push_back (m);
                continue;
            }
            auto onset = from;
            while (onset < to && std::abs (mono[(size_t) onset]) < 0.1f * peak)
                ++onset;
            m.latencyMs = (onset / rate - notes[n].on) * 1000.0;
            m.peakDb = rmsDb (onset, at (0.03) + 1);   // the first 30 ms
            if (notes[n].off > notes[n].on)
                m.endDb = rmsDb (at (notes[n].off + 0.03), at (0.02) + 1) - m.peakDb;   // 30-50 ms after the note-off, against the pick
            for (int i = onset - at (0.002); i < onset + at (0.04); ++i)
                m.signature.push_back (i >= 0 && i < (int) mono.size() ? mono[(size_t) i] : 0.0f);
            out.push_back (std::move (m));
        }
        return out;
    };
    const auto a = measure (env ("PARAGON_AB_A")), b = measure (env ("PARAGON_AB_B"));
    const auto same = [] (const std::vector<float>& x, const std::vector<float>& y)
    {
        // the same recording: normalised correlation 0.99 or more within a millisecond's shift (48 samples at 48 kHz)
        if (x.empty() || y.empty())
            return 0.0;
        auto best = -1.0;
        for (int shift = -48; shift <= 48; ++shift)
        {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = 64; i + 64 < x.size(); ++i)
            {
                const auto j = (int) i + shift;
                if (j < 0 || j >= (int) y.size())
                    continue;
                xy += x[i] * y[(size_t) j]; xx += x[i] * x[i]; yy += y[(size_t) j] * y[(size_t) j];
            }
            if (xx > 0 && yy > 0)
                best = juce::jmax (best, xy / std::sqrt (xx * yy));
        }
        return best;
    };
    juce::String table = "note  key vel | A: late ms  level dB  end dB | B: late ms  level dB  end dB | same take as the last hit (A / B)\n";
    std::array<int, 2> repeats {}, pairs {};
    std::array<std::vector<double>, 2> lates, ends;
    for (size_t n = 0; n < notes.size(); ++n)
    {
        juce::String sameText;
        for (int w = 0; w < 2; ++w)
        {
            const auto& r = w == 0 ? a : b;
            lates[(size_t) w].push_back (r[n].latencyMs);
            if (notes[n].off > notes[n].on)
                ends[(size_t) w].push_back (r[n].endDb);
            // the last hit of the same key before it
            for (int p = (int) n - 1; p >= 0; --p)
                if (notes[(size_t) p].key == notes[n].key)
                {
                    ++pairs[(size_t) w];
                    const auto c = same (r[(size_t) p].signature, r[n].signature);
                    repeats[(size_t) w] += c >= 0.99 ? 1 : 0;
                    sameText << (w == 0 ? "" : " / ") << (c >= 0.99 ? "SAME" : "diff") << " (" << juce::String (c, 3) << ")";
                    break;
                }
        }
        table << juce::String ((int) n + 1).paddedLeft (' ', 4) << "  " << juce::String (notes[n].key).paddedLeft (' ', 3) << " "
              << juce::String (notes[n].velocity).paddedLeft (' ', 3) << " | " << juce::String (a[n].latencyMs, 1).paddedLeft (' ', 9) << " "
              << juce::String (a[n].peakDb, 1).paddedLeft (' ', 9) << " " << juce::String (a[n].endDb, 1).paddedLeft (' ', 7) << " | "
              << juce::String (b[n].latencyMs, 1).paddedLeft (' ', 9) << " " << juce::String (b[n].peakDb, 1).paddedLeft (' ', 9) << " "
              << juce::String (b[n].endDb, 1).paddedLeft (' ', 7) << " | " << sameText << "\n";
    }
    const auto median = [] (std::vector<double> v) { if (v.empty()) return 0.0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
    table << "\nmedian lateness: A " << juce::String (median (lates[0]), 1) << " ms, B " << juce::String (median (lates[1]), 1) << " ms\n"
          << "median end (30-50 ms after note-off, against the pick): A " << juce::String (median (ends[0]), 1) << " dB, B "
          << juce::String (median (ends[1]), 1) << " dB\n"
          << "the same take as the last hit of that note: A " << repeats[0] << " of " << pairs[0] << ", B " << repeats[1] << " of " << pairs[1] << "\n";
    std::cout << table << std::endl;
    SUCCEED();
}

TEST_CASE ("0.4.3: a note's pick lands 15 ms after its note-on (takes keep 30 ms before the pick; playback starts 15 ms in)", "[guitar][engine]")
{
    // A take as an import cuts it: quiet lead-in, the pick (a sharp step) exactly prerollSeconds in.
    auto library = std::make_shared<Library>();
    const auto rate = 48000.0;
    for (int f = 0; f <= numFrets; ++f)
        for (int l = 0; l < renders::spec (Articulation::sustain).layers; ++l)
            for (const auto stroke : { Stroke::down, Stroke::up })
                for (int rr = 0; rr < renders::spec (Articulation::sustain).roundRobins; ++rr)
                {
                    auto take = std::make_shared<Take>();
                    take->sampleRate = rate;
                    const auto pick = (int) (renders::prerollSeconds * rate);
                    for (int i = 0; i < (int) (0.5 * rate); ++i)
                        take->head.push_back (i < pick ? 0.001f * (float) std::sin (i * 0.3) : 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * (i - pick) / rate));
                    take->frames = (juce::int64) take->head.size();
                    take->loudness = 0.3f;
                    library->add (Articulation::sustain, 2, f, l, stroke, rr, take);
                }
    Guitar g;
    g.prepare (rate);
    Settings s;
    s.library = library.get();
    s.keyMap = KeyMap::factory;
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    juce::MidiBuffer midi;
    const auto noteAt = 1000;
    midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), noteAt);
    g.process (l.data(), r.data(), (int) l.size(), midi, 0, s);
    auto pickAt = -1;
    for (int i = 0; i < (int) l.size() && pickAt < 0; ++i)
        if (std::abs (l[(size_t) i]) > 0.05f)
            pickAt = i;
    INFO ("the pick at " << pickAt << ", the note-on at " << noteAt);
    // (the sine's first sample over 0.05 comes a few samples after the step)
    CHECK (pickAt - noteAt >= (int) (takeStartSeconds * rate));
    CHECK (pickAt - noteAt <= (int) (takeStartSeconds * rate) + 8);
    // nothing loud before it: the lead-in faded in, no click
    for (int i = noteAt; i < noteAt + (int) (takeStartSeconds * rate) - 2; ++i)
        CHECK (std::abs (l[(size_t) i]) < 0.01f);
}
