#include "Song.h"

namespace song
{
    double Song::snap (double beat) const noexcept
    {
        // The nearest bar line (with the time signature changes).
        if (beat <= 0.0)
            return 0.0;
        const auto lines = barLines (beat);
        auto best = 0.0;
        for (const auto line : lines)
            if (std::abs (line - beat) < std::abs (best - beat))
                best = line;
        return best;
    }

    //==============================================================================
    // BARS AND TEMPO (0.11.0)
    Meter Song::meterAt (double beat) const noexcept
    {
        Meter m { 0.0, numerator, denominator };
        for (const auto& change : meters)
            if (change.beat <= beat + 1.0e-9)
                m = change;
        return m;
    }

    std::vector<double> Song::barLines (double upTo) const
    {
        std::vector<double> lines;
        for (auto at = 0.0; lines.size() < 100000; at += beatsPerBarAt (at))
        {
            lines.push_back (at);
            if (at > upTo)
                break;
        }
        return lines;
    }

    int Song::barAt (double beat) const
    {
        const auto lines = barLines (beat);
        auto bar = 0;
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i] <= beat + 1.0e-9)
                bar = (int) i;
        return bar;
    }

    void Song::tidyMeters()
    {
        // In order; each moved to the bar line nearest it (as the ones before it make the bars); one repeating the
        // time signature before it, or two on one bar line, kept once.
        std::stable_sort (meters.begin(), meters.end(), [] (const Meter& a, const Meter& b) { return a.beat < b.beat; });
        auto changes = std::move (meters);
        meters.clear();
        for (auto m : changes)
        {
            m.beat = snap (m.beat);
            if (m.beat <= 1.0e-9)
            {
                numerator = m.numerator;
                denominator = m.denominator;
                continue;
            }
            if (! meters.empty() && std::abs (meters.back().beat - m.beat) < 1.0e-9)
                meters.pop_back();
            const auto before = meterAt (m.beat - 1.0e-6);
            if (before.numerator != m.numerator || before.denominator != m.denominator)
                meters.push_back (m);
        }
    }

    void Song::setMeter (double beat, int num, int den)
    {
        num = juce::jlimit (1, 32, num);
        den = juce::jlimit (1, 32, den);
        const auto at = snap (beat);
        std::erase_if (meters, [at] (const Meter& m) { return std::abs (m.beat - at) < 1.0e-9; });
        meters.push_back ({ at, num, den });
        tidyMeters();
    }

    void Song::removeMeter (double beat)
    {
        std::erase_if (meters, [beat] (const Meter& m) { return std::abs (m.beat - beat) < 1.0e-6; });
        tidyMeters();
    }

    double Song::bpmAt (double beat, double daw) const noexcept
    {
        if (tempos.empty())
            return daw;
        if (beat <= tempos.front().beat)
            return tempos.front().bpm;
        for (size_t i = 0; i + 1 < tempos.size(); ++i)
            if (beat < tempos[i + 1].beat)
            {
                const auto& a = tempos[i];
                const auto& b = tempos[i + 1];
                return a.ramp ? a.bpm + (b.bpm - a.bpm) * (beat - a.beat) / juce::jmax (1.0e-9, b.beat - a.beat) : a.bpm;
            }
        return tempos.back().bpm;
    }

    namespace
    {
        // Seconds over `beats` from a tempo `a` gliding by `k` BPM a beat (k = 0: steady): the integral of 60 / bpm.
        double secondsOver (double a, double k, double beats) noexcept
        {
            return std::abs (k) < 1.0e-12 ? beats * 60.0 / a : 60.0 / k * std::log ((a + k * beats) / a);
        }
        // And back: beats in `seconds`.
        double beatsIn (double a, double k, double seconds) noexcept
        {
            return std::abs (k) < 1.0e-12 ? seconds * a / 60.0 : a * (std::exp (k * seconds / 60.0) - 1.0) / k;
        }
    }

    double Song::secondsAt (double beat, double daw) const noexcept
    {
        if (tempos.empty())
            return beat * 60.0 / juce::jmax (1.0, daw);
        // Before the first change: at its tempo. Then piece by piece.
        auto seconds = 0.0;
        auto at = 0.0;
        if (beat <= tempos.front().beat)
            return beat * 60.0 / tempos.front().bpm;
        seconds = tempos.front().beat * 60.0 / tempos.front().bpm;
        at = tempos.front().beat;
        for (size_t i = 0; i < tempos.size(); ++i)
        {
            const auto& a = tempos[i];
            const auto end = i + 1 < tempos.size() ? juce::jmin (beat, tempos[i + 1].beat) : beat;
            const auto k = a.ramp && i + 1 < tempos.size() ? (tempos[i + 1].bpm - a.bpm) / juce::jmax (1.0e-9, tempos[i + 1].beat - a.beat) : 0.0;
            seconds += secondsOver (a.bpm, k, end - at);
            at = end;
            if (at >= beat - 1.0e-12)
                break;
        }
        return seconds;
    }

    double Song::beatAtSeconds (double seconds, double daw) const noexcept
    {
        if (tempos.empty())
            return seconds * juce::jmax (1.0, daw) / 60.0;
        const auto firstSeconds = tempos.front().beat * 60.0 / tempos.front().bpm;
        if (seconds <= firstSeconds)
            return seconds * tempos.front().bpm / 60.0;
        auto left = seconds - firstSeconds;
        for (size_t i = 0; i < tempos.size(); ++i)
        {
            const auto& a = tempos[i];
            const auto last = i + 1 >= tempos.size();
            const auto k = a.ramp && ! last ? (tempos[i + 1].bpm - a.bpm) / juce::jmax (1.0e-9, tempos[i + 1].beat - a.beat) : 0.0;
            if (last)
                return a.beat + beatsIn (a.bpm, 0.0, left);
            const auto piece = secondsOver (a.bpm, k, tempos[i + 1].beat - a.beat);
            if (left < piece)
                return a.beat + beatsIn (a.bpm, k, left);
            left -= piece;
        }
        return 0.0;
    }

    int Song::addTempo (double beat, double bpm)
    {
        beat = juce::jmax (0.0, beat);
        bpm = juce::jlimit (20.0, 400.0, bpm);
        for (int i = 0; i < (int) tempos.size(); ++i)
            if (std::abs (tempos[(size_t) i].beat - beat) < 1.0e-9)
            {
                tempos[(size_t) i].bpm = bpm;
                return i;
            }
        const auto at = std::upper_bound (tempos.begin(), tempos.end(), beat, [] (double b, const TempoPoint& p) { return b < p.beat; });
        // (Inserted first, then counted from the start: one statement doing both would let a compiler take begin() before
        // the insert moves the list, and count from where it was: the 0.11.0 macOS crash.)
        const auto inserted = tempos.insert (at, { beat, bpm, false });
        return (int) std::distance (tempos.begin(), inserted);
    }

    void Song::moveTempo (int index, double beat, double bpm)
    {
        if (index < 0 || index >= (int) tempos.size())
            return;
        const auto low = index > 0 ? tempos[(size_t) index - 1].beat + 1.0e-6 : 0.0;
        const auto high = index + 1 < (int) tempos.size() ? tempos[(size_t) index + 1].beat - 1.0e-6 : 1.0e9;
        tempos[(size_t) index].beat = juce::jlimit (low, juce::jmax (low, high), beat);
        tempos[(size_t) index].bpm = juce::jlimit (20.0, 400.0, bpm);
    }

    void Song::removeTempo (int index)
    {
        if (index >= 0 && index < (int) tempos.size())
            tempos.erase (tempos.begin() + index);
    }

    int Song::blockAt (double beat) const noexcept
    {
        for (int i = 0; i < (int) blocks.size(); ++i)
            if (beat >= blocks[(size_t) i].start && beat < blocks[(size_t) i].end())
                return i;
        return -1;
    }

    void Song::settle (int keepFirst)
    {
        // In time order (the one just placed first among equals), then each block starting no earlier than the one
        // before it ends.
        std::vector<std::pair<Block, bool>> ordered;
        for (int i = 0; i < (int) blocks.size(); ++i)
            ordered.emplace_back (std::move (blocks[(size_t) i]), i == keepFirst);
        std::stable_sort (ordered.begin(), ordered.end(), [] (const auto& a, const auto& b)
        {
            return a.first.start != b.first.start ? a.first.start < b.first.start : (a.second && ! b.second);
        });
        blocks.clear();
        for (auto& [b, placed] : ordered)
        {
            if (! blocks.empty() && b.start < blocks.back().end() - 1.0e-9)
                b.start = blocks.back().end();
            blocks.push_back (std::move (b));
        }
    }

    int Song::place (Block b, double atBeat)
    {
        auto at = snap (atBeat);
        // Inside a block: before or after it, whichever edge is nearer the drop.
        if (const auto inside = blockAt (atBeat); inside >= 0)
        {
            const auto& there = blocks[(size_t) inside];
            at = atBeat - there.start < there.length * 0.5 ? there.start : there.end();
        }
        b.start = at;
        b.length = juce::jmax (beatsPerBarAt (at) * 0.25, b.length);
        blocks.push_back (std::move (b));
        // The new one goes in front of any block that starts where it does, and those move along.
        settle ((int) blocks.size() - 1);
        for (int i = 0; i < (int) blocks.size(); ++i)
            if (juce::exactlyEqual (blocks[(size_t) i].start, at))
                return i;
        return (int) blocks.size() - 1;
    }

    int Song::move (int index, double toBeat)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto b = blocks[(size_t) index];
        blocks.erase (blocks.begin() + index);
        return place (std::move (b), toBeat);
    }

    void Song::resize (int index, double newLength)
    {
        if (index < 0 || index >= (int) blocks.size())
            return;
        // Its end on the bar line nearest the new end (at least the first one after its start).
        auto& b = blocks[(size_t) index];
        const auto lines = barLines (b.start + juce::jmax (0.0, newLength) + 64.0);
        auto end = -1.0;
        for (const auto line : lines)
            if (line > b.start + 1.0e-9 && (end < 0.0 || std::abs (line - (b.start + newLength)) < std::abs (end - (b.start + newLength))))
                end = line;
        b.length = end - b.start;
        settle (index);
    }

    int Song::duplicate (int index)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto copy = blocks[(size_t) index];
        copy.start = blocks[(size_t) index].end();
        blocks.insert (blocks.begin() + index + 1, copy);
        settle (index + 1);
        return index + 1;
    }

    void Song::remove (int index)
    {
        if (index >= 0 && index < (int) blocks.size())
            blocks.erase (blocks.begin() + index);
    }

    std::vector<Hit> Song::events() const
    {
        std::vector<Hit> out;
        for (const auto& b : blocks)
        {
            if (b.muted || b.loopLength <= 0.0)
                continue;
            for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
                for (const auto& h : b.hits)
                    if (repeat + h.beat < b.length - 1.0e-9)
                        out.push_back ({ b.start + repeat + h.beat, h.slot, h.velocity });
        }
        std::stable_sort (out.begin(), out.end(), [] (const Hit& a, const Hit& c) { return a.beat < c.beat; });
        return out;
    }

    juce::MidiFile Song::toMidi (double bpm, const std::function<int (int)>& noteFor) const
    {
        constexpr int ticks = 960;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, "Song"));
        // The tempo (its changes: a glide as steps a 16th apart, each at the tempo half way through it) and the time
        // signatures.
        const auto tempoAt = [] (double b) { return juce::roundToInt (60.0e6 / juce::jlimit (1.0, 1000.0, b)); };
        if (tempos.empty())
            track.addEvent (juce::MidiMessage::tempoMetaEvent (tempoAt (bpm)));
        for (size_t i = 0; i < tempos.size(); ++i)
        {
            const auto& t = tempos[i];
            const auto from = i == 0 ? 0.0 : t.beat;   // the first change's tempo from the start
            if (t.ramp && i + 1 < tempos.size())
                for (auto b = t.beat; b < tempos[i + 1].beat - 1.0e-9; b += 0.25)
                    track.addEvent (juce::MidiMessage::tempoMetaEvent (tempoAt (bpmAt (juce::jmin (b + 0.125, tempos[i + 1].beat), bpm)))
                                        .withTimeStamp ((b <= t.beat ? from : b) * ticks));
            else
                track.addEvent (juce::MidiMessage::tempoMetaEvent (tempoAt (t.bpm)).withTimeStamp (from * ticks));
        }
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator));
        for (const auto& m : meters)
            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (m.numerator, m.denominator).withTimeStamp (m.beat * ticks));
        for (const auto& h : events())
        {
            const auto note = noteFor (h.slot);
            if (note < 0 || note > 127)
                continue;
            const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (h.velocity * 127.0f));
            track.addEvent (juce::MidiMessage::noteOn (10, note, velocity).withTimeStamp (h.beat * ticks));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (h.beat * ticks + ticks / 16.0));
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (juce::jmax (endBeat() * ticks, track.getEndTime()));
        track.addEvent (end);
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticks);
        file.addTrack (track);
        return file;
    }

    std::unique_ptr<juce::XmlElement> Song::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Song");
        xml->setAttribute ("num", numerator);
        xml->setAttribute ("den", denominator);
        // 0.11.0: "beat:bpm:ramp" and "beat:numerator:denominator".
        juce::StringArray t, m;
        for (const auto& p : tempos)
            t.add (juce::String (p.beat, 6) + ":" + juce::String (p.bpm, 4) + ":" + (p.ramp ? "1" : "0"));
        for (const auto& x : meters)
            m.add (juce::String (x.beat, 6) + ":" + juce::String (x.numerator) + ":" + juce::String (x.denominator));
        if (! t.isEmpty())
            xml->setAttribute ("tempos", t.joinIntoString (" "));
        if (! m.isEmpty())
            xml->setAttribute ("meters", m.joinIntoString (" "));
        if (! followDaw)
            xml->setAttribute ("followDaw", false);   // (0.12.0; on when it's not there)
        if (loopEnd > loopStart)   // (0.44.0)
        {
            xml->setAttribute ("loopOn", loopOn);
            xml->setAttribute ("loopStart", loopStart);
            xml->setAttribute ("loopEnd", loopEnd);
        }
        if (click != Click::recording)
            xml->setAttribute ("click", (int) click);
        for (const auto& b : blocks)
        {
            auto* e = xml->createNewChildElement ("Block");
            e->setAttribute ("name", b.name);
            e->setAttribute ("source", b.source.getFullPathName());
            e->setAttribute ("part", (int) b.part);
            e->setAttribute ("start", b.start);
            e->setAttribute ("length", b.length);
            e->setAttribute ("loop", b.loopLength);
            e->setAttribute ("muted", b.muted);
            juce::StringArray hits;   // "beat:slot:velocity"
            for (const auto& h : b.hits)
                hits.add (juce::String (h.beat, 6) + ":" + juce::String (h.slot) + ":" + juce::String (h.velocity, 4));
            e->setAttribute ("hits", hits.joinIntoString (" "));
        }
        return xml;
    }

    Song Song::fromXml (const juce::XmlElement& xml)
    {
        Song s;
        s.numerator = juce::jlimit (1, 32, xml.getIntAttribute ("num", 4));
        s.denominator = juce::jlimit (1, 32, xml.getIntAttribute ("den", 4));
        for (const auto& token : juce::StringArray::fromTokens (xml.getStringAttribute ("tempos"), " ", {}))
            if (const auto f = juce::StringArray::fromTokens (token, ":", {}); f.size() == 3)
                s.tempos[(size_t) s.addTempo (f[0].getDoubleValue(), f[1].getDoubleValue())].ramp = f[2] == "1";
        for (const auto& token : juce::StringArray::fromTokens (xml.getStringAttribute ("meters"), " ", {}))
            if (const auto f = juce::StringArray::fromTokens (token, ":", {}); f.size() == 3)
                s.meters.push_back ({ f[0].getDoubleValue(), f[1].getIntValue(), f[2].getIntValue() });
        s.tidyMeters();
        s.followDaw = xml.getBoolAttribute ("followDaw", true);
        s.loopOn = xml.getBoolAttribute ("loopOn", false);
        s.loopStart = juce::jmax (0.0, xml.getDoubleAttribute ("loopStart", 0.0));
        s.loopEnd = juce::jmax (s.loopStart, xml.getDoubleAttribute ("loopEnd", 0.0));
        s.click = (Click) juce::jlimit (0, 2, xml.getIntAttribute ("click", (int) Click::recording));
        for (const auto* e : xml.getChildWithTagNameIterator ("Block"))
        {
            Block b;
            b.name = e->getStringAttribute ("name");
            if (const auto path = e->getStringAttribute ("source"); juce::File::isAbsolutePath (path))
                b.source = juce::File (path);
            b.part = (grooves::Part) juce::jlimit (0, grooves::partNames.size() - 1, e->getIntAttribute ("part"));
            b.start = juce::jmax (0.0, e->getDoubleAttribute ("start"));
            b.length = juce::jmax (0.25, e->getDoubleAttribute ("length", 4.0));
            b.loopLength = juce::jmax (0.25, e->getDoubleAttribute ("loop", 4.0));
            b.muted = e->getBoolAttribute ("muted");
            for (const auto& token : juce::StringArray::fromTokens (e->getStringAttribute ("hits"), " ", {}))
            {
                const auto f = juce::StringArray::fromTokens (token, ":", {});
                if (f.size() == 3)
                    b.hits.push_back ({ juce::jmax (0.0, f[0].getDoubleValue()), juce::jlimit (0, synth::drums::numSlots - 1, f[1].getIntValue()),
                                        juce::jlimit (0.0f, 1.0f, f[2].getFloatValue()) });
            }
            s.blocks.push_back (std::move (b));
        }
        s.settle();
        return s;
    }

    Block blockFrom (const grooves::Groove& g, const synth::drums::notemap::Map& layout, grooves::TempoFactor factor, float velocityScale)
    {
        Block b;
        b.name = g.name;
        b.source = g.file;
        b.part = grooves::partOf (g.file);
        const auto scale = 1.0 / grooves::factorOf (factor);
        b.loopLength = juce::jmax (0.25, g.lengthBeats * scale);
        b.length = b.loopLength;
        for (const auto& h : g.hits)
            if (const auto slot = synth::drums::notemap::slotOf (layout, h.note); slot >= 0 && h.beat * scale < b.loopLength)
                b.hits.push_back ({ h.beat * scale, slot, juce::jlimit (0.0f, 1.0f, h.velocity * velocityScale) });
        return b;
    }

    void changeTempo (Block& b, grooves::TempoFactor factor)
    {
        const auto scale = 1.0 / grooves::factorOf (factor);
        b.loopLength *= scale;
        b.length *= scale;
        for (auto& h : b.hits)
            h.beat *= scale;
    }

    //==============================================================================
    // EDITING INSIDE A BLOCK (0.9.0)
    namespace
    {
        bool has (SlotMask slots, int slot) noexcept { return (slots & slotBit (slot)) != 0; }

        // In time order; on one drum and beat (within a hair) only the loudest kept.
        void tidy (std::vector<Hit>& hits)
        {
            std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b)
            {
                return a.beat != b.beat ? a.beat < b.beat : a.slot != b.slot ? a.slot < b.slot : a.velocity > b.velocity;
            });
            std::vector<Hit> kept;
            for (const auto& h : hits)
            {
                const auto same = std::find_if (kept.rbegin(), kept.rend(), [&h] (const Hit& k)
                {
                    return k.slot == h.slot && std::abs (k.beat - h.beat) < 1.0e-6;
                });
                if (same == kept.rend())
                    kept.push_back (h);
                else
                    same->velocity = std::max (same->velocity, h.velocity);
            }
            std::stable_sort (kept.begin(), kept.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
            hits = std::move (kept);
        }
    }

    SlotMask slotsOf (unsigned parts) noexcept
    {
        auto slots = 0u;
        for (int k = 0; k < synth::drums::numSlots; ++k)
            if (grooves::inParts (parts, k))
                slots |= slotBit (k);
        return slots;
    }

    SlotMask slotsIn (const Block& b) noexcept
    {
        auto slots = 0u;
        for (const auto& h : b.hits)
            if (h.slot >= 0 && h.slot < synth::drums::numSlots)
                slots |= slotBit (h.slot);
        return slots;
    }

    std::vector<Hit> unrolled (const Block& b)
    {
        std::vector<Hit> out;
        if (b.loopLength <= 0.0)
            return out;
        for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
            for (const auto& h : b.hits)
                if (repeat + h.beat < b.length - 1.0e-9)
                    out.push_back ({ repeat + h.beat, h.slot, h.velocity });
        return out;
    }

    void flatten (Block& b)
    {
        b.hits = unrolled (b);
        b.loopLength = b.length;
    }

    void removeDrums (Block& b, SlotMask slots)
    {
        std::erase_if (b.hits, [slots] (const Hit& h) { return has (slots, h.slot); });
    }

    void pasteDrums (Block& b, const Block& clip, SlotMask slots)
    {
        removeDrums (b, slots);
        if (clip.loopLength > 0.0)
            for (auto repeat = 0.0; repeat < b.loopLength - 1.0e-9; repeat += clip.loopLength)
                for (const auto& h : clip.hits)
                    if (has (slots, h.slot) && repeat + h.beat < b.loopLength - 1.0e-9)
                        b.hits.push_back ({ repeat + h.beat, h.slot, h.velocity });
        tidy (b.hits);
    }

    void quantize (Block& b, double grid, SlotMask slots)
    {
        if (grid <= 0.0)
            return;
        for (auto& h : b.hits)
            if (has (slots, h.slot))
            {
                h.beat = std::round (h.beat / grid) * grid;
                if (h.beat >= b.loopLength - 1.0e-9)
                    h.beat = 0.0;   // pushed to the groove's end: its start, the next time round
            }
        tidy (b.hits);
    }

    void scaleVelocity (Block& b, float factor, SlotMask slots)
    {
        for (auto& h : b.hits)
            if (has (slots, h.slot))
                h.velocity = juce::jlimit (1.0f / 127.0f, 1.0f, h.velocity * factor);
    }

    void moveDrum (Block& b, int from, int to)
    {
        if (from == to || to < 0 || to >= synth::drums::numSlots)
            return;
        for (auto& h : b.hits)
            if (h.slot == from)
                h.slot = to;
        tidy (b.hits);
    }

    void addHit (Block& b, Hit hit)
    {
        if (hit.beat < 0.0 || hit.beat >= b.loopLength || hit.slot < 0 || hit.slot >= synth::drums::numSlots)
            return;
        std::erase_if (b.hits, [&hit] (const Hit& h) { return h.slot == hit.slot && std::abs (h.beat - hit.beat) < 1.0e-6; });
        b.hits.push_back (hit);
        tidy (b.hits);
    }

    bool removeHitNear (Block& b, int slot, double beat, double within)
    {
        auto best = b.hits.end();
        auto distance = within;
        for (auto it = b.hits.begin(); it != b.hits.end(); ++it)
            if (it->slot == slot && std::abs (it->beat - beat) <= distance)
            {
                distance = std::abs (it->beat - beat);
                best = it;
            }
        if (best == b.hits.end())
            return false;
        b.hits.erase (best);
        return true;
    }

    int Song::split (int index, double atBeat)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto& first = blocks[(size_t) index];
        const auto cut = snap (atBeat);   // the song's bar line nearest (with its time signature changes)
        if (cut <= first.start + 1.0e-9 || cut >= first.end() - 1.0e-9)
            return -1;
        flatten (first);
        auto second = first;
        const auto at = cut - first.start;
        first.length = first.loopLength = at;
        std::erase_if (first.hits, [at] (const Hit& h) { return h.beat >= at - 1.0e-9; });
        second.start = cut;
        second.length = second.loopLength = second.length - at;
        std::erase_if (second.hits, [at] (const Hit& h) { return h.beat < at - 1.0e-9; });
        for (auto& h : second.hits)
            h.beat -= at;
        blocks.insert (blocks.begin() + index + 1, std::move (second));
        return index + 1;
    }

    int Song::mergeWithNext (int index)
    {
        if (index < 0 || index + 1 >= (int) blocks.size())
            return -1;
        auto& first = blocks[(size_t) index];
        const auto& next = blocks[(size_t) index + 1];
        auto hits = unrolled (first);
        for (auto h : unrolled (next))
        {
            h.beat += next.start - first.start;
            hits.push_back (h);
        }
        first.length = first.loopLength = next.end() - first.start;
        first.hits = std::move (hits);
        tidy (first.hits);
        blocks.erase (blocks.begin() + index + 1);
        return index;
    }

    juce::Colour colourOf (grooves::Part part) noexcept
    {
        switch (part)
        {
            // The family's colours only (0.11.0): the red / pink / orange range, pearl white and onyx.
            case grooves::Part::intro: return juce::Colour (0xffd6c6ce);      // pearl
            case grooves::Part::verse: return juce::Colour (0xffff8a3d);      // orange
            case grooves::Part::preChorus: return juce::Colour (0xffffb22e);  // amber
            case grooves::Part::chorus: return juce::Colour (0xffff3d7a);     // hot pink
            case grooves::Part::bridge: return juce::Colour (0xffffa0c0);     // light pink
            case grooves::Part::fill: return juce::Colour (0xffe0103c);       // red
            case grooves::Part::ending: return juce::Colour (0xffb3261e);     // oxblood
            case grooves::Part::countIn: return juce::Colour (0xff6b6569);    // onyx
            case grooves::Part::beat: break;
        }
        return juce::Colour (0xffff6b5e);                                     // salmon
    }
}

namespace song
{
    Song importMidi (const juce::MidiFile& file, const synth::drums::notemap::Map& layout, const juce::String& name, bool withTempo)
    {
        Song s;
        const auto tpq = (double) file.getTimeFormat();
        if (tpq <= 0.0)
            return s;   // timed in SMPTE frames, not beats
        juce::MidiMessageSequence all;
        for (int t = 0; t < file.getNumTracks(); ++t)
            all.addSequence (*file.getTrack (t), 0.0);
        all.sort();

        std::vector<Hit> hits;
        std::vector<std::pair<double, juce::String>> markers;
        auto end = 0.0;
        for (const auto* e : all)
        {
            const auto& m = e->message;
            const auto beat = m.getTimeStamp() / tpq;
            end = juce::jmax (end, beat);
            if (m.isNoteOn())
            {
                if (const auto slot = synth::drums::notemap::slotOf (layout, m.getNoteNumber()); slot >= 0)
                    hits.push_back ({ beat, slot, m.getFloatVelocity() });
            }
            else if (m.isTextMetaEvent() && (m.getMetaEventType() == 6 || m.getMetaEventType() == 7))
            {
                if (const auto text = m.getTextFromTextMetaEvent().trim(); text.isNotEmpty())
                    markers.emplace_back (beat, text);
            }
            else if (withTempo && m.isTempoMetaEvent())
            {
                s.addTempo (beat, 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote()));
            }
            else if (withTempo && m.isTimeSignatureMetaEvent())
            {
                int num = 4, den = 4;
                m.getTimeSignatureInfo (num, den);
                s.setMeter (beat, num, den);
            }
        }
        if (hits.empty())
            return {};
        // Without its tempo the song keeps the DAW's; one tempo at the start isn't a change: kept as the song's tempo.
        std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        // Its length: to the bar line after its last note (or its end, if later).
        const auto lines = s.barLines (juce::jmax (end, hits.back().beat) + 1.0e-6);
        auto length = lines.back();
        for (const auto line : lines)
            if (line >= juce::jmax (hits.back().beat + 1.0e-6, end - 1.0e-6))
            {
                length = line;
                break;
            }

        // Blocks: one from each marker to the next (the first from the start, if notes come before it).
        std::stable_sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::pair<double, juce::String>> starts;
        if (markers.empty() || markers.front().first > hits.front().beat + 1.0e-6)
            starts.emplace_back (0.0, name);
        for (const auto& [beat, text] : markers)
            if (beat < length - 1.0e-6 && (starts.empty() || beat > starts.back().first + 1.0e-6))
                starts.emplace_back (beat, text);
        for (size_t i = 0; i < starts.size(); ++i)
        {
            Block b;
            b.name = starts[i].second;
            b.part = grooves::partIn (b.name);
            b.start = starts[i].first;
            b.length = b.loopLength = (i + 1 < starts.size() ? starts[i + 1].first : length) - b.start;
            for (const auto& h : hits)
                if (h.beat >= b.start - 1.0e-9 && h.beat < b.end() - 1.0e-9)
                    b.hits.push_back ({ h.beat - b.start, h.slot, h.velocity });
            if (b.length > 1.0e-6)
                s.blocks.push_back (std::move (b));
        }
        return s;
    }
}
