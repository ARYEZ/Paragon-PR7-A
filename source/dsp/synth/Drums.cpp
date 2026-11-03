#include "dsp/synth/Drums.h"
#include "dsp/synth/Instrument.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace synth::drums
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        // Each model's level, so a hit at the defaults peaks near -3 dBFS (measured in the tests).
        constexpr std::array<float, 24> modelLevel { 0.651f, 0.716f, 1.373f, 2.314f, 2.006f, 0.550f, 0.824f, 0.770f, 1.119f, 1.0f,
                                                     1.097f, 0.549f, 1.017f, 1.099f,
                                                     0.656f, 0.804f, 0.887f,   // 0.24.0: the acoustic kick, snare, tom
                                                     0.462f, 0.505f, 2.11f, 0.491f, 2.11f, 1.071f, 0.689f };   // 0.25.0: the cymbals

        // THE ACOUSTIC MODELS (0.24.0). An ideal circular drumhead rings in modes (m nodal diameters, n nodal circles),
        // each at a frequency set by a zero of the Bessel function J_m: j_mn / j_01 times the lowest. The first 24, in
        // order (standard values).
        struct HeadMode { int m; double j; };
        constexpr std::array<HeadMode, 24> headModes {{
            { 0, 2.4048 }, { 1, 3.8317 }, { 2, 5.1356 }, { 0, 5.5201 }, { 3, 6.3802 }, { 1, 7.0156 }, { 4, 7.5883 }, { 2, 8.4172 },
            { 0, 8.6537 }, { 5, 8.7715 }, { 3, 9.7610 }, { 6, 9.9361 }, { 1, 10.1735 }, { 4, 11.0647 }, { 7, 11.0864 }, { 2, 11.6198 },
            { 0, 11.7915 }, { 8, 12.2251 }, { 5, 12.3386 }, { 3, 13.0152 }, { 1, 13.3237 }, { 9, 13.3543 }, { 6, 13.5893 }, { 2, 14.7960 },
        }};

        // J_m (x), from its power series (x below about 16 here, where doubles keep it to well under 1e-9).
        double besselJ (int m, double x) noexcept
        {
            const auto half = 0.5 * x;
            auto term = 1.0;
            for (int i = 1; i <= m; ++i)
                term *= half / i;
            auto sum = term;
            for (int k = 1; k < 60; ++k)
            {
                term *= -half * half / (k * (double) (k + m));
                sum += term;
                if (std::abs (term) < 1.0e-14 * (1.0 + std::abs (sum)))
                    break;
            }
            return sum;
        }

        // The six metal oscillators (hats, cymbal): unrelated pitches, so their harmonics clash like metal. The
        // classic analog-machine idea; these are our own numbers.
        constexpr std::array<double, 6> metalHz { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        constexpr std::array<double, 6> metalStart { 0.0, 0.37, 0.71, 0.13, 0.52, 0.89 };
        // The clap's bursts (seconds, at PITCH 0): three quick slaps, then the one that rings.
        constexpr std::array<double, 4> clapBursts { 0.0, 0.010, 0.020, 0.031 };
        // The tambourine's jingles striking one after another as the frame moves (seconds, at PITCH 0).
        constexpr std::array<double, 3> jingleBursts { 0.0, 0.006, 0.014 };
        // The ride bell's partials: inharmonic, as a bell's are (our own ratios), each a little quieter.
        constexpr std::array<double, 4> bellRatio { 1.0, 1.51, 2.24, 2.76 };
        constexpr std::array<float, 4> bellLevel { 1.0f, 0.6f, 0.45f, 0.3f };

        float decayCoefficient (double seconds, double sampleRate) noexcept
        {
            return (float) std::exp (-1.0 / (juce::jmax (1.0e-4, seconds) * sampleRate));
        }

        // A square wave with its jumps smoothed (polyBLEP), so the metal doesn't fold back down as aliasing.
        float blep (double t, double dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return (float) (t + t - t * t - 1.0);
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return (float) (t * t + t + t + 1.0);
            }
            return 0.0f;
        }

        float square (double phase, double increment) noexcept
        {
            auto value = phase < 0.5 ? 1.0f : -1.0f;
            value += blep (phase, increment);
            auto half = phase + 0.5;
            if (half >= 1.0)
                half -= 1.0;
            return value - blep (half, increment);
        }

        void advance (double& phase, double increment) noexcept
        {
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        // A sine from a 4096-point table, read with straight interpolation (within about -130 dB of std::sin, at a
        // fraction of its cost: the kit's voices read one or two a sample each).
        float sine (double phase) noexcept
        {
            static const auto table = []
            {
                std::array<float, 4097> t {};
                for (size_t i = 0; i < t.size(); ++i)
                    t[i] = (float) std::sin (twoPi * (double) i / 4096.0);
                return t;
            }();
            const auto position = phase * 4096.0;
            const auto index = (int) position;
            const auto frac = (float) (position - index);
            return table[(size_t) index] + frac * (table[(size_t) index + 1] - table[(size_t) index]);
        }

        // 4-point cubic (Hermite) between x1 and x2.
        float cubic (float x0, float x1, float x2, float x3, float t) noexcept
        {
            const auto c1 = 0.5f * (x2 - x0);
            const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * t + c2) * t + c1) * t + x1;
        }
    }

    //==============================================================================
    namespace sampleFiles
    {
        std::shared_ptr<const Sample> load (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
                return nullptr;
            const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
            const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
            juce::AudioBuffer<float> buffer (channels, length);
            reader->read (&buffer, 0, length, 0, true, channels > 1);
            auto sample = std::make_shared<Sample>();
            sample->name = file.getFileNameWithoutExtension();
            sample->path = file.getFullPathName();
            sample->sampleRate = reader->sampleRate;
            for (int c = 0; c < channels; ++c)
                sample->channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);
            return sample;
        }

        juce::String encode (const Sample& sample)
        {
            if (sample.length() < 2)
                return {};
            auto peak = 0.0f;
            for (const auto& channel : sample.channels)
                for (const auto x : channel)
                    peak = juce::jmax (peak, std::abs (x));
            const auto scale = peak > 0.0f ? peak : 1.0f;
            juce::MemoryOutputStream compressed;
            {
                juce::GZIPCompressorOutputStream zip (compressed, 9);
                zip.writeInt ((int) sample.channels.size());
                zip.writeInt (sample.length());
                zip.writeDouble (sample.sampleRate);
                zip.writeFloat (scale);
                for (const auto& channel : sample.channels)
                    for (const auto x : channel)
                        zip.writeShort ((short) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, x / scale) * 32767.0f));
            }
            return juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize());
        }

        std::vector<int> findHits (const Sample& sample)
        {
            std::vector<int> hits;
            const auto length = sample.length();
            if (length < 2)
                return hits;
            // The level per 2.5 ms window (the louder side).
            const auto window = juce::jmax (1, juce::roundToInt (0.0025 * sample.sampleRate));
            std::vector<float> peaks;
            for (int start = 0; start < length; start += window)
            {
                auto peak = 0.0f;
                for (const auto& channel : sample.channels)
                    for (int i = start; i < juce::jmin (length, start + window); ++i)
                        peak = juce::jmax (peak, std::abs (channel[(size_t) i]));
                peaks.push_back (peak);
            }
            const auto loudest = *std::max_element (peaks.begin(), peaks.end());
            if (loudest <= 1.0e-6f)
                return hits;

            const auto gap = (int) (0.04 * sample.sampleRate / window);   // hits at least 40 ms apart
            auto last = -gap;
            for (int w = 0; w < (int) peaks.size(); ++w)
            {
                // The quiet before it: the lowest window in the 20 ms before (silence before the file's start).
                auto before = w < 8 ? 0.0f : peaks[(size_t) w - 1];
                for (int k = juce::jmax (0, w - 8); k < w; ++k)
                    before = juce::jmin (before, peaks[(size_t) k]);
                if (peaks[(size_t) w] < 0.1f * loudest || peaks[(size_t) w] < 4.0f * before + 1.0e-6f || w - last < gap)
                    continue;
                // Back to where the rise begins: the first sample (from the window before) above 5% of this window's
                // level, then back to the zero crossing just before it (at most 2 ms).
                const auto threshold = 0.05f * peaks[(size_t) w];
                auto at = juce::jmax (0, (w - 1) * window);
                const auto& c = sample.channels[0];
                while (at < juce::jmin (length - 1, (w + 1) * window) && std::abs (c[(size_t) at]) < threshold)
                    ++at;
                const auto limit = juce::jmax (0, at - (int) (0.002 * sample.sampleRate));
                while (at > limit && (c[(size_t) at] > 0.0f) == (c[(size_t) at - 1] > 0.0f) && std::abs (c[(size_t) at - 1]) > 1.0e-6f)
                    --at;
                hits.push_back (at);
                last = w;
            }
            return hits;
        }

        std::shared_ptr<const Sample> decode (const juce::String& name, const juce::String& text)
        {
            juce::MemoryOutputStream compressed;
            if (! juce::Base64::convertFromBase64 (compressed, text))
                return nullptr;
            juce::MemoryInputStream input (compressed.getData(), compressed.getDataSize(), false);
            juce::GZIPDecompressorInputStream zip (input);
            const auto channels = zip.readInt(), length = zip.readInt();
            const auto rate = zip.readDouble();
            const auto scale = zip.readFloat();
            if (channels < 1 || channels > 2 || length < 2 || length > (int) (maxSeconds * 192000.0) || ! (rate > 0.0 && rate <= 768000.0))
                return nullptr;
            auto sample = std::make_shared<Sample>();
            sample->name = name;
            sample->sampleRate = rate;
            sample->channels.assign ((size_t) channels, std::vector<float> ((size_t) length));
            for (auto& channel : sample->channels)
                for (auto& x : channel)
                {
                    if (zip.isExhausted())
                        return nullptr;
                    x = (float) zip.readShort() / 32767.0f * scale;
                }
            return sample;
        }
    }

    void Svf::set (double hz, double q, double sampleRate) noexcept
    {
        const auto fc = juce::jlimit (10.0, 0.45 * sampleRate, hz);
        g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        ic1 = ic2 = 0.0f;
    }

    //==============================================================================
    void Voice::start (const SlotSettings& slotSettings, int slotIndex, float velocityGain, double rate, std::uint32_t seed,
                       const HitOptions& options) noexcept
    {
        auto s = slotSettings;
        s.pitch += options.pitch;
        active = true;
        reversed = false;
        gateLeft = options.gateSamples;
        {
            // The note's own pan: a balance, centre unchanged.
            const auto pan = juce::jlimit (-1.0f, 1.0f, options.pan);
            panLeft = pan > 0.0f ? 1.0f - pan : 1.0f;
            panRight = pan < 0.0f ? 1.0f + pan : 1.0f;
        }
        slot = slotIndex;
        model = s.model;
        choke = s.choke;
        sampleRate = rate;
        t = 0.0;
        fade = 1.0f;
        fadeStep = 0.0f;
        noise = Noise (seed);
        phase = {};
        pingPhase = {};
        increment = {};
        burst = 0;
        env = env2 = pitchEnv = click = 1.0f;
        envCoef = env2Coef = pitchCoef = clickCoef = 0.0f;
        sample = nullptr;
        level = velocityGain * modelLevel[(size_t) model];
        shape = nullptr;
        shapeGain = 1.0f;
        // DELAY (6c): a hit set later waits; one set early is the sequencer's business (it starts it early).
        waitLeft = s.delay > 0.0f ? juce::roundToInt (s.delay * rate) : 0;

        // REVERSE: the slot's sample (its START to END) or its baked hit, played backwards; a baked hit has its
        // pitch and shaping in it already, so only the note's own pitch moves it.
        if (options.reverse != drumseq::Reverse::off && s.reverseSource != nullptr && s.reverseSource->length() >= 3)
        {
            model = Model::sample;
            sample = s.reverseSource;
            reversed = true;
            level = velocityGain;
            const auto length = sample->length();
            const auto from = s.reverseBaked ? 0.0 : juce::jlimit (0.0, 1.0, (double) s.start);
            const auto to = s.reverseBaked ? 1.0 : juce::jlimit (from, 1.0, (double) s.end);
            startPosition = juce::jlimit (0.0, (double) length - 2.0, std::floor (from * length));
            // From one sample short of the end (playing reads one ahead).
            endPosition = juce::jlimit (startPosition + 1.0, (double) length - 2.0, std::floor (to * length));
            position = endPosition;
            step = std::exp2 ((s.reverseBaked ? options.pitch : s.pitch) / 12.0) * sample->sampleRate / rate;
            endFade = 0.001 * rate * step;   // its end (the hit's attack, backwards) eased over 1 ms
            envCoef = 1.0f;
            setTone (s.reverseBaked ? 0.5 : (double) s.tone, rate);   // a baked hit has its TONE in it already
            attackTime = 0.0f;
            spike = 0.0f;
            startShape (s, endPosition - startPosition);
            return;
        }

        const auto ratio = std::exp2 (s.pitch / 12.0);
        const auto d = (double) juce::jlimit (0.0f, 1.0f, s.decay);
        const auto tone = (double) juce::jlimit (0.0f, 1.0f, s.tone);
        const auto snap = juce::jlimit (0.0f, 1.0f, s.snap);
        const auto tau = [rate] (double seconds) { return decayCoefficient (seconds, rate); };

        switch (model)
        {
            case Model::acousticKick:
            case Model::acousticSnare:
            case Model::acousticTom:
                startAcoustic (s, velocityGain, rate);
                break;

            case Model::acousticCrash:
            case Model::acousticRide:
            case Model::acousticChina:
            case Model::acousticSplash:
            case Model::acousticStack:
            case Model::acousticHat:
            case Model::acousticHatPedal:
                startCymbal (s, velocityGain, rate);
                break;

            case Model::kick:
                // A sine falling from (1 + BEND) times its pitch to 52 Hz; TONE sets how far it falls; SNAP the
                // beater's click (high-passed noise, 1.5 ms).
                baseHz = 52.0 * ratio;
                bend = 0.5 + 6.0 * tone * tone;
                pitchCoef = tau (0.016);
                envCoef = tau (0.03 + 0.5 * d * d);
                clickCoef = tau (0.0015);
                noiseLevel = 0.7f * snap;
                f1.set (2500.0, 0.7, rate);
                break;

            case Model::snare:
                // Two drumhead modes (the second 1.59 times the first) that drop a little at the hit, and the wires:
                // noise, high-passed at 700 Hz and low-passed at TONE's 3-12 kHz; SNAP sets the wires' level.
                baseHz = 180.0 * ratio;
                bend = 0.2;
                pitchCoef = tau (0.008);
                envCoef = tau (0.03 + 0.15 * d * d);
                env2Coef = tau (0.03 + 0.25 * d * d);
                noiseLevel = 0.15f + 0.85f * snap;
                f1.set (700.0, 0.7, rate);
                f2.set (3000.0 + 9000.0 * tone, 0.7, rate);
                break;

            case Model::clap:
                // Noise through a band-pass (TONE moves it), in three slaps 10 ms apart and a fourth that rings for
                // DECAY. SNAP: the slaps' level against the ring.
                baseHz = ratio;
                env = env2 = 0.0f;
                envCoef = tau (0.0035);
                env2Coef = tau (0.04 + 0.25 * d * d);
                noiseLevel = 0.6f + 0.8f * snap;
                f1.set (1100.0 * ratio * (0.7 + 0.6 * tone), 1.6, rate);
                f2.set (400.0, 0.7, rate);
                break;

            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:
            {
                // Six squares at unrelated pitches, plus noise (SNAP: the sizzle), through two high-passes at TONE's
                // cutoff (hats around 7 kHz, the cymbal around 3.5 kHz).
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    increment[i] = juce::jmin (0.45, metalHz[i] * ratio / rate);
                    phase[i] = metalStart[i];
                }
                const auto cutoff = (model == Model::cymbal ? 3500.0 : 7000.0) * ratio * (0.7 + 0.6 * tone);
                f1.set (cutoff, 0.7, rate);
                f2.set (cutoff, 1.0, rate);
                f3.set (cutoff * (model == Model::cymbal ? 3.0 : 1.8), 0.7, rate);   // the very top tamed
                noiseLevel = 0.15f + 0.7f * snap;
                envCoef = tau (model == Model::closedHat ? 0.008 + 0.08 * d * d
                                                         : model == Model::openHat ? 0.05 + 0.4 * d * d : 0.25 + 2.8 * d * d);
                break;
            }

            case Model::ride:
            {
                // Our own: the cymbal's six squares a little higher, high-passed brighter and with less noise (a ride
                // washes less than a crash), ringing long; and the stick's ping, two inharmonic sines near 2.6 kHz
                // that die sooner. SNAP: the ping's level; TONE: the wash's brightness.
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    increment[i] = juce::jmin (0.45, metalHz[i] * 1.35 * ratio / rate);
                    phase[i] = metalStart[i];
                }
                const auto cutoff = 5000.0 * ratio * (0.7 + 0.6 * tone);
                f1.set (cutoff, 0.7, rate);
                f2.set (cutoff, 1.0, rate);
                f3.set (cutoff * 2.5, 0.7, rate);
                noiseLevel = 0.05f + 0.3f * (1.0f - snap);
                envCoef = tau (0.4 + 2.4 * d * d);
                baseHz = 2650.0 * ratio;
                env2Coef = tau (0.06 + 0.3 * d);
                mix = 0.15f + 0.6f * snap;
                break;
            }

            case Model::bell:
                // Our own: four inharmonic sines from 1.1 kHz (bellRatio), the upper two dying sooner; TONE: how loud
                // the upper ones are; SNAP: the stick's click (high-passed noise, 3 ms).
                baseHz = 1100.0 * ratio;
                envCoef = tau (0.3 + 2.0 * d * d);
                env2Coef = tau ((0.3 + 2.0 * d * d) * 0.35);
                clickCoef = tau (0.003);
                mix = (float) (0.3 + 1.0 * tone);
                noiseLevel = 0.8f * snap;
                f1.set (4000.0, 0.7, rate);
                break;

            case Model::tambourine:
            {
                // Our own: metal (the six squares, twice as high) high-passed around TONE's 6 kHz, struck three times in
                // 14 ms as the jingles hit one after another, then ringing for DECAY. SNAP: the noise in the jingles.
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    increment[i] = juce::jmin (0.45, metalHz[i] * 2.2 * ratio / rate);
                    phase[i] = metalStart[i];
                }
                const auto cutoff = 6000.0 * ratio * (0.7 + 0.6 * tone);
                f1.set (cutoff, 0.7, rate);
                f2.set (cutoff, 1.0, rate);
                f3.set (juce::jmin (0.45 * rate, cutoff * 2.2), 0.7, rate);
                baseHz = ratio;
                env = env2 = 0.0f;
                envCoef = tau (0.018);
                env2Coef = tau (0.06 + 0.35 * d * d);
                noiseLevel = 0.2f + 0.8f * snap;
                break;
            }

            case Model::shaker:
                // Our own: noise band-passed around TONE's 5 kHz, swelling in (SNAP: faster, 25 ms to 4 ms) and dying
                // away (DECAY): the swish of the beads.
                f1.set (juce::jmin (0.45 * rate, 5000.0 * ratio * (0.7 + 0.6 * tone)), 1.4, rate);
                click = 1.0f;
                clickCoef = tau (0.025 - 0.021 * snap);
                envCoef = tau (0.03 + 0.2 * d * d);
                break;

            case Model::tom:
                // A sine falling to its pitch (TONE: from how high), a second mode at 1.5 times that dies sooner, and
                // SNAP's stick click (band-passed noise, 4 ms).
                baseHz = 100.0 * ratio;
                bend = 0.15 + 1.2 * tone;
                pitchCoef = tau (0.05);
                envCoef = tau (0.05 + 0.5 * d * d);
                env2Coef = tau ((0.05 + 0.5 * d * d) * 0.45);
                clickCoef = tau (0.004);
                noiseLevel = 2.0f * snap;
                f1.set (2000.0 * ratio, 1.0, rate);
                break;

            case Model::rim:
                // A high and a low ring (TONE balances them), very short, with SNAP's click.
                increment[0] = 1700.0 * ratio / rate;
                increment[1] = 480.0 * ratio / rate;
                envCoef = tau (0.004 + 0.03 * d * d);
                env2Coef = tau ((0.004 + 0.03 * d * d) * 1.5);
                clickCoef = tau (0.002);
                mix = (float) (0.4 + 0.6 * tone);
                noiseLevel = 0.6f * snap;
                f1.set (2500.0 * ratio, 1.2, rate);
                f2.set (200.0, 0.7, rate);
                break;

            case Model::cowbell:
                // Two squares through a band-pass (TONE moves it); a quick bright strike (SNAP) and a ring (DECAY).
                increment[0] = 587.0 * ratio / rate;
                increment[1] = 845.0 * ratio / rate;
                f1.set (1800.0 * ratio * (0.7 + 0.6 * tone), 1.2, rate);
                envCoef = tau (0.012);
                env2Coef = tau (0.04 + 0.3 * d * d);
                noiseLevel = 0.3f + 0.7f * snap;
                break;

            case Model::sample:
            {
                sample = s.sample;
                if (sample == nullptr || sample->length() < 2)
                {
                    active = false;
                    return;
                }
                // START and END: the part of the file played (a single hit out of a file of several). Cut short of
                // the file's end, it fades over its last 3 ms so the cut doesn't click.
                const auto length = sample->length();
                const auto from = juce::jlimit (0.0, 1.0, (double) s.start), to = juce::jlimit (0.0, 1.0, (double) s.end);
                position = juce::jlimit (0.0, (double) length - 2.0, std::floor (from * length));
                startPosition = position;
                endPosition = juce::jlimit (position + 1.0, (double) length - 1.0, std::floor (juce::jmax (from, to) * length));
                step = ratio * sample->sampleRate / rate;
                endFade = endPosition < length - 1.0 ? 0.003 * rate * step : 0.0;
                // 0.28.0: a streamed hit plays its head (faded at its end) until the kit gives it its stream.
                streamer = nullptr;
                streamId = -1;
                streamOutput = sample->streamOutput;
                if (sample->stream != nullptr)
                    endFade = 0.005 * rate * step;
                // DECAY: the whole sample from the middle up; below it, a fade that shortens it.
                envCoef = d >= 0.5 ? 1.0f : tau (0.005 + 1.5 * std::pow (d / 0.5, 3.0));
                // TONE: darker (a low-pass down to 500 Hz) or thinner (a high-pass up to 2 kHz), flat at the middle.
                setTone (tone, rate);
                // SNAP: a softer attack (up to a 10 ms fade-in) or a spike (up to +9.5 dB, gone in a few ms).
                attackTime = snap < 0.5f ? (0.5f - snap) * 2.0f * 0.01f : 0.0f;
                spike = snap > 0.5f ? (snap - 0.5f) * 2.0f * 2.0f : 0.0f;
                clickCoef = tau (0.0015);
                break;
            }
        }
        if (active)
            startShape (s, model == Model::sample ? endPosition - startPosition : 0.0);
    }

    void Voice::startAcoustic (const SlotSettings& s, float velocityGain, double rate) noexcept
    {
        // THE ACOUSTIC MODELS (0.24.0): modal synthesis of a drum from its physics, our own design and numbers (no
        // recordings). The head's modes are the ideal membrane's (headModes), their spacing pulled in a little for the
        // air and the second head (a real drum's are closer than the ideal's), each:
        //   - excited by where it's struck: a mode's shape there, J_m (j r) (its norm divided out); a mode with
        //     nodal diameters (m > 0) radiates less (it pushes air both ways), so it's weighted down;
        //   - shaped by what strikes it: the stick or beater's contact time (TONE: soft felt about 3.5 ms to hard
        //     plastic or wood about 0.3 ms, shorter on a hard hit) passes the modes below about 1 / contact and
        //     rolls off the rest (a smoothed pulse spectrum); the contact is also the hit's rise;
        //   - dying at its own rate: DECAY (the muffling) sets the lowest mode's time, higher ones die faster the
        //     more it's muffled;
        //   - bent by a hard hit: the head's tension rises with its swing, so the pitch starts high and settles
        //     (how much: the velocity squared).
        // Then the drum's own parts: the kick's front head (SHELL: two modes a little above the batter's, ringing
        // longer), a snare's or tom's shell (three fixed modes), the beater's click or the stick's (SNAP on the kick and
        // tom), the snare wires (SNAP: noise buzzing with the head, stopping once its swing falls below their
        // tension). VARIATION moves where and how hard each hit lands, its contact and its tuning a little.
        auto& md = modes;
        md = {};
        const auto twelfthRoot = [] (double semitones) { return std::exp2 (semitones / 12.0); };
        const auto rand = [this] { return (double) noise.white(); };   // -1 to 1 (the hit's own seed)
        const auto variation = (double) juce::jlimit (0.0f, 1.0f, s.variation);
        const auto kick = model == Model::acousticKick, snare = model == Model::acousticSnare;
        // How hard it's struck, from its level (the velocity and its curve): 0 to 1.
        auto hardness = juce::jlimit (0.05, 1.0, std::sqrt ((double) velocityGain) * (1.0 + 0.08 * variation * rand()));
        const auto radius = juce::jlimit (0.02, 0.95, 0.02 + 0.9 * (double) s.position + 0.06 * variation * rand());
        const auto f0 = (kick ? 50.0 : snare ? 190.0 : 100.0) * twelfthRoot (s.pitch + 0.1 * variation * rand());
        const auto stretch = kick ? 0.8 : snare ? 0.9 : 0.82;
        const auto count = kick ? 16 : snare ? 24 : 20;
        const auto d = (double) juce::jlimit (0.0f, 1.0f, s.decay);
        const auto muffle = 1.0 - d;
        const auto t0 = kick ? 0.05 + 0.9 * d * d : snare ? 0.08 + 0.6 * d * d : 0.15 + 2.2 * d * d;
        auto contact = 0.0035 * std::pow (0.08, (double) juce::jlimit (0.0f, 1.0f, s.tone)) * std::pow (hardness, -0.5)
                     * (1.0 + 0.15 * variation * rand());
        contact = juce::jlimit (0.0002, 0.006, contact);
        const auto spectrum = [contact] (double hz) { return 1.0 / std::sqrt (1.0 + std::pow (hz * contact * 1.5, 4.0)); };
        const auto nyquistSafe = 0.45 * rate;

        auto total = 0.0;   // the head's modes' amplitudes so far
        const auto add = [&] (double hz, double amp, double seconds, float glides)
        {
            if (md.count >= Modes::max || hz >= nyquistSafe || std::abs (amp) < 1.0e-3 * juce::jmax (1.0e-9, total))
                return;   // (a mode a thousandth of the rest so far: never heard)
            const auto k = (size_t) md.count++;
            md.hz[k] = hz;
            md.amp[k] = (float) amp;
            md.env[k] = 1.0f;
            md.coef[k] = decayCoefficient (juce::jmax (0.005, seconds), rate);
            md.coef16[k] = std::pow (md.coef[k], 16.0f);
            md.glides[k] = glides;
            // A sine from nothing: x[0] = 0, x[-1] = -sin (w) / r (so x[1] = r sin (w)).
            const auto w = twoPi * hz / rate;
            md.y1[k] = 0.0f;
            md.y2[k] = (float) (-std::sin (w) / md.coef[k]);
            md.c[k] = (float) (2.0 * md.coef[k] * std::cos (w));
            md.r2[k] = md.coef[k] * md.coef[k];
        };
        for (int i = 0; i < count; ++i)
        {
            const auto& mode = headModes[(size_t) i];
            const auto ratio = 1.0 + (mode.j / headModes[0].j - 1.0) * stretch;
            const auto hz = f0 * ratio;
            const auto modeShape = besselJ (mode.m, mode.j * radius) / std::abs (besselJ (mode.m + 1, mode.j));
            const auto radiation = mode.m == 0 ? 1.0 : mode.m == 1 ? 0.6 : 0.4;
            const auto amp = std::abs (modeShape) * radiation * spectrum (hz) / std::sqrt (ratio);
            add (hz, amp, t0 / (1.0 + (0.4 + 2.5 * muffle) * (ratio - 1.0)), 1.0f);
            total += amp;
        }
        const auto shell = (double) juce::jlimit (0.0f, 1.0f, s.shell);
        if (kick)
        {
            // The front head: tuned a little above the batter, its own modes ringing on longer (a port in it would
            // shorten them: less SHELL).
            add (f0 * 1.06, total * 0.35 * shell, t0 * 1.6 + 0.1, 0.5f);
            add (f0 * 1.06 * (1.0 + (headModes[3].j / headModes[0].j - 1.0) * stretch), total * 0.08 * shell, t0 * 0.8 + 0.05, 0.5f);
        }
        else
        {
            // The shell's own ring, fixed against the head's pitch (our ratios).
            for (const auto& [ratio, shellLevel] : { std::pair { 1.85, 0.12 }, std::pair { 3.4, 0.08 }, std::pair { 5.6, 0.05 } })
                add (f0 * ratio, total * shellLevel * shell * spectrum (f0 * ratio), 0.12 + 0.1 * d, 0.0f);
        }
        // Normalised (the hit's loudness is the model's level and the velocity's), sines starting at nothing.
        const auto scale = total > 0.0 ? 1.0 / total : 1.0;
        for (int k = 0; k < md.count; ++k)
            md.amp[(size_t) k] *= (float) scale;
        md.riseCoef = (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0, 0.35 * contact * rate)));
        md.glide = (float) ((kick ? 0.12 : snare ? 0.04 : 0.10) * hardness * hardness);
        md.glideEnv = 1.0f;
        md.glideCoef = decayCoefficient (0.5 * t0, rate);

        // The click: the beater's (kick: SNAP, brighter with a harder beater) or the stick's (snare, tom).
        const auto snap = juce::jlimit (0.0f, 1.0f, s.snap);
        click = 1.0f;
        clickCoef = decayCoefficient (1.5 * contact + 0.0008, rate);
        f1.set (kick ? 1800.0 + 3000.0 * s.tone : snare ? 3000.0 : 2500.0, 1.2, rate);
        // (A soft hit's click is quieter still against its body: the contact is gentler.)
        noiseLevel = (float) hardness * (kick ? 2.0f * snap : snare ? 0.4f : 0.8f * snap);
        // The snare wires: noise high-passed at 2 kHz and low-passed by TONE, buzzing with the head's swing.
        md.wires = snare ? snap : 0.0f;
        md.wireThreshold = 0.04f;
        f2.set (2000.0, 0.7, rate);
        f3.set (3000.0 + 9000.0 * s.tone, 0.7, rate);
    }

    void Voice::startCymbal (const SlotSettings& s, float velocityGain, double rate) noexcept
    {
        // THE ACOUSTIC CYMBALS (0.25.0, our own design from how cymbals behave; no recordings):
        //   - the plate's modes: 40, log-spaced from the cymbal's lowest (its "gong") up to 14 kHz, each nudged by a
        //     fixed pattern of its own (so a crash always has the same voice), loudest around the cymbal's bright
        //     region, rolled off above what the stick's contact passes (TONE), the high ones dying sooner;
        //   - the bell: four strong partials, ringing long, loud where POSITION is near the bell (0);
        //   - the WASH: struck hard, a cymbal's energy spreads from its first modes to a dense shimmer that blooms
        //     after the hit (tens of milliseconds) and dies with its highs going first: noise in eight bands shaped
        //     like the modes, built up, falling in brightness, shimmering slowly. More towards the edge (POSITION),
        //     with WASH (SHELL) and with a harder hit (faster too);
        //   - the stick's PING (SNAP): a short bright click at the contact;
        //   - CHINA: a honky middle and a hard, fast bloom, then a soft clip (the trash: tones beating into new
        //     ones); STACK: two clamped together, short, trashy, choked;
        //   - HI-HAT: OPEN (SHELL) from tight and short to fully open, the two plates rattling against each other
        //     most when half open; HAT PEDAL: the plates clapped by the foot (a chick, a little low thump).
        auto& md = modes;
        md = {};
        auto& cy = cymbal;
        cy = {};
        const auto rand = [this] { return (double) noise.white(); };
        const auto variation = (double) juce::jlimit (0.0f, 1.0f, s.variation);
        const auto d = (double) juce::jlimit (0.0f, 1.0f, s.decay);
        const auto tone = (double) juce::jlimit (0.0f, 1.0f, s.tone);
        const auto snap = (double) juce::jlimit (0.0f, 1.0f, s.snap);
        const auto wash = (double) juce::jlimit (0.0f, 1.0f, s.shell);
        auto hardness = juce::jlimit (0.05, 1.0, std::sqrt ((double) velocityGain) * (1.0 + 0.08 * variation * rand()));
        const auto strike = juce::jlimit (0.0, 1.0, (double) s.position + 0.05 * variation * rand());
        const auto tune = std::exp2 ((s.pitch + 0.05 * variation * rand()) / 12.0);

        // Each type: its lowest mode, its bright region, how long it rings, its bell, how fast and how strong its wash.
        struct Type { double low, bright, seconds, bellHz, bellLevel, build, washLevel; };
        // (`seconds`: its time constant; it's 60 dB down about 7 times later: a crash in about 6 s, a ride 8, a china
        // 3.5, a splash 2, a stack 0.6.)
        Type type { 200.0, 4200.0, 0.87, 950.0, 0.5, 0.025, 1.0 };   // the crash
        if (model == Model::acousticRide)          type = { 240.0, 4000.0, 1.16, 1150.0, 1.0, 0.07, 0.45 };
        else if (model == Model::acousticChina)    type = { 280.0, 2200.0, 0.5, 900.0, 0.3, 0.012, 1.3 };
        else if (model == Model::acousticSplash)   type = { 520.0, 6000.0, 0.29, 1700.0, 0.4, 0.012, 1.0 };
        else if (model == Model::acousticStack)    type = { 400.0, 3500.0, 0.09, 1300.0, 0.2, 0.003, 1.4 };
        else if (model == Model::acousticHat)      type = { 480.0, 8000.0, 0.0, 2000.0, 0.15, 0.002, 1.0 };
        else if (model == Model::acousticHatPedal) type = { 520.0, 7000.0, 0.0, 2100.0, 0.0, 0.002, 1.0 };
        const auto hat = model == Model::acousticHat, pedal = model == Model::acousticHatPedal;
        const auto china = model == Model::acousticChina, stack = model == Model::acousticStack;
        // How long it rings (DECAY 0.5: the type's own); a hat: by how open it is.
        const auto open = (double) juce::jlimit (0.0f, 1.0f, s.shell);
        auto seconds = type.seconds * std::exp2 (2.0 * (d - 0.5));
        if (hat)
            seconds = (0.022 + 0.42 * std::pow (open, 1.8)) * std::exp2 (1.5 * (d - 0.5));   // closed 0.15 s, open 3 s
        if (pedal)
            seconds = 0.015 + 0.02 * d;

        // The modes: log-spaced from the lowest to 14 kHz, each nudged by the type's own fixed pattern.
        auto pattern = (std::uint32_t) (1 + (int) model * 7919);
        const auto fixedRand = [&pattern]
        {
            pattern = pattern * 1664525u + 1013904223u;
            return (double) (std::int32_t) pattern / 2147483648.0;
        };
        // A stick on metal: a far shorter contact than on a head (0.5 ms for a soft tip, 0.13 ms for a hard one), and
        // a gentler roll-off above it.
        const auto contact = juce::jlimit (0.00008, 0.0008, 0.0005 * std::pow (0.25, tone) * std::pow (hardness, -0.4));
        const auto stick = [contact] (double hz) { return 1.0 / std::sqrt (1.0 + std::pow (hz * contact * 2.0, 2.0)); };
        const auto brightShape = [&type] (double hz)
        {
            const auto x = hz / type.bright;
            return std::sqrt (x) / (1.0 + std::pow (x, 1.5));
        };
        const auto add = [&] (double hz, double amp, double ringSeconds)
        {
            if (md.count >= Modes::max || hz >= 0.45 * rate || amp < 1.0e-5)
                return;
            const auto k = (size_t) md.count++;
            md.hz[k] = hz;
            md.amp[k] = (float) amp;
            md.env[k] = 1.0f;
            md.coef[k] = decayCoefficient (juce::jmax (0.004, ringSeconds), rate);
            md.coef16[k] = std::pow (md.coef[k], 16.0f);
            const auto w = twoPi * hz / rate;
            md.y2[k] = (float) (-std::sin (w) / md.coef[k]);
            md.c[k] = (float) (2.0 * md.coef[k] * std::cos (w));
            md.r2[k] = md.coef[k] * md.coef[k];
        };
        constexpr int plateModes = 40;
        const auto low = type.low * tune, top = 14000.0;
        // The edge excites the low "gong" more, the bow less; nearer the bell, fewer of the plate's modes.
        const auto lowBoost = 0.6 + 0.8 * strike;
        // Struck on the bell, the plate's own modes sound less (the bell's thick and stiff; it rings in its own).
        const auto onBell = 1.0 - juce::jlimit (0.0, 1.0, strike / 0.35);
        const auto plateShare = 1.0 - 0.6 * onBell;
        auto total = 0.0;
        for (int k = 0; k < plateModes; ++k)
        {
            const auto x = (k + 0.5 + 0.4 * fixedRand()) / plateModes;
            const auto hz = low * std::pow (top / low, x) * (1.0 + 0.004 * variation * rand());
            // (Log-spaced, each mode stands for a band as wide as its frequency: sqrt (hz) gives each its share.)
            auto amp = brightShape (hz) * stick (hz) * std::sqrt (hz / 1000.0) * (0.6 + 0.4 * std::abs (fixedRand()));
            if (hz < 1500.0)
                amp *= lowBoost;
            amp *= plateShare;
            if (china && hz > 600.0 && hz < 1500.0)
                amp *= 2.2;   // its honk
            const auto ring = seconds * juce::jlimit (0.3, 1.4, std::pow (1000.0 / hz, 0.35));
            add (hz, amp, juce::jmax (0.01, ring));
            total += amp / plateShare;   // (normalised by the plate's full strength: the bell doesn't make it louder)
        }
        // The bell: strongest where the stick strikes it.
        const auto bellAmount = type.bellLevel * (0.06 + 3.0 * onBell * onBell);
        const std::array<double, 4> bellRatios { 1.0, 1.51, 2.24, 2.76 }, bellLevels { 1.0, 0.6, 0.45, 0.3 };
        for (size_t b = 0; b < bellRatios.size(); ++b)
            add (type.bellHz * tune * bellRatios[b], total * 0.08 * bellAmount * bellLevels[b] * stick (type.bellHz * bellRatios[b]),
                 (hat || pedal ? seconds : seconds * 1.2) * std::pow (1000.0 / (type.bellHz * bellRatios[b]), 0.3));
        // (Forty unrelated sines add up to much less than their sum: three times the drums' share, so the strike is the
        // loudest moment, as on a real cymbal, the wash blooming just under it.)
        const auto scale = total > 0.0 ? 3.0 / total : 1.0;
        for (int k = 0; k < md.count; ++k)
            md.amp[(size_t) k] *= (float) scale;
        md.riseCoef = (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0, 0.35 * contact * rate)));
        md.glide = 0.0f;
        md.glideEnv = 0.0f;
        md.glideCoef = 1.0f;

        // The wash: eight bands from 1.2 to 15 kHz shaped like the modes; built up over `build` (sooner on a hard
        // hit), dying over the ring, its brightness falling from 18 kHz towards a third of it.
        // Towards the edge the wash grows faster than straight (the plate thrown into big, trashy motion); on the bow
        // the stick's ping stands out (below).
        const auto edge = 0.25 + 1.2 * std::pow (strike, 1.5);
        cy.washLevel = (float) (type.washLevel * (pedal ? 1.0 : edge) * (0.25 + 1.5 * wash * (hat || pedal ? 0.5 : 1.0))
                                * std::pow (hardness, 0.5) * (hat || pedal ? 1.0 : 0.6));
        for (size_t b = 0; b < cy.bands.size(); ++b)
        {
            const auto hz = 1200.0 * std::pow (15000.0 / 1200.0, (double) b / 7.0) * juce::jmin (1.4, tune);
            cy.bands[b].set (juce::jmin (hz, 0.45 * rate), 2.0, rate);
            cy.weights[b] = (float) (brightShape (hz) * (china && hz < 2500.0 ? 1.6 : 1.0));
        }
        cy.build = 0.0f;
        cy.buildCoef = (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0, type.build * std::pow (hardness, -0.7) * rate)));
        cy.washEnv = 1.0f;
        cy.washCoef = decayCoefficient (juce::jmax (0.01, seconds * 0.75), rate);
        cy.lp = 0.0f;
        cy.lpCoef = 1.0f;   // starts open; falls towards lpFloor
        cy.lpFloor = (float) (1.0 - std::exp (-twoPi * 5000.0 / rate));
        cy.lpFall = decayCoefficient (juce::jmax (0.02, seconds * 0.5), rate);
        cy.lfoPhase = 0.5 + 0.5 * rand();
        cy.lfoStep = (4.0 + 4.0 * std::abs (rand())) / rate;
        cy.shimmer = hat || pedal || stack ? 0.0f : 0.18f;
        // The trash: a soft clip, harder for the china (more with SNAP: TRASH) and the stack.
        cy.drive = china ? (float) (1.5 + 4.0 * snap) : stack ? 4.0f : 0.0f;
        // A hi-hat: below 350 Hz out (no gong); the plates rattling, most when half open; the pedal's thump.
        cy.highPass = hat || pedal ? (float) (1.0 - std::exp (-twoPi * 350.0 / rate)) : 0.0f;
        cy.rattle = hat ? (float) (0.7 * std::sin (juce::MathConstants<double>::pi * juce::jlimit (0.0, 1.0, open))) : 0.0f;
        cy.rattleMin = juce::jmax (1, (int) (rate / 60.0));
        cy.rattleSpread = juce::jmax (1, (int) (rate / 60.0));
        cy.rattleLeft = 0;
        cy.rattleGain = 1.0f;
        if (pedal)
        {
            cy.thump = (float) (0.35 * hardness);
            cy.thumpCoef = decayCoefficient (0.015, rate);
            cy.thumpHz = 180.0;
            cy.thumpPhase = 0.0;
        }

        // The stick's PING: a short bright click (none for the pedal: no stick).
        click = pedal ? 0.0f : 1.0f;
        clickCoef = decayCoefficient (2.0 * contact + 0.0005, rate);
        f1.set (juce::jmin (5500.0 * (0.6 + 0.8 * tone), 0.45 * rate), 1.0, rate);
        noiseLevel = (float) ((china ? 0.6 : 0.3 + 1.2 * snap) * hardness * (1.2 - 0.7 * strike));   // the ping: clearest on the bow
    }

    void Voice::enableStream (Streamer* to, int id, bool wait) noexcept
    {
        // DISK STREAMING (0.28.0): the whole hit, its head from memory and the rest from its stream.
        if (! active || model != Model::sample || sample == nullptr || sample->stream == nullptr || to == nullptr || id < 0)
            return;
        streamer = to;
        streamId = id;
        waitForDisk = wait;
        const auto full = (double) sample->frames() - 1.0;
        if (shape != nullptr && ! reversed)
            shapeDx *= juce::jmax (1.0, endPosition - startPosition) / juce::jmax (1.0, full - startPosition);
        endPosition = full;
        endFade = 0.0;
        streamer->attach (streamId, streamOutput);
    }

    void Voice::startShape (const SlotSettings& s, double regionSamples) noexcept
    {
        // The envelope runs across the hit from 0 to 1: a sample's own content (START to END, whatever its pitch,
        // and backwards when it's reversed); a drum synth's hit by time, over the length the slot gives.
        shape = envelopeShapes (s.envelope) ? s.envelope : nullptr;
        shapeCountdown = 0;
        shapeStep = 0.0f;
        if (shape == nullptr)
            return;
        if (model == Model::sample)
        {
            shapeDx = step / juce::jmax (1.0, regionSamples);
            shapeX = reversed ? 1.0 : 0.0;
            if (reversed)
                shapeDx = -shapeDx;
        }
        else
        {
            shapeDx = 1.0 / ((s.envelopeSeconds > 0.0f ? (double) s.envelopeSeconds : 1.0) * sampleRate);
            shapeX = 0.0;
        }
        shapeGain = shape->valueAt (shapeX);
    }

    bool Voice::advanceShape() noexcept
    {
        if (shape == nullptr)
            return true;
        if (--shapeCountdown <= 0)
        {
            shapeCountdown = 8;
            const auto target = shape->valueAt (shapeX + 8.0 * shapeDx);
            const auto past = shapeDx > 0.0 ? shapeX >= 1.0 : shapeX <= 0.0;
            if (past && shapeGain < 1.0e-4f && target < 1.0e-4f)
                return false;   // drawn down to nothing at its end: the hit is over
            shapeStep = (target - shapeGain) / 8.0f;
        }
        shapeGain += shapeStep;
        shapeX += shapeDx;
        return true;
    }

    float Voice::nextModes() noexcept
    {
        auto& md = modes;
        // Every 16 samples: each mode's tuning for the glide, its envelope, and which are still heard.
        if (--md.countdown <= 0)
        {
            md.countdown = 16;
            const auto glideNow = (double) (md.glide * md.glideEnv);
            auto loudest = 0.0f;
            for (int k = 0; k < md.count; ++k)
            {
                const auto i = (size_t) k;
                if (md.glides[i] > 0.0f && glideNow > 1.0e-7)   // (only a gliding mode's tuning moves)
                    md.c[i] = (float) (2.0 * md.coef[i] * std::cos (twoPi * md.hz[i] * (1.0 + glideNow * md.glides[i]) / sampleRate));
                loudest = juce::jmax (loudest, md.amp[i] * md.env[i]);
                md.env[i] *= md.coef16[i];
            }
            md.loudest = loudest;
            // Those still heard for the next 16 samples (faded below hearing, -120 dB: skipped).
            md.numHeard = 0;
            for (int k = 0; k < md.count; ++k)
                if (md.amp[(size_t) k] * md.env[(size_t) k] >= 1.0e-6f)
                    md.heard[(size_t) md.numHeard++] = (std::uint8_t) k;
        }
        auto sum = 0.0f;
        for (int h = 0; h < md.numHeard; ++h)
        {
            const auto i = (size_t) md.heard[(size_t) h];
            const auto y = md.c[i] * md.y1[i] - md.r2[i] * md.y2[i];
            md.y2[i] = md.y1[i];
            md.y1[i] = y;
            sum += md.amp[i] * y;
        }
        return sum;
    }

    float Voice::next() noexcept
    {
        float out = 0.0f;
        switch (model)
        {
            case Model::acousticCrash:
            case Model::acousticRide:
            case Model::acousticChina:
            case Model::acousticSplash:
            case Model::acousticStack:
            case Model::acousticHat:
            case Model::acousticHatPedal:
            {
                auto& md = modes;
                auto& cy = cymbal;
                md.rise += (1.0f - md.rise) * md.riseCoef;
                auto x = nextModes() * md.rise;
                // The wash: eight bands of noise, built up and dying, its brightness falling, shimmering.
                if (cy.washEnv > 1.0e-5f)
                {
                    const auto n = noise.white();
                    auto w = 0.0f;
                    for (size_t b = 0; b < cy.bands.size(); ++b)
                    {
                        cy.bands[b].process (n);
                        w += cy.bands[b].bp * cy.weights[b];
                    }
                    cy.build += (1.0f - cy.build) * cy.buildCoef;
                    cy.washEnv *= cy.washCoef;
                    cy.lpCoef = cy.lpFloor + (cy.lpCoef - cy.lpFloor) * cy.lpFall;
                    cy.lp += (w - cy.lp) * cy.lpCoef;
                    cy.lfoPhase += cy.lfoStep;
                    if (cy.lfoPhase >= 1.0)
                        cy.lfoPhase -= 1.0;
                    const auto shimmer = 1.0f + cy.shimmer * sine (cy.lfoPhase);
                    x += cy.lp * cy.build * cy.washEnv * cy.washLevel * shimmer;
                }
                // The stick's ping.
                if (click > 1.0e-5f)
                {
                    f1.process (noise.white());
                    x += f1.bp * click * noiseLevel;
                    click *= clickCoef;
                }
                // The pedal's thump.
                if (cy.thump > 1.0e-5f)
                {
                    advance (cy.thumpPhase, cy.thumpHz / sampleRate);
                    x += cy.thump * sine (cy.thumpPhase);
                    cy.thump *= cy.thumpCoef;
                }
                // The china's and stack's trash.
                if (cy.drive > 0.0f)
                    x = std::tanh (cy.drive * 3.0f * x) / 3.0f;   // clipping from about a third of full scale / drive
                // A hi-hat's plates rattling: the level jumping at random, 60 to 120 times a second.
                if (cy.rattle > 0.0f)
                {
                    if (--cy.rattleLeft <= 0)
                    {
                        cy.rattleLeft = cy.rattleMin + (int) (std::abs (noise.white()) * (float) cy.rattleSpread);
                        cy.rattleGain = 1.0f - cy.rattle * std::abs (noise.white());
                    }
                    x *= cy.rattleGain;
                }
                if (cy.highPass > 0.0f)
                {
                    cy.highState += (x - cy.highState) * cy.highPass;
                    x -= cy.highState;
                }
                out = x;
                break;
            }
            case Model::acousticKick:
            case Model::acousticSnare:
            case Model::acousticTom:
            {
                auto& md = modes;
                md.glideEnv *= md.glideCoef;
                md.rise += (1.0f - md.rise) * md.riseCoef;
                const auto sum = nextModes();
                out = sum * md.rise;
                // The click: band-passed noise as long as the contact.
                if (click > 1.0e-5f)
                {
                    f1.process (noise.white());
                    out += f1.bp * click * noiseLevel;
                    click *= clickCoef;
                }
                // The wires: buzzing while the head swings past their tension (its lowest mode's envelope), louder
                // as it swings out.
                if (md.wires > 0.0f && md.count > 0)
                {
                    const auto swing = (md.env[0] * md.rise - md.wireThreshold) / (1.0f - md.wireThreshold);
                    f2.process (noise.white());
                    f3.process (f2.hp);
                    if (swing > 0.0f)
                        out += f3.lp * swing * md.wires * (0.6f + 0.4f * juce::jmin (1.0f, std::abs (md.y1[0]) / juce::jmax (1.0e-6f, md.env[0])));
                }
                break;
            }
            case Model::kick:
            {
                pitchEnv *= pitchCoef;
                advance (phase[0], baseHz * (1.0 + bend * pitchEnv) / sampleRate);
                f1.process (noise.white());
                out = sine (phase[0]) * env + f1.hp * click * noiseLevel;
                env *= envCoef;
                click *= clickCoef;
                break;
            }
            case Model::snare:
            {
                pitchEnv *= pitchCoef;
                const auto hz = baseHz * (1.0 + bend * pitchEnv);
                advance (phase[0], hz / sampleRate);
                advance (phase[1], 1.59 * hz / sampleRate);
                const auto body = (0.65f * sine (phase[0]) + 0.35f * sine (phase[1])) * env;
                f1.process (noise.white());
                f2.process (f1.hp);
                out = 0.8f * body + f2.lp * env2 * noiseLevel;
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::clap:
            {
                if (burst < (int) clapBursts.size() && t >= clapBursts[(size_t) burst] / baseHz)
                {
                    env = 1.0f;
                    if (burst == (int) clapBursts.size() - 1)
                        env2 = 1.0f;
                    ++burst;
                }
                f2.process (noise.white());
                f1.process (f2.hp);
                out = f1.bp * (env * noiseLevel + env2 * 0.7f);
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:
            {
                auto metal = 0.0f;
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    metal += square (phase[i], increment[i]);
                    advance (phase[i], increment[i]);
                }
                f1.process (metal * (1.0f / 6.0f) + noiseLevel * noise.white());
                f2.process (f1.hp);
                f3.process (f2.hp);
                out = f3.lp * env;
                env *= envCoef;
                break;
            }
            case Model::ride:
            {
                auto metal = 0.0f;
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    metal += square (phase[i], increment[i]);
                    advance (phase[i], increment[i]);
                }
                f1.process (metal * (1.0f / 6.0f) + noiseLevel * noise.white());
                f2.process (f1.hp);
                f3.process (f2.hp);
                advance (pingPhase[0], baseHz / sampleRate);
                advance (pingPhase[1], 1.47 * baseHz / sampleRate);
                out = f3.lp * env + mix * (0.6f * sine (pingPhase[0]) + 0.4f * sine (pingPhase[1])) * env2;
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::bell:
            {
                float partials[2] {};
                for (size_t i = 0; i < bellRatio.size(); ++i)
                {
                    advance (phase[i], bellRatio[i] * baseHz / sampleRate);
                    partials[i / 2] += bellLevel[i] * sine (phase[i]);
                }
                f1.process (noise.white());
                out = 0.6f * (partials[0] * env + mix * partials[1] * env2) + f1.hp * click * noiseLevel;
                env *= envCoef;
                env2 *= env2Coef;
                click *= clickCoef;
                break;
            }
            case Model::tambourine:
            {
                if (burst < (int) jingleBursts.size() && t >= jingleBursts[(size_t) burst] / baseHz)
                {
                    env = 1.0f;
                    if (burst == 0)
                        env2 = 1.0f;
                    ++burst;
                }
                auto metal = 0.0f;
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    metal += square (phase[i], increment[i]);
                    advance (phase[i], increment[i]);
                }
                f1.process (metal * (1.0f / 6.0f) + noiseLevel * noise.white());
                f2.process (f1.hp);
                f3.process (f2.hp);
                out = f3.lp * (0.7f * env + 0.5f * env2);
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::shaker:
            {
                f1.process (noise.white());
                out = f1.bp * (1.0f - click) * env;
                env *= envCoef;
                click *= clickCoef;
                break;
            }
            case Model::tom:
            {
                pitchEnv *= pitchCoef;
                const auto hz = baseHz * (1.0 + bend * pitchEnv);
                advance (phase[0], hz / sampleRate);
                advance (phase[1], 1.5 * hz / sampleRate);
                f1.process (noise.white());
                out = sine (phase[0]) * env + 0.2f * sine (phase[1]) * env2 + f1.bp * click * noiseLevel;
                env *= envCoef;
                env2 *= env2Coef;
                click *= clickCoef;
                break;
            }
            case Model::rim:
            {
                advance (phase[0], increment[0]);
                advance (phase[1], increment[1]);
                f1.process (noise.white());
                f2.process (mix * sine (phase[0]) * env + (1.0f - mix) * sine (phase[1]) * env2 + f1.bp * click * noiseLevel);
                out = f2.hp;
                env *= envCoef;
                env2 *= env2Coef;
                click *= clickCoef;
                break;
            }
            case Model::cowbell:
            {
                const auto bell = square (phase[0], increment[0]) + square (phase[1], increment[1]);
                advance (phase[0], increment[0]);
                advance (phase[1], increment[1]);
                f1.process (0.5f * bell);
                out = f1.bp * (noiseLevel * env + 0.5f * env2);
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::sample:
                break;
        }
        t += 1.0 / sampleRate;
        return out * level;
    }

    bool Voice::finished() const noexcept
    {
        constexpr auto quiet = 1.0e-4f;   // -80 dB
        switch (model)
        {
            case Model::acousticKick:
            case Model::acousticSnare:
            case Model::acousticTom:
            {
                // Its loudest mode 80 dB under full scale, this hit's own level counted (a soft hit ends sooner).
                return click < quiet && modes.loudest * level < quiet;
            }
            case Model::acousticCrash:
            case Model::acousticRide:
            case Model::acousticChina:
            case Model::acousticSplash:
            case Model::acousticStack:
            case Model::acousticHat:
            case Model::acousticHatPedal:
                return click < quiet && modes.loudest * level < quiet && cymbal.washEnv * cymbal.washLevel * level < quiet
                    && cymbal.thump < quiet;
            case Model::kick:    return env < quiet && click < quiet;
            case Model::snare:   return env < quiet && env2 < quiet;
            case Model::clap:    return burst >= (int) clapBursts.size() && env2 < quiet && env < quiet;
            case Model::tom:     return env < quiet && click < quiet;
            case Model::rim:     return env < quiet && env2 < quiet && click < quiet;
            case Model::cowbell: return env < quiet && env2 < quiet;
            case Model::sample:  return env < quiet;
            case Model::ride:    return env < quiet && env2 < quiet;
            case Model::bell:    return env < quiet && env2 < quiet && click < quiet;
            case Model::tambourine: return burst >= (int) jingleBursts.size() && env < quiet && env2 < quiet;
            case Model::shaker:  return env < quiet;
            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:  return env < quiet;
        }
        return true;
    }

    void Voice::setTone (double tone, double rate) noexcept
    {
        lowPole = highPole = 0.0f;
        lowState = highState = {};
        tone = juce::jlimit (0.0, 1.0, tone);
        if (tone < 0.499)
            lowPole = (float) (1.0 - std::exp (-twoPi * 20000.0 * std::pow (40.0, -(0.5 - tone) * 2.0) / rate));
        else if (tone > 0.501)
            highPole = (float) (1.0 - std::exp (-twoPi * 20.0 * std::pow (100.0, (tone - 0.5) * 2.0) / rate));
    }

    void Voice::renderStreamed (float* left, float* right, int numSamples) noexcept
    {
        // DISK STREAMING (0.28.0): as SAMPLE's loop, each frame from the head in memory or, past it, the stream.
        const auto head = (juce::int64) sample->length();
        const auto length = sample->frames();
        const auto channels = juce::jmin (2, (int) sample->channels.size());
        const auto frameAt = [this, head, length] (int c, juce::int64 f) -> float
        {
            if (f < 0 || f >= length)
                return 0.0f;
            if (f < head)
                return sample->channels[(size_t) c][(size_t) f];
            return streamer->sample (streamId, streamOutput, c, f, waitForDisk);
        };
        for (int i = 0; i < numSamples; ++i)
        {
            if (waitLeft > 0)
            {
                --waitLeft;
                continue;
            }
            if (! advanceShape())
            {
                active = false;
                break;
            }
            const auto index = (juce::int64) position;
            if (position >= endPosition || index >= length - 1 || env < 1.0e-4f)
            {
                active = false;
                break;
            }
            const auto frac = (float) (position - (double) index);
            auto gain = env * level * fade * shapeGain;
            if (attackTime > 0.0f && t < attackTime)
                gain *= (float) t / attackTime;
            gain *= 1.0f + spike * click;
            click *= clickCoef;
            env *= envCoef;
            std::array<float, 2> out {};
            for (int c = 0; c < channels; ++c)
            {
                // At its own pitch every read falls on a sample (frac 0): one read, what the curve gives there anyway.
                auto y = frac <= 0.0f ? frameAt (c, index)
                                      : cubic (frameAt (c, index - 1), frameAt (c, index), frameAt (c, index + 1), frameAt (c, index + 2), frac);
                if (lowPole > 0.0f)
                    y = (lowState[(size_t) c] += lowPole * (y - lowState[(size_t) c]));
                if (highPole > 0.0f)
                    y -= (highState[(size_t) c] += highPole * (y - highState[(size_t) c]));
                out[(size_t) c] = y * gain;
            }
            left[i] += out[0] * panLeft;
            right[i] += (channels > 1 ? out[1] : out[0]) * panRight;
            position += step;
            t += 1.0 / sampleRate;
            if (gateLeft > 0 && --gateLeft == 0)
                fadeOut (0.003);
            if (fadeStep > 0.0f && (fade -= fadeStep) <= 0.0f)
            {
                active = false;
                break;
            }
        }
        // Where it's read up to: the stream may reuse what's behind it.
        streamer->consumed (streamId, streamOutput, juce::jmax (head, (juce::int64) position - 2));
    }

    void Voice::fadeOut (double seconds) noexcept
    {
        if (active)
            fadeStep = juce::jmax (fadeStep, (float) (1.0 / juce::jmax (1.0, seconds * sampleRate)));
    }

    void Voice::render (float* left, float* right, int numSamples) noexcept
    {
        if (model == Model::sample && streamId >= 0)
        {
            renderStreamed (left, right, numSamples);
            return;
        }
        if (model == Model::sample)
        {
            const auto length = sample->length();
            const auto channels = (int) sample->channels.size();
            for (int i = 0; i < numSamples; ++i)
            {
                if (waitLeft > 0)
                {
                    --waitLeft;
                    continue;
                }
                if (! advanceShape())
                {
                    active = false;
                    return;
                }
                const auto index = (int) position;
                if ((reversed ? position <= startPosition : position >= endPosition) || index >= length - 1 || env < 1.0e-4f)
                {
                    active = false;
                    return;
                }
                const auto frac = (float) (position - index);
                auto gain = env * level * fade * shapeGain;
                if (endFade > 0.0)
                    gain *= (float) juce::jmin (1.0, (reversed ? position - startPosition : endPosition - position) / endFade);
                if (attackTime > 0.0f && t < attackTime)
                    gain *= (float) t / attackTime;
                gain *= 1.0f + spike * click;
                click *= clickCoef;
                env *= envCoef;

                std::array<float, 2> out {};
                for (int c = 0; c < juce::jmin (2, channels); ++c)
                {
                    const auto* x = sample->channels[(size_t) c].data();
                    auto y = cubic (x[juce::jmax (0, index - 1)], x[index], x[index + 1], x[juce::jmin (length - 1, index + 2)], frac);
                    if (lowPole > 0.0f)
                        y = (lowState[(size_t) c] += lowPole * (y - lowState[(size_t) c]));
                    if (highPole > 0.0f)
                        y -= (highState[(size_t) c] += highPole * (y - highState[(size_t) c]));
                    out[(size_t) c] = y * gain;
                }
                left[i] += out[0] * panLeft;
                right[i] += (channels > 1 ? out[1] : out[0]) * panRight;
                position += reversed ? -step : step;
                t += 1.0 / sampleRate;
                if (gateLeft > 0 && --gateLeft == 0)
                    fadeOut (0.003);
                if (fadeStep > 0.0f && (fade -= fadeStep) <= 0.0f)
                {
                    active = false;
                    return;
                }
            }
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (waitLeft > 0)
            {
                --waitLeft;
                continue;
            }
            if (! advanceShape())
            {
                active = false;
                return;
            }
            const auto x = next() * fade * shapeGain;
            left[i] += x * panLeft;
            right[i] += x * panRight;
            if (gateLeft > 0 && --gateLeft == 0)
                fadeOut (0.003);
            if (fadeStep > 0.0f && (fade -= fadeStep) <= 0.0f)
            {
                active = false;
                return;
            }
            if ((i & 31) == 31)
            {
                if (finished())
                {
                    active = false;
                    return;
                }
                // Envelopes that have died away go to zero, not on into denormal numbers (very slow on most
                // processors, where nothing has turned them off: the page's preview, for one).
                for (auto* e : { &env, &env2, &pitchEnv, &click })
                    if (*e < 1.0e-12f)
                        *e = 0.0f;
            }
        }
        if (finished())
            active = false;
    }

    //==============================================================================
    void Kit::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = juce::jmax (1, maxBlockSize);
        scratch.assign ((size_t) (2 * numSlots * blockSize), 0.0f);
        channelScratch.assign ((size_t) (4 * mixer::numMicChannels * blockSize), 0.0f);
        busScratch.assign ((size_t) (2 * mixer::numBuses * blockSize), 0.0f);
        voiceScratch.assign ((size_t) (2 * blockSize), 0.0f);
        // SIMULATED MICS (0.26.0).
        static_assert (micsim::Simulator::numSlots == numSlots && micsim::numPieces == numPieces);
        micSim.prepare (sampleRate, blockSize);
        simScratch.assign ((size_t) (8 * blockSize), 0.0f);   // a mic's 4 lines, the room's 4
        for (int p = 0; p < numPieces; ++p)
            for (int c = 0; c < mixer::numChannels; ++c)
                for (int side = 0; side < 2; ++side)
                {
                    const auto slot = pieces[(size_t) p].articulations[0].slot;
                    simTaps[(size_t) p][(size_t) c][(size_t) side] = micsim::tapFor (p, c, side == 1, isDirect (slot, c) && ! mixer::hearsWholeKit (c), sampleRate);
                }
        reset();
    }

    void Kit::reset() noexcept
    {
        for (auto& v : voices)
            v.active = false;
        slotGain.fill (1.0f);
        lastHit.fill (-1.0);
        stripTail = {};
        micSim.reset();
        lowState = {};
        highState = {};
    }

    int Kit::getNumActiveVoices() const noexcept
    {
        int count = 0;
        for (const auto& v : voices)
            count += v.active ? 1 : 0;
        return count;
    }

    bool Kit::isSounding (int slot) const noexcept
    {
        for (const auto& v : voices)
            if (v.active && v.slot == slot)
                return true;
        return false;
    }

    float Kit::getHitAge (int slot) const noexcept
    {
        const auto age = lastHit[(size_t) juce::jlimit (0, numSlots - 1, slot)];
        return age >= 0.0 && age < 1.0 ? (float) age : -1.0f;
    }

    void Kit::trigger (int slot, float velocity, const KitSettings& settings) noexcept
    {
        if (slot >= 0 && slot < numSlots)
            startHit (slot, velocity, settings);
    }

    void Kit::pushMidiEvent (const MidiEvent& e) noexcept
    {
        if (monitorFifo.getFreeSpace() < 1)
            return;
        const auto scope = monitorFifo.write (1);
        if (scope.blockSize1 > 0)
            monitorEvents[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0)
            monitorEvents[(size_t) scope.startIndex2] = e;
    }

    int Kit::popMidiEvents (MidiEvent* into, int max) noexcept
    {
        const auto scope = monitorFifo.read (juce::jmin (max, monitorFifo.getNumReady()));
        int n = 0;
        for (int i = 0; i < scope.blockSize1; ++i)
            into[n++] = monitorEvents[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            into[n++] = monitorEvents[(size_t) (scope.startIndex2 + i)];
        return n;
    }

    void Kit::grab (int slot) noexcept
    {
        // A cymbal grabbed stops all of it: the ride's bow, bell and edge alike.
        const auto piece = pieceOfSlot (slot);
        for (auto& v : voices)
            if (v.active && v.fadeStep <= 0.0f && (v.slot == slot || (piece >= 0 && pieceOfSlot (v.slot) == piece)))
                v.fadeOut (0.08);
    }

    void Kit::startHit (int asked, float velocity, const KitSettings& settings, const HitOptions& options) noexcept
    {
        // ARTICULATIONS (0.17.0): an articulation switched off plays its stand-in.
        const auto slot = settings.playsAs (asked);
        if (slot < 0)
            return;
        const auto& slotSettings = settings.slots[(size_t) slot];
        // THE SAMPLE ENGINE (0.19.0): an instrument's hit for this velocity, played as the slot's sample (whole).
        SlotSettings chosenHit;
        const SlotSettings* use = &slotSettings;
        float hitGain = 1.0f;
        const std::vector<Instrument::Hit::Mic>* mics = nullptr;   // THE MIXER (0.20.0): a multi-mic hit's parts
        if (slotSettings.model == Model::sample && slotSettings.instrument != nullptr)
        {
            const auto choice = slotSettings.instrument->choose (juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)),
                                                                 slotSettings.randomize, slotSettings.adjacent, slotSettings.layerLimit,
                                                                 hitMemory[(size_t) slot]);
            if (choice.sample == nullptr)
                return;
            chosenHit = slotSettings;
            chosenHit.sample = choice.sample;
            chosenHit.start = 0.0f;
            chosenHit.end = 1.0f;
            chosenHit.reverseSource = nullptr;
            use = &chosenHit;
            hitGain = choice.gain;
            lastChoice[(size_t) slot] = choice.layer * 1000 + choice.hit + 1;
            if (choice.mics != nullptr && ! choice.mics->empty())
                mics = choice.mics;
        }
        const auto& s = *use;
        // 0.18.0: the articulation's VELOCITY GATE (on the velocity as it came) and VELOCITY CURVE.
        auto hitVelocity = juce::jlimit (0.0f, 1.0f, velocity);
        if (hitVelocity < s.velMin || hitVelocity > s.velMax)
            return;
        if (s.velocityCurve != nullptr)
            if (hitVelocity = s.velocityCurve->apply (hitVelocity); hitVelocity < 0.0f)
                return;
        lastHit[(size_t) slot] = 0.0;
        // A slot that's silent starts at its MUTE / SOLO level (it glides only while it sounds).
        if (! isSounding (slot))
            slotGain[(size_t) slot] = audible (settings, slot) ? 1.0f : 0.0f;
        const auto reversing = options.reverse != drumseq::Reverse::off && s.reverseSource != nullptr;
        if (s.model == Model::sample && ! reversing && (s.sample == nullptr || s.sample->length() < 2))
            return;

        // 0.33.0: a choke articulation (hit, then grabbed) stops what's ringing of its cymbal first.
        if (isChoke (slot))
        {
            const auto piece = pieceOfSlot (slot);
            for (auto& v : voices)
                if (v.active && v.fadeStep <= 0.0f && piece >= 0 && pieceOfSlot (v.slot) == piece)
                    v.fadeOut (0.03);
        }

        // CHOKE: this hit stops the other slots in its group, quickly but without a click.
        if (s.choke > 0)
            for (auto& v : voices)
                if (v.active && v.choke == s.choke && v.slot != slot)
                    v.fadeOut (0.005);

        // MONO (6c): this hit takes over from the slot's earlier ones, which fade quickly but without a click.
        if (s.mono)
            for (auto& v : voices)
                if (v.active && v.slot == slot)
                    v.fadeOut (0.002);

        // Up to VOICES hits of one slot overlap (a roll); past that, the oldest fades (all of a multi-mic hit's
        // voices: they share its age).
        Voice* oldestOfSlot = nullptr;
        int count = 0;
        for (auto& v : voices)
            if (v.active && v.slot == slot && v.fadeStep <= 0.0f && v.primary)
            {
                ++count;
                if (oldestOfSlot == nullptr || v.age < oldestOfSlot->age)
                    oldestOfSlot = &v;
            }
        if (count >= juce::jlimit (1, maxVoices, s.voices) && oldestOfSlot != nullptr)
        {
            const auto oldAge = oldestOfSlot->age;
            for (auto& v : voices)
                if (v.active && v.age == oldAge)
                    v.fadeOut (0.002);
        }

        // A free voice, or (rarely: every voice busy) the oldest one, cut.
        const auto freeVoice = [this]
        {
            Voice* chosen = nullptr;
            for (auto& v : voices)
                if (! v.active)
                    return &v;
            for (auto& v : voices)
                if (chosen == nullptr || v.age < chosen->age)
                    chosen = &v;
            return chosen;
        };

        // The sequencer gives its notes' level as velocity squared, past the kit's VELOCITY: kept, from the curved one.
        auto velocityGain = options.velocityGain >= 0.0f ? hitVelocity * hitVelocity
                                                         : 1.0f - settings.velocity + settings.velocity * hitVelocity * hitVelocity;
        // An instrument: its layers already sound soft or hard. VELOCITY TO VOLUME off: each hit as recorded; on, the
        // velocity's level too (so a soft note within a layer is a little quieter). A neighbour layer's hit matched.
        const auto plainVelocityGain = velocityGain;   // 0.29.0: the layer's
        if (s.instrument != nullptr)
            velocityGain = (s.velocityToVolume ? velocityGain : 1.0f) * hitGain;
        // Every hit busy (48 playing): the oldest is cut, all its voices (a hit's voices share its age).
        {
            int hits = 0;
            const Voice* oldest = nullptr;
            for (const auto& v : voices)
                if (v.active && v.primary)
                {
                    ++hits;
                    if (oldest == nullptr || v.age < oldest->age)
                        oldest = &v;
                }
            if (hits >= maxHits && oldest != nullptr)
            {
                const auto oldAge = oldest->age;
                for (auto& v : voices)
                    if (v.active && v.age == oldAge)
                        v.active = false;
            }
        }
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const auto age = ++ages;
        // ITS LAYER (0.29.0): the user's sample with the hit, as a voice of the hit (same age: choked, stolen and
        // faded with it; not counted as a hit of its own), whole, at its own pitch, the velocity's level x LAYER LEVEL,
        // into its mixer channel (late by that channel's TIME OFFSET) or the drum's own way.
        const auto startLayer = [&]
        {
            if (slotSettings.layer == nullptr || slotSettings.layerGain <= 0.0f)
                return;
            const auto choice = slotSettings.layer->choose (juce::jlimit (1, 127, juce::roundToInt (hitVelocity * 127.0f)), slotSettings.randomize,
                                                            slotSettings.adjacent, 0, layerMemory[(size_t) slot]);
            if (choice.sample == nullptr || choice.sample->length() < 2)
                return;
            auto part = slotSettings;
            part.model = Model::sample;
            part.sample = choice.sample;
            part.instrument = nullptr;
            part.start = 0.0f;
            part.end = 1.0f;
            part.reverseSource = nullptr;
            part.pitch = 0.0f;
            part.decay = 1.0f;
            part.tone = 0.5f;
            part.snap = 0.5f;
            part.envelope = nullptr;
            part.delay = 0.0f;
            const auto channel = slotSettings.layerChannel >= 0 && slotSettings.layerChannel < mixer::numMicChannels ? slotSettings.layerChannel : -1;
            if (channel >= 0)
                part.delay = (float) settings.channels[(size_t) channel].offsetSeconds;
            HitOptions plain;
            plain.pan = options.pan;
            plain.gateSamples = options.gateSamples;
            auto* voice = freeVoice();
            voice->start (part, slot, plainVelocityGain * choice.gain * slotSettings.layerGain, sampleRate, noiseSeed, plain);
            voice->age = age;
            voice->channel = channel;
            voice->primary = false;
            voice->micGain = 1.0f;
            voice->bleed = false;
            voice->releaseLeft = -1;
        };
        if (mics == nullptr)
        {
            auto* chosen = freeVoice();
            chosen->start (s, slot, velocityGain, sampleRate, noiseSeed, options);
            chosen->age = age;
            chosen->channel = -1;
            chosen->primary = true;
            chosen->releaseLeft = -1;
            startLayer();
            return;
        }
        // THE MIXER (0.20.0): a voice per mic, into its channel, at the bleed / close-mic level the mixer gives this
        // piece there, late by the channel's TIME OFFSET; the channel's RELEASE fades it after that long.
        const auto piece = juce::jlimit (0, mixer::numPieces - 1, pieceOfSlot (slot));
        auto first = true;
        // DISK STREAMING (0.28.0): a streamed hit claims one stream for all its mics (none free: its heads only).
        auto stream = -1;
        if (settings.streamer != nullptr)
            for (const auto& mic : *mics)
                if (mic.sample != nullptr && mic.sample->stream != nullptr)
                {
                    stream = settings.streamer->claim (*mic.sample->stream);
                    break;
                }
        for (const auto& mic : *mics)
        {
            // The mic's own channel (by its name), then ROUTE INSTRUMENT MICROPHONES' (0.23.0): another channel, a user
            // channel, or none. It stays direct or bleed, and takes its BLEED FROM INSTRUMENTS cell, as in its own
            // channel; the channel it plays in gives the rest (bleed level and on, offset, release).
            const auto source = mic.channel >= 0 ? mic.channel : homeChannel (slot);
            const auto channel = mixer::routedChannel (settings.micRoutes, pieceOfSlot (slot), source);
            if (channel < 0)
                continue;
            // 0.33.0: a mic of the drum's own that has a USER channel to itself (a sample pack's sub kick, trigger, tom 4)
            // is its direct sound, with no BLEED FROM INSTRUMENTS cell (that table has the 13 named channels).
            const auto own = source >= mixer::numChannels;
            const auto direct = own || isDirect (slot, source);
            const auto& ch = settings.channels[(size_t) channel];
            auto gain = settings.bleed != nullptr && ! own ? (*settings.bleed)[(size_t) source][(size_t) piece].gain() : 1.0f;
            if (! direct)
                gain *= ch.bleedOn ? ch.bleedGain : 0.0f;
            if (std::abs (gain) < 1.0e-9f || mic.sample == nullptr)
                continue;
            auto part = s;
            part.sample = mic.sample.get();
            part.delay = (float) (juce::jmax (0.0, (double) s.delay) + ch.offsetSeconds);
            auto* voice = freeVoice();
            voice->start (part, slot, velocityGain, sampleRate, noiseSeed, options);
            voice->age = age;
            voice->channel = channel;
            voice->primary = first;
            voice->micGain = gain;
            voice->bleed = ! direct;
            voice->releaseLeft = ch.releaseSeconds > 0.0 ? juce::jmax (1, (int) (ch.releaseSeconds * sampleRate)) + voice->waitLeft : -1;
            voice->enableStream (settings.streamer, stream, settings.waitForDisk);
            first = false;
        }
        startLayer();
    }

    double Kit::reverseSeconds (const SlotSettings& s, float notePitch, double) noexcept
    {
        if (s.reverseSource == nullptr || s.reverseSource->length() < 2)
            return 0.0;
        const auto share = s.reverseBaked ? 1.0 : juce::jmax (0.0, (double) s.end - (double) s.start);
        const auto ratio = std::exp2 ((s.reverseBaked ? notePitch : s.pitch + notePitch) / 12.0);
        return share * s.reverseSource->length() / (s.reverseSource->sampleRate * ratio);
    }

    void Kit::patternKey (int pattern, bool down, float velocity) noexcept
    {
        // The latest key held wins; letting it go returns to the one held before, or stops.
        auto* held = heldPatterns.data();
        numHeld = (int) (std::remove (held, held + numHeld, pattern) - held);
        if (down && numHeld < (int) heldPatterns.size())
        {
            held[numHeld++] = pattern;
            if (! keyRunning)
            {
                keyRunning = true;
                keyClock = 0.0;
                keyVelocity = velocity;
            }
        }
        if (numHeld == 0)
            keyRunning = false;
        else
            keyPattern = held[numHeld - 1];
    }

    void Kit::runSequencer (const Outputs& outputs, int from, int to, const KitSettings& settings, const drumseq::Settings& sequencer,
                            double ppq, double bpm) noexcept
    {
        const auto renderRange = [&] (int a, int b)
        {
            while (a < b)
            {
                const auto n = juce::jmin (blockSize, b - a);
                render (outputs, a, n, settings);
                renderChannels (outputs, a, n, settings);   // THE MIXER's mic channels (0.20.0)
                a += n;
            }
        };
        const auto active = settings.on && sequencer.on && sequencer.patterns != nullptr;
        const auto stepsPerSecond = bpm / 60.0 / sequencer.beatsPerStep();
        const auto stepsPerSample = stepsPerSecond / sampleRate;
        const auto song = sequencer.trigger == drumseq::Trigger::song;
        const auto running = active && (song ? ppq >= 0.0 : keyRunning);
        if (! running)
        {
            renderRange (from, to);
            return;
        }
        const auto startSteps = song ? (ppq + from * bpm / 60.0 / sampleRate) / sequencer.beatsPerStep() : keyClock;
        const auto pattern = juce::jlimit (0, drumseq::numPatterns - 1, song ? sequencer.pattern : keyPattern);

        // The hits due in [from, to), in time order.
        auto count = 0;
        drumseq::forEachHit ((*sequencer.patterns)[(size_t) pattern], sequencer, startSteps, startSteps + (to - from) * stepsPerSample,
                             [&] (const drumseq::Note& n) { return reverseSeconds (settings.slots[(size_t) n.row], n.pitch, sampleRate) * stepsPerSecond; },
                             [&] (const drumseq::Note& n) { return settings.slots[(size_t) n.row].anchorSeconds() * stepsPerSecond; },
                             [&] (double t, const drumseq::Note& n, int)
                             {
                                 if (count < (int) sequenced.size())
                                     sequenced[(size_t) count++] = { juce::jlimit (from, to - 1, from + juce::roundToInt ((t - startSteps) / stepsPerSample)), n };
                             });
        std::sort (sequenced.begin(), sequenced.begin() + count, [] (const auto& a, const auto& b) { return a.offset < b.offset; });

        const auto keyScale = song ? 1.0f : 1.0f - sequencer.velocityAmount + sequencer.velocityAmount * keyVelocity;
        auto at = from;
        for (int i = 0; i < count; ++i)
        {
            const auto& hit = sequenced[(size_t) i];
            renderRange (at, hit.offset);
            at = hit.offset;
            HitOptions options;
            options.pan = hit.note.pan;
            options.pitch = hit.note.pitch;
            options.reverse = hit.note.reverse;
            options.gateSamples = hit.note.gate < 1.0f ? juce::jmax (1, juce::roundToInt (hit.note.gate * 60.0 / bpm * sampleRate)) : -1;
            const auto v = juce::jlimit (0.0f, 1.0f, hit.note.velocity * keyScale);
            options.velocityGain = v * v;
            startHit (hit.note.row, v, settings, options);
        }
        renderRange (at, to);
        if (! song)
            keyClock += (to - from) * stepsPerSample;
    }

    void Kit::process (const Outputs& outputs, int numSamples, const juce::MidiBuffer& midi, int midiStart, const KitSettings& settings,
                       const drumseq::Settings& sequencer, double ppq, double bpm, const SlotHit* hits, int numHits) noexcept
    {
        if (blockSize <= 0)
            return;   // not prepared yet
        const auto sequencing = settings.on && sequencer.on && sequencer.patterns != nullptr;
        const auto keyed = sequencing && sequencer.trigger == drumseq::Trigger::key;
        if (! keyed)
        {
            keyRunning = false;
            numHeld = 0;
        }
        int done = 0;
        const auto renderTo = [&] (int until)
        {
            if (until > done)
                runSequencer (outputs, done, until, settings, sequencer, ppq, bpm);
            done = juce::jmax (done, until);
        };
        // The given hits up to `until` (a note there comes after them).
        auto nextHit = 0;
        const auto playHitsBefore = [&] (int until)
        {
            for (; nextHit < numHits && hits[nextHit].offset < until; ++nextHit)
            {
                const auto& h = hits[nextHit];
                if (h.slot < 0 || h.slot >= numSlots || h.offset < 0)
                    continue;
                renderTo (h.offset);
                startHit (h.slot, juce::jlimit (0.0f, 1.0f, h.velocity), settings);
            }
        };
        if (settings.on)
            for (const auto metadata : midi)
            {
                const auto position = metadata.samplePosition - midiStart;
                if (position < 0)
                    continue;
                if (position >= numSamples)
                    break;
                playHitsBefore (position);
                const auto message = metadata.getMessage();
                // MIDI IN CHANNEL (0.18.0): the others ignored.
                if (settings.channel > 0 && message.getChannel() != settings.channel)
                    continue;
                MidiEvent event;
                event.channel = (std::uint8_t) juce::jlimit (1, 16, message.getChannel());
                // Controllers: each one's last value kept (CC ZONES); CC 4 is THE HI-HAT PEDAL (127 closed).
                if (message.isController())
                {
                    controllers[(size_t) message.getControllerNumber()] = (std::uint8_t) message.getControllerValue();
                    if (message.getControllerNumber() == 4)
                        pedal = message.getControllerValue();
                    event.type = MidiEvent::Type::controller;
                    event.number = (std::uint8_t) message.getControllerNumber();
                    event.value = (std::uint8_t) message.getControllerValue();
                    pushMidiEvent (event);
                    continue;
                }
                // MUTE TAIL: a cymbal grabbed by a note let go or by aftertouch, as its slot asks.
                if (message.isNoteOff() || message.isAftertouch())
                {
                    const auto played = settings.playsAs (settings.slotFor (message.getNoteNumber(), pedal.load()));
                    event.type = message.isNoteOff() ? MidiEvent::Type::noteOff : MidiEvent::Type::aftertouch;
                    event.number = (std::uint8_t) message.getNoteNumber();
                    event.value = (std::uint8_t) (message.isNoteOff() ? message.getVelocity() : message.getAfterTouchValue());
                    event.slot = (std::int8_t) played;
                    pushMidiEvent (event);
                    const auto wanted = message.isNoteOff() ? MuteTail::noteOff : MuteTail::aftertouch;
                    if (played >= 0 && settings.slots[(size_t) played].muteTail == wanted
                        && (message.isNoteOff() || message.getAfterTouchValue() > 0))
                    {
                        renderTo (position);
                        grab (played);
                    }
                }
                if (! message.isNoteOnOrOff())
                    continue;
                if (const auto pattern = keyed && ! settings.mapped ? patternForKey (message.getNoteNumber(), settings.rootKey) : -1; pattern >= 0)
                {
                    renderTo (position);
                    patternKey (pattern, message.isNoteOn(), message.getFloatVelocity());
                    continue;
                }
                if (! message.isNoteOn())
                    continue;
                // CC ZONES: a positional pad's note plays the articulation for where it was hit.
                auto slot = settings.slotFor (message.getNoteNumber(), pedal.load());
                if (const auto* z = settings.zonesFor (message.getNoteNumber()))
                    if (const auto zoneSlot = z->slots[(size_t) z->zoneOf (controllers[(size_t) juce::jlimit (0, 127, (int) z->cc)])]; zoneSlot >= 0)
                        slot = zoneSlot;
                // THE INCOMING VELOCITY CURVE (gated: no hit).
                auto velocity = message.getFloatVelocity();
                if (settings.inputCurve != nullptr)
                    velocity = settings.inputCurve->apply (velocity);
                event.type = MidiEvent::Type::noteOn;
                event.number = (std::uint8_t) message.getNoteNumber();
                event.value = message.getVelocity();
                event.slot = (std::int8_t) (velocity >= 0.0f ? settings.playsAs (slot) : -1);
                event.valueOut = (std::int16_t) (velocity >= 0.0f ? juce::roundToInt (velocity * 127.0f) : -1);
                pushMidiEvent (event);
                if (slot < 0 || velocity < 0.0f)
                    continue;
                renderTo (position);
                startHit (slot, velocity, settings);
            }
        if (settings.on)
            playHitsBefore (numSamples);
        renderTo (numSamples);

        // Where the beat is now, for the page's playhead.
        auto position = -1.0;
        if (sequencing)
        {
            const auto length = sequencer.lengthSteps();
            if (sequencer.trigger == drumseq::Trigger::song && ppq >= 0.0)
                position = std::fmod ((ppq + numSamples * bpm / 60.0 / sampleRate) / sequencer.beatsPerStep(), length);
            else if (keyed && keyRunning)
                position = std::fmod (keyClock, length);
        }
        sequencerPosition = position;
        sequencerPattern = sequencer.trigger == drumseq::Trigger::key ? keyPattern : sequencer.pattern;

        // DISK STREAMING (0.28.0): the streams no voice plays any more go back to the pool.
        if (settings.streamer != nullptr)
        {
            std::array<bool, Streamer::maxStreams> inUse {};
            for (const auto& v : voices)
                if (v.active && v.streamId >= 0 && v.streamId < Streamer::maxStreams)
                    inUse[(size_t) v.streamId] = true;
            settings.streamer->release (inUse);
        }
    }

    bool Kit::audible (const KitSettings& settings, int slot) noexcept
    {
        auto anySolo = false;
        for (const auto& s : settings.slots)
            anySolo = anySolo || s.solo;
        const auto& s = settings.slots[(size_t) slot];
        return ! s.mute && (! anySolo || s.solo);
    }

    void Kit::render (const Outputs& outputs, int offset, int numSamples, const KitSettings& settings) noexcept
    {
        const auto seconds = numSamples / sampleRate;
        for (auto& age : lastHit)
            if (age >= 0.0)
                age += seconds;

        // Each sounding slot's voices, drawn into its scratch in one pass over the voices (not one per slot).
        std::array<bool, numSlots> used {};
        for (auto& v : voices)
        {
            if (! v.active || v.channel >= 0)
                continue;
            auto* left = scratch.data() + (size_t) (2 * v.slot * blockSize);
            if (! used[(size_t) v.slot])
            {
                used[(size_t) v.slot] = true;
                std::fill (left, left + numSamples, 0.0f);
                std::fill (left + blockSize, left + blockSize + numSamples, 0.0f);
            }
            v.render (left, left + blockSize, numSamples);
        }

        const auto glide = (float) (1.0 / (0.005 * sampleRate));   // MUTE / SOLO: 5 ms

        for (int slot = 0; slot < numSlots; ++slot)
        {
            const auto& s = settings.slots[(size_t) slot];
            const auto target = audible (settings, slot) ? 1.0f : 0.0f;
            if (! used[(size_t) slot])
            {
                slotGain[(size_t) slot] = target;
                lowState[(size_t) slot] = {};    // LP / HP start afresh with the next hit
                highState[(size_t) slot] = {};
                continue;
            }

            auto* left = scratch.data() + (size_t) (2 * slot * blockSize);
            auto* right = left + blockSize;
            filterSlot (s, left, right, numSamples, lowState[(size_t) slot], highState[(size_t) slot], sampleRate);

            // SIMULATED MICS (0.26.0): the drum's sound (mono, at its level) into the mics; the mixer takes it on.
            if (settings.micSim.on && s.miked)
            {
                const auto gain = settings.gain * s.gain * s.trim * 0.5f;
                auto* in = micSim.input (slot);
                auto& current = slotGain[(size_t) slot];
                for (int i = 0; i < numSamples; ++i)
                {
                    if (current < target)
                        current = juce::jmin (target, current + glide);
                    else if (current > target)
                        current = juce::jmax (target, current - glide);
                    in[i] = (left[i] + right[i]) * gain * current;
                }
                micSim.fed (slot, numSamples);
                continue;
            }

            // Into the slot's bus, or its output (the mix, or another the DAW has on), and (while each effect is on)
            // the sends.
            const auto gain = settings.gain * s.gain * s.trim;
            const auto panLeft = s.pan > 0.0f ? 1.0f - s.pan : 1.0f;
            const auto panRight = s.pan < 0.0f ? 1.0f + s.pan : 1.0f;
            const auto bus = s.insertBus - 1;
            const auto toBus = bus >= 0 && bus < (int) outputs.busLeft.size() && outputs.busLeft[(size_t) bus] != nullptr;
            const auto aux = s.output - 1;
            const auto toAux = ! toBus && aux >= 0 && aux < (int) outputs.auxLeft.size() && outputs.auxLeft[(size_t) aux] != nullptr;
            auto* outLeft = (toBus ? outputs.busLeft[(size_t) bus] : toAux ? outputs.auxLeft[(size_t) aux] : outputs.left) + offset;
            auto* outRight = (toBus ? outputs.busRight[(size_t) bus] : toAux ? outputs.auxRight[(size_t) aux] : outputs.right) + offset;
            std::array<float*, 2> sendLeft {}, sendRight {};
            for (size_t k = 0; k < sendLeft.size(); ++k)
                if (! toBus && settings.sendOn[k] && outputs.sendLeft[k] != nullptr && s.send[k] > 0.0f)
                {
                    sendLeft[k] = outputs.sendLeft[k] + offset;
                    sendRight[k] = outputs.sendRight[k] + offset;
                }

            auto& current = slotGain[(size_t) slot];
            for (int i = 0; i < numSamples; ++i)
            {
                if (current < target)
                    current = juce::jmin (target, current + glide);
                else if (current > target)
                    current = juce::jmax (target, current - glide);
                const auto g = current * gain;
                const auto l = left[i] * g * panLeft, r = right[i] * g * panRight;
                outLeft[i] += l;
                outRight[i] += r;
                for (size_t k = 0; k < sendLeft.size(); ++k)
                    if (sendLeft[k] != nullptr)
                    {
                        sendLeft[k][i] += l * s.send[k];
                        sendRight[k][i] += r * s.send[k];
                    }
            }
        }
    }

    void Kit::simulateMics (const KitSettings& settings, int numSamples, std::array<bool, mixer::numMicChannels>& used) noexcept
    {
        // SIMULATED MICS (0.26.0): each sounding drum's line tapped into each mic it's heard in, at the delay and level
        // its distance gives, as direct sound (its own close mics, the overheads, room and ambience) or bleed; each
        // mic's sound coloured as that mic is, into the channel it's routed to (ROUTE INSTRUMENT MICROPHONES), at the
        // BLEED FROM INSTRUMENTS cell and that channel's BLEED and TIME OFFSET, as a multi-mic sampled drum would be.
        micSim.begin (numSamples);
        const auto bs = (size_t) blockSize;
        std::array<int, numSlots> pieceOf {};
        auto any = false;
        for (int slot = 0; slot < numSlots; ++slot)
        {
            pieceOf[(size_t) slot] = micSim.sounding (slot) ? pieceOfSlot (slot) : -1;
            any = any || pieceOf[(size_t) slot] >= 0;
        }
        // The room hears every drum along the room mics' paths; its sound goes to the room and ambience channels.
        constexpr float roomSend = 0.5f;
        if (any)
            for (int slot = 0; slot < numSlots; ++slot)
                if (const auto p = pieceOf[(size_t) slot]; p >= 0)
                    for (int side = 0; side < 2; ++side)
                    {
                        const auto& t = simTaps[(size_t) p][mixer::room][(size_t) side];
                        micSim.tap (slot, t.delay, t.gain * roomSend, micSim.roomInput (side == 1), numSamples);
                    }
        auto* roomOut = simScratch.data() + 4 * bs;
        for (size_t line = 0; line < 4; ++line)   // (this piece's samples only: 0.40.0)
            std::fill (roomOut + line * bs, roomOut + line * bs + (size_t) numSamples, 0.0f);
        micSim.runRoom (settings.micSim, numSamples, roomOut, roomOut + bs, roomOut + 2 * bs, roomOut + 3 * bs);
        const auto reverb = micSim.roomRinging();

        // The level all of it is played at: a hit through every mic with every fader at 0 dB about as loud as it was
        // without them (our choice, measured on the modelled snare: see the tests).
        constexpr float simLevel = 0.8f;
        for (int source = 0; source < mixer::numChannels; ++source)
        {
            const auto roomPart = reverb && (source == mixer::room || source == mixer::ambience);
            // The channels this mic's sound goes to (one, unless routes split it by drum).
            std::array<int, 4> destinations {};
            int numDestinations = 0;
            const auto addDestination = [&] (int d)
            {
                for (int k = 0; k < numDestinations; ++k)
                    if (destinations[(size_t) k] == d)
                        return;
                if (numDestinations < (int) destinations.size())
                    destinations[(size_t) numDestinations++] = d;
            };
            for (int slot = 0; slot < numSlots; ++slot)
                if (const auto p = pieceOf[(size_t) slot]; p >= 0 && micsim::hears (p, source))
                    if (const auto d = mixer::routedChannel (settings.micRoutes, p, source); d >= 0)
                        addDestination (d);
            if (roomPart)
                addDestination (source);   // the room's sound stays in its own channel
            const auto stereo = micsim::micFor (source).stereo;
            for (int k = 0; k < numDestinations; ++k)
            {
                const auto destination = destinations[(size_t) k];
                const auto& ch = settings.channels[(size_t) destination];
                const auto offset = (int) std::lround (ch.offsetSeconds * sampleRate);
                auto* lines = simScratch.data();
                for (size_t line = 0; line < 4; ++line)
                    std::fill (lines + line * bs, lines + line * bs + (size_t) numSamples, 0.0f);
                auto hasDirect = false, hasBleed = false;
                for (int slot = 0; slot < numSlots; ++slot)
                {
                    const auto p = pieceOf[(size_t) slot];
                    if (p < 0 || ! micsim::hears (p, source) || mixer::routedChannel (settings.micRoutes, p, source) != destination)
                        continue;
                    const auto direct = isDirect (slot, source);
                    auto gain = settings.bleed != nullptr ? (*settings.bleed)[(size_t) source][(size_t) p].gain() : 1.0f;
                    if (! direct)
                        gain *= ch.bleedOn ? ch.bleedGain : 0.0f;
                    if (std::abs (gain) < 1.0e-9f)
                        continue;
                    gain *= simLevel;
                    auto* line = lines + (direct ? 0 : 2 * bs);
                    const auto& tl = simTaps[(size_t) p][(size_t) source][0];
                    micSim.tap (slot, tl.delay + offset, gain * tl.gain, line, numSamples);
                    if (stereo)
                    {
                        const auto& tr = simTaps[(size_t) p][(size_t) source][1];
                        micSim.tap (slot, tr.delay + offset, gain * tr.gain, line + bs, numSamples);
                    }
                    (direct ? hasDirect : hasBleed) = true;
                }
                if (roomPart && destination == source)
                {
                    const auto* from = roomOut + (source == mixer::ambience ? 2 * bs : 0);
                    for (size_t i = 0; i < (size_t) numSamples; ++i)
                    {
                        lines[i] += from[i] * simLevel;
                        lines[bs + i] += from[bs + i] * simLevel;
                    }
                    hasDirect = true;
                }
                if (! hasDirect && ! hasBleed)
                    continue;
                // The mic's colour (a mono mic: one side, copied), then into the channel.
                if (! used[(size_t) destination])
                    clearChannelScratch (destination, numSamples);
                auto* into = channelScratch.data() + 4 * (size_t) destination * bs;
                for (int which = 0; which < 4; ++which)
                {
                    if ((which < 2 && ! hasDirect) || (which >= 2 && ! hasBleed))
                        continue;
                    auto* line = lines + (size_t) which * bs;
                    if ((which & 1) == 1 && ! stereo)
                        std::copy (line - bs, line - bs + numSamples, line);
                    else
                        micSim.colour (source, destination, line, which, numSamples);
                    auto* to = into + (size_t) which * bs;
                    for (size_t i = 0; i < (size_t) numSamples; ++i)
                        to[i] += line[i];
                }
                used[(size_t) destination] = true;
            }
        }
    }

    void Kit::clearChannelScratch (int channel, int numSamples) noexcept
    {
        auto* lines = channelScratch.data() + 4 * (size_t) channel * (size_t) blockSize;
        for (size_t line = 0; line < 4; ++line)
            std::fill (lines + line * (size_t) blockSize, lines + line * (size_t) blockSize + (size_t) numSamples, 0.0f);
    }

    void Kit::renderChannels (const Outputs& outputs, int offset, int numSamples, const KitSettings& settings) noexcept
    {
        // THE MIXER (0.20.0): each mic voice into its channel at its drum's VOLUME, GAIN, MUTE / SOLO and its bleed /
        // close-mic level, the drums it's meant for (direct) and the rest (bleed) apart; then each channel's polarity,
        // fader, pan, MUTE / SOLO into its output or (0.21.0) its bus, and its SENDS; then the buses, in order.
        auto anyVoice = false, anyTail = false;
        for (const auto& v : voices)
            anyVoice = anyVoice || (v.active && v.channel >= 0);
        for (const auto t : stripTail)
            anyTail = anyTail || t > 0;
        const auto simBusy = micSim.busy();
        if (! anyVoice && ! anyTail && ! simBusy)
            return;
        auto* const host = settings.effects;
        const auto hasEffects = [host] (int strip) { return host != nullptr && host->hasEffects (strip); };
        // A strip with effects runs on for 3 s after its sound stops (a chorus's or a compressor's tail).
        const auto tailSamples = (int) (3.0 * sampleRate) + numSamples;
        const auto bs = (size_t) blockSize;
        // 0.40.0: a channel's scratch cleared (its four lines, this piece only) the first time it's used here, not every
        // channel's whole block every time (a tenth of a modelled kit's audio work was that clearing).
        std::array<bool, mixer::numMicChannels> used {};
        auto* vl = voiceScratch.data();
        auto* vr = vl + blockSize;
        for (auto& v : voices)
        {
            if (! v.active || v.channel < 0)
                continue;
            // RELEASE: the channel's fade once its time is up.
            if (v.releaseLeft >= 0)
            {
                v.releaseLeft -= numSamples;
                if (v.releaseLeft < 0 && v.fadeStep <= 0.0f)
                    v.fadeOut (0.03);
            }
            std::fill (vl, vl + numSamples, 0.0f);
            std::fill (vr, vr + numSamples, 0.0f);
            v.render (vl, vr, numSamples);
            const auto& s = settings.slots[(size_t) v.slot];
            const auto g = v.micGain * settings.gain * s.gain * s.trim * (audible (settings, v.slot) ? 1.0f : 0.0f);
            if (! used[(size_t) v.channel])
                clearChannelScratch (v.channel, numSamples);
            auto* cl = channelScratch.data() + 4 * (size_t) v.channel * bs + (v.bleed ? 2 * bs : 0);
            auto* cr = cl + blockSize;
            for (int i = 0; i < numSamples; ++i)
            {
                cl[i] += vl[i] * g;
                cr[i] += vr[i] * g;
            }
            used[(size_t) v.channel] = true;
        }
        if (simBusy)
        {
            if (settings.micSim.on)
                simulateMics (settings, numSamples, used);
            micSim.advance (numSamples);
        }
        for (int c = 0; c < mixer::numMicChannels; ++c)
        {
            const auto strip = (size_t) mixer::stripOfChannel (c);
            if (used[(size_t) c])
                stripTail[strip] = tailSamples;
            else if (stripTail[strip] > 0 && hasEffects ((int) strip))
            {
                clearChannelScratch (c, numSamples);
                used[(size_t) c] = true;   // silence in, its effects' tails out
            }
        }

        // Where a strip's sound goes. A bus's scratch is cleared when it's first used here; an FX BUS the processor
        // hasn't readied is left out (a send) or falls back to the output (a route); an output the DAW has off: MAIN.
        std::array<bool, mixer::numBuses> busUsed {};
        const auto busBuffer = [&] (int bus)
        {
            auto* l = busScratch.data() + 2 * (size_t) bus * bs;
            if (! busUsed[(size_t) bus])
            {
                busUsed[(size_t) bus] = true;
                std::fill (l, l + 2 * bs, 0.0f);
            }
            return l;
        };
        struct Stereo { float* left = nullptr; float* right = nullptr; };
        const auto outputFor = [&] (int output) -> Stereo
        {
            const auto aux = output - 1;
            if (aux >= 0 && aux < (int) outputs.auxLeft.size() && outputs.auxLeft[(size_t) aux] != nullptr)
                return { outputs.auxLeft[(size_t) aux] + offset, outputs.auxRight[(size_t) aux] + offset };
            return { outputs.left + offset, outputs.right + offset };
        };
        const auto fxBus = [&] (int target) -> Stereo
        {
            if (const auto f = mixer::fxBusOfTarget (target); f >= 0 && f < (int) outputs.busLeft.size() && outputs.busLeft[(size_t) f] != nullptr)
                return { outputs.busLeft[(size_t) f] + offset, outputs.busRight[(size_t) f] + offset };
            return {};
        };
        const auto destination = [&] (int from, int route, int output) -> Stereo
        {
            if (const auto b = mixer::busOfTarget (route); b >= 0 && mixer::canFeedBus (from, b))
            {
                auto* l = busBuffer (b);
                return { l, l + blockSize };
            }
            if (const auto fx = fxBus (route); fx.left != nullptr)
                return fx;
            return outputFor (output);
        };
        const auto sendTarget = [&] (int from, int target) -> Stereo
        {
            if (const auto b = mixer::busOfTarget (target); b >= 0)
            {
                if (! mixer::canFeedBus (from, b))
                    return {};
                auto* l = busBuffer (b);
                return { l, l + blockSize };
            }
            if (target == mixer::delayTarget || target == mixer::reverbTarget)
            {
                const auto k = (size_t) (target == mixer::delayTarget ? 0 : 1);
                if (k < outputs.sendLeft.size() && settings.sendOn[k] && outputs.sendLeft[k] != nullptr)
                    return { outputs.sendLeft[k] + offset, outputs.sendRight[k] + offset };
                return {};
            }
            return fxBus (target);
        };
        // Adds a stereo source in at a gain and a pan (a balance), and returns its peak.
        const auto addIn = [numSamples] (const float* sl, const float* sr, Stereo to, float gain, float pan)
        {
            const auto gl = gain * (pan > 0.0f ? 1.0f - pan : 1.0f), gr = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
            auto peak = 0.0f;
            if (gl == 0.0f && gr == 0.0f)
                return peak;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto l = sl[i] * gl, r = sr[i] * gr;
                to.left[i] += l;
                to.right[i] += r;
                peak = juce::jmax (peak, std::abs (l), std::abs (r));
            }
            return peak;
        };
        const auto sendsOf = [&settings] (int strip) -> const std::array<mixer::Send, mixer::numSendSlots>*
        {
            return settings.sends != nullptr ? &(*settings.sends)[(size_t) strip] : nullptr;
        };
        // A muted (or soloed-out) strip's sends: silent, except pre-fader ones under POST SEND (mute only).
        const auto sendSilenced = [&settings] (const mixer::Send& send, bool muted, bool soloedOut)
        {
            return soloedOut || (muted && (settings.muteMode == mixer::MuteMode::preSend || send.mode == mixer::SendMode::postFader));
        };

        // SOLO: among the channels, and among the buses, apart (a soloed channel's sends still reach its buses).
        auto anySolo = false, anyBusSolo = false;
        for (const auto& c : settings.channels)
            anySolo = anySolo || c.solo;
        for (const auto& b : settings.buses)
            anyBusSolo = anyBusSolo || b.solo;

        for (int channel = 0; channel < mixer::numMicChannels; ++channel)
        {
            if (! used[(size_t) channel])
                continue;
            // Its strip (USER 1-8's come after the buses): sends, effects, routing; its settings and scratch by channel.
            const auto c = mixer::stripOfChannel (channel);
            const auto& ch = settings.channels[(size_t) channel];
            auto* dl = channelScratch.data() + 4 * (size_t) channel * bs;
            auto* dr = dl + bs;
            const auto* bl = dl + 2 * bs;
            const auto* br = dl + 3 * bs;
            const auto soloedOut = anySolo && ! ch.solo;
            const auto polarity = ch.invert ? -1.0f : 1.0f;
            const auto* sends = sendsOf (c);
            // A send's direct and bleed sound apart, as they are now.
            const auto sendApart = [&] (const mixer::Send& send, float fader)
            {
                if (const auto target = sendTarget (c, send.target); target.left != nullptr)
                {
                    addIn (dl, dr, target, polarity * fader * mixer::Send::gainOf (send.directOn, send.directDb), send.directPan);
                    addIn (bl, br, target, polarity * fader * mixer::Send::gainOf (send.bleedOn, send.bleedDb), send.bleedPan);
                }
            };
            // PRE EFFECTS sends: before anything.
            if (sends != nullptr)
                for (const auto& send : *sends)
                    if (send.isOn() && send.mode == mixer::SendMode::preEffects && ! sendSilenced (send, ch.mute, soloedOut))
                        sendApart (send, 1.0f);
            // THE EFFECTS (0.22.0): direct and bleed together through them.
            const auto fx = hasEffects (c);
            if (fx)
            {
                for (size_t i = 0; i < (size_t) numSamples; ++i)
                {
                    dl[i] += bl[i];
                    dr[i] += br[i];
                }
                host->process (c, dl, dr, numSamples);
            }
            const auto g = ! ch.mute && ! soloedOut ? ch.gain * polarity : 0.0f;
            const auto to = destination (c, ch.route, ch.output);
            const auto peak = fx ? addIn (dl, dr, to, g, ch.pan) : juce::jmax (addIn (dl, dr, to, g, ch.pan), addIn (bl, br, to, g, ch.pan));
            auto& meter = channelPeaks[(size_t) channel];   // by channel (0.26.0: was by strip, past the end for USER 1-8)
            if (peak > meter.load())
                meter = peak;
            if (sends != nullptr)
                for (const auto& send : *sends)
                {
                    if (! send.isOn() || send.mode == mixer::SendMode::preEffects || sendSilenced (send, ch.mute, soloedOut))
                        continue;
                    const auto fader = send.mode == mixer::SendMode::postFader ? ch.gain : 1.0f;
                    if (! fx)
                        sendApart (send, fader);
                    else if (const auto target = sendTarget (c, send.target); target.left != nullptr)
                        addIn (dl, dr, target, polarity * fader * mixer::Send::gainOf (send.directOn, send.directDb), send.directPan);
                }
        }

        // A bus with effects runs on for their tails too.
        for (int b = 0; b < mixer::numBuses; ++b)
        {
            auto& tail = stripTail[(size_t) (mixer::numChannels + b)];
            if (busUsed[(size_t) b])
                tail = tailSamples;
            else if (tail > 0 && hasEffects (mixer::numChannels + b))
                busBuffer (b);
        }

        for (int b = 0; b < mixer::numBuses; ++b)
        {
            if (! busUsed[(size_t) b])
                continue;
            const auto& bus = settings.buses[(size_t) b];
            const auto from = mixer::numChannels + b;
            auto* sl = busScratch.data() + 2 * (size_t) b * bs;
            auto* sr = sl + bs;
            const auto soloedOut = anyBusSolo && ! bus.solo;
            const auto polarity = bus.invert ? -1.0f : 1.0f;
            if (const auto* sends = sendsOf (from))
                for (const auto& send : *sends)
                    if (send.isOn() && send.mode == mixer::SendMode::preEffects && ! sendSilenced (send, bus.mute, soloedOut))
                        if (const auto target = sendTarget (from, send.target); target.left != nullptr)
                            addIn (sl, sr, target, polarity * mixer::Send::gainOf (send.directOn, send.directDb), send.directPan);
            if (hasEffects (from))
                host->process (from, sl, sr, numSamples);
            const auto g = ! bus.mute && ! soloedOut ? bus.gain * polarity : 0.0f;
            const auto peak = addIn (sl, sr, destination (from, bus.route, bus.output), g, bus.pan);
            auto& meter = busPeaks[(size_t) b];
            if (peak > meter.load())
                meter = peak;
            if (const auto* sends = sendsOf (from))
                for (const auto& send : *sends)
                {
                    if (! send.isOn() || send.mode == mixer::SendMode::preEffects || sendSilenced (send, bus.mute, soloedOut))
                        continue;
                    if (const auto target = sendTarget (from, send.target); target.left != nullptr)
                        addIn (sl, sr, target, polarity * (send.mode == mixer::SendMode::postFader ? bus.gain : 1.0f)
                                                   * mixer::Send::gainOf (send.directOn, send.directDb), send.directPan);
                }
        }
        for (auto& t : stripTail)
            t = juce::jmax (0, t - numSamples);
    }

    void Kit::filterSlot (const SlotSettings& s, float* left, float* right, int numSamples, std::array<float, 2>& low,
                          std::array<float, 2>& high, double sampleRate) noexcept
    {
        // LP / HP (6c): one pole each, 6 dB an octave, on the slot's hits together. Zero-delay ("topology-preserving")
        // one-poles: exactly -3 dB at the corner wherever it's set (the plain y += a (x - y) form's high-pass is 6 dB
        // down at a 5 kHz corner).
        if (! s.filtered())
        {
            low = {};
            high = {};
            return;
        }
        const auto gain = [sampleRate] (float hz)
        {
            const auto g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (5.0, 0.45 * sampleRate, (double) hz) / sampleRate);
            return (float) (g / (1.0 + g));
        };
        const auto lowOn = s.lowPass < 19999.0f, highOn = s.highPass > 20.01f;
        const auto lowG = lowOn ? gain (s.lowPass) : 0.0f;
        const auto highG = highOn ? gain (s.highPass) : 0.0f;
        float* sides[] = { left, right };
        for (size_t c = 0; c < 2; ++c)
        {
            auto* x = sides[c];
            auto& l = low[c];
            auto& h = high[c];
            for (int i = 0; i < numSamples; ++i)
            {
                auto y = x[i];
                if (lowOn)
                {
                    const auto v = (y - l) * lowG;
                    y = v + l;
                    l = y + v;
                }
                if (highOn)
                {
                    const auto v = (y - h) * highG;
                    const auto lp = v + h;
                    h = lp + v;
                    y -= lp;
                }
                x[i] = y;
            }
            if (std::abs (l) < 1.0e-15f)
                l = 0.0f;
            if (std::abs (h) < 1.0e-15f)
                h = 0.0f;
        }
    }

    juce::AudioBuffer<float> Kit::renderHit (const SlotSettings& settings, float velocity, double sampleRate, double seconds)
    {
        juce::AudioBuffer<float> buffer (2, juce::jmax (1, (int) (seconds * sampleRate)));
        buffer.clear();
        auto s = settings;
        s.delay = 0.0f;   // the hit itself, wherever it falls
        Voice voice;
        voice.start (s, 0, velocity * velocity, sampleRate, 0x2545f491u);
        for (int done = 0; done < buffer.getNumSamples() && voice.active; done += 256)
            voice.render (buffer.getWritePointer (0, done), buffer.getWritePointer (1, done),
                          juce::jmin (256, buffer.getNumSamples() - done));
        std::array<float, 2> low {}, high {};
        filterSlot (s, buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), low, high, sampleRate);
        buffer.applyGain (settings.gain * settings.trim);
        return buffer;
    }

    double Kit::hitSeconds (const SlotSettings& settings)
    {
        if (settings.model == Model::sample)
        {
            if (settings.sample == nullptr || settings.sample->length() < 2)
                return 0.0;
            const auto share = juce::jmax (0.0, (double) settings.end - (double) settings.start);
            return share * settings.sample->length() / (settings.sample->sampleRate * std::exp2 (settings.pitch / 12.0));
        }
        auto s = settings;
        s.envelope = nullptr;
        s.trim = s.gain = 1.0f;
        s.lowPass = 20000.0f;
        s.highPass = 20.0f;
        constexpr double rate = 24000.0;   // plenty to measure by
        const auto hit = renderHit (s, 1.0f, rate, 4.0);
        const auto* x = hit.getReadPointer (0);
        auto peak = 0.0f;
        for (int i = 0; i < hit.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (x[i]));
        auto last = 0;
        for (int i = hit.getNumSamples(); --i > 0;)
            if (std::abs (x[i]) > peak * 0.001f)
            {
                last = i;
                break;
            }
        return (last + 1) / rate;
    }
}
