#include "guitar/RenderImport.h"
#include "guitar/RoundRobinCheck.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <cmath>

namespace guitar::renders
{
    namespace
    {
        // The first sample in [from, to) at a tenth of the stretch's peak, and the peak (-1: silent).
        std::pair<int, float> onsetIn (const float* audio, int numSamples, int from, int to)
        {
            from = juce::jlimit (0, numSamples, from);
            to = juce::jlimit (0, numSamples, to);
            auto peak = 0.0f;
            for (int i = from; i < to; ++i)
                peak = std::max (peak, std::abs (audio[i]));
            if (peak < 1.0e-3f)   // -60 dBFS: nothing played here
                return { -1, peak };
            for (int i = from; i < to; ++i)
                if (std::abs (audio[i]) >= 0.1f * peak)
                    return { i, peak };
            return { -1, peak };
        }

        juce::String layerName (int layer) { return layer == 0 ? "soft" : layer == 1 ? "medium" : "hard"; }
    }

    juce::String ImportReport::summary() const
    {
        if (! ok())
            return error;
        auto s = juce::String (written) + " notes imported";
        if (outOfTune.isEmpty())
            s << ", all in tune";
        else
            s << ", " << outOfTune.size() << " out of tune";
        if (silent > 0)
            s << ", " << silent << " silent (left out)";
        if (copies > 0)
            s << ", " << copies << " copies of other takes left out (as few as " << fewestRoundRobins << " different per stroke)";
        return s;
    }

    double pitchOf (const float* audio, int numSamples, double sampleRate, double expectedHz)
    {
        // Yin's difference function (de Cheveigne and Kawahara's pitch detector), over the lags of half an octave around
        // the expected pitch: the lag where the signal best matches itself, refined between samples.
        const auto minLag = std::max (2, (int) std::floor (sampleRate / (expectedHz * 1.4142)));
        const auto maxLag = (int) std::ceil (sampleRate / (expectedHz / 1.4142));
        const auto window = std::min (4096, numSamples - maxLag - 2);
        if (window < 256)
            return 0.0;
        std::vector<double> d ((size_t) maxLag + 2, 0.0);
        for (int lag = 1; lag <= maxLag + 1; ++lag)
        {
            if (lag < minLag - 1)
                continue;
            double sum = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const auto diff = (double) audio[i] - (double) audio[i + lag];
                sum += diff * diff;
            }
            d[(size_t) lag] = sum;
        }
        auto best = -1;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (best < 0 || d[(size_t) lag] < d[(size_t) best])
                best = lag;
        if (best < 0)
            return 0.0;
        auto lag = (double) best;
        if (best > minLag && best < maxLag)
        {
            const auto a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            if (const auto denom = a - 2.0 * b + c; denom > 0.0)
                lag += 0.5 * (a - c) / denom;
        }
        return sampleRate / lag;
    }

    ImportReport importAudio (const float* audio, int numSamples, double sampleRate, Articulation articulation, int string,
                              const juce::File& guitarFolder, std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        ImportReport report;
        const auto list = takes (articulation, string);
        const auto toSample = [sampleRate] (double seconds) { return (int) std::lround (seconds * sampleRate); };

        // The render's offset: from the first dozen takes that sound.
        std::vector<double> offsets;
        for (size_t i = 0; i < list.size() && offsets.size() < 12; ++i)
        {
            const auto at = toSample (list[i].noteSeconds);
            if (const auto [onset, peak] = onsetIn (audio, numSamples, at - toSample (0.1), at + toSample (0.4)); onset >= 0)
                offsets.push_back ((onset - at) / sampleRate);
        }
        if (offsets.size() < 3)
        {
            report.error = "No notes found where the list puts them: is this the render of " + fileName (articulation, string) + "?";
            return report;
        }
        std::sort (offsets.begin(), offsets.end());
        report.offsetSeconds = offsets[offsets.size() / 2];

        const auto holdSeconds = spec (articulation).holdSeconds;
        const auto needed = list.back().noteSeconds + report.offsetSeconds + holdSeconds;
        if ((double) numSamples / sampleRate < needed)
        {
            report.error = "This render is shorter than the list (" + juce::String ((double) numSamples / sampleRate / 60.0, 1) + " min, "
                         + juce::String (needed / 60.0, 1) + " needed): was it rendered to the end, and is it "
                         + fileName (articulation, string) + "?";
            return report;
        }

        // Cut and check every take first; save only if the render is the right one.
        struct Cut { const Take* take; std::vector<float> samples; };
        std::vector<Cut> cuts;
        const auto length = toSample (prerollSeconds + holdSeconds + tailSeconds);
        const auto fadeIn = std::max (1, toSample (0.001)), fadeOut = std::max (1, toSample (0.03));
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (cancel != nullptr && cancel->load())
            {
                report.error = "Cancelled";
                return report;
            }
            const auto& take = list[i];
            const auto start = toSample (take.noteSeconds + report.offsetSeconds - prerollSeconds);
            std::vector<float> samples ((size_t) length, 0.0f);
            auto peak = 0.0f;
            for (int k = 0; k < length; ++k)
                if (const auto at = start + k; at >= 0 && at < numSamples)
                {
                    samples[(size_t) k] = audio[at];
                    peak = std::max (peak, std::abs (audio[at]));
                }
            if (peak < 1.0e-3f)
            {
                ++report.silent;
                continue;
            }
            for (int k = 0; k < fadeIn; ++k)
                samples[(size_t) k] *= (float) k / (float) fadeIn;
            for (int k = 0; k < fadeOut; ++k)
                samples[(size_t) (length - 1 - k)] *= (float) k / (float) fadeOut;

            const auto expected = frequencyOf (take.note());
            const auto from = toSample (prerollSeconds + 0.12);
            if (const auto hz = pitchOf (samples.data() + from, length - from, sampleRate, expected); hz > 0.0)
            {
                const auto cents = 1200.0 * std::log2 (hz / expected);
                if (std::abs (cents) > centsTolerance)
                    report.outOfTune.add ("fret " + juce::String (take.fret) + ", " + layerName (take.layer) + ", "
                                          + (take.stroke == Stroke::down ? "down" : "up") + ": "
                                          + (cents > 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " cents");
            }
            else
                report.outOfTune.add ("fret " + juce::String (take.fret) + ", " + layerName (take.layer) + ": no clear pitch");
            cuts.push_back ({ &take, std::move (samples) });
            if (progress != nullptr)
                *progress = 0.8f * (float) (i + 1) / (float) list.size();
        }
        if (cuts.empty() || (double) report.outOfTune.size() > 0.3 * (double) cuts.size())
        {
            report.error = juce::String (report.outOfTune.size()) + " of " + juce::String ((int) cuts.size())
                         + " notes aren't the pitch the list asked for: is this the render of " + fileName (articulation, string)
                         + ", with the force-string keyswitches and drop A tuning as they come?";
            return report;
        }

        // COPIES: each note, dynamic and stroke's round-robins compared; a copy of an earlier one isn't saved.
        std::vector<bool> keep (cuts.size(), true);
        report.fewestRoundRobins = spec (articulation).roundRobins;
        {
            std::map<std::tuple<int, int, int>, std::vector<size_t>> sets;
            for (size_t i = 0; i < cuts.size(); ++i)
                sets[{ cuts[i].take->fret, cuts[i].take->layer, (int) cuts[i].take->stroke }].push_back (i);
            const auto compare = toSample (prerollSeconds + rrcheck::compareSeconds);
            for (const auto& [key, members] : sets)
            {
                std::vector<std::vector<float>> heads;
                for (const auto i : members)
                    heads.emplace_back (cuts[i].samples.begin(), cuts[i].samples.begin() + std::min ((size_t) compare, cuts[i].samples.size()));
                const auto ids = rrcheck::sameAs (heads, sampleRate);
                std::vector<bool> seen (ids.size(), false);
                auto different = 0;
                for (size_t k = 0; k < ids.size(); ++k)
                {
                    if (seen[(size_t) ids[k]])
                    {
                        keep[members[k]] = false;
                        ++report.copies;
                    }
                    else
                    {
                        seen[(size_t) ids[k]] = true;
                        ++different;
                    }
                }
                report.fewestRoundRobins = std::min (report.fewestRoundRobins, different);
            }
        }

        // 0.6.2: the string's earlier takes of this articulation cleared first: a re-import keeps a different set of
        // round-robins (another render, or the v2 palm-mute lists), and an old take left beside them would play as if
        // it were one of the new.
        for (const auto& old : guitarFolder.getChildFile (articulationName (articulation)).findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (const auto id = parseTakeFile (old); id && id->articulation == articulation && id->string == string)
                old.deleteFile();

        juce::WavAudioFormat wav;
        for (size_t i = 0; i < cuts.size(); ++i)
        {
            if (! keep[i])
                continue;
            const auto file = guitarFolder.getChildFile (takeFileName (articulation, *cuts[i].take));
            file.getParentDirectory().createDirectory();
            file.deleteFile();
            auto out = file.createOutputStream();
            if (out == nullptr)
            {
                report.error = "Couldn't write " + file.getFullPathName();
                return report;
            }
            std::unique_ptr<juce::OutputStream> stream (out.release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                           .withSampleRate (sampleRate)
                                                           .withNumChannels (1)
                                                           .withBitsPerSample (24));
            if (writer == nullptr)
            {
                report.error = "Couldn't write " + file.getFullPathName();
                return report;
            }
            const float* channels[] { cuts[i].samples.data() };
            writer->writeFromFloatArrays (channels, 1, (int) cuts[i].samples.size());
            ++report.written;
            if (progress != nullptr)
                *progress = 0.8f + 0.2f * (float) (i + 1) / (float) cuts.size();
        }
        return report;
    }

    ImportReport importFile (const juce::File& render, Articulation articulation, int string, const juce::File& guitarFolder,
                             std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (render));
        if (reader == nullptr)
        {
            ImportReport report;
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
        return importAudio (buffer.getReadPointer (0), frames, reader->sampleRate, articulation, string, guitarFolder, progress, cancel);
    }
}
