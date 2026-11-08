#include "NoiseLists.h"

namespace guitar::noises
{
    namespace
    {
        constexpr int unpitchedFrets[] = { 0, 7, 12, 19 };
        constexpr int powerChordStrings = 3, powerChordFrets = 16;   // roots on strings 0-2, frets 0-15
        constexpr int fretNoiseHits = 32;
    }

    Layout layout (List list)
    {
        switch (list)
        {
            case List::pitchedRelease:    return { 4, 1.25, holdSeconds };
            case List::powerChordRelease: return { 3, 1.25, holdSeconds };
            case List::unpitched:         return { 12, 1.25, holdSeconds };
            case List::fretNoise:         return { fretNoiseHits, 1.0, 0.05 };
        }
        return {};
    }

    bool perString (List list) noexcept { return list == List::pitchedRelease; }

    std::vector<Hit> hits (List list, int string)
    {
        std::vector<Hit> result;
        const auto l = layout (list);
        auto slot = 0;
        const auto add = [&] (int s, int fret, int repeat, int note)
        {
            Hit h;
            h.string = s;
            h.fret = fret;
            h.repeat = repeat;
            h.note = note;
            h.noteSeconds = renders::leadInSeconds + slot * l.slotSeconds + renders::noteDelaySeconds;
            h.offSeconds = h.noteSeconds + l.holdSeconds;
            result.push_back (h);
            ++slot;
        };
        switch (list)
        {
            case List::pitchedRelease:
                for (int f = 0; f <= numFrets; ++f)
                    for (int r = 0; r < l.repeats; ++r)
                        add (string, f, r, noteOf (string, f));
                break;
            case List::powerChordRelease:
                for (int s = 0; s < powerChordStrings; ++s)
                    for (int f = 0; f < powerChordFrets; ++f)
                        for (int r = 0; r < l.repeats; ++r)
                            add (s, f, r, noteOf (s, f));
                break;
            case List::unpitched:
                for (int s = 0; s < numStrings; ++s)
                    for (const auto f : unpitchedFrets)
                        for (int r = 0; r < l.repeats; ++r)
                            add (s, f, r, noteOf (s, f));
                break;
            case List::fretNoise:
                for (int r = 0; r < l.repeats; ++r)
                    add (0, 0, r, fretNoiseKey);
                break;
        }
        return result;
    }

    double lengthSeconds (List list, int string)
    {
        return renders::leadInSeconds + (double) hits (list, string).size() * layout (list).slotSeconds + 1.0;
    }

    juce::MidiFile toMidi (List list, int string)
    {
        constexpr int ppq = 960;
        const auto ticks = [] (double seconds) { return seconds * renders::bpm / 60.0 * ppq; };
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, fileName (list, string)));
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / renders::bpm)));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        const auto key = [&] (int note, double at)
        {
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100).withTimeStamp (ticks (at)));
            track.addEvent (juce::MidiMessage::noteOff (1, note).withTimeStamp (ticks (at + 0.05)));
        };
        key (renders::keys::legatoOff, 0.75);
        const auto articulation = list == List::powerChordRelease ? powerChordKey : renders::keys::sustain;
        for (const auto& h : hits (list, string))
        {
            const auto slot = h.noteSeconds - renders::noteDelaySeconds;
            if (list != List::fretNoise)
            {
                key (renders::keys::forceStringLowA + h.string, slot);
                key (articulation, slot);
                key (renders::keys::pickDown, slot + 0.05);
            }
            track.addEvent (juce::MidiMessage::textMetaEvent (6, list == List::fretNoise ? "FRET NOISE " + juce::String (h.repeat + 1)
                                                                    : "S" + juce::String (playerNumber (h.string)) + " F" + juce::String (h.fret)
                                                                        + " " + juce::String (h.repeat + 1)).withTimeStamp (ticks (h.noteSeconds)));
            const auto v = list == List::fretNoise ? fretNoiseVelocity : velocity;
            track.addEvent (juce::MidiMessage::noteOn (1, h.note, (juce::uint8) v).withTimeStamp (ticks (h.noteSeconds)));
            track.addEvent (juce::MidiMessage::noteOff (1, h.note).withTimeStamp (ticks (h.offSeconds)));
        }
        track.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp (ticks (lengthSeconds (list, string))));
        track.sort();
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (track);
        return file;
    }

    juce::String fileName (List list, int string)
    {
        switch (list)
        {
            case List::pitchedRelease:
            {
                const auto which = string == 0 ? juce::String ("low A") : string == numStrings - 1 ? juce::String ("high E") : stringName (string);
                return "PR7-I Release - String " + juce::String (playerNumber (string)) + " (" + which + ")";
            }
            case List::powerChordRelease: return "PR7-I Power Chord Release";
            case List::unpitched:         return "PR7-I Unpitched Noise";
            case List::fretNoise:         return "PR7-I Fret Noise";
        }
        return {};
    }
}
