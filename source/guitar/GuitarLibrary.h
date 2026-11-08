#pragma once

#include "guitar/RenderList.h"
#include "guitar/NoiseLists.h"
#include "dsp/DiskStreamer.h"

#include <array>
#include <atomic>
#include <memory>

// A GUITAR (PR7-I 0.2.0): the takes imported into one folder (renders::takeFileName), ready to play. Each take keeps
// its first `headSeconds` in memory and streams the rest from disk while it plays (DiskStreamer, as PR7-D's sampled
// kits do), so a whole neck of long notes fits in memory. Our own design and code.
//
// FINDING A TAKE for a note (`find`, on the audio thread, nothing allocated), nearest first:
//   the take itself (the round-robin asked for, else the other); the other stroke; the nearest dynamic (same stroke first); the nearest fret on the same string,
//   played back faster or slower to the note's pitch; the same note on another string. So a guitar with gaps (a string
//   not imported yet) still plays every note, its stand-ins said in `Found::exact`.
namespace guitar
{
    using Streamer = streaming::Streamer;
    using StreamSource = streaming::StreamSource;

    struct Take
    {
        std::vector<float> head;                       // mono, from the start
        std::shared_ptr<const StreamSource> stream;    // the whole file (null: all of it is in `head`)
        double sampleRate = 48000.0;
        juce::int64 frames = 0;                        // its whole length
        juce::File file;
        float loudness = 0.0f;                         // 0.3.0: the RMS of its first 100 ms (to match levels when borrowed)
        static float loudnessOf (const std::vector<float>& head, double sampleRate);
    };

    class Library
    {
    public:
        static constexpr double headSeconds = 0.25;

        // Loads a guitar's folder (any thread). Never null; empty when the folder holds no takes.
        static std::shared_ptr<const Library> load (const juce::File& folder, std::atomic<float>* progress = nullptr,
                                                    const std::atomic<bool>* cancel = nullptr);
        // A guitar from takes in memory (tests): every take whole in `head`.
        void add (Articulation, int string, int fret, int layer, Stroke, int roundRobin, std::shared_ptr<const Take>);

        struct Found
        {
            const Take* take = nullptr;
            double pitchRatio = 1.0;   // how much faster to play it (2: an octave up)
            bool exact = false;
        };
        Found find (Articulation, int string, int fret, int layer, Stroke, int roundRobin = 0) const noexcept;
        // 0.3.0: that take only (null: not imported, or left out as a copy).
        const Take* exact (Articulation, int string, int fret, int layer, Stroke, int roundRobin) const noexcept;

        int count (Articulation, int string) const noexcept;   // takes imported for a string (of takesPerString)
        int roundRobinsOf (Articulation, int string, int fret, int layer, Stroke) const noexcept;   // 0.4.2: the different takes there
        static int takesPerString (Articulation a) noexcept   // sustain 300
        {
            return (numFrets + 1) * renders::spec (a).layers * 2 * renders::spec (a).roundRobins;
        }
        int total() const noexcept;   // the notes' takes (not the noises)

        // 0.6.0, THE NOISES (noises::noiseFileName), each whole in memory (they're short): a pitched release per string
        // and fret (each fret's different recordings), the power chords' by their root, the unpitched noises and fret
        // noises in the order they came. On the audio thread, nothing allocated:
        struct NoiseFound { const Take* take = nullptr; double pitchRatio = 1.0; };
        // The release for a note (a power chord's: by its root), the `turn`th of that fret's recordings; a fret with none
        // borrows the nearest fret on the same string, played at the note's pitch. Null: none on that string.
        NoiseFound release (bool powerChord, int string, int fret, int turn) const noexcept;
        const Take* unpitched (int turn) const noexcept;    // null: none
        const Take* fretNoise (int turn) const noexcept;
        int noiseCount (noises::List, int string = 0) const noexcept;   // (the pitched releases': one string's)
        int noiseTotal() const noexcept;
        void addNoise (noises::List, int string, int fret, std::shared_ptr<const Take>);   // tests (and load)
        bool isEmpty() const noexcept { return total() == 0; }
        const juce::File& getFolder() const noexcept { return folder; }

    private:
        juce::File folder;
        using RoundRobins = std::array<std::shared_ptr<const Take>, renders::maxRoundRobins>;
        using Strokes = std::array<RoundRobins, 2>;
        using Layers = std::array<Strokes, renders::maxLayers>;
        using Frets = std::array<Layers, numFrets + 1>;
        std::array<std::array<Frets, numStrings>, numArticulations> takes;
        using NoiseTakes = std::vector<std::shared_ptr<const Take>>;
        std::array<std::array<NoiseTakes, numFrets + 1>, numStrings> releases, powerReleases;
        NoiseTakes unpitchedNoises, fretNoises;
        const Take* at (int a, int s, int f, int l, int k, int rr) const noexcept;
        const Take* anyLayer (int a, int s, int f, int layer, int stroke, int rr) const noexcept;
    };
}
