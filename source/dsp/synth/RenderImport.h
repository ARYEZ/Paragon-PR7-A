// IMPORT A RENDER (PR7-D 0.51.0): a drum recorded out of another drum program a mic at a time (its channels bounced as
// separate files: "Snare Top.wav", "OH.wav", "Room.wav"...), with the MIDI that played it, made into a multi-mic
// instrument of velocity layers and round robins, to play as a drum's layer.
//
// How: every mic's file cut at the same moments (each hit's start from the MIDI, or found by listening when there's no
// MIDI), so the mics stay in time with each other; each hit up to the next one (its tail's silence trimmed, the cut
// faded); the hits sorted by note (one instrument per note: a snare's centre and rimshot in one render) and by
// velocity (each velocity played a layer, its repeats round robins). Written as a folder per note, a folder per mic
// inside, files named "hit_vel<velocity>_rr<n>.wav", which the kit's folder loader reads as a multi-mic instrument
// with those velocities (instruments::fromFolder).
//
// Our own design: the owner bounces their renders from the other program; its library files are never read.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>
#include <vector>

namespace synth::drums::renderimport
{
    struct Mic
    {
        juce::File file;
        int channel = -1;          // the mixer's mic channel its name says (-1: none: the drum's own, if it's the only file)
        juce::String name;         // the file's name (its folder's name in what's written)
    };
    struct Hit
    {
        double seconds = 0.0;      // where it starts in the files
        int note = -1;             // the MIDI's note (-1: no MIDI)
        int velocity = 100;        // the MIDI's velocity (no MIDI: 1-127 by loudness)
    };
    struct Plan
    {
        std::vector<Mic> mics;
        std::vector<Hit> hits;            // in time order
        double sampleRate = 0.0;
        juce::int64 length = 0;           // the shortest file's, in samples
        juce::File midi;                  // the MIDI used (none: the hits were found by listening)
        double shiftSeconds = 0.0;        // how far the audio was from the MIDI's times (a bounce started early or late)
        int offTime = 0;                  // hits whose sound didn't start near where the MIDI says
        juce::StringArray problems, leftOut;
        bool ok() const noexcept { return ! mics.empty() && ! hits.empty() && sampleRate > 0.0; }
    };

    // The folder's audio files as mics (a whole-kit mix "Main" / "Master" / "Mix" left out when there are mics), and its
    // hits: from `midi` (none given: a .mid / .midi in the folder), else found in the files. Reads the files' names and
    // the MIDI; the audio only to find (or check) the hits.
    Plan scan (const juce::File& folder, const juce::File& midi = {});

    // What was written for one note: its folder, how many velocity layers and round robins.
    struct Written
    {
        int note = -1;
        juce::File folder;
        int layers = 0, rounds = 0, hits = 0;
        std::vector<int> velocities;   // each layer's (rising)
    };
    // Cuts the hits out of every mic and writes them under `into` (a folder per note named "<name> - <note>", or just
    // `into` with no MIDI). `progress` (0 to 1) is called as it goes; returning false stops it.
    std::vector<Written> write (const Plan&, const juce::File& into, const juce::String& name,
                                const std::function<bool (double)>& progress = {});

    // The longest a hit is kept (a long cymbal's tail beyond it is cut and faded).
    constexpr double maxHitSeconds = 8.0;
    // Before each hit's start, kept (its very first moment).
    constexpr double preRollSeconds = 0.001;
}
