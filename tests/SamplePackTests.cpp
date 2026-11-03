// SAMPLE PACKS (PR7-D 0.33.0) and the six drums added for them. A pack made here, named as the owner's multi-mic
// library names its files ("<PREFIX>_<PIECE>[_<ARTICULATION>][_<MIC>]_V<layer>_RR<round>.wav"; the sounds invented:
// each mic a sine of its own pitch, each layer louder, each round-robin a few hertz apart), read and played through
// the mixer: every sound on its drum, every mic on its channel, layers and rounds as named, streamed past its head.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/NoteMap.h"
#include "dsp/synth/SamplePack.h"
#include "rigs/Rig.h"

namespace
{
    using namespace synth::drums::notemap;
    namespace pack = synth::drums::samplepack;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-pack-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    // A mic's pitch, so the channels can be told apart.
    double hzOf (const juce::String& mic)
    {
        if (mic == "DYN_KICK" || mic == "DYN1" || mic == "SPOT" || mic == "DYN" || mic.isEmpty()) return 400.0;
        if (mic == "CON_KICK" || mic == "BTM") return 600.0;
        if (mic == "SUB_KICK") return 800.0;
        if (mic == "TRIGGER" || mic == "DYN2") return 1000.0;
        if (mic.startsWith ("OH")) return 1300.0;
        return 1700.0;   // the room
    }

    void writeHit (const juce::File& file, double hz, float level, double seconds = 1.0)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (1, (int) (seconds * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    // The pack: a top folder, its INDIVIDUAL MICS (read) and ONE-SHOTS (left alone), as the owner's is laid out.
    juce::File makePack (const juce::File& at)
    {
        const auto top = at.getChildFile ("TEST DJENT PACK");
        const auto mics = top.getChildFile ("WAV & ONE-SHOTS/INDIVIDUAL MICS");
        const auto add = [&] (const juce::String& folder, const juce::String& sound, const juce::StringArray& micNames, int layers, int rounds)
        {
            for (const auto& mic : micNames)
                for (int v = 1; v <= layers; ++v)
                    for (int r = 1; r <= rounds; ++r)
                        writeHit (mics.getChildFile (folder).getChildFile ("TDP_DJENT_" + sound + (mic.isEmpty() ? "" : "_" + mic)
                                                                           + "_V0" + juce::String (v) + "_RR" + juce::String (r) + ".wav"),
                                  hzOf (mic) + 5.0 * (r - 1), 0.1f * (float) v);
        };
        add ("SHELLS/KICK", "KICK", { "DYN_KICK", "CON_KICK", "SUB_KICK", "TRIGGER", "OH_KICK", "RM_KICK" }, 3, 2);
        add ("SHELLS/SNARE", "SNR", { "DYN1", "DYN2", "BTM", "OH", "RM" }, 2, 2);
        add ("SHELLS/CROSS STICK", "SNR_CROSS", { "DYN1", "OH" }, 1, 1);
        add ("SHELLS/TOM 4", "TOM4", { "DYN", "OH", "RM" }, 1, 1);
        add ("CYMBALS", "HATS_CLOSED", { "", "OH", "RM_CLOSE" }, 1, 1);
        add ("CYMBALS", "HATS_SHOULDER", { "", "OH" }, 1, 1);
        add ("CYMBALS", "CRASH1", { "SPOT", "OH" }, 1, 1);
        add ("CYMBALS", "CRASH1_CHOKE", { "SPOT", "OH" }, 1, 1);
        // Left alone: a pre-mixed one-shot, a stray file of the operating system's, a name without layer and round.
        writeHit (top.getChildFile ("WAV & ONE-SHOTS/ONE-SHOTS/SAMPLES/01 MIX/TDP_DJENT_01_MIX_KICK_V01_RR1.wav"), 100.0, 0.5f);
        mics.getChildFile ("SHELLS/.DS_Store").replaceWithText ("x");
        writeHit (mics.getChildFile ("SHELLS/KICK/TDP_DJENT_KICK_README.wav"), 100.0, 0.5f);
        return top;
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        TempFolder temp;
        Plugin()
        {
            processor->setNonRealtime (true);   // the stream read there and then (as a bounce): exact
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (temp.folder.getChildFile ("Presets"));
            testkit::set (*processor, ParamIDs::drumsMap, (float) (int) Layout::root);
        }
        float get (const juce::String& id) const
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor->getParameters().getParameter (id));
            return p->convertFrom0to1 (p->getValue());
        }
        std::vector<float> play (int slot, int velocity, double seconds)
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
        void soloOnly (int channel)
        {
            for (int c = 0; c < synth::mixer::numMicChannels; ++c)
                testkit::set (*processor, ParamIDs::mix (c, "Solo"), c == channel ? 1.0f : 0.0f);
        }
    };
}

TEST_CASE ("SAMPLE PACK: the names read as drums, mics and channels by our rules", "[samplepack]")
{
    using mixer = synth::mixer::Channel;
    const auto slot = [] (const char* words) { return pack::slotFor (juce::StringArray::fromTokens (words, " ", {})); };
    CHECK (slot ("KICK") == BD);
    CHECK (slot ("SNR") == SD);
    CHECK (slot ("SNR CROSS") == RIM);
    CHECK (slot ("TOM1") == HT);
    CHECK (slot ("TOM2") == MT);
    CHECK (slot ("TOM3") == LT);
    CHECK (slot ("TOM4") == T4);
    CHECK (slot ("HATS CLOSED") == CH);
    CHECK (slot ("HATS OPEN HALF") == HH);
    CHECK (slot ("HATS OPEN") == OH);
    CHECK (slot ("HATS PEDAL") == PH);
    CHECK (slot ("HATS SHOULDER") == HSH);
    CHECK (slot ("RIDE TIP") == RD);
    CHECK (slot ("RIDE BELL") == RB);
    CHECK (slot ("RIDE CRASH") == RE);
    CHECK (slot ("CRASH1") == CY);
    CHECK (slot ("CRASH1 CHOKE") == C1C);
    CHECK (slot ("CRASH2") == CR2);
    CHECK (slot ("CRASH2 CHOKE") == C2C);
    CHECK (slot ("CHINA") == CHN);
    CHECK (slot ("CHINA CHOKE") == CHC);
    CHECK (slot ("SPLASH") == SPL);
    CHECK (slot ("SPLASH CHOKE") == SPC);
    CHECK (slot ("COWBELL") == -1);

    CHECK (pack::channelFor ("DYN", BD) == mixer::kickIn);
    CHECK (pack::channelFor ("CON", BD) == mixer::kickOut);
    CHECK (pack::channelFor ("SUB", BD) == -1);          // a USER channel of its own
    CHECK (pack::channelFor ("DYN1", SD) == mixer::snareTop);
    CHECK (pack::channelFor ("DYN2", SD) == -1);
    CHECK (pack::channelFor ("BTM", RIM) == mixer::snareBottom);
    CHECK (pack::channelFor ("DYN", LT) == mixer::tom3);
    CHECK (pack::channelFor ("DYN", T4) == -1);          // no TOM 4 channel: its own
    CHECK (pack::channelFor ("", CH) == mixer::hiHat);   // a hat's close mic, named by no word
    CHECK (pack::channelFor ("SPOT", CY) == mixer::cymbals);
    CHECK (pack::channelFor ("OH", CY) == mixer::overheads);
    CHECK (pack::channelFor ("RM CLOSE", CY) == mixer::room);
    CHECK (pack::channelFor ("RM", BD) == mixer::room);
}

TEST_CASE ("SAMPLE PACK: a library's folder scanned: its INDIVIDUAL MICS, every sound on its drum, extra mics on USER channels", "[samplepack]")
{
    TempFolder t;
    const auto info = pack::scan (makePack (t.folder));
    REQUIRE (info.valid());
    CHECK (info.name == "TEST DJENT PACK");
    CHECK (info.folder.getFileName() == "INDIVIDUAL MICS");   // its one-shots left alone
    for (const auto s : { BD, SD, RIM, T4, CH, HSH, CY, C1C })
        CHECK (info.slots.count (s) == 1);
    CHECK (info.slots.size() == 8);
    CHECK (info.slots.at (BD).size() == 6 * 3 * 2);   // 6 mics x 3 layers x 2 rounds
    // The extra mics, in name order: always the same channels for the same pack.
    CHECK (info.userChannels == juce::StringArray { "KICK SUB", "KICK TRIGGER", "SNARE TOP 2", "TOM 4" });
    CHECK (info.unused.contains ("TDP_DJENT_KICK_README"));
    std::set<int> kickChannels;
    for (const auto& f : info.slots.at (BD))
        kickChannels.insert (f.channel);
    CHECK (kickChannels == std::set<int> { synth::mixer::kickIn, synth::mixer::kickOut, synth::mixer::overheads, synth::mixer::room,
                                           synth::mixer::numChannels + 0, synth::mixer::numChannels + 1 });

    // Loaded: 3 layers (softest first) of 2 rounds, 6 mics a hit, streamed past its first 0.2 s.
    juce::StringArray problems;
    const auto kick = pack::load (info, BD, problems);
    INFO (problems.joinIntoString ("; "));
    REQUIRE (kick != nullptr);
    REQUIRE (kick->layers.size() == 3);
    CHECK (kick->layers[0].level < kick->layers[1].level);
    CHECK (kick->layers[1].level < kick->layers[2].level);
    CHECK (kick->layers[0].hits.size() == 2);
    CHECK (kick->layers[0].lo == 1);
    CHECK (kick->layers[2].hi == 127);
    CHECK (kick->layers[0].hits[0].mics.size() == 6);
    CHECK (kick->streamed());
    CHECK (problems.isEmpty());
}

TEST_CASE ("SAMPLE PACK: imported onto the kit and played: each mic on its channel, layers by velocity, rounds in turn, streamed", "[samplepack]")
{
    Plugin p;
    const auto top = makePack (p.temp.folder);
    const auto result = p.processor->importSamplePack (top);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (result.slots.size() == 8);
    CHECK (result.userChannels.size() == 4);
    CHECK (p.processor->getMixerChannelName (synth::mixer::numChannels + 0) == "KICK SUB");
    CHECK (p.get (ParamIDs::mix (synth::mixer::numChannels + 0, "Level")) == Catch::Approx (-6.0f).margin (0.01));   // a sub kick
    CHECK (p.get (ParamIDs::mix (synth::mixer::numChannels + 1, "Level")) == Catch::Approx (-12.0f).margin (0.01));  // a trigger
    CHECK (p.get (ParamIDs::mix (synth::mixer::numChannels + 2, "Level")) == Catch::Approx (0.0f).margin (0.01));    // snare top 2
    CHECK (p.processor->getUserChannelsInUse().size() == 4);
    CHECK (p.get (ParamIDs::drum (HO1, "On")) < 0.5f);   // what it has none of: off (their stand-ins play)
    CHECK (p.get (ParamIDs::drum (T4, "On")) > 0.5f);

    // Each mic in its channel: soloed, its own pitch only. KICK IN (the DYN mic's 400 Hz).
    p.soloOnly (synth::mixer::kickIn);
    auto out = p.play (BD, 127, 0.8);
    CHECK (levelAt (out, 400.0, 0, 4800) > 0.05);
    CHECK (levelAt (out, 600.0, 0, 4800) < 0.005);
    // Past the 0.2 s head (from disk, the stream reading the mic's own file): still there, as loud.
    CHECK (levelAt (out, 400.0, (int) (0.5 * sampleRate), 4800) == Catch::Approx (levelAt (out, 400.0, 0, 4800)).epsilon (0.1));
    // KICK SUB (USER 1): its 800 Hz, 6 dB down.
    p.soloOnly (synth::mixer::numChannels + 0);
    out = p.play (BD, 127, 0.3);
    CHECK (levelAt (out, 800.0, 0, 4800) > 0.02);
    CHECK (levelAt (out, 400.0, 0, 4800) < 0.005);
    // TOM 4's own mic: USER 4.
    p.soloOnly (synth::mixer::numChannels + 3);
    out = p.play (T4, 127, 0.3);
    CHECK (levelAt (out, 400.0, 0, 4800) > 0.02);

    // Velocity layers: a soft note the quiet layer (V01: a third of V03's level, as written).
    p.soloOnly (synth::mixer::kickIn);
    testkit::set (*p.processor, ParamIDs::drum (BD, "VelToVol"), 0.0f);   // the layers alone, no velocity scaling
    const auto hard = levelAt (p.play (BD, 127, 0.2), 400.0, 0, 4800);
    const auto soft = levelAt (p.play (BD, 20, 0.2), 400.0, 0, 4800);
    INFO ("hard " << hard << ", soft " << soft);
    CHECK (soft / hard == Catch::Approx (1.0 / 3.0).epsilon (0.15));

    // Round-robins: RR1's 400 Hz and RR2's 405 Hz in turn (never the same twice).
    testkit::set (*p.processor, ParamIDs::drum (BD, "Randomize"), 0.0f);
    const auto a = p.play (BD, 127, 0.4), b = p.play (BD, 127, 0.4);
    const auto isFirst = [] (const std::vector<float>& x) { return frequencyOf (x, 0, 16384) < 402.5; };
    CHECK (isFirst (a) != isFirst (b));
}

TEST_CASE ("SAMPLE PACK: a preset keeps it (where the pack is), and loads it again", "[samplepack]")
{
    Plugin p;
    const auto top = makePack (p.temp.folder);
    p.processor->importSamplePack (top);
    REQUIRE (p.processor->waitForInstruments());
    const auto ref = p.processor->getInstrumentRef (BD);
    CHECK (ref.startsWith ("pack|0|{kit:TEST DJENT PACK}"));
    REQUIRE (p.processor->saveCurrentRigAs ("Pack Kit"));
    const RigLibrary::Entry* entry = nullptr;
    for (const auto& e : p.processor->getRigLibrary().getEntries())
        if (e.rig.name == "Pack Kit")
            entry = &e;
    REQUIRE (entry != nullptr);
    CHECK (entry->rig.drumInstruments.at (BD) == ref);
    p.processor->loadRig (*p.processor->getRigLibrary().getEntries().begin());   // something else first
    p.processor->loadRig (*entry);
    REQUIRE (p.processor->waitForInstruments());
    REQUIRE (p.processor->getDrumInstrument (BD) != nullptr);
    CHECK (p.processor->getDrumInstrument (BD)->layers.size() == 3);
    CHECK (p.processor->getMixerChannelName (synth::mixer::numChannels + 1) == "KICK TRIGGER");
}

TEST_CASE ("0.33.0's DRUMS: tom 4, the hat's shoulder, the chokes; a choke stops its cymbal; older files load as they were", "[samplepack][articulations]")
{
    using namespace synth::drums;
    CHECK (numSlots == 29);
    CHECK (articulationName (T4) == "TOM 4");
    CHECK (articulationName (HSH) == "HI-HAT SHOULDER");
    CHECK (articulationName (C1C) == "CRASH 1 CHOKE");
    CHECK (articulationName (SPC) == "SPLASH CHOKE");
    CHECK (isChoke (C1C));
    CHECK_FALSE (isChoke (CY));
    CHECK (generalMidi()[41] == T4);
    CHECK (grooves::laneOf (T4) == (int) grooves::Lane::toms);
    CHECK (grooves::laneOf (C2C) == (int) grooves::Lane::crash);

    // A crash ringing, then its choke: the crash stops (within 30 ms), the choke plays.
    Plugin p;
    testkit::useSample (*p.processor, CY, testkit::sine (500.0, 2.0, 0.25f));
    testkit::useSample (*p.processor, C1C, testkit::sine (2000.0, 0.1, 0.25f));
    p.processor->prepareToPlay (sampleRate, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (CY), (juce::uint8) 127), 0);
    midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (C1C), (juce::uint8) 127), (int) (0.25 * sampleRate));
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (p.processor->getTotalNumOutputChannels(), blockSize);
    for (int b = 0; b < (int) (0.6 * sampleRate / blockSize); ++b)
    {
        juce::MidiBuffer now;
        for (const auto m : midi)
            if (m.samplePosition >= b * blockSize && m.samplePosition < (b + 1) * blockSize)
                now.addEvent (m.getMessage(), m.samplePosition - b * blockSize);
        buffer.clear();
        p.processor->processBlock (buffer, now);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    CHECK (levelAt (out, 500.0, (int) (0.1 * sampleRate), 4800) > 0.05);   // ringing before
    CHECK (levelAt (out, 500.0, (int) (0.4 * sampleRate), 4800) < 1.0e-3); // stopped by its choke
    CHECK (levelAt (out, 2000.0, (int) (0.26 * sampleRate), 2400) > 0.02); // the choke's own sound

    // A preset from before 0.33.0: the new drums off; a layer saved as instrument 23 + slot is that drum's layer now.
    auto xml = Rig {}.toXml();
    xml->setAttribute ("parameterVersion", 2);
    auto* layerRef = xml->createNewChildElement ("DrumInstrument");
    layerRef->setAttribute ("slot", 23 + 0 + 1);   // the kick's layer, as 0.29.0-0.32.0 saved it
    layerRef->setAttribute ("ref", "file||/my/kick.wav");
    const auto old = Rig::fromXml (*xml);
    REQUIRE (old.has_value());
    CHECK (old->drumInstruments.count (ParagonX1AudioProcessor::layerOf (BD)) == 1);
    CHECK (old->drumInstruments.count (23) == 0);
    for (const auto& id : ParamIDs::slotsAdded033Off)
        CHECK (old->parameters.at (id) == 0.0f);
}
