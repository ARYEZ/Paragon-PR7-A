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
