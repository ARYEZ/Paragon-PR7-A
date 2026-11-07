#include "guitar/Guitar.h"

namespace guitar
{
    int layerFor (int velocity) noexcept
    {
        // Halfway between the sustain list's velocities (45, 85, 118): below 65, below 102, then hard.
        return renders::layerFor (Articulation::sustain, velocity);
    }

    bool isArticulationKey (KeyMap map, int note) noexcept
    {
        using namespace keyswitch;
        return map == KeyMap::split ? (note == c1 || note == cSharp1)
                                    : (note == c1 || note == cSharp1 || note == dSharp1 || note == e1);
    }

    Played playedFor (KeyMap map, int key, int velocity) noexcept
    {
        using namespace keyswitch;
        velocity = juce::jlimit (1, 127, velocity);
        // A zone [lo, hi] spread over 1-127 (the owner's SCALING).
        const auto scaled = [velocity] (int lo, int hi) { return juce::jlimit (1, 127, 1 + juce::roundToInt ((velocity - lo) * 126.0 / (double) (hi - lo))); };
        if (map == KeyMap::split)
        {
            const auto power = key == cSharp1;
            if (velocity <= 59)
                return { Articulation::mute, scaled (1, 59), power };
            if (velocity <= 119)
                return { Articulation::sustain, scaled (60, 119), power };
            return { Articulation::sustain, 127, power };   // the reference's rake / pinch: not rendered
        }
        if (key == cSharp1 || key == e1)
            return { Articulation::mute, velocity, key == e1 };
        return { Articulation::sustain, juce::jmin (119, velocity), key == dSharp1 };
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
            soundingMuted[(size_t) s] = false;
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
                else if (isArticulationKey (s.keyMap, n)) articulationKey = n;
                return;
            }
            const auto note = n + s.transpose;
            const auto played = playedFor (s.keyMap, articulationKey, velocity);
            // A power chord: the root, the fifth and the octave (as far as the guitar goes), fretted together.
            for (const auto interval : { 0, 7, 12 })
            {
                if (interval > 0 && ! played.powerChord)
                    break;
                const auto each = note + interval;
                if (each >= lowestNote && each <= highestNote && chord.count < maxChord)
                {
                    chord.notes[(size_t) chord.count] = each;
                    chord.velocities[(size_t) chord.count] = played.velocity;
                    chord.sources[(size_t) chord.count] = note;
                    chord.articulations[(size_t) chord.count++] = played.articulation;
                }
            }
        }
        else if (m.isNoteOff())
        {
            const auto note = m.getNoteNumber() + s.transpose;
            for (auto& v : voices)
                if (v.active && v.held && v.sourceNote == note)
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
        // Each articulation's round-robins in turn (as 0.2.0), once per stroke for the whole chord.
        std::array<int, numArticulations> roundRobins { -1, -1 };
        const auto roundRobinFor = [&] (Articulation a)
        {
            auto& r = roundRobins[(size_t) a];
            if (r < 0)
            {
                auto& counter = nextRoundRobin[(size_t) a][stroke == Stroke::down ? 0u : 1u];
                r = counter;
                counter = (counter + 1) % renders::spec (a).roundRobins;
            }
            return r;
        };

        for (size_t i = 0; i < (size_t) chord.count; ++i)
        {
            const auto p = places[i];
            if (! p.valid())
                continue;
            last = p;
            if (const auto old = onString[(size_t) p.string]; old >= 0)
                stopVoice (voices[(size_t) old], stopSeconds);
            auto articulation = chord.articulations[i];
            auto velocity = chord.velocities[i];
            auto layer = renders::layerFor (articulation, velocity);
            // Which take: the stroke's round-robin in turn (as 0.2.0); with VARIATION, now and then a borrowed one instead.
            auto found = library != nullptr ? library->find (articulation, p.string, p.fret, layer, stroke, roundRobinFor (articulation)) : Library::Found {};
            if (found.take == nullptr && articulation == Articulation::mute && library != nullptr)
            {
                // No palm mutes imported yet: the softest sustain takes stand in.
                articulation = Articulation::sustain;
                layer = 0;
                velocity = renders::spec (articulation).velocities[0];
                found = library->find (articulation, p.string, p.fret, layer, stroke, roundRobinFor (articulation));
            }
            auto borrowedGain = 1.0f;
            auto& lastTake = lastByStroke[(size_t) p.string][stroke == Stroke::down ? 0u : 1u];
            if (library != nullptr && found.take != nullptr && found.exact && s.variation > 0.0f
                && random.nextFloat() < 0.35f * s.variation)
            {
                struct Candidate { const Take* take; double ratio; float gain; };
                std::array<Candidate, 24> candidates {};
                auto count = 0;
                const auto own = found.take->loudness;
                const auto borrow = [&] (int fret, int fromLayer)
                {
                    for (int rr = 0; rr < renders::spec (articulation).roundRobins && count < (int) candidates.size(); ++rr)
                        if (const auto* t = library->exact (articulation, p.string, fret, fromLayer, stroke, rr);
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
            v.sourceNote = chord.sources[i];
            v.muted = articulation == Articulation::mute;
            v.take = found.take;
            v.step = found.take->sampleRate / rate * found.pitchRatio;
            const auto ratio = (double) velocity / (double) renders::spec (articulation).velocities[(size_t) layer];
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
            soundingMuted[(size_t) st] = v != nullptr && v->active && v->muted;
        }
        shownHand = handNow (s);
        activeVoices = any;
    }
}
