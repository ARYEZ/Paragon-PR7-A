#include "Parameters.h"
#include "NoteNames.h"

#include "dsp/synth/Drums.h"
#include "dsp/synth/NoteMap.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    // Version hints. PR7-D 0.1.0's parameters are PR7-K 0.24.0's, with PR7-K's hints (the PR7-K version that brought
    // each in: 1 for 0.1.0 ... 23 for 0.23.0; 16-19 are the rack's, in RackParameters.cpp). PR7-D's own new
    // parameters start at 24.
    constexpr int firstVersion = 1;
    constexpr int stage4d = 11;  // PR7-K 0.12.0: SHUFFLE
    constexpr int stage6a = 20;  // PR7-K 0.21.0: the kit
    constexpr int stage6aStart = 21;   // 0.21.1: SAMPLE's START and END
    constexpr int stage6b = 22;  // 0.22.0: the drum sequencer
    constexpr int stage6c = 23;  // 0.23.0: the sample editor
    constexpr int pr7d020 = 24;  // PR7-D 0.2.0: drums 13 to 20, the note map
    constexpr int pr7d030 = 25;  // PR7-D 0.3.0: the outputs (also in RackParameters.cpp)
    constexpr int pr7d0170 = 26; // PR7-D 0.17.0: drums 21 to 23, ON, MUTE TAIL, the hi-hat pedal
    constexpr int pr7d0180 = 27; // PR7-D 0.18.0: the velocity gate, MIDI IN CHANNEL
    constexpr int pr7d0190 = 28; // PR7-D 0.19.0: the sample engine's hit variation, layer limit, voices
    constexpr int pr7d0200 = 29; // PR7-D 0.20.0: the mixer's mic channels
    constexpr int pr7d0210 = 30; // PR7-D 0.21.0: the mixer's buses, routes and mute mode
    constexpr int pr7d0230 = 31; // PR7-D 0.23.0: the mixer's user channels
    constexpr int pr7d0240 = 32; // PR7-D 0.24.0: the acoustic models' position, shell, variation
    constexpr int pr7d0260 = 33; // PR7-D 0.26.0: simulated mics and room
    constexpr int pr7d0290 = 34; // PR7-D 0.29.0: a drum's layer
    constexpr int pr7d0330 = 35; // PR7-D 0.33.0: drums 24 to 29 (every setting of theirs)
    constexpr int pr7d0340 = 36; // PR7-D 0.34.0: the mixer's USER 9 to 16
    constexpr int pr7d0351 = 37; // PR7-D 0.35.1: NOTE NAMES
    constexpr int pr7d0360 = 38; // PR7-D 0.36.0: drums 30 to 36 (every setting of theirs)
    constexpr int pr7d0380 = 39; // PR7-D 0.38.0: THE DRUM MACHINE's own TEMPO
    constexpr int pr7d0500 = 40; // PR7-D 0.50.0: LAYERS 2 and 3

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };
    const ToText asPan = [] (float pan, int)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        return amount == 0 ? juce::String ("C") : (pan < 0.0f ? "L" : "R") + juce::String (amount);
    };
    const ToText asNote = [] (float value, int) { return notenames::name (juce::roundToInt (value)); };

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
    add (layout, patchLevel, "Preset Level", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, asDb);

    // THE DRUMS (PR7-K's stage 6a).
    {
        const ToText asPitch = [] (float value, int)
        {
            const auto rounded = std::round (value);
            const auto text = std::abs (value - rounded) < 0.005f ? juce::String ((int) rounded) : juce::String (value, 2);
            return (value > 0.004f ? "+" : "") + text + " st";
        };
        const ToText asLevel = [] (float db, int) { return db <= -47.95f ? juce::String ("-inf") : juce::String (db, 1) + " dB"; };
        const ToText asShare = [] (float value, int) { return juce::String (value * 100.0f, 2) + "%"; };   // of the sample
        // DELAY: later to the right; to the left, the anchor (how far into the hit lands on the step), ms either way.
        juce::NormalisableRange<float> delayRange (-1000.0f, 1000.0f, 0.1f, 0.35f, true);
        const ToText asDrumDelay = [] (float ms, int)
        {
            if (std::abs (ms) < 0.05f)
                return juce::String ("0 ms");
            const auto text = juce::String (std::abs (ms), std::abs (ms) < 10.0f ? 1 : 0) + " ms";
            return ms > 0.0f ? "+" + text : "Anchor " + text;
        };
        const ToText asLowPass = [] (float hz, int) { return hz >= 19999.0f ? juce::String ("Off") : asFrequency (hz, 0); };
        const ToText asHighPass = [] (float hz, int) { return hz <= 20.01f ? juce::String ("Off") : asFrequency (hz, 0); };
        add (layout, drumsRoot, "Drums Root Key", { 0.0f, 116.0f, 1.0f }, 36.0f, asNote, stage6a);
        add (layout, drumsLevel, "Drums Level", { -24.0f, 12.0f, 0.1f }, 0.0f, asDb, stage6a);
        add (layout, drumsVelocity, "Drums Velocity", { 0.0f, 1.0f }, 1.0f, asPercent, stage6a);
        addChoice (layout, drumsMap, "Drums Note Map", synth::drums::notemap::layoutNames, 1, pr7d020);
        addChoice (layout, drumsMidiChannel, "Drums MIDI Channel", midiChannelNames, 0, pr7d0180);
        addChoice (layout, noteNames, "Note Names", notenames::choiceNames, 0, pr7d0351);
        {
            const synth::drums::KitSettings defaults;
            for (int i = 0; i < 4; ++i)
                add (layout, drumsPedalSteps[i], "Hi-Hat Pedal Step " + juce::String (i + 1), { 0.0f, 127.0f, 1.0f },
                     (float) defaults.pedalSteps[(size_t) i], [] (float v, int) { return juce::String (juce::roundToInt (v)); }, pr7d0170);
        }
        for (int k = 0; k < numDrums; ++k)
        {
            // PR7-K's 12 drums keep their version hints; the 8 PR7-D added take 0.2.0's, the 3 after them 0.17.0's;
            // 0.33.0's six take its hint for every setting (`newer` below).
            const auto added = k >= 12, articulation = k >= 20, newer = k >= 23, newest = k >= 29;
            const auto at = [newer, newest] (int hint) { return newest ? pr7d0360 : newer ? pr7d0330 : hint; };
            const auto hint = articulation ? pr7d0170 : added ? pr7d020 : stage6a;
            const auto hintStart = articulation ? pr7d0170 : added ? pr7d020 : stage6aStart;
            const auto hintEditor = articulation ? pr7d0170 : added ? pr7d020 : stage6c;
            const auto& d = synth::drums::slotDefaults[(size_t) k];
            const auto name = "Drum " + juce::String (k + 1) + " ";
            addChoice (layout, drum (k, "Model"), name + "Model", synth::drums::modelNames, (int) d.model, at (hint));
            add (layout, drum (k, "Level"), name + "Volume", { -48.0f, 12.0f, 0.1f }, 0.0f, asLevel, at (hint));
            add (layout, drum (k, "Pan"), name + "Pan", { -1.0f, 1.0f }, 0.0f, asPan, at (hint));
            add (layout, drum (k, "Pitch"), name + "Pitch", { -24.0f, 24.0f, 0.01f }, d.pitch, asPitch, at (hint));
            add (layout, drum (k, "Decay"), name + "Decay", { 0.0f, 1.0f }, d.decay, asPercent, at (hint));
            add (layout, drum (k, "Tone"), name + "Tone", { 0.0f, 1.0f }, d.tone, asPercent, at (hint));
            add (layout, drum (k, "Snap"), name + "Snap", { 0.0f, 1.0f }, d.snap, asPercent, at (hint));
            addSwitch (layout, drum (k, "Mute"), name + "Mute", false, at (hint));
            addSwitch (layout, drum (k, "Solo"), name + "Solo", false, at (hint));
            addChoice (layout, drum (k, "Choke"), name + "Choke", synth::drums::chokeNames, d.choke, at (hint));
            addChoice (layout, drum (k, "Insert"), name + "Insert", insertBusNames, 0, at (hint));
            add (layout, drum (k, "SendDelay"), name + "Send Delay", { 0.0f, 1.0f }, 1.0f, asPercent, at (hint));
            add (layout, drum (k, "SendReverb"), name + "Send Reverb", { 0.0f, 1.0f }, 1.0f, asPercent, at (hint));
            add (layout, drum (k, "SampleStart"), name + "Sample Start", { 0.0f, 1.0f }, 0.0f, asShare, at (hintStart));
            add (layout, drum (k, "SampleEnd"), name + "Sample End", { 0.0f, 1.0f }, 1.0f, asShare, at (hintStart));
            // THE SAMPLE EDITOR (6c).
            add (layout, drum (k, "Gain"), name + "Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, asDb, at (hintEditor));
            addSwitch (layout, drum (k, "Mono"), name + "Mono", false, at (hintEditor));
            add (layout, drum (k, "Delay"), name + "Delay", delayRange, 0.0f, asDrumDelay, at (hintEditor));
            add (layout, drum (k, "LowPass"), name + "Low Pass", logRange (20.0, 20000.0), 20000.0f, asLowPass, at (hintEditor));
            add (layout, drum (k, "HighPass"), name + "High Pass", logRange (20.0, 20000.0), 20.0f, asHighPass, at (hintEditor));
            addChoice (layout, drum (k, "Output"), name + "Output", outputNames, 0, at (articulation ? pr7d0170 : pr7d030));
            // ARTICULATIONS (0.17.0).
            addSwitch (layout, drum (k, "On"), name + "On", true, at (pr7d0170));
            addChoice (layout, drum (k, "MuteTail"), name + "Mute Tail", synth::drums::muteTailNames, 0, at (pr7d0170));
            // THE VELOCITY GATE (0.18.0): the velocities (as they come in) that play it.
            const ToText asVelocity = [] (float v, int) { return juce::String (juce::roundToInt (v)); };
            add (layout, drum (k, "VelMin"), name + "Velocity Gate Low", { 0.0f, 127.0f, 1.0f }, 0.0f, asVelocity, at (pr7d0180));
            add (layout, drum (k, "VelMax"), name + "Velocity Gate High", { 0.0f, 127.0f, 1.0f }, 127.0f, asVelocity, at (pr7d0180));
            // THE SAMPLE ENGINE (0.19.0): HIT VARIATION (an instrument's hits), LAYER LIMIT, VOICES (every drum).
            addSwitch (layout, drum (k, "Randomize"), name + "Randomize Hits", true, at (pr7d0190));
            addSwitch (layout, drum (k, "Adjacent"), name + "Adjacent Layers", true, at (pr7d0190));
            addSwitch (layout, drum (k, "VelToVol"), name + "Velocity To Volume", true, at (pr7d0190));
            add (layout, drum (k, "LayerLimit"), name + "Layer Limit", { 0.0f, 99.0f, 1.0f }, 0.0f,
                 [] (float v, int) { return v < 0.5f ? juce::String ("All") : juce::String (juce::roundToInt (v)); }, at (pr7d0190));
            add (layout, drum (k, "Voices"), name + "Voices", { 1.0f, 48.0f, 1.0f }, (float) synth::drums::Kit::maxPerSlot,
                 [] (float v, int) { return juce::String (juce::roundToInt (v)); }, at (pr7d0190));
            const synth::drums::SlotSettings acoustic;
            add (layout, drum (k, "Position"), name + "Position", { 0.0f, 1.0f }, acoustic.position, asPercent, at (pr7d0240));
            add (layout, drum (k, "Shell"), name + "Shell", { 0.0f, 1.0f }, acoustic.shell, asPercent, at (pr7d0240));
            add (layout, drum (k, "Variation"), name + "Variation", { 0.0f, 1.0f }, acoustic.variation, asPercent, at (pr7d0240));
            addSwitch (layout, drum (k, "Mics"), name + "Simulated Mics", true, at (pr7d0260));
            add (layout, drum (k, "LayerLevel"), name + "Layer Level", { -60.0f, 12.0f, 0.1f }, 0.0f,
                 [] (float v, int) { return v <= -59.95f ? juce::String ("Off") : juce::String (v, 1) + " dB"; }, at (pr7d0290));
            addChoice (layout, drum (k, "LayerOut"), name + "Layer Out", layerOutNames, 0, at (pr7d0290));
            for (int n = 1; n < numLayers; ++n)
            {
                add (layout, drum (k, layerLevel (n)), name + "Layer " + juce::String (n + 1) + " Level", { -60.0f, 12.0f, 0.1f }, 0.0f,
                     [] (float v, int) { return v <= -59.95f ? juce::String ("Off") : juce::String (v, 1) + " dB"; }, pr7d0500);
                addChoice (layout, drum (k, layerOut (n)), name + "Layer " + juce::String (n + 1) + " Out", layerOutNames, 0, pr7d0500);
            }
        }

        // THE MIXER (0.20.0): the mic channels.
        for (int c = 0; c < synth::mixer::numMicChannels; ++c)
        {
            // 0.23.0: USER 1-8 after the named channels (their own version hint).
            const auto user = synth::mixer::isUserChannel (c);
            const auto userHint = c - synth::mixer::numChannels >= 8 ? pr7d0340 : pr7d0230;   // (0.34.0: USER 9-16)
            const auto hint020 = user ? userHint : pr7d0200, hint021 = user ? userHint : pr7d0210;
            const auto name = "Mix " + (user ? "user " + juce::String (c - synth::mixer::numChannels + 1) : synth::mixer::channelNames[c].toLowerCase()) + " ";
            const ToText asFader = [] (float v, int) { return v <= -59.95f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
            add (layout, mix (c, "Level"), name + "Level", { -60.0f, 12.0f, 0.1f }, 0.0f, asFader, hint020);
            add (layout, mix (c, "Pan"), name + "Pan", { -1.0f, 1.0f }, 0.0f, asPan, hint020);
            addSwitch (layout, mix (c, "Mute"), name + "Mute", false, hint020);
            addSwitch (layout, mix (c, "Solo"), name + "Solo", false, hint020);
            addSwitch (layout, mix (c, "Phase"), name + "Polarity", false, hint020);
            add (layout, mix (c, "Bleed"), name + "Bleed", { -60.0f, 12.0f, 0.1f }, 0.0f, asFader, hint020);
            addSwitch (layout, mix (c, "BleedOn"), name + "Bleed On", true, hint020);
            add (layout, mix (c, "Offset"), name + "Time Offset", { 0.0f, 10.0f, 0.01f }, 0.0f,
                 [] (float v, int) { return juce::String (v, 2) + " ms"; }, hint020);
            add (layout, mix (c, "Release"), name + "Release", { 0.0f, 2000.0f, 1.0f }, 0.0f,
                 [] (float v, int) { return v < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " ms"; }, hint020);
            addChoice (layout, mix (c, "Output"), name + "Output", outputNames, 0, hint020);
            addChoice (layout, mix (c, "Route"), name + "Route", synth::mixer::routeNames,
                       user ? 0 : synth::mixer::defaultRouteOf (c), hint021);   // 0.39.0: the drum buses
        }
        for (int b = 0; b < synth::mixer::numBuses; ++b)
        {
            const auto name = "Mix bus " + juce::String (b + 1) + " ";
            const ToText asFader = [] (float v, int) { return v <= -59.95f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
            add (layout, mixBus (b, "Level"), name + "Level", { -60.0f, 12.0f, 0.1f }, 0.0f, asFader, pr7d0210);
            add (layout, mixBus (b, "Pan"), name + "Pan", { -1.0f, 1.0f }, 0.0f, asPan, pr7d0210);
            addSwitch (layout, mixBus (b, "Mute"), name + "Mute", false, pr7d0210);
            addSwitch (layout, mixBus (b, "Solo"), name + "Solo", false, pr7d0210);
            addSwitch (layout, mixBus (b, "Phase"), name + "Polarity", false, pr7d0210);
            addChoice (layout, mixBus (b, "Output"), name + "Output", outputNames, 0, pr7d0210);
            addChoice (layout, mixBus (b, "Route"), name + "Route", synth::mixer::routeNames, 0, pr7d0210);
        }
        addChoice (layout, mixMuteMode, "Mix Mute Mode", synth::mixer::muteModeNames, 0, pr7d0210);
        // SIMULATED MICS AND ROOM (0.26.0).
        addSwitch (layout, drumsMicSim, "Drums Simulated Mics", false, pr7d0260);
        addChoice (layout, drumsRoom, "Drums Room", synth::micsim::roomNames, 1, pr7d0260);
        add (layout, drumsRoomDecay, "Drums Room Decay", logRange (0.3, 3.0), 1.0f,
             [] (float v, int) { return "x" + juce::String (v, 2); }, pr7d0260);
        add (layout, drumsRoomTone, "Drums Room Tone", { 0.0f, 1.0f }, 0.5f, asPercent, pr7d0260);

        // THE DRUM SEQUENCER (stage 6b).
        const synth::drumseq::Settings sequencer;
        addSwitch (layout, dseqOn, "Drum Seq On", sequencer.on, stage6b);
        addChoice (layout, dseqTrigger, "Drum Seq Trigger", synth::drumseq::triggerNames, (int) sequencer.trigger, stage6b);
        addChoice (layout, dseqPattern, "Drum Seq Pattern", synth::drumseq::patternNames, sequencer.pattern, stage6b);
        addChoice (layout, dseqDivision, "Drum Seq Speed", synth::seq::divisionNames, sequencer.division, stage6b);
        addChoice (layout, dseqTiming, "Drum Seq Timing", synth::seq::timingNames, (int) sequencer.timing, stage6b);
        add (layout, dseqLength, "Drum Seq Length", { 1.0f, (float) synth::drumseq::maxBeats, 1.0f }, (float) sequencer.lengthBeats,
             [] (float value, int) { return juce::String (juce::roundToInt (value)) + (juce::roundToInt (value) == 1 ? " beat" : " beats"); },
             stage6b);
        add (layout, dseqVelocity, "Drum Seq Velocity Amount", { 0.0f, 1.0f }, sequencer.velocityAmount, asPercent, stage6b);
        add (layout, shuffle, "Shuffle", { 0.0f, 1.0f }, 0.0f, asPercent, stage4d);
        // THE DRUM MACHINE (0.38.0): the tempo its PLAY runs at while the DAW isn't playing (the DAW's leads when it is).
        add (layout, machineTempo, "Machine Tempo", { 40.0f, 300.0f, 0.1f }, 120.0f,
             [] (float bpm, int) { return juce::String (bpm, 1) + " BPM"; }, pr7d0380);
    }

    addRackParameters (layout);   // the rack (PR7-K's 7a): RackParameters.cpp
    addSendParameters (layout);   // the sends (7b): RackParameters.cpp
    addModFxParameters (layout);  // MOD, MOD 2, LO-FI (7c): RackParameters.cpp
    addInsertParameters (layout); // the insert buses (7d): RackParameters.cpp
    return layout;
}
