#pragma once

#include "guitar/RenderList.h"
#include "dsp/synth/DiskStreamer.h"

#include <array>
#include <atomic>
#include <memory>

// A GUITAR (PR7-I 0.2.0): the takes imported into one folder (renders::takeFileName), ready to play. Each take keeps
// its first `headSeconds` in memory and streams the rest from disk while it plays (DiskStreamer, as PR7-D's sampled
// kits do), so a whole neck of long notes fits in memory. Our own design and code.
//
// FINDING A TAKE for a note (`find`, on the audio thread, nothing allocated), nearest first:
//   the take itself; the other stroke; the nearest dynamic (same stroke first); the nearest fret on the same string,
//   played back faster or slower to the note's pitch; the same note on another string. So a guitar with gaps (a string
//   not imported yet) still plays every note, its stand-ins said in `Found::exact`.
namespace guitar
{
    using Streamer = synth::drums::Streamer;
    using StreamSource = synth::drums::StreamSource;

    struct Take
    {
        std::vector<float> head;                       // mono, from the start
        std::shared_ptr<const StreamSource> stream;    // the whole file (null: all of it is in `head`)
        double sampleRate = 48000.0;
        juce::int64 frames = 0;                        // its whole length
        juce::File file;
    };

    class Library
    {
    public:
        static constexpr double headSeconds = 0.25;

        // Loads a guitar's folder (any thread). Never null; empty when the folder holds no takes.
        static std::shared_ptr<const Library> load (const juce::File& folder, std::atomic<float>* progress = nullptr,
                                                    const std::atomic<bool>* cancel = nullptr);
        // A guitar from takes in memory (tests): every take whole in `head`.
        void add (Articulation, int string, int fret, int layer, Stroke, std::shared_ptr<const Take>);

        struct Found
        {
            const Take* take = nullptr;
            double pitchRatio = 1.0;   // how much faster to play it (2: an octave up)
            bool exact = false;
        };
        Found find (Articulation, int string, int fret, int layer, Stroke) const noexcept;

        int count (Articulation, int string) const noexcept;   // takes imported for a string (of 150)
        int total() const noexcept;
        bool isEmpty() const noexcept { return total() == 0; }
        const juce::File& getFolder() const noexcept { return folder; }

    private:
        juce::File folder;
        using Strokes = std::array<std::shared_ptr<const Take>, 2>;
        using Layers = std::array<Strokes, renders::numLayers>;
        using Frets = std::array<Layers, numFrets + 1>;
        std::array<std::array<Frets, numStrings>, numArticulations> takes;
        const Take* at (int a, int s, int f, int l, int k) const noexcept { return takes[(size_t) a][(size_t) s][(size_t) f][(size_t) l][(size_t) k].get(); }
        const Take* anyLayer (int a, int s, int f, int layer, int stroke) const noexcept;
    };
}
