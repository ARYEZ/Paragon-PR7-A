// The plugin as a DAW sees it: an instrument, MIDI in and sound out.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    using testkit::set;

    std::unique_ptr<ParagonX1AudioProcessor> makeProcessor (int outputs = 2)
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        processor->setPlayConfigDetails (0, outputs, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        testkit::useRootKeys (*processor);   // (0.59.0: ROOT's keys, as these tests play them)
        return processor;
    }

    // Plays `seconds`, with `midi` (positions from the start) spread over the blocks; returns channel 0 (and 1).
    std::array<std::vector<float>, 2> play (ParagonX1AudioProcessor& processor, double seconds, const juce::MidiBuffer& midi = {}, int channels = 2,
                                            int block = blockSize)
    {
        std::array<std::vector<float>, 2> out;
        juce::AudioBuffer<float> buffer (channels, block);
        for (int start = 0; start < (int) (seconds * sampleRate); start += block)
        {
            juce::MidiBuffer slice;
            slice.addEvents (midi, start, block, -start);
            buffer.clear();
            processor.processBlock (buffer, slice);
            for (int ch = 0; ch < channels; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + block);
        }
        return out;
    }

    juce::MidiBuffer note (int number, int at = 0, int length = 1 << 30, float velocity = 1.0f)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, number, velocity), at);
        if (length < (1 << 30))
            midi.addEvent (juce::MidiMessage::noteOff (1, number), at + length);
        return midi;
    }

    // Frequency of a steady tone from its zero crossings (rising), over the second half.
    double frequencyOf (const std::vector<float>& samples)
    {
        const auto from = samples.size() / 2;
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (auto i = from + 1; i < samples.size(); ++i)
            if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
            {
                const auto exact = (double) (i - 1) + samples[i - 1] / (samples[i - 1] - samples[i]);
                if (first < 0.0) first = exact; else ++crossings;
                last = exact;
            }
        return crossings > 0 ? sampleRate * crossings / (last - first) : 0.0;
    }

    double rms (const std::vector<float>& samples, size_t from, size_t to)
    {
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += (double) samples[i] * samples[i];
        return std::sqrt (sum / (double) (to - from));
    }
}

TEST_CASE ("PR7-D is an instrument: no audio in, MIDI in, mono or stereo out; 15 more stereo outputs, off at first", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK (processor.acceptsMidi());
    CHECK_FALSE (processor.producesMidi());
    CHECK (processor.getBusCount (true) == 0);
    CHECK (processor.getMainBusNumOutputChannels() == 2);
    REQUIRE (processor.getBusCount (false) == 16);
    CHECK (processor.getTotalNumOutputChannels() == 2);   // the DAW switches the others on
    CHECK (processor.getBus (false, 0)->getName() == "Main");
    CHECK (processor.getBus (false, 15)->getName() == "Out 16");
    for (int o = 1; o < 16; ++o)
        CHECK_FALSE (processor.getBus (false, o)->isEnabled());

    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    for (int o = 1; o < 16; ++o)
        layout.outputBuses.add (juce::AudioChannelSet::disabled());
    CHECK (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (3) = juce::AudioChannelSet::stereo();   // OUT 4 on
    CHECK (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (3) = juce::AudioChannelSet::mono();     // the others: stereo or off
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (3) = juce::AudioChannelSet::disabled();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (processor.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}

TEST_CASE ("Silent until a key plays; a hit starts on its own sample, as the sample plays it", "[processor]")
{
    auto processor = makeProcessor();
    testkit::useSample (*processor, 9, testkit::sine (440.0));   // A: the high tom's slot
    const auto silent = play (*processor, 0.2);
    CHECK (*std::max_element (silent[0].begin(), silent[0].end()) == 0.0f);

    const auto out = play (*processor, 1.0, note (testkit::keyFor (9), 100));
    for (int i = 0; i < 100; ++i)
        REQUIRE (out[0][(size_t) i] == 0.0f);   // nothing before its sample
    CHECK (std::abs (out[0][101]) + std::abs (out[0][102]) + std::abs (out[0][110]) > 0.0f);
    CHECK_THAT (frequencyOf (out[0]), WithinAbs (440.0, 0.05));
}

TEST_CASE ("The keyboard maps the kit: the 23 keys from ROOT play it, in order; other keys do nothing", "[processor]")
{
    const auto peakFor = [] (int root, int key)
    {
        auto processor = makeProcessor();
        set (*processor, ParamIDs::drumsRoot, (float) root);
        const auto out = play (*processor, 0.3, note (key));
        return std::max (*std::max_element (out[0].begin(), out[0].end()), -*std::min_element (out[0].begin(), out[0].end()));
    };
    for (int slot = 0; slot < synth::drums::numSlots; ++slot)
        CHECK (peakFor (36, 36 + slot) > 0.01f);
    CHECK (peakFor (36, 35) == 0.0f);   // below the kit (no sequencer on KEY)
    CHECK (peakFor (36, 36 + synth::drums::numSlots) == 0.0f);   // above it
    CHECK (peakFor (48, 36) == 0.0f);   // ROOT moved up: the old kick key is silent...
    CHECK (peakFor (48, 48) > 0.01f);   // ...and its new one plays

    // Each key its own drum: the kick's key plays the kick (a low thump), not the closed hat.
    auto processor = makeProcessor();
    testkit::useSample (*processor, 6, testkit::sine (3000.0));
    const auto hat = play (*processor, 0.3, note (testkit::keyFor (6)));
    CHECK_THAT (frequencyOf (hat[0]), WithinAbs (3000.0, 0.5));
}

TEST_CASE ("OUTPUT and the preset's LEVEL apply their dB; BYPASS fades to silence", "[processor]")
{
    const auto level = [] (float outputDb, float levelDb, bool bypass)
    {
        auto processor = makeProcessor();
        testkit::useSample (*processor, 0, testkit::sine (220.0));
        set (*processor, ParamIDs::outputGain, outputDb);
        set (*processor, ParamIDs::patchLevel, levelDb);
        set (*processor, ParamIDs::bypass, bypass ? 1.0f : 0.0f);
        const auto out = play (*processor, 0.5, note (testkit::keyFor (0)));
        return rms (out[0], 12000, 24000);
    };
    const auto reference = level (0.0f, 0.0f, false);
    REQUIRE (reference > 0.01);
    CHECK_THAT (level (-6.0f, 0.0f, false) / reference, WithinAbs (0.5012, 0.003));
    CHECK_THAT (level (0.0f, 6.0f, false) / reference, WithinAbs (1.9953, 0.01));
    CHECK (level (0.0f, 0.0f, true) < 1.0e-6);
}

TEST_CASE ("OUTPUT starts at -6 dB, for headroom when hits stack up", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK_THAT (processor.getParameters().getRawParameterValue (ParamIDs::outputGain)->load(), WithinAbs (-6.0, 1.0e-6));
}

TEST_CASE ("The DAW's bypass button is our BYPASS", "[processor]")
{
    ParagonX1AudioProcessor processor;
    CHECK (processor.getBypassParameter() == processor.getParameters().getParameter (ParamIDs::bypass));
}

TEST_CASE ("A mono output plays both sides folded together", "[processor]")
{
    auto processor = makeProcessor (1);
    set (*processor, ParamIDs::drum (0, "Pan"), -1.0f);   // hard left: only the left side has it
    const auto out = play (*processor, 0.3, note (testkit::keyFor (0)), 1);
    CHECK (*std::max_element (out[0].begin(), out[0].end()) > 0.1f);
}

TEST_CASE ("Oversized blocks (more than the DAW promised) play the same", "[processor]")
{
    const auto render = [] (int block)
    {
        auto processor = makeProcessor();
        processor->seedRandomForTest (1);   // the snare's noise the same both times
        return play (*processor, 0.25, note (testkit::keyFor (2), 300, 6000), 2, block)[0];
    };
    const auto normal = render (blockSize);
    const auto big = render (blockSize * 8);
    REQUIRE (normal.size() <= big.size());
    for (size_t i = 0; i < normal.size(); ++i)
        REQUIRE (std::abs (normal[i] - big[i]) < 1.0e-6f);
}

TEST_CASE ("Settings survive a save and reload, as in a DAW project", "[processor][state]")
{
    juce::MemoryBlock state;
    {
        ParagonX1AudioProcessor processor;
        set (processor, ParamIDs::drum (2, "Pitch"), -12.0f);
        set (processor, ParamIDs::drum (5, "LowPass"), 640.0f);
        set (processor, ParamIDs::drum (11, "Model"), (float) synth::drums::Model::cowbell);
        set (processor, ParamIDs::outputGain, -9.0f);
        processor.getStateInformation (state);
    }
    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const auto get = [&restored] (const juce::String& id) { return restored.getParameters().getRawParameterValue (id)->load(); };
    CHECK_THAT (get (ParamIDs::drum (2, "Pitch")), WithinAbs (-12.0, 1.0e-4));
    CHECK_THAT (get (ParamIDs::drum (5, "LowPass")), WithinAbs (640.0, 0.5));
    CHECK (juce::roundToInt (get (ParamIDs::drum (11, "Model"))) == (int) synth::drums::Model::cowbell);
    CHECK_THAT (get (ParamIDs::outputGain), WithinAbs (-9.0, 1.0e-4));
}

TEST_CASE ("Fuzz: any settings, changed as notes play, never crash it or make it blow up", "[processor][fuzz]")
{
    auto processor = makeProcessor();
    juce::Random random (2026);
    const auto& parameters = static_cast<juce::AudioProcessor&> (*processor).getParameters();
    juce::AudioBuffer<float> buffer (2, blockSize);
    auto finite = true;
    float loudest = 0.0f;
    for (int block = 0; block < 3000; ++block)
    {
        // Every 20 blocks, a few parameters jump anywhere (except OUTPUT and BYPASS, to keep it audible).
        if (block % 20 == 0)
            for (int k = 0; k < 12; ++k)
            {
                auto* parameter = parameters[random.nextInt (parameters.size())];
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    if (withId->paramID != ParamIDs::outputGain && withId->paramID != ParamIDs::bypass && withId->paramID != ParamIDs::patchLevel)
                        parameter->setValueNotifyingHost (random.nextFloat());
            }
        juce::MidiBuffer midi;
        if (random.nextInt (4) == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 24 + random.nextInt (84), (juce::uint8) (1 + random.nextInt (127))), random.nextInt (blockSize));
        if (random.nextInt (4) == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, 24 + random.nextInt (84)), random.nextInt (blockSize));
        if (random.nextInt (30) == 0)
            midi.addEvent (juce::MidiMessage::pitchWheel (1, random.nextInt (16384)), random.nextInt (blockSize));
        if (random.nextInt (60) == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, random.nextBool() ? 127 : 0), random.nextInt (blockSize));
        processor->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = buffer.getSample (ch, i);
                finite = finite && std::isfinite (x);
                loudest = juce::jmax (loudest, std::abs (x));
            }
    }
    CHECK (finite);
    CHECK (loudest < 1000.0f);   // nothing ran away (it can be loud: 12 drums at +12 dB, GAIN +24, into the effects)
}
