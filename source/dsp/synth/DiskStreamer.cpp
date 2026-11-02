#include "dsp/synth/DiskStreamer.h"

namespace synth::drums
{
    class Streamer::Thread final : public juce::Thread
    {
    public:
        explicit Thread (Streamer& s) : juce::Thread ("PR7-D disk streaming"), owner (s) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                auto busy = false;
                for (auto& stream : owner.streams)
                {
                    const auto state = stream->state.load (std::memory_order_acquire);
                    if (state == 3)
                    {
                        // Freed by the audio thread: ready for the next hit.
                        const std::scoped_lock lock (stream->fillLock);
                        stream->source = nullptr;
                        stream->attached.store (0);
                        stream->state.store (0, std::memory_order_release);
                    }
                    else if (state == 2)
                        busy = owner.fill (*stream, false) || busy;
                }
                // Nothing to read: rest a little (a new hit plays its head, 0.2 s, while the first read comes).
                if (! busy)
                    wait (2);
            }
        }

    private:
        Streamer& owner;
    };

    Streamer::Streamer()
    {
        formats.registerBasicFormats();
        for (int k = 0; k < maxStreams; ++k)
        {
            auto s = std::make_unique<Stream>();
            s->ring.assign ((size_t) maxOutputs * 2 * ringFrames, 0.0f);
            streams.push_back (std::move (s));
        }
        thread = std::make_unique<Thread> (*this);
        thread->startThread (juce::Thread::Priority::high);
    }

    Streamer::~Streamer()
    {
        thread->stopThread (2000);
    }

    int Streamer::claim (const StreamSource& source) noexcept
    {
        for (int k = 0; k < maxStreams; ++k)
        {
            auto& s = *streams[(size_t) k];
            auto expected = 0;
            if (! s.state.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
                continue;
            s.source = &source;
            s.written.store (source.headFrames, std::memory_order_relaxed);
            for (auto& r : s.read)
                r.store (source.headFrames, std::memory_order_relaxed);
            s.attached.store (0, std::memory_order_relaxed);
            s.state.store (2, std::memory_order_release);
            return k;
        }
        return -1;
    }

    void Streamer::attach (int stream, int output) noexcept
    {
        if (stream >= 0 && stream < maxStreams && output >= 0 && output < maxOutputs)
            streams[(size_t) stream]->attached.fetch_or (1u << output);
    }

    void Streamer::consumed (int stream, int output, juce::int64 frame) noexcept
    {
        if (stream >= 0 && stream < maxStreams && output >= 0 && output < maxOutputs)
            streams[(size_t) stream]->read[(size_t) output].store (frame, std::memory_order_release);
    }

    float Streamer::sample (int stream, int output, int channel, juce::int64 frame, bool waitForDisk) noexcept
    {
        if (stream < 0 || stream >= maxStreams || output < 0 || output >= maxOutputs)
            return 0.0f;
        auto& s = *streams[(size_t) stream];
        const auto* source = s.source;
        if (source == nullptr || frame < source->headFrames || frame >= source->length)
            return 0.0f;
        auto written = s.written.load (std::memory_order_acquire);
        if (frame >= written)
        {
            if (! waitForDisk)
            {
                underruns.fetch_add (1, std::memory_order_relaxed);
                return 0.0f;
            }
            // Bouncing: read it now (the disk thread may be at it too: the stream's lock keeps them apart).
            while (frame >= (written = s.written.load (std::memory_order_acquire)) && written < source->length)
                if (! fill (s, false))
                    break;
            if (frame >= written)
                return 0.0f;
        }
        const auto index = (size_t) ((frame - source->headFrames) & (ringFrames - 1));
        return s.ring[((size_t) output * 2 + (size_t) juce::jlimit (0, 1, channel)) * ringFrames + index];
    }

    void Streamer::release (const std::array<bool, maxStreams>& inUse) noexcept
    {
        for (int k = 0; k < maxStreams; ++k)
            if (! inUse[(size_t) k])
            {
                auto expected = 2;
                streams[(size_t) k]->state.compare_exchange_strong (expected, 3, std::memory_order_acq_rel);
            }
    }

    int Streamer::getActiveStreams() const noexcept
    {
        int n = 0;
        for (const auto& s : streams)
            n += s->state.load() == 2 ? 1 : 0;
        return n;
    }

    juce::AudioFormatReader* Streamer::readerFor (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (const auto it = readers.find (key); it != readers.end())
            return it->second.get();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return nullptr;
        // At most 64 files open: the one used longest ago closes.
        if (readerOrder.size() >= 64)
        {
            readers.erase (readerOrder.front());
            readerOrder.erase (readerOrder.begin());
        }
        readerOrder.push_back (key);
        return (readers[key] = std::move (reader)).get();
    }

    bool Streamer::fill (Stream& s, bool)
    {
        const std::scoped_lock lock (s.fillLock);
        if (s.state.load (std::memory_order_acquire) != 2 || s.source == nullptr)
            return false;
        const auto& source = *s.source;
        const auto written = s.written.load (std::memory_order_acquire);
        if (written >= source.length)
            return false;
        // Room: the ring holds ringFrames past the slowest of the voices playing it (none yet: from the head).
        auto slowest = written;
        const auto attached = s.attached.load (std::memory_order_acquire);
        auto any = false;
        for (int o = 0; o < maxOutputs; ++o)
            if ((attached & (1u << o)) != 0)
            {
                const auto r = s.read[(size_t) o].load (std::memory_order_acquire);
                slowest = any ? juce::jmin (slowest, r) : r;
                any = true;
            }
        if (! any)
            slowest = source.headFrames;
        const auto room = slowest + ringFrames - written;
        const auto frames = (int) juce::jmin<juce::int64> (chunkFrames, source.length - written, room);
        if (frames <= 0)
            return false;

        {
            const std::scoped_lock readerLock (readersLock);
            auto* reader = readerFor (source.file);
            if (reader == nullptr)
            {
                // The file's gone: silence from here (the hit ends at its head's end, in effect).
                s.written.store (source.length, std::memory_order_release);
                return false;
            }
            const auto channels = (int) reader->numChannels;
            if (chunk.getNumChannels() < channels || chunk.getNumSamples() < chunkFrames)
                chunk.setSize (juce::jmax (channels, chunk.getNumChannels()), chunkFrames, false, false, true);
            chunk.clear();
            reader->read (&chunk, 0, frames, written, true, true);
            // Into each mic's ring, as the recipe says.
            const auto start = (size_t) ((written - source.headFrames) & (ringFrames - 1));
            for (int o = 0; o < source.numOutputs && o < maxOutputs; ++o)
                for (int c = 0; c < 2; ++c)
                {
                    auto* ring = s.ring.data() + ((size_t) o * 2 + (size_t) c) * ringFrames;
                    for (int i = 0; i < frames; ++i)
                        ring[(start + (size_t) i) & (ringFrames - 1)] = 0.0f;
                }
            for (const auto& part : source.parts)
            {
                if (part.fileChannel < 0 || part.fileChannel >= channels || part.output < 0 || part.output >= maxOutputs)
                    continue;
                const auto* from = chunk.getReadPointer (part.fileChannel);
                auto* ring = s.ring.data() + ((size_t) part.output * 2 + (size_t) juce::jlimit (0, 1, part.channel)) * ringFrames;
                for (int i = 0; i < frames; ++i)
                    ring[(start + (size_t) i) & (ringFrames - 1)] += from[i] * part.gain;
            }
        }
        s.written.store (written + frames, std::memory_order_release);
        return true;
    }
}
