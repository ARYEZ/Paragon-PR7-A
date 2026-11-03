#include "Riff.h"

#include <juce_dsp/juce_dsp.h>

namespace grooves
{
    namespace
    {
        constexpr double maxSeconds = 300.0;   // a whole song's worth: longer files are cut there (memory)

        // Peak to -12 dBFS (the drums stay on top of it at first; the panel's level sets the rest).
        void normalise (juce::AudioBuffer<float>& b)
        {
            const auto peak = b.getMagnitude (0, b.getNumSamples());
            if (peak > 1.0e-6f)
                b.applyGain (juce::Decibels::decibelsToGain (-12.0f) / peak);
        }

        juce::AudioBuffer<float> mono (const juce::AudioBuffer<float>& in)
        {
            juce::AudioBuffer<float> out (1, in.getNumSamples());
            out.clear();
            for (int c = 0; c < in.getNumChannels(); ++c)
                out.addFrom (0, 0, in, c, 0, in.getNumSamples(), 1.0f / (float) juce::jmax (1, in.getNumChannels()));
            return out;
        }

        // A MIDI riff to hear: each note a plucked tone (a few harmonics, falling away) at its pitch and velocity.
        juce::AudioBuffer<float> pluck (const std::vector<std::tuple<double, double, int, float>>& notes, double seconds, double rate)
        {
            juce::AudioBuffer<float> out (1, juce::jmax (1, (int) std::ceil ((seconds + 1.5) * rate)));
            out.clear();
            auto* d = out.getWritePointer (0);
            for (const auto& [start, length, note, velocity] : notes)
            {
                const auto hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const auto first = (int) (start * rate);
                const auto count = (int) (juce::jlimit (0.08, 1.5, length + 0.05) * rate);
                for (int i = 0; i < count && first + i < out.getNumSamples(); ++i)
                {
                    const auto t = i / rate;
                    auto s = 0.0;
                    for (int h = 1; h <= 6; ++h)
                        s += std::sin (juce::MathConstants<double>::twoPi * hz * h * t) / h * std::exp (-t * (2.0 + h));
                    const auto attack = juce::jmin (1.0, t / 0.004);
                    const auto release = juce::jmin (1.0, (count - i) / (0.01 * rate));
                    d[first + i] += (float) (s * velocity * attack * release * 0.3);
                }
            }
            return out;
        }

        // A drum lane's hits in [start, start + length) of a looping groove, from `start`, merged under a 24th of a beat.
        std::vector<double> window (const Rhythm& r, Lane lane, double start, double length, double early)
        {
            std::vector<double> at;
            const auto& beats = r.beats[(size_t) lane];
            if (beats.empty() || r.lengthBeats <= 0.0)
                return at;
            for (auto cycle = std::floor (start / r.lengthBeats) * r.lengthBeats; cycle < start + length; cycle += r.lengthBeats)
                for (const auto b : beats)
                {
                    if (b >= r.lengthBeats)
                        continue;
                    const auto t = cycle + b - start;
                    if (t < -early || t >= length)
                        continue;
                    at.push_back (t < 0.0 ? t + length : t);
                }
            std::sort (at.begin(), at.end());
            std::vector<double> out;
            for (const auto t : at)
                if (out.empty() || t - out.back() > 1.0 / 24.0)
                    out.push_back (t);
            return out;
        }

        double circular (double a, double b, double length)
        {
            const auto d = std::abs (a - b);
            return juce::jmin (d, length - d);
        }
    }

    bool isAudioRiff (const juce::File& f)
    {
        return f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3");
    }

    bool isRiffFile (const juce::File& f)
    {
        return isAudioRiff (f) || f.hasFileExtension ("mid;midi");
    }

    //==========================================================================
    std::vector<Onset> findOnsets (const juce::AudioBuffer<float>& in, double rate)
    {
        std::vector<Onset> onsets;
        if (in.getNumSamples() == 0 || rate <= 0.0)
            return onsets;
        const auto audio = mono (in);
        const auto* x = audio.getReadPointer (0);
        const auto numSamples = audio.getNumSamples();

        // 1. Where the notes are, roughly: spectral flux (how much each frequency's compressed level rose since the
        //    frame before, summed) over frames of about 21 ms, 1/8 apart (about 2.7 ms).
        const auto order = juce::jlimit (9, 12, (int) std::round (std::log2 (rate * 0.0213)));
        const auto size = 1 << order, hop = size / 8;
        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) size), frame ((size_t) size * 2), last ((size_t) size / 2 + 1, 0.0f);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
        std::vector<float> flux;
        const auto numFrames = juce::jmax (0, (numSamples - size) / hop + 1);
        flux.reserve ((size_t) numFrames);
        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                frame[(size_t) i] = x[f * hop + i] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);
            auto rise = 0.0f;
            for (int k = 1; k <= size / 2; ++k)
            {
                const auto level = std::log1p (10.0f * frame[(size_t) k] / (float) size * 4.0f);
                rise += juce::jmax (0.0f, level - last[(size_t) k]);
                last[(size_t) k] = level;
            }
            flux.push_back (f == 0 ? 0.0f : rise);
        }
        if (flux.size() < 3)
            return onsets;
        // A little smoothing (three frames).
        std::vector<float> smooth (flux.size());
        for (size_t f = 0; f < flux.size(); ++f)
            smooth[f] = 0.25f * flux[f > 0 ? f - 1 : f] + 0.5f * flux[f] + 0.25f * flux[f + 1 < flux.size() ? f + 1 : f];
        const auto loudest = *std::max_element (smooth.begin(), smooth.end());
        if (loudest <= 1.0e-6f)
            return onsets;

        // 2. Peaks: the highest within 40 ms either side, well above the median of the 300 ms around (the ring of a
        //    note isn't a new one) and above a tenth of the loudest.
        const auto frameSeconds = hop / rate;
        const auto near = juce::jmax (1, (int) std::lround (0.040 / frameSeconds));
        const auto around = juce::jmax (4, (int) std::lround (0.150 / frameSeconds));
        std::vector<float> scratch;
        struct Peak { int frame; float value; };
        std::vector<Peak> peaks;
        for (int f = 1; f + 1 < (int) smooth.size(); ++f)
        {
            const auto v = smooth[(size_t) f];
            if (v < 0.1f * loudest)
                continue;
            auto isPeak = true;
            for (int k = juce::jmax (0, f - near); k <= juce::jmin ((int) smooth.size() - 1, f + near) && isPeak; ++k)
                if (k != f && (smooth[(size_t) k] > v || (k < f && smooth[(size_t) k] >= v)))
                    isPeak = false;
            if (! isPeak)
                continue;
            scratch.assign (smooth.begin() + juce::jmax (0, f - around), smooth.begin() + juce::jmin ((int) smooth.size(), f + around + 1));
            std::nth_element (scratch.begin(), scratch.begin() + (std::ptrdiff_t) scratch.size() / 2, scratch.end());
            if (v < 1.5f * scratch[scratch.size() / 2] + 0.05f * loudest)
                continue;
            peaks.push_back ({ f, v });
        }

        // 3. Each one's time: the flux peaks when a note's attack is about the frame's middle (measured on tones with
        //    known starts: 2.5 frames before it, within about 3 ms). A weaker peak soon after a stronger one is the
        //    same note's ring, not a new note: within 120 ms it must be at least 40% as strong.
        for (const auto& p : peaks)
        {
            const auto seconds = juce::jmax (0.0, (p.frame * hop + size / 2 - 2.5 * hop) / rate);
            const auto strength = p.value / loudest;
            if (! onsets.empty() && seconds - onsets.back().seconds < 0.120 && strength < 0.40f * onsets.back().strength)
                continue;
            if (! onsets.empty() && seconds - onsets.back().seconds < 0.05)
            {
                if (strength > onsets.back().strength)
                    onsets.back() = { seconds, strength };
                continue;
            }
            onsets.push_back ({ seconds, strength });
        }
        auto top = 0.0f;
        for (const auto& o : onsets)
            top = juce::jmax (top, o.strength);
        for (auto& o : onsets)
            o.strength /= juce::jmax (1.0e-6f, top);
        return onsets;
    }

    double findTempo (const std::vector<Onset>& onsets, double fileSeconds, double beatsPerBar)
    {
        if (onsets.size() < 4)
            return 0.0;
        // The hits as a curve at 200 points a second (each a little bump), and how alike it is to itself moved by
        // one beat (and two, and half): the beat that repeats best, within 60-200 BPM, near 120 a little preferred.
        constexpr double rate = 200.0;
        const auto length = (int) std::ceil ((onsets.back().seconds + 1.0) * rate);
        std::vector<double> curve ((size_t) length, 0.0);
        for (const auto& o : onsets)
            for (int k = -6; k <= 6; ++k)
                if (const auto i = (int) std::lround (o.seconds * rate) + k; i >= 0 && i < length)
                    curve[(size_t) i] += o.strength * std::exp (-0.5 * (k / 2.5) * (k / 2.5));
        const auto alike = [&] (double lag)
        {
            if (lag < 1.0 || lag >= length)
                return 0.0;
            const auto l = (int) std::lround (lag);
            auto s = 0.0;
            for (int i = 0; i + l < length; ++i)
                s += curve[(size_t) i] * curve[(size_t) (i + l)];
            return s / (double) (length - l);
        };
        auto best = 0.0, bestScore = 0.0;
        for (auto bpm = 60.0; bpm <= 200.0; bpm += 0.5)
        {
            const auto lag = 60.0 / bpm * rate;
            // A riff repeats every bar: how alike it is a bar (four beats) on counts as much as a beat on.
            const auto prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.0, 2.0));
            const auto score = (alike (lag) + 0.5 * alike (2.0 * lag) + 0.5 * alike (0.5 * lag) + alike (4.0 * lag)) * prior;
            if (score > bestScore)
            {
                bestScore = score;
                best = bpm;
            }
        }
        if (best <= 0.0)
            return 0.0;
        // A riff cut to whole bars says its tempo exactly by its length: taken when it's within 4%.
        if (fileSeconds > 0.0 && beatsPerBar > 0.0)
            for (const auto bars : { 1, 2, 4, 8, 16, 32 })
                if (const auto byLength = bars * beatsPerBar * 60.0 / fileSeconds; std::abs (byLength / best - 1.0) < 0.04)
                    return byLength;
        return best;
    }

    //==========================================================================
    Riff riffFromAudio (juce::AudioBuffer<float> audio, double rate, double songBpm, int songNumerator, int songDenominator,
                        const juce::String& name)
    {
        Riff r;
        r.name = name;
        r.numerator = juce::jmax (1, songNumerator);
        r.denominator = juce::jmax (1, songDenominator);
        r.sampleRate = rate;
        r.seconds = audio.getNumSamples() / rate;
        r.onsets = findOnsets (audio, rate);

        // Its tempo: its name's; else the song's (a riff from the song is at its tempo: what's heard in a short, sparse
        // riff was right only 4 times in 5, measured); what's heard only with no song tempo. `heardBpm` keeps what was
        // heard for the panel to offer.
        r.heardBpm = findTempo (r.onsets, r.seconds, r.beatsPerBar());
        if (const auto named = tempoInName (name); named > 0.0)
        {
            r.bpm = named;
            r.tempoFrom = Riff::TempoFrom::name;
        }
        else if (songBpm > 0.0 || r.heardBpm <= 0.0)
        {
            r.bpm = songBpm > 0.0 ? songBpm : 120.0;
            r.tempoFrom = Riff::TempoFrom::song;
        }
        else
        {
            r.bpm = r.heardBpm;
            r.tempoFrom = Riff::TempoFrom::found;
        }
        r.bpm = juce::jlimit (30.0, 300.0, r.bpm);

        // Its first beat: its first clear hit (a riff starts on its first beat as a rule). A riff that opens with a
        // rest has it moved in the panel (a rule guessing exports by their grid was wrong one recorded riff in five).
        for (const auto& o : r.onsets)
            if (o.strength >= 0.35f)
            {
                r.startSeconds = juce::jmax (0.0, o.seconds);
                break;
            }
        auto heard = mono (audio);
        normalise (heard);
        r.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (heard));
        return r;
    }

    std::optional<Riff> readRiff (const juce::File& file, double songBpm, int songNumerator, int songDenominator, juce::String* problem)
    {
        const auto fail = [problem] (const juce::String& why) -> std::optional<Riff>
        {
            if (problem != nullptr)
                *problem = why;
            return std::nullopt;
        };
        if (file.hasFileExtension ("mid;midi"))
        {
            juce::FileInputStream in (file);
            juce::MidiFile midi;
            if (! in.openedOk() || ! midi.readFrom (in) || midi.getTimeFormat() <= 0)
                return fail ("couldn't read it as a MIDI file");
            const auto ticks = (double) midi.getTimeFormat();
            Riff r;
            r.file = file;
            r.name = file.getFileNameWithoutExtension();
            r.fromMidi = true;
            r.numerator = juce::jmax (1, songNumerator);
            r.denominator = juce::jmax (1, songDenominator);
            r.bpm = songBpm > 0.0 ? songBpm : 120.0;
            r.tempoFrom = Riff::TempoFrom::song;
            auto tempoFound = false, signatureFound = false;
            std::vector<std::tuple<double, double, int, float>> notes;   // beat, length (beats), note, velocity
            for (int t = 0; t < midi.getNumTracks(); ++t)
            {
                juce::MidiMessageSequence track (*midi.getTrack (t));
                track.updateMatchedPairs();
                for (int i = 0; i < track.getNumEvents(); ++i)
                {
                    const auto& m = track.getEventPointer (i)->message;
                    const auto beat = m.getTimeStamp() / ticks;
                    if (m.isTempoMetaEvent() && ! tempoFound && m.getTempoSecondsPerQuarterNote() > 0.0)
                    {
                        r.bpm = std::round (60000.0 / m.getTempoSecondsPerQuarterNote()) / 1000.0;
                        r.tempoFrom = Riff::TempoFrom::file;
                        tempoFound = true;
                    }
                    else if (m.isTimeSignatureMetaEvent() && ! signatureFound)
                    {
                        int n = 4, d = 4;
                        m.getTimeSignatureInfo (n, d);
                        if (n > 0 && d > 0)
                        {
                            r.numerator = n;
                            r.denominator = d;
                            signatureFound = true;
                        }
                    }
                    else if (m.isNoteOn() && m.getVelocity() > 0)
                    {
                        const auto* off = track.getEventPointer (i)->noteOffObject;
                        const auto length = off != nullptr ? off->message.getTimeStamp() / ticks - beat : 0.5;
                        notes.push_back ({ beat, length, m.getNoteNumber(), m.getFloatVelocity() });
                    }
                }
            }
            if (notes.empty())
                return fail ("it has no notes");
            std::sort (notes.begin(), notes.end());
            if (! tempoFound)
                if (const auto named = tempoInName (r.name); named > 0.0)
                {
                    r.bpm = named;
                    r.tempoFrom = Riff::TempoFrom::name;
                }
            const auto secondsPerBeat = 60.0 / r.bpm;
            // A chord (notes within a 24th of a beat) is one hit, as hard as its hardest note, a little more for each
            // note it has.
            std::vector<std::pair<double, float>> hits;
            for (size_t i = 0; i < notes.size();)
            {
                const auto start = std::get<0> (notes[i]);
                auto hardest = 0.0f;
                auto count = 0;
                for (; i < notes.size() && std::get<0> (notes[i]) - start < 1.0 / 24.0; ++i, ++count)
                    hardest = juce::jmax (hardest, std::get<3> (notes[i]));
                hits.push_back ({ start, hardest * (1.0f + 0.1f * (float) juce::jmin (4, count - 1)) });
            }
            auto top = 0.0f;
            for (const auto& h : hits)
                top = juce::jmax (top, h.second);
            for (const auto& h : hits)
                r.onsets.push_back ({ h.first * secondsPerBeat, h.second / juce::jmax (1.0e-6f, top) });
            auto lastEnd = 0.0;
            std::vector<std::tuple<double, double, int, float>> heard;
            for (const auto& [beat, length, note, velocity] : notes)
            {
                lastEnd = juce::jmax (lastEnd, (beat + length) * secondsPerBeat);
                heard.push_back ({ beat * secondsPerBeat, length * secondsPerBeat, note, velocity });
            }
            r.seconds = juce::jmin (maxSeconds, lastEnd);
            r.sampleRate = 48000.0;
            auto audio = pluck (heard, r.seconds, r.sampleRate);
            normalise (audio);
            r.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (audio));
            r.startSeconds = 0.0;   // a MIDI file starts on its first bar
            return r;
        }

        if (! isAudioRiff (file))
            return fail ("it isn't an audio or MIDI file");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return fail ("couldn't read it as audio");
        const auto frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
        juce::AudioBuffer<float> audio ((int) juce::jmax (1u, reader->numChannels), frames);
        reader->read (&audio, 0, frames, 0, true, true);
        auto r = riffFromAudio (std::move (audio), reader->sampleRate, songBpm, songNumerator, songDenominator, file.getFileNameWithoutExtension());
        r.file = file;
        if (r.onsets.empty())
            return fail ("no notes could be heard in it");
        return r;
    }

    //==========================================================================
    Feel feelOf (const std::vector<std::pair<double, float>>& hits)
    {
        // As a groove's FEEL: off-beat hits near the straight grid against the triplet one.
        auto straight = 0, triplet = 0;
        for (const auto& h : hits)
        {
            const auto f = h.first - std::floor (h.first);
            if (f < 0.03 || f > 0.97)
                continue;
            const auto near = [f] (std::initializer_list<double> grid)
            {
                return std::any_of (grid.begin(), grid.end(), [f] (double x) { return std::abs (f - x) < 0.035; });
            };
            if (near ({ 0.25, 0.5, 0.75 }))
                ++straight;
            else if (near ({ 1.0 / 6.0, 1.0 / 3.0, 2.0 / 3.0, 5.0 / 6.0 }))
                ++triplet;
        }
        return triplet >= 2 && triplet > straight ? Feel::swung : Feel::straight;
    }

    void markAccents (std::vector<std::pair<double, float>>& hits)
    {
        // The riff's strong hits and its weaker ones: the split of their strengths into two groups that leaves each
        // group tightest (Otsu's method), when the stronger group is clearly stronger (1.15 times, on average: distortion
        // squeezes a guitar's accents to about 1.25); else
        // all of them count as strong. Strong ones become 1, weaker ones 0.3: what riffMatch reads.
        if (hits.empty())
            return;
        std::vector<float> s;
        for (const auto& h : hits)
            s.push_back (h.second);
        std::sort (s.begin(), s.end());
        auto bestSplit = 0.0f;
        auto bestBetween = -1.0;
        auto ratio = 1.0;
        for (size_t k = 1; k < s.size(); ++k)
        {
            auto low = 0.0, high = 0.0;
            for (size_t i = 0; i < k; ++i) low += s[i];
            for (size_t i = k; i < s.size(); ++i) high += s[i];
            low /= (double) k;
            high /= (double) (s.size() - k);
            const auto between = (double) k * (double) (s.size() - k) * (high - low) * (high - low);
            if (between > bestBetween)
            {
                bestBetween = between;
                bestSplit = 0.5f * (s[k - 1] + s[k]);
                ratio = low > 1.0e-6 ? high / low : 10.0;
            }
        }
        const auto twoLevels = s.size() >= 3 && ratio >= 1.15;
        for (auto& h : hits)
            h.second = ! twoLevels || h.second >= bestSplit ? 1.0f : 0.3f;
    }

    RiffQuery queryOf (const Riff& r)
    {
        RiffQuery q;
        q.beatsPerBar = r.beatsPerBar();
        q.lengthBeats = r.lengthBeats();
        constexpr double early = 0.125;   // a hit a little before the part's start is its first beat
        for (const auto& o : r.onsets)
        {
            const auto beat = r.beatAt (o.seconds);
            if (beat < -early || beat >= q.lengthBeats - early)
                continue;
            q.hits.push_back ({ juce::jmax (0.0, beat), o.strength });
        }
        markAccents (q.hits);
        q.feel = feelOf (q.hits);
        q.key = r.file.getFullPathName() + "|" + r.name + "|" + juce::String (r.startSeconds, 4) + "|" + juce::String (r.bars)
                + "|" + juce::String (r.bpm, 3) + "|" + juce::String (r.numerator) + "/" + juce::String (r.denominator)
                + "|" + juce::String ((int) r.onsets.size());
        return q;
    }

    double riffMatch (const RiffQuery& q, const Rhythm& groove, Feel grooveFeel)
    {
        if (q.hits.empty() || groove.isEmpty() || q.lengthBeats <= 0.0)
            return 0.0;
        const auto length = q.lengthBeats;
        constexpr double tolerance = 0.125;   // half a sixteenth
        constexpr float accent = 0.5f;        // a hit at least half as strong as the part's loudest
        auto numAccents = 0;
        for (const auto& h : q.hits)
            numAccents += h.second >= accent ? 1 : 0;

        auto best = 0.0;
        const auto step = juce::jmax (0.25, groove.beatsPerBar);
        for (auto start = 0.0; start < groove.lengthBeats - 1.0e-9; start += step)
        {
            const auto kicks = window (groove, Lane::kick, start, length, tolerance);
            if (kicks.empty())
                continue;
            // Crashes and snares meet the riff's accents only when they're accents themselves: a crash ridden (more
            // than 2 a bar) or a snare roll (more than 4 a bar) would meet every one.
            const auto barsInWindow = juce::jmax (1.0, length / juce::jmax (0.25, q.beatsPerBar));
            auto crashes = window (groove, Lane::crash, start, length, tolerance);
            if ((double) crashes.size() > 2.0 * barsInWindow)
                crashes.clear();
            auto snares = window (groove, Lane::snare, start, length, tolerance);
            if ((double) snares.size() > 4.0 * barsInWindow)
                snares.clear();

            // Kicks to the riff's hits, nearest first (an accent before a weak hit at the same distance), one to one.
            struct Pair { double distance; size_t kick, hit; };
            std::vector<Pair> pairs;
            for (size_t k = 0; k < kicks.size(); ++k)
                for (size_t h = 0; h < q.hits.size(); ++h)
                    if (const auto d = circular (kicks[k], q.hits[h].first, length); d <= tolerance)
                        pairs.push_back ({ d + (q.hits[h].second >= accent ? 0.0 : 0.01), k, h });
            std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.distance < b.distance; });
            std::vector<bool> kickUsed (kicks.size()), hitUsed (q.hits.size());
            auto onHits = 0.0;   // kicks on the riff's hits: 1 on an accent, 0.8 on a weaker one
            std::vector<double> covered (q.hits.size(), 0.0);
            for (const auto& p : pairs)
                if (! kickUsed[p.kick] && ! hitUsed[p.hit])
                {
                    kickUsed[p.kick] = hitUsed[p.hit] = true;
                    const auto strong = q.hits[p.hit].second >= accent;
                    onHits += strong ? 1.0 : 0.8;
                    covered[p.hit] = 1.0;
                }
            // Accents no kick has: a crash on it counts fully, a snare partly.
            const auto near = [&] (const std::vector<double>& hits, double at)
            {
                return std::any_of (hits.begin(), hits.end(), [&] (double t) { return circular (t, at, length) <= tolerance; });
            };
            auto found = 0.0;
            for (size_t h = 0; h < q.hits.size(); ++h)
            {
                if (q.hits[h].second < accent)
                    continue;
                if (covered[h] <= 0.0)
                    covered[h] = near (crashes, q.hits[h].first) ? 1.0 : near (snares, q.hits[h].first) ? 0.6 : 0.0;
                found += covered[h];
            }
            const auto recall = numAccents > 0 ? found / numAccents : 0.0;
            const auto precision = onHits / (double) kicks.size();
            if (recall <= 0.0 || precision <= 0.0)
                continue;
            // The riff's accents met matter more than a kick off the riff (F with beta 1.5: measured on two real groove
            // packs' grooves as riffs, 67% top result right against 60% for beta 1 and 55% for 0.7).
            constexpr double b2 = 2.25;
            best = juce::jmax (best, (1.0 + b2) * precision * recall / (b2 * precision + recall));
        }
        if (q.feel != grooveFeel)
            best *= 0.8;
        if (std::abs (q.beatsPerBar - groove.beatsPerBar) > 1.0e-6)
            best *= 0.75;
        return best;
    }
}
