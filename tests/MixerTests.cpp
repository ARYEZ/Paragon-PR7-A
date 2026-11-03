// THE MIXER (PR7-D 0.20.0): mic names sorted into channels, multi-mic kits (SFZ and folders) keeping each mic apart,
// and what the mixer does to the sound: each channel's fader, mute, solo, polarity, pan and output; bleed (the
// channel's and each piece's); time offset to the sample; release; synth drums left alone; presets keeping it.
// Each test mic file is a sine at its own pitch, so each channel can be measured in the output.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/Instrument.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/NoteMap.h"
#include "rigs/Rig.h"
#include "ui/MixerPage.h"

namespace
{
    using namespace synth;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-mixer-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    void writeSine (const juce::File& file, double hz, float level, double seconds = 1.0)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (1, (int) (seconds * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    // A kit: the kick (36) through KICK IN (100 Hz) and the overheads (1000 Hz); the snare (38) through SNARE TOP
    // (300 Hz), KICK IN (bleed: 500 Hz) and the room (2000 Hz). Each at 0.25.
    juce::File writeKit (const juce::File& folder)
    {
        writeSine (folder.getChildFile ("kick_in.wav"), 100.0, 0.25f);
        writeSine (folder.getChildFile ("kick_oh.wav"), 1000.0, 0.25f);
        writeSine (folder.getChildFile ("snare_top.wav"), 300.0, 0.25f);
        writeSine (folder.getChildFile ("snare_kickin.wav"), 500.0, 0.25f);
        writeSine (folder.getChildFile ("snare_room.wav"), 2000.0, 0.25f);
        const auto sfz = folder.getChildFile ("kit.sfz");
        sfz.replaceWithText ("<group> key=36\n"
                             "<region> sample=kick_in.wav group_label=Kick In\n"
                             "<region> sample=kick_oh.wav group_label=OH\n"
                             "<group> key=38\n"
                             "<region> sample=snare_top.wav group_label=Snare Top\n"
                             "<region> sample=snare_kickin.wav group_label=Kick In\n"
                             "<region> sample=snare_room.wav group_label=Room\n");
        return sfz;
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        explicit Plugin (const juce::File& presets, bool out2 = false)
        {
            auto layout = processor->getBusesLayout();
            if (out2)
                layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            REQUIRE (processor->setBusesLayout (layout));
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (presets);
        }
        void set (const juce::String& id, float value) { testkit::set (*processor, id, value); }
        // Every output channel, `seconds` of it, `note` hit at velocity 127 at the start.
        std::vector<std::vector<float>> play (int note, double seconds = 0.3)
        {
            processor->prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 127), 0);
            const auto channels = processor->getTotalNumOutputChannels();
            std::vector<std::vector<float>> out ((size_t) channels);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor->processBlock (buffer, midi);
                midi.clear();
                for (int c = 0; c < channels; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
        // A pitch's level in MAIN's left channel, over 0.05 to 0.25 s.
        double level (int note, double hz)
        {
            const auto out = play (note);
            return levelAt (out[0], hz, (size_t) (0.05 * sampleRate), (size_t) (0.2 * sampleRate));
        }
    };
}

TEST_CASE ("MIXER: mic names into channels", "[mixer]")
{
    using namespace synth::mixer;
    CHECK (channelForName ("Kick In") == kickIn);
    CHECK (channelForName ("kick_out") == kickOut);
    CHECK (channelForName ("Snare Top") == snareTop);
    CHECK (channelForName ("SnareBottom") == snareBottom);
    CHECK (channelForName ("Snare Btm") == snareBottom);
    CHECK (channelForName ("HH") == hiHat);
    CHECK (channelForName ("Tom 1") == tom1);
    CHECK (channelForName ("Floor Tom") == tom2);
    CHECK (channelForName ("OH") == overheads);
    CHECK (channelForName ("Overheads") == overheads);
    CHECK (channelForName ("Room") == room);
    CHECK (channelForName ("Room Kick") == room);   // a room mic, though it says kick
    CHECK (channelForName ("Room Far") == ambience);
    CHECK (channelForName ("Samples/OH/snare_v3.wav") == overheads);
    CHECK (channelForName ("misc") == -1);
    CHECK (numPieces == drums::numPieces);
    // A drum's own channels (direct sound) and the ones it bleeds into.
    CHECK (drums::homeChannel (drums::notemap::SD) == snareTop);
    CHECK (drums::isDirect (drums::notemap::SD, snareBottom));
    CHECK (drums::isDirect (drums::notemap::SD, overheads));
    CHECK_FALSE (drums::isDirect (drums::notemap::SD, kickIn));
    CHECK (drums::homeChannel (drums::notemap::RD) == cymbals);
}

TEST_CASE ("MIXER: a multi-mic SFZ kit and a folder of mic folders keep each mic apart; a single-mic one doesn't", "[mixer]")
{
    TempFolder temp;
    const auto sfz = drums::instruments::fromSfz (writeKit (temp.folder));
    REQUIRE (sfz.notes.size() == 2);
    const auto& snare = *sfz.notes.at (38);
    REQUIRE (snare.hasMics());
    CHECK (snare.micChannels() == juce::Array<int> { mixer::kickIn, mixer::snareTop, mixer::room });
    CHECK (snare.layers[0].hits[0].mics.size() == 3);
    // Its summed hit is still there (pictures, as before).
    CHECK (snare.layers[0].hits[0].sample != nullptr);

    // A folder with a folder per mic: each hit by its name in each.
    const auto folder = temp.folder.getChildFile ("Tom");
    for (const auto* mic : { "Tom 1", "OH", "Room" })
        for (int i = 1; i <= 2; ++i)
            writeSine (folder.getChildFile (mic).getChildFile ("tom " + juce::String (i) + ".wav"), 200.0 * i, 0.2f * (float) i, 0.2);
    const auto tom = drums::instruments::fromFolder (folder);
    REQUIRE (tom != nullptr);
    CHECK (tom->numHits() == 2);
    CHECK (tom->micChannels() == juce::Array<int> { mixer::tom1, mixer::overheads, mixer::room });

    // One mic: plays as before (no channels).
    temp.folder.getChildFile ("single.sfz").replaceWithText ("<region> sample=kick_in.wav key=36\n");
    CHECK_FALSE (drums::instruments::fromSfz (temp.folder.getChildFile ("single.sfz")).notes.at (36)->hasMics());
}

TEST_CASE ("MIXER: faders, mute, solo, polarity, bleed, time offset, release, output; synth drums left alone", "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    p.set (ParamIDs::drum (0, "VelToVol"), 0.0f);

    // Each mic's sine at its level (0.25 x the kit's -6 dB... measured, then compared).
    const auto top = p.level (38, 300.0), bleed = p.level (38, 500.0), room = p.level (38, 2000.0);
    REQUIRE (top > 0.05);
    CHECK (bleed == Catch::Approx (top).epsilon (0.02));
    CHECK (room == Catch::Approx (top).epsilon (0.02));

    // A fader 6 dB down: that mic alone.
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), 0.0f);

    // MUTE the room; SOLO the snare top (only it heard).
    p.set (ParamIDs::mix (mixer::room, "Mute"), 1.0f);
    CHECK (p.level (38, 2000.0) < room * 0.001);
    p.set (ParamIDs::mix (mixer::room, "Mute"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (38, 2000.0) < room * 0.001);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // BLEED: the snare in the kick mic is bleed. The channel's BLEED off, or -12 dB, takes it out or down; the snare's
    // own sound in its own mic (direct) isn't touched. The kick's own sound in KICK IN isn't bleed either.
    p.set (ParamIDs::mix (mixer::kickIn, "BleedOn"), 0.0f);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (36, 100.0) > 0.05);
    p.set (ParamIDs::mix (mixer::kickIn, "BleedOn"), 1.0f);
    p.set (ParamIDs::mix (mixer::kickIn, "Bleed"), -12.0f);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.251).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::kickIn, "Bleed"), 0.0f);
    // BLEED FROM INSTRUMENTS: the snare's own level in KICK IN (-6 dB), and its polarity.
    auto m = p.processor->getModMatrix();
    const auto snarePiece = drums::pieceOfSlot (drums::notemap::SD);
    m.bleed (mixer::kickIn, snarePiece).gainDb = -6.0f;
    p.processor->setModMatrix (m);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.501).epsilon (0.02));
    m.bleed (mixer::kickIn, snarePiece) = { 0.0f, true, true };
    p.processor->setModMatrix (m);
    const auto normal = [&]
    {
        m.bleed (mixer::kickIn, snarePiece).invert = false;
        p.processor->setModMatrix (m);
        return p.play (38)[0];
    }();
    m.bleed (mixer::kickIn, snarePiece).invert = true;
    p.processor->setModMatrix (m);
    const auto flipped = p.play (38)[0];
    // Flipping that one mic's snare: the sum differs by twice that mic's 500 Hz.
    CHECK (levelAt (normal, 500.0, 2400, 9600) == Catch::Approx (levelAt (flipped, 500.0, 2400, 9600)).epsilon (0.02));
    CHECK (levelAt (normal, 300.0, 2400, 9600) == Catch::Approx (levelAt (flipped, 300.0, 2400, 9600)).epsilon (0.02));
    double difference = 0.0;
    for (size_t i = 2400; i < 9600; ++i)
        difference = std::max (difference, (double) std::abs (normal[i] - flipped[i]));
    CHECK (difference > 0.1);
    p.processor->setModMatrix ({});

    // Polarity of a whole channel: the snare top's sine upside down.
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    const auto up = p.play (38)[0];
    p.set (ParamIDs::mix (mixer::snareTop, "Phase"), 1.0f);
    const auto down = p.play (38)[0];
    for (size_t i = 100; i < 4000; i += 97)
        CHECK (down[i] == Catch::Approx (-up[i]).margin (1.0e-5));
    p.set (ParamIDs::mix (mixer::snareTop, "Phase"), 0.0f);

    // TIME OFFSET 5 ms: the channel's first sample 240 samples later.
    const auto firstSound = [] (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                return (int) i;
        return -1;
    };
    const auto at = firstSound (up);
    INFO ("first sound " << at << ", rms " << rms (up, 0, 4000));
    REQUIRE (at >= 0);
    p.set (ParamIDs::mix (mixer::snareTop, "Offset"), 5.0f);
    CHECK (firstSound (p.play (38)[0]) - at == 240);
    p.set (ParamIDs::mix (mixer::snareTop, "Offset"), 0.0f);

    // RELEASE 50 ms: the channel quiet by 100 ms (the fade takes 30), the sine still going without it.
    p.set (ParamIDs::mix (mixer::snareTop, "Release"), 50.0f);
    const auto released = p.play (38)[0];
    CHECK (rms (released, (size_t) (0.1 * sampleRate), (size_t) (0.2 * sampleRate)) < 1.0e-4);
    CHECK (rms (released, 500, 1500) > 0.02);
    p.set (ParamIDs::mix (mixer::snareTop, "Release"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // OUTPUT: the room to OUT 2 (on in the DAW): there, not in MAIN.
    p.set (ParamIDs::mix (mixer::room, "Output"), 1.0f);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    INFO ("channels " << out.size() << " out2 " << out2 << " total " << p.processor->getTotalNumOutputChannels());
    REQUIRE (out2 >= 0);
    REQUIRE (out2 < (int) out.size());
    CHECK (levelAt (out[(size_t) out2], 2000.0, 2400, 9600) == Catch::Approx (room).epsilon (0.05));
    CHECK (levelAt (out[0], 2000.0, 2400, 9600) < room * 0.001);
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::mix (mixer::room, "Output")));
    p.set (ParamIDs::mix (mixer::room, "Output"), 0.0f);

    // A synth drum (the clap) doesn't go through the mixer: every channel muted, it still plays.
    for (int c = 0; c < mixer::numChannels; ++c)
        p.set (ParamIDs::mix (c, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < 1.0e-4);
    auto clapPeak = 0.0f;
    const auto clap = p.play (39);
    for (const auto x : clap[0])
        clapPeak = std::max (clapPeak, std::abs (x));
    CHECK (clapPeak > 0.05f);
}

TEST_CASE ("MIXER: VOICES counts hits, not mics; presets keep the channels and the bleed; the page", "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);

    // VOICES 2 on the snare: five quick hits of three mics each, at most two hits (six voices) sounding.
    p.set (ParamIDs::drum (2, "Voices"), 2.0f);
    juce::MidiBuffer midi;
    for (int i = 0; i < 5; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), i * 30);
    juce::AudioBuffer<float> buffer (2, blockSize);
    p.processor->processBlock (buffer, midi);
    juce::MidiBuffer none;
    for (int b = 0; b < 3; ++b)
        p.processor->processBlock (buffer, none);
    CHECK (p.processor->getDrumKit().getNumActiveVoices() <= 6);
    CHECK (p.processor->getDrumKit().getNumActiveVoices() >= 4);

    // The kit's 48-hit limit counts hits too: 20 snare hits of three mics each are 60 voices.
    p.set (ParamIDs::drum (2, "Voices"), 48.0f);
    for (int b = 0; b < (int) (1.5 * sampleRate / blockSize); ++b)   // the earlier hits rung out (1 s files)
        p.processor->processBlock (buffer, none);
    REQUIRE (p.processor->getDrumKit().getNumActiveVoices() == 0);
    midi.clear();
    for (int i = 0; i < 20; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), i * 10);
    p.processor->processBlock (buffer, midi);
    midi.clear();
    CHECK (p.processor->getDrumKit().getNumActiveVoices() == 60);

    // A preset keeps the channels' settings and the bleed table.
    p.set (ParamIDs::mix (mixer::overheads, "Level"), -3.0f);
    auto m = p.processor->getModMatrix();
    m.bleed (mixer::kickIn, 1) = { -9.0f, true, false };
    p.processor->setModMatrix (m);
    const auto rig = Rig::fromXml (*p.processor->captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->parameters.at (ParamIDs::mix (mixer::overheads, "Level")) == Catch::Approx (-3.0f));
    CHECK (rig->modulation.bleed (mixer::kickIn, 1) == mixer::BleedCell { -9.0f, true, false });

    // The page: the channels the kit plays into; a bleed row's change (one undo step).
    MixerPage page (*p.processor);
    page.setSize (1400, 650);
    CHECK (page.getChannelsInUse() == juce::Array<int> { mixer::kickIn, mixer::snareTop, mixer::overheads, mixer::room });
    page.selectChannel (mixer::room);
    page.setBleed (0, -4.0f, false, true);
    CHECK (p.processor->getModMatrix().bleed (mixer::room, 0).gainDb == Catch::Approx (-4.0f));
}

namespace
{
    // A send as the page would set it.
    void setSend (Plugin& p, int strip, int slot, const mixer::Send& send)
    {
        auto m = p.processor->getModMatrix();
        m.send (strip, slot) = send;
        p.processor->setModMatrix (m);
    }
    mixer::Send sendTo (int target, float directDb = 0.0f, mixer::SendMode mode = mixer::SendMode::postFader)
    {
        mixer::Send s;
        s.target = target;
        s.directDb = directDb;
        s.bleedDb = directDb;
        s.mode = mode;
        return s;
    }
    constexpr int busStrip (int bus) { return mixer::numChannels + bus; }
}

TEST_CASE ("MIXER BUSES: a channel routed into a bus, the bus's fader, mute, polarity and output; bus into a later bus only",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    const auto top = p.level (38, 300.0), room = p.level (38, 2000.0);
    REQUIRE (top > 0.05);

    // The snare top into BUS 1, at 0 dB: the same in MAIN.
    p.processor->setMixerRoute (mixer::snareTop, 1);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 1);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    // The bus 6 dB down: the snare top alone.
    p.set (ParamIDs::mixBus (0, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    // Its pan hard left: nothing on the right.
    p.set (ParamIDs::mixBus (0, "Level"), 0.0f);
    p.set (ParamIDs::mixBus (0, "Pan"), -1.0f);
    {
        const auto out = p.play (38);
        CHECK (levelAt (out[1], 300.0, 2400, 9600) < top * 0.001);
        CHECK (levelAt (out[0], 300.0, 2400, 9600) == Catch::Approx (top).epsilon (0.02));
    }
    p.set (ParamIDs::mixBus (0, "Pan"), 0.0f);
    // MUTE the bus: the snare top gone, the room still there.
    p.set (ParamIDs::mixBus (0, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mixBus (0, "Mute"), 0.0f);
    // Polarity: the bus upside down (the snare top soloed, so only it).
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    const auto up = p.play (38)[0];
    p.set (ParamIDs::mixBus (0, "Phase"), 1.0f);
    const auto down = p.play (38)[0];
    for (size_t i = 100; i < 4000; i += 97)
        CHECK (down[i] == Catch::Approx (-up[i]).margin (1.0e-5));
    p.set (ParamIDs::mixBus (0, "Phase"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // BUS 1 into BUS 2, and BUS 2 down 6 dB: the snare top down 6 dB.
    p.processor->setMixerRoute (busStrip (0), 2);
    p.set (ParamIDs::mixBus (1, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    // BUS 2 can't go back into BUS 1 (it would loop): refused.
    p.processor->setMixerRoute (busStrip (1), 1);
    CHECK (p.processor->getMixerRoute (busStrip (1)) == 0);
    // ... nor if it's set some other way (an old preset): the bus plays to its output instead.
    p.set (ParamIDs::mixBus (1, "Route"), 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    p.set (ParamIDs::mixBus (1, "Route"), 0.0f);

    // BUS 2's OUTPUT to OUT 2: the snare top there, not in MAIN.
    p.processor->setMixerOutput (busStrip (1), 1);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    REQUIRE (out2 >= 0);
    CHECK (levelAt (out[(size_t) out2], 300.0, 2400, 9600) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (levelAt (out[0], 300.0, 2400, 9600) < top * 0.001);
    CHECK (levelAt (out[0], 2000.0, 2400, 9600) == Catch::Approx (room).epsilon (0.02));
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::mixBus (1, "Output")));
    CHECK (! ParamIDs::notInRigs.contains (ParamIDs::mixBus (1, "Route")));
    CHECK (! ParamIDs::notInRigs.contains (ParamIDs::mix (0, "Route")));
    // Choosing an output takes the channel off its bus.
    p.processor->setMixerOutput (mixer::snareTop, 0);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 0);

    // SOLO among the buses: BUS 3 soloed silences BUS 1's snare top; the channels straight to MAIN still play.
    p.processor->setMixerRoute (mixer::snareTop, 1);
    p.processor->setMixerRoute (busStrip (0), 0);
    p.set (ParamIDs::mixBus (2, "Solo"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
}

TEST_CASE ("MIXER SENDS: pre / post fader, direct and bleed apart with their own pans, MUTE MODE, into DELAY, REVERB and an FX bus",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    const auto top = p.level (38, 300.0), bleed = p.level (38, 500.0);
    REQUIRE (top > 0.05);

    // The snare top sent at -6 dB into BUS 1 (to MAIN): the sine 1.5 times as loud (in phase).
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f));
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 1.501).epsilon (0.02));
    // POST FADER: the channel's fader down to -inf takes the send with it; PRE FADER: the send alone.
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), -60.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f, mixer::SendMode::preFader));
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), 0.0f);

    // MUTE MODE. PRE SEND (as before): a muted channel's sends are silent too.
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    // POST SEND: its pre-fader send goes on (the snare's "reverb alone"); a post-fader one doesn't.
    p.set (ParamIDs::mixMuteMode, 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f));
    CHECK (p.level (38, 300.0) < top * 0.001);
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 0.0f);
    p.set (ParamIDs::mixMuteMode, 0.0f);
    setSend (p, mixer::snareTop, 0, {});

    // DIRECT and BLEED: KICK IN carries the snare as bleed (500 Hz). Muted with POST SEND, a pre-fader send to BUS 1:
    // its bleed level, on and pan apart from its direct sound's.
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 1.0f);
    p.set (ParamIDs::mixMuteMode, 1.0f);
    auto send = sendTo (1, 0.0f, mixer::SendMode::preFader);
    send.bleedOn = false;
    setSend (p, mixer::kickIn, 0, send);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (36, 100.0) > 0.05);   // the kick's own sound in it, still sent
    send.bleedOn = true;
    send.bleedDb = -12.0f;
    setSend (p, mixer::kickIn, 0, send);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.251).epsilon (0.02));
    send.bleedDb = 0.0f;
    send.bleedPan = 1.0f;   // the bleed hard right
    setSend (p, mixer::kickIn, 0, send);
    {
        const auto out = p.play (38);
        CHECK (levelAt (out[0], 500.0, 2400, 9600) < bleed * 0.001);
        CHECK (levelAt (out[1], 500.0, 2400, 9600) == Catch::Approx (bleed).epsilon (0.02));
    }
    setSend (p, mixer::kickIn, 0, {});
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 0.0f);
    p.set (ParamIDs::mixMuteMode, 0.0f);

    // REVERB: the snare top sent there (the REVERB on): sound after the 1 s sine has stopped; without the send, none.
    p.set (ParamIDs::reverbOn, 1.0f);
    const auto tail = [&]
    {
        const auto out = p.play (38, 1.6);
        return rms (out[0], (size_t) (1.15 * sampleRate), (size_t) (1.5 * sampleRate));
    };
    const auto dry = tail();
    setSend (p, mixer::snareTop, 0, sendTo (mixer::reverbTarget));
    const auto wet = tail();
    INFO ("dry tail " << dry << ", wet tail " << wet);
    CHECK (dry < 1.0e-4);
    CHECK (wet > 1.0e-3);
    // ... and with the REVERB off, the send is silent (nothing to hear it).
    p.set (ParamIDs::reverbOn, 0.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    setSend (p, mixer::snareTop, 0, {});

    // An FX BUS: the snare top routed into FX bus A, whose OUTPUT is OUT 2: the snare top there.
    p.set (ParamIDs::ins (0, "Output"), 1.0f);
    p.processor->setMixerRoute (mixer::snareTop, mixer::firstFxRoute);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    REQUIRE (out2 >= 0);
    CHECK (levelAt (out[(size_t) out2], 300.0, 2400, 9600) > top * 0.5);
    CHECK (levelAt (out[0], 300.0, 2400, 9600) < top * 0.001);
    // A send into FX bus A as well as the channel to MAIN: in both.
    p.processor->setMixerRoute (mixer::snareTop, 0);
    setSend (p, mixer::snareTop, 1, sendTo (mixer::firstFxRoute));
    const auto both = p.play (38);
    CHECK (levelAt (both[(size_t) out2], 300.0, 2400, 9600) > top * 0.5);
    CHECK (levelAt (both[0], 300.0, 2400, 9600) == Catch::Approx (top).epsilon (0.02));
}

TEST_CASE ("MIXER MENU: STEREO REVERSE, STEREO and MULTICHANNEL OUTPUTS; presets keep the sends, routes and mute mode, not outputs",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    auto& state = p.processor->getParameters();
    const auto value = [&state] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };

    p.set (ParamIDs::mix (mixer::hiHat, "Pan"), -0.4f);
    p.set (ParamIDs::mixBus (3, "Pan"), 0.7f);
    auto send = sendTo (2);
    send.directPan = 0.5f;
    send.bleedPan = -0.25f;
    setSend (p, mixer::room, 1, send);
    p.processor->mixerStereoReverse();
    CHECK (value (ParamIDs::mix (mixer::hiHat, "Pan")) == Catch::Approx (0.4f));
    CHECK (value (ParamIDs::mixBus (3, "Pan")) == Catch::Approx (-0.7f));
    CHECK (value (ParamIDs::mix (mixer::kickIn, "Pan")) == Catch::Approx (0.0f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).directPan == Catch::Approx (-0.5f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).bleedPan == Catch::Approx (0.25f));

    // MULTICHANNEL: by instrument; STEREO: everything to MAIN, off the buses.
    p.processor->setMixerRoute (mixer::tom2, 3);
    p.processor->applyMixerOutputs (true);
    CHECK (p.processor->getMixerOutput (mixer::kickOut) == 1);
    CHECK (p.processor->getMixerOutput (mixer::snareBottom) == 2);
    CHECK (p.processor->getMixerOutput (mixer::tom2) == 4);
    CHECK (p.processor->getMixerRoute (mixer::tom2) == 0);
    CHECK (p.processor->getMixerOutput (mixer::ambience) == 7);
    CHECK (p.processor->getMixerOutput (busStrip (0)) == 0);
    p.processor->applyMixerOutputs (false);
    for (int strip = 0; strip < mixer::numStrips; ++strip)
        CHECK (p.processor->getMixerOutput (strip) == 0);

    // A preset: routes, mute mode and sends kept; outputs left with the project.
    p.processor->setMixerRoute (mixer::snareTop, 4);
    p.processor->setMixerRoute (busStrip (3), 6);
    p.set (ParamIDs::mixMuteMode, 1.0f);
    p.processor->setMixerOutput (busStrip (5), 3);
    RigLibrary::Entry entry;
    entry.rig = *Rig::fromXml (*p.processor->captureRig().toXml());
    p.processor->setMixerRoute (mixer::snareTop, 0);
    p.processor->setMixerRoute (busStrip (3), 0);
    p.set (ParamIDs::mixMuteMode, 0.0f);
    setSend (p, mixer::room, 1, {});
    p.processor->setMixerOutput (busStrip (5), 5);
    p.processor->loadRig (entry);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 4);
    CHECK (p.processor->getMixerRoute (busStrip (3)) == 6);
    CHECK (value (ParamIDs::mixMuteMode) == Catch::Approx (1.0f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).target == 2);
    CHECK (p.processor->getMixerOutput (busStrip (5)) == 5);
    // The sends as XML and back.
    const auto xml = p.processor->getModMatrix().toXml();
    CHECK (ModMatrix::fromXml (*xml) == p.processor->getModMatrix());
}

TEST_CASE ("MIXER page 0.21.0: bus strips, MICS / BUSES, the OUTPUT and SEND buttons' words, a send set from its editor, MENU...",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    MixerPage page (*p.processor);
    page.setSize (1400, 650);

    // Every channel and bus shown; MICS off leaves the buses (the chosen channel gives way to the first bus).
    for (int strip = 0; strip < mixer::numStrips; ++strip)
        CHECK (page.isStripShown (strip) == (strip < mixer::firstUserStrip));   // the user channels only once used (0.23.0)
    page.selectChannel (mixer::snareTop);
    page.showGroups (false, true);
    CHECK (! page.isStripShown (mixer::snareTop));
    CHECK (page.isStripShown (busStrip (0)));
    CHECK (page.getSelectedStrip() == busStrip (0));
    CHECK (page.getTab() == MixerPage::Tab::effects);   // a bus: no BLEED tab (0.22.0: its effects)
    page.showGroups (true, true);

    // OUTPUT's words: the output, or the bus / FX bus it's routed into.
    CHECK (page.describeOutput (mixer::room) == "Main");
    p.processor->setMixerRoute (mixer::room, 3);
    CHECK (page.describeOutput (mixer::room) == "TOMS");   // 0.39.0: the bus by its name (BUS 3 is the drum buses' TOMS)
    p.processor->setMixerRoute (mixer::room, 4);
    CHECK (page.describeOutput (mixer::room) == "BUS 4");
    p.processor->setMixerRoute (mixer::room, mixer::firstFxRoute + 1);
    CHECK (page.describeOutput (mixer::room) == "FX Bus B");
    p.processor->setMixerOutput (mixer::room, 4);
    CHECK (page.describeOutput (mixer::room) == "Out 5");

    // A send's target from its slot's menu: one undo step; a bus can't send into an earlier bus; OFF clears it.
    page.setSendTarget (mixer::overheads, 1, mixer::reverbTarget);
    CHECK (page.describeSend (mixer::overheads, 1) == "Reverb");
    CHECK (page.describeSend (mixer::overheads, 0) == "+ SEND");
    page.setSendTarget (busStrip (4), 0, 2);
    CHECK (! p.processor->getModMatrix().send (busStrip (4), 0).isOn());
    page.setSendTarget (busStrip (4), 0, 7);
    CHECK (p.processor->getModMatrix().send (busStrip (4), 0).target == 7);
    page.setSendTarget (busStrip (4), 0, 0);
    CHECK (p.processor->getModMatrix().send (busStrip (4), 0) == mixer::Send {});

    // The editor's send for the chosen channel.
    page.selectChannel (mixer::kickIn);
    page.showTab (MixerPage::Tab::sends);
    auto send = sendTo (mixer::firstFxRoute, -3.0f, mixer::SendMode::preFader);
    send.bleedOn = false;
    page.setSend (1, send);
    CHECK (p.processor->getModMatrix().send (mixer::kickIn, 1) == send);

    // MENU...: POST SEND, STEREO OUTPUTS, MULTICHANNEL OUTPUTS (with a note on the DAW's outputs).
    page.runMenuItem (MixerPage::postSend);
    CHECK (p.processor->getParameters().getRawParameterValue (ParamIDs::mixMuteMode)->load() == Catch::Approx (1.0f));
    page.runMenuItem (MixerPage::multichannelOutputs);
    CHECK (page.describeOutput (mixer::snareBottom) == "Out 3");
    CHECK (page.getNote().contains ("DAW"));
    page.runMenuItem (MixerPage::stereoOutputs);
    CHECK (page.describeOutput (mixer::snareBottom) == "Main");
    page.update();
}

//==============================================================================
// THE MIXER's EFFECTS (0.22.0)
#include "MixerEffects.h"
#include "dsp/effects/Mars.h"

namespace
{
    void setEffect (Plugin& p, int strip, int slot, const mixer::FxSlot& fx)
    {
        auto m = p.processor->getModMatrix();
        m.effect (strip, slot) = fx;
        p.processor->setModMatrix (m);
    }
    mixer::FxSlot effectOf (int type, std::initializer_list<std::pair<int, float>> values = {})
    {
        auto fx = mixerfx::defaultSlot (type);
        for (const auto& [index, value] : values)
            fx.values[(size_t) index] = value;
        return fx;
    }
    // The snare top alone (soloed) at 300 Hz, its level over 0.05 to 0.25 s.
    struct SnareTop
    {
        TempFolder temp;
        Plugin p { temp.folder.getChildFile ("Presets"), true };
        SnareTop()
        {
            p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
            REQUIRE (p.processor->waitForInstruments());
            p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
            p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
            p.set (ParamIDs::drum (0, "VelToVol"), 0.0f);
            p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
        }
        double level (double hz = 300.0) { return p.level (38, hz); }
    };
}

TEST_CASE ("MIXER EFFECTS: EQ (a bell, LOW CUT), COMPRESSOR (and MIX), OUTPUT, bypass; presets keep the slots", "[mixer][mixerfx]")
{
    SnareTop t;
    auto& p = t.p;
    const auto dry = t.level();
    REQUIRE (dry > 0.05);

    // EQ: MID 1 at 300 Hz, +6 dB: the sine twice as loud. LOW CUT at 1 kHz: well under half.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::eq, { { 3, 300.0f }, { 4, 6.0f } }));
    CHECK (p.processor->getMixerEffectType (mixer::snareTop, 0) == mixerfx::eq);
    CHECK (t.level() == Catch::Approx (dry * 1.995).epsilon (0.03));
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::eq, { { 0, 1000.0f } }));
    CHECK (t.level() < dry * 0.2);
    // Flat (the defaults): untouched.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::eq));
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.01));

    // OUTPUT -6 dB; bypassed (ON off): as if it weren't there.
    auto boosted = effectOf (mixerfx::eq, { { 3, 300.0f }, { 4, 6.0f } });
    boosted.outputDb = -6.0f;
    setEffect (p, mixer::snareTop, 0, boosted);
    CHECK (t.level() == Catch::Approx (dry * 1.995 * 0.501).epsilon (0.03));
    boosted.on = false;
    setEffect (p, mixer::snareTop, 0, boosted);
    t.level();   // (the bypass fades over 20 ms)
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.01));

    // COMPRESSOR: threshold -40 dB at 20:1 on a -12 dB sine: much quieter, its meter showing it; MIX 0: dry again.
    setEffect (p, mixer::snareTop, 0, {});
    setEffect (p, mixer::snareTop, 1, effectOf (mixerfx::compressor, { { 0, -40.0f }, { 1, 20.0f }, { 2, 1.0f } }));
    const auto squashed = t.level();
    INFO ("compressed " << squashed << " of " << dry);
    CHECK (squashed < dry * 0.3);
    CHECK (p.processor->getMixerEffectReduction (mixer::snareTop, 1) > 10.0f);
    setEffect (p, mixer::snareTop, 1, effectOf (mixerfx::compressor, { { 0, -40.0f }, { 1, 20.0f }, { 7, 0.0f } }));
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.01));

    // A preset: the slots saved and loaded; an unknown type (a later version's) left empty.
    setEffect (p, mixer::room, 3, effectOf (mixerfx::gate, { { 0, -30.0f } }));
    const auto xml = p.processor->getModMatrix().toXml();
    const auto back = ModMatrix::fromXml (*xml);
    CHECK (back == p.processor->getModMatrix());
    CHECK (back.effect (mixer::room, 3).values[0] == Catch::Approx (-30.0f));
    auto future = p.processor->getModMatrix();
    future.effect (mixer::room, 2).type = 99;
    p.processor->setModMatrix (future);
    CHECK (p.processor->getMixerEffectType (mixer::room, 2) == 0);
    CHECK (p.processor->getMixerEffectType (mixer::room, 3) == mixerfx::gate);
}

TEST_CASE ("MIXER EFFECTS: GATE (threshold, range, its side chain's filters), DRIVE's harmonics, a chorus's tail after the sound stops",
           "[mixer][mixerfx]")
{
    SnareTop t;
    auto& p = t.p;
    const auto dry = t.level();

    // The sine peaks at about -12 dB: a gate at -6 dB stays shut (80 dB down); at -30 dB it's open.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::gate, { { 0, -6.0f } }));
    CHECK (t.level() < dry * 0.001);
    CHECK (p.processor->getMixerEffectReduction (mixer::snareTop, 0) > 60.0f);
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::gate, { { 0, -30.0f } }));
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.01));
    // RANGE 12 dB: shut is only 12 dB down.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::gate, { { 0, -6.0f }, { 1, 12.0f } }));
    CHECK (t.level() == Catch::Approx (dry * 0.251).epsilon (0.03));
    // Its side chain high-passed at 2 kHz: it hardly hears the 300 Hz sine, and stays shut.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::gate, { { 0, -30.0f }, { 5, 2000.0f } }));
    CHECK (t.level() < dry * 0.001);

    // DRIVE: the sine's odd harmonics appear (900 Hz), none without it.
    setEffect (p, mixer::snareTop, 0, {});
    CHECK (t.level (900.0) < dry * 0.001);
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::drive, { { 1, 10.0f } }));
    CHECK (t.level (900.0) > dry * 0.01);

    // A chorus (MOD) on the channel: after the 1 s sine stops, its delay line still sounds (the channel runs on
    // for its effects' tails); without it, silence.
    setEffect (p, mixer::snareTop, 0, {});
    const auto after = [&]
    {
        const auto out = p.play (38, 1.2);
        return rms (out[0], (size_t) (1.0015 * sampleRate), (size_t) (1.006 * sampleRate));
    };
    CHECK (after() < 1.0e-5);
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::mod));
    CHECK (after() > 1.0e-3);
}

TEST_CASE ("MIXER EFFECTS 0.35.0: SATURATION (MARS): harmonics at an even level, MIX, bands by their crossovers, LEVEL, QUALITY, presets",
           "[mixer][mixerfx][saturation]")
{
    SnareTop t;
    auto& p = t.p;
    const auto dry = t.level();
    REQUIRE (dry > 0.05);
    REQUIRE (t.level (900.0) < dry * 0.001);
    using mixerfx::saturationBandsAt;
    const auto band = [] (int b, int what) { return saturationBandsAt + b * mixerfx::saturationPerBand + what; };   // what: 0 STYLE .. 4 LEVEL

    // The defaults (one band, warm tape, DRIVE 30%): harmonics, the sine's own level kept within 3 dB.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation));
    CHECK (p.processor->getMixerEffectType (mixer::snareTop, 0) == mixerfx::saturation);
    const auto warm = t.level (900.0);
    INFO ("900 Hz at 30%: " << warm << ", the sine " << t.level() << " of " << dry);
    CHECK (warm > dry * 0.001);
    CHECK (juce::Decibels::gainToDecibels (t.level() / dry) == Catch::Approx (0.0).margin (3.0));
    // DRIVE 100%: more harmonics, the level still kept (within 3 dB).
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { band (0, 1), 100.0f } }));
    const auto hot = t.level (900.0);
    INFO ("900 Hz at 100%: " << hot);
    CHECK (hot > warm * 2.0);
    CHECK (juce::Decibels::gainToDecibels (t.level() / dry) == Catch::Approx (0.0).margin (3.0));
    // Every style makes something of it and none blows up.
    for (int style = 0; style < effects::Mars::numStyles; ++style)
    {
        setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { band (0, 0), (float) style }, { band (0, 1), 70.0f } }));
        const auto out = p.play (38, 0.3);
        const auto level = rms (out[0], (size_t) (0.05 * sampleRate), (size_t) (0.25 * sampleRate));
        INFO (effects::Mars::styleNames[style] << ": " << level);
        CHECK (std::isfinite (level));
        CHECK (level > dry * 0.05);
        CHECK (level < dry * 6.0);
    }
    // The slot's MIX 0: dry again, no harmonics.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { band (0, 1), 100.0f }, { 3, 0.0f } }));
    t.level();
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.03));
    CHECK (t.level (900.0) < dry * 0.002);

    // Three bands, crossovers at 1 kHz and 5 kHz: the 300 Hz sine is in band 1. Band 1 clean (its MIX 0) and band 3
    // driven hard: hardly any harmonics; band 1 driven: plenty.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { 0, 2.0f }, { 1, 1000.0f }, { 2, 5000.0f },
                                                                       { band (0, 3), 0.0f }, { band (2, 1), 100.0f } }));
    t.level();
    const auto cleanLow = t.level (900.0);
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { 0, 2.0f }, { 1, 1000.0f }, { 2, 5000.0f },
                                                                       { band (0, 1), 100.0f } }));
    t.level();
    const auto drivenLow = t.level (900.0);
    INFO ("900 Hz with band 1 clean: " << cleanLow << ", driven: " << drivenLow);
    CHECK (drivenLow > cleanLow * 5.0);
    // Band 1's LEVEL -6 dB (clean): the sine 6 dB down.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { 0, 2.0f }, { 1, 1000.0f }, { 2, 5000.0f },
                                                                       { band (0, 3), 0.0f }, { band (0, 4), -6.0f } }));
    t.level();
    CHECK (juce::Decibels::gainToDecibels (t.level() / dry) == Catch::Approx (-6.0).margin (0.6));

    // QUALITY 1x and 8x both work (harmonics, finite).
    for (const auto quality : { 0.0f, 2.0f })
    {
        setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::saturation, { { 4, quality }, { band (0, 1), 100.0f } }));
        CHECK (t.level (900.0) > dry * 0.01);
    }

    // A preset keeps all 20 values (12 before 0.35.0: the slot grew).
    auto fx = effectOf (mixerfx::saturation, { { 0, 1.0f }, { band (2, 4), -9.5f } });
    setEffect (p, mixer::kickIn, 2, fx);
    const auto back = ModMatrix::fromXml (*p.processor->getModMatrix().toXml());
    CHECK (back.effect (mixer::kickIn, 2) == fx);
    CHECK (back.effect (mixer::kickIn, 2).values[(size_t) band (2, 4)] == Catch::Approx (-9.5f));
    CHECK (mixerfx::paramsOf (mixerfx::saturation).size() == (size_t) mixer::maxFxValues);
    CHECK (mixerfx::valueText (mixerfx::saturation, band (0, 0), 5.0f) == "Warm Tape");
}

TEST_CASE ("MIXER EFFECTS 0.35.0: SATURATION's CPU, on every mic channel of a multi-mic kit hit every sixteenth note",
           "[mixer][mixerfx][saturation][performance]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    const auto measure = [&] (float bands, float quality)
    {
        auto m = p.processor->getModMatrix();
        for (int strip = 0; strip < mixer::numStrips; ++strip)
            m.effect (strip, 0) = effectOf (mixerfx::saturation, { { 0, bands }, { 4, quality } });
        p.processor->setModMatrix (m);
        p.processor->prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto blocks = (int) (4.0 * sampleRate / blockSize);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b % (int) (0.125 * sampleRate / blockSize) == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0);
                midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 0);
            }
            p.processor->processBlock (buffer, midi);
        }
        return (juce::Time::getMillisecondCounterHiRes() - started) / 4000.0 * 100.0;
    };
    const auto oneBand = measure (0.0f, 1.0f), threeBands = measure (2.0f, 1.0f), superb = measure (2.0f, 2.0f);
    WARN ("SATURATION on 4 sounding mic channels: 1 band 4x " << oneBand << "%, 3 bands 4x " << threeBands
          << "%, 3 bands 8x " << superb << "% of one core");
    CHECK (threeBands < 90.0);   // (measured ~30% here: CI runners are slower)
}

TEST_CASE ("MIXER EFFECTS: TRANSIENT's ATTACK lifts each hit's front, SUSTAIN its ring", "[mixer][mixerfx]")
{
    TempFolder temp;
    // A hit: a 200 Hz sine dying away over about 150 ms.
    const auto file = temp.folder.getChildFile ("hit.wav");
    {
        juce::AudioBuffer<float> b (1, (int) (0.6 * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) (std::exp (-i / (0.15 * sampleRate)) * std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / sampleRate)));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    const auto sfz = temp.folder.getChildFile ("kit.sfz");
    sfz.replaceWithText ("<group> key=38\n<region> sample=hit.wav group_label=Snare Top\n<region> sample=hit.wav group_label=Room\n");
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (sfz, drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    const auto front = [&] (const std::vector<float>& x) { return rms (x, 0, (size_t) (0.005 * sampleRate)); };
    const auto ring = [&] (const std::vector<float>& x) { return rms (x, (size_t) (0.15 * sampleRate), (size_t) (0.3 * sampleRate)); };
    const auto plain = p.play (38, 0.4)[0];
    REQUIRE (front (plain) > 0.05);

    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::transient, { { 0, 100.0f } }));
    const auto punchy = p.play (38, 0.4)[0];
    INFO ("front " << front (punchy) << " against " << front (plain));
    CHECK (front (punchy) > front (plain) * 1.5);
    CHECK (ring (punchy) == Catch::Approx (ring (plain)).epsilon (0.15));

    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::transient, { { 1, 100.0f } }));
    CHECK (ring (p.play (38, 0.4)[0]) > ring (plain) * 1.5);
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::transient, { { 1, -100.0f } }));
    CHECK (ring (p.play (38, 0.4)[0]) < ring (plain) * 0.7);
}

TEST_CASE ("MIXER EFFECTS: PRE EFFECTS sends before them; with an effect loaded the later sends take the processed sound; a bus's effects",
           "[mixer][mixerfx]")
{
    SnareTop t;
    auto& p = t.p;
    const auto dry = t.level();

    // A shut gate on the snare top: a PRE FADER send to BUS 1 is shut too (it's after the effects); a PRE EFFECTS
    // one isn't.
    setEffect (p, mixer::snareTop, 0, effectOf (mixerfx::gate, { { 0, -6.0f } }));
    setSend (p, mixer::snareTop, 0, sendTo (1, 0.0f, mixer::SendMode::preFader));
    CHECK (t.level() < dry * 0.001);
    setSend (p, mixer::snareTop, 0, sendTo (1, 0.0f, mixer::SendMode::preEffects));
    CHECK (t.level() == Catch::Approx (dry).epsilon (0.02));
    setSend (p, mixer::snareTop, 0, {});
    setEffect (p, mixer::snareTop, 0, {});

    // KICK IN carries the snare as bleed (500 Hz). A post-fader send with its BLEED off: without effects it sends no
    // bleed; with a (flat) EQ loaded, the channel's sound goes through it as one, so it does.
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 1.0f);
    p.set (ParamIDs::mixMuteMode, 1.0f);
    auto send = sendTo (1, 0.0f, mixer::SendMode::preFader);
    send.bleedOn = false;
    setSend (p, mixer::kickIn, 0, send);
    const auto bleed = p.level (38, 500.0);
    CHECK (bleed < 1.0e-4);
    setEffect (p, mixer::kickIn, 0, effectOf (mixerfx::eq));
    CHECK (p.level (38, 500.0) > 0.05);
    // ... unless the send is PRE EFFECTS.
    send.mode = mixer::SendMode::preEffects;
    setSend (p, mixer::kickIn, 0, send);
    CHECK (p.level (38, 500.0) < 1.0e-4);
    setSend (p, mixer::kickIn, 0, {});
    setEffect (p, mixer::kickIn, 0, {});
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 0.0f);
    p.set (ParamIDs::mixMuteMode, 0.0f);

    // A bus's effects: the snare top into BUS 1, its EQ +6 dB at 300 Hz.
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    p.processor->setMixerRoute (mixer::snareTop, 1);
    setEffect (p, busStrip (0), 0, effectOf (mixerfx::eq, { { 3, 300.0f }, { 4, 6.0f } }));
    CHECK (t.level() == Catch::Approx (dry * 1.995).epsilon (0.03));
}

TEST_CASE ("MIXER EFFECTS page: a slot's effect from its menu (defaults), the EFFECTS tab, copy / paste, remove; one undo step each",
           "[mixer][mixerfx]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    MixerPage page (*p.processor);
    page.setSize (1400, 650);
    page.selectChannel (mixer::hiHat);

    CHECK (page.describeEffect (mixer::hiHat, 0) == "+ FX");
    page.setEffectType (mixer::hiHat, 0, mixerfx::compressor);
    CHECK (page.describeEffect (mixer::hiHat, 0) == "Compressor");
    CHECK (p.processor->getModMatrix().effect (mixer::hiHat, 0) == mixerfx::defaultSlot (mixerfx::compressor));
    CHECK (p.processor->getMixerEffectType (mixer::hiHat, 0) == mixerfx::compressor);

    // The editor's change to the chosen slot.
    page.showTab (MixerPage::Tab::effects);
    page.chooseEffectSlot (0);
    auto fx = p.processor->getModMatrix().effect (mixer::hiHat, 0);
    fx.values[0] = -24.0f;
    fx.outputDb = 2.0f;
    page.setEffect (0, fx);
    CHECK (p.processor->getModMatrix().effect (mixer::hiHat, 0).values[0] == Catch::Approx (-24.0f));

    // Copy onto a bus's slot 3; remove it from the hi-hat.
    page.copyEffect (mixer::hiHat, 0);
    page.pasteEffect (busStrip (2), 2);
    CHECK (p.processor->getModMatrix().effect (busStrip (2), 2) == fx);
    page.setEffectType (mixer::hiHat, 0, 0);
    CHECK (! p.processor->getModMatrix().effect (mixer::hiHat, 0).isLoaded());
    CHECK (p.processor->getMixerEffectType (mixer::hiHat, 0) == 0);
    page.update();

    // The value texts.
    CHECK (mixerfx::valueText (mixerfx::eq, 0, 20.0f) == "OFF");
    CHECK (mixerfx::valueText (mixerfx::eq, 11, 20000.0f) == "OFF");
    CHECK (mixerfx::valueText (mixerfx::eq, 3, 2500.0f) == "2.50 kHz");
    CHECK (mixerfx::valueText (mixerfx::compressor, 5, 2.0f) == "FET");
}

TEST_CASE ("MIXER EFFECTS: CPU, a multi-mic kit hit every sixteenth note with four effects on every channel and bus",
           "[mixer][mixerfx][performance]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    auto m = p.processor->getModMatrix();
    for (int strip = 0; strip < mixer::numStrips; ++strip)
    {
        m.effect (strip, 0) = mixerfx::defaultSlot (mixerfx::gate);
        m.effect (strip, 1) = effectOf (mixerfx::eq, { { 4, 3.0f } });
        m.effect (strip, 2) = mixerfx::defaultSlot (mixerfx::compressor);
        m.effect (strip, 3) = effectOf (mixerfx::transient, { { 0, 30.0f } });
    }
    p.processor->setModMatrix (m);
    p.processor->setMixerRoute (mixer::snareTop, 1);
    p.processor->setMixerRoute (mixer::kickIn, 2);
    p.processor->prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    const auto blocks = (int) (8.0 * sampleRate / blockSize);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b % (int) (0.125 * sampleRate / blockSize) == 0)
        {
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0);
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 0);
        }
        p.processor->processBlock (buffer, midi);
    }
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 8000.0 * 100.0;
    WARN ("4 mic channels and 2 buses, each with gate, EQ, compressor, transient: " << percent << "% of one core");
    CHECK (percent < 25.0);
}

//==============================================================================
// ROUTE INSTRUMENT MICROPHONES and the USER CHANNELS (0.23.0)
TEST_CASE ("MIC ROUTING: a drum's mic to NONE, to another channel, to a new USER channel with its own fader, output and effects",
           "[mixer][routing]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    p.set (ParamIDs::drum (0, "VelToVol"), 0.0f);
    const auto snare = drums::pieceOfSlot (drums::notemap::SD), kick = drums::pieceOfSlot (0);
    REQUIRE (snare >= 0);
    REQUIRE (kick >= 0);
    const auto top = p.level (38, 300.0), bleed = p.level (38, 500.0), room = p.level (38, 2000.0);
    REQUIRE (bleed > 0.05);

    // The snare's KICK IN mic (its bleed there) to NONE: gone; its other mics untouched.
    p.processor->setMicRoute (snare, mixer::kickIn, mixer::routeNone);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    p.processor->setMicRoute (snare, mixer::kickIn, mixer::routeAuto);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed).epsilon (0.02));

    // The snare's ROOM mic into the SNARE TOP channel: muting ROOM no longer touches it; muting SNARE TOP does.
    p.processor->setMicRoute (snare, mixer::room, mixer::snareTop);
    p.set (ParamIDs::mix (mixer::room, "Mute"), 1.0f);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 1.0f);
    CHECK (p.level (38, 2000.0) < room * 0.001);
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 0.0f);
    p.set (ParamIDs::mix (mixer::room, "Mute"), 0.0f);

    // A NEW CHANNEL for the snare's room mic: USER 1, named after them, playing where ROOM played (OUT 2 here).
    p.processor->setMixerOutput (mixer::room, 1);
    const auto user = p.processor->createUserChannel (snare, mixer::room);
    REQUIRE (user == mixer::numChannels);
    CHECK (p.processor->getMicRoute (snare, mixer::room) == user);
    CHECK (p.processor->getMixerChannelName (user) == "SNARE ROOM");
    CHECK (p.processor->getUserChannelsInUse() == juce::Array<int> { user });
    CHECK (p.processor->getMixerOutput (mixer::stripOfChannel (user)) == 1);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    REQUIRE (out2 >= 0);
    CHECK (levelAt (p.play (38)[(size_t) out2], 2000.0, 2400, 9600) == Catch::Approx (room).epsilon (0.03));
    p.processor->setMixerOutput (mixer::stripOfChannel (user), 0);
    p.processor->setMixerOutput (mixer::room, 0);
    // Its own fader (ROOM's doesn't touch it now), and its own effects (an EQ +6 dB at 2 kHz on its strip).
    p.set (ParamIDs::mix (mixer::room, "Level"), -60.0f);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mix (user, "Level"), -6.0f);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room * 0.501).epsilon (0.02));
    p.set (ParamIDs::mix (user, "Level"), 0.0f);
    auto m = p.processor->getModMatrix();
    m.effect (mixer::stripOfChannel (user), 0) = mixerfx::defaultSlot (mixerfx::eq);
    m.effect (mixer::stripOfChannel (user), 0).values[3] = 2000.0f;
    m.effect (mixer::stripOfChannel (user), 0).values[4] = 6.0f;
    p.processor->setModMatrix (m);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room * 1.995).epsilon (0.03));

    // A second one: the snare's KICK IN mic (bleed) to USER 2. It stays bleed there: USER 2's BLEED off silences it.
    const auto user2 = p.processor->createUserChannel (snare, mixer::kickIn);
    CHECK (user2 == mixer::numChannels + 1);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed).epsilon (0.02));
    p.set (ParamIDs::mix (user2, "BleedOn"), 0.0f);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));

    // Renaming; presets keep the routes and names; back to AUTO, USER 2 isn't in use any more.
    p.processor->renameUserChannel (user, "room crush");
    CHECK (p.processor->getMixerChannelName (user) == "ROOM CRUSH");
    const auto back = ModMatrix::fromXml (*p.processor->getModMatrix().toXml());
    CHECK (back == p.processor->getModMatrix());
    CHECK (back.micRoute (snare, mixer::room) == user);
    CHECK (back.micRoute (kick, mixer::overheads) == mixer::routeAuto);
    p.processor->setMicRoute (snare, mixer::kickIn, mixer::routeAuto);
    CHECK (p.processor->getUserChannelsInUse() == juce::Array<int> { user });
}

TEST_CASE ("MIC ROUTING page: ROUTE MICS... lists the drums and their mics (close first); user channels' strips appear once used",
           "[mixer][routing]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    MixerPage page (*p.processor);
    page.setSize (1400, 650);
    const auto snare = drums::pieceOfSlot (drums::notemap::SD);

    page.openRouter (true);
    CHECK (page.isRouterOpen());
    page.chooseRouterPiece (snare);
    CHECK (page.getRouterPiece() == snare);
    CHECK (page.getRouterMics() == juce::Array<int> { mixer::snareTop, mixer::kickIn, mixer::room });

    const auto userStrip = mixer::stripOfChannel (mixer::numChannels);
    CHECK (! page.isStripShown (userStrip));
    const auto user = p.processor->createUserChannel (snare, mixer::room);
    page.routingChanged();
    CHECK (page.isStripShown (userStrip));
    CHECK (page.getChannelsInUse().contains (user));
    CHECK (! page.getChannelsInUse().contains (mixer::room));   // the snare's room mic was the only one there
    page.selectChannel (user);
    CHECK (page.getSelectedStrip() == userStrip);
    page.showTab (MixerPage::Tab::bleed);
    page.update();
    page.openRouter (false);
    CHECK (! page.isRouterOpen());
}

//==============================================================================
// 0.39.0: THE DRUM BUSES
TEST_CASE ("THE DRUM BUSES: a fresh mixer groups the kick's, snare's and toms' channels into KICK, SNARE and TOMS", "[mixer][drumbuses]")
{
    ParagonX1AudioProcessor p;
    for (const auto c : { mixer::kickIn, mixer::kickOut })
        CHECK (p.getMixerRoute (c) == 1);
    for (const auto c : { mixer::snareTop, mixer::snareBottom })
        CHECK (p.getMixerRoute (c) == 2);
    for (const auto c : { mixer::tom1, mixer::tom2, mixer::tom3 })
        CHECK (p.getMixerRoute (c) == 3);
    for (const auto c : { mixer::hiHat, mixer::cymbals, mixer::overheads, mixer::room, mixer::ambience, mixer::other })
        CHECK (p.getMixerRoute (c) == 0);
    CHECK (p.getMixerRoute (mixer::stripOfChannel (mixer::numChannels)) == 0);   // USER 1
    CHECK (p.getMixerBusName (0) == "KICK");
    CHECK (p.getMixerBusName (1) == "SNARE");
    CHECK (p.getMixerBusName (2) == "TOMS");
    CHECK (p.getMixerBusName (3) == "BUS 4");

    // Renamed (one undo step); empty is its number again.
    p.renameBus (3, "fx bus");
    CHECK (p.getMixerBusName (3) == "FX BUS");
    p.flushUndoStep();
    p.undo();
    CHECK (p.getMixerBusName (3) == "BUS 4");
    p.renameBus (0, "");
    CHECK (p.getMixerBusName (0) == "BUS 1");
}

TEST_CASE ("THE DRUM BUSES: through them, a kit sounds exactly as it did on the channels' outputs", "[mixer][drumbuses]")
{
    TempFolder temp;
    const auto render = [&] (bool buses)
    {
        Plugin p (temp.folder.getChildFile ("presets"));
        p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
        REQUIRE (p.processor->waitForInstruments());
        p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
        if (! buses)
            for (int c = 0; c < mixer::numChannels; ++c)
                p.processor->setMixerRoute (c, 0);
        auto kick = p.play (36);
        const auto snare = p.play (38);
        kick[0].insert (kick[0].end(), snare[0].begin(), snare[0].end());
        kick[1].insert (kick[1].end(), snare[1].begin(), snare[1].end());
        return kick;
    };
    const auto onBuses = render (true), direct = render (false);
    REQUIRE (onBuses[0].size() == direct[0].size());
    auto peak = 0.0f, worst = 0.0f;
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < direct[c].size(); ++i)
        {
            peak = std::max (peak, std::abs (direct[c][i]));
            worst = std::max (worst, std::abs (direct[c][i] - onBuses[c][i]));
        }
    CHECK (peak > 0.05f);
    CHECK (worst < 1.0e-6f);

    // And a bus fader moves its drum's channels together: KICK at -6 dB, the kick half as loud (its overheads too
    // stay: they're the OVERHEADS channel's, not the bus's).
    Plugin p (temp.folder.getChildFile ("presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    const auto inBefore = p.level (36, 100.0), ohBefore = p.level (36, 1000.0);
    p.set (ParamIDs::mixBus (0, "Level"), -6.0f);
    CHECK (std::abs (juce::Decibels::gainToDecibels (p.level (36, 100.0) / inBefore) + 6.0) < 0.1);
    CHECK (std::abs (juce::Decibels::gainToDecibels (p.level (36, 1000.0) / ohBefore)) < 0.1);
}

TEST_CASE ("THE DRUM BUSES: presets and projects from before keep their routes; new ones keep the buses", "[mixer][drumbuses]")
{
    // A preset saved before 0.39.0 (parameterVersion 4) that doesn't give the routes: on their outputs, buses unnamed.
    ParagonX1AudioProcessor p;
    auto xml = p.captureRig().toXml();
    REQUIRE (xml != nullptr);
    xml->setAttribute ("parameterVersion", 4);
    for (auto* e = xml->getFirstChildElement(); e != nullptr;)
    {
        auto* next = e->getNextElement();
        if (e->hasTagName ("Param") && ParamIDs::routesOnBuses039.contains (e->getStringAttribute ("id")))
            xml->removeChildElement (e, true);
        e = next;
    }
    if (auto* modulation = xml->getChildByName ("Modulation"))
        while (auto* name = modulation->getChildByName ("BusName"))
            modulation->removeChildElement (name, true);
    const auto old = Rig::fromXml (*xml);
    REQUIRE (old.has_value());
    for (const auto& id : ParamIDs::routesOnBuses039)
        CHECK (juce::roundToInt (old->parameters.at (id)) == 0);
    CHECK (old->modulation.busNames[0].isEmpty());

    // One saved now: the buses as they were.
    const auto now = Rig::fromXml (*p.captureRig().toXml());
    REQUIRE (now.has_value());
    CHECK (juce::roundToInt (now->parameters.at ("mix1Route")) == 1);
    CHECK (now->modulation.busNames[0] == "KICK");

    // A project from before: the same.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    auto project = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (project != nullptr);
    project->setAttribute ("parameterVersion", 4);
    for (auto* e = project->getFirstChildElement(); e != nullptr;)
    {
        auto* next = e->getNextElement();
        if (ParamIDs::routesOnBuses039.contains (e->getStringAttribute ("id")))
            project->removeChildElement (e, true);
        e = next;
    }
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*project, oldState);
    ParagonX1AudioProcessor q;
    q.setStateInformation (oldState.getData(), (int) oldState.getSize());
    CHECK (q.getMixerRoute (mixer::kickIn) == 0);
    CHECK (q.getMixerRoute (mixer::tom2) == 0);
    CHECK (q.getMixerBusName (0) == "BUS 1");

    // A project saved now keeps them.
    ParagonX1AudioProcessor r;
    r.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (r.getMixerRoute (mixer::kickIn) == 1);
    CHECK (r.getMixerBusName (0) == "KICK");
}

TEST_CASE ("THE DRUM BUSES: a drum's layer channel goes where its drum's channel goes", "[mixer][drumbuses]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    const auto layer = temp.folder.getChildFile ("layer.wav");
    writeSine (layer, 60.0, 0.25f);
    p.processor->loadLayer (0, layer);   // the kick: a multi-mic instrument, so its layer gets a USER channel
    REQUIRE (p.processor->waitForInstruments());
    const auto out = juce::roundToInt (p.processor->getParameters().getRawParameterValue (ParamIDs::drum (0, "LayerOut"))->load());
    REQUIRE (out > 0);
    CHECK (p.processor->getMixerRoute (mixer::stripOfChannel (out - 1)) == 1);   // onto KICK
}

TEST_CASE ("THE DRUM MACHINE 0.39.0: a pad takes another drum (swapped with its own pad), kept in the project", "[machine]")
{
    ParagonX1AudioProcessor p;
    CHECK (p.getMachinePad (0) == 0);
    p.setMachinePad (0, 30);   // CRASH 4 onto the first pad: the kick onto CRASH 4's
    CHECK (p.getMachinePad (0) == 30);
    CHECK (p.getMachinePad (30) == 0);
    std::array<bool, ParamIDs::numDrums> seen {};
    for (int i = 0; i < ParamIDs::numDrums; ++i)
        seen[(size_t) p.getMachinePad (i)] = true;
    CHECK (std::all_of (seen.begin(), seen.end(), [] (bool b) { return b; }));

    juce::MemoryBlock state;
    p.getStateInformation (state);
    ParagonX1AudioProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getMachinePad (0) == 30);
    CHECK (q.getMachinePad (30) == 0);
    q.resetMachinePads();
    CHECK (q.getMachinePad (0) == 0);
}
