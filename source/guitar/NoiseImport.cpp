#include "guitar/NoiseImport.h"
#include "guitar/RoundRobinCheck.h"
#include "guitar/RenderImport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>

namespace guitar::noises
{
    namespace
    {
        constexpr float silentPeak = 3.16e-4f;   // -70 dBFS
        constexpr float loudPeak = 1.0e-3f;      // -60 dBFS

        // Where each of the list's noises starts in the file: a release at its note-off, fret noise at its key.
        double eventSeconds (List list, const Hit& h) { return list == List::fretNoise ? h.noteSeconds : h.offSeconds; }

        juce::String what (List list)
        {
            switch (list)
            {
                case List::pitchedRelease:    return "releases";
                case List::powerChordRelease: return "power-chord releases";
                case List::unpitched:         return "unpitched noises";
                case List::fretNoise:         return "fret noises";
            }
            return {};
        }

        // The render's onsets: after at least 150 ms under -60 dBFS, the first sample at a tenth of the next 50 ms's peak.
        std::vector<double> onsets (const float* audio, int numSamples, double rate)
        {
            std::vector<double> found;
            const auto quiet = (int) (0.15 * rate), look = (int) (0.05 * rate);
            auto quietFor = quiet;
            for (int i = 0; i < numSamples; ++i)
            {
                if (std::abs (audio[i]) < loudPeak)
                {
                    ++quietFor;
                    continue;
                }
                if (quietFor >= quiet)
                {
                    auto peak = 0.0f;
                    const auto from = std::max (0, i - look / 2), to = std::min (numSamples, i + look);
                    for (int k = from; k < to; ++k)
                        peak = std::max (peak, std::abs (audio[k]));
                    auto at = i;
                    for (int k = from; k < to; ++k)
                        if (std::abs (audio[k]) >= 0.1f * peak)
                        {
                            at = k;
                            break;
                        }
                    found.push_back ((double) at / rate);
                }
                quietFor = 0;
            }
            return found;
        }
    }

    juce::String NoiseReport::summary (List list) const
    {
        if (! ok())
            return error;
        auto s = juce::String (written) + " " + what (list) + " imported";
        if (copies > 0)
            s << ", " << copies << " copies left out";
        if (silent > 0)
            s << ", " << silent << " of " << hits << " hits silent";
        return s;
    }

    NoiseReport importAudio (const float* audio, int numSamples, double rate, List list, int string, const juce::File& guitarFolder,
                             std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        NoiseReport report;
        const auto all = hits (list, string);
        report.hits = (int) all.size();
        const auto toSample = [rate] (double seconds) { return (int) std::lround (seconds * rate); };
        if (numSamples < toSample (eventSeconds (list, all.back()) - 1.0))
        {
            report.error = "This render is too short for " + fileName (list, string) + ": was it bounced to the end of the list?";
            return report;
        }

        // The notes heard? (Rendered with the notes' Volume at minimum, only the short noises are loud.)
        if (list != List::fretNoise)
        {
            const auto frame = std::max (1, toSample (0.01));
            const auto from = toSample (all.front().noteSeconds), to = std::min (numSamples, toSample (all.back().offSeconds + 0.5));
            auto loud = 0, frames = 0;
            for (int f = from; f + frame <= to; f += frame, ++frames)
            {
                auto peak = 0.0f;
                for (int k = f; k < f + frame; ++k)
                    peak = std::max (peak, std::abs (audio[k]));
                loud += peak >= loudPeak ? 1 : 0;
            }
            if (frames > 0 && loud > 0.4 * frames)
            {
                report.error = juce::String ("The notes can be heard in this render: turn ") + (list == List::powerChordRelease ? "Power Chord Sustain's" : "Sustain's")
                             + " Volume all the way down in your guitar instrument's SOUND tab and render it again (only the noises should be left).";
                return report;
            }
        }

        // LINED UP: the offset that matches most onsets to the list's noises.
        const auto found = onsets (audio, numSamples, rate);
        if (found.empty())
        {
            report.error = "Nothing can be heard in this render: are the " + what (list) + " on in your guitar instrument?";
            return report;
        }
        std::vector<double> expected;
        for (const auto& h : all)
            expected.push_back (eventSeconds (list, h));
        const auto nearest = [&expected] (double t)
        {
            const auto it = std::lower_bound (expected.begin(), expected.end(), t);
            auto best = 1.0e9;
            if (it != expected.end()) best = *it - t;
            if (it != expected.begin() && t - *(it - 1) < std::abs (best)) best = *(it - 1) - t;
            return best;   // expected - t
        };
        const auto scored = std::min<size_t> (found.size(), 40);
        auto bestScore = -1;
        auto bestOffset = 0.0;
        for (size_t j = 0; j < std::min<size_t> (found.size(), 8); ++j)
            for (const auto e : expected)
            {
                const auto offset = found[j] - e;
                if (offset < -3.0 || offset > 1.0)
                    continue;
                auto score = 0;
                for (size_t k = 0; k < scored; ++k)
                    score += std::abs (nearest (found[k] - offset)) < 0.02 ? 1 : 0;
                if (score > bestScore || (score == bestScore && std::abs (offset) < std::abs (bestOffset)))
                {
                    bestScore = score;
                    bestOffset = offset;
                }
            }
        if (bestScore < std::min<int> (5, (int) (scored + 1) / 2))
        {
            report.error = "Couldn't line this render up with " + fileName (list, string) + ": is it that list's render, bounced from bar 1?";
            return report;
        }
        {
            std::vector<double> residuals;
            for (size_t k = 0; k < scored; ++k)
                if (const auto d = nearest (found[k] - bestOffset); std::abs (d) < 0.02)
                    residuals.push_back (-d);
            std::sort (residuals.begin(), residuals.end());
            report.offsetSeconds = bestOffset + (residuals.empty() ? 0.0 : residuals[residuals.size() / 2]);
        }

        // EACH NOISE cut.
        struct Cut { const Hit* hit; std::vector<float> samples; };
        std::vector<Cut> cuts;
        const auto length = toSample (list == List::fretNoise ? fretCutSeconds : cutSeconds);
        const auto fadeIn = std::max (1, toSample (0.001)), fadeOut = std::max (1, toSample (0.03));
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (cancel != nullptr && cancel->load())
            {
                report.error = "Cancelled";
                return report;
            }
            const auto start = toSample (eventSeconds (list, all[i]) + report.offsetSeconds - preSeconds);
            std::vector<float> samples ((size_t) length, 0.0f);
            auto peak = 0.0f;
            for (int k = 0; k < length; ++k)
                if (const auto at = start + k; at >= 0 && at < numSamples)
                {
                    samples[(size_t) k] = audio[at];
                    peak = std::max (peak, std::abs (audio[at]));
                }
            if (peak < silentPeak)
            {
                ++report.silent;
                continue;
            }
            // Its silent end trimmed (60 dB under its peak, then 10 ms more; at least 100 ms kept).
            auto end = length;
            while (end > 0 && std::abs (samples[(size_t) end - 1]) < peak * 1.0e-3f)
                --end;
            end = juce::jlimit (std::min (length, toSample (0.1)), length, end + toSample (0.01));
            samples.resize ((size_t) end);
            for (int k = 0; k < std::min (fadeIn, end); ++k)
                samples[(size_t) k] *= (float) k / (float) fadeIn;
            for (int k = 0; k < std::min (fadeOut, end); ++k)
                samples[(size_t) (end - 1 - k)] *= (float) k / (float) fadeOut;
            cuts.push_back ({ &all[i], std::move (samples) });
            if (progress != nullptr)
                *progress = 0.7f * (float) (i + 1) / (float) all.size();
        }

        // CHECKED: releases and fret noise should be there for most hits; unpitched noises only now and then.
        const auto heard = (int) cuts.size();
        if ((list == List::pitchedRelease || list == List::powerChordRelease || list == List::fretNoise) && heard < report.hits / 2)
        {
            report.error = juce::String (report.silent) + " of " + juce::String (report.hits) + " hits have no noise: are the " + what (list)
                         + " on in your guitar instrument" + (list == List::fretNoise ? "" : " (Pitched Release's Volume up)") + "?";
            return report;
        }
        if (list == List::unpitched && heard > report.hits / 2)
        {
            report.error = juce::String (heard) + " of " + juce::String (report.hits) + " notes have a noise, but the unpitched ones only come now "
                           "and then: turn Pitched Release's Volume all the way down too, and render it again.";
            return report;
        }

        // The right string's? Pitched releases sound at their note's pitch (the owner's noise check: the low A's at ~55 Hz,
        // the D string's 7th fret at ~217 Hz), and every string's list has the same times, so a render imported into
        // another string's row would line up. Lenient (a release is a noise, not a note): refused only when most of
        // them clearly have another pitch and hardly any have theirs.
        if (list == List::pitchedRelease || list == List::powerChordRelease)
        {
            auto match = 0, wrong = 0;
            const auto from = toSample (preSeconds + 0.01);
            for (const auto& cut : cuts)
            {
                const auto expected = frequencyOf (cut.hit->note);
                const auto n = (int) cut.samples.size() - from;
                if (n <= 0)
                    continue;
                if (const auto hz = renders::pitchOf (cut.samples.data() + from, n, rate, expected); hz > 0.0)
                    (std::abs (1200.0 * std::log2 (hz / expected)) <= 150.0 ? match : wrong) += 1;
            }
            if (wrong > (int) cuts.size() / 2 && match < (int) cuts.size() / 5)
            {
                report.error = "These releases aren't at the pitches " + fileName (list, string) + " plays: is this that list's render "
                               "(each string has its own)?";
                return report;
            }
        }

        // COPIES left out, and what's kept numbered in order.
        std::vector<int> index (cuts.size(), -1);
        {
            std::map<std::pair<int, int>, std::vector<size_t>> groups;
            for (size_t i = 0; i < cuts.size(); ++i)
            {
                const auto key = (list == List::unpitched || list == List::fretNoise) ? std::pair<int, int> { 0, 0 }
                                                                                       : std::pair<int, int> { cuts[i].hit->string, cuts[i].hit->fret };
                groups[key].push_back (i);
            }
            const auto compare = (size_t) toSample (renders::rrcheck::compareSeconds);
            for (const auto& [key, members] : groups)
            {
                std::vector<std::vector<float>> heads;
                for (const auto i : members)
                    heads.emplace_back (cuts[i].samples.begin(), cuts[i].samples.begin() + (long) std::min (compare, cuts[i].samples.size()));
                const auto ids = renders::rrcheck::sameAs (heads, rate);
                std::vector<bool> seen (ids.size() + 1, false);
                auto next = 0;
                for (size_t k = 0; k < ids.size(); ++k)
                {
                    if (seen[(size_t) ids[k]])
                        ++report.copies;
                    else
                    {
                        seen[(size_t) ids[k]] = true;
                        index[members[k]] = next++;
                    }
                }
            }
        }

        // SAVED, replacing what this list imported before.
        const auto folder = guitarFolder.getChildFile (folderName (list));
        folder.createDirectory();
        for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (const auto id = parseNoiseFile (old); id && (list != List::pitchedRelease || id->string == string))
                old.deleteFile();
        juce::WavAudioFormat wav;
        for (size_t i = 0; i < cuts.size(); ++i)
        {
            if (index[i] < 0)
                continue;
            const auto file = guitarFolder.getChildFile (noiseFileName (list, cuts[i].hit->string, cuts[i].hit->fret, index[i]));
            auto out = file.createOutputStream();
            if (out == nullptr)
            {
                report.error = "Couldn't write " + file.getFullPathName();
                return report;
            }
            std::unique_ptr<juce::OutputStream> stream (out.release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            if (writer == nullptr)
            {
                report.error = "Couldn't write " + file.getFullPathName();
                return report;
            }
            const float* channels[] { cuts[i].samples.data() };
            writer->writeFromFloatArrays (channels, 1, (int) cuts[i].samples.size());
            ++report.written;
            if (progress != nullptr)
                *progress = 0.7f + 0.3f * (float) (i + 1) / (float) cuts.size();
        }
        return report;
    }

    NoiseReport importFile (const juce::File& render, List list, int string, const juce::File& guitarFolder,
                            std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (render));
        if (reader == nullptr)
        {
            NoiseReport report;
            report.error = "Couldn't read " + render.getFileName() + " as audio (WAV, AIFF or FLAC)";
            return report;
        }
        const auto frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) 2147483000 / 4);
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, frames);
        reader->read (&buffer, 0, frames, 0, true, channels > 1);
        if (channels > 1)
        {
            buffer.addFrom (0, 0, buffer, 1, 0, frames);
            buffer.applyGain (0, 0, frames, 0.5f);
        }
        return importAudio (buffer.getReadPointer (0), frames, reader->sampleRate, list, string, guitarFolder, progress, cancel);
    }
}
