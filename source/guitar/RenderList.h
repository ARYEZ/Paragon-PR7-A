#pragma once

#include "guitar/Tuning.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <vector>

// THE RENDER LIST (PR7-I 0.2.0): every note PR7-I needs from the owner's reference guitar instrument, as MIDI files to
// render through it (one file per string and articulation), and where each note lands in the rendered audio. Our own
// design: because PR7-I writes the MIDI, it knows exactly when each note starts and which string, fret, dynamic and
// stroke it is, so the import can cut the render without guessing.
//
// Each file, at 120 BPM:
//   - every take in turn: each fret 0-24, each of `numLayers` dynamics, `numRoundRobins` round-robins of a down then an up
//     stroke (down, up, down, up: the reference instrument's own round-robins give each its own sample);
//   - each take in its own slot of `slotSeconds`: the keyswitches first (the articulation, the stroke's picking mode),
//     then `noteDelaySeconds` later the note, held `holdSeconds`, then silence for its tail to die away;
//   - at the file's start, the keyswitches that force the string and switch legato off, and `leadInSeconds` of
//     silence before the first slot.
// The keyswitch notes are the reference instrument's defaults (its manual): force string C-2 (low A) to F#-2 (high E),
// legato off D#0, articulations from C-1 (sustain) and C#-1 (mute), picking mode up C7 / down C#7. In MIDI note numbers
// (its C-2 is note 0): 0-6, 27, 12 / 13, 108 / 109.
namespace guitar::renders
{
    inline constexpr int numLayers = 3;
    inline constexpr int numRoundRobins = 2;   // takes of each stroke (so 4 per note and dynamic: down, up, down, up)
    // The dynamics' velocities: sustain stays at 119 or below (above it the reference plays its rake and pinch harmonic).
    inline constexpr std::array<int, numLayers> layerVelocities { 45, 85, 118 };
    inline constexpr double bpm = 120.0;
    inline constexpr double leadInSeconds = 2.0;
    inline constexpr double noteDelaySeconds = 0.25;   // keyswitches first, then the note
    inline constexpr double holdSeconds = 4.0;          // how long a take's note is held (the longest a note can ring)
    inline constexpr double slotSeconds = 5.5;          // from one take's keyswitches to the next's

    namespace keys
    {
        inline constexpr int forceStringLowA = 0;       // + string: 0 low A .. 6 high E
        inline constexpr int sustain = 12, mute = 13;
        inline constexpr int legatoOff = 27;
        inline constexpr int pickUp = 108, pickDown = 109;
    }

    struct Take
    {
        int string = 0, fret = 0, layer = 0;
        Stroke stroke = Stroke::down;
        int roundRobin = 0;
        double noteSeconds = 0.0;   // when its note starts in the file
        int note() const noexcept { return noteOf (string, fret); }
        int velocity() const noexcept { return layerVelocities[(size_t) layer]; }
    };

    std::vector<Take> takes (Articulation, int string);
    double lengthSeconds (Articulation, int string);   // to the end of the last take's slot
    juce::MidiFile toMidi (Articulation, int string);
    juce::String fileName (Articulation, int string);   // "PR7-I Sustain - String 7 (low A)" (no extension)

    // Where an imported take's file goes in a guitar's folder: "<Sustain>/s0_f05_l2_d_r1.wav", and back.
    juce::String takeFileName (Articulation, const Take&);
    struct TakeId { Articulation articulation = Articulation::sustain; int string = 0, fret = 0, layer = 0; Stroke stroke = Stroke::down; int roundRobin = 0; };
    std::optional<TakeId> parseTakeFile (const juce::File&);
}
