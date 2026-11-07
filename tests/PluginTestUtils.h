// Shared by the tests that play the whole plugin: a steady test tone from the kit. With the synth gone (PR7-K's
// tests played a sine oscillator), a drum slot set to SAMPLE plays a sine made here: at the processor's own rate and
// PITCH 0 it comes out sample for sample, so levels and frequencies can be measured as exactly as before.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

namespace testkit
{
    // A sample from a cycle's shape (0 to 1 in, -1 to 1 out), `seconds` long at `rate`, peak about `level`, on 1 or 2
    // channels; its last 3 ms fade out (as a synth note's release would), so its end doesn't click.
    template <typename Shape>
    std::shared_ptr<const synth::drums::Sample> tone (Shape shape, double hz, double seconds, float level, int channels, double rate)
    {
        auto sample = std::make_shared<synth::drums::Sample>();
        sample->name = "tone";
        sample->sampleRate = rate;
        const auto length = (size_t) (seconds * rate);
        const auto fade = std::min (length, (size_t) (0.003 * rate));
        sample->channels.assign ((size_t) channels, std::vector<float> (length));
        for (size_t i = 0; i < length; ++i)
        {
            const auto left = length - i;
            const auto gain = left < fade ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) left / (double) fade) : 1.0;
            const auto phase = std::fmod (hz * (double) i / rate, 1.0);
            for (auto& channel : sample->channels)
                channel[i] = (float) (level * gain * shape (phase));
        }
        return sample;
    }

    // A sine at `hz`.
    inline std::shared_ptr<const synth::drums::Sample> sine (double hz, double seconds = 20.0, float level = 0.5f, int channels = 1,
                                                             double rate = 48000.0)
    {
        return tone ([] (double p) { return std::sin (juce::MathConstants<double>::twoPi * p); }, hz, seconds, level, channels, rate);
    }

    // A band-limited saw at `hz` (its harmonics up to 20 kHz, each 1/n), as PR7-K's default oscillator played.
    inline std::shared_ptr<const synth::drums::Sample> saw (double hz, double seconds = 20.0, float level = 0.5f, int channels = 1,
                                                            double rate = 48000.0)
    {
        const auto harmonics = juce::jmax (1, (int) (20000.0 / hz));
        return tone ([harmonics] (double p)
        {
            auto sum = 0.0;
            for (int n = 1; n <= harmonics; ++n)
                sum += std::sin (juce::MathConstants<double>::twoPi * n * p) / n;
            return sum * 2.0 / juce::MathConstants<double>::pi * 0.85;   // peaks near 1 (the Gibbs overshoot under it)
        }, hz, seconds, level, channels, rate);
    }

    inline void set (ParagonX1AudioProcessor& processor, const juce::String& id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }

    // 0.59.0: a new PR7-D starts on GROOVE LIBRARY. Tests that play the kit on ROOT's keys (keyFor: 36 + slot) put it
    // on ROOT first.
    inline void useRootKeys (ParagonX1AudioProcessor& processor) { set (processor, ParamIDs::drumsMap, 0.0f); }

    // Slot `slot` plays `sample` as recorded: SAMPLE, PITCH 0, DECAY / TONE / SNAP at their neutral middle.
    inline void useSample (ParagonX1AudioProcessor& processor, int slot, std::shared_ptr<const synth::drums::Sample> sample)
    {
        processor.setDrumSample (slot, std::move (sample));
        set (processor, ParamIDs::drum (slot, "Model"), (float) synth::drums::Model::sample);
        set (processor, ParamIDs::drum (slot, "Pitch"), 0.0f);
        for (const auto* what : { "Decay", "Tone", "Snap" })
            set (processor, ParamIDs::drum (slot, what), 0.5f);
    }

    // The key that plays slot `slot` (ROOT at its default, 36).
    inline int keyFor (int slot) { return 36 + slot; }
}
