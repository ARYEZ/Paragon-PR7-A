#pragma once

#include "dsp/synth/Instrument.h"

#include <map>
#include <memory>
#include <vector>

// DRUMGIZMO KITS (PR7-D 0.27.0): multi-mic drum kits in the open DrumGizmo format (drumgizmo.org), read from where the
// user unzipped them. Free kits recorded in real studios come in it (the CrocellKit, a metal kit, CC-BY 4.0; the
// DRSKit, the MuldjordKit...); we never ship their audio: the user downloads a kit, PR7-D loads it.
//
// The format (as the kits' own files show it): a kit file (<drumkit>, e.g. "CrocellKit_full.xml") lists the mic
// channels and the instruments (each its own file); an instrument file lists its hits (<sample power="...">), each hit
// one multichannel WAV with a file channel per mic (<audiofile channel="SnareTop" file="..." filechannel="13"/>).
// "power" is how hard it was hit (the kit's own measure: higher is harder). A midimap file next to the kit file
// ("Midimap_full.xml" for "<Kit>_full.xml") gives each instrument its MIDI note.
//
// Here (our own design):
//   - a hit's mics go into THE MIXER's channels by their names (KDrumInside: KICK IN, OHLeft / OHRight: OVERHEADS'
//     sides, OHCenter: both, AmbLeft / AmbRight: ROOM...), a left / right pair into one stereo mic;
//   - the hits, sorted by power, are shared out into velocity layers, each layer's velocities from its hits' power
//     (on a log scale between the softest and the hardest), each layer's hits its round-robins;
//   - LIGHT (the default): up to 8 layers of 2 hits, each hit at most 4.5 s long (cymbals 5 s in 6 layers; tails faded out), the mics
//     that only hear it as bleed (another drum's close mic) at most 1 s, mics that hardly hear it at all left out; so a
//     whole kit fits in well under a gigabyte of memory. FULL: every hit, every mic, at full length (the CrocellKit:
//     about 6 GB).
namespace synth::drums::drumgizmo
{
    struct KitInfo
    {
        juce::String name, description;
        double sampleRate = 48000.0;
        juce::StringArray channels;                         // the kit's mic channels, in its order
        struct Entry { juce::String name; juce::File file; int note = -1; };
        std::vector<Entry> instruments;                     // in the kit file's order, each with its midimap note
        bool valid() const noexcept { return ! instruments.empty(); }
    };
    // Whether a file is a DrumGizmo kit file (its root element <drumkit>).
    bool isKit (const juce::File&);
    // The kit file's channels and instruments, and their notes from the midimap beside it.
    KitInfo read (const juce::File& kitFile);

    // A kit channel's mixer channel, and its side (-1 left, 1 right, 0 mono / centre): our rules from its name.
    struct MicPlace { int channel = -1; int side = 0; };
    MicPlace micFor (const juce::String& kitChannel);

    struct Trim
    {
        bool full = false;
        int maxLayers = 8, hitsPerLayer = 2;
        double maxSeconds = 4.5, bleedSeconds = 1.0;
        static Trim light() { return {}; }
        static Trim everything() { Trim t; t.full = true; t.maxLayers = 16; t.hitsPerLayer = 1000; t.maxSeconds = 120.0; t.bleedSeconds = 120.0; return t; }
        // 0.28.0: STREAMED: everything, as FULL, with each hit's first `headSeconds` in memory and the rest read from
        // disk while it plays (DiskStreamer.h).
        bool stream = false;
        double headSeconds = 0.2;
        static Trim streamed() { auto t = everything(); t.stream = true; return t; }
    };

    // One drum from the kit: the named instruments' hits together (a double kick's two feet as one drum, alternating
    // in each layer), trimmed. Null if none loaded; what went wrong added to `problems`.
    std::shared_ptr<const Instrument> load (const juce::File& kitFile, const juce::StringArray& instruments, const Trim&,
                                            juce::StringArray& problems);

    // Which of the kit's instruments play which of our slots: our rules by name first (a kick, a snare's rimshot, a
    // stopped crash left out...), the midimap's note (General MIDI) choosing among a kind's slots (the first crash
    // CRASH 1 if its note says so...), the rest of a kind into its free slots in turn. Both feet of a kick go to the
    // kick. `unused`: the instruments that found no slot.
    struct Assignment
    {
        std::map<int, juce::StringArray> slots;
        juce::StringArray unused;
    };
    Assignment assign (const KitInfo&);
}
