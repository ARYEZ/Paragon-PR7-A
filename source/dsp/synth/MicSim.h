#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Mixer.h"

#include <array>
#include <vector>

// SIMULATED MICS AND ROOM (PR7-D 0.26.0): a synth drum (or a single sample) heard the way a recorded kit is, through
// close mics, a pair of overheads, a room pair and a far ambience pair, each with the delay and level its distance
// from the drum gives, the other drums bleeding into each close mic (quieter and duller off the mic's axis), and a
// modelled room behind the room and ambience mics. Each mic plays into THE MIXER's channel for it, so the mixer's
// faders, bleed, sends, effects and routing work on synth drums as on a multi-mic sampled kit.
//
// Our own design: the kit's layout (where each piece and mic stands, in metres from the drummer's seat, right-handed:
// the hi-hat on the left) is ours, levels fall with distance (1 / d), sound arrives at 343 m/s, and each mic channel
// has a fixed colour of our choosing (the kick-in mic's click, the kick-out's boom...). The room is a feedback delay
// network (8 lines) with early reflections. Nothing in it is measured from a real room or microphone.
namespace synth::micsim
{
    // ROOM, by index (saved: only append).
    inline const juce::StringArray roomNames { "Small Room", "Studio", "Live Room", "Big Hall" };

    struct Settings
    {
        bool on = false;       // SIMULATED MICS: synth drums through the mixer's mic channels
        int room = 1;          // roomNames' index
        float decay = 1.0f;    // the room's decay, times its own (0.3 to 3)
        float tone = 0.5f;     // the room's colour, dark (0) to bright (1)
    };

    // Where things stand: x to the drummer's right, y forward, z up (metres).
    struct Point { float x = 0.0f, y = 0.0f, z = 0.0f; };
    // A piece's place (THE PIECES' order, drums::pieces).
    inline constexpr int numPieces = 22;
    inline constexpr std::array<Point, numPieces> piecePlaces {{
        {  0.0f,  0.45f, 0.30f },   // KICK (its beater)
        { -0.15f, 0.35f, 0.65f },   // SNARE
        { -0.45f, 0.40f, 0.95f },   // HI-HAT
        {  0.0f,  0.62f, 0.85f },   // RACK TOM
        {  0.45f, 0.35f, 0.60f },   // FLOOR TOM 1
        {  0.65f, 0.12f, 0.55f },   // FLOOR TOM 2
        { -0.45f, 0.75f, 1.35f },   // CRASH 1
        {  0.30f, 0.85f, 1.40f },   // CRASH 2
        {  0.65f, 0.60f, 1.15f },   // RIDE
        {  0.85f, 0.75f, 1.45f },   // CHINA
        { -0.10f, 0.80f, 1.30f },   // SPLASH
        {  0.0f,  0.30f, 1.00f },   // CLAP (in front of the kit)
        { -0.45f, 0.40f, 1.10f },   // TAMBOURINE (on the hi-hat's stand)
        {  0.0f,  0.30f, 1.00f },   // SHAKER
        {  0.85f, 0.10f, 0.45f },   // TOM 4 (0.33.0: beyond floor tom 2)
        { -0.75f, -0.20f, 1.55f },  // CRASH 3 (0.36.0: the bigger kit, left high)
        { -0.20f, -0.55f, 1.75f },  // CRASH 4 (over the rack toms)
        {  0.55f, -0.45f, 1.65f },  // CRASH 5 (right high)
        {  0.85f, -0.15f, 1.45f },  // CRASH 6 (right)
        { -0.80f, 0.15f, 1.15f },   // FX CRASH (left, low)
        {  1.00f, 0.25f, 1.05f },   // CHINA 2 (right, low)
        {  0.95f, 0.30f, 0.45f },   // TOM 5 (beyond tom 4)
    }};

    // The mics a piece plays through (every channel but OTHER for the kit's drums and cymbals; the clap, tambourine
    // and shaker: OTHER, the overheads, room and ambience).
    juce::Array<int> channelsFor (int piece);
    // Whether a piece is heard in a mic channel (the same, without making a list: for the audio thread).
    inline bool hears (int piece, int channel) noexcept
    {
        if (piece >= 11 && piece <= 13)
            return channel == mixer::other || mixer::hearsWholeKit (channel);
        return channel != mixer::other && channel >= 0 && channel < mixer::numChannels;
    }

    // A mic: its place (a pair: left and right), how loud a drum at `reference` metres is in it, and how much quieter
    // a drum it isn't pointed at is (bleed).
    struct Mic
    {
        int channel = 0;
        Point left, right;
        bool stereo = false;
        float reference = 0.15f;
        float offAxis = 1.0f;
    };
    const Mic& micFor (int channel);

    // A tap: where a piece is heard in one side of a mic, as `delay` samples and `gain`.
    struct Tap { int delay = 0; float gain = 0.0f; };
    // The direct path (or a close mic's own drum: `own`) from a piece to one side of a mic, at a sample rate.
    Tap tapFor (int piece, int channel, bool rightSide, bool own, double sampleRate);

    // A biquad (direct form 2, transposed), for the mics' colours.
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
        enum class Type { lowCut, highCut, peak, lowShelf, highShelf };
        void set (Type, double hz, double q, double gainDb, double sampleRate) noexcept;
        float process (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    // The simulator: each slot's sound (fed once a block), the taps from it into the channels, the room.
    class Simulator
    {
    public:
        static constexpr int numSlots = 36;
        static constexpr int maxFilters = 3;

        void prepare (double sampleRate, int blockSize);
        void reset() noexcept;

        // A slot's sound for this block (mono, at its level): written into its line. Call before `play`.
        float* input (int slot) noexcept { return inputs.data() + (size_t) slot * (size_t) blockSize; }
        void fed (int slot, int numSamples) noexcept;
        // Before this block's taps: the lines of slots that sounded lately but weren't fed now, silent here.
        void begin (int numSamples) noexcept;
        // Slots whose lines still hold sound some tap hasn't reached; the room ringing.
        bool busy() const noexcept;
        bool sounding (int slot) const noexcept { return lineLeft[(size_t) slot] > 0; }

        // Adds `gain` x a slot's sound `delay` samples ago into `into`, for this block's samples.
        void tap (int slot, int delay, float gain, float* into, int numSamples) const noexcept;
        // The room: what reaches it (stereo), then its sound, added into the room and ambience mics.
        float* roomInput (bool right) noexcept { return roomIn.data() + (right ? (size_t) blockSize : 0); }
        void runRoom (const Settings&, int numSamples, float* roomLeft, float* roomRight, float* ambienceLeft, float* ambienceRight) noexcept;
        bool roomRinging() const noexcept { return roomLeft > 0; }
        // A source mic channel's colour on one of its lines (`which`: direct L / R, bleed L / R) on its way to a
        // destination channel (routed elsewhere, it keeps its colour), in place.
        void colour (int source, int destination, float* line, int which, int numSamples) noexcept;
        // Called once a block, after the taps: lines age.
        void advance (int numSamples) noexcept;

        double getSampleRate() const noexcept { return sampleRate; }
        int maxDelay() const noexcept { return (int) lineMask - blockSize; }

    private:
        double sampleRate = 48000.0;
        int blockSize = 512;
        std::vector<float> lines, inputs;          // numSlots lines of lineSize
        size_t lineSize = 0, lineMask = 0;
        size_t writePos = 0;                       // where this block's first sample goes in every line
        std::array<int, numSlots> lineLeft {};     // samples until a slot's line has gone quiet in every mic
        std::array<bool, numSlots> fedNow {};

        // Each mic channel's colour: up to 2 filters on each of its 4 lines (the bleed lines a 3rd: off axis), and
        // each source and destination pair's filters' memory.
        std::array<std::array<std::array<Biquad, maxFilters>, 4>, mixer::numChannels> filters {};
        std::array<int, mixer::numChannels> numFilters {};
        std::vector<float> states;

        // THE ROOM: early reflections from a tapped line, then 8 delay lines feeding back through a Householder
        // matrix, each with a damping filter; the ambience mics hear its tail later.
        std::vector<float> roomIn, early, fdn, ambienceDelay;
        size_t earlyMask = 0, earlyPos = 0, ambienceMask = 0, ambiencePos = 0, ambienceLength = 1, fdnMask = 0, fdnPos = 0;
        static constexpr int numLines = 8;
        std::array<size_t, numLines> fdnLength {};
        std::array<float, numLines> fdnGain {}, damp {}, dampState {};
        std::array<int, 8> earlyTapsL {}, earlyTapsR {};
        int lastRoom = -1;
        float lastDecay = -1.0f, lastTone = -1.0f, earlyLevel = 0.7f, tailSeconds = 0.6f;
        int roomLeft = 0;                          // samples until the room's tail is gone
        float toneState[4] {};
        float toneCoef = 0.5f;
        void setRoom (const Settings&) noexcept;
    };
}
