#pragma once

#include "guitar/RenderList.h"

// THE NOISE LISTS (PR7-I 0.6.0): what PR7-I needs from the reference to play its noises, as MIDI files to render
// through it, the note lists' way (renders::), so the import knows where each noise lands. Designed from the owner's
// noise check (November 2026: "PR7-I Noise Check.mid" rendered twice, normally and with the notes' Volume at minimum):
//   - every note-off of a sustained note plays a PITCHED RELEASE at the note's pitch (the low A's at ~55 Hz, the D
//     string's 7th fret at ~217 Hz): instantly, ~25 dB under the note, gone in ~0.3 s; nearly one recording, 16 identical
//     notes giving near-copies (similarity 0.93-0.98); velocity changes it ~3 dB at most; notes held 2 s got none (held
//     0.1-1 s all did); palm mutes none;
//   - POWER CHORDS their own release (~5 dB louder than a single note's);
//   - now and then an UNPITCHED noise (a scrape: energy 200 Hz - 4 kHz) mixed in (2 of 16);
//   - FRET NOISE on F#0 (its own keyswitch): ~0.1 s, 13 different recordings played in turn whatever the velocity,
//     velocity only its level (~8 dB from 127 down to 40).
// The Volume knob silences an articulation's notes completely (the render without them measured below -100 dB), so
// rendered with the notes' Volume (and, for the pitched lists, the unpitched release's) at minimum, each list holds
// its noises alone. THE LAYOUTS ARE FIXED once sent to the owner (November 2026): never change them, or their renders
// won't import.
//
//   - PITCHED RELEASE, one list per string: frets 0-24, each 4 times, sustain (C-1) at velocity 85, held 0.5 s.
//   - POWER CHORD RELEASE, one list: the root forced on strings 7, 6 and 5 (indices 0-2), frets 0-15, each 3 times,
//     the factory power-chord sustain (D#-1), velocity 85, held 0.5 s.
//   - UNPITCHED NOISE, one list: every string, frets 0 / 7 / 12 / 19, each 12 times, sustain, velocity 85, held 0.5 s
//     (the noise comes now and then: the import keeps the hits that have one); 1.25 s slots as the releases'.
//   - FRET NOISE, one list: F#0 (30) 32 times at velocity 127, 0.05 s long, a second apart (13 recordings in turn,
//     so each comes at least twice).
// Every note a down stroke (C#7 before it, as the note lists); each file starts with legato off (D#0) and, but for
// fret noise, every hit gets its force-string key and articulation key 0.25 s before it.
namespace guitar::noises
{
    enum class List { pitchedRelease, powerChordRelease, unpitched, fretNoise };
    inline constexpr int numLists = 4;

    inline constexpr int fretNoiseKey = 30;          // F#0
    inline constexpr int powerChordKey = 15;         // D#-1, the reference's factory power-chord sustain
    inline constexpr int velocity = 85, fretNoiseVelocity = 127;
    inline constexpr double holdSeconds = 0.5;

    struct Hit
    {
        int string = 0, fret = 0, repeat = 0;
        int note = 0;               // what's played (the root for a power chord; F#0 for fret noise)
        double noteSeconds = 0.0;   // when it starts in the file
        double offSeconds = 0.0;    // its note-off: where a release starts
    };

    struct Layout
    {
        int repeats = 1;
        double slotSeconds = 1.0;   // from one hit's keyswitches to the next's
        double holdSeconds = 0.5;
    };
    Layout layout (List);
    bool perString (List) noexcept;                  // one list per string (the pitched releases) or one in all

    std::vector<Hit> hits (List, int string = 0);    // (string: the pitched releases' list)
    double lengthSeconds (List, int string = 0);
    juce::MidiFile toMidi (List, int string = 0);
    juce::String fileName (List, int string = 0);    // "PR7-A Release - String 7 (low A)", "PR7-A Fret Noise"...

    // The RENDERS page's NOISES rows: 0-6 each string's pitched releases (the low A first), 7 power-chord releases,
    // 8 unpitched noises, 9 fret noise.
    inline constexpr int numRows = numStrings + 3;
    struct RowList { List list = List::pitchedRelease; int string = 0; };
    RowList rowList (int row) noexcept;
    juce::String rowName (int row);                  // "String 7 (low A)", "Power chords", "Unpitched", "Fret noise"

    // Where an imported noise goes in a guitar's folder (0.6.0): "Release/s0_f05_r0.wav" (pitched release: string,
    // fret, which of its different recordings), "PowerRelease/s1_f03_r0.wav" (the root's string and fret),
    // "Unpitched/n007.wav", "FretNoise/n04.wav" (numbered in the order they came: the reference plays them in turn).
    juce::String folderName (List);
    juce::String noiseFileName (List, int string, int fret, int index);
    struct NoiseId { List list = List::pitchedRelease; int string = 0, fret = 0, index = 0; };
    std::optional<NoiseId> parseNoiseFile (const juce::File&);
}
