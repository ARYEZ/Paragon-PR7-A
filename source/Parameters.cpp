#include "Parameters.h"
#include "NoteNames.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    constexpr int firstVersion = 1;   // PR7-I 0.2.0
    constexpr int v030 = 2;           // PR7-I 0.2.1: VARIATION
    constexpr int v030b = 3;          // PR7-I 0.3.0: KEY MAP
    constexpr int v040 = 4;           // PR7-I 0.4.0: the new GUITAR page's controls
    constexpr int v060 = 5;           // PR7-I 0.6.0: the noises

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };

    // A range whose knob travel is logarithmic: each octave takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    void add (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
              Range range, float defaultValue, const ToText& toText, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, version }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    }

    void addSwitch (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name, bool defaultValue,
                    int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, defaultValue));
    }

    void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
                    const juce::StringArray& choices, int defaultIndex, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, choices, defaultIndex));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using namespace ParamIDs;

    add (layout, outputGain, "Output", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, ParamRanges::outputDefaultDb, asDb);
    addSwitch (layout, bypass, "Bypass", false);
    addChoice (layout, noteNames, "Note Names", notenames::choiceNames, 0);
    addChoice (layout, keyMap, "Key Map", keyMapNames, 0, v030b);
    add (layout, patchLevel, "Preset Level", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, asDb);

    // THE GUITAR.
    add (layout, tone, "Tone", logRange (ParamRanges::toneMinHz, ParamRanges::toneMaxHz), ParamRanges::toneMaxHz,
         [] (float hz, int) { return hz >= ParamRanges::toneMaxHz * 0.995f ? juce::String ("Open") : asFrequency (hz, 0); });
    addChoice (layout, fretMode, "Fretting Mode", fretModeNames, 0);
    addChoice (layout, pickMode, "Picking Mode", pickModeNames, 0);
    addChoice (layout, forceString, "Force String", forceStringNames, 0);
    add (layout, handPosition, "Hand Position", { 0.0f, (float) ParamRanges::maxHand, 1.0f }, 0.0f,
         [] (float v, int) { const auto f = juce::roundToInt (v); return f == 0 ? juce::String ("Auto") : "Fret " + juce::String (f); });
    add (layout, velocityToVolume, "Vel > Vol", { 0.0f, 1.0f, 0.01f }, 0.5f, asPercent);
    add (layout, release, "Release", { 0.005f, 0.5f, 0.001f, 0.5f }, 0.08f,
         [] (float s, int) { return juce::String (juce::roundToInt (s * 1000.0f)) + " ms"; });
    add (layout, transpose, "Transpose", { -12.0f, 12.0f, 1.0f }, 0.0f,
         [] (float v, int) { const auto st = juce::roundToInt (v); return (st > 0 ? "+" : "") + juce::String (st) + " st"; });
    add (layout, variation, "Variation", { 0.0f, 1.0f, 0.01f }, 0.0f, asPercent, v030);   // 0.2.2: off by default (0.2.0's sound)
    add (layout, capo, "Capo", { 0.0f, (float) ParamRanges::maxCapo, 1.0f }, 0.0f,
         [] (float v, int) { const auto f = juce::roundToInt (v); return f == 0 ? juce::String ("Off") : "Fret " + juce::String (f); }, v040);
    addChoice (layout, handSpan, "Hand Span", handSpanNames, 0, v040);
    add (layout, handReset, "Hand Reset", { 0.1f, 5.0f, 0.01f, 0.5f }, 1.0f, [] (float s, int) { return juce::String (s, 1) + " s"; }, v040);
    addSwitch (layout, mono, "Mono", false, v040);
    add (layout, polyWindow, "Poly Window", { 0.0f, ParamRanges::maxPolyWindowMs, 0.1f, 0.5f }, 0.0f,
         [] (float ms, int) { return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms"; }, v040);
    add (layout, bite, "Bite", { 0.0f, 1.0f, 0.01f }, 0.0f, asPercent, v040);
    add (layout, attack, "Attack", { 0.0f, 1.0f, 0.01f }, 0.0f, asPercent, v040);
    // THE NOISES (0.6.0): on at their rendered level, as the owner compares against the reference with its noises on.
    const auto noiseLevel = [] (float db, int)
    {
        return db <= ParamRanges::noiseMinDb + 0.05f ? juce::String ("Off") : (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    };
    add (layout, releaseNoise, "Release Noise", { ParamRanges::noiseMinDb, ParamRanges::noiseMaxDb, 0.1f }, 0.0f, noiseLevel, v060);
    add (layout, scrapes, "Scrapes", { 0.0f, 1.0f, 0.01f }, 0.12f, asPercent, v060);
    add (layout, fretNoise, "Fret Noise", { ParamRanges::noiseMinDb, ParamRanges::noiseMaxDb, 0.1f }, 0.0f, noiseLevel, v060);
    return layout;
}
