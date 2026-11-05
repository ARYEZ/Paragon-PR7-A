#include "dsp/synth/RenderImport.h"
#include "dsp/synth/Mixer.h"

namespace synth::drums::renderimport
{
    namespace
    {
        juce::AudioFormatManager& formats()
        {
            static juce::AudioFormatManager manager;
            static const bool registered = [] { manager.registerBasicFormats(); return true; }();
            juce::ignoreUnused (registered);
            return manager;
        }
        const juce::String audioWildcard ("*.wav;*.aif;*.aiff;*.flac");

        // A bounce of everything (the main mix), not a mic: left out when there are mics.
        bool isWholeMix (const juce::String& name)
        {
            const auto n = name.toLowerCase();
            for (const auto* w : { "main", "master", "mix", "stereo out", "full kit", "output" })
                if (n.contains (w))
                    return true;
            return false;
        }

        using Readers = std::vector<std::unique_ptr<juce::AudioFormatReader>>;

        // Each 1 ms frame's loudest sample across every mic (a mic turned over can't cancel another).
        std::vector<float> framePeaks (const Readers& readers, juce::int64 length, int frame)
        {
            std::vector<float> peaks ((size_t) (length / frame + 1), 0.0f);
            constexpr int chunk = 1 << 16;
            juce::AudioBuffer<float> buffer;
            for (const auto& reader : readers)
                for (juce::int64 at = 0; at < length; at += chunk)
                {
                    const auto n = (int) juce::jmin ((juce::int64) chunk, length - at);
                    buffer.setSize ((int) reader->numChannels, n, false, false, true);
                    reader->read (&buffer, 0, n, at, true, true);
                    for (int c = 0; c < buffer.getNumChannels(); ++c)
                    {
                        const auto* x = buffer.getReadPointer (c);
                        for (int i = 0; i < n; ++i)
                        {
                            auto& p = peaks[(size_t) ((at + i) / frame)];
                            p = juce::jmax (p, std::abs (x[i]));
                        }
                    }
                }
            return peaks;
        }

        // Every mic's samples over [from, from + n), as each sample's loudest across them.
        std::vector<float> combined (const Readers& readers, juce::int64 from, int n)
        {
            std::vector<float> c ((size_t) juce::jmax (0, n), 0.0f);
            juce::AudioBuffer<float> buffer;
            for (const auto& reader : readers)
            {
                buffer.setSize ((int) reader->numChannels, n, false, false, true);
                reader->read (&buffer, 0, n, from, true, true);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < n; ++i)
                        c[(size_t) i] = juce::jmax (c[(size_t) i], std::abs (buffer.getSample (ch, i)));
            }
            return c;
        }

        // Where hits start (samples) and how loud each is: a rise of 12 dB over the quietest of the last 30 ms, above
        // 45 dB under the loudest moment, at least 60 ms after the last; then to the sample: the first past a tenth of
        // its peak, moved back to where its rise begins (a fiftieth).
        std::vector<std::pair<juce::int64, float>> findOnsets (const Readers& readers, juce::int64 length, double rate)
        {
            const auto frame = juce::jmax (1, (int) (rate / 1000.0));
            const auto e = framePeaks (readers, length, frame);
            float top = 0.0f;
            for (const auto v : e)
                top = juce::jmax (top, v);
            std::vector<std::pair<juce::int64, float>> onsets;
            if (top <= 1.0e-7f)
                return onsets;
            const auto threshold = top * juce::Decibels::decibelsToGain (-45.0f);
            auto last = -1000;
            for (int f = 0; f < (int) e.size(); ++f)
            {
                if (e[(size_t) f] < threshold || f - last < 60)
                    continue;
                auto floor = 1.0e-9f;
                if (f > 0)
                {
                    floor = e[(size_t) (f - 1)];
                    for (int g = juce::jmax (0, f - 30); g < f; ++g)
                        floor = juce::jmin (floor, e[(size_t) g]);
                }
                if (f > 0 && e[(size_t) f] < 4.0f * floor)
                    continue;
                last = f;
                const auto from = juce::jmax ((juce::int64) 0, (juce::int64) (f - 2) * frame);
                const auto n = (int) juce::jmin ((juce::int64) (24 * frame), length - from);
                const auto c = combined (readers, from, n);
                float peak = 0.0f;
                for (const auto v : c)
                    peak = juce::jmax (peak, v);
                auto i = 0;
                while (i < n && c[(size_t) i] < 0.1f * peak)
                    ++i;
                while (i > 0 && c[(size_t) i - 1] > 0.02f * peak)
                    --i;
                onsets.emplace_back (from + i, peak);
            }
            return onsets;
        }

        void fadeEnd (juce::AudioBuffer<float>& b, int length, int fade)
        {
            fade = juce::jmin (fade, length / 4);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < fade; ++i)
                    b.setSample (ch, length - fade + i, b.getSample (ch, length - fade + i)
                                                        * (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 1) / fade)));
        }
    }

    Plan scan (const juce::File& folder, const juce::File& midiGiven)
    {
        Plan plan;
        auto files = folder.findChildFiles (juce::File::findFiles, false, audioWildcard);
        files.sort();
        if (files.isEmpty())
        {
            plan.problems.add ("no audio files in " + folder.getFullPathName());
            return plan;
        }
        // Its mics: each file's name gives its channel. A whole-kit mix only when it's all there is.
        std::vector<Mic> mics, mixes, unknown;
        for (const auto& f : files)
        {
            Mic m { f, mixer::channelForName (f.getFileNameWithoutExtension()), f.getFileNameWithoutExtension() };
            if (m.channel >= 0)
                mics.push_back (m);
            else if (isWholeMix (m.name))
                mixes.push_back (m);
            else
                unknown.push_back (m);
        }
        if (mics.empty() && mixes.size() + unknown.size() == 1)
        {
            plan.mics = mixes.empty() ? unknown : mixes;   // one file: the drum's own sound
            plan.mics.front().channel = -1;
        }
        else
        {
            plan.mics = mics;
            for (const auto& m : mixes)
                plan.leftOut.add (m.file.getFileName() + " (a whole mix, not a mic)");
            for (const auto& m : unknown)
                plan.leftOut.add (m.file.getFileName() + " (no mixer channel goes by that name: rename it after one, e.g. SNARE TOP, OH, ROOM)");
            if (plan.mics.empty() && ! mixes.empty())
            {
                plan.mics = { mixes.front() };
                plan.mics.front().channel = -1;
                plan.leftOut.removeRange (0, 1);
            }
        }
        if (plan.mics.empty())
        {
            plan.problems.add ("none of the files is a mic the mixer knows by its name");
            return plan;
        }
        // One rate, one length (the shortest).
        Readers readers;
        std::vector<Mic> kept;
        for (const auto& m : plan.mics)
            if (std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (m.file)); r != nullptr)
            {
                if (plan.sampleRate <= 0.0)
                    plan.sampleRate = r->sampleRate;
                if (std::abs (r->sampleRate - plan.sampleRate) > 0.5)
                {
                    plan.leftOut.add (m.file.getFileName() + " (at " + juce::String (r->sampleRate, 0) + " Hz, not "
                                      + juce::String (plan.sampleRate, 0) + " Hz like the rest)");
                    continue;
                }
                plan.length = plan.length == 0 ? r->lengthInSamples : juce::jmin (plan.length, r->lengthInSamples);
                readers.push_back (std::move (r));
                kept.push_back (m);
            }
            else
                plan.leftOut.add (m.file.getFileName() + " (couldn't read it)");
        plan.mics = kept;
        if (readers.empty() || plan.length < 2)
        {
            plan.problems.add ("couldn't read the files");
            return plan;
        }

        // The MIDI: the one given, else one in the folder.
        auto midiFile = midiGiven;
        if (! midiFile.existsAsFile())
        {
            auto found = folder.findChildFiles (juce::File::findFiles, false, "*.mid;*.midi");
            found.sort();
            midiFile = found.isEmpty() ? juce::File() : found.getFirst();
        }
        const auto onsets = findOnsets (readers, plan.length, plan.sampleRate);
        const auto rate = plan.sampleRate;
        if (midiFile.existsAsFile())
        {
            juce::MidiFile midi;
            juce::FileInputStream in (midiFile);
            if (! in.openedOk() || ! midi.readFrom (in))
            {
                plan.problems.add ("couldn't read " + midiFile.getFileName());
                return plan;
            }
            midi.convertTimestampTicksToSeconds();
            for (int t = 0; t < midi.getNumTracks(); ++t)
                for (const auto* e : *midi.getTrack (t))
                    if (e->message.isNoteOn() && e->message.getVelocity() > 0)
                        plan.hits.push_back ({ e->message.getTimeStamp(), e->message.getNoteNumber(), (int) e->message.getVelocity() });
            std::sort (plan.hits.begin(), plan.hits.end(), [] (const Hit& a, const Hit& b) { return a.seconds < b.seconds; });
            plan.midi = midiFile;
            if (plan.hits.empty())
            {
                plan.problems.add (midiFile.getFileName() + " has no notes");
                return plan;
            }
            // How far the sound is from the MIDI's times (a bounce started at another bar, or with silence before it):
            // each of the first hits heard tried as the first note's, and none; the one that puts the most notes on a
            // hit (within 10 ms) kept, the smallest move on a tie.
            const auto landed = [&] (double shift)
            {
                auto count = 0;
                size_t j = 0;
                for (const auto& h : plan.hits)
                {
                    const auto at = (h.seconds + shift) * rate;
                    while (j < onsets.size() && (double) onsets[j].first < at - 0.01 * rate)
                        ++j;
                    count += j < onsets.size() && std::abs ((double) onsets[j].first - at) <= 0.01 * rate ? 1 : 0;
                }
                return count;
            };
            std::vector<double> candidates { 0.0 };
            for (size_t i = 0; i < onsets.size() && i < 32; ++i)
                candidates.push_back ((double) onsets[i].first / rate - plan.hits.front().seconds);
            auto best = -1;
            for (const auto c : candidates)
                if (const auto n = landed (c); n > best || (n == best && std::abs (c) < std::abs (plan.shiftSeconds)))
                {
                    best = n;
                    plan.shiftSeconds = c;
                }
            // Then to the sample: the usual distance from each note (so shifted) to the hit nearest it.
            std::vector<double> distances;
            for (const auto& h : plan.hits)
            {
                auto nearest = 1.0e9;
                for (const auto& o : onsets)
                    if (const auto d = (double) o.first / rate - (h.seconds + plan.shiftSeconds); std::abs (d) < std::abs (nearest))
                        nearest = d;
                if (std::abs (nearest) < 0.01)
                    distances.push_back (nearest);
            }
            if (! distances.empty())
            {
                std::nth_element (distances.begin(), distances.begin() + (std::ptrdiff_t) (distances.size() / 2), distances.end());
                plan.shiftSeconds += distances[distances.size() / 2];
            }
            for (auto& h : plan.hits)
            {
                const auto at = h.seconds + plan.shiftSeconds;
                auto near = false;
                for (const auto& o : onsets)
                    near = near || std::abs ((double) o.first / rate - at) < 0.01;
                plan.offTime += near ? 0 : 1;
                h.seconds = juce::jmax (0.0, at - preRollSeconds);
            }
            if (plan.offTime > 0)
                plan.problems.add (juce::String (plan.offTime) + " of " + juce::String ((int) plan.hits.size())
                                   + " notes had no hit near them in the audio: is it the MIDI that played this render?");
        }
        else
        {
            // No MIDI: the hits found, their velocities by how loud each is (the loudest 127, 40 dB down 1).
            float loudest = 0.0f;
            for (const auto& o : onsets)
                loudest = juce::jmax (loudest, o.second);
            for (const auto& o : onsets)
            {
                const auto db = juce::Decibels::gainToDecibels (o.second / juce::jmax (1.0e-9f, loudest));
                plan.hits.push_back ({ juce::jmax (0.0, (double) o.first / rate - preRollSeconds), -1,
                                       juce::jlimit (1, 127, juce::roundToInt (127.0 * juce::jlimit (0.0, 1.0, 1.0 + db / 40.0))) });
            }
            if (plan.hits.empty())
                plan.problems.add ("no hits found in the files (silent?)");
        }
        plan.hits.erase (std::remove_if (plan.hits.begin(), plan.hits.end(),
                                         [&plan] (const Hit& h) { return h.seconds * plan.sampleRate >= (double) plan.length - 2.0; }),
                         plan.hits.end());
        return plan;
    }

    std::vector<Written> write (const Plan& plan, const juce::File& into, const juce::String& name, const std::function<bool (double)>& progress)
    {
        std::vector<Written> written;
        if (! plan.ok())
            return written;
        Readers readers;
        for (const auto& m : plan.mics)
            readers.emplace_back (formats().createReaderFor (m.file));
        for (const auto& r : readers)
            if (r == nullptr)
                return written;
        const auto rate = plan.sampleRate;
        const auto multi = plan.mics.size() > 1 || plan.mics.front().channel >= 0;

        // Each hit's part of the files: to the next hit (whatever its note), at most maxHitSeconds.
        std::vector<juce::int64> starts;
        for (const auto& h : plan.hits)
            starts.push_back ((juce::int64) std::llround (h.seconds * rate));

        // The notes, each one's hits by velocity: up to 16 layers (more velocities than that: shared out in order).
        std::map<int, std::vector<size_t>> byNote;
        for (size_t i = 0; i < plan.hits.size(); ++i)
            byNote[plan.hits[i].note].push_back (i);
        auto done = 0;
        const auto total = (int) plan.hits.size();
        juce::WavAudioFormat wav;
        for (const auto& [note, indices] : byNote)
        {
            Written w;
            w.note = note;
            w.folder = note < 0 ? into : into.getChildFile (juce::File::createLegalFileName (name + " - note " + juce::String (note)));
            w.folder.deleteRecursively();
            w.folder.createDirectory();
            std::vector<size_t> sorted = indices;
            std::stable_sort (sorted.begin(), sorted.end(), [&plan] (size_t a, size_t b) { return plan.hits[a].velocity < plan.hits[b].velocity; });
            std::vector<int> distinct;
            for (const auto i : sorted)
                if (distinct.empty() || distinct.back() != plan.hits[i].velocity)
                    distinct.push_back (plan.hits[i].velocity);
            const auto layers = juce::jmin (16, (int) distinct.size());
            // A layer's velocity: the loudest of those in it (it plays up to there).
            std::vector<std::vector<size_t>> groups ((size_t) layers);
            for (const auto i : sorted)
            {
                const auto rank = (int) (std::lower_bound (distinct.begin(), distinct.end(), plan.hits[i].velocity) - distinct.begin());
                groups[(size_t) (rank * layers / (int) distinct.size())].push_back (i);
            }
            for (auto& g : groups)
            {
                std::sort (g.begin(), g.end());   // round robins in the order they were played
                auto top = 1;
                for (const auto i : g)
                    top = juce::jmax (top, plan.hits[i].velocity);
                w.velocities.push_back (top);
                w.rounds = juce::jmax (w.rounds, (int) g.size());
            }
            w.layers = layers;
            for (int l = 0; l < layers; ++l)
                for (size_t r = 0; r < groups[(size_t) l].size(); ++r)
                {
                    const auto i = groups[(size_t) l][r];
                    const auto start = starts[i];
                    auto end = juce::jmin (plan.length, start + (juce::int64) (maxHitSeconds * rate));
                    for (const auto s : starts)
                        if (s > start)
                            end = juce::jmin (end, s);
                    const auto n = (int) (end - start);
                    if (n < 16)
                        continue;
                    // Every mic's part, then where the last of it dies away (70 dB under its loudest, all mics together).
                    std::vector<juce::AudioBuffer<float>> parts;
                    float peak = 0.0f;
                    for (const auto& reader : readers)
                    {
                        juce::AudioBuffer<float> b ((int) reader->numChannels, n);
                        reader->read (&b, 0, n, start, true, true);
                        peak = juce::jmax (peak, b.getMagnitude (0, n));
                        parts.push_back (std::move (b));
                    }
                    const auto quiet = peak * juce::Decibels::decibelsToGain (-70.0f);
                    auto lastLoud = 0;
                    for (const auto& b : parts)
                        for (int ch = 0; ch < b.getNumChannels(); ++ch)
                        {
                            const auto* x = b.getReadPointer (ch);
                            for (int s = n - 1; s > lastLoud; --s)
                                if (std::abs (x[s]) > quiet)
                                {
                                    lastLoud = s;
                                    break;
                                }
                        }
                    const auto fade = (int) (0.01 * rate);
                    const auto keep = juce::jlimit (juce::jmin (n, (int) (0.05 * rate)), n, lastLoud + fade);
                    const auto fileName = "hit_vel" + juce::String (w.velocities[(size_t) l]).paddedLeft ('0', 3) + "_rr" + juce::String ((int) r + 1) + ".wav";
                    for (size_t m = 0; m < parts.size(); ++m)
                    {
                        auto& b = parts[m];
                        fadeEnd (b, keep, fade);
                        const auto target = multi ? w.folder.getChildFile (juce::File::createLegalFileName (plan.mics[m].name)).getChildFile (fileName)
                                                  : w.folder.getChildFile (fileName);
                        target.getParentDirectory().createDirectory();
                        target.deleteFile();
                        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (target), rate,
                                                                                              (unsigned int) b.getNumChannels(), 32, {}, 0));
                        if (writer != nullptr)
                            writer->writeFromAudioSampleBuffer (b, 0, keep);
                    }
                    ++w.hits;
                    ++done;
                    if (progress && ! progress ((double) done / (double) juce::jmax (1, total)))
                    {
                        written.push_back (w);
                        return written;
                    }
                }
            written.push_back (w);
        }
        return written;
    }
}
