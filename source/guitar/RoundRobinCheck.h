#pragma once

#include "guitar/RenderList.h"

// THE ROUND-ROBIN CHECK (PR7-I 0.3.0): how many different takes the owner's reference guitar instrument really has for
// each stroke, before the render lists ask for them (asking for more than it has only renders copies). Our own design.
//
//   - THE CHECK LIST: one short MIDI file (about 4 minutes): three notes, each played 16 times with a down stroke, then
//     16 times with an up stroke: the low A open (sustain, hard), the low A open palm-muted, the D string's 5th fret
//     (sustain, medium). The same keyswitches as the render lists.
//   - THE ANALYSIS: each note's 16 takes of a stroke compared, from just before the pick for `compareSeconds`: two takes
//     whose shapes match (a normalised correlation of `sameAbove` or more, the best within a few samples' shift) are the
//     same recording. Groups of matching takes are the instrument's round-robins; the order they came in shows how it
//     cycles them (in turn: "1 2 3 4 1 2 3 4"; at random otherwise).
//   - 0.6.1, THE PALM MUTE CHECK (Check::palmMute): the owner's palm-mute imports kept fewer different takes on the thin
//     strings (about 4 a stroke on the low A, 2 on the high E, of the 8 rendered): five palm-muted notes from the high E
//     down to the low A, each 16 downs then 16 ups (the factory C#-1, velocity choosing the degree), 1 s in 1.6 s slots.
//     Each result also says how alike its "copies" are (the reference's own repeats measured 1.000: bit-identical) and
//     how alike its closest different takes are, so it shows whether the 0.99 copy test merges real takes.
namespace guitar::renders::rrcheck
{
    enum class Check { roundRobin, palmMute };
    inline constexpr int repeats = 16;
    inline constexpr double slotSeconds = 2.5, holdSeconds = 2.0;   // (the round-robin check's; palm mute: slotFor / holdFor)
    double slotFor (Check) noexcept;
    double holdFor (Check) noexcept;
    inline constexpr double compareSeconds = 0.25;
    inline constexpr double sameAbove = 0.99;

    struct Group { Articulation articulation; int string, fret, velocity; juce::String name; };
    const std::vector<Group>& groups (Check = Check::roundRobin);

    struct ProbeTake { int group = 0; Stroke stroke = Stroke::down; int repeat = 0; double noteSeconds = 0.0; };
    std::vector<ProbeTake> takes (Check = Check::roundRobin);
    double lengthSeconds (Check = Check::roundRobin);
    juce::MidiFile toMidi (Check = Check::roundRobin);
    inline const juce::String fileName = "PR7-I Round-Robin Check";
    juce::String fileNameFor (Check);   // "PR7-I Round-Robin Check", "PR7-I Palm Mute Check"

    // Which takes are the same recording: 0, 1, 2... by first appearance, one per take given.
    std::vector<int> sameAs (const std::vector<std::vector<float>>& takes, double sampleRate);

    struct Result
    {
        juce::String name;        // "Sustain, low A open, hard"
        Stroke stroke = Stroke::down;
        int different = 0;        // of `repeats`
        std::vector<int> order;   // each take's recording (1-based)
        bool inTurn = false;      // the recordings come round in a fixed cycle
        double copiesAlike = 1.0;     // 0.6.1: the least alike a take is to the first of its recording (1.000: an exact copy)
        double closestDifferent = 0.0;   // 0.6.1: the most alike two different recordings are
        juce::String describe() const;   // "Sustain, low A open, hard, down: 4 different, in turn (1 2 3 4 1 2 3 4 ...)"
    };
    struct Report
    {
        juce::String error;
        std::vector<Result> results;
        int fewest() const;       // the fewest different takes any stroke has (what the render lists can ask for)
        juce::String summary() const;
    };
    // Analyses a render of the check list (mono, at `sampleRate`).
    Report analyse (const float* audio, int numSamples, double sampleRate, Check = Check::roundRobin);
    Report analyseFile (const juce::File&, Check = Check::roundRobin);
}
