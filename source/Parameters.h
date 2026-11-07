#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved in DAW projects and presets, so never rename one once it has shipped. Add new parameters with
// a new ID instead. Choices are saved by index: only ever append to a choice list, never reorder it.
//
// PR7-I 0.2.0 starts its own set (the guitar's); PR7-D's drum and effect parameters are gone. Version hints count from
// 1 again (1: 0.2.0).
namespace ParamIDs
{
    // Global: not part of a preset.
    inline constexpr auto outputGain = "outputGain";   // the OUTPUT knob
    inline constexpr auto bypass     = "bypass";
    // NOTE NAMES: how notes are named (notenames::choiceNames: middle C is C3 or C4).
    inline constexpr auto noteNames  = "noteNames";
    // KEY MAP (0.3.0, hint 3): how the articulation keyswitches work (guitar::KeyMap): the owner's own map (C-1 palm
    // mute / sustain by velocity) or the reference's factory one. Not in presets: it belongs to the MIDI parts.
    inline constexpr auto keyMap     = "keyMap";
    inline const juce::StringArray keyMapNames { "Split (C-1 by velocity)", "Factory (C-1 / C#-1)" };

    // In presets.
    inline constexpr auto patchLevel = "patchLevel";   // the preset's own level
    // THE GUITAR (0.2.0): how notes are fretted and picked (guitar::Settings).
    inline constexpr auto tone           = "tone";            // a gentle low-pass on the whole guitar (Hz; the top: open)
    inline constexpr auto fretMode       = "fretMode";        // guitar::FretMode
    inline constexpr auto pickMode       = "pickMode";        // guitar::PickMode
    inline constexpr auto forceString    = "forceString";     // 0 off, then strings 7 (low A) .. 1 (high E)
    inline constexpr auto handPosition   = "handPosition";    // 0 AUTO, else the hand's fret
    inline constexpr auto velocityToVolume = "velToVol";
    inline constexpr auto release        = "release";         // seconds: a note-off damping its string
    inline constexpr auto transpose      = "transpose";       // semitones
    inline constexpr auto variation      = "variation";       // 0.3.0 (hint 2): each note a little different (guitar::Settings::variation)

    inline const juce::StringArray fretModeNames { "Natural", "Polyphonic" };
    inline const juce::StringArray pickModeNames { "Alternate", "Down", "Up" };
    inline const juce::StringArray forceStringNames { "Off", "String 7 (low A)", "String 6 (E)", "String 5 (A)", "String 4 (D)",
                                                      "String 3 (G)", "String 2 (B)", "String 1 (high E)" };

    // Not in presets: they belong to the project (or the computer), not the sound.
    inline const juce::StringArray notInRigs { outputGain, bypass, noteNames, keyMap };
}

namespace ParamRanges
{
    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 12.0f;
    inline constexpr float outputDefaultDb = -6.0f;   // headroom (the family's default)
    inline constexpr float toneMinHz = 1000.0f, toneMaxHz = 20000.0f;
    inline constexpr int maxHand = 21;                 // the hand's fret (its span then reaches the 24th)
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
