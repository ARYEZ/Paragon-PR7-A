#include "Rhythm.h"

#include <set>

namespace grooves
{
    using namespace synth::drums::notemap;

    int laneOf (int slot) noexcept
    {
        switch (slot)
        {
            case BD: return (int) Lane::kick;
            case SD: case SD2: case RIM: case CLP: return (int) Lane::snare;   // 0.31.0: the clap too (an electronic backbeat)
            case CH: case OH: case HH: case HO1: case HO3: return (int) Lane::hats;
            case RD: case RB: case RE: return (int) Lane::ride;
            case LT: case MT: case HT: return (int) Lane::toms;
            case CY: case CR2: case CHN: case SPL: return (int) Lane::crash;
            default: return -1;   // the pedal hat (a foot), percussion
        }
    }

    int slotOfLane (int lane) noexcept
    {
        constexpr int slots[] = { BD, SD, CH, RD, MT, CY };
        return lane >= 0 && lane < numLanes ? slots[lane] : -1;
    }

    juce::String Rhythm::encode() const
    {
        juce::StringArray lanes;
        for (int l = 0; l < numLanes; ++l)
        {
            juce::StringArray at;
            for (size_t i = 0; i < beats[(size_t) l].size(); ++i)
                if (const auto b = beats[(size_t) l][i]; b < maxBeats)
                {
                    juce::String t (juce::roundToInt (b * 12.0));
                    if (const auto v = juce::jlimit (1, 127, juce::roundToInt (velocityOf (l, i) * 127.0f)); v < 127)
                        t << "~" << v;
                    at.add (t);
                }
            if (! at.isEmpty())
                lanes.add (juce::String (l) + ":" + at.joinIntoString (","));
        }
        return juce::String (lengthBeats, 4) + ";" + juce::String (beatsPerBar, 4) + ";" + lanes.joinIntoString ("|");
    }

    void Rhythm::add (int lane, double beat, float velocity)
    {
        if (lane < 0 || lane >= numLanes)
            return;
        auto& b = beats[(size_t) lane];
        auto& v = velocities[(size_t) lane];
        v.resize (b.size(), 1.0f);
        const auto at = (size_t) (std::upper_bound (b.begin(), b.end(), beat) - b.begin());
        b.insert (b.begin() + (std::ptrdiff_t) at, beat);
        v.insert (v.begin() + (std::ptrdiff_t) at, juce::jlimit (0.0f, 1.0f, velocity));
    }

    Rhythm Rhythm::decode (const juce::String& text)
    {
        Rhythm r;
        const auto parts = juce::StringArray::fromTokens (text, ";", {});
        if (parts.size() < 3)
            return r;
        r.lengthBeats = juce::jmax (0.25, parts[0].getDoubleValue());
        r.beatsPerBar = juce::jmax (0.25, parts[1].getDoubleValue());
        for (const auto& lane : juce::StringArray::fromTokens (parts[2], "|", {}))
        {
            const auto l = lane.upToFirstOccurrenceOf (":", false, false).getIntValue();
            if (l < 0 || l >= numLanes)
                continue;
            for (const auto& t : juce::StringArray::fromTokens (lane.fromFirstOccurrenceOf (":", false, false), ",", {}))
                r.add (l, t.upToFirstOccurrenceOf ("~", false, false).getIntValue() / 12.0,
                       t.containsChar ('~') ? juce::jlimit (1, 127, t.fromFirstOccurrenceOf ("~", false, false).getIntValue()) / 127.0f : 1.0f);
        }
        return r;
    }

    Rhythm rhythmOf (const Groove& g, const Map& layout)
    {
        Rhythm r;
        r.lengthBeats = g.lengthBeats;
        r.beatsPerBar = g.beatsPerBar();
        for (const auto& h : g.hits)
        {
            if (h.beat >= Rhythm::maxBeats || h.note < 0 || h.note > 127)
                continue;
            if (const auto lane = laneOf (synth::drums::notemap::slotOf (layout, h.note)); lane >= 0)
                r.add (lane, h.beat, h.velocity);
        }
        return r;
    }

    Rhythm firstBars (const Rhythm& r, double lengthBeats)
    {
        Rhythm out;
        out.lengthBeats = lengthBeats;
        out.beatsPerBar = r.beatsPerBar;
        for (int l = 0; l < numLanes; ++l)
            for (size_t i = 0; i < r.beats[(size_t) l].size(); ++i)
                if (r.beats[(size_t) l][i] < lengthBeats)
                    out.add (l, r.beats[(size_t) l][i], r.velocityOf (l, i));
        return out;
    }

    Rhythm searchRhythm (const Rhythm& r)
    {
        return firstBars (r, juce::jmin (r.lengthBeats, searchBars * r.beatsPerBar));
    }

    namespace
    {
        struct Hits
        {
            std::vector<double> at;      // within [0, length)
            std::vector<bool> ghost;
        };

        // Hits closer than a 24th of a beat are one (a flam, a doubled note), the louder kept.
        Hits merged (std::vector<std::pair<double, float>> hits, double length)
        {
            std::sort (hits.begin(), hits.end());
            std::vector<std::pair<double, float>> out;
            for (const auto& h : hits)
                if (out.empty() || h.first - out.back().first > 1.0 / 24.0)
                    out.push_back (h);
                else
                    out.back().second = juce::jmax (out.back().second, h.second);
            if (out.size() > 1 && out.front().first + length - out.back().first <= 1.0 / 24.0)
            {
                out.front().second = juce::jmax (out.front().second, out.back().second);
                out.pop_back();
            }
            Hits r;
            auto loudest = 0.0f;
            for (const auto& h : out)
                loudest = juce::jmax (loudest, h.second);
            for (const auto& h : out)
            {
                r.at.push_back (h.first);
                r.ghost.push_back (h.second < 0.55f * loudest);
            }
            return r;
        }

        // A group's hits in [start, start + length) of a looping rhythm, from `start`.
        Hits window (const Rhythm& r, int lane, double start, double length, double early)
        {
            std::vector<std::pair<double, float>> hits;
            const auto& beats = r.beats[(size_t) lane];
            if (beats.empty() || r.lengthBeats <= 0.0)
                return {};
            for (auto cycle = std::floor (start / r.lengthBeats) * r.lengthBeats; cycle < start + length; cycle += r.lengthBeats)
                for (size_t i = 0; i < beats.size(); ++i)
                {
                    if (beats[i] >= r.lengthBeats)
                        continue;
                    const auto t = cycle + beats[i] - start;
                    // A hair before the window's start counts (as its end: the window loops; distances wrap round).
                    if (t < -early || t >= length)
                        continue;
                    hits.push_back ({ t < 0.0 ? t + length : t, r.velocityOf (lane, i) });
                }
            return merged (std::move (hits), length);
        }

        // A group's score: 2 x found / (tapped + found + the groove's other hits, weighed lightly).
        double laneScore (const std::vector<double>& tapped, const Hits& groove, double length, double tolerance, double grid)
        {
            if (tapped.empty() || groove.at.empty())
                return 0.0;
            struct Pair { double distance; size_t a, b; };
            std::vector<Pair> pairs;
            for (size_t a = 0; a < tapped.size(); ++a)
                for (size_t b = 0; b < groove.at.size(); ++b)
                {
                    auto d = std::abs (tapped[a] - groove.at[b]);
                    d = juce::jmin (d, length - d);
                    if (d <= tolerance + 1.0e-9)
                        pairs.push_back ({ d, a, b });
                }
            std::sort (pairs.begin(), pairs.end(), [] (const Pair& x, const Pair& y) { return x.distance < y.distance; });
            std::vector<bool> usedA (tapped.size()), usedB (groove.at.size());
            auto found = 0;
            for (const auto& p : pairs)
                if (! usedA[p.a] && ! usedB[p.b])
                {
                    usedA[p.a] = usedB[p.b] = true;
                    ++found;
                }
            auto others = 0.0;
            // Detail finer than QUANTIZE (a sixteenth when tapping eighths) is what a sketch leaves out: as a ghost note.
            const auto offGrid = [grid] (double t) { return std::abs (t / grid - std::round (t / grid)) * grid > 1.0 / 48.0; };
            for (size_t b = 0; b < groove.at.size(); ++b)
                if (! usedB[b])
                    others += groove.ghost[b] || offGrid (groove.at[b]) ? 0.1 : 0.25;
            return 2.0 * found / ((double) tapped.size() + found + others);
        }
    }

    double match (const Rhythm& query, const Rhythm& groove, double grid)
    {
        if (query.isEmpty() || groove.isEmpty() || grid <= 0.0)
            return 0.0;
        const auto length = query.lengthBeats;
        if (length <= 0.0)
            return 0.0;
        // How far a tap may be from a groove's hit and still be it: half a QUANTIZE step (1/8: a sixteenth either way).
        const auto tolerance = 0.5 * grid;

        // The taps where they were played (not moved: snapping a played sixteenth to an eighth would lose it), and up
        // to their slack earlier.
        using Taps = std::array<std::vector<double>, numLanes>;
        std::vector<Taps> queries;
        for (int k = 0; k < (query.slackBeats > 0.0 ? 5 : 1); ++k)
        {
            Taps q;
            for (int l = 0; l < numLanes; ++l)
            {
                std::vector<std::pair<double, float>> at;
                for (const auto b : query.beats[(size_t) l])
                {
                    const auto t = std::fmod (b - query.slackBeats * k / 4.0, length);
                    at.push_back ({ t < 0.0 ? t + length : t, 1.0f });
                }
                q[(size_t) l] = merged (std::move (at), length).at;
            }
            queries.push_back (std::move (q));
        }

        auto best = 0.0;
        const auto step = juce::jmax (0.25, groove.beatsPerBar);
        const auto hats = (size_t) Lane::hats, ride = (size_t) Lane::ride;
        for (auto start = 0.0; start < groove.lengthBeats - 1.0e-9; start += step)
        {
            std::array<Hits, numLanes> has;
            for (int l = 0; l < numLanes; ++l)
                if (! query.beats[(size_t) l].empty() || l == (int) hats || l == (int) ride)
                    has[(size_t) l] = window (groove, l, start, length, tolerance);
            // The riding hand: a tapped hi-hat is matched against the ride if the groove has no hats there, and back.
            if (has[hats].at.empty())
                has[hats] = has[ride];
            else if (has[ride].at.empty())
                has[ride] = has[hats];
            for (const auto& q : queries)
            {
                auto score = 0.0, total = 0.0;
                for (int l = 0; l < numLanes; ++l)
                    if (! q[(size_t) l].empty())
                    {
                        total += (double) q[(size_t) l].size();
                        score += (double) q[(size_t) l].size() * laneScore (q[(size_t) l], has[(size_t) l], length, tolerance, grid);
                    }
                if (total > 0.0)
                    best = juce::jmax (best, score / total);
            }
        }
        // Another bar length (3/4 against 4/4): the same hits mean less.
        if (std::abs (query.beatsPerBar - groove.beatsPerBar) > 1.0e-6)
            best *= 0.75;
        return best;
    }
}
