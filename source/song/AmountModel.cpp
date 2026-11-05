#include "song/AmountModel.h"

#include <bit>
#include <cmath>
#include <set>

namespace song::playstyle
{
    namespace
    {
        int count (std::uint64_t bits) noexcept { return std::popcount (bits); }
    }

    void AmountModel::add (const grooves::Rhythm& r, bool swung, bool fill)
    {
        const auto steps = (int) std::lround (r.beatsPerBar * stepsPerBeat);
        if (steps < stepsPerBeat || steps > maxSteps || std::abs (steps - r.beatsPerBar * stepsPerBeat) > 1.0e-6)
            return;
        const auto length = std::min (r.lengthBeats, grooves::Rhythm::maxBeats);
        const auto numBars = (int) std::floor (length / r.beatsPerBar + 1.0e-6);
        if (numBars <= 0)
            return;
        std::vector<BarPattern> bars ((size_t) numBars);
        for (int l = 0; l < grooves::numLanes; ++l)
            for (const auto beat : r.beats[(size_t) l])
            {
                const auto bar = (int) std::floor (beat / r.beatsPerBar + 1.0e-9);
                if (bar < 0 || bar >= numBars)
                    continue;
                const auto step = (int) std::lround ((beat - bar * r.beatsPerBar) * stepsPerBeat);
                if (step >= 0 && step < steps)   // (a hit rounded onto the next bar line belongs to the next bar: left out)
                    bars[(size_t) bar].lanes[(size_t) l] |= std::uint64_t (1) << step;
            }
        // Each different bar of a groove once (a groove repeating its bar would otherwise count it eight times).
        std::set<BarPattern> seen;
        auto& bucket = buckets[keyOf (steps, swung, fill)];
        for (const auto& b : bars)
            if (! b.isEmpty() && seen.insert (b).second)
            {
                bucket.bars.push_back (b);
                ++total;
            }
    }

    void AmountModel::finish()
    {
        for (auto& [key, bucket] : buckets)
        {
            const auto steps = key / 4;
            for (int l = 0; l < grooves::numLanes; ++l)
            {
                auto& prior = bucket.prior[(size_t) l];
                prior.fill (0.0f);
                for (const auto& b : bucket.bars)
                    for (int s = 0; s < steps; ++s)
                        if ((b.lanes[(size_t) l] >> s) & 1u)
                            prior[(size_t) s] += 1.0f;
                for (auto& p : prior)
                    p /= (float) juce::jmax ((size_t) 1, bucket.bars.size());
            }
        }
    }

    void AmountModel::learn (const std::vector<grooves::Entry>& entries)
    {
        buckets.clear();
        total = 0;
        for (const auto& e : entries)
            if (e.rhythm != nullptr)
                add (*e.rhythm, e.feel == grooves::Feel::swung, e.part == grooves::Part::fill);
        finish();
    }

    const AmountModel::Bucket* AmountModel::bucketFor (int steps, bool swung, bool fill) const
    {
        // Its own kind first; then the other kind of bar (a fill's beats, a beat's fills), then the other feel.
        for (const auto& [s, f] : { std::pair { swung, fill }, std::pair { swung, ! fill }, std::pair { ! swung, fill }, std::pair { ! swung, ! fill } })
            if (const auto it = buckets.find (keyOf (steps, s, f)); it != buckets.end() && (int) it->second.bars.size() >= enoughBars)
                return &it->second;
        return nullptr;
    }

    std::vector<double> AmountModel::likelihood (const Query& q) const
    {
        if (q.lane < 0 || q.lane >= grooves::numLanes || q.steps < 1 || q.steps > maxSteps)
            return {};
        const auto* bucket = bucketFor (q.steps, q.swung, q.fill);
        if (bucket == nullptr)
            return {};
        const auto mine = q.bar.lanes[(size_t) q.lane];
        const auto have = count (mine);
        if (! q.adding && have == 0)
            return {};

        // How alike each bar is: the asked group (adding: how much of this bar's it plays; removing: how much of its
        // own is this bar's), and the other groups' likeness (shared hits over all their hits, each group playing in
        // either), cubed and squared so the closest count most.
        struct Vote { double weight; const BarPattern* bar; };
        std::vector<Vote> votes;
        votes.reserve (bucket->bars.size());
        for (const auto& b : bucket->bars)
        {
            const auto theirs = b.lanes[(size_t) q.lane];
            const auto both = count (mine & theirs);
            double alike = 1.0;
            if (q.adding)
            {
                if ((theirs & ~mine) == 0)
                    continue;   // nothing more to learn from it
                alike = have > 0 ? (double) both / have : 1.0;
            }
            else
            {
                if (theirs == 0)
                    continue;   // it says take everything out, not which
                alike = (double) both / count (theirs);
            }
            if (alike <= 0.0)
                continue;
            double others = 0.0;
            int groups = 0;
            for (int l = 0; l < grooves::numLanes; ++l)
                if (l != q.lane)
                    if (const auto either = count (q.bar.lanes[(size_t) l] | b.lanes[(size_t) l]); either > 0)
                    {
                        others += (double) count (q.bar.lanes[(size_t) l] & b.lanes[(size_t) l]) / either;
                        ++groups;
                    }
            const auto context = groups > 0 ? others / groups : 1.0;
            const auto weight = alike * alike * alike * std::pow (0.2 + 0.8 * context, 2.0);
            if (weight > 1.0e-4)
                votes.push_back ({ weight, &b });
        }
        const auto k = std::min ((size_t) neighbours, votes.size());
        std::partial_sort (votes.begin(), votes.begin() + (std::ptrdiff_t) k, votes.end(),
                           [] (const Vote& x, const Vote& y) { return x.weight > y.weight; });
        double sum = 0.0;
        for (size_t i = 0; i < k; ++i)
            sum += votes[i].weight;
        if (sum < 0.3)
            return {};   // nothing close enough: the rules

        // The vote, blended with the whole library's habit (worth one close bar).
        constexpr double prior = 1.0;
        std::vector<double> out ((size_t) q.steps, 0.0);
        for (size_t i = 0; i < k; ++i)
            for (int s = 0; s < q.steps; ++s)
                if ((votes[i].bar->lanes[(size_t) q.lane] >> s) & 1u)
                    out[(size_t) s] += votes[i].weight;
        for (int s = 0; s < q.steps; ++s)
            out[(size_t) s] = (out[(size_t) s] + prior * bucket->prior[(size_t) q.lane][(size_t) s]) / (sum + prior);
        return out;
    }

    BarPattern barOf (const std::vector<Hit>& notes, int lane, const Block& block, SlotMask excluded, int bar, double beatsPerBar)
    {
        BarPattern p;
        const auto from = bar * beatsPerBar;
        const auto steps = (int) std::lround (beatsPerBar * AmountModel::stepsPerBeat);
        const auto put = [&] (int l, double beat)
        {
            const auto step = (int) std::lround ((beat - from) * AmountModel::stepsPerBeat);
            if (l >= 0 && l < grooves::numLanes && step >= 0 && step < steps && step < AmountModel::maxSteps)
                p.lanes[(size_t) l] |= std::uint64_t (1) << step;
        };
        for (const auto& h : notes)
            if (h.beat >= from - 1.0e-9 && h.beat < from + beatsPerBar - 1.0e-9 && grooves::laneOf (h.slot) == lane)
                put (lane, h.beat);
        for (const auto& h : block.hits)
            if (const auto l = grooves::laneOf (h.slot);
                l != lane && (excluded & slotBit (h.slot)) == 0 && h.beat >= from - 1.0e-9 && h.beat < from + beatsPerBar - 1.0e-9)
                put (l, h.beat);   // (another instrument of the same group, another tom: not this one's hits)
        return p;
    }
}
