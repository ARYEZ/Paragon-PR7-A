// THE GUITAR (PR7-I 0.2.0): fretting, the render list, importing a render, the library and the playing engine. The
// renders here are made up (tones at each take's pitch, where the list puts them, a little late as a DAW would), so
// the import's cutting and checking are measured exactly.
#include "TestUtils.h"

#include "guitar/Guitar.h"
#include "guitar/PolyWindow.h"
#include "guitar/RenderImport.h"
#include "guitar/RoundRobinCheck.h"

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
    {
        const auto file = folder.getChildFile (renders::rrcheck::fileName + ".mid");
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (renders::rrcheck::toMidi().writeTo (out));
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
    CHECK_THAT (list[1].noteSeconds - list[0].noteSeconds, WithinAbs (1.6, 1e-9));
    CHECK (renders::fileName (Articulation::mute, 0) == "PR7-I Mute - String 7 (low A)");
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
