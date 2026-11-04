// A DRUM'S LAYER (PR7-D 0.29.0): a sample (or folder of hits) of the user's played with each of a drum's hits. Measured
// with test tones: the drum's sound and the layer's both there, the layer at LAYER LEVEL and the hit's velocity, OFF
// silent; through the mixer, a USER channel of its own (named, its fader and mute its own) and the drum's close mics 10 dB
// down; a folder's hits in turn; presets and projects keeping it.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/Instrument.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/NoteMap.h"
#include "rigs/Rig.h"
#include "ui/DrumPage.h"

namespace
{
    using namespace synth;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-layer-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    void writeSine (const juce::File& file, double hz, float level, int channels = 1, double seconds = 1.0)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (channels, (int) (seconds * sampleRate));
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, (unsigned int) channels, 24, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
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
        std::vector<float> play (int slot, int velocity = 127, double seconds = 0.3)
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
    double level (const std::vector<float>& x, double hz) { return levelAt (x, hz, 2400, 9600); }
}

TEST_CASE ("LAYER: a drum's layer plays with each hit, at LAYER LEVEL and the hit's velocity; OFF silent", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.25f));
    const auto file = p.temp.folder.getChildFile ("my kick.wav");
    writeSine (file, 1000.0, 0.25f);
    const auto before = p.play (slot);
    CHECK (level (before, 1000.0) < 1.0e-4);
    p.processor->loadLayer (slot, file);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (p.processor->getLayerRef (slot).startsWith ("file||"));
    REQUIRE (p.processor->getLayer (slot) != nullptr);
    // A kit not through the mixer: the layer plays with the drum (its own way).
    CHECK (p.get (ParamIDs::drum (slot, "LayerOut")) == Catch::Approx (0.0f).margin (0.01));
    const auto full = p.play (slot);
    const auto drum = level (full, 300.0), layer = level (full, 1000.0);
    INFO ("drum " << drum << ", layer " << layer);
    CHECK (drum == Catch::Approx (level (before, 300.0)).epsilon (0.01));   // the drum as it was
    CHECK (layer == Catch::Approx (drum).epsilon (0.02));                   // the same file level, the same way: as loud
    p.set (ParamIDs::drum (slot, "LayerLevel"), -6.0f);
    CHECK (level (p.play (slot), 1000.0) == Catch::Approx (layer * 0.501).epsilon (0.02));
    // Velocity: as the drum's (the kit's VELOCITY: the level goes as velocity squared).
    p.set (ParamIDs::drum (slot, "LayerLevel"), 0.0f);
    const auto soft = p.play (slot, 64);
    CHECK (level (soft, 1000.0) / layer == Catch::Approx (level (soft, 300.0) / drum).epsilon (0.02));
    CHECK (level (soft, 1000.0) < layer * 0.4);
    // OFF.
    p.set (ParamIDs::drum (slot, "LayerLevel"), -60.0f);
    CHECK (level (p.play (slot), 1000.0) < 1.0e-4);
    p.set (ParamIDs::drum (slot, "LayerLevel"), 0.0f);
    // Removed: gone.
    p.processor->clearLayer (slot);
    CHECK (p.processor->getLayerRef (slot).isEmpty());
    CHECK (level (p.play (slot), 1000.0) < 1.0e-4);
}

TEST_CASE ("LAYER: through the mixer it gets a USER channel of its own, the drum's close mics 10 dB down", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.25f));
    p.set (ParamIDs::drumsMicSim, 1.0f);   // the drum through the mixer (simulated mics)
    const auto file = p.temp.folder.getChildFile ("my snare.wav");
    writeSine (file, 1000.0, 0.25f);
    p.processor->loadLayer (slot, file);
    REQUIRE (p.processor->waitForInstruments());
    const auto channel = juce::roundToInt (p.get (ParamIDs::drum (slot, "LayerOut"))) - 1;
    REQUIRE (mixer::isUserChannel (channel));
    CHECK (p.processor->getMixerChannelName (channel) == "SNARE LAYER");
    CHECK (p.processor->getUserChannelsInUse().contains (channel));
    CHECK (p.get (ParamIDs::mix (mixer::snareTop, "Level")) == Catch::Approx (-10.0f).margin (0.01));
    CHECK (p.get (ParamIDs::mix (mixer::snareBottom, "Level")) == Catch::Approx (-10.0f).margin (0.01));
    CHECK (p.get (ParamIDs::mix (mixer::overheads, "Level")) == Catch::Approx (0.0f).margin (0.01));
    // The layer in its channel alone: soloed it's there, muted it's gone.
    p.set (ParamIDs::mix (channel, "Solo"), 1.0f);
    const auto alone = p.play (slot);
    CHECK (level (alone, 1000.0) > 0.05);
    CHECK (level (alone, 300.0) < 1.0e-4);
    p.set (ParamIDs::mix (channel, "Solo"), 0.0f);
    p.set (ParamIDs::mix (channel, "Mute"), 1.0f);
    CHECK (level (p.play (slot), 1000.0) < 1.0e-4);
    p.set (ParamIDs::mix (channel, "Mute"), 0.0f);
    // Its fader: -6 dB.
    const auto at0 = level (p.play (slot), 1000.0);
    p.set (ParamIDs::mix (channel, "Level"), -6.0f);
    CHECK (level (p.play (slot), 1000.0) == Catch::Approx (at0 * 0.501).epsilon (0.02));
}

TEST_CASE ("LAYER: a folder of hits plays them in turn; presets and the project keep the layer", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::BD;
    testkit::useSample (*p.processor, slot, testkit::sine (60.0, 1.0, 0.25f));
    const auto folder = p.temp.folder.getChildFile ("kicks");
    writeSine (folder.getChildFile ("kick_rr1.wav"), 1000.0, 0.25f);
    writeSine (folder.getChildFile ("kick_rr2.wav"), 1500.0, 0.25f);
    p.processor->loadLayer (slot, folder);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (p.processor->getLayerRef (slot).startsWith ("folder||"));
    REQUIRE (p.processor->getLayer (slot)->numHits() == 2);
    // Two hits in a row: one of each (never the same twice running).
    const auto first = p.play (slot), second = p.play (slot);
    const auto isFirstA = level (first, 1000.0) > level (first, 1500.0);
    const auto isSecondA = level (second, 1000.0) > level (second, 1500.0);
    CHECK (isFirstA != isSecondA);

    // A preset keeps it (as an instrument's file), and loads it again.
    REQUIRE (p.processor->saveCurrentRigAs ("Layered"));
    const auto saved = p.processor->getLayerRef (slot);
    p.processor->clearLayer (slot);
    CHECK (p.processor->getLayerRef (slot).isEmpty());
    const auto* entry = [&]() -> const RigLibrary::Entry*
    {
        for (const auto& e : p.processor->getRigLibrary().getEntries())
            if (e.rig.name == "Layered")
                return &e;
        return nullptr;
    }();
    REQUIRE (entry != nullptr);
    CHECK (entry->rig.drumInstruments.at (ParagonX1AudioProcessor::layerOf (slot)) == saved);
    p.processor->loadRig (*entry);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (p.processor->getLayerRef (slot) == saved);
    CHECK (p.processor->getLayer (slot) != nullptr);
    // A preset's XML round trip keeps it too.
    const auto again = Rig::fromXml (*entry->rig.toXml());
    REQUIRE (again.has_value());
    CHECK (again->drumInstruments.at (ParagonX1AudioProcessor::layerOf (slot)) == saved);

    // The project.
    juce::MemoryBlock state;
    p.processor->getStateInformation (state);
    Plugin q;
    q.processor->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (q.processor->waitForInstruments());
    CHECK (q.processor->getLayerRef (slot) == saved);
    CHECK (q.processor->getLayer (slot) != nullptr);
}

// ============================================================================================================
// THREE LAYERS (0.50.0): each with its own LEVEL and OUT, PITCH, OFFSET, POLARITY, velocities and LENGTH, AUTO-ALIGN,
// multi-mic layers (their mics into their channels), and every place that keeps them.
namespace
{
    // A hit: silence, then at `at` samples a decaying sine (a drum's body) with a sharp start.
    std::shared_ptr<const drums::Sample> burst (double hz, int at, float sign = 1.0f, double seconds = 0.3, double decay = 0.03)
    {
        auto s = std::make_shared<drums::Sample>();
        s->name = "burst";
        s->sampleRate = sampleRate;
        s->channels.assign (1, std::vector<float> ((size_t) (seconds * sampleRate)));
        for (size_t i = (size_t) at; i < s->channels[0].size(); ++i)
        {
            const auto t = (double) (i - (size_t) at) / sampleRate;
            s->channels[0][i] = sign * 0.5f * (float) (std::exp (-t / decay) * std::sin (juce::MathConstants<double>::twoPi * hz * t + 0.6));
        }
        return s;
    }
    void writeSample (const juce::File& file, const drums::Sample& s)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b ((int) s.channels.size(), (int) s.length());
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.copyFrom (c, 0, s.channels[(size_t) c].data(), b.getNumSamples());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), s.sampleRate,
                                                                         (unsigned int) b.getNumChannels(), 32, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    int firstLoud (const std::vector<float>& x, float threshold = 0.01f)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > threshold)
                return (int) i;
        return -1;
    }
    float peakOf (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (const auto v : x)
            p = juce::jmax (p, std::abs (v));
        return p;
    }
}

TEST_CASE ("LAYERS 0.50.0: three layers play together, each at its own LEVEL; each plays only on its velocities", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.25f));
    const double hz[3] = { 1000.0, 1500.0, 2200.0 };
    for (int n = 0; n < 3; ++n)
    {
        const auto file = p.temp.folder.getChildFile ("layer" + juce::String (n) + ".wav");
        writeSine (file, hz[n], 0.25f);
        p.processor->loadLayer (slot, file, n);
    }
    REQUIRE (p.processor->waitForInstruments());
    for (int n = 0; n < 3; ++n)
        REQUIRE (p.processor->getLayer (slot, n) != nullptr);
    CHECK (p.processor->firstFreeLayer (slot) == -1);
    const auto all = p.play (slot);
    const auto drum = level (all, 300.0);
    for (int n = 0; n < 3; ++n)
        CHECK (level (all, hz[n]) == Catch::Approx (drum).epsilon (0.03));
    // Layer 2 at -12 dB, layer 3 off.
    p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (1)), -12.0f);
    p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (2)), -60.0f);
    const auto mixed = p.play (slot);
    CHECK (level (mixed, hz[0]) == Catch::Approx (drum).epsilon (0.03));
    CHECK (level (mixed, hz[1]) == Catch::Approx (drum * 0.2512).epsilon (0.03));
    CHECK (level (mixed, hz[2]) < 1.0e-4);
    p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (1)), 0.0f);
    p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (2)), 0.0f);
    // VEL LOW 100 on layer 2: a hit at 64 leaves it out, one at 127 plays it. VEL HIGH 90 on layer 3: the reverse.
    auto t2 = p.processor->getLayerTweak (slot, 1);
    t2.velLo = 100;
    p.processor->setLayerTweak (slot, 1, t2);
    auto t3 = p.processor->getLayerTweak (slot, 2);
    t3.velHi = 90;
    p.processor->setLayerTweak (slot, 2, t3);
    const auto soft = p.play (slot, 64), hard = p.play (slot, 127);
    CHECK (level (soft, hz[1]) < 1.0e-4);
    CHECK (level (soft, hz[2]) > 0.01);
    CHECK (level (hard, hz[1]) > 0.05);
    CHECK (level (hard, hz[2]) < 1.0e-4);
    CHECK (level (soft, hz[0]) > 0.01);   // layer 1 on every velocity
    // Removing one leaves the others (and its settings back to new).
    p.processor->clearLayer (slot, 1);
    CHECK (p.processor->getLayerTweak (slot, 1).isDefault());
    CHECK (p.processor->firstFreeLayer (slot) == 1);
    const auto after = p.play (slot, 64);
    CHECK (level (after, hz[1]) < 1.0e-4);
    CHECK (level (after, hz[0]) > 0.01);
}

TEST_CASE ("LAYERS 0.50.0: PITCH moves the layer by semitones; LENGTH fades it there", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.0f));   // the drum silent: the layer alone
    const auto file = p.temp.folder.getChildFile ("tone.wav");
    writeSine (file, 1000.0, 0.25f);
    p.processor->loadLayer (slot, file);
    REQUIRE (p.processor->waitForInstruments());
    auto t = p.processor->getLayerTweak (slot, 0);
    t.pitch = 12.0f;
    p.processor->setLayerTweak (slot, 0, t);
    const auto up = p.play (slot);
    CHECK (level (up, 2000.0) > 0.05);
    CHECK (level (up, 1000.0) < 0.01);
    CHECK (frequencyOf (up, 2400, 9600) == Catch::Approx (2000.0).epsilon (0.01));
    t.pitch = 0.0f;
    p.processor->setLayerTweak (slot, 0, t);
    const auto whole = p.play (slot);
    t.lengthMs = 50.0f;
    p.processor->setLayerTweak (slot, 0, t);
    const auto cut = p.play (slot);
    const auto rmsOf = [] (const std::vector<float>& x, double from, double to) { return rms (x, (size_t) (from * sampleRate), (size_t) (to * sampleRate)); };
    CHECK (rmsOf (cut, 0.01, 0.045) == Catch::Approx (rmsOf (whole, 0.01, 0.045)).epsilon (0.01));   // as it was until its LENGTH
    CHECK (rmsOf (whole, 0.08, 0.2) > 0.05);
    CHECK (rmsOf (cut, 0.08, 0.2) < 1.0e-5);                                                          // gone after its fade (12.5 ms)
}

TEST_CASE ("LAYERS 0.50.0: OFFSET plays the layer later, or earlier by skipping into it, to the sample; POLARITY turns it over", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.0f));
    const auto file = p.temp.folder.getChildFile ("hit.wav");
    writeSample (file, *burst (180.0, 480));   // its hit 10 ms in
    p.processor->loadLayer (slot, file);
    REQUIRE (p.processor->waitForInstruments());
    const auto at = firstLoud (p.play (slot));
    CHECK (at == 480);
    auto t = p.processor->getLayerTweak (slot, 0);
    t.offsetMs = 5.0f;
    p.processor->setLayerTweak (slot, 0, t);
    CHECK (firstLoud (p.play (slot)) == 480 + 240);
    t.offsetMs = -2.0f;
    p.processor->setLayerTweak (slot, 0, t);
    CHECK (firstLoud (p.play (slot)) == 480 - 96);
    // POLARITY: the same hit as the drum, turned over, cancels it.
    testkit::useSample (*p.processor, slot, burst (180.0, 480));
    t = {};
    p.processor->setLayerTweak (slot, 0, t);
    const auto together = peakOf (p.play (slot));
    t.invert = true;
    p.processor->setLayerTweak (slot, 0, t);
    const auto cancelled = peakOf (p.play (slot));
    INFO ("together " << together << ", turned over " << cancelled);
    CHECK (together > 0.3f);
    CHECK (cancelled < together * 0.01f);
}

TEST_CASE ("LAYERS 0.50.0: AUTO-ALIGN finds the delay and the polarity, and the two then add", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, burst (180.0, 100));
    // Late by 37 samples and turned over.
    const auto late = p.temp.folder.getChildFile ("late.wav");
    writeSample (late, *burst (180.0, 137, -1.0f));
    p.processor->loadLayer (slot, late);
    REQUIRE (p.processor->waitForInstruments());
    const auto drumAlone = [&]
    {
        p.set (ParamIDs::drum (slot, "LayerLevel"), -60.0f);
        const auto x = p.play (slot);
        p.set (ParamIDs::drum (slot, "LayerLevel"), 0.0f);
        return x;
    }();
    const auto before = peakOf (p.play (slot));
    const auto result = p.processor->alignLayer (slot, 0);
    REQUIRE (result.found);
    INFO ("offset " << result.offsetMs << " ms, invert " << (int) result.invert << ", match " << result.match);
    CHECK (result.offsetMs == Catch::Approx (-37.0 / 48.0).margin (0.011));
    CHECK (result.invert);
    CHECK (result.match > 0.99);
    CHECK (p.processor->getLayerTweak (slot, 0).invert);
    // Lined up, the layer doubles the drum, sample for sample.
    const auto aligned = p.play (slot);
    CHECK (peakOf (aligned) == Catch::Approx (2.0f * peakOf (drumAlone)).epsilon (0.01));
    CHECK (peakOf (aligned) > before * 1.3f);

    // Early by 50 samples (not turned over): set 50 samples later.
    const auto early = p.temp.folder.getChildFile ("early.wav");
    writeSample (early, *burst (180.0, 50));
    p.processor->loadLayer (slot, early, 1);
    REQUIRE (p.processor->waitForInstruments());
    const auto second = p.processor->alignLayer (slot, 1);
    REQUIRE (second.found);
    CHECK (second.offsetMs == Catch::Approx (50.0 / 48.0).margin (0.011));
    CHECK (! second.invert);

    // A different drum (another pitch, its own decay): lined up within 0.3 ms of its start, where the two add up the
    // most: summed, at least as strong as lined up exactly on the starts.
    const auto other = p.temp.folder.getChildFile ("other.wav");
    writeSample (other, *burst (240.0, 300, 1.0f, 0.3, 0.05));
    p.processor->loadLayer (slot, other, 2);
    REQUIRE (p.processor->waitForInstruments());
    for (int n = 0; n < 2; ++n)
        p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (n)), -60.0f);
    const auto third = p.processor->alignLayer (slot, 2);
    REQUIRE (third.found);
    CHECK (third.offsetMs == Catch::Approx (-200.0 / 48.0).margin (0.31));
    CHECK (! third.invert);
    const auto strength = [&] { const auto x = p.play (slot); return rms (x, 0, (size_t) (0.025 * sampleRate)); };
    const auto aligned3 = strength();
    auto onStarts = p.processor->getLayerTweak (slot, 2);
    onStarts.offsetMs = (float) (-200.0 / 48.0);
    p.processor->setLayerTweak (slot, 2, onStarts);
    const auto starts3 = strength();
    INFO ("aligned " << aligned3 << ", on the starts " << starts3);
    CHECK (aligned3 >= starts3 * 0.999);

    // Nothing to compare: not found, nothing changed.
    p.processor->clearLayer (slot, 2);
    const auto none = p.processor->alignLayer (slot, 2);
    CHECK (! none.found);
    CHECK (none.problem.isNotEmpty());
}

TEST_CASE ("LAYERS 0.50.0: a multi-mic layer's close mic into its own channel, its overheads into theirs", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, testkit::sine (300.0, 1.0, 0.0f));
    const auto folder = p.temp.folder.getChildFile ("Foundry Snare");
    writeSine (folder.getChildFile ("Snare Top").getChildFile ("hit_V1_RR1.wav"), 1000.0, 0.25f);
    writeSine (folder.getChildFile ("OH").getChildFile ("hit_V1_RR1.wav"), 1700.0, 0.25f, 2);
    REQUIRE (drums::instruments::hasMicFolders (folder));
    p.processor->loadLayer (slot, folder);
    REQUIRE (p.processor->waitForInstruments());
    const auto layer = p.processor->getLayer (slot);
    REQUIRE (layer != nullptr);
    CHECK (layer->hasMics());
    // A multi-mic layer goes through the mixer even with a plain drum: a USER channel of its own.
    const auto channel = juce::roundToInt (p.get (ParamIDs::drum (slot, "LayerOut"))) - 1;
    REQUIRE (mixer::isUserChannel (channel));
    CHECK (p.processor->getMixerChannelName (channel) == "SNARE LAYER");
    // Its close mic in its channel (soloed: there; its overheads not).
    p.set (ParamIDs::mix (channel, "Solo"), 1.0f);
    const auto close = p.play (slot);
    CHECK (level (close, 1000.0) > 0.05);
    CHECK (level (close, 1700.0) < 1.0e-4);
    p.set (ParamIDs::mix (channel, "Solo"), 0.0f);
    // Its overheads in OVERHEADS.
    p.set (ParamIDs::mix (mixer::overheads, "Solo"), 1.0f);
    const auto oh = p.play (slot);
    CHECK (level (oh, 1700.0) > 0.05);
    CHECK (level (oh, 1000.0) < 1.0e-4);
    p.set (ParamIDs::mix (mixer::overheads, "Solo"), 0.0f);
    // OUT back to WITH THE DRUM: the close mic goes to the snare's own channel (SNARE TOP) instead.
    p.set (ParamIDs::drum (slot, "LayerOut"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    CHECK (level (p.play (slot), 1000.0) > 0.01);
    // A second multi-mic layer gets the next USER channel, named LAYER 2.
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);
    p.set (ParamIDs::drum (slot, "LayerOut"), (float) (channel + 1));
    p.processor->loadLayer (slot, folder, 1);
    REQUIRE (p.processor->waitForInstruments());
    const auto channel2 = juce::roundToInt (p.get (ParamIDs::drum (slot, ParamIDs::layerOut (1)))) - 1;
    CHECK (mixer::isUserChannel (channel2));
    CHECK (channel2 != channel);
    CHECK (p.processor->getMixerChannelName (channel2) == "SNARE LAYER 2");
}

TEST_CASE ("LAYERS 0.50.0: presets, kits and the project keep all three and their settings", "[layer]")
{
    Plugin p;
    constexpr int slot = drums::notemap::BD;
    testkit::useSample (*p.processor, slot, testkit::sine (60.0, 1.0, 0.25f));
    for (int n = 0; n < 3; ++n)
    {
        const auto file = p.temp.folder.getChildFile ("k" + juce::String (n) + ".wav");
        writeSine (file, 1000.0 + 300.0 * n, 0.25f);
        p.processor->loadLayer (slot, file, n);
        drums::LayerTweak t;
        t.pitch = 1.5f * (float) (n + 1);
        t.offsetMs = -1.25f + (float) n;
        t.invert = n == 1;
        t.velLo = 10 * n;
        t.velHi = 127 - n;
        t.lengthMs = 100.0f * (float) n;
        p.processor->setLayerTweak (slot, n, t);
    }
    p.set (ParamIDs::drum (slot, ParamIDs::layerLevel (2)), -7.5f);
    REQUIRE (p.processor->waitForInstruments());
    const auto check = [slot] (ParagonX1AudioProcessor& q, const juce::String& where)
    {
        INFO (where);
        for (int n = 0; n < 3; ++n)
        {
            CHECK (q.getLayerRef (slot, n).endsWith ("k" + juce::String (n) + ".wav"));
            const auto& t = q.getLayerTweak (slot, n);
            CHECK (t.pitch == Catch::Approx (1.5f * (float) (n + 1)));
            CHECK (t.offsetMs == Catch::Approx (-1.25f + (float) n));
            CHECK (t.invert == (n == 1));
            CHECK (t.velLo == 10 * n);
            CHECK (t.velHi == 127 - n);
            CHECK (t.lengthMs == Catch::Approx (100.0f * (float) n));
        }
        auto* level3 = dynamic_cast<juce::RangedAudioParameter*> (q.getParameters().getParameter (ParamIDs::drum (slot, ParamIDs::layerLevel (2))));
        CHECK (level3->convertFrom0to1 (level3->getValue()) == Catch::Approx (-7.5f).margin (0.05));
    };
    // A preset (its XML round trip too), loaded back after the layers were taken away.
    REQUIRE (p.processor->saveCurrentRigAs ("Three"));
    const auto* entry = [&]() -> const RigLibrary::Entry*
    {
        for (const auto& e : p.processor->getRigLibrary().getEntries())
            if (e.rig.name == "Three")
                return &e;
        return nullptr;
    }();
    REQUIRE (entry != nullptr);
    for (int n = 0; n < 3; ++n)
        CHECK (entry->rig.drumInstruments.count (ParagonX1AudioProcessor::layerOf (slot, n)) == 1);
    {
        const auto again = Rig::fromXml (*entry->rig.toXml());
        REQUIRE (again.has_value());
        CHECK (again->drumInstruments == entry->rig.drumInstruments);
        CHECK (again->modulation == entry->rig.modulation);
    }
    const auto savedEntry = *entry;
    const auto kit = p.processor->captureDrumKit();
    juce::MemoryBlock state;
    p.processor->getStateInformation (state);
    for (int n = 0; n < 3; ++n)
        p.processor->clearLayer (slot, n);
    p.processor->loadRig (savedEntry);
    REQUIRE (p.processor->waitForInstruments());
    check (*p.processor, "preset");
    // A kit file.
    {
        Plugin q;
        REQUIRE (q.processor->applyDrumKit (*kit));
        REQUIRE (q.processor->waitForInstruments());
        check (*q.processor, "kit");
    }
    // The project.
    {
        Plugin q;
        q.processor->setStateInformation (state.getData(), (int) state.getSize());
        REQUIRE (q.processor->waitForInstruments());
        check (*q.processor, "project");
    }
    // Saved instrument numbers: a 0.29.0-0.49.0 file's layer (numDrums + k) is layer 1, layers 2 and 3 after it.
    CHECK (ParamIDs::instrumentIndexFrom (ParamIDs::numDrums + 4, 5) == ParagonX1AudioProcessor::layerOf (4, 0));
    CHECK (ParamIDs::instrumentIndexFrom (2 * ParamIDs::numDrums + 4, 5) == ParagonX1AudioProcessor::layerOf (4, 1));
    CHECK (ParamIDs::instrumentIndexFrom (3 * ParamIDs::numDrums + 4, 5) == ParagonX1AudioProcessor::layerOf (4, 2));
    CHECK (ParamIDs::instrumentIndexFrom (23 + 4, 2) == ParagonX1AudioProcessor::layerOf (4, 0));
    CHECK (ParamIDs::instrumentIndexFrom (29 + 4, 3) == ParagonX1AudioProcessor::layerOf (4, 0));
}

TEST_CASE ("LAYERS 0.50.0: the DRUM view's LAYERS tab shows the chosen drum's three, its knobs set them, AUTO-ALIGN reports", "[layer][ui]")
{
    Plugin p;
    constexpr int slot = drums::notemap::SD;
    testkit::useSample (*p.processor, slot, burst (180.0, 100));
    const auto file = p.temp.folder.getChildFile ("late.wav");
    writeSample (file, *burst (180.0, 148, -1.0f));
    p.processor->loadLayer (slot, file, 1);
    REQUIRE (p.processor->waitForInstruments());
    DrumPage page (*p.processor);
    page.setSize (1476, 672);
    page.showView (DrumPage::View::drum);
    page.select (slot);
    page.showLayers (true);
    CHECK (page.isShowingLayers());
    auto& panel = page.getLayerPanel();
    CHECK (panel.isVisible());
    CHECK (panel.getSlot() == slot);
    CHECK (panel.getWidth() > 600);
    CHECK (! panel.getPictureArea().isEmpty());
    // VELOCITY / ENVELOPE take the space back.
    page.showVelocity (true);
    CHECK (! panel.isVisible());
    page.showLayers (true);
    // A knob: an undo step with the value.
    panel.setKnob (1, LayerPanel::Knob::pitch, 3.0);
    CHECK (p.processor->getLayerTweak (slot, 1).pitch == Catch::Approx (3.0f));
    panel.setKnob (1, LayerPanel::Knob::pitch, 0.0);
    panel.align (1);
    CHECK (panel.getStatus (1).startsWith ("ALIGNED: -1.00 ms, TURNED OVER"));
    CHECK (p.processor->getLayerTweak (slot, 1).invert);
    panel.align (2);   // nothing there
    CHECK (panel.getStatus (2).startsWith ("CAN'T ALIGN"));
    // Another drum chosen: the panel follows.
    page.select (drums::notemap::BD);
    CHECK (panel.getSlot() == drums::notemap::BD);
}
