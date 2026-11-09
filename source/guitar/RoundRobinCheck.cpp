#include "guitar/RoundRobinCheck.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace guitar::renders::rrcheck
{
    const std::vector<Group>& groups (Check check)
    {
        static const std::vector<Group> g {
            { Articulation::sustain, 0, 0, 118, "Sustain, low A open, hard" },
            { Articulation::mute, 0, 0, 100, "Palm mute, low A open" },
            { Articulation::sustain, 3, 5, 85, "Sustain, D string 5th fret, medium" } };
        // 0.6.1: palm mutes from the thin strings down (velocity: the degree of muting, the list's 64 = the middle one).
        static const std::vector<Group> pm {
            { Articulation::mute, 6, 0, 64, "Palm mute, high E open, middle" },
            { Articulation::mute, 6, 7, 64, "Palm mute, high E 7th fret, middle" },
            { Articulation::mute, 5, 0, 64, "Palm mute, B string open, middle" },
            { Articulation::mute, 4, 5, 115, "Palm mute, G string 5th fret, least muted" },
            { Articulation::mute, 0, 0, 64, "Palm mute, low A open, middle" } };
        // 0.6.2: the order check.
        static const std::vector<Group> order {
            { Articulation::mute, 6, 0, 64, "Palm mute, high E open, in the lists' order (a picking key before each note)", 1 },
            { Articulation::mute, 6, 0, 64, "Palm mute, high E open, ALTERNATE picking set once", 2 },
            { Articulation::mute, 0, 0, 64, "Palm mute, low A open, in the lists' order", 1 } };
        return check == Check::palmMute ? pm : check == Check::order ? order : g;
    }

    double slotFor (Check check) noexcept { return check == Check::roundRobin ? slotSeconds : 1.6; }
    double holdFor (Check check) noexcept { return check == Check::roundRobin ? holdSeconds : 1.0; }

    juce::String fileNameFor (Check check)
    {
        return check == Check::palmMute ? juce::String ("PR7-I Palm Mute Check") : check == Check::order ? juce::String ("PR7-I Order Check") : fileName;
    }

    std::vector<ProbeTake> takes (Check check)
    {
        std::vector<ProbeTake> result;
        auto t = leadInSeconds + noteDelaySeconds;
        for (int g = 0; g < (int) groups (check).size(); ++g)
        {
            if (groups (check)[(size_t) g].style == 0)
            {
                for (const auto stroke : { Stroke::down, Stroke::up })
                    for (int r = 0; r < repeats; ++r)
                    {
                        result.push_back ({ g, stroke, r, t });
                        t += slotFor (check);
                    }
                continue;
            }
            // down, up, down, up (as the render lists play each note's round-robins)
            for (int r = 0; r < repeats; ++r)
                for (const auto stroke : { Stroke::down, Stroke::up })
                {
                    result.push_back ({ g, stroke, r, t });
                    t += slotFor (check);
                }
        }
        return result;
    }

    double lengthSeconds (Check check)
    {
        return leadInSeconds + slotFor (check) * (double) takes (check).size();
    }

    juce::MidiFile toMidi (Check check)
    {
        constexpr int ppq = 960;
        const auto ticks = [] (double seconds) { return seconds * bpm / 60.0 * ppq; };
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, fileNameFor (check)));
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        const auto key = [&] (int note, double at)
        {
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100).withTimeStamp (ticks (at)));
            track.addEvent (juce::MidiMessage::noteOff (1, note).withTimeStamp (ticks (at + 0.05)));
        };
        key (keys::legatoOff, 0.75);
        for (const auto& take : takes (check))
        {
            const auto& g = groups (check)[(size_t) take.group];
            const auto slot = take.noteSeconds - noteDelaySeconds;
            key (keys::forceStringLowA + g.string, slot);
            key (g.articulation == Articulation::mute ? keys::mute : keys::sustain, slot + 0.05);
            if (g.style != 2)
                key (take.stroke == Stroke::down ? keys::pickDown : keys::pickUp, slot + 0.1);
            else if (take.repeat == 0 && take.stroke == Stroke::down)
                key (110, slot + 0.1);   // D7: ALTERNATE picking, once (the first note then down, as the reference starts)
            const auto note = noteOf (g.string, g.fret);
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) g.velocity).withTimeStamp (ticks (take.noteSeconds)));
            track.addEvent (juce::MidiMessage::noteOff (1, note).withTimeStamp (ticks (take.noteSeconds + holdFor (check))));
        }
        track.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp (ticks (lengthSeconds (check))));
        track.sort();
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (track);
        return file;
    }

    namespace
    {
        // The best normalised correlation of a and b with b shifted up to `maxShift` samples either way.
        double likeness (const std::vector<float>& a, const std::vector<float>& b, int maxShift)
        {
            const auto n = (int) std::min (a.size(), b.size()) - 2 * maxShift;
            if (n <= 0)
                return 0.0;
            auto best = -1.0;
            for (int shift = -maxShift; shift <= maxShift; ++shift)
            {
                double ab = 0.0, aa = 0.0, bb = 0.0;
                for (int i = maxShift; i < maxShift + n; ++i)
                {
                    const double x = a[(size_t) i], y = b[(size_t) (i + shift)];
                    ab += x * y;
                    aa += x * x;
                    bb += y * y;
                }
                if (aa > 0.0 && bb > 0.0)
                    best = std::max (best, ab / std::sqrt (aa * bb));
            }
            return best;
        }
    }

    std::vector<int> sameAs (const std::vector<std::vector<float>>& cuts, double sampleRate)
    {
        const auto maxShift = std::max (1, (int) std::lround (0.0005 * sampleRate));   // half a millisecond
        std::vector<int> id (cuts.size(), -1);
        std::vector<size_t> firsts;   // the first take of each recording
        for (size_t i = 0; i < cuts.size(); ++i)
        {
            for (size_t k = 0; k < firsts.size() && id[i] < 0; ++k)
                if (likeness (cuts[firsts[k]], cuts[i], maxShift) >= sameAbove)
                    id[i] = (int) k;
            if (id[i] < 0)
            {
                id[i] = (int) firsts.size();
                firsts.push_back (i);
            }
        }
        return id;
    }

    juce::String Result::describe() const
    {
        juce::StringArray o;
        for (const auto r : order)
            o.add (juce::String (r));
        auto text = name + ", " + (stroke == Stroke::down ? "down" : "up") + ": " + juce::String (different) + " different"
                  + (different > 1 ? (inTurn ? ", in turn" : ", not in a fixed order") : juce::String()) + " (" + o.joinIntoString (" ") + ")";
        // 0.6.1: how alike its copies are (1.000: exact) and its closest different takes.
        if (different < (int) order.size())
            text << "; copies alike " << juce::String (copiesAlike, 4);
        if (different > 1)
            text << "; different ones at most " << juce::String (closestDifferent, 3) << " alike";
        return text;
    }

    int Report::fewest() const
    {
        auto n = repeats;
        for (const auto& r : results)
            n = std::min (n, r.different);
        return results.empty() ? 0 : n;
    }

    juce::String Report::summary() const
    {
        if (error.isNotEmpty())
            return error;
        juce::StringArray lines;
        for (const auto& r : results)
            lines.add (r.describe());
        return lines.joinIntoString ("\n");
    }

    Report analyse (const float* audio, int numSamples, double sampleRate, Check check)
    {
        Report report;
        const auto list = takes (check);
        const auto at = [sampleRate] (double s) { return (int) std::lround (s * sampleRate); };
        if ((double) numSamples / sampleRate < list.back().noteSeconds + 0.5)
        {
            report.error = "This render is shorter than the check list: was it rendered to the end?";
            return report;
        }
        // The render's offset: the first take's onset (a tenth of its peak) against where the list put it.
        auto offset = 0;
        {
            const auto from = at (list.front().noteSeconds - 0.1), to = at (list.front().noteSeconds + 0.4);
            auto peak = 0.0f;
            for (int i = std::max (0, from); i < std::min (numSamples, to); ++i)
                peak = std::max (peak, std::abs (audio[i]));
            if (peak < 1.0e-3f)
            {
                report.error = "No note where the check list's first one should be: is this its render?";
                return report;
            }
            for (int i = std::max (0, from); i < std::min (numSamples, to); ++i)
                if (std::abs (audio[i]) >= 0.1f * peak)
                {
                    offset = i - at (list.front().noteSeconds);
                    break;
                }
        }
        const auto length = at (compareSeconds);
        for (int g = 0; g < (int) groups (check).size(); ++g)
            for (const auto stroke : { Stroke::down, Stroke::up })
            {
                std::vector<std::vector<float>> cuts;
                for (const auto& take : list)
                    if (take.group == g && take.stroke == stroke)
                    {
                        const auto start = at (take.noteSeconds) + offset - at (0.005);
                        std::vector<float> cut ((size_t) length, 0.0f);
                        for (int i = 0; i < length; ++i)
                            if (start + i >= 0 && start + i < numSamples)
                                cut[(size_t) i] = audio[start + i];
                        cuts.push_back (std::move (cut));
                    }
                const auto ids = sameAs (cuts, sampleRate);
                Result r;
                r.name = groups (check)[(size_t) g].name;
                r.stroke = stroke;
                r.different = ids.empty() ? 0 : *std::max_element (ids.begin(), ids.end()) + 1;
                for (const auto id : ids)
                    r.order.push_back (id + 1);
                // In turn: every take is the one `different` before it.
                r.inTurn = true;
                for (size_t i = 0; i < ids.size(); ++i)
                    r.inTurn = r.inTurn && ids[i] == (int) (i % (size_t) std::max (1, r.different));
                // 0.6.1: how alike the copies and the closest different recordings are.
                {
                    const auto maxShift = std::max (1, (int) std::lround (0.0005 * sampleRate));
                    std::vector<size_t> firsts ((size_t) r.different, 0);
                    std::vector<bool> seen ((size_t) r.different, false);
                    for (size_t i = 0; i < ids.size(); ++i)
                        if (! seen[(size_t) ids[i]])
                        {
                            seen[(size_t) ids[i]] = true;
                            firsts[(size_t) ids[i]] = i;
                        }
                    for (size_t i = 0; i < ids.size(); ++i)
                        if (firsts[(size_t) ids[i]] != i)
                            r.copiesAlike = std::min (r.copiesAlike, likeness (cuts[firsts[(size_t) ids[i]]], cuts[i], maxShift));
                    for (size_t a = 0; a < firsts.size(); ++a)
                        for (size_t b = a + 1; b < firsts.size(); ++b)
                            r.closestDifferent = std::max (r.closestDifferent, likeness (cuts[firsts[a]], cuts[firsts[b]], maxShift));
                }
                report.results.push_back (r);
            }
        return report;
    }

    Report analyseFile (const juce::File& file, Check check)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            Report r;
            r.error = "Couldn't read " + file.getFileName() + " as audio";
            return r;
        }
        const auto frames = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> buffer ((int) juce::jlimit (1u, 2u, reader->numChannels), frames);
        reader->read (&buffer, 0, frames, 0, true, true);
        if (buffer.getNumChannels() > 1)
        {
            buffer.addFrom (0, 0, buffer, 1, 0, frames);
            buffer.applyGain (0, 0, frames, 0.5f);
        }
        return analyse (buffer.getReadPointer (0), frames, reader->sampleRate, check);
    }
}
