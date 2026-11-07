#pragma once

#include "guitar/Fretting.h"
#include "guitar/GuitarLibrary.h"

#include <juce_audio_basics/juce_audio_basics.h>

// THE PLAYING ENGINE (PR7-I 0.2.0): MIDI in, a guitar out, from a Library's takes. Our own design and code, working the
// way the owner's reference guitar instrument does:
//
//   - Each note gets a string and fret (Fretting.h): notes struck at the same moment are a chord, fretted together.
//   - ONE NOTE PER STRING: a new note on a sounding string stops it (5 ms), as picking it again does; a NOTE-OFF damps
//     the string over RELEASE. Notes on other strings ring on.
//   - PICKING: ALTERNATE (down, up, down... one stroke per chord), DOWN or UP: which stroke's take plays; each stroke's
//     ROUND-ROBINS in turn, so the same take never plays twice running.
//   - VELOCITY picks the dynamic (the render list's three: below 65 soft, below 102 medium, then hard), and within it
//     VEL > VOL scales the level (0: the take as recorded; 1: fully by velocity).
//   - KEYSWITCHES, below the guitar's lowest note (A1) and above its highest: the reference's defaults, so MIDI parts
//     written for it play the same: C-2..F#-2 (notes 0-6) force string low A..high E, G-2 (7) force string off; A#-2
//     (10) hand position = its velocity, B-2 (11) hand AUTO; C-1 (12) sustain; C7 / C#7 / D7 (108-110) picking up /
//     down / alternate. A keyswitch overrides the page's setting until that setting is changed.
//   - THE HAND (AUTO): follows the notes; back to the bottom of the neck after a second with nothing held.
namespace guitar
{
    enum class PickMode { alternate, down, up };   // append only

    struct Settings
    {
        FretMode fretMode = FretMode::natural;
        int forceString = -1;          // -1 off
        int hand = 0;                  // 0 AUTO, else the hand's fret
        PickMode pick = PickMode::alternate;
        float velocityToVolume = 0.5f;
        float releaseSeconds = 0.08f;
        int transpose = 0;
        const Library* library = nullptr;
        Streamer* streamer = nullptr;
        bool waitForDisk = false;      // bouncing: wait for the disk rather than play silence
    };

    namespace keyswitch
    {
        inline constexpr int forceLowA = 0, forceOff = 7, setHand = 10, handAuto = 11, sustain = 12;
        inline constexpr int pickUp = 108, pickDown = 109, pickAlternate = 110;
    }

    int layerFor (int velocity) noexcept;

    class Guitar
    {
    public:
        static constexpr int maxVoices = 32;
        static constexpr double stopSeconds = 0.005;

        void prepare (double sampleRate);
        void reset() noexcept;   // every voice off at once (a new library, the transport...)

        // Adds the guitar into `left` / `right`, numSamples of them, playing `midi`'s events from sample `midiOffset` on.
        void process (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, int midiOffset, const Settings&) noexcept;

        // What's sounding, for the page (any thread): each string's fret (-1: none) and how loud.
        struct Sounding { int fret = -1; float level = 0.0f; };
        Sounding getSounding (int string) const noexcept { return { soundingFret[(size_t) string].load(), soundingLevel[(size_t) string].load() }; }
        int getHand() const noexcept { return shownHand.load(); }
        int getActiveVoices() const noexcept { return activeVoices.load(); }
        int getForcedString (const Settings&) const noexcept;   // as a keyswitch or the setting leaves it
        Position lastPosition() const noexcept { return last; }  // tests: the last note's string and fret
        Stroke lastStroke() const noexcept { return lastStrokePlayed; }
        const Take* lastTake() const noexcept { return lastTakePlayed; }

    private:
        struct Voice
        {
            bool active = false, held = false;
            int string = -1, note = -1;
            const Take* take = nullptr;
            double position = 0.0, step = 1.0;
            float gain = 1.0f, fade = 1.0f, fadeStep = 0.0f;
            int stream = -1;
            juce::uint32 age = 0;
        };
        std::array<Voice, maxVoices> voices {};
        std::array<int, numStrings> onString {};   // the held voice on each string (-1: none)
        double rate = 48000.0;
        juce::uint32 clock = 0;
        int autoHand = 1;
        double idleSamples = 0.0;
        Stroke nextStroke = Stroke::down;
        std::array<int, 2> nextRoundRobin {};   // by stroke
        int ksForce = -2;           // -2: no keyswitch (the setting), -1: off, else a string
        int ksHand = -1;            // -1: no keyswitch, 0: AUTO, else a fret
        int ksPick = -1;
        Settings lastSeen;          // to tell when a setting changes (it then wins over its keyswitch)
        bool seenSettings = false;
        const Library* library = nullptr;
        Position last;
        Stroke lastStrokePlayed = Stroke::down;
        const Take* lastTakePlayed = nullptr;
        std::array<std::atomic<int>, numStrings> soundingFret {};
        std::array<std::atomic<float>, numStrings> soundingLevel {};
        std::atomic<int> shownHand { 1 }, activeVoices { 0 };

        struct Chord { std::array<int, maxChord> notes {}, velocities {}; int count = 0; };
        void handle (const juce::MidiMessage&, const Settings&, Chord&) noexcept;
        void strike (const Chord&, const Settings&) noexcept;
        void stopVoice (Voice&, double seconds) noexcept;
        Voice& freeVoice() noexcept;
        void render (Voice&, float* left, float* right, int numSamples, const Settings&) noexcept;
        int handNow (const Settings&) const noexcept;
    };
}
