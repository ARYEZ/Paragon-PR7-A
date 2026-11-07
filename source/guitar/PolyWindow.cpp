#include "guitar/PolyWindow.h"
#include "guitar/Guitar.h"

namespace guitar
{
    bool PolyWindow::isKeyswitch (int note) noexcept
    {
        return note < lowestNote || note >= keyswitch::pickUp;
    }

    void PolyWindow::prepare (int capacity)
    {
        queue.assign ((size_t) juce::jmax (16, capacity), {});
        reset();
    }

    void PolyWindow::reset() noexcept
    {
        count = 0;
        now = 0;
    }

    void PolyWindow::process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, int windowSamples) noexcept
    {
        out.clear();
        windowSamples = juce::jmax (0, windowSamples);
        for (const auto metadata : in)
        {
            const auto size = metadata.numBytes;
            if (size < 1 || size > 3 || metadata.samplePosition < 0 || metadata.samplePosition >= numSamples)
                continue;   // (system exclusive and the like: nothing the guitar uses)
            if (count == (int) queue.size())
                break;      // full (thousands of events within one window): the rest are dropped
            auto& e = queue[(size_t) count++];
            e = {};
            e.time = now + metadata.samplePosition;
            e.size = size;
            std::copy (metadata.data, metadata.data + size, e.data);
        }

        const auto end = now + numSamples;
        const auto emit = [&] (const Event& e, juce::int64 at)
        {
            out.addEvent (e.data, e.size, (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples - 1, at - now));
        };
        for (int i = 0; i < count; ++i)
        {
            auto& e = queue[(size_t) i];
            if (e.done)
                continue;
            const auto due = e.time + windowSamples;
            if (due >= end)
                break;   // (the queue is in time order)
            e.done = true;
            if (windowSamples == 0 || ! isNoteOn (e) || isKeyswitch (e.data[1]))
            {
                emit (e, due);
                continue;
            }
            // A note: the notes (and keyswitches) arriving within the window after it come along, keyswitches first.
            for (int pass = 0; pass < 3; ++pass)
            {
                if (pass == 1)
                {
                    emit (e, due);
                    continue;
                }
                for (int j = i + 1; j < count; ++j)
                {
                    auto& other = queue[(size_t) j];
                    if (other.time >= due)
                        break;
                    if (other.done || ! isNoteOn (other) || isKeyswitch (other.data[1]) != (pass == 0))
                        continue;
                    auto releasedFirst = false;   // a note-off of this key still waiting before it: leave it in its place
                    for (int k = i + 1; k < j && ! releasedFirst; ++k)
                        releasedFirst = ! queue[(size_t) k].done && isNoteOff (queue[(size_t) k]) && queue[(size_t) k].data[1] == other.data[1];
                    if (releasedFirst)
                        continue;
                    other.done = true;
                    emit (other, due);
                }
            }
        }
        // Drop what's gone out.
        auto kept = 0;
        for (int i = 0; i < count; ++i)
            if (! queue[(size_t) i].done)
                queue[(size_t) kept++] = queue[(size_t) i];
        count = kept;
        now = end;
    }
}
