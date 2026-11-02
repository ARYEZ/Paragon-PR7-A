// DRUMGIZMO KITS (PR7-D 0.27.0): a tiny kit in the DrumGizmo format made here (multichannel float WAVs, each mic a
// sine of its own pitch, louder hits louder), read as the real kits are. Measured: each kit channel's mixer channel and
// side, which instruments take which drums (both feet of a kick together, a rimshot, a stopped crash left out, the
// midimap choosing among a kind's slots), the layers by power and their velocities, the mics apart (a stereo pair, a
// centre mic in both sides), LIGHT's trimming (bleed mics cut short, silent mics left out, more than 8 layers thinned),
// the level, the whole kit onto the slots through the processor and the mixer, and a preset finding a kit by name.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/DrumGizmo.h"
#include "dsp/synth/Instrument.h"
#include "dsp/synth/NoteMap.h"

namespace
{
    using namespace synth;
    using namespace synth::drums;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-dg-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    // The kit's mics, each a sine at its own pitch in every hit.
    const juce::StringArray channels { "KDrumInside", "SnareTop", "OHLeft", "OHRight", "OHCenter", "AmbLeft", "AmbRight", "FTom1" };
    const double pitches[] = { 100.0, 300.0, 1000.0, 1500.0, 2000.0, 700.0, 900.0, 250.0 };

    // A hit's file: 2 s, each channel its sine at `level` x its weight, the close mic (`own`) loudest; channels in
    // `silent` silent.
    void writeHit (const juce::File& file, float level, int own, const juce::Array<int>& silent = {})
    {
        file.getParentDirectory().createDirectory();
        const auto length = (int) (2.0 * sampleRate);
        juce::AudioBuffer<float> b (channels.size(), length);
        for (int c = 0; c < channels.size(); ++c)
        {
            const auto weight = silent.contains (c) ? 0.0f : c == own ? 1.0f : 0.25f;
            for (int i = 0; i < length; ++i)
                b.setSample (c, i, level * weight * (float) std::sin (juce::MathConstants<double>::twoPi * pitches[c] * i / sampleRate));
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, (unsigned int) channels.size(), 32, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, length);
    }

    // An instrument: `count` hits, power and level rising, its close mic `own`.
    void writeInstrument (const juce::File& kit, const juce::String& name, int count, int own, const juce::Array<int>& silent = {})
    {
        juce::String xml ("<?xml version='1.0' encoding='UTF-8'?>\n<instrument version=\"2.0\" name=\"" + name + "\">\n <samples>\n");
        for (int k = 0; k < count; ++k)
        {
            const auto level = 0.02f * std::pow (1.4f, (float) k);
            writeHit (kit.getChildFile (name + "/samples/" + juce::String (k + 1) + "-" + name + ".wav"), juce::jmin (0.5f, level), own, silent);
            xml << "  <sample name=\"" << name << "-" << (k + 1) << "\" power=\"" << juce::String (level * level, 6) << "\">\n";
            for (int c = 0; c < channels.size(); ++c)
                xml << "   <audiofile channel=\"" << channels[c] << "\" file=\"samples/" << (k + 1) << "-" << name << ".wav\" filechannel=\"" << (c + 1) << "\"/>\n";
            xml << "  </sample>\n";
        }
        xml << " </samples>\n</instrument>\n";
        kit.getChildFile (name + "/" + name + ".xml").replaceWithText (xml);
    }

    // The kit: two kick feet, a snare and its rimshot, a stopped crash, a floor tom; a midimap in General MIDI.
    juce::File writeKit (const juce::File& folder)
    {
        const auto kit = folder.getChildFile ("TinyKit");
        const std::tuple<const char*, int, int, int> instruments[] = {
            { "KDrumL", 4, 0, 35 }, { "KDrumR", 4, 0, 36 }, { "Snare", 12, 1, 38 }, { "SnareRimShot", 3, 1, 40 },
            { "CrashLStopped", 2, 2, 56 }, { "FTom1", 3, 7, 43 } };
        juce::String xml ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<drumkit name=\"TinyKit\" samplerate=\"48000\">\n <channels>\n");
        for (const auto& c : channels)
            xml << "  <channel name=\"" << c << "\"/>\n";
        xml << " </channels>\n <instruments>\n";
        juce::String midimap ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<midimap>\n");
        for (const auto& [name, count, own, note] : instruments)
        {
            // The floor tom's hits: the kick's mic hears nothing of them.
            writeInstrument (kit, name, count, own, juce::String (name) == "FTom1" ? juce::Array<int> { 0 } : juce::Array<int> {});
            xml << "  <instrument name=\"" << name << "\" file=\"" << name << "/" << name << ".xml\"/>\n";
            midimap << " <map note=\"" << note << "\" instr=\"" << name << "\"/>\n";
        }
        xml << " </instruments>\n</drumkit>\n";
        kit.getChildFile ("TinyKit_full.xml").replaceWithText (xml);
        kit.getChildFile ("Midimap_full.xml").replaceWithText (midimap + "</midimap>\n");
        return kit.getChildFile ("TinyKit_full.xml");
    }

    const Instrument::Hit::Mic* micOf (const Instrument::Hit& hit, int channel)
    {
        for (const auto& m : hit.mics)
            if (m.channel == channel)
                return &m;
        return nullptr;
    }
    double peakOf (const Sample& s)
    {
        double p = 0.0;
        for (const auto& c : s.channels)
            for (const auto v : c)
                p = std::max (p, (double) std::abs (v));
        return p;
    }
}

TEST_CASE ("DRUMGIZMO: kit channels into mixer channels; instruments onto drums", "[drumgizmo]")
{
    using drumgizmo::micFor;
    CHECK (micFor ("KDrumInside").channel == mixer::kickIn);
    CHECK (micFor ("KDrumOutside").channel == mixer::kickOut);
    CHECK (micFor ("SnareBottom").channel == mixer::snareBottom);
    CHECK (micFor ("Hihat").channel == mixer::hiHat);
    CHECK (micFor ("Ride").channel == mixer::cymbals);
    CHECK (micFor ("Tom1").channel == mixer::tom1);
    CHECK (micFor ("FTom1").channel == mixer::tom2);
    CHECK (micFor ("FTom2").channel == mixer::tom3);
    CHECK (micFor ("OHLeft").channel == mixer::overheads);
    CHECK (micFor ("OHLeft").side == -1);
    CHECK (micFor ("OHRight").side == 1);
    CHECK (micFor ("OHCenter").side == 0);
    CHECK (micFor ("AmbRight").channel == mixer::room);
    CHECK (micFor ("AmbRight").side == 1);
    CHECK (micFor ("Amb L").side == -1);

    TempFolder temp;
    const auto info = drumgizmo::read (writeKit (temp.folder));
    REQUIRE (info.valid());
    CHECK (info.name == "TinyKit");
    CHECK (info.channels.size() == channels.size());
    CHECK (info.instruments[2].note == 38);
    const auto a = drumgizmo::assign (info);
    CHECK (a.slots.at (notemap::BD) == juce::StringArray { "KDrumL", "KDrumR" });   // both feet
    CHECK (a.slots.at (notemap::SD) == juce::StringArray { "Snare" });
    CHECK (a.slots.at (notemap::SD2) == juce::StringArray { "SnareRimShot" });
    CHECK (a.slots.at (notemap::LT) == juce::StringArray { "FTom1" });   // note 43: General MIDI's low tom
    CHECK (a.unused.contains ("CrashLStopped"));
    CHECK (a.slots.count (notemap::CY) == 0);
}

TEST_CASE ("DRUMGIZMO: layers by power, velocities rising, mics apart, LIGHT trimming, the level", "[drumgizmo]")
{
    TempFolder temp;
    const auto kit = writeKit (temp.folder);
    juce::StringArray problems;
    const auto snare = drumgizmo::load (kit, { "Snare" }, drumgizmo::Trim::light(), problems);
    INFO (problems.joinIntoString ("; "));
    REQUIRE (snare != nullptr);
    CHECK (snare->kind == Instrument::Kind::drumGizmo);
    // 12 hits into 8 layers (LIGHT): velocities from 1 to 127, rising, each layer louder than the one before.
    REQUIRE (snare->layers.size() == 8);
    CHECK (snare->layers.front().lo == 1);
    CHECK (snare->layers.back().hi == 127);
    for (size_t k = 1; k < snare->layers.size(); ++k)
    {
        CHECK (snare->layers[k].lo == snare->layers[k - 1].hi + 1);
        CHECK (snare->layers[k].level > snare->layers[k - 1].level);
    }
    // FULL: every hit (12 into 12 layers).
    const auto full = drumgizmo::load (kit, { "Snare" }, drumgizmo::Trim::everything(), problems);
    REQUIRE (full != nullptr);
    CHECK (full->numHits() == 12);

    // The mics: the snare's own (SNARE TOP) at full length; the overheads a stereo pair, OHLeft's 1000 Hz only on the
    // left, OHRight's 1500 Hz only on the right, OHCenter's 2000 Hz in both; the room a pair too.
    const auto& hit = snare->layers.back().hits.front();
    const auto* top = micOf (hit, mixer::snareTop);
    const auto* oh = micOf (hit, mixer::overheads);
    const auto* room = micOf (hit, mixer::room);
    const auto* kick = micOf (hit, mixer::kickIn);
    REQUIRE (top != nullptr);
    REQUIRE (oh != nullptr);
    REQUIRE (room != nullptr);
    REQUIRE (kick != nullptr);
    CHECK (top->sample->channels.size() == 1);
    REQUIRE (oh->sample->channels.size() == 2);
    const auto& left = oh->sample->channels[0];
    const auto& right = oh->sample->channels[1];
    CHECK (levelAt (left, 1500.0, 0, 9600) < 0.01 * levelAt (right, 1500.0, 0, 9600));
    CHECK (levelAt (right, 1000.0, 0, 9600) < 0.01 * levelAt (left, 1000.0, 0, 9600));
    CHECK (levelAt (left, 2000.0, 0, 9600) == Catch::Approx (levelAt (right, 2000.0, 0, 9600)).epsilon (0.01));
    CHECK (room->sample->channels.size() == 2);
    // LIGHT: the snare's own mic and the whole-kit mics keep 2 s (the hit's length, under the 4.5 s cap); the kick mic
    // (bleed here) only 1 s, its end faded.
    CHECK (top->sample->length() == Catch::Approx (2.0 * sampleRate).margin (2));
    CHECK (oh->sample->length() == Catch::Approx (2.0 * sampleRate).margin (2));
    CHECK (kick->sample->length() == Catch::Approx (1.0 * sampleRate).margin (2));
    CHECK (std::abs (kick->sample->channels[0].back()) < 1.0e-4f);
    // The level: the hardest hit's whole-kit mix peaks at -8 dBFS; its mics' balance as recorded (the bleed a quarter
    // of the close mic: -12 dB).
    double loudest = 0.0;
    for (const auto& l : snare->layers)
        for (const auto& h : l.hits)
            loudest = std::max (loudest, peakOf (*h.sample));
    CHECK (juce::Decibels::gainToDecibels (loudest) == Catch::Approx (-8.0).margin (0.05));
    CHECK (levelAt (kick->sample->channels[0], 100.0, 0, 9600) == Catch::Approx (0.25 * levelAt (top->sample->channels[0], 300.0, 0, 9600)).epsilon (0.01));

    // A mic that hears nothing of a hit is left out (the floor tom in the kick mic).
    const auto tom = drumgizmo::load (kit, { "FTom1" }, drumgizmo::Trim::light(), problems);
    REQUIRE (tom != nullptr);
    CHECK (micOf (tom->layers.front().hits.front(), mixer::kickIn) == nullptr);
    CHECK (micOf (tom->layers.front().hits.front(), mixer::tom2) != nullptr);

    // Both feet of the kick in one drum: their 8 hits in its layers, from both instruments.
    const auto both = drumgizmo::load (kit, { "KDrumL", "KDrumR" }, drumgizmo::Trim::everything(), problems);
    REQUIRE (both != nullptr);
    CHECK (both->numHits() == 8);
}

TEST_CASE ("DRUMGIZMO: a whole kit onto the slots, played through the mixer; a preset finds the kit by its name", "[drumgizmo]")
{
    TempFolder temp;
    const auto kit = writeKit (temp.folder);
    ParagonX1AudioProcessor p;
    p.prepareToPlay (sampleRate, blockSize);
    p.useRigFolder (temp.folder.getChildFile ("Presets"));
    const auto result = p.importDrumGizmoKit (kit, ParagonX1AudioProcessor::KitLoad::light);
    CHECK (result.kitName == "TinyKit");
    REQUIRE (p.waitForInstruments());
    CHECK (p.getInstrumentRef (notemap::SD).startsWith ("dg|Snare;light|"));
    CHECK (p.getInstrumentRef (notemap::BD).startsWith ("dg|KDrumL,KDrumR;light|"));
    REQUIRE (p.getDrumInstrument (notemap::SD) != nullptr);
    CHECK (p.getDrumInstrument (notemap::SD)->hasMics());
    // The articulations it has no hits for: off (their stand-ins play).
    CHECK (p.getParameters().getRawParameterValue (ParamIDs::drum (notemap::HO1, "On"))->load() < 0.5f);
    // It remembered where the kit is.
    CHECK (p.knownKitFolder ("TinyKit") == kit.getParentDirectory());

    // The snare, played: its close mic's 300 Hz in MAIN through SNARE TOP; muting that channel takes it out.
    const auto play = [&p]
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (notemap::SD), (juce::uint8) 127), 0);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), blockSize);
        for (int b = 0; b < 40; ++b)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        return out;
    };
    const auto before = levelAt (play(), 300.0, 2400, 4800);
    CHECK (before > 0.01);
    testkit::set (p, ParamIDs::mix (mixer::snareTop, "Mute"), 1.0f);
    CHECK (levelAt (play(), 300.0, 2400, 4800) < before * 0.001);
    testkit::set (p, ParamIDs::mix (mixer::snareTop, "Mute"), 0.0f);

    // A preset naming the kit as "{kit:TinyKit}/..." loads it from where this computer keeps it; one naming a kit
    // never loaded here says so.
    ParagonX1AudioProcessor q;
    q.prepareToPlay (sampleRate, blockSize);
    q.useRigFolder (temp.folder.getChildFile ("Presets"));
    q.loadInstrumentRef (notemap::SD, "dg|Snare;light|{kit:TinyKit}/TinyKit_full.xml");
    REQUIRE (q.waitForInstruments());
    CHECK (q.getDrumInstrument (notemap::SD) != nullptr);
    q.loadInstrumentRef (notemap::SD2, "dg|SnareRimShot;light|{kit:NoSuchKit}/NoSuchKit_full.xml");
    REQUIRE (q.waitForInstruments());
    CHECK (q.getDrumInstrument (notemap::SD2) == nullptr);
    CHECK (q.getInstrumentProblems().joinIntoString (" ").contains ("NoSuchKit"));
}

TEST_CASE ("DISK STREAMING: a streamed kit holds only each hit's head and plays exactly as the same kit in memory, live and bouncing",
           "[drumgizmo][streaming]")
{
    TempFolder temp;
    const auto kit = writeKit (temp.folder);
    using Load = ParagonX1AudioProcessor::KitLoad;
    const auto make = [&] (Load load, bool offline)
    {
        auto p = std::make_unique<ParagonX1AudioProcessor>();
        p->setNonRealtime (offline);
        p->prepareToPlay (sampleRate, blockSize);
        p->useRigFolder (temp.folder.getChildFile ("Presets"));
        p->importDrumGizmoKit (kit, load);
        REQUIRE (p->waitForInstruments());
        return p;
    };
    // Snare and kick at once, then again 1 s later (the streams of the first freed and taken again), 3 s in all; live
    // (not offline), each block waits a little, as a real audio callback comes at its time, so the disk thread keeps up.
    const auto play = [] (ParagonX1AudioProcessor& p, bool paced)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), blockSize);
        const auto blocks = (int) (3.0 * sampleRate / blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 || b == (int) (sampleRate / blockSize))
                for (const auto slot : { (int) notemap::SD, (int) notemap::BD })
                    midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (slot), (juce::uint8) 120), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            if (paced)
                juce::Thread::sleep (2);
        }
        return out;
    };
    const auto memory = make (Load::full, true);
    const auto bounced = make (Load::streamed, true);
    const auto live = make (Load::streamed, false);

    // Streamed: each mic holds 0.2 s (the hits are 2 s long); in memory: all of it.
    const auto& streamedHit = bounced->getDrumInstrument (notemap::SD)->layers.back().hits.front();
    const auto& memoryHit = memory->getDrumInstrument (notemap::SD)->layers.back().hits.front();
    REQUIRE_FALSE (streamedHit.mics.empty());
    for (const auto& m : streamedHit.mics)
    {
        CHECK (m.sample->stream != nullptr);
        CHECK (m.sample->length() == Catch::Approx (0.2 * sampleRate).margin (1));
        CHECK (m.sample->frames() == Catch::Approx (2.0 * sampleRate).margin (1));
    }
    CHECK (memoryHit.mics.front().sample->length() == Catch::Approx (2.0 * sampleRate).margin (1));
    CHECK (bounced->getDrumInstrument (notemap::SD)->streamed());

    const auto reference = play (*memory, false);
    const auto fromDisk = play (*bounced, false);
    const auto fromDiskLive = play (*live, true);
    REQUIRE (reference.size() == fromDisk.size());
    double worst = 0.0, worstLive = 0.0, loudest = 0.0;
    // Compared up to the first hit's last 5 ms: in memory a hit's end is faded (it may have been cut short); streamed,
    // it ends as its file does (a kit's files end in silence).
    for (size_t i = 0; i < (size_t) (1.99 * sampleRate); ++i)
    {
        worst = std::max (worst, (double) std::abs (reference[i] - fromDisk[i]));
        worstLive = std::max (worstLive, (double) std::abs (reference[i] - fromDiskLive[i]));
        loudest = std::max (loudest, (double) std::abs (reference[i]));
    }
    INFO ("loudest " << loudest << ", bounce differs by " << worst << ", live by " << worstLive << ", underruns " << live->getStreamUnderruns());
    REQUIRE (loudest > 0.05);
    // Well past the head (1.5 s into a 2 s hit) it still sounds, from the disk.
    CHECK (rms (fromDisk, (size_t) (0.5 * sampleRate), (size_t) (0.9 * sampleRate)) > 0.001);
    CHECK (worst < 1.0e-5);       // the same, to float rounding (gains multiply in another order)
    CHECK (worstLive < 1.0e-5);
    CHECK (live->getStreamUnderruns() == 0);
    // Every stream free again once the hits are over.
    CHECK (live->getActiveStreams() == 0);
    CHECK (bounced->getActiveStreams() == 0);
}
