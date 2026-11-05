#include "song/PlayStyle.h"
#include "song/AmountModel.h"

#include "dsp/synth/Drums.h"
#include "dsp/synth/NoteMap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace song::playstyle
{
    namespace
    {
        using synth::drums::pieces;

        juce::String pieceName (int piece)
        {
            return piece >= 0 && piece < synth::drums::numPieces ? juce::String (pieces[(size_t) piece].name) : juce::String();
        }

        // How strongly a place in the bar is leaned on (1: the bar's first beat ... 0.3: a sixteenth or triplet).
        double levelAt (double inBar, double beatsPerBar) noexcept
        {
            const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-4; };
            if (near (inBar, 0.0))
                return 1.0;
            if (std::fmod (beatsPerBar, 2.0) < 1.0e-6 && near (inBar, beatsPerBar / 2.0))
                return 0.85;
            const auto inBeat = inBar - std::floor (inBar + 1.0e-6);
            if (near (inBeat, 0.0))
                return 0.75;
            if (near (inBeat, 0.5))
                return 0.5;
            return 0.3;
        }

        // What each kind of drum plays there (our reading of how a drummer plays), on top of the level.
        double kindPrior (Kind kind, double inBar, double beatsPerBar) noexcept
        {
            const auto beat = (int) std::floor (inBar + 1.0e-6);
            const auto inBeat = inBar - (double) beat;
            const auto onBeat = inBeat < 1.0e-4;
            const auto onEighth = std::abs (inBeat - 0.5) < 1.0e-4;
            switch (kind)
            {
                case Kind::kick:
                    if (beat == 0 && onBeat) return 1.3;
                    if (onBeat && std::abs (inBar - beatsPerBar / 2.0) < 1.0e-4) return 1.2;
                    if (onBeat) return beat % 2 == 1 ? 0.6 : 0.9;
                    return onEighth ? 0.9 : 0.6;
                case Kind::snare:
                    if (onBeat) return beat % 2 == 1 ? 1.4 : (beat == 0 ? 0.3 : 0.5);
                    return onEighth ? 0.6 : 0.55;   // the ghost notes
                case Kind::hats:
                case Kind::ride:
                case Kind::percussion:
                    return onBeat ? 1.0 : (onEighth ? 0.95 : 0.7);
                case Kind::tom:
                    return beat >= (int) std::ceil (beatsPerBar) - 1 ? 1.3 : 0.4;   // fills at the bar's end
                case Kind::crash:
                    if (inBar < 1.0e-4) return 1.5;
                    return onBeat ? 0.5 : 0.1;
            }
            return 1.0;
        }

        float defaultVelocity (Kind kind) noexcept
        {
            switch (kind)
            {
                case Kind::kick: return 0.85f;
                case Kind::snare: return 0.8f;
                case Kind::crash: return 0.85f;
                case Kind::tom: return 0.75f;
                default: return 0.6f;
            }
        }

        // A small fixed nudge per place, so equal places don't all go in (or out) together, the same every time.
        double nudge (double beat) noexcept
        {
            auto x = (std::uint32_t) std::llround (beat * 960.0) * 2654435761u;
            x ^= x >> 13;
            return (double) (x % 1000u) / 1000.0 * 0.04;
        }

        // The groove's grid: eighth-note triplets when more of its notes sit only on the triplet grid than only on the
        // sixteenth one, else sixteenths.
        double gridOf (const Block& b) noexcept
        {
            int triplet = 0, straight = 0;
            for (const auto& h : b.hits)
            {
                const auto on = [&h] (double step) { const auto r = std::fmod (h.beat, step); return r < 1.0e-3 || step - r < 1.0e-3; };
                const auto t = on (1.0 / 3.0), s = on (0.25);
                if (t && ! s) ++triplet;
                if (s && ! t) ++straight;
            }
            return triplet > straight ? 1.0 / 3.0 : 0.25;
        }
    }

    Kind kindOf (int piece) noexcept
    {
        const auto name = pieceName (piece);
        if (name == "KICK") return Kind::kick;
        if (name == "SNARE") return Kind::snare;
        if (name == "HI-HAT") return Kind::hats;
        if (name == "RIDE") return Kind::ride;
        if (name.startsWith ("TOM")) return Kind::tom;
        if (name.contains ("CRASH") || name.startsWith ("CHINA") || name == "SPLASH") return Kind::crash;
        return Kind::percussion;   // CLAP, TAMBOURINE, SHAKER
    }

    SlotMask slotsOfPiece (int piece) noexcept
    {
        SlotMask m = 0;
        if (piece >= 0 && piece < synth::drums::numPieces)
            for (int a = 0; a < pieces[(size_t) piece].count; ++a)
                m |= slotBit (pieces[(size_t) piece].articulations[(size_t) a].slot);
        return m;
    }

    std::vector<int> piecesIn (const Block& b)
    {
        std::vector<int> out;
        const auto add = [&out] (int slot)
        {
            if (const auto p = synth::drums::pieceOfSlot (slot); p >= 0 && std::find (out.begin(), out.end(), p) == out.end())
                out.push_back (p);
        };
        for (const auto& h : b.hits)
            add (h.slot);
        if (b.openingOn)
            add (b.openingSlot);
        std::sort (out.begin(), out.end());
        return out;
    }

    SlotMask playingDrums (const Block& b)
    {
        SlotMask m = 0;
        for (const auto p : piecesIn (b))
            m |= slotsOfPiece (p);
        return m;
    }

    SlotMask allToms() noexcept
    {
        SlotMask m = 0;
        for (int p = 0; p < synth::drums::numPieces; ++p)
            if (kindOf (p) == Kind::tom)
                m |= slotsOfPiece (p);
        return m;
    }

    SlotMask allCrashes() noexcept
    {
        SlotMask m = 0;
        for (int p = 0; p < synth::drums::numPieces; ++p)
            if (kindOf (p) == Kind::crash)
                m |= slotsOfPiece (p);
        return m;
    }

    int powerHandPiece (const Block& b)
    {
        // Moved there (and still playing there): that one.
        if (const auto moved = synth::drums::pieceOfSlot (b.powerHand); moved >= 0 && ! notesOf (b, moved).empty())
            return moved;
        std::array<int, synth::drums::numPieces> counts {};
        for (const auto& h : b.hits)
            if (const auto p = synth::drums::pieceOfSlot (h.slot); p >= 0)
            {
                const auto k = kindOf (p);
                if ((k == Kind::hats && h.slot != synth::drums::notemap::PH) || k == Kind::ride || k == Kind::crash)
                    ++counts[(size_t) p];
            }
        auto best = -1;
        for (int p = 0; p < synth::drums::numPieces; ++p)
            if (counts[(size_t) p] > 0 && (best < 0 || counts[(size_t) p] > counts[(size_t) best]))
                best = p;
        return best;
    }

    int powerHandSlot (const Block& b)
    {
        const auto piece = powerHandPiece (b);
        if (piece < 0)
            return -1;
        std::array<int, synth::drums::numSlots> counts {};
        for (const auto& h : b.hits)
            if (synth::drums::pieceOfSlot (h.slot) == piece && h.slot != synth::drums::notemap::PH)
                ++counts[(size_t) h.slot];
        return (int) std::distance (counts.begin(), std::max_element (counts.begin(), counts.end()));
    }

    std::vector<Hit> notesOf (const Block& b, int piece)
    {
        std::vector<Hit> out;
        const auto mask = slotsOfPiece (piece);
        for (const auto& h : b.hits)
            if ((mask & slotBit (h.slot)) != 0)
                out.push_back (h);
        return out;
    }

    void setNotes (Block& b, int piece, const std::vector<Hit>& notes)
    {
        const auto mask = slotsOfPiece (piece);
        std::erase_if (b.hits, [mask] (const Hit& h) { return (mask & slotBit (h.slot)) != 0; });
        for (const auto& h : notes)
            if (h.beat >= 0.0 && h.beat < b.loopLength - 1.0e-9)
                b.hits.push_back (h);
        std::stable_sort (b.hits.begin(), b.hits.end(), [] (const Hit& x, const Hit& y) { return x.beat < y.beat; });
    }

    void movePowerHand (Block& b, int toPiece, int toSlot)
    {
        const auto from = powerHandPiece (b);
        if (from < 0 || toPiece < 0 || toPiece >= synth::drums::numPieces)
            return;
        if (toSlot < 0 || (slotsOfPiece (toPiece) & slotBit (toSlot)) == 0)
            toSlot = pieces[(size_t) toPiece].articulations[0].slot;
        // Its playing notes (not the pedal: the foot stays where it is) to the new instrument's articulation.
        const auto fromMask = slotsOfPiece (from) & ~slotBit (synth::drums::notemap::PH);
        for (int s = 0; s < synth::drums::numSlots; ++s)
            if ((fromMask & slotBit (s)) != 0 && s != toSlot)
                moveDrum (b, s, toSlot);
        b.powerHand = toSlot;
    }

    void setArticulation (Block& b, int piece, int slot)
    {
        if ((slotsOfPiece (piece) & slotBit (slot)) == 0)
            return;
        for (int s = 0; s < synth::drums::numSlots; ++s)
            if ((slotsOfPiece (piece) & slotBit (s)) != 0 && s != slot && (s != synth::drums::notemap::PH || slot == s))
                moveDrum (b, s, slot);   // (not the hi-hat's pedal: that's the foot, playing on as it did)
    }

    namespace
    {
        // The learned likelihoods of each bar of the block for the instrument (empty: the rules decide), and which
        // places the group's other instruments (another tom, another crash) already play.
        struct Learned
        {
            int lane = -1;
            std::vector<std::vector<double>> bars;     // per bar of the loop, per twelfth (empty: not learned)
            std::vector<std::uint64_t> othersThere;    // per bar: the group's other instruments' places
            bool any() const noexcept
            {
                return std::any_of (bars.begin(), bars.end(), [] (const auto& b) { return ! b.empty(); });
            }
            // -1: not learned there.
            double at (double beat, double beatsPerBar) const
            {
                const auto bar = (int) std::floor (beat / beatsPerBar + 1.0e-9);
                if (bar < 0 || bar >= (int) bars.size() || bars[(size_t) bar].empty())
                    return -1.0;
                const auto step = (int) std::lround ((beat - bar * beatsPerBar) * AmountModel::stepsPerBeat);
                if (step < 0 || step >= (int) bars[(size_t) bar].size())
                    return -1.0;
                auto v = bars[(size_t) bar][(size_t) step];
                if ((othersThere[(size_t) bar] >> step) & 1u)
                    v *= 0.3;   // another of the group plays there already (two toms at once is rarer)
                return v;
            }
        };

        Learned learnedFor (const std::vector<Hit>& source, int piece, const Block& block, double beatsPerBar,
                            const AmountModel* model, bool adding)
        {
            Learned out;
            if (model == nullptr || model->numBars() == 0)
                return out;
            out.lane = grooves::laneOf (pieces[(size_t) piece].articulations[0].slot);
            const auto steps = (int) std::lround (beatsPerBar * AmountModel::stepsPerBeat);
            if (out.lane < 0 || steps > AmountModel::maxSteps || std::abs (steps - beatsPerBar * AmountModel::stepsPerBeat) > 1.0e-6)
                return out;
            const auto mask = slotsOfPiece (piece);
            const auto numBars = (int) std::ceil (block.loopLength / beatsPerBar - 1.0e-9);
            const auto swung = gridOf (block) > 0.3;   // (the triplet grid)
            for (int bar = 0; bar < numBars; ++bar)
            {
                AmountModel::Query q;
                q.lane = out.lane;
                q.steps = steps;
                q.swung = swung;
                q.fill = block.part == grooves::Part::fill;
                q.adding = adding;
                q.bar = barOf (source, out.lane, block, mask, bar, beatsPerBar);
                out.bars.push_back (model->likelihood (q));
                std::uint64_t others = 0;
                for (const auto& h : block.hits)
                    if ((mask & slotBit (h.slot)) == 0 && grooves::laneOf (h.slot) == out.lane)
                        if (const auto step = (int) std::lround ((h.beat - bar * beatsPerBar) * AmountModel::stepsPerBeat); step >= 0 && step < steps)
                            others |= std::uint64_t (1) << step;
                out.othersThere.push_back (others);
            }
            return out;
        }
    }

    bool isLearned (const std::vector<Hit>& source, int piece, const Block& block, double beatsPerBar, const AmountModel* learned)
    {
        return learnedFor (source, piece, block, juce::jmax (1.0, beatsPerBar), learned, true).any();
    }

    std::vector<Hit> withAmount (const std::vector<Hit>& source, int piece, double amount, const Block& block, double beatsPerBar,
                                 const AmountModel* learnedModel)
    {
        amount = juce::jlimit (-1.0, 1.0, amount);
        if (std::abs (amount) < 1.0e-6 || block.loopLength <= 0.0)
            return source;
        const auto kind = kindOf (piece);
        beatsPerBar = juce::jmax (1.0, beatsPerBar);
        const auto ruleWeight = [&] (double beat)
        {
            const auto inBar = std::fmod (beat, beatsPerBar);
            return levelAt (inBar, beatsPerBar) * kindPrior (kind, inBar, beatsPerBar) + nudge (beat);
        };
        const auto learned = learnedFor (source, piece, block, beatsPerBar, learnedModel, amount > 0.0);
        // Learned (0 to 1) where it was, the rules (scaled under it) elsewhere; the rules break ties. A kick added
        // keeps the rules' sense of the beat alongside (measured on the owner's two packs, putting back hits taken out
        // of 762 real grooves: kicks 27% right learned alone, 34% by the rules, 39% both; the other drums best learned
        // alone).
        const auto ruleShare = kind == Kind::kick ? 1.0 : 1.0e-3;
        const auto weight = [&] (double beat)
        {
            const auto rule = ruleWeight (beat);
            const auto l = learned.at (beat, beatsPerBar);
            return l >= 0.0 ? l + rule * ruleShare : rule * 0.5;
        };
        if (amount < 0.0)
        {
            // The notes drummers leave out most (the rules: the least leaned-on), softest first.
            auto ranked = source;
            const auto keep = [&] (const Hit& h)
            {
                const auto l = grooves::laneOf (h.slot) == learned.lane ? learned.at (h.beat, beatsPerBar) : -1.0;
                return l >= 0.0 ? l * 0.85 + h.velocity * 0.15 + ruleWeight (h.beat) * 1.0e-3
                                : ruleWeight (h.beat) * 0.6 + h.velocity * 0.4;
            };
            std::stable_sort (ranked.begin(), ranked.end(), [&keep] (const Hit& x, const Hit& y) { return keep (x) > keep (y); });
            ranked.resize ((size_t) std::llround ((double) ranked.size() * (1.0 + amount)));
            std::stable_sort (ranked.begin(), ranked.end(), [] (const Hit& x, const Hit& y) { return x.beat < y.beat; });
            return ranked;
        }
        // Adding: the empty places on the groove's grid, most leaned-on first, at the instrument's own loudness.
        const auto grid = gridOf (block);
        const auto slot = source.empty() ? pieces[(size_t) piece].articulations[0].slot : source.front().slot;
        auto loud = 0.0f;
        for (const auto& h : source)
            loud += h.velocity;
        loud = source.empty() ? defaultVelocity (kind) : loud / (float) source.size();
        struct Place { double beat, weight; };
        std::vector<Place> places;
        for (auto beat = 0.0; beat < block.loopLength - 1.0e-6; beat += grid)
        {
            const auto taken = std::any_of (source.begin(), source.end(), [beat, grid] (const Hit& h) { return std::abs (h.beat - beat) < grid * 0.5; });
            if (! taken)
                places.push_back ({ beat, weight (beat) });
        }
        std::stable_sort (places.begin(), places.end(), [] (const Place& x, const Place& y) { return x.weight > y.weight; });
        const auto count = (size_t) std::llround ((double) places.size() * amount);
        auto out = source;
        for (size_t i = 0; i < count && i < places.size(); ++i)
        {
            const auto inBar = std::fmod (places[i].beat, beatsPerBar);
            const auto level = levelAt (inBar, beatsPerBar);
            // A snare note off its backbeats (the bar's 2 and 4) is a ghost note: soft, a little less so on the beat.
            const auto beatInBar = (int) std::floor (inBar + 1.0e-6);
            const auto backbeat = inBar - beatInBar < 1.0e-4 && beatInBar % 2 == 1;
            const auto velocity = kind == Kind::snare && ! backbeat ? juce::jmin (loud, 0.35f) * (float) (0.75 + 0.25 * level)
                                                                    : loud * (float) (0.55 + 0.45 * level);
            out.push_back ({ places[i].beat, slot, juce::jlimit (1.0f / 127.0f, 1.0f, velocity) });
        }
        std::stable_sort (out.begin(), out.end(), [] (const Hit& x, const Hit& y) { return x.beat < y.beat; });
        return out;
    }
}
