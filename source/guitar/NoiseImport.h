#pragma once

#include "guitar/NoiseLists.h"

#include <atomic>

// IMPORT A NOISE RENDER (PR7-I 0.6.0): the owner's render of one noise list (NoiseLists.h) cut into its noises and saved
// in the guitar's folder (noises::noiseFileName). Our own design and code, the note import's way (RenderImport.h):
//
//   - LINED UP: the render's onsets (the first sample at a tenth of the local peak, after a stretch of quiet) are matched
//     against where the list puts its noises (a release starts at its note-off, fret noise at its key), trying every
//     offset an early onset suggests from 3 s early to 1 s late; the one that lines most onsets up within 20 ms wins,
//     then the median of their differences. A render bounced from bar 1 measures about 0 (the owner's noise check: 0.4 ms).
//   - EACH NOISE: from 5 ms before it, up to `cutSeconds` (fret noise `fretCutSeconds`), its silent end trimmed (60 dB
//     under its peak, plus 10 ms), a 1 ms fade in and a 30 ms fade out; mono, 24-bit at the render's rate. A hit with
//     nothing above -70 dBFS is silent (left out: the unpitched list's most often).
//   - CHECKED, so a render made the wrong way never fills the guitar with the wrong sounds: the notes heard (over 40% of
//     the render loud: the notes' Volume wasn't down) refuses it; so do most releases (or fret noises) silent, and the
//     unpitched list with a noise on most hits (the pitched releases weren't turned down).
//   - COPIES left out (rrcheck::sameAs: 0.999 alike): each fret's repeats for the releases, all the hits for the unpitched
//     noises and fret noise; what's left numbered in the order it came (fret noise: the reference's 13 in turn).
// Saving replaces what that list imported before.
namespace guitar::noises
{
    inline constexpr double cutSeconds = 0.6, fretCutSeconds = 0.5;
    inline constexpr double preSeconds = 0.005;

    struct NoiseReport
    {
        juce::String error;            // empty: imported
        int written = 0, silent = 0, copies = 0, hits = 0;
        double offsetSeconds = 0.0;
        bool ok() const noexcept { return error.isEmpty(); }
        juce::String summary (List) const;
    };

    NoiseReport importAudio (const float* audio, int numSamples, double sampleRate, List, int string, const juce::File& guitarFolder,
                             std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
    NoiseReport importFile (const juce::File& render, List, int string, const juce::File& guitarFolder,
                            std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}
