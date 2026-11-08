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
    inline constexpr double bpm = 120.0;
    inline constexpr double leadInSeconds = 2.0;
    inline constexpr double noteDelaySeconds = 0.25;   // keyswitches first, then the note

    // EACH ARTICULATION'S LIST (0.3.0: per articulation; 0.2.0 had sustain's alone):
    //   - SUSTAIN (fixed: the owner renders these lists): 3 dynamics at velocities 45 / 85 / 118 (below the reference's
    //     rake, 120-126, and pinch harmonic, 127); 2 round-robins of each stroke (what the reference has: its round-robin
    //     check, November 2026); each note held 4 s in a 5.5 s slot.
    //   - MUTE (palm mute, 0.3.0): velocity sets how muted (the reference's way): its five degrees (Jupiter's Mute Layer
    //     table) at 13 / 38 / 64 / 89 / 115; 8 round-robins of each stroke (Jupiter has 6 up strokes and 10 down strokes;
    //     the import leaves copies out); a palm mute is gone within a second, so each held 1 s in a 1.6 s slot.
    inline constexpr int maxLayers = 6, maxRoundRobins = 8;
    struct Spec
    {
        int layers = 1;
        std::array<int, maxLayers> velocities {};
        int roundRobins = 1;
        double holdSeconds = 1.0;   // how long a take's note is held (the longest it can ring)
        double slotSeconds = 2.0;   // from one take's keyswitches to the next's
        int key = 12;               // the articulation's keyswitch (the reference's factory map)
    };
    const Spec& spec (Articulation);
    int layerFor (Articulation, int velocity) noexcept;   // halfway between the list's velocities

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
        Articulation articulation = Articulation::sustain;
        int note() const noexcept { return noteOf (string, fret); }
        int velocity() const noexcept { return spec (articulation).velocities[(size_t) layer]; }
    };

    std::vector<Take> takes (Articulation, int string);
    double lengthSeconds (Articulation, int string);   // to the end of the last take's slot
    juce::MidiFile toMidi (Articulation, int string);
    juce::String fileName (Articulation, int string);   // "PR7-A Sustain - String 7 (low A)" (no extension)

    // Where an imported take's file goes in a guitar's folder: "<Sustain>/s0_f05_l2_d_r1.wav", and back.
    juce::String takeFileName (Articulation, const Take&);
    struct TakeId { Articulation articulation = Articulation::sustain; int string = 0, fret = 0, layer = 0; Stroke stroke = Stroke::down; int roundRobin = 0; };
    std::optional<TakeId> parseTakeFile (const juce::File&);
}
