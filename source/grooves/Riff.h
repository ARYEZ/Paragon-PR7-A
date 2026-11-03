// RIFF MATCH (PR7-D 0.32.0): drum grooves that fit a riff. Our own design, inspired by the riff-to-groove search of
// commercial drum software: the player drops a recorded part (guitar, bass, keys: an audio file) or a MIDI file of
// one, chooses which bars of it to use, and every groove gets a score for how well it would play with it.
//
//   - An audio riff's hits are found by spectral flux (how much the sound's spectrum grows from one moment to the next:
//     a note's attack), its tempo from its name, the song, or the hits themselves (and the file's length).
//   - A MIDI riff's hits are its notes (a chord is one hit), its tempo the file's (or the song's).
//   - A groove fits when its kick (and crashes) land on the riff's strong hits and not where the riff is silent: how a
//     drummer locks to a riff. A backbeat snare on a riff accent counts partly. Another feel (swung against straight)
//     or another bar length counts less.
#pragma once

#include "Groove.h"
#include "Rhythm.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <optional>

namespace grooves
{
    struct Onset
    {
        double seconds = 0.0;   // from the file's start
        float strength = 1.0f;  // the riff's loudest 1
    };

    struct Riff
    {
        juce::File file;
        juce::String name;
        bool fromMidi = false;

        // Its tempo, and where that came from.
        enum class TempoFrom { file, name, song, found, chosen };
        double bpm = 120.0;
        TempoFrom tempoFrom = TempoFrom::song;
        int numerator = 4, denominator = 4;
        double heardBpm = 0.0;       // an audio riff's tempo as heard (0: none), offered when it isn't the one used

        std::vector<Onset> onsets;   // in time order
        double seconds = 0.0;        // the whole file

        // To hear it with the grooves: the file (a MIDI one's notes as plucked tones), its peak at -12 dBFS.
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double sampleRate = 48000.0;

        // The part searched with (and heard): `bars` bars from `startSeconds`.
        double startSeconds = 0.0;
        int bars = 2;

        double beatsPerBar() const noexcept { return numerator * 4.0 / juce::jmax (1, denominator); }
        double lengthBeats() const noexcept { return bars * beatsPerBar(); }
        double loopSeconds() const noexcept { return lengthBeats() * 60.0 / juce::jmax (1.0, bpm); }
        double beatAt (double secondsIn) const noexcept { return (secondsIn - startSeconds) * bpm / 60.0; }
        double secondsAt (double beat) const noexcept { return startSeconds + beat * 60.0 / juce::jmax (1.0, bpm); }
    };

    inline const juce::StringArray tempoFromNames { "from the file", "from its name", "the song's", "found in it", "yours" };

    bool isAudioRiff (const juce::File&);   // wav, aiff, flac, ogg, mp3
    bool isRiffFile (const juce::File&);    // those, or MIDI

    // Reads a riff. `songBpm` and the song's time signature: used when the file doesn't say its own (a riff from the
    // song is at its tempo; an audio riff's heard tempo is offered in the panel). Nothing if it can't be read (why in
    // `problem`).
    std::optional<Riff> readRiff (const juce::File&, double songBpm, int songNumerator, int songDenominator,
                                  juce::String* problem = nullptr);
    // The same for audio already in memory (tests; `name` for its tempo, as a file's).
    Riff riffFromAudio (juce::AudioBuffer<float> audio, double sampleRate, double songBpm, int songNumerator,
                        int songDenominator, const juce::String& name = {});

    // An audio file's hits: spectral flux peaks, each at its attack, its strength from the flux (loudest 1).
    std::vector<Onset> findOnsets (const juce::AudioBuffer<float>&, double sampleRate);
    // A tempo for those hits (60-200 BPM), or 0 when they give none. `fileSeconds`: a riff cut to whole bars says its
    // tempo by its length, used when it agrees.
    double findTempo (const std::vector<Onset>&, double fileSeconds, double beatsPerBar);

    // What a search uses: the part's hits in beats from its start.
    struct RiffQuery
    {
        double lengthBeats = 8.0, beatsPerBar = 4.0;
        std::vector<std::pair<double, float>> hits;   // beat, strength (the part's loudest 1)
        Feel feel = Feel::straight;
        juce::String key;                             // what makes it this search (for the scores remembered)
    };
    RiffQuery queryOf (const Riff&);
    // The hits' strengths to strong (1) and weak (0.3): two levels when there are two clear ones, else all strong.
    void markAccents (std::vector<std::pair<double, float>>& hits);
    Feel feelOf (const std::vector<std::pair<double, float>>& hits);

    // How well `groove` would play with the riff, 0 to 1 (see above): tried from each of the groove's bars, the best kept.
    double riffMatch (const RiffQuery&, const Rhythm& groove, Feel grooveFeel);
}
