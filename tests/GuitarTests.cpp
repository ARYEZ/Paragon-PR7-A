// THE GUITAR (PR7-I 0.2.0): fretting, the render list, importing a render, the library and the playing engine. The
// renders here are made up (tones at each take's pitch, where the list puts them, a little late as a DAW would), so
// the import's cutting and checking are measured exactly.
#include "TestUtils.h"

#include "guitar/Guitar.h"
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
            for (int i = 0; i < (int) (renders::holdSeconds * rate); ++i)
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
    std::shared_ptr<Library> toneGuitar (double rate = 8000.0, double seconds = 3.0)
    {
        auto library = std::make_shared<Library>();
        for (int s = 0; s < numStrings; ++s)
            for (int f = 0; f <= numFrets; ++f)
                for (int l = 0; l < renders::numLayers; ++l)
                    for (const auto stroke : { Stroke::down, Stroke::up })
                        for (int rr = 0; rr < renders::numRoundRobins; ++rr)
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
        Player()
        {
            guitar.prepare (sampleRate);
            settings.library = library.get();
            settings.velocityToVolume = 0.0f;
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
    REQUIRE (list.size() == 25 * 3 * 2 * 4);
    REQUIRE ((int) list.size() == Library::takesPerString);
    CHECK_THAT (list.front().noteSeconds, WithinAbs (2.25, 1e-9));
    CHECK_THAT (list[1].noteSeconds - list[0].noteSeconds, WithinAbs (renders::slotSeconds, 1e-9));
    CHECK (list[0].stroke == Stroke::down);
    CHECK (list[1].stroke == Stroke::up);
    CHECK (list[2].stroke == Stroke::down);   // down, up, down, up...: the four round-robins of each stroke
    CHECK (list[2].roundRobin == 1);
    CHECK (list[7].roundRobin == 3);
    CHECK (list[7].layer == 0);
    CHECK (list[8].layer == 1);
    CHECK (list.back().fret == 24);
    CHECK (list.back().note() == 33 + 24);
    for (const auto& t : list)
        CHECK (t.velocity() <= 119);   // sustain's velocities (above: the reference's rake and pinch harmonic)
    CHECK_THAT (renders::lengthSeconds (Articulation::sustain, 0), WithinAbs (2.0 + 600 * 5.5, 1e-9));
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
    CHECK (notes == 600);
    CHECK_THAT (firstNote, WithinAbs (2.25, 0.001));
    CHECK (firstStrokeKey < firstNote);

    // A take's file name, and back.
    const auto name = renders::takeFileName (Articulation::sustain, list[7]);
    CHECK (name == "Sustain/s0_f00_l0_u_r3.wav");
    const auto id = renders::parseTakeFile (juce::File ("/x").getChildFile (name));
    REQUIRE (id);
    CHECK (id->fret == 0);
    CHECK (id->layer == 0);
    CHECK (id->stroke == Stroke::up);
    CHECK (id->roundRobin == 3);
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
    CHECK (report.written == 600);
    CHECK (report.copies == 0);
    CHECK (report.outOfTune.isEmpty());
    CHECK_THAT (report.offsetSeconds, WithinAbs (0.023, 0.001));
    CHECK (progress.load() == 1.0f);
    CHECK (report.summary() == "600 notes imported, all in tune");

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
    CHECK_THAT ((double) take.getNumSamples() / rate, WithinAbs (renders::prerollSeconds + renders::holdSeconds + renders::tailSeconds, 0.001));

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
    CHECK (library->count (Articulation::sustain, 0) == 600);
    CHECK (library->total() == 600);
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

    // DOWN: every stroke a down stroke, its round-robins at random but never the take just played (a chug of 32 downs:
    // no take twice running, all four used); the soft and medium dynamics by velocity.
    p.settings.pick = PickMode::down;
    std::set<const Take*> used;
    const Take* previous = nullptr;
    for (int i = 0; i < 32; ++i)
    {
        p.play (0.01, { { Player::off (45), 0 }, { Player::on (45, 120), 1 } });
        CHECK (p.guitar.lastTake() != previous);
        previous = p.guitar.lastTake();
        used.insert (previous);
    }
    CHECK (used.size() == (size_t) renders::numRoundRobins);
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
    for (int s = 0; s < numStrings; ++s)
    {
        const auto file = folder.getChildFile (renders::fileName (Articulation::sustain, s) + ".mid");
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (renders::toMidi (Articulation::sustain, s).writeTo (out));
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
    // An instrument with only two takes per stroke, asked for four: round-robins 3 and 4 copy 1 and 2.
    constexpr double rate = 8000.0;
    const auto folder = tempGuitar ("Copies");
    const auto audio = fakeRender (Articulation::sustain, 1, rate, 0.01, 0, 1.0, 2);
    const auto report = renders::importAudio (audio.data(), (int) audio.size(), rate, Articulation::sustain, 1, folder);
    INFO (report.summary());
    REQUIRE (report.ok());
    CHECK (report.written == 300);
    CHECK (report.copies == 300);
    CHECK (report.fewestRoundRobins == 2);
    CHECK (report.summary() == "300 notes imported, all in tune, 300 copies of other takes left out (as few as 2 different per stroke)");
    CHECK (folder.getChildFile ("Sustain/s1_f03_l1_d_r1.wav").existsAsFile());
    CHECK_FALSE (folder.getChildFile ("Sustain/s1_f03_l1_d_r2.wav").exists());
    // The guitar then cycles the two it has.
    const auto library = Library::load (folder);
    CHECK (library->find (Articulation::sustain, 1, 3, 1, Stroke::down, 2).take == library->find (Articulation::sustain, 1, 3, 1, Stroke::down, 0).take);
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
            result.push_back (levelAt (p.left, 110.0, 2400, 9600));
        }
        return result;
    };
    // Off: every note the take's own level (1.0: the hard dynamic's down takes here).
    for (const auto l : levels (0.0f))
        CHECK_THAT (l, WithinAbs (1.0, 0.01));
    // Full: within half a dB either way, and not all alike.
    const auto varied = levels (1.0f);
    auto lowest = 10.0, highest = 0.0;
    for (const auto l : varied)
    {
        CHECK (std::abs (db (l)) < 0.55);
        lowest = std::min (lowest, l);
        highest = std::max (highest, l);
    }
    CHECK (db (highest) - db (lowest) > 0.2);
}
