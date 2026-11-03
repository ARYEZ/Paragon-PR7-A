// RIFF MATCH (PR7-D 0.32.0): a riff's hits and tempo found in audio and MIDI, and grooves scored by how well they'd
// play with it. Measured on generated riffs whose hits and tempo are known.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grooves/Riff.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
    using grooves::Lane;
    using grooves::Rhythm;

    struct Note { double beat; float level; double hz = 110.0; };

    // A riff as audio: each note a plucked string (a bright tone falling away), optionally driven like a distorted
    // guitar, with a little noise under it.
    juce::AudioBuffer<float> render (const std::vector<Note>& notes, double bpm, double seconds, bool distorted, double rate = 48000.0,
                                     double leadIn = 0.0)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * rate));
        b.clear();
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        // One string: a note stops the one before it (as picking a string again does), over 3 ms.
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const auto& n = notes[k];
            const auto first = (int) ((leadIn + n.beat * 60.0 / bpm) * rate);
            const auto next = k + 1 < notes.size() ? (int) ((leadIn + notes[k + 1].beat * 60.0 / bpm) * rate) - first : (int) (0.6 * rate);
            for (int i = 0; first + i < b.getNumSamples() && i < juce::jmin ((int) (0.6 * rate), next + (int) (0.003 * rate)); ++i)
            {
                const auto t = i / rate;
                const auto cut = i < next ? 1.0 : 1.0 - (i - next) / (0.003 * rate);
                auto s = 0.0;
                for (int h = 1; h <= 8; ++h)
                    s += std::sin (juce::MathConstants<double>::twoPi * n.hz * h * t) / h * std::exp (-t * (3.0 + 2.0 * h));
                s *= n.level * std::exp (-t * 4.0) * juce::jmin (1.0, t / 0.002) * cut;
                if (distorted)
                    s = std::tanh (6.0 * s) * 0.5;
                b.addSample (0, first + i, (float) s);
                b.addSample (1, first + i, (float) s);
            }
        }
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                b.addSample (c, i, 0.0005f * noise (rng));
        return b;
    }

    // The same riff's notes as kick-like accents (1.0) and weaker notes (0.45): a typical metal / rock riff, 2 bars of 4/4.
    std::vector<Note> rockRiff()
    {
        std::vector<Note> n;
        for (const auto b : { 0.0, 0.75, 1.5, 2.0, 3.5, 4.0, 4.75, 5.5, 6.0, 7.0, 7.5 })
            n.push_back ({ b, 1.0f, 82.4 });
        for (const auto b : { 0.25, 0.5, 2.5, 3.0, 4.25, 4.5, 6.5 })
            n.push_back ({ b, 0.45f, 82.4 });
        std::sort (n.begin(), n.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
        return n;
    }

    Rhythm groove (std::vector<double> kick, std::vector<double> snare = { 1.0, 3.0, 5.0, 7.0 }, double length = 8.0, double perBar = 4.0)
    {
        Rhythm r;
        r.lengthBeats = length;
        r.beatsPerBar = perBar;
        for (const auto b : kick) r.add ((int) Lane::kick, b);
        for (const auto b : snare) r.add ((int) Lane::snare, b);
        for (auto b = 0.0; b < length - 1e-9; b += 0.5) r.add ((int) Lane::hats, b);
        return r;
    }

    // How many of `truth` (seconds) have a found onset within `ms`, and how many found have none.
    std::pair<int, int> agreement (const std::vector<grooves::Onset>& found, const std::vector<double>& truth, double ms, double* meanError = nullptr)
    {
        auto hit = 0;
        auto errorSum = 0.0;
        std::vector<bool> used (found.size());
        for (const auto t : truth)
        {
            auto bestK = -1;
            auto bestD = 1e9;
            for (size_t k = 0; k < found.size(); ++k)
                if (! used[k] && std::abs (found[k].seconds - t) < bestD)
                {
                    bestD = std::abs (found[k].seconds - t);
                    bestK = (int) k;
                }
            if (bestK >= 0 && bestD <= ms / 1000.0)
            {
                used[(size_t) bestK] = true;
                ++hit;
                errorSum += found[(size_t) bestK].seconds - t;
            }
        }
        if (meanError != nullptr)
            *meanError = hit > 0 ? errorSum / hit * 1000.0 : 0.0;
        return { hit, (int) std::count (used.begin(), used.end(), false) };
    }
}

TEST_CASE ("RIFF MATCH: an audio riff's hits are found where they start, the accents stronger", "[riff]")
{
    for (const auto distorted : { false, true })
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto notes = rockRiff();
            const auto audio = render (notes, 120.0, 4.6, distorted, rate, 0.3);
            const auto found = grooves::findOnsets (audio, rate);
            std::vector<double> truth;
            for (const auto& n : notes)
                truth.push_back (0.3 + n.beat * 0.5);
            double meanError = 0.0;
            const auto [hit, extra] = agreement (found, truth, 15.0, &meanError);
            INFO ((distorted ? "distorted" : "clean") << " at " << rate << ": " << hit << " of " << truth.size() << " found, "
                  << extra << " extra, mean error " << meanError << " ms");
            CHECK (hit == (int) truth.size());
            CHECK (extra == 0);
            CHECK (std::abs (meanError) < 6.0);
            // The accents read stronger than the weak notes (clean: by level; distorted: still, though less).
            auto accents = 0.0, weak = 0.0;
            auto na = 0, nw = 0;
            for (const auto& o : found)
                for (const auto& n : notes)
                    if (std::abs (o.seconds - (0.3 + n.beat * 0.5)) < 0.015)
                    {
                        (n.level > 0.5f ? accents : weak) += o.strength;
                        (n.level > 0.5f ? na : nw) += 1;
                    }
            INFO ("accents " << accents / na << ", weak " << weak / nw);
            CHECK (accents / na > weak / nw);
        }
}

TEST_CASE ("RIFF MATCH: a riff's tempo, from what's heard, its length, its name or the song", "[riff]")
{
    // Sixteenth chugs with accents, 4 bars: what's heard alone (not cut to whole bars: a tail of silence).
    for (const auto bpm : { 85.0, 97.0, 120.0, 140.0, 172.0 })
    {
        std::vector<Note> notes;
        for (auto b = 0.0; b < 16.0 - 1e-9; b += 0.25)
        {
            const auto i = (int) std::lround (b * 4.0);
            const auto accent = i % 16 == 0 || i % 16 == 3 || i % 16 == 6 || i % 16 == 10 || i % 16 == 12;
            if (accent || i % 2 == 0)
                notes.push_back ({ b, accent ? 1.0f : 0.4f, 82.4 });
        }
        const auto seconds = 16.0 * 60.0 / bpm;
        const auto audio = render (notes, bpm, seconds + 0.77, true);
        const auto found = grooves::findOnsets (audio, 48000.0);
        const auto heard = grooves::findTempo (found, audio.getNumSamples() / 48000.0, 4.0);
        INFO (bpm << " BPM heard as " << heard);
        // Steady sixteenths at 172 are steady eighths at 86: no one can tell them apart from the sound alone (the
        // song's tempo, or the panel's x2 / /2, settles it). Either is right; anything else is wrong.
        const auto octave = [bpm] (double t, double within)
        {
            return std::abs (t / bpm - 1.0) < within || std::abs (t / (2.0 * bpm) - 1.0) < within || std::abs (t / (0.5 * bpm) - 1.0) < within;
        };
        CHECK (octave (heard, 0.02));
        // Cut to exactly its 4 bars: the length gives it to a hundredth.
        const auto exact = render (notes, bpm, seconds, true);
        const auto byLength = grooves::findTempo (grooves::findOnsets (exact, 48000.0), seconds, 4.0);
        CHECK (octave (byLength, 0.0005));
    }

    // From the song: a riff heard at the song's tempo (or half / twice it) takes it exactly.
    const auto notes = rockRiff();
    auto r = grooves::riffFromAudio (render (notes, 118.0, 4.6, true), 48000.0, 118.0, 4, 4, "my riff");
    CHECK (r.tempoFrom == grooves::Riff::TempoFrom::song);
    CHECK_THAT (r.bpm, WithinAbs (118.0, 1e-9));
    // Its name says otherwise: the name wins.
    r = grooves::riffFromAudio (render (notes, 132.0, 4.6, true), 48000.0, 90.0, 4, 4, "Riff 132 BPM");
    CHECK (r.tempoFrom == grooves::Riff::TempoFrom::name);
    CHECK_THAT (r.bpm, WithinAbs (132.0, 1e-9));
    // The song at another tempo: the song's is used (a riff from the song is at its tempo), what's heard offered.
    r = grooves::riffFromAudio (render (notes, 150.0, 3.5, true), 48000.0, 100.0, 4, 4, "riff");
    CHECK (r.tempoFrom == grooves::Riff::TempoFrom::song);
    CHECK_THAT (r.bpm, WithinAbs (100.0, 1e-9));
    CHECK (std::abs (r.heardBpm / 150.0 - 1.0) < 0.03);
    // No song tempo: what's heard.
    r = grooves::riffFromAudio (render (notes, 150.0, 3.5, true), 48000.0, 0.0, 4, 4, "riff");
    CHECK (r.tempoFrom == grooves::Riff::TempoFrom::found);
    CHECK (std::abs (r.bpm / 150.0 - 1.0) < 0.03);
    // It starts at its first clear hit; the audio to hear is mono, its peak at -12 dBFS.
    r = grooves::riffFromAudio (render (notes, 120.0, 4.6, false, 48000.0, 0.4), 48000.0, 120.0, 4, 4, "riff");
    CHECK_THAT (r.startSeconds, WithinAbs (0.4, 0.008));
    REQUIRE (r.audio != nullptr);
    CHECK (r.audio->getNumChannels() == 1);
    CHECK_THAT (r.audio->getMagnitude (0, r.audio->getNumSamples()), WithinAbs (juce::Decibels::decibelsToGain (-12.0f), 1e-4));
}

TEST_CASE ("RIFF MATCH: a MIDI riff: its notes, chords as one hit, its tempo and time signature, a sound to hear", "[riff]")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pr7d-riff-test.mid");
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / 100.0)));
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (3, 4));
    const auto note = [&] (double beat, int n, int velocity, double length = 0.4)
    {
        track.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) velocity).withTimeStamp (beat * 480));
        track.addEvent (juce::MidiMessage::noteOff (1, n).withTimeStamp ((beat + length) * 480));
    };
    note (0.0, 40, 120); note (0.0, 47, 110); note (0.0, 52, 100);   // a chord
    note (1.0, 40, 60);
    note (1.5, 43, 110);
    note (2.0, 40, 50);
    track.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (track);
    file.deleteFile();
    {
        juce::FileOutputStream out (file);
        midi.writeTo (out);
    }
    const auto r = grooves::readRiff (file, 140.0, 4, 4);
    file.deleteFile();
    REQUIRE (r.has_value());
    CHECK (r->fromMidi);
    CHECK (r->tempoFrom == grooves::Riff::TempoFrom::file);
    CHECK_THAT (r->bpm, WithinAbs (100.0, 1e-6));
    CHECK (r->numerator == 3);
    REQUIRE (r->onsets.size() == 4);
    CHECK_THAT (r->onsets[1].seconds, WithinAbs (0.6, 1e-6));   // beat 1 at 100 BPM
    CHECK_THAT (r->onsets[0].strength, WithinAbs (1.0, 1e-6));  // the chord, loudest
    CHECK (r->onsets[3].strength < 0.5f);
    CHECK_THAT (r->startSeconds, WithinAbs (0.0, 1e-9));
    REQUIRE (r->audio != nullptr);
    CHECK (r->audio->getMagnitude (0, r->audio->getNumSamples()) > 0.2f);
    const auto q = grooves::queryOf (*r);
    CHECK_THAT (q.beatsPerBar, WithinAbs (3.0, 1e-9));
    CHECK_THAT (q.lengthBeats, WithinAbs (6.0, 1e-9));   // 2 bars of 3/4
    REQUIRE (q.hits.size() == 4);
    CHECK_THAT (q.hits[2].first, WithinAbs (1.5, 1e-9));
    CHECK_FALSE (grooves::readRiff (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nothing-here.mid"), 120.0, 4, 4).has_value());
}

TEST_CASE ("RIFF MATCH: grooves whose kick locks to the riff's accents score highest", "[riff]")
{
    grooves::RiffQuery q;
    for (const auto& n : rockRiff())
        q.hits.push_back ({ n.beat, n.level });
    const auto accents = std::vector<double> { 0.0, 0.75, 1.5, 2.0, 3.5, 4.0, 4.75, 5.5, 6.0, 7.0, 7.5 };

    // The riff's own accents on the kick: a full match.
    const auto locked = grooves::riffMatch (q, groove (accents), grooves::Feel::straight);
    CHECK_THAT (locked, WithinAbs (1.0, 1e-9));
    // Four on the floor: half its kicks on the riff, most accents missed.
    const auto floor = grooves::riffMatch (q, groove ({ 0, 1, 2, 3, 4, 5, 6, 7 }), grooves::Feel::straight);
    // A plain rock beat: on 1 and 3 (the riff has those).
    const auto rock = grooves::riffMatch (q, groove ({ 0, 2, 4, 6 }), grooves::Feel::straight);
    // Busy double kick on every sixteenth: every accent met, but most kicks where the riff is silent.
    std::vector<double> sixteenths;
    for (auto b = 0.0; b < 8.0 - 1e-9; b += 0.25) sixteenths.push_back (b);
    const auto busy = grooves::riffMatch (q, groove (sixteenths), grooves::Feel::straight);
    INFO ("locked " << locked << ", rock " << rock << ", four on the floor " << floor << ", sixteenths " << busy);
    CHECK (locked > rock + 0.3);
    CHECK (locked > floor + 0.3);   // (rock against four on the floor: about even, half the accents each)
    CHECK (locked > busy + 0.2);

    // A crash on an accent the kick misses counts as meeting it.
    Rhythm crash = groove ({ 0.0, 0.75, 1.5, 2.0, 4.0, 4.75, 5.5, 6.0, 7.0, 7.5 });   // no kick on 3.5
    const auto without = grooves::riffMatch (q, crash, grooves::Feel::straight);
    crash.add ((int) Lane::crash, 3.5);
    CHECK (grooves::riffMatch (q, crash, grooves::Feel::straight) > without);

    // A crash ridden (every eighth) isn't an accent: it doesn't meet the riff's (the kick alone counts).
    Rhythm ridden = groove ({ 0.0, 2.0, 4.0, 6.0 });
    const auto plain = grooves::riffMatch (q, ridden, grooves::Feel::straight);
    for (auto b = 0.0; b < 8.0 - 1e-9; b += 0.5) ridden.add ((int) Lane::crash, b);
    CHECK_THAT (grooves::riffMatch (q, ridden, grooves::Feel::straight), WithinAbs (plain, 1e-9));

    // Another feel or bar length: less.
    CHECK_THAT (grooves::riffMatch (q, groove (accents), grooves::Feel::swung), WithinAbs (0.8, 1e-9));
    CHECK_THAT (grooves::riffMatch (q, groove (accents, { 1.0, 3.0 }, 6.0, 3.0), grooves::Feel::straight), WithinAbs (0.75 * grooves::riffMatch (q, groove (accents, { 1.0, 3.0 }, 6.0, 3.0), grooves::Feel::straight) / 0.75, 1e-9));
    CHECK (grooves::riffMatch (q, groove ({ 0, 2, 4 }, { 1.0, 3.0, 5.0 }, 6.0, 3.0), grooves::Feel::straight) <= 0.75);
    // Nothing to match.
    CHECK (grooves::riffMatch (grooves::RiffQuery(), groove (accents), grooves::Feel::straight) == 0.0);
    CHECK (grooves::riffMatch (q, groove ({}), grooves::Feel::straight) == 0.0);
}

TEST_CASE ("RIFF MATCH: an audio riff finds the groove whose kick follows it", "[riff]")
{
    // The whole path: audio -> hits -> tempo -> the part -> scores.
    const auto audio = render (rockRiff(), 120.0, 4.6, true, 48000.0, 0.25);
    auto r = grooves::riffFromAudio (audio, 48000.0, 120.0, 4, 4, "riff");
    r.bars = 2;
    const auto q = grooves::queryOf (r);
    const auto accents = std::vector<double> { 0.0, 0.75, 1.5, 2.0, 3.5, 4.0, 4.75, 5.5, 6.0, 7.0, 7.5 };
    const auto right = grooves::riffMatch (q, groove (accents), grooves::Feel::straight);
    const auto wrong = grooves::riffMatch (q, groove ({ 0, 1, 2, 3, 4, 5, 6, 7 }), grooves::Feel::straight);
    const auto rock = grooves::riffMatch (q, groove ({ 0, 2, 4, 6 }), grooves::Feel::straight);
    INFO ("right " << right << ", rock " << rock << ", four on the floor " << wrong);
    CHECK (right > 0.9);
    CHECK (right > rock);
    CHECK (right > wrong);
}
