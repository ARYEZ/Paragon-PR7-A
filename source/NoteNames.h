#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

// NOTE NAMES (PR7-D 0.35.1): which octave number MIDI note 60 (middle C) is written with. DAWs disagree: Cubase and
// Yamaha call it C3 (so the kick, note 36, is C1); Roland and most other software C4 (the kick C2). The note numbers
// are the same either way; only the names. SETTINGS' NOTE NAMES (ParamIDs::noteNames, not in presets) sets it, the
// editor keeps this in step, and everything that writes a note's name asks here.
namespace notenames
{
    inline const juce::StringArray choiceNames { "C3 Cubase", "C4 Roland" };   // saved by index: only append
    inline std::atomic<int> middleCOctave { 3 };
    inline int octaveForChoice (int choice) noexcept { return choice == 1 ? 4 : 3; }
    inline juce::String name (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, middleCOctave.load()); }
}
