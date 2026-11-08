#pragma once

#include "guitar/RenderList.h"

#include <atomic>

// IMPORT A RENDER (PR7-I 0.2.0): the owner's render of one render-list file (one string, one articulation) cut into its
// takes and saved in the guitar's folder, one file per take (see renders::takeFileName). Our own design and code.
//
//   - WHERE THE NOTES ARE: the DAW and the reference instrument start each note a little late (latency, the pick's
//     noise before the note). The first dozen takes' onsets (the first sample at a tenth of the take's peak) are
//     measured against where the list put them; the median is the render's OFFSET, used for every take.
//   - EACH TAKE: from `prerollSeconds` before its onset (the pick's noise kept) for its note's length and a little more,
//     a 1 ms fade in and a 30 ms fade out; mixed to mono (a DI is one channel); saved 24-bit at the render's rate.
//   - CHECKED: each take's pitch is measured (an autocorrelation a moment after the pick) against the note the list
//     asked for. More than 30% out (over 40 cents) or a render too short means the wrong file (another string's, or
//     not rendered to the end): nothing is saved, and the report says so. A few out are listed but saved.
//   - COPIES (0.3.0): the round-robins of each note, dynamic and stroke compared (rrcheck::sameAs): a take that's the same
//     recording as an earlier one (the instrument has fewer round-robins than the list asks for) isn't saved, so the
//     guitar never takes a copy for variety; the report says how many different takes there were.
namespace guitar::renders
{
    inline constexpr double prerollSeconds = 0.03;   // (playback starts takeStartSeconds, 15 ms, in: Guitar.h)
    inline constexpr double tailSeconds = 0.08;     // kept after the note's end: the string being stopped
    inline constexpr double centsTolerance = 40.0;

    struct ImportReport
    {
        juce::String error;                         // empty: imported
        int written = 0, silent = 0;
        int copies = 0;                             // takes left out: the same recording as another round-robin
        int fewestRoundRobins = 0;                  // the fewest different takes any note's stroke had
        double offsetSeconds = 0.0;
        juce::StringArray outOfTune;                // "fret 5, soft, down: +62 cents"
        bool ok() const noexcept { return error.isEmpty(); }
        juce::String summary() const;               // one line for the page
    };

    // The pitch (Hz) of `numSamples` of `audio` near `expectedHz` (within half an octave either way); 0: none found.
    double pitchOf (const float* audio, int numSamples, double sampleRate, double expectedHz);

    // Cuts `audio` (mono, at `sampleRate`) as the list for (articulation, string) says, into `guitarFolder`.
    ImportReport importAudio (const float* audio, int numSamples, double sampleRate, Articulation, int string,
                              const juce::File& guitarFolder, std::atomic<float>* progress = nullptr,
                              const std::atomic<bool>* cancel = nullptr);
    // The same from an audio file (WAV, AIFF, FLAC).
    ImportReport importFile (const juce::File& render, Articulation, int string, const juce::File& guitarFolder,
                             std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}
