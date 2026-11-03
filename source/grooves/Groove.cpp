#include "Groove.h"
#include "grooves/GrooveDatabase.h"

namespace grooves
{
    namespace
    {
        using namespace synth::drums::notemap;

        PowerHand handOf (int slot) noexcept
        {
            switch (slot)
            {
                case CH: case HSH: return PowerHand::closedHat;
                case OH: case HH: case HO1: case HO3: return PowerHand::openHat;
                case RD: case RB: case RE: return PowerHand::ride;
                case CY: case CR2: case CHN: case SPL: case C1C: case C2C: case CHC: case SPC:
                case C3: case C4: case C5: case C6: case CFX: case CH2: return PowerHand::crash;
                case TMB: case SHK: return PowerHand::percussion;
                default: return PowerHand::none;   // the feet (kick, pedal hat), snares, toms, the clap
            }
        }
    }

    int drumPartOf (int slot) noexcept
    {
        using namespace synth::drums::notemap;
        switch (slot)
        {
            case BD: return (int) DrumPart::kick;
            case SD: case SD2: case RIM: return (int) DrumPart::snare;
            case CH: case OH: case HH: case HO1: case HO3: case HSH: return (int) DrumPart::hats;
            case PH: return (int) DrumPart::pedal;
            case RD: case RB: case RE: return (int) DrumPart::ride;
            case LT: case MT: case HT: case T4: case T5: return (int) DrumPart::toms;
            case CY: case CR2: case CHN: case SPL: case C1C: case C2C: case CHC: case SPC:
            case C3: case C4: case C5: case C6: case CFX: case CH2: return (int) DrumPart::crash;
            case CLP: case TMB: case SHK: return (int) DrumPart::percussion;
            default: return -1;
        }
    }

    Part partIn (const juce::String& name)
    {
        // Lower case, '-' and '_' as spaces, so "Pre-Chorus", "pre_chorus" and "Pre Chorus" read alike. Checked in this
        // order: COUNT-IN only as "count in" / "count off" (not "Country"); then FILL, before the song's sections (0.8.1:
        // "Introfill" and "Verse Fill 2" are fills, as groove packs label them); PRE-CHORUS before CHORUS.
        const auto t = " " + name.toLowerCase().replaceCharacters ("-_", "  ") + " ";
        const std::pair<const char*, Part> words[] = {
            { "count in", Part::countIn }, { "countin", Part::countIn }, { "count off", Part::countIn },
            { "fill", Part::fill },
            { "pre chorus", Part::preChorus }, { "prechorus", Part::preChorus },
            { "intro", Part::intro }, { "verse", Part::verse }, { "chorus", Part::chorus }, { "bridge", Part::bridge },
            { "ending", Part::ending }, { "outro", Part::ending }, { " end ", Part::ending },
        };
        for (const auto& [word, part] : words)
        {
            if (t.contains (word))
                return part;
            // BREAK (0.8.1): a fill too ("909 Break 2" in electronic packs), as a word of its own, a number after it or
            // not ("Break3"), so "Breakdown", "Breakbeat", "Breaks" and "Jailbreak" aren't fills.
            if (part == Part::fill)
                for (auto at = t.indexOf (" break"); at >= 0; at = t.indexOf (at + 1, " break"))
                    if (const auto after = t[at + 6]; after == ' ' || juce::CharacterFunctions::isDigit (after))
                        return Part::fill;
        }
        return Part::beat;
    }

    Part partOf (const juce::File& file)
    {
        auto f = file;
        for (int level = 0; level < 3 && f.getFullPathName().isNotEmpty(); ++level, f = f.getParentDirectory())
            if (const auto part = partIn (f.getFileNameWithoutExtension()); part != Part::beat)
                return part;
        return Part::beat;
    }

    double tempoInName (const juce::String& text)
    {
        // The number just before "BPM" (any case, a space or not).
        const auto lower = text.toLowerCase();
        for (auto at = lower.indexOf ("bpm"); at >= 0; at = lower.indexOf (at + 3, "bpm"))
        {
            auto end = at;
            while (end > 0 && lower[end - 1] == ' ')
                --end;
            auto start = end;
            while (start > 0 && juce::CharacterFunctions::isDigit (lower[start - 1]))
                --start;
            if (start < end)
                if (const auto bpm = lower.substring (start, end).getIntValue(); bpm >= 30 && bpm <= 300)
                    return (double) bpm;
        }
        return 0.0;
    }

    std::optional<Groove> read (const juce::File& file, const Map& layout)
    {
        // A groove inside a groove database (0.15.0): read from there.
        if (! file.existsAsFile())
        {
            std::shared_ptr<const db::Contents> keep;
            if (const auto* item = db::itemFor (file, keep))
            {
                juce::MemoryInputStream in (item->midi, false);
                return read (in, file, layout);
            }
            return std::nullopt;
        }
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return std::nullopt;
        return read (in, file, layout);
    }

    std::optional<Groove> read (juce::InputStream& in, const juce::File& file, const Map& layout)
    {
        juce::MidiFile midi;
        if (! midi.readFrom (in, false))
            return std::nullopt;
        const auto ticks = (double) midi.getTimeFormat();
        if (ticks <= 0.0)
            return std::nullopt;   // SMPTE time: not how grooves are made

        Groove g;
        g.file = file;
        g.name = file.getFileNameWithoutExtension();
        auto tempoFound = false, signatureFound = false;
        auto lastHit = 0.0, endOfTrack = 0.0;
        for (int t = 0; t < midi.getNumTracks(); ++t)
            for (const auto* e : *midi.getTrack (t))
            {
                const auto& m = e->message;
                const auto beat = m.getTimeStamp() / ticks;
                if (m.isEndOfTrackMetaEvent())
                    endOfTrack = juce::jmax (endOfTrack, beat);
                else if (m.isTempoMetaEvent() && ! tempoFound && m.getTempoSecondsPerQuarterNote() > 0.0)
                {
                    // To a thousandth: the file keeps microseconds a beat, so 90 BPM comes back as 89.99996.
                    g.bpm = std::round (60000.0 / m.getTempoSecondsPerQuarterNote()) / 1000.0;
                    g.bpmKnown = tempoFound = true;
                }
                else if (m.isTimeSignatureMetaEvent() && ! signatureFound)
                {
                    int n = 4, d = 4;
                    m.getTimeSignatureInfo (n, d);
                    if (n > 0 && d > 0)
                    {
                        g.numerator = n;
                        g.denominator = d;
                        signatureFound = true;
                    }
                }
                else if (m.isNoteOn() && m.getVelocity() > 0)
                {
                    g.hits.push_back ({ beat, m.getNoteNumber(), m.getFloatVelocity() });
                    lastHit = juce::jmax (lastHit, beat);
                }
            }
        if (g.hits.empty())
            return std::nullopt;
        std::stable_sort (g.hits.begin(), g.hits.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });

        // No tempo in the file: the one in its name, or its folders' (groove packs often name them "... 102 BPM").
        if (! tempoFound)
            for (auto f = file; f.getFullPathName().isNotEmpty() && f != f.getParentDirectory(); f = f.getParentDirectory())
                if (const auto bpm = tempoInName (f.getFileNameWithoutExtension()); bpm > 0.0)
                {
                    g.bpm = bpm;
                    g.bpmKnown = true;
                    break;
                }

        // Whole bars, up to the last hit's start (how long a drum note is held means nothing): a hit right on the
        // next bar line (a fill's closing crash) is the last bar's end, not a bar of its own. The track's end counts
        // only when it marks at least half a bar of rest after that (a groove ending in silence): many programs put it
        // at the last note's end, a moment after a closing crash.
        const auto bpb = g.beatsPerBar();
        g.bars = juce::jmax (1, (int) std::ceil (lastHit / bpb - 1.0e-6));
        if (endOfTrack >= g.bars * bpb + 0.5 * bpb)
            g.bars = (int) std::ceil (endOfTrack / bpb - 1.0e-6);
        g.lengthBeats = g.bars * g.beatsPerBar();

        // THE POWER HAND: the cymbal, hat or percussion played most.
        std::array<int, 6> counts {};
        auto velocity = 0.0;
        for (const auto& h : g.hits)
        {
            velocity += h.velocity;
            if (h.note >= 0 && h.note < 128)
                ++counts[(size_t) handOf (synth::drums::notemap::slotOf (layout, h.note))];
        }
        counts[0] = 0;
        if (const auto most = std::max_element (counts.begin(), counts.end()); *most > 0)
            g.hand = (PowerHand) std::distance (counts.begin(), most);

        // FEEL (our own measure): the hits off the beat, each near the straight grid (quarter and half beats) or the
        // triplet one (thirds and sixths of a beat); swung when the triplet ones are more (and at least two).
        auto straight = 0, triplet = 0;
        for (const auto& h : g.hits)
        {
            const auto f = h.beat - std::floor (h.beat);
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
        g.feel = triplet >= 2 && triplet > straight ? Feel::swung : Feel::straight;

        // INTENSITY (our own measure): how hard, on average, and how busy (8 hits a beat counts as all-out), half each.
        const auto hard = velocity / (double) g.hits.size();
        const auto busy = juce::jmin (1.0, (double) g.hits.size() / g.lengthBeats / 8.0);
        g.intensity = (float) (0.5 * hard + 0.5 * busy);
        return g;
    }

    juce::MidiFile toMidi (const Groove& g, double bpm, float velocityScale, const std::function<int (int)>& noteFor, TempoFactor factor)
    {
        constexpr int ticks = 960;
        const auto scale = 1.0 / factorOf (factor);
        auto numerator = g.numerator, denominator = g.denominator;
        if (factor == TempoFactor::twoThirds && numerator == 4 && denominator == 4)
            numerator = 6, denominator = 8;
        else if (factor == TempoFactor::threeHalves && numerator == 6 && denominator == 8)
            numerator = 4, denominator = 4;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, g.name));
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jmax (1.0, bpm))));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator));
        for (const auto& h : g.hits)
        {
            const auto note = noteFor (h.note);
            if (note < 0 || note > 127)
                continue;
            const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (h.velocity * velocityScale * 127.0f));
            const auto at = h.beat * scale * ticks;
            track.addEvent (juce::MidiMessage::noteOn (10, note, velocity).withTimeStamp (at));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (at + ticks / 16.0));   // a 1/64 note: drums are one-shots
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (juce::jmax (g.lengthBeats * scale * ticks, track.getEndTime()));
        track.addEvent (end);
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticks);
        file.addTrack (track);
        return file;
    }
}
