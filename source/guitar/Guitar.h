#pragma once

#include "guitar/Fretting.h"
#include "guitar/GuitarLibrary.h"

#include <juce_audio_basics/juce_audio_basics.h>

// THE PLAYING ENGINE (PR7-I 0.2.0): MIDI in, a guitar out, from a Library's takes. Our own design and code, working the
// way the owner's reference guitar instrument does:
//
//   - Each note gets a string and fret (Fretting.h): notes struck at the same moment are a chord, fretted together.
//   - ONE NOTE PER STRING: a new note on a sounding string stops its held note (5 ms), as picking it again does; a NOTE-OFF
//     damps the string over RELEASE (a note already let go rings out its release under the next: 0.2.1 cut it, and the
//     owner heard 0.2.0's way as better). Notes on other strings ring on.
//   - PICKING: ALTERNATE (down, up, down... one stroke per chord), DOWN or UP: which stroke's take plays; each stroke's
//     ROUND-ROBINS IN TURN (down 1, up 1, down 2, up 2: with two takes a stroke, the widest spacing there is). 0.2.1 chose
//     them at random, which with two takes often played the same one every other note: the machine gun the owner heard.
//   - VARIATION (0 by default: exactly the above): now and then (up to a third of the notes, times VARIATION) the in-turn
//     take is swapped for a BORROWED one: the neighbouring dynamic's (level matched by `Take::loudness`) or a neighbouring
//     fret's (a semitone off, played back at the note's pitch), never the take that stroke played last on that string;
//     and each note up to 3 ms further into its take, +/-0.5 dB, +/-3 cents (times VARIATION). An experiment: to be
//     judged by ear.
//   - VELOCITY picks the dynamic (the render list's three: below 65 soft, below 102 medium, then hard), and within it
//     VEL > VOL scales the level (0: the take as recorded; 1: fully by velocity).
//   - KEYSWITCHES, below the guitar's lowest note (A1) and above its highest: the reference's defaults, so MIDI parts
//     written for it play the same: C-2..F#-2 (notes 0-6) force string low A..high E, G-2 (7) force string off; A#-2
//     (10) hand position = its velocity, B-2 (11) hand AUTO; C7 / C#7 / D7 (108-110) picking up / down / alternate. A
//     keyswitch overrides the page's setting until that setting is changed.
//   - ARTICULATIONS (0.3.0), latching keyswitches, by the KEY MAP:
//       SPLIT (the owner's own map, the default): C-1 (12) palm mute at velocities 1-59 and sustain at 60-119 (120-127:
//       the reference's rake and pinch, not rendered: sustain at its hardest); C#-1 (13) the same for power chords. Each
//       zone SCALED over its articulation's whole velocity range (the owner's map has SCALING on), so 1-59 reaches all
//       five degrees of muting and 60-119 all three sustain dynamics.
//       FACTORY (the reference's own): C-1 sustain, C#-1 palm mute, D#-1 (15) power-chord sustain, E-1 (16) power-chord
//       mute; velocity as it comes.
//     PALM MUTE plays the guitar's Mute takes (velocity choosing the degree of muting); a guitar with none yet plays its
//     softest sustain takes instead. POWER CHORDS (not rendered yet) play the root, the fifth and the octave as three
//     notes, fretted together (our stand-in, as a player would: drop A's D5 a barre).
//   - THE HAND (AUTO): follows the notes; back to the bottom of the neck after HAND RESET (a second) with nothing held.
//   - 0.4.0: CAPO and HAND SPAN (Fretting.h); ECONOMY picking (alternate, but a note crossing to the next string in the
//     way the pick is already moving takes the same stroke again: a down stroke going on to a higher string stays down,
//     an up stroke going on to a lower one stays up); MONO (one note at a time: a new one stops the others); ATTACK (each
//     note's first milliseconds louder, by velocity: our stand-in for the reference's pick noise, which isn't rendered).
//     The page's NOW PLAYING reads what the last note did (getNowPlaying).
namespace guitar
{
    enum class PickMode { alternate, down, up, economy };   // append only
    enum class KeyMap { split, factory };           // append only

    struct Settings
    {
        FretMode fretMode = FretMode::natural;
        int forceString = -1;          // -1 off
        int hand = 0;                  // 0 AUTO, else the hand's fret
        PickMode pick = PickMode::alternate;
        float velocityToVolume = 0.5f;
        float releaseSeconds = 0.08f;
        float variation = 0.0f;        // 0 to 1
        KeyMap keyMap = KeyMap::split;
        int transpose = 0;
        int capo = 0;                  // 0.4.0: 0 none, else its fret
        int handSpan = 0;              // 0.4.0: 0 the mode's own
        float handResetSeconds = 1.0f; // 0.4.0: an AUTO hand goes home after this long with nothing held
        bool mono = false;             // 0.4.0: one note at a time
        float attack = 0.0f;           // 0.4.0: 0 to 1, each note's first milliseconds louder
        const Library* library = nullptr;
        Streamer* streamer = nullptr;
        bool waitForDisk = false;      // bouncing: wait for the disk rather than play silence
    };

    namespace keyswitch
    {
        inline constexpr int forceLowA = 0, forceOff = 7, setHand = 10, handAuto = 11;
        inline constexpr int c1 = 12, cSharp1 = 13, dSharp1 = 15, e1 = 16;   // the articulation keys
        inline constexpr int pickUp = 108, pickDown = 109, pickAlternate = 110;
    }

    int layerFor (int velocity) noexcept;

    // What a note-on plays under a key map, with the articulation keyswitch `key` latched: the articulation, its velocity
    // (scaled, in the SPLIT map, from the zone to the articulation's whole range) and whether it's a power chord.
    struct Played { Articulation articulation = Articulation::sustain; int velocity = 1; bool powerChord = false; };
    Played playedFor (KeyMap, int key, int velocity) noexcept;
    bool isArticulationKey (KeyMap, int note) noexcept;

    // 0.4.3: where in each take a note starts. Imports cut each take from 30 ms before its pick (RenderImport's
    // prerollSeconds), and until 0.4.2 notes played from the very start, so every pick landed 30 ms after its note-on.
    // The owner's A/B render of a chug riff measured their guitar instrument's picks 15-22 ms earlier than PR7-I's, so
    // notes now start 15 ms in (15 ms of the pick's lead-in kept, a 1 ms fade into it): the pick lands 15 ms after the
    // note-on, as theirs do.
    inline constexpr double takeStartSeconds = 0.015;

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
        struct Sounding { int fret = -1; float level = 0.0f; bool muted = false; };
        Sounding getSounding (int string) const noexcept
        {
            return { soundingFret[(size_t) string].load(), soundingLevel[(size_t) string].load(), soundingMuted[(size_t) string].load() };
        }
        int getArticulationKey() const noexcept { return articulationKey; }
        int getHand() const noexcept { return shownHand.load(); }
        int getActiveVoices() const noexcept { return activeVoices.load(); }
        int getForcedString (const Settings&) const noexcept;   // as a keyswitch or the setting leaves it

        // What the last note (or chord) played, for the page's NOW PLAYING (any thread; -1 fields: nothing yet).
        struct NowPlaying
        {
            int note = -1, inputVelocity = 0, velocity = 0, layer = 0, layers = 1, roundRobin = 0, roundRobins = 1, string = -1, fret = -1, notes = 0;
            Articulation articulation = Articulation::sustain;
            Stroke stroke = Stroke::down;
            bool powerChord = false;
            juce::uint32 count = 0;   // how many notes or chords so far (to tell a new one)
        };
        NowPlaying getNowPlaying() const noexcept;
        Position lastPosition() const noexcept { return last; }  // tests: the last note's string and fret
        Stroke lastStroke() const noexcept { return lastStrokePlayed; }
        const Take* lastTake() const noexcept { return lastTakePlayed; }

    private:
        struct Voice
        {
            bool active = false, held = false;
            int string = -1, note = -1, sourceNote = -1;   // sourceNote: the MIDI note that started it (a power chord: its root)
            bool muted = false;
            const Take* take = nullptr;
            double position = 0.0, step = 1.0;
            float gain = 1.0f, fade = 1.0f, fadeStep = 0.0f;
            float attack = 0.0f, attackDecay = 1.0f;   // ATTACK: an extra gain at the start, decaying
            float fadeIn = 1.0f, fadeInStep = 0.0f;    // 0.4.3: a 1 ms fade in (the take starts inside its lead-in)
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
        std::array<std::array<int, 2>, numArticulations> nextRoundRobin {};   // by articulation and stroke
        int articulationKey = keyswitch::c1;                                  // the articulation keyswitch latched
        std::array<std::array<const Take*, 2>, numStrings> lastByStroke {};   // the take each stroke played last on each string
        juce::Random random { 0x7072 };
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
        std::array<std::atomic<bool>, numStrings> soundingMuted {};
        std::atomic<int> shownHand { 1 }, activeVoices { 0 };
        std::array<std::atomic<int>, 12> nowPlaying {};   // NowPlaying's fields, written by the audio thread
        std::atomic<juce::uint32> nowCount { 0 };

        struct Chord
        {
            std::array<int, maxChord> notes {}, velocities {}, sources {}, inputVelocities {};
            std::array<bool, maxChord> power {};
            std::array<Articulation, maxChord> articulations {};
            int count = 0;
        };
        void handle (const juce::MidiMessage&, const Settings&, Chord&) noexcept;
        void strike (const Chord&, const Settings&) noexcept;
        void stopVoice (Voice&, double seconds) noexcept;
        Voice& freeVoice() noexcept;
        void render (Voice&, float* left, float* right, int numSamples, const Settings&) noexcept;
        int handNow (const Settings&) const noexcept;
    };
}
