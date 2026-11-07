#include "guitar/Guitar.h"

namespace guitar
{
    int layerFor (int velocity) noexcept
    {
        // Halfway between the sustain list's velocities (45, 85, 118): below 65, below 102, then hard.
        return renders::layerFor (Articulation::sustain, velocity);
    }

    void Guitar::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void Guitar::reset() noexcept
    {
        for (auto& v : voices)
            v = {};
        onString.fill (-1);
        for (int s = 0; s < numStrings; ++s)
        {
            soundingFret[(size_t) s] = -1;
            soundingLevel[(size_t) s] = 0.0f;
        }
        activeVoices = 0;
    }

    int Guitar::getForcedString (const Settings& s) const noexcept
    {
        return ksForce >= -1 ? ksForce : s.forceString;
    }

    int Guitar::handNow (const Settings& s) const noexcept
    {
        const auto hand = ksHand >= 0 ? ksHand : s.hand;
        return hand > 0 ? hand : autoHand;
    }

    Guitar::Voice& Guitar::freeVoice() noexcept
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.active)
                return v;
        // None free: the oldest released one, else the oldest of all.
        for (auto& v : voices)
            if (! v.held && (best == nullptr || v.age < best->age))
                best = &v;
        if (best == nullptr)
            for (auto& v : voices)
                if (best == nullptr || v.age < best->age)
                    best = &v;
        best->active = false;
        best->stream = -1;
        return *best;
    }

    void Guitar::stopVoice (Voice& v, double seconds) noexcept
    {
        if (! v.active)
            return;
        if (v.held && v.string >= 0 && onString[(size_t) v.string] == (int) (&v - voices.data()))
            onString[(size_t) v.string] = -1;
        v.held = false;
        const auto step = (float) (1.0 / juce::jmax (1.0, seconds * rate));
        v.fadeStep = juce::jmax (v.fadeStep, step);
    }

    void Guitar::handle (const juce::MidiMessage& m, const Settings& s, Chord& chord) noexcept
    {
        if (m.isNoteOn())
        {
            const auto n = m.getNoteNumber();
            const auto velocity = (int) m.getVelocity();
            if (n < lowestNote || n == keyswitch::pickUp || n == keyswitch::pickDown || n == keyswitch::pickAlternate)
            {
                if (n >= keyswitch::forceLowA && n < keyswitch::forceLowA + numStrings) ksForce = n - keyswitch::forceLowA;
                else if (n == keyswitch::forceOff) ksForce = -1;
                else if (n == keyswitch::setHand) ksHand = juce::jlimit (1, numFrets - 3, velocity);
                else if (n == keyswitch::handAuto) ksHand = 0;
                else if (n == keyswitch::pickUp) ksPick = (int) PickMode::up;
                else if (n == keyswitch::pickDown) ksPick = (int) PickMode::down;
                else if (n == keyswitch::pickAlternate) ksPick = (int) PickMode::alternate;
                return;   // (C-1, sustain: the only articulation yet)
            }
            const auto note = n + s.transpose;
            if (note >= lowestNote && note <= highestNote && chord.count < maxChord)
            {
                chord.notes[(size_t) chord.count] = note;
                chord.velocities[(size_t) chord.count++] = velocity;
            }
        }
        else if (m.isNoteOff())
        {
            const auto note = m.getNoteNumber() + s.transpose;
            for (auto& v : voices)
                if (v.active && v.held && v.note == note)
                    stopVoice (v, s.releaseSeconds);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
                stopVoice (v, m.isAllSoundOff() ? stopSeconds : s.releaseSeconds);
        }
    }

    void Guitar::strike (const Chord& chord, const Settings& s) noexcept
    {
        if (chord.count == 0)
            return;
        std::array<bool, numStrings> busy {};
        for (int st = 0; st < numStrings; ++st)
            busy[(size_t) st] = onString[(size_t) st] >= 0;
        FretSettings fs { s.fretMode, getForcedString (s) };
        std::array<Position, maxChord> places {};
        const auto handIsAuto = (ksHand >= 0 ? ksHand : s.hand) == 0;
        chooseChord (chord.notes.data(), chord.count, busy, handNow (s), fs, places.data(), handIsAuto);

        const auto pick = ksPick >= 0 ? (PickMode) ksPick : s.pick;
        const auto stroke = pick == PickMode::down ? Stroke::down : pick == PickMode::up ? Stroke::up : nextStroke;
        nextStroke = stroke == Stroke::down ? Stroke::up : Stroke::down;
        lastStrokePlayed = stroke;
        auto& rrCounter = nextRoundRobin[stroke == Stroke::down ? 0u : 1u];
        const auto roundRobin = rrCounter;
        rrCounter = (rrCounter + 1) % renders::spec (Articulation::sustain).roundRobins;

        for (size_t i = 0; i < (size_t) chord.count; ++i)
        {
            const auto p = places[i];
            if (! p.valid())
                continue;
            last = p;
            if (const auto old = onString[(size_t) p.string]; old >= 0)
                stopVoice (voices[(size_t) old], stopSeconds);
            const auto velocity = chord.velocities[i];
            const auto layer = layerFor (velocity);
            // Which take: the stroke's round-robin in turn (as 0.2.0); with VARIATION, now and then a borrowed one instead.
            auto found = library != nullptr ? library->find (Articulation::sustain, p.string, p.fret, layer, stroke, roundRobin) : Library::Found {};
            auto borrowedGain = 1.0f;
            auto& lastTake = lastByStroke[(size_t) p.string][stroke == Stroke::down ? 0u : 1u];
            if (library != nullptr && found.take != nullptr && found.exact && s.variation > 0.0f
                && random.nextFloat() < 0.35f * s.variation)
            {
                struct Candidate { const Take* take; double ratio; float gain; };
                std::array<Candidate, 8> candidates {};
                auto count = 0;
                const auto own = found.take->loudness;
                const auto borrow = [&] (int fret, int fromLayer)
                {
                    for (int rr = 0; rr < renders::spec (Articulation::sustain).roundRobins && count < (int) candidates.size(); ++rr)
                        if (const auto* t = library->exact (Articulation::sustain, p.string, fret, fromLayer, stroke, rr);
                            t != nullptr && t != lastTake && t->loudness > 0.0f && own > 0.0f)
                            candidates[(size_t) count++] = { t, std::pow (2.0, (p.fret - fret) / 12.0), juce::jlimit (0.25f, 4.0f, own / t->loudness) };
                };
                borrow (p.fret, layer - 1);
                borrow (p.fret, layer + 1);
                borrow (p.fret - 1, layer);
                borrow (p.fret + 1, layer);
                if (count > 0)
                {
                    const auto& chosen = candidates[(size_t) random.nextInt (count)];
                    found = { chosen.take, chosen.ratio, false };
                    borrowedGain = chosen.gain;
                }
            }
            if (found.take == nullptr)
                continue;
            lastTake = found.take;
            lastTakePlayed = found.take;
            auto& v = freeVoice();
            v = {};
            v.active = v.held = true;   // (VARIATION below may start it a little in)
            v.string = p.string;
            v.note = chord.notes[i];
            v.take = found.take;
            v.step = found.take->sampleRate / rate * found.pitchRatio;
            const auto ratio = (double) velocity / (double) renders::spec (Articulation::sustain).velocities[(size_t) layer];
            v.gain = (float) juce::jlimit (0.05, 2.0, std::pow (ratio, 1.5 * (double) s.velocityToVolume)) * borrowedGain;
            if (s.variation > 0.0f)
            {
                // VARIATION: a little later into the take, a little louder or quieter, a little sharp or flat.
                v.position = (double) s.variation * random.nextDouble() * 0.003 * found.take->sampleRate;
                v.gain *= juce::Decibels::decibelsToGain (s.variation * (random.nextFloat() - 0.5f));
                v.step *= std::pow (2.0, (double) s.variation * (random.nextDouble() * 6.0 - 3.0) / 1200.0);
            }
            v.age = ++clock;
            if (found.take->stream != nullptr && s.streamer != nullptr)
                if ((v.stream = s.streamer->claim (*found.take->stream)) >= 0)
                    s.streamer->attach (v.stream, 0);
            onString[(size_t) p.string] = (int) (&v - voices.data());
            if (handIsAuto)
                autoHand = followHand (autoHand, p.fret, s.fretMode);
        }
    }

    void Guitar::render (Voice& v, float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        const auto& take = *v.take;
        const auto head = (juce::int64) take.head.size();
        const auto length = v.stream >= 0 ? take.frames : head;
        const auto frameAt = [&] (juce::int64 f) -> float
        {
            if (f < 0 || f >= length)
                return 0.0f;
            if (f < head)
                return take.head[(size_t) f];
            return s.streamer->sample (v.stream, 0, 0, f, s.waitForDisk);
        };
        for (int i = 0; i < numSamples; ++i)
        {
            const auto f = (juce::int64) v.position;
            if (f + 1 >= length)
            {
                v.active = false;
                break;
            }
            const auto frac = (float) (v.position - (double) f);
            const auto a = frameAt (f), b = frameAt (f + 1);
            const auto out = (a + (b - a) * frac) * v.gain * v.fade;
            left[i] += out;
            right[i] += out;
            v.position += v.step;
            if (v.fadeStep > 0.0f && (v.fade -= v.fadeStep) <= 0.0f)
            {
                v.active = false;
                break;
            }
        }
        if (! v.active && v.held && v.string >= 0 && onString[(size_t) v.string] == (int) (&v - voices.data()))
            onString[(size_t) v.string] = -1;
        if (v.stream >= 0 && v.active)
            s.streamer->consumed (v.stream, 0, juce::jmax (head, (juce::int64) v.position - 2));
    }

    void Guitar::process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, int midiOffset, const Settings& s) noexcept
    {
        // A new guitar: every voice let go first (their takes belong to the old one).
        if (s.library != library)
        {
            reset();
            library = s.library;
        }
        // A setting changed on the page: it wins over its keyswitch again.
        if (seenSettings)
        {
            if (s.forceString != lastSeen.forceString) ksForce = -2;
            if (s.hand != lastSeen.hand) ksHand = -1;
            if (s.pick != lastSeen.pick) ksPick = -1;
        }
        lastSeen = s;
        seenSettings = true;

        Chord chord;
        auto done = 0;
        const auto renderTo = [&] (int to)
        {
            if (to <= done)
                return;
            for (auto& v : voices)
                if (v.active)
                    render (v, left + done, right + done, to - done, s);
            done = to;
        };
        auto chordAt = -1;
        for (const auto metadata : midi)
        {
            const auto pos = metadata.samplePosition - midiOffset;
            if (pos < 0 || pos >= numSamples)
                continue;
            if (pos != chordAt && chord.count > 0)
            {
                strike (chord, s);
                chord.count = 0;
            }
            renderTo (pos);
            chordAt = pos;
            handle (metadata.getMessage(), s, chord);
        }
        strike (chord, s);
        renderTo (numSamples);

        // The hand goes home after a second with nothing held; the page's view of it all.
        auto held = false;
        auto any = 0;
        std::array<bool, Streamer::maxStreams> inUse {};
        for (const auto& v : voices)
            if (v.active)
            {
                ++any;
                held = held || v.held;
                if (v.stream >= 0 && v.stream < Streamer::maxStreams)
                    inUse[(size_t) v.stream] = true;
            }
        if (s.streamer != nullptr)
            s.streamer->release (inUse);
        idleSamples = held ? 0.0 : idleSamples + numSamples;
        if (idleSamples > rate)
            autoHand = 1;
        for (int st = 0; st < numStrings; ++st)
        {
            const auto index = onString[(size_t) st];
            const auto* v = index >= 0 ? &voices[(size_t) index] : nullptr;
            soundingFret[(size_t) st] = v != nullptr && v->active ? v->note - openNotes[(size_t) st] : -1;
            soundingLevel[(size_t) st] = v != nullptr && v->active ? v->gain * v->fade : 0.0f;
        }
        shownHand = handNow (s);
        activeVoices = any;
    }
}
