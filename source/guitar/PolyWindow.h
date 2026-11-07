#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// POLY WINDOW (PR7-I 0.4.0): notes played within a few milliseconds of each other are fretted together as one chord, as
// the owner's reference guitar instrument's "poly input latency" does: our own code. Every MIDI event is delayed by the
// window (the processor reports it as latency, so the DAW plays the track that much earlier and the notes still land
// on time); when a note's turn comes, the notes that arrived within the window after it are pulled forward to the same
// sample, so the guitar sees them as a chord. Keyswitches pulled with them go first, so a keyswitch a hair after its
// note still applies to it. A note-on isn't pulled past an earlier note-off of the same key (a re-struck note stays
// after its own release). A window of 0 passes everything straight through.
namespace guitar
{
    class PolyWindow
    {
    public:
        void prepare (int capacity = 4096);
        void reset() noexcept;

        // Takes this block's events (`in`, numSamples long) and puts the ones due now into `out` (cleared first), at
        // their sample within the block. `windowSamples`: the window (and the delay).
        void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, int windowSamples) noexcept;

        static bool isKeyswitch (int note) noexcept;   // the guitar's keyswitch keys (below A1, and C7 up)

    private:
        struct Event
        {
            juce::int64 time = 0;
            juce::uint8 data[3] {};
            int size = 0;
            bool done = false;
        };
        std::vector<Event> queue;
        int count = 0;
        juce::int64 now = 0;
        bool isNoteOn (const Event& e) const noexcept { return e.size == 3 && (e.data[0] & 0xf0) == 0x90 && e.data[2] > 0; }
        bool isNoteOff (const Event& e) const noexcept
        {
            return e.size == 3 && ((e.data[0] & 0xf0) == 0x80 || ((e.data[0] & 0xf0) == 0x90 && e.data[2] == 0));
        }
    };
}
