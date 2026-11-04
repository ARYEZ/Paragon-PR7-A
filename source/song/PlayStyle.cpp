#include "song/PlayStyle.h"

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

    std::vector<Hit> withAmount (const std::vector<Hit>& source, int piece, double amount, const Block& block, double beatsPerBar)
    {
        amount = juce::jlimit (-1.0, 1.0, amount);
        if (std::abs (amount) < 1.0e-6 || block.loopLength <= 0.0)
            return source;
        const auto kind = kindOf (piece);
        beatsPerBar = juce::jmax (1.0, beatsPerBar);
        const auto weight = [&] (double beat)
        {
            const auto inBar = std::fmod (beat, beatsPerBar);
            return levelAt (inBar, beatsPerBar) * kindPrior (kind, inBar, beatsPerBar) + nudge (beat);
        };
        if (amount < 0.0)
        {
            // The least leaned-on, softest notes go first.
            auto ranked = source;
            std::stable_sort (ranked.begin(), ranked.end(), [&weight] (const Hit& x, const Hit& y)
            {
                return weight (x.beat) * 0.6 + x.velocity * 0.4 > weight (y.beat) * 0.6 + y.velocity * 0.4;
            });
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
            const auto level = levelAt (std::fmod (places[i].beat, beatsPerBar), beatsPerBar);
            const auto velocity = kind == Kind::snare && level < 0.4 ? juce::jmin (loud, 0.35f)   // a ghost note
                                                                     : loud * (float) (0.55 + 0.45 * level);
            out.push_back ({ places[i].beat, slot, juce::jlimit (1.0f / 127.0f, 1.0f, velocity) });
        }
        std::stable_sort (out.begin(), out.end(), [] (const Hit& x, const Hit& y) { return x.beat < y.beat; });
        return out;
    }
}
