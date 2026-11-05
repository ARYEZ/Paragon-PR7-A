#pragma once

#include "dsp/synth/Drums.h"
#include "dsp/synth/Mixer.h"

#include <map>
#include <tuple>
#include <memory>
#include <vector>

// THE SAMPLE ENGINE (PR7-D 0.19.0): a drum played from many recorded hits, as drum software plays its sampled kits
// (inspired by its velocity layers, round-robins and "hit variation"; our own design and code). Never their sounds:
// the user's own samples, from SFZ files (an open, plain-text format many multi-sampled drum kits come in) or from a
// folder of hits.
//
//   - VELOCITY LAYERS: the hits recorded at one loudness, for a range of velocities (soft hits for soft notes...).
//   - ROUND-ROBINS: several hits per layer, so repeated notes don't play the very same sound (the "machine gun").
//   - Each hit may come from several files at once (an SFZ kit's microphones: close, overheads, room); in 0.19.0
//     they're summed into one stereo hit as it loads (each file's own level and pan), as the kit's full mix. 0.20.0
//     will keep them apart, as mic channels.
//
// Choosing a hit (Instrument::choose, on the audio thread, nothing allocated):
//   - the layer for the velocity (or the nearest);
//   - ADJACENT LAYERS: a velocity in the upper or lower part of its layer may take a hit from the layer next to it
//     that way, its level matched to this layer's;
//   - RANDOMIZE: a random hit, never the one just played (off: each in turn);
//   - LAYER LIMIT: only the first N hits of each layer (0: all).
// Velocity to volume (whether a softer note within a layer also plays quieter) is the kit's business (Kit::startHit).
namespace synth::drums
{
    struct Instrument
    {
        struct Hit
        {
            std::shared_ptr<const Sample> sample;   // the hit, its files summed (the whole kit's mix of it)
            juce::StringArray files;                // where it came from (each mic's file)
            // 0.20.0, MIC CHANNELS: a multi-mic kit's hit, each mic's recording apart (`channel`: synth::mixer::Channel,
            // -1: the drum's own close channel). Empty for a single-mic hit (it plays `sample`, as before).
            struct Mic { int channel = -1; std::shared_ptr<const Sample> sample; };
            std::vector<Mic> mics;
        };
        struct Layer
        {
            int lo = 1, hi = 127;                   // velocities
            std::vector<Hit> hits;
            float level = 1.0f;                     // its hits' average loudness (the first 100 ms, RMS)
        };
        std::vector<Layer> layers;                  // by velocity, lowest first
        juce::String name;

        // Where it came from, to load it again (presets and projects keep this, not the audio).
        enum class Kind { sfz, folder, drumGizmo, samplePack };   // 0.27.0: a DrumGizmo kit's instrument(s); 0.33.0: a sample pack's
        Kind kind = Kind::folder;
        juce::File source;                          // the .sfz file, or the folder
        int note = -1;                              // SFZ: the note whose regions it is (-1: all of the file's)

        int numHits() const noexcept;
        bool streamed() const noexcept;              // 0.28.0: its hits play from disk past their heads
        double longestSeconds() const noexcept;      // its longest hit, in seconds (streamed: whole)
        bool hasMics() const noexcept;               // its hits come in mic channels (0.20.0)
        juce::Array<int> micChannels() const;        // which ones
        int layerFor (int velocity) const noexcept;   // 1-127 -> the layer's index (-1: none)

        struct Choice { const Sample* sample = nullptr; const std::vector<Hit::Mic>* mics = nullptr; float gain = 1.0f; int layer = -1, hit = -1; };
        Choice choose (int velocity, bool randomize, bool adjacent, int layerLimit, HitMemory&) const noexcept;
    };

    namespace instruments
    {
        // A folder of hits (WAV, AIFF, FLAC...) as one instrument. Names that number their layers and round-robins
        // ("v3", "vel3", "rr2", "_3_2") are used as they say; otherwise the hits are sorted by loudness and shared out
        // into layers, as many as the square root of the number of hits (at most 16), quietest first: our own rule.
        // 0.20.0: a folder with no hits of its own but a subfolder per mic ("Kick In", "OH", "Room"...) is a multi-mic
        // instrument: each hit's file found by name in every mic's folder, its loudness that of them summed.
        std::shared_ptr<const Instrument> fromFolder (const juce::File& folder);
        // 0.50.0: a folder laid out as a multi-mic instrument (no hits of its own, two or more mic folders).
        bool hasMicFolders (const juce::File& folder);
        // 0.29.0: one audio file as an instrument of one hit (a drum's layer).
        std::shared_ptr<const Instrument> fromFile (const juce::File& file);

        // An SFZ file: its regions, by note. Each note's regions as an instrument: their velocity ranges as layers,
        // their round-robins (seq_position, lorand) as hits, regions sharing all of those (a kit's mics) summed into
        // one hit. Read: <control> default_path, <global> <master> <group> <region> (each passing its settings on),
        // #define, #include, sample, key / lokey / hikey (numbers or names, c4 = 60), lovel / hivel, seq_length /
        // seq_position, lorand / hirand, volume, amplitude, pan, offset, trigger (release regions left out). Not
        // read: anything controller-driven (a kit's mic faders), filters, envelopes, loops, tuning.
        struct Sfz
        {
            std::map<int, std::shared_ptr<const Instrument>> notes;
            juce::StringArray problems;            // files that didn't load, opcodes skipped...
        };
        // onlyNote: -1 each note its own instrument; a note: only that one; wholeFile (-2): every region in one
        // instrument whatever its keys (a file of one drum spread over the keyboard), under note 0.
        static constexpr int wholeFile = -2;
        Sfz fromSfz (const juce::File& file, int onlyNote = -1);
        // Every note an SFZ file plays, and how many regions each, without loading any audio (to map a kit first).
        std::map<int, int> sfzNotes (const juce::File& file);

        // A hit's loudness: the RMS of its first 100 ms (both channels).
        float loudness (const Sample&);
    }
}
