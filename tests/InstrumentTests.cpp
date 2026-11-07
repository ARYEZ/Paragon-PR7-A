// THE SAMPLE ENGINE (PR7-D 0.19.0): instruments from folders and SFZ files, choosing hits (velocity layers,
// round-robins, adjacent layers, the layer limit), velocity to volume, voices, kit import, and presets / projects
// loading them again. Each test hit is a sine at its own pitch, so what played can be told from the output.
#include "TestUtils.h"
#include <catch2/catch_approx.hpp>
#include "rigs/Rig.h"
#include "PluginTestUtils.h"

#include "dsp/synth/Instrument.h"
#include "dsp/synth/NoteMap.h"

#include <set>

namespace
{
    using namespace synth::drums;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-instrument-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    // A mono WAV: a sine at `hz`, `level` peak, 0.3 s, fading out over its last 5 ms.
    void writeHit (const juce::File& file, double hz, float level, double seconds = 0.3)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (1, (int) (seconds * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto left = b.getNumSamples() - i;
            const auto fade = juce::jmin (1.0f, (float) left / (float) (0.005 * sampleRate));
            b.setSample (0, i, level * fade * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        }
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    float peakOf (const Sample& s)
    {
        auto p = 0.0f;
        for (const auto& c : s.channels)
            for (const auto x : c)
                p = std::max (p, std::abs (x));
        return p;
    }

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        explicit Plugin (const juce::File& presets)
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
            processor.useRigFolder (presets);
        }
        void set (const juce::String& id, float value) { testkit::set (processor, id, value); }
        std::vector<float> hit (int note, int velocity, double seconds = 0.2)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) velocity), 0);
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor.processBlock (buffer, midi);
                midi.clear();
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
        double pitchOf (int note, int velocity)
        {
            processor.prepareToPlay (sampleRate, blockSize);   // only this hit sounding
            const auto out = hit (note, velocity);
            return frequencyOf (out, (size_t) (0.01 * sampleRate), (size_t) (0.15 * sampleRate));
        }
    };
}

TEST_CASE ("SAMPLE ENGINE: a folder of hits shared into layers by loudness, or by the names' numbers", "[instrument]")
{
    TempFolder temp;
    // Nine hits, loudness rising with their pitch, in a name order that isn't their loudness order.
    const auto folder = temp.folder.getChildFile ("Snare");
    for (int i = 0; i < 9; ++i)
        writeHit (folder.getChildFile ("hit " + juce::String ((i * 4) % 9) + ".wav"), 200.0 + 50.0 * ((i * 4) % 9), 0.1f + 0.1f * (float) ((i * 4) % 9));
    const auto inst = instruments::fromFolder (folder);
    REQUIRE (inst != nullptr);
    CHECK (inst->name == "Snare");
    REQUIRE (inst->layers.size() == 3);   // the square root of 9
    CHECK (inst->numHits() == 9);
    CHECK (inst->layers[0].lo == 1);
    CHECK (inst->layers[2].hi == 127);
    for (size_t l = 0; l < 3; ++l)
    {
        CHECK (inst->layers[l].hits.size() == 3);
        for (const auto& h : inst->layers[l].hits)
            CHECK (peakOf (*h.sample) == Catch::Approx (0.2f + 0.3f * (float) l).margin (0.11f));   // the quietest three first
        if (l > 0)
            CHECK (inst->layers[l].level > inst->layers[l - 1].level);
    }

    // Names that number them: v1-v2, rr1-rr3, used as they say (layers by loudness, round-robins in order).
    const auto named = temp.folder.getChildFile ("Kick");
    for (int v = 1; v <= 2; ++v)
        for (int rr = 1; rr <= 3; ++rr)
            writeHit (named.getChildFile ("kick_v" + juce::String (v) + "_rr" + juce::String (rr) + ".wav"), 100.0 * v + 10.0 * rr, 0.3f * (float) v);
    const auto kick = instruments::fromFolder (named);
    REQUIRE (kick != nullptr);
    REQUIRE (kick->layers.size() == 2);
    CHECK (kick->layers[0].hits.size() == 3);
    CHECK (kick->layers[1].hits[2].files[0].contains ("kick_v2_rr3"));
    CHECK (instruments::fromFolder (temp.folder.getChildFile ("Nothing")) == nullptr);
}

TEST_CASE ("SAMPLE ENGINE: choosing hits: the velocity's layer, round-robins never repeating, in turn, the layer limit, adjacent layers", "[instrument]")
{
    // Three layers (1-42, 43-84, 85-127) of four hits, made in memory: each hit's first sample says layer and hit.
    Instrument inst;
    for (int l = 0; l < 3; ++l)
    {
        Instrument::Layer layer;
        layer.lo = 1 + 42 * l;
        layer.hi = l == 2 ? 127 : 42 * (l + 1);
        layer.level = 0.2f * (float) (l + 1);
        for (int h = 0; h < 4; ++h)
        {
            auto s = std::make_shared<Sample>();
            s->channels.assign (1, std::vector<float> (4, (float) (l * 10 + h)));
            layer.hits.push_back ({ s, {} });
        }
        inst.layers.push_back (layer);
    }
    const auto id = [] (const Instrument::Choice& c) { return juce::roundToInt (c.sample->channels[0][0]); };
    HitMemory memory;
    CHECK (inst.layerFor (1) == 0);
    CHECK (inst.layerFor (42) == 0);
    CHECK (inst.layerFor (43) == 1);
    CHECK (inst.layerFor (127) == 2);

    // RANDOMIZE: from the velocity's layer, never the same twice running, every hit used.
    std::set<int> seen;
    auto previous = -1;
    for (int i = 0; i < 200; ++i)
    {
        const auto c = inst.choose (64, true, false, 0, memory);
        CHECK (c.layer == 1);
        CHECK (id (c) / 10 == 1);
        CHECK (id (c) != previous);
        previous = id (c);
        seen.insert (id (c));
    }
    CHECK (seen.size() == 4);
    // In turn: 0, 1, 2, 3, 0...
    HitMemory inTurn;
    for (int i = 0; i < 8; ++i)
        CHECK (id (inst.choose (100, false, false, 0, inTurn)) == 20 + i % 4);
    // LAYER LIMIT 2: only the first two hits.
    std::set<int> limited;
    for (int i = 0; i < 100; ++i)
        limited.insert (id (inst.choose (20, true, false, 2, memory)));
    CHECK (limited == std::set<int> { 0, 1 });
    // ADJACENT LAYERS: near the top of the middle layer, sometimes the top layer's hits, matched to the middle's level;
    // in the middle of it, never.
    auto fromAbove = 0;
    for (int i = 0; i < 300; ++i)
        if (const auto c = inst.choose (82, true, true, 0, memory); c.layer == 2)
        {
            ++fromAbove;
            CHECK (c.gain == Catch::Approx (0.4f / 0.6f));
        }
        else
            CHECK (c.gain == 1.0f);
    CHECK (fromAbove > 50);
    CHECK (fromAbove < 200);
    for (int i = 0; i < 100; ++i)
        CHECK (inst.choose (64, true, true, 0, memory).layer == 1);
}

TEST_CASE ("SAMPLE ENGINE: an SFZ kit: notes, layers, round-robins, mics summed, volume and pan, #define / #include, release regions left out", "[instrument]")
{
    TempFolder temp;
    const auto samples = temp.folder.getChildFile ("Samples");
    writeHit (samples.getChildFile ("kick soft 1.wav"), 100.0, 0.2f);
    writeHit (samples.getChildFile ("kick soft 2.wav"), 110.0, 0.2f);
    writeHit (samples.getChildFile ("kick hard 1.wav"), 120.0, 0.8f);
    writeHit (samples.getChildFile ("kick hard 2.wav"), 130.0, 0.8f);
    writeHit (samples.getChildFile ("snare close.wav"), 300.0, 0.4f);
    writeHit (samples.getChildFile ("snare room.wav"), 300.0, 0.2f);
    writeHit (samples.getChildFile ("hat.wav"), 900.0, 0.5f);
    writeHit (samples.getChildFile ("hat release.wav"), 950.0, 0.5f);
    temp.folder.getChildFile ("hats.sfz").replaceWithText (
        "<region> sample=hat.wav key=f#2 // the closed hat, by its name\n"
        "<region> sample=hat release.wav key=42 trigger=release\n");
    temp.folder.getChildFile ("kit.sfz").replaceWithText (
        "/* A test kit */\n"
        "#define $KICK 36\n"
        "<control> default_path=Samples/\n"
        "<global> volume=0\n"
        "<group> key=$KICK lovel=1 hivel=63 seq_length=2\n"
        "<region> sample=kick soft 1.wav seq_position=1\n"
        "<region> sample=kick soft 2.wav seq_position=2\n"
        "<group> key=$KICK lovel=64 hivel=127 seq_length=2\n"
        "<region> sample=kick hard 1.wav seq_position=1 <region> sample=kick hard 2.wav seq_position=2\n"
        "<group> key=38\n"
        "<region> sample=snare close.wav\n"
        "<region> sample=snare room.wav volume=-6.0206 pan=100\n"
        "<control> default_path=Samples/\n"
        "#include \"hats.sfz\"\n");

    const auto notes = instruments::sfzNotes (temp.folder.getChildFile ("kit.sfz"));
    CHECK (notes == std::map<int, int> { { 36, 4 }, { 38, 2 }, { 42, 1 } });

    const auto sfz = instruments::fromSfz (temp.folder.getChildFile ("kit.sfz"));
    CHECK (sfz.problems.isEmpty());
    REQUIRE (sfz.notes.size() == 3);
    const auto& kick = *sfz.notes.at (36);
    REQUIRE (kick.layers.size() == 2);
    CHECK (kick.layers[0].lo == 1);
    CHECK (kick.layers[0].hi == 63);
    CHECK (kick.layers[1].lo == 64);
    CHECK (kick.layers[0].hits.size() == 2);
    CHECK (kick.layers[1].hits[1].files[0] == "kick hard 2.wav");
    CHECK (kick.note == 36);
    // The snare: its close and room files one hit, the room 6 dB down and only on the right.
    const auto& snare = *sfz.notes.at (38);
    REQUIRE (snare.numHits() == 1);
    const auto& both = *snare.layers[0].hits[0].sample;
    REQUIRE (both.channels.size() == 2);
    auto left = 0.0f, right = 0.0f;
    for (size_t i = 0; i < both.channels[0].size(); ++i)
    {
        left = std::max (left, std::abs (both.channels[0][i]));
        right = std::max (right, std::abs (both.channels[1][i]));
    }
    CHECK (left == Catch::Approx (0.4f).margin (0.01f));
    CHECK (right == Catch::Approx (0.5f).margin (0.01f));   // 0.4 + 0.2 / 2
    // The hat, from the #included file; its release region left out.
    CHECK (sfz.notes.at (42)->numHits() == 1);

    // One note only; a missing file reported.
    CHECK (instruments::fromSfz (temp.folder.getChildFile ("kit.sfz"), 38).notes.size() == 1);
    temp.folder.getChildFile ("broken.sfz").replaceWithText ("<region> sample=gone.wav key=36\n");
    const auto broken = instruments::fromSfz (temp.folder.getChildFile ("broken.sfz"));
    CHECK (broken.notes.empty());
    CHECK (broken.problems.size() == 1);
    // A whole file as one drum, whatever its keys (a snare spread over the keyboard).
    temp.folder.getChildFile ("wide.sfz").replaceWithText ("<region> sample=Samples/snare close.wav lokey=0 hikey=127\n");
    CHECK (instruments::sfzNotes (temp.folder.getChildFile ("wide.sfz")).empty());
    const auto wide = instruments::fromSfz (temp.folder.getChildFile ("wide.sfz"), instruments::wholeFile);
    REQUIRE (wide.notes.size() == 1);
    CHECK (wide.notes.begin()->second->note == -1);

    // In the plugin: the kit imported through General MIDI, and played.
    Plugin p (temp.folder.getChildFile ("Presets"));
    const auto import = p.processor.importSfzKit (temp.folder.getChildFile ("kit.sfz"), notemap::Layout::generalMidi);
    CHECK (import.slotNotes == std::vector<std::pair<int, int>> { { 0, 36 }, { 2, 38 }, { 6, 42 } });
    CHECK (import.unused.isEmpty());
    REQUIRE (p.processor.waitForInstruments());
    CHECK (p.processor.getDrumInstrument (0) == p.processor.getDrumInstrument (0));
    REQUIRE (p.processor.getDrumInstrument (0) != nullptr);
    CHECK (p.processor.getKitSettings().slots[0].model == Model::sample);
    p.set (ParamIDs::drum (0, "Randomize"), 0.0f);
    p.set (ParamIDs::drum (0, "Adjacent"), 0.0f);
    // Soft notes: the soft layer's two hits in turn; hard: the hard layer's.
    CHECK (p.pitchOf (36, 30) == Catch::Approx (100.0).margin (3.0));
    CHECK (p.pitchOf (36, 30) == Catch::Approx (110.0).margin (3.0));
    CHECK (p.pitchOf (36, 30) == Catch::Approx (100.0).margin (3.0));
    CHECK (p.pitchOf (36, 120) == Catch::Approx (120.0).margin (3.0));
    CHECK (p.pitchOf (36, 120) == Catch::Approx (130.0).margin (3.0));
    // The page sees what played last.
    CHECK (p.processor.getLastHit (0) == std::pair { 1, 1 });
}

TEST_CASE ("SAMPLE ENGINE: VELOCITY TO VOLUME, VOICES, and presets / projects / kits loading the instrument again", "[instrument]")
{
    TempFolder temp;
    const auto folder = temp.folder.getChildFile ("Tom");
    for (int i = 0; i < 4; ++i)
        writeHit (folder.getChildFile ("tom_v1_rr" + juce::String (i + 1) + ".wav"), 150.0, 0.5f, 1.0);
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor.loadInstrumentFolder (9, folder);   // HT
    REQUIRE (p.processor.waitForInstruments());
    REQUIRE (p.processor.getDrumInstrument (9) != nullptr);
    const auto peak = [&p] (int velocity)
    {
        p.processor.prepareToPlay (sampleRate, blockSize);
        const auto out = p.hit (45, velocity, 0.1);
        auto m = 0.0f;
        for (const auto x : out)
            m = std::max (m, std::abs (x));
        return m;
    };
    // On (the default): a softer note in the same layer is quieter.
    CHECK (peak (60) < peak (120) * 0.5f);
    // Off: every hit as recorded.
    p.set (ParamIDs::drum (9, "VelToVol"), 0.0f);
    CHECK (peak (60) == Catch::Approx (peak (120)).epsilon (0.01));

    // VOICES 2: five quick hits, at most two of them sounding.
    p.set (ParamIDs::drum (9, "Voices"), 2.0f);
    p.processor.prepareToPlay (sampleRate, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 5; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (10, 45, (juce::uint8) 100), i * 20);
    juce::AudioBuffer<float> buffer (2, blockSize);
    p.processor.processBlock (buffer, midi);
    juce::MidiBuffer none;
    for (int b = 0; b < 4; ++b)
        p.processor.processBlock (buffer, none);   // the faded ones gone
    CHECK (p.processor.getDrumKit().getNumActiveVoices() <= 2);

    // A project: the reference kept, the instrument loaded again from the folder.
    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin q (temp.folder.getChildFile ("Presets"));
    q.processor.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (q.processor.waitForInstruments());
    REQUIRE (q.processor.getDrumInstrument (9) != nullptr);
    CHECK (q.processor.getDrumInstrument (9)->numHits() == 4);
    // A preset.
    const auto rig = p.processor.captureRig();
    CHECK (rig.drumInstruments.at (9).startsWith ("folder||"));
    const auto reread = Rig::fromXml (*rig.toXml());
    REQUIRE (reread.has_value());
    CHECK (reread->drumInstruments.at (9) == rig.drumInstruments.at (9));
    // A kit file.
    REQUIRE (p.processor.saveDrumKit ("With Tom"));
    Plugin r (temp.folder.getChildFile ("Presets"));
    REQUIRE (r.processor.loadDrumKit (r.processor.drumKitsFolder().getChildFile ("With Tom.pr7kit")));
    REQUIRE (r.processor.waitForInstruments());
    CHECK (r.processor.getDrumInstrument (9) != nullptr);
    // Loading one file into the slot takes the instrument out; CLEAR does too.
    r.processor.clearDrumSample (9);
    CHECK (r.processor.getDrumInstrument (9) == nullptr);
    CHECK (r.processor.getInstrumentRef (9).isEmpty());
    // A folder that's gone: reported, nothing loaded.
    folder.deleteRecursively();
    Plugin s (temp.folder.getChildFile ("Presets"));
    s.processor.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (s.processor.waitForInstruments());
    CHECK (s.processor.getDrumInstrument (9) == nullptr);
    CHECK_FALSE (s.processor.getInstrumentProblems().isEmpty());
}
