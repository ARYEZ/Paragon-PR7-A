// IMPORT A RENDER (PR7-D 0.51.0): a drum bounced out of another program a mic at a time, with its MIDI, made into
// layers. Measured on a render made here: three mics (the room's sound 5 ms after the close mic's, as in a real room),
// a whole mix and an unknown file beside them, two notes (a snare's centre at three velocities, its rimshot at one),
// three round robins of each velocity; the hits found where the MIDI says (and where it doesn't, when the bounce
// started early), cut with every mic at the same moments, sorted into velocity layers and round robins, loaded as
// layers on the drums the notes play, playing their mics in their channels in time.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <set>

#include "dsp/synth/Instrument.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/NoteMap.h"
#include "dsp/synth/RenderImport.h"

namespace
{
    using namespace synth;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-render-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    struct Note { double seconds; int note, velocity, round; };

    // The notes: the centre (38) at velocities 40, 80, 120, three rounds each, then the rimshot (40) twice, 0.5 s apart.
    std::vector<Note> renderNotes()
    {
        std::vector<Note> notes;
        auto t = 0.5;
        for (const auto v : { 40, 80, 120 })
            for (int r = 0; r < 3; ++r, t += 0.5)
                notes.push_back ({ t, 38, v, r });
        for (int r = 0; r < 2; ++r, t += 0.5)
            notes.push_back ({ t, 40, 100, r });
        return notes;
    }

    // A mic's file: each hit a burst at the mic's pitch (each round a little higher, the rimshot higher still), as loud
    // as its velocity, `late` seconds after the note (the room's distance), the whole file `shift` later than the MIDI.
    void writeMic (const juce::File& file, double hz, double late, int channels, double shift = 0.0, double decay = 0.02)
    {
        const auto notes = renderNotes();
        const auto length = (int) ((notes.back().seconds + 1.0 + shift) * sampleRate);
        juce::AudioBuffer<float> b (channels, length);
        b.clear();
        for (const auto& n : notes)
        {
            const auto start = (int) std::llround ((n.seconds + late + shift) * sampleRate);
            const auto f = hz * (n.note == 40 ? 1.5 : 1.0) + 40.0 * n.round;
            const auto level = 0.6f * (float) n.velocity / 127.0f;
            for (int i = 0; start + i < length && i < (int) (0.4 * sampleRate); ++i)
            {
                const auto t = i / sampleRate;
                const auto x = level * (float) (std::exp (-t / decay) * std::sin (juce::MathConstants<double>::twoPi * f * t + 0.5));
                for (int c = 0; c < channels; ++c)
                    b.addSample (c, start + i, x);
            }
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, (unsigned int) channels, 32, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, length);
    }

    void writeMidi (const juce::File& file)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);   // 120 BPM: 960 ticks a half second
        for (const auto& n : renderNotes())
        {
            const auto ticks = n.seconds * 2.0 * 960.0;
            track.addEvent (juce::MidiMessage::noteOn (10, n.note, (juce::uint8) n.velocity), ticks);
            track.addEvent (juce::MidiMessage::noteOff (10, n.note), ticks + 120.0);
        }
        track.updateMatchedPairs();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (960);
        midi.addTrack (track);
        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        midi.writeTo (out);
    }

    // The render: SNARE TOP (mono, 1 kHz), OH (stereo, 1.6 kHz, 2 ms late), ROOM (stereo, 2.3 kHz, 5 ms late), a whole
    // mix and a file no channel goes by.
    void writeRender (const juce::File& folder, bool withMidi, double shift = 0.0)
    {
        folder.createDirectory();
        writeMic (folder.getChildFile ("Snare Top.wav"), 1000.0, 0.0, 1, shift);
        writeMic (folder.getChildFile ("OH.wav"), 1600.0, 0.002, 2, shift);
        writeMic (folder.getChildFile ("Room.wav"), 2300.0, 0.005, 2, shift);
        writeMic (folder.getChildFile ("Main Mix.wav"), 700.0, 0.0, 2, shift);
        writeMic (folder.getChildFile ("Shaker Thing.wav"), 700.0, 0.0, 1, shift);
        if (withMidi)
            writeMidi (folder.getChildFile ("snare render.mid"));
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        TempFolder temp;
        Plugin()
        {
            processor->setNonRealtime (true);
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (temp.folder.getChildFile ("Presets"));
        }
        void set (const juce::String& id, float value) { testkit::set (*processor, id, value); }
        float get (const juce::String& id) const
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor->getParameters().getParameter (id));
            return p->convertFrom0to1 (p->getValue());
        }
        std::vector<float> play (int slot, int velocity, double seconds = 0.25)
        {
            processor->prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (slot), (juce::uint8) velocity), 0);
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor->processBlock (buffer, midi);
                midi.clear();
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };
    int firstLoud (const std::vector<float>& x, float threshold = 0.005f)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > threshold)
                return (int) i;
        return -1;
    }
}

TEST_CASE ("IMPORT A RENDER: the mics by their names, the hits where the MIDI says", "[renderimport]")
{
    TempFolder temp;
    const auto folder = temp.folder.getChildFile ("Foundry Snare");
    writeRender (folder, true);
    const auto plan = drums::renderimport::scan (folder);
    REQUIRE (plan.ok());
    REQUIRE (plan.mics.size() == 3);
    std::set<int> channels;
    for (const auto& m : plan.mics)
        channels.insert (m.channel);
    CHECK (channels == std::set<int> { mixer::snareTop, mixer::overheads, mixer::room });
    REQUIRE (plan.leftOut.size() == 2);
    CHECK (plan.leftOut.joinIntoString (";").contains ("Main Mix.wav (a whole mix"));
    CHECK (plan.leftOut.joinIntoString (";").contains ("Shaker Thing.wav (no mixer channel"));
    CHECK (plan.midi.getFileName() == "snare render.mid");
    const auto notes = renderNotes();
    REQUIRE (plan.hits.size() == notes.size());
    CHECK (std::abs (plan.shiftSeconds) < 0.0005);
    CHECK (plan.offTime == 0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK (plan.hits[i].note == notes[i].note);
        CHECK (plan.hits[i].velocity == notes[i].velocity);
        CHECK (plan.hits[i].seconds == Catch::Approx (notes[i].seconds - drums::renderimport::preRollSeconds).margin (0.0006));
    }
    // The bounce started earlier than the MIDI (its sound later): a quarter second (half the hits' spacing: each note
    // as near the hit before it as the one after), and a whole bar (2 s at 120 BPM): followed.
    for (const auto shift : { 0.25, 2.0 })
    {
        INFO ("shift " << shift);
        const auto late = temp.folder.getChildFile ("Late Bounce " + juce::String (shift));
        writeRender (late, true, shift);
        const auto shifted = drums::renderimport::scan (late);
        REQUIRE (shifted.ok());
        CHECK (shifted.shiftSeconds == Catch::Approx (shift).margin (0.0006));
        CHECK (shifted.offTime == 0);
        CHECK (shifted.hits.front().seconds == Catch::Approx (0.5 + shift - drums::renderimport::preRollSeconds).margin (0.0006));
    }
}

TEST_CASE ("IMPORT A RENDER: written as a multi-mic folder per note, velocity layers and round robins, tails trimmed", "[renderimport]")
{
    TempFolder temp;
    const auto folder = temp.folder.getChildFile ("Foundry Snare");
    writeRender (folder, true);
    const auto plan = drums::renderimport::scan (folder);
    const auto into = temp.folder.getChildFile ("out");
    const auto written = drums::renderimport::write (plan, into, "Foundry Snare");
    REQUIRE (written.size() == 2);
    const auto& centre = written[0];
    CHECK (centre.note == 38);
    CHECK (centre.layers == 3);
    CHECK (centre.rounds == 3);
    CHECK (centre.hits == 9);
    CHECK (centre.velocities == std::vector<int> { 40, 80, 120 });
    CHECK (written[1].note == 40);
    CHECK (written[1].layers == 1);
    CHECK (written[1].rounds == 2);
    // A folder per mic, each with every hit.
    for (const auto* mic : { "Snare Top", "OH", "Room" })
        CHECK (centre.folder.getChildFile (mic).findChildFiles (juce::File::findFiles, false, "*.wav").size() == 9);
    // Tails: a 20 ms decay is 70 dB down after 0.16 s: cut there (plus a 10 ms fade), not at the next hit (0.5 s).
    const auto file = centre.folder.getChildFile ("Snare Top").getChildFile ("hit_vel080_rr2.wav");
    REQUIRE (file.existsAsFile());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (file), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples < (juce::int64) (0.2 * sampleRate));
    CHECK (reader->lengthInSamples > (juce::int64) (0.15 * sampleRate));
    // The folder loader reads it as a multi-mic instrument with those velocities (halfway between them).
    const auto inst = drums::instruments::fromFolder (centre.folder);
    REQUIRE (inst != nullptr);
    CHECK (inst->hasMics());
    REQUIRE (inst->layers.size() == 3);
    CHECK (inst->layers[0].lo == 1);
    CHECK (inst->layers[0].hi == 60);
    CHECK (inst->layers[1].lo == 61);
    CHECK (inst->layers[1].hi == 100);
    CHECK (inst->layers[2].lo == 101);
    CHECK (inst->layers[2].hi == 127);
    for (const auto& l : inst->layers)
        CHECK (l.hits.size() == 3);
}

TEST_CASE ("IMPORT A RENDER: each note a layer on its drum; its mics in their channels, still in time with each other", "[renderimport]")
{
    Plugin p;
    using drums::notemap::SD;
    using drums::notemap::SD2;
    testkit::useSample (*p.processor, SD, testkit::sine (300.0, 1.0, 0.0f));   // the drums silent: the layers alone
    testkit::useSample (*p.processor, SD2, testkit::sine (300.0, 1.0, 0.0f));
    const auto folder = p.temp.folder.getChildFile ("Foundry Snare");
    writeRender (folder, true);
    auto result = p.processor->prepareRenderImport (folder, {}, drums::notemap::Layout::grooveLibrary, 0);
    REQUIRE (result.items.size() == 2);
    CHECK (result.items[0].slot == SD);
    CHECK (result.items[1].slot == SD2);
    CHECK (result.items[0].folder.isAChildOf (p.processor->renderedLayersFolder()));
    p.processor->applyRenderImport (result);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (result.items[0].layer == 0);
    const auto layer = p.processor->getLayer (SD, 0);
    REQUIRE (layer != nullptr);
    REQUIRE (layer->layers.size() == 3);
    // Its close mic in its own USER channel, its overheads and room in theirs.
    const auto channel = juce::roundToInt (p.get (ParamIDs::drum (SD, "LayerOut"))) - 1;
    REQUIRE (mixer::isUserChannel (channel));
    const auto soloed = [&] (int c, int velocity)
    {
        p.set (ParamIDs::mix (c, "Solo"), 1.0f);
        const auto x = p.play (SD, velocity);
        p.set (ParamIDs::mix (c, "Solo"), 0.0f);
        return x;
    };
    const auto close = soloed (channel, 120), room = soloed (mixer::room, 120);
    const auto atClose = firstLoud (close), atRoom = firstLoud (room);
    INFO ("close mic from " << atClose << ", room from " << atRoom);
    REQUIRE (atClose >= 0);
    REQUIRE (atRoom >= 0);
    CHECK (atRoom - atClose == Catch::Approx (240).margin (3));   // the room still 5 ms after the close mic
    CHECK (atClose == Catch::Approx (48).margin (3));             // 1 ms kept before each hit's start
    CHECK (frequencyOf (close, (size_t) atClose + 48, (size_t) atClose + 2400) == Catch::Approx (1000.0).margin (50.0 * 2 + 5));
    CHECK (frequencyOf (room, (size_t) atRoom + 48, (size_t) atRoom + 2400) == Catch::Approx (2300.0).margin (50.0 * 2 + 5));
    // The velocity layers: a soft hit plays the 40 one (quieter), a hard one the 120 one.
    const auto soft = soloed (channel, 30), hard = soloed (channel, 127);
    CHECK (rms (soft, 0, 4800) < rms (hard, 0, 4800) * 0.6);
    // The rimshot: on SD2.
    REQUIRE (p.processor->getLayer (SD2, 0) != nullptr);
    CHECK (p.processor->getLayer (SD2, 0)->numHits() == 2);
}

TEST_CASE ("IMPORT A RENDER: no MIDI, the hits found by listening, their velocities by loudness; stopping it loads nothing", "[renderimport]")
{
    Plugin p;
    const auto folder = p.temp.folder.getChildFile ("No Midi");
    writeRender (folder, false);
    const auto plan = drums::renderimport::scan (folder);
    REQUIRE (plan.ok());
    CHECK (! plan.midi.existsAsFile());
    const auto notes = renderNotes();
    REQUIRE (plan.hits.size() == notes.size());
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK (plan.hits[i].note == -1);
        CHECK (plan.hits[i].seconds == Catch::Approx (notes[i].seconds - drums::renderimport::preRollSeconds).margin (0.0006));
    }
    // Louder hits, higher velocities: the 120s above the 80s above the 40s.
    CHECK (plan.hits[0].velocity < plan.hits[3].velocity);
    CHECK (plan.hits[3].velocity < plan.hits[6].velocity);
    CHECK (plan.hits[6].velocity == 127);
    // Onto the drum it was meant for.
    auto result = p.processor->prepareRenderImport (folder, {}, drums::notemap::Layout::grooveLibrary, drums::notemap::LT);
    REQUIRE (result.items.size() == 1);
    CHECK (result.items[0].slot == drums::notemap::LT);
    CHECK (result.items[0].hits == (int) notes.size());
    // Stopped part way: nothing loaded.
    auto calls = 0;
    auto stopped = p.processor->prepareRenderImport (folder, {}, drums::notemap::Layout::grooveLibrary, drums::notemap::LT,
                                                     [&calls] (double) { return ++calls < 3; });
    CHECK (stopped.stopped);
    p.processor->applyRenderImport (stopped);
    CHECK (p.processor->getLayerRef (drums::notemap::LT, 0).isEmpty());
}
