#include "MicSim.h"

#include <cmath>

namespace synth::micsim
{
    namespace
    {
        constexpr float speedOfSound = 343.0f;
        constexpr float nearest = 0.1f;   // metres: a close mic's own drum arrives at once (delays count past this)

        // Each mic channel, in mixer::Channel's order. Close mics point at their drum: one they aren't pointed at is
        // `offAxis` quieter (and duller: see colour). The overheads, room and ambience are pairs over / in front of
        // the kit and hear every drum the same way.
        const std::array<Mic, mixer::numChannels> mics {{
            { mixer::kickIn,      {  0.0f,  0.60f, 0.30f }, {}, false, 0.15f, 0.12f },   // inside the kick
            { mixer::kickOut,     {  0.0f,  1.05f, 0.30f }, {}, false, 0.50f, 0.25f },   // in front of its front head
            { mixer::snareTop,    { -0.12f, 0.25f, 0.75f }, {}, false, 0.15f, 0.35f },
            { mixer::snareBottom, { -0.15f, 0.38f, 0.50f }, {}, false, 0.10f, 0.30f },
            { mixer::hiHat,       { -0.45f, 0.35f, 1.15f }, {}, false, 0.20f, 0.35f },
            { mixer::tom1,        {  0.0f,  0.50f, 0.97f }, {}, false, 0.17f, 0.30f },
            { mixer::tom2,        {  0.42f, 0.25f, 0.72f }, {}, false, 0.16f, 0.30f },
            { mixer::tom3,        {  0.62f, 0.02f, 0.67f }, {}, false, 0.16f, 0.30f },
            { mixer::cymbals,     {  0.10f, 0.70f, 1.75f }, {}, false, 0.30f, 0.50f },   // each cymbal's own mic, 0.3 m over it
            { mixer::overheads,   { -0.45f, 0.35f, 1.90f }, {  0.45f, 0.35f, 1.90f }, true, 0.45f, 1.0f },
            { mixer::room,        { -1.60f, 2.60f, 1.40f }, {  1.60f, 2.60f, 1.40f }, true, 0.80f, 1.0f },
            { mixer::ambience,    { -2.80f, 5.50f, 2.40f }, {  2.80f, 5.50f, 2.40f }, true, 0.90f, 1.0f },
            { mixer::other,       {  0.0f,  0.30f, 1.30f }, {}, false, 0.30f, 0.30f },
        }};

        bool isExtra (int piece) noexcept { return piece >= 11 && piece <= 13; }   // the clap, tambourine, shaker

        float distance (Point a, Point b) noexcept
        {
            return std::sqrt ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        }

        // THE ROOMS: their size (scales the delay lines and reflections), decay (seconds to -60 dB), how loud the
        // early reflections are, and how fast the highs die (a damping cutoff's scale).
        struct Room { float size, seconds, early, damping; };
        constexpr std::array<Room, 4> rooms {{
            { 0.45f, 0.32f, 0.85f, 1.2f },   // Small Room
            { 0.75f, 0.60f, 0.70f, 1.0f },   // Studio
            { 1.15f, 1.10f, 0.60f, 0.85f },  // Live Room
            { 1.90f, 2.30f, 0.45f, 0.7f },   // Big Hall
        }};
        constexpr std::array<float, 8> lineMs { 23.3f, 29.7f, 37.1f, 41.1f, 47.9f, 53.3f, 59.9f, 67.1f };
        constexpr std::array<float, 8> earlyMsL { 4.3f, 7.9f, 11.3f, 15.7f, 19.1f, 23.9f, 29.3f, 34.1f };
        constexpr std::array<float, 8> earlyMsR { 5.1f, 8.7f, 12.9f, 14.9f, 21.3f, 25.1f, 27.7f, 36.3f };
        constexpr float earlyMaxMs = 40.0f, ambienceMs = 18.0f, biggest = 1.9f;

        size_t powerOfTwoAbove (size_t n) noexcept
        {
            size_t p = 1;
            while (p < n)
                p <<= 1;
            return p;
        }
    }

    juce::Array<int> channelsFor (int piece)
    {
        if (isExtra (piece))
            return { mixer::other, mixer::overheads, mixer::room, mixer::ambience };
        juce::Array<int> all;
        for (int c = 0; c < mixer::numChannels; ++c)
            if (c != mixer::other)
                all.add (c);
        return all;
    }

    const Mic& micFor (int channel) { return mics[(size_t) juce::jlimit (0, mixer::numChannels - 1, channel)]; }

    Tap tapFor (int piece, int channel, bool rightSide, bool own, double sampleRate)
    {
        const auto& mic = micFor (channel);
        const auto place = piecePlaces[(size_t) juce::jlimit (0, numPieces - 1, piece)];
        auto d = distance (place, rightSide && mic.stereo ? mic.right : mic.left);
        if (channel == mixer::cymbals && own)
            d = 0.3f;   // the cymbal's own mic
        if (channel == mixer::other && own)
            d = 0.3f;
        const auto pointed = own || mixer::hearsWholeKit (channel);
        Tap t;
        t.gain = juce::jmin (1.2f, mic.reference / juce::jmax (0.05f, d)) * (pointed ? 1.0f : mic.offAxis);
        t.delay = juce::jmax (0, (int) std::lround ((d - nearest) / speedOfSound * sampleRate));
        return t;
    }

    void Biquad::set (Type type, double hz, double q, double gainDb, double sampleRate) noexcept
    {
        // The RBJ cookbook's.
        const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * sampleRate, hz) / sampleRate;
        const auto cw = std::cos (w), sw = std::sin (w);
        const auto alpha = sw / (2.0 * q);
        const auto A = std::pow (10.0, gainDb / 40.0);
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;
        switch (type)
        {
            case Type::lowCut:
                nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::highCut:
                nb0 = (1 - cw) / 2; nb1 = 1 - cw; nb2 = (1 - cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::peak:
                nb0 = 1 + alpha * A; nb1 = -2 * cw; nb2 = 1 - alpha * A; na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A;
                break;
            case Type::lowShelf:
            {
                const auto s = 2 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) - (A - 1) * cw + s); nb1 = 2 * A * ((A - 1) - (A + 1) * cw); nb2 = A * ((A + 1) - (A - 1) * cw - s);
                na0 = (A + 1) + (A - 1) * cw + s; na1 = -2 * ((A - 1) + (A + 1) * cw); na2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::highShelf:
            {
                const auto s = 2 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) + (A - 1) * cw + s); nb1 = -2 * A * ((A - 1) + (A + 1) * cw); nb2 = A * ((A + 1) + (A - 1) * cw - s);
                na0 = (A + 1) - (A - 1) * cw + s; na1 = 2 * ((A - 1) - (A + 1) * cw); na2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }

    //==============================================================================
    void Simulator::prepare (double newSampleRate, int newBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = juce::jmax (1, newBlockSize);
        // The longest tap: the farthest piece from an ambience mic, plus the mixer's TIME OFFSET (10 ms), with room.
        lineSize = powerOfTwoAbove ((size_t) std::ceil (0.06 * sampleRate) + (size_t) blockSize);
        lineMask = lineSize - 1;
        lines.assign ((size_t) numSlots * lineSize, 0.0f);
        inputs.assign ((size_t) numSlots * (size_t) blockSize, 0.0f);

        // Each channel's colour (our choices): what the mic and where it is do to the sound.
        using T = Biquad::Type;
        const auto set = [this] (int channel, std::initializer_list<std::tuple<T, double, double, double>> list)
        {
            int n = 0;
            for (const auto& [type, hz, q, db] : list)
            {
                for (auto& line : filters[(size_t) channel])
                    line[(size_t) n].set (type, hz, q, db, sampleRate);
                ++n;
            }
            numFilters[(size_t) channel] = n;
            // Bleed: off the mic's axis, the highs fall away (the last filter, on the bleed lines only).
            for (int which = 2; which < 4; ++which)
                filters[(size_t) channel][(size_t) which][(size_t) n].set (T::highCut, 5000.0, 0.7, 0.0, sampleRate);
        };
        set (mixer::kickIn,      { { T::peak, 4000.0, 1.0, 5.0 }, { T::peak, 350.0, 1.2, -5.0 } });   // the beater's click, less box
        set (mixer::kickOut,     { { T::lowShelf, 70.0, 0.7, 3.0 }, { T::highCut, 5000.0, 0.7, 0.0 } }); // the boom
        set (mixer::snareTop,    { { T::lowCut, 70.0, 0.7, 0.0 }, { T::peak, 5000.0, 0.8, 2.0 } });
        set (mixer::snareBottom, { { T::lowCut, 250.0, 0.7, 0.0 }, { T::highShelf, 6000.0, 0.7, 4.0 } }); // the wires
        set (mixer::hiHat,       { { T::lowCut, 350.0, 0.7, 0.0 }, { T::highShelf, 9000.0, 0.7, 2.0 } });
        for (const auto tom : { mixer::tom1, mixer::tom2, mixer::tom3 })
            set (tom,            { { T::lowCut, 45.0, 0.7, 0.0 }, { T::peak, 450.0, 1.2, -4.0 } });
        set (mixer::cymbals,     { { T::lowCut, 300.0, 0.7, 0.0 }, { T::highShelf, 10000.0, 0.7, 1.5 } });
        set (mixer::overheads,   { { T::lowCut, 90.0, 0.6, 0.0 }, { T::highShelf, 10000.0, 0.7, 2.0 } });
        set (mixer::room,        { { T::lowShelf, 120.0, 0.7, 1.5 }, { T::highCut, 9000.0, 0.7, 0.0 } });
        set (mixer::ambience,    { { T::lowCut, 60.0, 0.7, 0.0 }, { T::highCut, 6000.0, 0.7, 0.0 } });
        set (mixer::other,       { { T::lowCut, 80.0, 0.7, 0.0 } });
        states.assign ((size_t) mixer::numChannels * (size_t) mixer::numMicChannels * 4 * maxFilters * 2, 0.0f);

        // The room, at its biggest.
        roomIn.assign (2 * (size_t) blockSize, 0.0f);
        earlyMask = powerOfTwoAbove ((size_t) std::ceil (earlyMaxMs * biggest * 0.001 * sampleRate) + 2) - 1;
        early.assign (2 * (earlyMask + 1), 0.0f);
        ambienceMask = powerOfTwoAbove ((size_t) std::ceil (ambienceMs * biggest * 0.001 * sampleRate) + 2) - 1;
        ambienceDelay.assign (2 * (ambienceMask + 1), 0.0f);
        fdnMask = powerOfTwoAbove ((size_t) std::ceil (lineMs.back() * biggest * 0.001 * sampleRate) + 2) - 1;
        fdn.assign ((size_t) numLines * (fdnMask + 1), 0.0f);
        lastRoom = -1;
        reset();
    }

    void Simulator::reset() noexcept
    {
        std::fill (lines.begin(), lines.end(), 0.0f);
        std::fill (states.begin(), states.end(), 0.0f);
        std::fill (early.begin(), early.end(), 0.0f);
        std::fill (ambienceDelay.begin(), ambienceDelay.end(), 0.0f);
        std::fill (fdn.begin(), fdn.end(), 0.0f);
        std::fill (roomIn.begin(), roomIn.end(), 0.0f);
        lineLeft = {};
        fedNow = {};
        dampState = {};
        for (auto& t : toneState)
            t = 0.0f;
        roomLeft = 0;
        writePos = 0;
    }

    void Simulator::fed (int slot, int numSamples) noexcept
    {
        const auto* in = input (slot);
        auto* line = lines.data() + (size_t) slot * lineSize;
        for (int i = 0; i < numSamples; ++i)
            line[(writePos + (size_t) i) & lineMask] = in[i];
        fedNow[(size_t) slot] = true;
        lineLeft[(size_t) slot] = (int) lineSize;
    }

    void Simulator::begin (int numSamples) noexcept
    {
        // A slot that sounded lately but not in this block: its line silent here, so its taps read silence.
        for (int s = 0; s < numSlots; ++s)
            if (! fedNow[(size_t) s] && lineLeft[(size_t) s] > 0)
            {
                auto* line = lines.data() + (size_t) s * lineSize;
                for (int i = 0; i < numSamples; ++i)
                    line[(writePos + (size_t) i) & lineMask] = 0.0f;
            }
    }

    bool Simulator::busy() const noexcept
    {
        if (roomLeft > 0)
            return true;
        for (const auto l : lineLeft)
            if (l > 0)
                return true;
        return false;
    }

    void Simulator::tap (int slot, int delay, float gain, float* into, int numSamples) const noexcept
    {
        if (gain == 0.0f)
            return;
        const auto* line = lines.data() + (size_t) slot * lineSize;
        auto pos = (writePos - (size_t) juce::jlimit (0, maxDelay(), delay)) & lineMask;
        for (int i = 0; i < numSamples; ++i)
        {
            into[i] += gain * line[pos];
            pos = (pos + 1) & lineMask;
        }
    }

    void Simulator::colour (int source, int destination, float* line, int which, int numSamples) noexcept
    {
        const auto n = numFilters[(size_t) source] + (which >= 2 ? 1 : 0);
        auto& f = filters[(size_t) source][(size_t) which];
        // Each source and destination pair keeps its own filters' memory (a mic routed elsewhere is its own line).
        auto* z = states.data() + ((((size_t) source * mixer::numMicChannels + (size_t) destination) * 4 + (size_t) which) * maxFilters) * 2;
        for (int k = 0; k < n; ++k)
        {
            auto bq = f[(size_t) k];
            bq.z1 = z[2 * k];
            bq.z2 = z[2 * k + 1];
            for (int i = 0; i < numSamples; ++i)
                line[i] = bq.process (line[i]);
            z[2 * k] = std::abs (bq.z1) < 1.0e-15f ? 0.0f : bq.z1;
            z[2 * k + 1] = std::abs (bq.z2) < 1.0e-15f ? 0.0f : bq.z2;
        }
    }

    void Simulator::setRoom (const Settings& settings) noexcept
    {
        const auto index = juce::jlimit (0, (int) rooms.size() - 1, settings.room);
        if (index == lastRoom && settings.decay == lastDecay && settings.tone == lastTone)
            return;
        lastRoom = index;
        lastDecay = settings.decay;
        lastTone = settings.tone;
        const auto& room = rooms[(size_t) index];
        const auto seconds = juce::jmax (0.05f, room.seconds * juce::jlimit (0.3f, 3.0f, settings.decay));
        // Each line feeds back at the level that makes the whole fall 60 dB in `seconds`; its damping filter (one
        // pole) makes the highs fall sooner, more so in a big, dark room.
        const auto cutoff = (2500.0f + 9000.0f * settings.tone) * room.damping;
        const auto a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate);
        for (int i = 0; i < numLines; ++i)
        {
            fdnLength[(size_t) i] = juce::jlimit ((size_t) 1, fdnMask, (size_t) std::lround (lineMs[(size_t) i] * room.size * 0.001 * sampleRate));
            fdnGain[(size_t) i] = std::pow (10.0f, -3.0f * (float) fdnLength[(size_t) i] / (float) sampleRate / seconds);
            damp[(size_t) i] = a;
        }
        for (size_t k = 0; k < earlyTapsL.size(); ++k)
        {
            earlyTapsL[k] = juce::jlimit (1, (int) earlyMask, (int) std::lround (earlyMsL[k] * room.size * 0.001 * sampleRate));
            earlyTapsR[k] = juce::jlimit (1, (int) earlyMask, (int) std::lround (earlyMsR[k] * room.size * 0.001 * sampleRate));
        }
        ambienceLength = juce::jlimit ((size_t) 1, ambienceMask, (size_t) std::lround (ambienceMs * room.size * 0.001 * sampleRate));
        earlyLevel = room.early;
        tailSeconds = seconds;
        toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (4000.0f + 12000.0f * settings.tone) / (float) sampleRate);
    }

    void Simulator::runRoom (const Settings& settings, int numSamples, float* roomLeftOut, float* roomRightOut,
                             float* ambienceLeftOut, float* ambienceRightOut) noexcept
    {
        setRoom (settings);
        const auto* inL = roomIn.data();
        const auto* inR = inL + blockSize;
        auto heard = false;
        for (int i = 0; i < numSamples && ! heard; ++i)
            heard = inL[i] != 0.0f || inR[i] != 0.0f;
        if (heard)
            roomLeft = (int) ((tailSeconds * 1.3f + 0.1f) * (float) sampleRate);
        if (roomLeft <= 0)
            return;

        auto* eL = early.data();
        auto* eR = eL + earlyMask + 1;
        auto* aL = ambienceDelay.data();
        auto* aR = aL + ambienceMask + 1;
        const auto lineStride = fdnMask + 1;
        constexpr float earlyGains[8] { 0.80f, -0.66f, 0.55f, -0.45f, 0.37f, -0.30f, 0.25f, -0.20f };
        for (int i = 0; i < numSamples; ++i)
        {
            eL[earlyPos] = inL[i];
            eR[earlyPos] = inR[i];
            // The early reflections: each a copy from one side or the other (alternating), quieter as they come later.
            auto erL = 0.0f, erR = 0.0f;
            for (size_t k = 0; k < 8; ++k)
            {
                erL += earlyGains[k] * ((k & 1) == 0 ? eL : eR)[(earlyPos - (size_t) earlyTapsL[k]) & earlyMask];
                erR += earlyGains[k] * ((k & 1) == 0 ? eR : eL)[(earlyPos - (size_t) earlyTapsR[k]) & earlyMask];
            }
            earlyPos = (earlyPos + 1) & earlyMask;

            // The tail: 8 lines, out through damping and feedback gain, mixed (Householder) and back in with the
            // early reflections.
            std::array<float, numLines> out {};
            auto sum = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                const auto* line = fdn.data() + (size_t) k * lineStride;
                const auto o = line[(fdnPos - fdnLength[(size_t) k]) & fdnMask];
                auto& s = dampState[(size_t) k];
                s += damp[(size_t) k] * (o - s);
                out[(size_t) k] = s * fdnGain[(size_t) k];
                sum += out[(size_t) k];
            }
            const auto mixDown = sum * (2.0f / numLines);
            auto tailL = 0.0f, tailR = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                auto* line = fdn.data() + (size_t) k * lineStride;
                const auto inject = ((k & 1) == 0 ? erL : erR) * ((k & 2) == 0 ? 0.5f : -0.5f);
                line[fdnPos] = out[(size_t) k] - mixDown + inject;
                if ((k & 1) == 0)
                    tailL += out[(size_t) k] * ((k & 2) == 0 ? 1.0f : -1.0f);
                else
                    tailR += out[(size_t) k] * ((k & 2) == 0 ? 1.0f : -1.0f);
            }
            fdnPos = (fdnPos + 1) & fdnMask;
            tailL *= 0.6f;
            tailR *= 0.6f;

            // The room mics: the reflections and the tail; the ambience, farther: only the tail, later (it hears the
            // drums' own sound first, by its own taps). Both darker as TONE goes down.
            aL[ambiencePos] = tailL;
            aR[ambiencePos] = tailR;
            const auto farL = aL[(ambiencePos - ambienceLength) & ambienceMask];
            const auto farR = aR[(ambiencePos - ambienceLength) & ambienceMask];
            ambiencePos = (ambiencePos + 1) & ambienceMask;
            const float values[4] { erL * earlyLevel + tailL * 0.7f, erR * earlyLevel + tailR * 0.7f,
                                    farL * 1.1f, farR * 1.1f };
            float* outs[4] { roomLeftOut, roomRightOut, ambienceLeftOut, ambienceRightOut };
            for (int c = 0; c < 4; ++c)
            {
                toneState[c] += toneCoef * (values[c] - toneState[c]);
                outs[c][i] += toneState[c];
            }
        }
        roomLeft -= numSamples;
        if (roomLeft <= 0)
        {
            // Gone (below -78 dB): start the next sound from silence.
            std::fill (fdn.begin(), fdn.end(), 0.0f);
            std::fill (early.begin(), early.end(), 0.0f);
            std::fill (ambienceDelay.begin(), ambienceDelay.end(), 0.0f);
            dampState = {};
            for (auto& t : toneState)
                t = 0.0f;
        }
    }

    void Simulator::advance (int numSamples) noexcept
    {
        writePos = (writePos + (size_t) numSamples) & lineMask;
        for (int s = 0; s < numSlots; ++s)
        {
            if (! fedNow[(size_t) s])
                lineLeft[(size_t) s] = juce::jmax (0, lineLeft[(size_t) s] - numSamples);
            fedNow[(size_t) s] = false;
        }
        std::fill (roomIn.begin(), roomIn.end(), 0.0f);
    }
}
