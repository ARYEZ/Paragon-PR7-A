#include "guitar/Fretting.h"

#include <algorithm>

namespace guitar
{
    int handSpan (FretMode mode) noexcept
    {
        return mode == FretMode::polyphonic ? 5 : 4;
    }

    namespace
    {
        float costOf (int fret, bool busy, int hand, int span) noexcept
        {
            auto cost = 0.0f;
            if (fret == 0)
                cost = hand <= 5 ? 0.0f : 0.5f * (float) (hand - 5);   // open strings are easy low on the neck
            else if (fret < hand)
                cost = (float) (hand - fret);
            else if (fret > hand + span - 1)
                cost = (float) (fret - (hand + span - 1));
            if (busy)
                cost += 100.0f;
            return cost;
        }
    }

    Position choose (int note, const std::array<bool, numStrings>& busy, int hand, const FretSettings& settings) noexcept
    {
        hand = juce::jlimit (1, numFrets - 3, hand);
        if (settings.forceString >= 0 && settings.forceString < numStrings)
        {
            const auto fret = note - openNotes[(size_t) settings.forceString];
            if (fret >= 0 && fret <= numFrets)
                return { settings.forceString, fret };
        }
        const auto span = handSpan (settings.mode);
        Position best;
        auto bestCost = 1.0e9f;
        for (int s = 0; s < numStrings; ++s)
        {
            const auto fret = note - openNotes[(size_t) s];
            if (fret < 0 || fret > numFrets)
                continue;
            if (const auto cost = costOf (fret, busy[(size_t) s], hand, span); cost < bestCost)   // strictly: ties keep the lower string
            {
                bestCost = cost;
                best = { s, fret };
            }
        }
        return best;
    }

    void chooseChord (const int* notes, int count, std::array<bool, numStrings> busy, int hand, const FretSettings& settings,
                      Position* places, bool handFollows) noexcept
    {
        count = juce::jlimit (0, maxChord, count);
        std::array<int, maxChord> order {};
        for (int i = 0; i < count; ++i)
            order[(size_t) i] = i;
        std::sort (order.begin(), order.begin() + count, [notes] (int a, int b) { return notes[a] < notes[b] || (notes[a] == notes[b] && a < b); });
        auto chordSettings = settings;
        if (count > 1)
            chordSettings.forceString = -1;   // a forced string takes a single note, not a whole chord
        for (int k = 0; k < count; ++k)
        {
            const auto index = order[(size_t) k];
            const auto p = choose (notes[index], busy, hand, chordSettings);
            places[index] = p;
            if (p.valid())
            {
                busy[(size_t) p.string] = true;
                if (handFollows)
                    hand = followHand (hand, p.fret, settings.mode);
            }
        }
    }

    std::vector<Position> chooseChord (const std::vector<int>& notes, std::array<bool, numStrings> busy, int hand, const FretSettings& settings,
                                       bool handFollows)
    {
        std::vector<Position> result (std::min ((size_t) maxChord, notes.size()));
        chooseChord (notes.data(), (int) result.size(), busy, hand, settings, result.data(), handFollows);
        return result;
    }

    int followHand (int hand, int fret, FretMode mode) noexcept
    {
        if (fret <= 0)
            return hand;
        const auto span = handSpan (mode);
        if (fret < hand)
            hand = fret;
        else if (fret > hand + span - 1)
            hand = fret - (span - 1);
        return juce::jlimit (1, numFrets - 3, hand);
    }
}
