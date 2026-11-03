// THE DRUMS (stage 6a): what each drum sounds like, the kit's playing and routing, and samples.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/synth/Drums.h"

#include <juce_dsp/juce_dsp.h>

using namespace synth::drums;

namespace
{
    constexpr double rate = 48000.0;

    float peakDb (const float* x, int n)
    {
        auto peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::abs (x[i]));
        return juce::Decibels::gainToDecibels (peak, -200.0f);
    }

    // Seconds until the level (peak over 5 ms windows) stays below `belowPeakDb` under the hit's peak.
    double ringSeconds (const juce::AudioBuffer<float>& b, float belowPeakDb)
    {
        const auto* x = b.getReadPointer (0);
        const auto n = b.getNumSamples();
        const auto threshold = juce::Decibels::decibelsToGain (peakDb (x, n) - belowPeakDb);
        const auto window = (int) (0.005 * rate);
        auto last = 0;
        for (int start = 0; start < n; start += window)
        {
            auto peak = 0.0f;
            for (int i = start; i < std::min (n, start + window); ++i)
                peak = std::max (peak, std::abs (x[i]));
            if (peak >= threshold)
                last = start + window;
        }
        return last / rate;
    }

    // The power-weighted mean frequency of the first `seconds`.
    double centroidHz (const juce::AudioBuffer<float>& b, double seconds = 0.1)
    {
        constexpr int order = 13, size = 1 << order;
        std::vector<float> data ((size_t) size * 2, 0.0f);
        const auto n = std::min ({ b.getNumSamples(), size, (int) (seconds * rate) });
        for (int i = 0; i < n; ++i)
            data[(size_t) i] = b.getSample (0, i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n));
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (data.data());
        double sum = 0.0, weighted = 0.0;
        for (int k = 1; k < size / 2; ++k)
        {
            const auto p = (double) data[(size_t) k] * data[(size_t) k];
            sum += p;
            weighted += p * k * rate / size;
        }
        return weighted / std::max (1e-30, sum);
    }

    // How bright the first 10 ms are: the change from sample to sample against the level (no window, so an attack
    // in the first milliseconds counts fully).
    double brightness (const juce::AudioBuffer<float>& b)
    {
        double change = 0.0, level = 0.0;
        for (int i = 1; i < (int) (0.01 * rate); ++i)
        {
            const auto x = (double) b.getSample (0, i), d = x - b.getSample (0, i - 1);
            change += d * d;
            level += x * x;
        }
        return change / std::max (1e-30, level);
    }

    SlotSettings slotDefaults (int slot)
    {
        SlotSettings s;
        const auto& d = synth::drums::slotDefaults[(size_t) slot];
        s.model = d.model;
        s.pitch = d.pitch;
        s.decay = d.decay;
        s.tone = d.tone;
        s.snap = d.snap;
        s.choke = d.choke;
        return s;
    }

    // Every drum synth (SAMPLE plays a file: not one of them), the ones PR7-D 0.2.0 added after SAMPLE too.
    std::vector<Model> synthModels()
    {
        std::vector<Model> models;
        for (int m = 0; m < modelNames.size(); ++m)
            if ((Model) m != Model::sample)
                models.push_back ((Model) m);
        return models;
    }

    SlotSettings model (Model m)
    {
        SlotSettings s;
        s.model = m;
        return s;
    }
}

TEST_CASE ("DRUMS: report each model at its defaults", "[drums][report]")
{
    for (const auto m : synthModels())
    {
        auto s = model (m);
        s.decay = 1.0f;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
            Kit::renderHit (s, 1.0f, rate, 1.0);
        WARN (modelNames[(int) m] << ": " << (juce::Time::getMillisecondCounterHiRes() - started) / 20000.0 * 100.0 << "% of a core per hit sounding");
    }
    for (const auto m : synthModels())
    {
        const auto hit = Kit::renderHit (model (m), 1.0f, rate, 4.0);
        WARN (modelNames[(int) m] << ": peak " << peakDb (hit.getReadPointer (0), hit.getNumSamples()) << " dB, -40 dB after "
                            << ringSeconds (hit, 40.0f) << " s, centroid " << centroidHz (hit) << " Hz");
    }
}

namespace
{
    // The frequency of a steady stretch, from its rising zero crossings (interpolated).
    double frequencyHz (const juce::AudioBuffer<float>& b, double from, double to)
    {
        const auto* x = b.getReadPointer (0);
        const auto start = (int) (from * rate), end = std::min (b.getNumSamples(), (int) (to * rate));
        double first = -1.0, last = -1.0;
        int count = 0;
        for (int i = start + 1; i < end; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const auto t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * rate / (last - first) : 0.0;
    }

    // A kit playing into plain buffers.
    struct Rig
    {
        Kit kit;
        KitSettings settings;
        static constexpr int block = 256;
        std::vector<float> left, right, busLeft, busRight, delayLeft, delayRight;

        Rig()
        {
            kit.prepare (rate, block);
            kit.seedRandom (7);
            settings.on = true;
            for (int k = 0; k < numSlots; ++k)
                settings.slots[(size_t) k] = slotDefaults (k);
        }

        // Plays `seconds` with these notes (note, velocity, at seconds), returning the mix's left side.
        void play (double seconds, std::vector<std::tuple<int, float, double>> notes = {}, bool offs = false)
        {
            const auto n = (int) (seconds * rate);
            for (auto* v : { &left, &right, &busLeft, &busRight, &delayLeft, &delayRight })
                v->assign ((size_t) n, 0.0f);
            for (int done = 0; done < n; done += block)
            {
                const auto count = std::min (block, n - done);
                juce::MidiBuffer midi;
                for (const auto& [note, velocity, at] : notes)
                {
                    const auto position = (int) (at * rate) - done;
                    if (position >= 0 && position < count)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), position);
                        if (offs)
                            midi.addEvent (juce::MidiMessage::noteOff (1, note), std::min (count - 1, position + 10));
                    }
                }
                Outputs out;
                out.left = left.data() + done;
                out.right = right.data() + done;
                out.busLeft[1] = busLeft.data() + done;
                out.busRight[1] = busRight.data() + done;
                out.sendLeft[0] = delayLeft.data() + done;
                out.sendRight[0] = delayRight.data() + done;
                kit.process (out, count, midi, 0, settings);
            }
        }

        static float peak (const std::vector<float>& v, double from = 0.0, double to = 1.0e9)
        {
            auto p = 0.0f;
            for (size_t i = (size_t) (from * rate); i < std::min (v.size(), (size_t) (to * rate)); ++i)
                p = std::max (p, std::abs (v[i]));
            return p;
        }
    };

    std::shared_ptr<Sample> sineSample (double hz, double sampleRate, double seconds, bool stereoSilentRight = false)
    {
        auto s = std::make_shared<Sample>();
        s->name = "sine";
        s->sampleRate = sampleRate;
        s->channels.resize (stereoSilentRight ? 2 : 1);
        const auto n = (int) (seconds * sampleRate);
        for (auto& c : s->channels)
            c.assign ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
            s->channels[0][(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
        return s;
    }
}

TEST_CASE ("DRUMS: every model at its defaults peaks near -3 dBFS and dies away", "[drums]")
{
    for (const auto m : synthModels())
    {
        INFO (modelNames[(int) m]);
        // (The modelled cymbals ring as long as real ones: a ride well past 5 s. Theirs, 8.)
        const auto cymbal = isAcousticCymbal (m);
        const auto hit = Kit::renderHit (model (m), 1.0f, rate, cymbal ? 12.0 : 8.0);
        const auto peak = peakDb (hit.getReadPointer (0), hit.getNumSamples());
        CHECK (peak > -6.0f);
        CHECK (peak < 0.0f);
        CHECK (ringSeconds (hit, 40.0f) < (cymbal ? 8.0 : 5.0));
        CHECK (peakDb (hit.getReadPointer (0, hit.getNumSamples() - 4800), 4800) < peak - 60.0f);
    }
}

TEST_CASE ("DRUMS: DECAY lengthens every model", "[drums]")
{
    for (const auto m : synthModels())
    {
        INFO (modelNames[(int) m]);
        double previous = 0.0;
        for (const auto decay : { 0.1f, 0.5f, 0.9f })
        {
            auto s = model (m);
            s.decay = decay;
            const auto seconds = ringSeconds (Kit::renderHit (s, 1.0f, rate, 8.0), 40.0f);
            CHECK (seconds > previous);
            previous = seconds;
        }
    }
}

TEST_CASE ("DRUMS: KICK settles at 52 Hz (PITCH moves it) and TONE sets how far it falls", "[drums]")
{
    auto s = model (Model::kick);
    s.decay = 0.9f;
    CHECK_THAT (frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.15, 0.7), Catch::Matchers::WithinAbs (52.0, 1.0));
    s.pitch = 12.0f;
    CHECK_THAT (frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.15, 0.7), Catch::Matchers::WithinAbs (104.0, 2.0));
    s.pitch = 0.0f;
    s.snap = 0.0f;   // no click: the pitch alone
    s.tone = 0.1f;
    const auto low = frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.0, 0.02);
    s.tone = 0.9f;
    const auto high = frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.0, 0.02);
    CHECK (high > 1.8 * low);
}

TEST_CASE ("DRUMS: TOM settles at its pitch", "[drums]")
{
    auto s = model (Model::tom);
    s.decay = 0.9f;
    CHECK_THAT (frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.3, 0.8), Catch::Matchers::WithinAbs (100.0, 3.0));
    s.pitch = -5.0f;
    CHECK_THAT (frequencyHz (Kit::renderHit (s, 1.0f, rate, 1.0), 0.3, 0.8), Catch::Matchers::WithinAbs (100.0 * std::exp2 (-5.0 / 12.0), 3.0));
}

TEST_CASE ("DRUMS: TONE brightens the snare, clap, hats, cymbal and cowbell", "[drums]")
{
    for (const auto m : { Model::snare, Model::clap, Model::closedHat, Model::openHat, Model::cymbal, Model::cowbell })
    {
        INFO (modelNames[(int) m]);
        auto s = model (m);
        s.tone = 0.1f;
        const auto dark = centroidHz (Kit::renderHit (s, 1.0f, rate, 0.2));
        s.tone = 0.9f;
        const auto bright = centroidHz (Kit::renderHit (s, 1.0f, rate, 0.2));
        CHECK (bright > 1.15 * dark);
    }
}

TEST_CASE ("DRUMS: SNAP adds the kick's click and the snare's wires", "[drums]")
{
    for (const auto m : { Model::kick, Model::snare, Model::tom })
    {
        INFO (modelNames[(int) m]);
        auto s = model (m);
        s.snap = 0.0f;
        const auto soft = brightness (Kit::renderHit (s, 1.0f, rate, 0.2));
        s.snap = 1.0f;
        const auto snappy = brightness (Kit::renderHit (s, 1.0f, rate, 0.2));
        CHECK (snappy > 2.0 * soft);
    }
}

TEST_CASE ("DRUMS: the hats are metal up high, the kick is all low end", "[drums]")
{
    CHECK (centroidHz (Kit::renderHit (model (Model::closedHat), 1.0f, rate, 0.2)) > 6000.0);
    CHECK (centroidHz (Kit::renderHit (model (Model::kick), 1.0f, rate, 0.5)) < 120.0);
}

TEST_CASE ("DRUMS: the kit's keys: C to B from its root, one-shots, nothing outside", "[drums]")
{
    Rig rig;
    rig.play (0.3, { { 38, 1.0f, 0.0 } }, true);   // D: the snare, released at once
    CHECK (rig.kit.isSounding (2));
    CHECK (Rig::peak (rig.left, 0.1, 0.2) > 0.01f);   // still ringing after its note-off
    CHECK_FALSE (rig.kit.isSounding (0));

    Rig outside;
    outside.play (0.2, { { 35, 1.0f, 0.0 }, { 36 + numSlots, 1.0f, 0.0 } });   // just under the kit, and just over its keys
    CHECK (outside.kit.getNumActiveVoices() == 0);
    CHECK (Rig::peak (outside.left) == 0.0f);

    Rig moved;
    moved.settings.rootKey = 48;
    moved.play (0.2, { { 48, 1.0f, 0.0 } });
    CHECK (moved.kit.isSounding (0));

    Rig off;
    off.settings.on = false;
    off.play (0.2, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (off.left) == 0.0f);
}

TEST_CASE ("DRUMS: hits start on their sample", "[drums]")
{
    Rig rig;
    rig.settings.slots[0].model = Model::rim;
    rig.play (0.1, { { 36, 1.0f, 1000.0 / rate } });
    for (int i = 0; i < 1000; ++i)
        REQUIRE (rig.left[(size_t) i] == 0.0f);
    CHECK (Rig::peak (rig.left, 1001.0 / rate, 1100.0 / rate) > 0.0f);
}

TEST_CASE ("DRUMS: the closed hat chokes the open hat; without CHOKE it rings on", "[drums]")
{
    for (const auto choke : { true, false })
    {
        Rig rig;
        if (! choke)
            rig.settings.slots[10].choke = 0;
        rig.settings.slots[6].mute = true;   // hear only the open hat
        rig.play (0.4, { { 46, 1.0f, 0.0 }, { 42, 1.0f, 0.2 } });
        const auto after = Rig::peak (rig.left, 0.215, 0.4);
        if (choke)
            CHECK (after == 0.0f);
        else
            CHECK (after > 0.01f);
    }
}

TEST_CASE ("DRUMS: MUTE silences a slot, SOLO leaves only the soloed", "[drums]")
{
    Rig muted;
    muted.settings.slots[0].mute = true;
    muted.play (0.3, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (muted.left) == 0.0f);

    Rig solo;
    solo.settings.slots[2].solo = true;
    solo.play (0.3, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (solo.left) == 0.0f);
    solo.play (0.3, { { 38, 1.0f, 0.0 } });
    CHECK (Rig::peak (solo.left) > 0.1f);
}

TEST_CASE ("DRUMS: VELOCITY sets how much the keys' velocity matters", "[drums]")
{
    for (const auto sensitivity : { 1.0f, 0.0f })
    {
        Rig loud, soft;
        loud.settings.velocity = soft.settings.velocity = sensitivity;
        loud.settings.slots[0].model = soft.settings.slots[0].model = Model::tom;
        loud.play (0.3, { { 36, 1.0f, 0.0 } });
        soft.play (0.3, { { 36, 0.5f, 0.0 } });
        const auto db = juce::Decibels::gainToDecibels (Rig::peak (soft.left) / Rig::peak (loud.left));
        CHECK_THAT (db, Catch::Matchers::WithinAbs (sensitivity > 0.5f ? -12.04 : 0.0, 0.3));
    }
}

TEST_CASE ("DRUMS: VOLUME, PAN, the bus and the sends", "[drums]")
{
    Rig rig;
    rig.settings.slots[0].model = Model::tom;
    rig.settings.slots[0].pan = -1.0f;
    rig.settings.sendOn = { true, false };
    rig.settings.slots[0].send = { 0.5f, 1.0f };
    rig.play (0.3, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (rig.left) > 0.5f);
    CHECK (Rig::peak (rig.right) == 0.0f);
    CHECK_THAT (Rig::peak (rig.delayLeft) / Rig::peak (rig.left), Catch::Matchers::WithinAbs (0.5, 1.0e-4));

    Rig quieter;
    quieter.settings.slots[0].model = Model::tom;
    quieter.settings.slots[0].gain = 0.5f;
    quieter.play (0.3, { { 36, 1.0f, 0.0 } });
    CHECK_THAT (Rig::peak (quieter.left) / Rig::peak (rig.left), Catch::Matchers::WithinAbs (0.5, 1.0e-4));

    // On bus B: there, and nowhere else (the bus has its own sends).
    Rig bused;
    bused.settings.slots[0].insertBus = 2;
    bused.settings.sendOn = { true, true };
    bused.play (0.3, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (bused.busLeft) > 0.3f);
    CHECK (Rig::peak (bused.left) == 0.0f);
    CHECK (Rig::peak (bused.delayLeft) == 0.0f);
}

TEST_CASE ("DRUMS: a roll keeps at most six hits of a slot", "[drums]")
{
    Rig rig;
    std::vector<std::tuple<int, float, double>> notes;
    for (int i = 0; i < 16; ++i)
        notes.emplace_back (49 - 2, 1.0f, i * 0.02);   // B: the cymbal, rings long
    rig.play (0.4, notes);
    CHECK (rig.kit.getNumActiveVoices() <= Kit::maxPerSlot);
    CHECK (rig.kit.getNumActiveVoices() >= Kit::maxPerSlot - 1);
}

TEST_CASE ("DRUMS: SAMPLE plays at its own pitch at any rate, PITCH and length follow", "[drums]")
{
    const auto sine = sineSample (1000.0, 44100.0, 0.5);
    auto s = model (Model::sample);
    s.sample = sine.get();
    auto hit = Kit::renderHit (s, 1.0f, rate, 1.0);
    CHECK_THAT (frequencyHz (hit, 0.1, 0.4), Catch::Matchers::WithinAbs (1000.0, 1.0));
    CHECK_THAT (ringSeconds (hit, 20.0f), Catch::Matchers::WithinAbs (0.5, 0.006));
    s.pitch = 12.0f;
    hit = Kit::renderHit (s, 1.0f, rate, 1.0);
    CHECK_THAT (frequencyHz (hit, 0.05, 0.2), Catch::Matchers::WithinAbs (2000.0, 2.0));
    CHECK_THAT (ringSeconds (hit, 20.0f), Catch::Matchers::WithinAbs (0.25, 0.006));
    // Played as it is: 0.5 at the middle of DECAY, TONE and SNAP.
    s.pitch = 0.0f;
    CHECK_THAT (peakDb (Kit::renderHit (s, 1.0f, rate, 1.0).getReadPointer (0), 48000), Catch::Matchers::WithinAbs (-6.02, 0.05));
}

TEST_CASE ("DRUMS: SAMPLE's DECAY shortens, TONE tilts, SNAP softens or spikes", "[drums]")
{
    const auto sine = sineSample (1000.0, 48000.0, 1.0);
    auto s = model (Model::sample);
    s.sample = sine.get();
    s.decay = 0.2f;
    CHECK (ringSeconds (Kit::renderHit (s, 1.0f, rate, 1.2), 40.0f) < 0.6);
    s.decay = 0.5f;

    // TONE on noise: darker below the middle, thinner above.
    auto noise = std::make_shared<Sample>();
    noise->channels.assign (1, std::vector<float> (48000));
    juce::Random random (3);
    for (auto& x : noise->channels[0])
        x = random.nextFloat() - 0.5f;
    s.sample = noise.get();
    const auto flat = centroidHz (Kit::renderHit (s, 1.0f, rate, 0.5), 0.3);
    s.tone = 0.0f;
    CHECK (centroidHz (Kit::renderHit (s, 1.0f, rate, 0.5), 0.3) < 0.5 * flat);
    s.tone = 1.0f;
    CHECK (centroidHz (Kit::renderHit (s, 1.0f, rate, 0.5), 0.3) > 1.05 * flat);

    // SNAP: a fade-in at 0, a spike at 1.
    s.tone = 0.5f;
    s.sample = sine.get();
    s.snap = 0.0f;
    CHECK (peakDb (Kit::renderHit (s, 1.0f, rate, 0.1).getReadPointer (0), 48) < -20.0f);
    s.snap = 1.0f;
    CHECK (peakDb (Kit::renderHit (s, 1.0f, rate, 0.1).getReadPointer (0), 48) > -2.0f);
}

TEST_CASE ("DRUMS: a stereo sample keeps its sides", "[drums]")
{
    const auto sine = sineSample (500.0, 48000.0, 0.3, true);
    Rig rig;
    rig.settings.slots[0].model = Model::sample;
    rig.settings.slots[0].sample = sine.get();
    rig.play (0.4, { { 36, 1.0f, 0.0 } });
    CHECK (Rig::peak (rig.left) > 0.4f);
    CHECK (Rig::peak (rig.right) == 0.0f);
}

TEST_CASE ("DRUMS: CPU, the whole kit hit every sixteenth note", "[drums][performance]")
{
    Rig rig;
    std::vector<std::tuple<int, float, double>> notes;
    for (int step = 0; step < 64; ++step)
        for (int k = 0; k < numSlots; ++k)
            notes.emplace_back (36 + k, 1.0f, step * 0.125);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    rig.play (8.0, notes);
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 8000.0 * 100.0;
    WARN (numSlots << " drums every sixteenth note at 120 BPM: " << percent << "% of one core");
    CHECK (percent < 14.0);   // (8% for PR7-K's 12)
}

TEST_CASE ("DRUMS: at most 48 hits sound at once, the oldest cut first (as before the mixer's 192 voices)", "[drums]")
{
    // Every drum a 2 s sine, each hit three times in a quarter of a second: 69 hits wanting to ring together.
    Rig rig;
    const auto sine = sineSample (440.0, rate, 2.0);
    for (auto& s : rig.settings.slots)
    {
        s.model = Model::sample;
        s.sample = sine.get();
        s.choke = 0;     // the hats would stop each other
        s.mono = false;
    }
    // (0.33.0's chokes left out: a choke stops its cymbal, which this isn't about.)
    std::vector<std::tuple<int, float, double>> notes;
    for (int step = 0; step < 3; ++step)
        for (int k = 0; k < numSlots; ++k)
            if (! isChoke (k))
                notes.emplace_back (36 + k, 1.0f, step * 0.1);
    rig.play (0.3, notes);
    CHECK (rig.kit.getNumActiveVoices() == Kit::maxHits);
    // The ones cut were the first round's; every drum still sounds through a later hit.
    for (int k = 0; k < numSlots; ++k)
        CHECK (rig.kit.isSounding (k) == ! isChoke (k));
}

namespace
{
    // A file like a sample pack's "kick, nine ways": nine kicks 0.45 s apart, each a bit higher, 4 s in all.
    std::shared_ptr<Sample> nineKicks (std::vector<int>* starts = nullptr)
    {
        auto file = std::make_shared<Sample>();
        file->name = "Kit_Kick";
        file->sampleRate = 44100.0;
        file->channels.assign (1, std::vector<float> ((size_t) (4.0 * 44100.0), 0.0f));
        for (int k = 0; k < 9; ++k)
        {
            auto s = model (Model::kick);
            s.pitch = (float) k;
            s.decay = 0.3f;
            const auto hit = Kit::renderHit (s, 1.0f, 44100.0, 0.4);
            const auto at = (int) (0.05 * 44100.0) + (int) (k * 0.45 * 44100.0);
            if (starts != nullptr)
                starts->push_back (at);
            for (int i = 0; i < hit.getNumSamples() && at + i < file->length(); ++i)
                file->channels[0][(size_t) (at + i)] = hit.getSample (0, i);
        }
        return file;
    }
}

TEST_CASE ("DRUMS: SAMPLE finds each hit in a file of several", "[drums]")
{
    std::vector<int> starts;
    const auto file = nineKicks (&starts);
    const auto hits = sampleFiles::findHits (*file);
    REQUIRE (hits.size() == 9);
    for (size_t k = 0; k < hits.size(); ++k)
    {
        INFO ("hit " << k + 1);
        CHECK (std::abs (hits[k] - starts[k]) <= (int) (0.001 * 44100.0));   // within 1 ms of where it really starts
        CHECK (hits[k] <= starts[k] + 5);                                    // and not after its first moment
    }

    // One hit is one; silence is none.
    const auto single = sineSample (200.0, 48000.0, 0.5);
    CHECK (sampleFiles::findHits (*single).size() == 1);
    Sample quiet;
    quiet.channels.assign (1, std::vector<float> (48000, 0.0f));
    CHECK (sampleFiles::findHits (quiet).empty());
}

TEST_CASE ("DRUMS: SAMPLE plays only from START to END, fading at a cut END", "[drums]")
{
    // 500 Hz for the first half second, 2 kHz for the second.
    auto file = std::make_shared<Sample>();
    file->sampleRate = 48000.0;
    file->channels.assign (1, std::vector<float> (48000));
    for (int i = 0; i < 48000; ++i)
        file->channels[0][(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (i < 24000 ? 500.0 : 2000.0) * i / 48000.0);
    auto s = model (Model::sample);
    s.sample = file.get();

    s.start = 0.5f;   // the second half only
    auto hit = Kit::renderHit (s, 1.0f, rate, 1.0);
    CHECK_THAT (frequencyHz (hit, 0.05, 0.4), Catch::Matchers::WithinAbs (2000.0, 2.0));
    CHECK_THAT (ringSeconds (hit, 20.0f), Catch::Matchers::WithinAbs (0.5, 0.006));

    s.start = 0.0f;
    s.end = 0.5f;     // the first half only
    hit = Kit::renderHit (s, 1.0f, rate, 1.0);
    CHECK_THAT (frequencyHz (hit, 0.05, 0.4), Catch::Matchers::WithinAbs (500.0, 1.0));
    CHECK_THAT (ringSeconds (hit, 20.0f), Catch::Matchers::WithinAbs (0.5, 0.006));
    // Its last millisecond is fading out (no click), and nothing plays after it.
    CHECK (peakDb (hit.getReadPointer (0, 24000 - 48), 48) < -12.0f);
    CHECK (peakDb (hit.getReadPointer (0, 24010), 4800) <= -200.0f);   // silence (the measure's floor)

    // One kick out of nine: just that kick (the 4th), as long as it is.
    std::vector<int> starts;
    const auto kicks = nineKicks (&starts);
    const auto hits = sampleFiles::findHits (*kicks);
    REQUIRE (hits.size() == 9);
    s.sample = kicks.get();
    s.start = (float) hits[3] / (float) kicks->length();
    s.end = (float) (hits[4] - 44) / (float) kicks->length();
    hit = Kit::renderHit (s, 1.0f, rate, 1.0);
    CHECK (ringSeconds (hit, 60.0f) < 0.46);
    CHECK (frequencyHz (hit, 0.15, 0.3) > 52.0 * std::exp2 (2.5 / 12.0));   // the 4th kick is 3 semitones up
    CHECK (frequencyHz (hit, 0.15, 0.3) < 52.0 * std::exp2 (3.5 / 12.0));
}

//==============================================================================
// THE BIGGER KIT (PR7-D 0.2.0): the four new drum synths, each measured for what makes it that drum.
namespace
{
    // A frequency's level in [from, from + length) seconds (Goertzel over whole cycles), as a share of full scale.
    double toneLevel (const juce::AudioBuffer<float>& b, double hz, double from, double length)
    {
        const auto* x = b.getReadPointer (0);
        const auto start = (int) (from * rate);
        const auto n = (int) (std::floor (length * hz) / hz * rate);
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / rate);
        double s1 = 0.0, s2 = 0.0;
        for (int i = start; i < start + n && i < b.getNumSamples(); ++i)
        {
            const auto s0 = x[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return 2.0 * std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / juce::jmax (1, n);
    }

    // Slot k as a new kit has it (its model, pitch and shaping).
    SlotSettings kitSlot (int k)
    {
        SlotSettings s;
        const auto& d = synth::drums::slotDefaults[(size_t) k];
        s.model = d.model;
        s.pitch = d.pitch;
        s.decay = d.decay;
        s.tone = d.tone;
        s.snap = d.snap;
        s.choke = d.choke;
        return s;
    }

    // The loudest sample in each `ms`-long window, from the start.
    std::vector<float> envelopeOf (const juce::AudioBuffer<float>& b, double ms)
    {
        const auto window = juce::jmax (1, (int) (ms * 0.001 * rate));
        std::vector<float> peaks;
        for (int i = 0; i + window <= b.getNumSamples(); i += window)
            peaks.push_back (b.getMagnitude (0, i, window));
        return peaks;
    }
}

TEST_CASE ("THE BIGGER KIT: the ride rings long and bright, with its stick's ping (SNAP) around 2.6 kHz", "[drums][kit20]")
{
    const auto ride = Kit::renderHit (kitSlot (12), 1.0f, rate, 4.0);
    const auto crash = Kit::renderHit (kitSlot (11), 1.0f, rate, 4.0);
    CHECK (ringSeconds (ride, 40.0f) > 1.0);   // a ride sustains
    // The ping: at 2650 Hz in the first 50 ms, far louder with SNAP up than down.
    auto pinged = kitSlot (12), soft = kitSlot (12);
    pinged.snap = 1.0f;
    soft.snap = 0.0f;
    const auto hard = toneLevel (Kit::renderHit (pinged, 1.0f, rate, 0.2), 2650.0, 0.0, 0.05);
    const auto quiet = toneLevel (Kit::renderHit (soft, 1.0f, rate, 0.2), 2650.0, 0.0, 0.05);
    WARN ("the ride's ping at 2650 Hz: SNAP 100% " << juce::Decibels::gainToDecibels (hard) << " dB, 0% "
                                                     << juce::Decibels::gainToDecibels (quiet) << " dB");
    CHECK (hard > 3.0 * quiet);
    // Its wash sits lower than the crash's (a ride is darker and less splashy).
    CHECK (centroidHz (ride) < centroidHz (crash));
}

TEST_CASE ("THE BIGGER KIT: the ride bell sounds its partials, 1.1 kHz and inharmonic ones above, TONE raising the upper ones", "[drums][kit20]")
{
    const auto bell = Kit::renderHit (kitSlot (13), 1.0f, rate, 1.0);
    const auto fundamental = toneLevel (bell, 1100.0, 0.05, 0.2);
    const auto between = toneLevel (bell, 1300.0, 0.05, 0.2);
    CHECK (fundamental > 10.0 * between);                                    // a pitched ring, not noise
    CHECK (toneLevel (bell, 1100.0 * 1.51, 0.05, 0.2) > 3.0 * between);   // and its second partial
    auto dark = kitSlot (13), bright = kitSlot (13);
    dark.tone = 0.0f;
    bright.tone = 1.0f;
    const auto upper = [] (const SlotSettings& s)
    {
        const auto hit = Kit::renderHit (s, 1.0f, rate, 0.5);
        return toneLevel (hit, 1100.0 * 2.24, 0.01, 0.1) / toneLevel (hit, 1100.0, 0.01, 0.1);
    };
    CHECK (upper (bright) > 2.0 * upper (dark));
}

TEST_CASE ("THE BIGGER KIT: the tambourine's jingles strike three times in its first 15 ms", "[drums][kit20]")
{
    const auto hit = Kit::renderHit (kitSlot (18), 1.0f, rate, 0.5);
    const auto env = envelopeOf (hit, 1.0);   // the loudest sample in each millisecond
    // A strike at 0, 6 and 14 ms: each a peak above the millisecond before it.
    for (const auto ms : { 6, 14 })
    {
        INFO (ms << " ms");
        CHECK (env[(size_t) ms] > 1.3f * env[(size_t) ms - 1]);
    }
    CHECK (centroidHz (hit) > 6000.0);   // bright metal
}

TEST_CASE ("THE BIGGER KIT: the shaker swells in (SNAP: faster) and is noise high up", "[drums][kit20]")
{
    auto soft = kitSlot (19), sharp = kitSlot (19);
    soft.snap = 0.0f;
    sharp.snap = 1.0f;
    const auto peakAt = [] (const SlotSettings& s)
    {
        const auto env = envelopeOf (Kit::renderHit (s, 1.0f, rate, 0.5), 1.0);
        return (int) std::distance (env.begin(), std::max_element (env.begin(), env.end()));
    };
    WARN ("the shaker peaks after " << peakAt (soft) << " ms (SNAP 0) and " << peakAt (sharp) << " ms (SNAP 100%)");
    CHECK (peakAt (soft) > 15);           // a swell, not a strike
    CHECK (peakAt (sharp) < peakAt (soft));
    CHECK (peakAt (sharp) >= 2);
    CHECK (centroidHz (Kit::renderHit (kitSlot (19), 1.0f, rate, 0.5)) > 3000.0);
}

TEST_CASE ("THE BIGGER KIT: the half-open hat chokes with the hats; the new cymbals each ring", "[drums][kit20]")
{
    CHECK (synth::drums::slotDefaults[17].choke == synth::drums::slotDefaults[6].choke);   // HH in the hats' group
    for (const auto k : { 14, 15, 16 })   // crash 2, china, splash
    {
        INFO (synth::drums::slotDefaults[(size_t) k].key);
        const auto hit = Kit::renderHit (kitSlot (k), 1.0f, rate, 6.0);
        CHECK (ringSeconds (hit, 40.0f) > 0.3);
    }
    // The splash is the shortest of the three, crash 2 the longest; the china the darkest.
    const auto seconds = [] (int k) { return ringSeconds (Kit::renderHit (kitSlot (k), 1.0f, rate, 6.0), 40.0f); };
    CHECK (seconds (16) < seconds (15));
    CHECK (seconds (15) < seconds (14));
    CHECK (centroidHz (Kit::renderHit (kitSlot (15), 1.0f, rate, 1.0)) < centroidHz (Kit::renderHit (kitSlot (14), 1.0f, rate, 1.0)));
}
