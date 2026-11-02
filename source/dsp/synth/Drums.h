#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/synth/DrumSequencer.h"
#include "dsp/synth/MultiPoint.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/MicSim.h"
#include "dsp/synth/DiskStreamer.h"
#include "dsp/synth/Noise.h"
#include "dsp/synth/VelocityCurve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// THE DRUMS (PR7-K's stage 6a): a kit of slots, the first 12 one per key of an octave (C the kick, D# the clap, as on
// Avenger's drum tab and General MIDI); PR7-D 0.2.0 added 8 more (rides, more cymbals, a half-open hat, tambourine and
// shaker). Which keys play which slot is the note map's business (NoteMap.h). Each slot is one of our own drum synths or a sample the user loads; they play as one-shots (a
// note-off doesn't stop them), with VOLUME, PAN, PITCH, and three shaping knobs whose meaning suits the model:
//
//   DECAY  how long it rings;
//   TONE   its brightness (or, on KICK and TOM, how far the pitch drops);
//   SNAP   its attack (the beater's click, the snare's wires, the hats' sizzle...).
//
// The drum synths are our own designs from the classic analog ideas: a sine whose pitch falls (kick, tom), tuned modes
// plus filtered noise (snare), noise in bursts (clap), six square waves at unrelated pitches for metal (hats, cymbal,
// cowbell). Not a copy of any drum machine's circuit. SAMPLE plays the slot's sample (cubic interpolation, any rate);
// its DECAY, TONE and SNAP are neutral at the middle: DECAY under it shortens the sample, TONE tilts it darker or
// thinner, SNAP softens the attack or adds a spike.
//
// CHOKE: a hit stops the other slots in its group (the closed hat stops the open hat). MUTE and SOLO as on a mixer.
namespace synth
{
    // The effects the kit plays into: DELAY and REVERB's sends, and the insert buses A to D (PR7-K's numbers).
    inline constexpr int numSends = 2, numInsertBuses = 4;
    inline constexpr int numOutputs = 16;   // the plugin's stereo outputs: MAIN, then 15 more (PR7-D 0.3.0)
}

namespace synth::drums
{
    inline constexpr int numSlots = 23;   // 12 in PR7-K, 20 from PR7-D 0.2.0, 23 from 0.17.0 (articulations)

    // Saved by index (drum{k}Model): only ever append. RIDE, BELL, TAMBOURINE and SHAKER came in PR7-D 0.2.0.
    // ACOUSTIC KICK, SNARE and TOM came in 0.24.0: physically modelled drums (see Voice::startAcoustic).
    // The acoustic cymbals came in 0.25.0 (see Voice::startCymbal).
    enum class Model { kick, snare, clap, closedHat, openHat, tom, rim, cowbell, cymbal, sample, ride, bell, tambourine, shaker,
                       acousticKick, acousticSnare, acousticTom,
                       acousticCrash, acousticRide, acousticChina, acousticSplash, acousticStack, acousticHat, acousticHatPedal };
    inline const juce::StringArray modelNames { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cowbell", "Cymbal", "Sample",
                                                "Ride", "Bell", "Tambourine", "Shaker", "Acoustic Kick", "Acoustic Snare", "Acoustic Tom",
                                                "Acoustic Crash", "Acoustic Ride", "Acoustic China", "Acoustic Splash", "Acoustic Stack",
                                                "Acoustic Hi-Hat", "Acoustic Hat Pedal" };
    inline constexpr bool isAcousticCymbal (Model m) noexcept { return m >= Model::acousticCrash && m <= Model::acousticHatPedal; }
    inline constexpr bool isAcoustic (Model m) noexcept { return (m >= Model::acousticKick && m <= Model::acousticTom) || isAcousticCymbal (m); }
    inline const juce::StringArray chokeNames { "None", "1", "2", "3", "4" };   // drum{k}Choke, by index: only append

    // The new kit, slot by slot (the first 12 C to B from ROOT, then the 8 added in 0.2.0): what each starts as, and a
    // short name for it.
    struct SlotDefault
    {
        Model model;
        float pitch;          // semitones
        float decay, tone, snap;
        int choke;
        const char* key;      // e.g. "BD"
    };
    inline constexpr std::array<SlotDefault, numSlots> slotDefaults {{
        { Model::kick,      0.0f, 0.5f, 0.5f, 0.5f, 0, "BD" },     // C
        { Model::rim,       0.0f, 0.4f, 0.5f, 0.5f, 0, "RIM" },    // C#
        { Model::snare,     0.0f, 0.5f, 0.5f, 0.5f, 0, "SD" },     // D
        { Model::clap,      0.0f, 0.5f, 0.5f, 0.5f, 0, "CLP" },    // D#
        { Model::snare,     3.0f, 0.4f, 0.7f, 0.8f, 0, "SD2" },    // E
        { Model::tom,      -5.0f, 0.5f, 0.5f, 0.5f, 0, "LT" },     // F
        { Model::closedHat, 0.0f, 0.4f, 0.5f, 0.5f, 1, "CH" },     // F#
        { Model::tom,       0.0f, 0.5f, 0.5f, 0.5f, 0, "MT" },     // G
        { Model::closedHat, -2.0f, 0.2f, 0.4f, 0.3f, 1, "PH" },    // G#: the pedal hat
        { Model::tom,       5.0f, 0.5f, 0.5f, 0.5f, 0, "HT" },     // A
        { Model::openHat,   0.0f, 0.5f, 0.5f, 0.5f, 1, "OH" },     // A#
        { Model::cymbal,    0.0f, 0.6f, 0.5f, 0.5f, 0, "CY" },     // B: crash 1
        // PR7-D 0.2.0, ROOT + 12 to + 19 (C to G an octave up):
        { Model::ride,      0.0f, 0.6f, 0.5f, 0.5f, 0, "RD" },     // the ride's bow
        { Model::bell,      0.0f, 0.5f, 0.5f, 0.5f, 0, "RB" },     // the ride's bell
        { Model::cymbal,   -2.0f, 0.65f, 0.45f, 0.5f, 0, "CR2" },  // crash 2: lower and longer
        { Model::cymbal,   -5.0f, 0.5f, 0.2f, 0.9f, 0, "CHN" },    // china: low, dark and trashy
        { Model::cymbal,    5.0f, 0.3f, 0.6f, 0.5f, 0, "SPL" },    // splash: high and short
        { Model::openHat,   0.0f, 0.25f, 0.5f, 0.5f, 1, "HH" },    // the half-open hat
        { Model::tambourine, 0.0f, 0.5f, 0.5f, 0.5f, 0, "TMB" },
        { Model::shaker,    0.0f, 0.5f, 0.5f, 0.5f, 0, "SHK" },
        // PR7-D 0.17.0, articulations (ROOT + 20 to + 22): the ride's edge (the ride hit across its edge, crashing:
        // darker and longer than a crash), and two more steps of the hi-hat's openness, between the closed hat (CH),
        // the half-open (HH) and the open (OH): OPEN 1 just loosened, OPEN 3 nearly open.
        { Model::cymbal,   -6.0f, 0.75f, 0.35f, 0.6f, 0, "RE" },   // the ride's edge
        { Model::openHat,   0.0f, 0.15f, 0.5f, 0.5f, 1, "HO1" },  // the hat, open 1
        { Model::openHat,   0.0f, 0.38f, 0.5f, 0.5f, 1, "HO3" },  // the hat, open 3
    }};

    // THE PIECES AND THEIR ARTICULATIONS (PR7-D 0.17.0). Inspired by drum software where each drum of the kit is one
    // instrument with several ways of playing it (the snare's centre, rimshot and sidestick...), each with its own
    // notes, level and on / off. Here each articulation is one of the kit's slots, as it always was (so presets,
    // patterns and songs keep working), gathered into pieces: what the kit view, the articulation list and the
    // stand-ins below work with. Names are ours.
    struct Articulation
    {
        int slot;
        const char* name;     // e.g. "Rimshot"
    };
    struct Piece
    {
        const char* name;     // e.g. "SNARE"
        std::array<Articulation, 6> articulations;
        int count;
    };
    inline constexpr int numPieces = 14;
    inline constexpr std::array<Piece, numPieces> pieces {{
        { "KICK",        {{ { 0, "Hit" } }}, 1 },
        { "SNARE",       {{ { 2, "Center" }, { 4, "Rimshot" }, { 1, "Sidestick" } }}, 3 },
        { "HI-HAT",      {{ { 6, "Closed" }, { 21, "Open 1" }, { 17, "Open 2" }, { 22, "Open 3" }, { 10, "Open 4" }, { 8, "Pedal" } }}, 6 },
        { "RACK TOM",    {{ { 9, "Hit" } }}, 1 },
        { "FLOOR TOM 1", {{ { 7, "Hit" } }}, 1 },
        { "FLOOR TOM 2", {{ { 5, "Hit" } }}, 1 },
        { "CRASH 1",     {{ { 11, "Hit" } }}, 1 },
        { "CRASH 2",     {{ { 14, "Hit" } }}, 1 },
        { "RIDE",        {{ { 12, "Bow" }, { 13, "Bell" }, { 20, "Edge" } }}, 3 },
        { "CHINA",       {{ { 15, "Hit" } }}, 1 },
        { "SPLASH",      {{ { 16, "Hit" } }}, 1 },
        { "CLAP",        {{ { 3, "Hit" } }}, 1 },
        { "TAMBOURINE",  {{ { 18, "Hit" } }}, 1 },
        { "SHAKER",      {{ { 19, "Hit" } }}, 1 },
    }};

    // The piece a slot belongs to, and which of its articulations it is.
    inline int pieceOfSlot (int slot) noexcept
    {
        for (int p = 0; p < numPieces; ++p)
            for (int a = 0; a < pieces[(size_t) p].count; ++a)
                if (pieces[(size_t) p].articulations[(size_t) a].slot == slot)
                    return p;
        return -1;
    }
    inline juce::String articulationName (int slot)
    {
        if (const auto p = pieceOfSlot (slot); p >= 0)
            for (int a = 0; a < pieces[(size_t) p].count; ++a)
                if (pieces[(size_t) p].articulations[(size_t) a].slot == slot)
                    return juce::String (pieces[(size_t) p].name) + (pieces[(size_t) p].count > 1 ? " " + juce::String (pieces[(size_t) p].articulations[(size_t) a].name).toUpperCase() : juce::String());
        return {};
    }

    // THE MIXER (0.20.0): the mic channel a piece is close to (its own; a multi-mic hit's other close mics are bleed).
    inline int homeChannel (int slot) noexcept
    {
        const juce::String name (pieceOfSlot (slot) >= 0 ? pieces[(size_t) pieceOfSlot (slot)].name : "");
        if (name == "KICK") return mixer::kickIn;
        if (name == "SNARE") return mixer::snareTop;
        if (name == "HI-HAT") return mixer::hiHat;
        if (name == "RACK TOM") return mixer::tom1;
        if (name == "FLOOR TOM 1") return mixer::tom2;
        if (name == "FLOOR TOM 2") return mixer::tom3;
        if (name.startsWith ("CRASH") || name == "RIDE" || name == "CHINA" || name == "SPLASH") return mixer::cymbals;
        return mixer::other;
    }
    // Whether a channel is a slot's own (direct sound) or bleed: its close mic(s), or a mic over the whole kit.
    inline bool isDirect (int slot, int channel) noexcept
    {
        if (mixer::hearsWholeKit (channel))
            return true;
        const auto home = homeChannel (slot);
        if (home == mixer::kickIn)
            return channel == mixer::kickIn || channel == mixer::kickOut;
        if (home == mixer::snareTop)
            return channel == mixer::snareTop || channel == mixer::snareBottom;
        return channel == home;
    }

    // STAND-INS: when an articulation is switched off (its ON), what plays its notes instead: the nearest one that's
    // on, in this order (our own choices: the next openness step first, then the piece's main articulation, then a
    // similar piece, as a drummer would play it on a smaller kit). -1 ends a list.
    inline constexpr std::array<std::array<std::int8_t, 6>, numSlots> standIns {{
        { -1, -1, -1, -1, -1, -1 },          // BD
        { 2, 4, -1, -1, -1, -1 },            // RIM (sidestick): the snare, its rimshot
        { 4, 1, -1, -1, -1, -1 },            // SD
        { -1, -1, -1, -1, -1, -1 },          // CLP
        { 2, 1, -1, -1, -1, -1 },            // SD2 (rimshot)
        { 7, 9, -1, -1, -1, -1 },            // LT (floor tom 2): floor tom 1, the rack tom
        { 21, 8, 17, -1, -1, -1 },           // CH: open 1, the pedal
        { 5, 9, -1, -1, -1, -1 },            // MT (floor tom 1)
        { 6, 21, -1, -1, -1, -1 },           // PH: closed
        { 7, 5, -1, -1, -1, -1 },            // HT (rack tom)
        { 22, 17, 21, 6, -1, -1 },           // OH (open 4): open 3, 2, 1, closed
        { 14, 16, 15, -1, -1, -1 },          // CY: crash 2, the splash, the china
        { 20, 13, -1, -1, -1, -1 },          // RD (bow): its edge, its bell
        { 12, 20, -1, -1, -1, -1 },          // RB (bell): the bow
        { 11, 16, 15, -1, -1, -1 },          // CR2: crash 1
        { 14, 11, -1, -1, -1, -1 },          // CHN: the crashes
        { 11, 14, -1, -1, -1, -1 },          // SPL: the crashes
        { 21, 22, 10, 6, -1, -1 },           // HH (open 2): open 1, 3, 4, closed
        { -1, -1, -1, -1, -1, -1 },          // TMB
        { -1, -1, -1, -1, -1, -1 },          // SHK
        { 12, 14, 11, -1, -1, -1 },          // RE (ride edge): the bow, the crashes
        { 17, 6, 22, 10, -1, -1 },           // HO1: open 2, closed, open 3, 4
        { 10, 17, 21, 6, -1, -1 },           // HO3: open 4, 2, 1, closed
    }};

    // THE HI-HAT PEDAL (PR7-D 0.17.0): a controller's hi-hat pedal sends CC 4, 127 fully closed, 0 fully open. A note
    // mapped to HI-HAT (PEDAL) plays the openness step the pedal is at: closed at `steps[0]` and above, open 1 from
    // steps[1], open 2 from steps[2], open 3 from steps[3], below that open 4. Before any CC 4 arrives the pedal
    // counts as closed (so grooves, which use a note for each openness, play as written).
    inline constexpr std::array<int, 5> pedalSteps { 6, 21, 17, 22, 10 };   // closed ... open 4
    inline int pedalStepSlot (int cc, const std::array<int, 4>& steps) noexcept
    {
        for (int i = 0; i < 4; ++i)
            if (cc >= steps[(size_t) i])
                return pedalSteps[(size_t) i];
        return pedalSteps[4];
    }
    // MUTE TAIL (PR7-D 0.17.0): how a cymbal is grabbed to stop it ringing (a held key let go, or pressing harder on a
    // pad that sends aftertouch). Saved by index (drum{k}MuteTail): only append.
    enum class MuteTail { off, noteOff, aftertouch };
    inline const juce::StringArray muteTailNames { "Off", "Note Off", "Aftertouch" };

    // A loaded sample: one or two channels at its own rate.
    struct Sample
    {
        juce::String name;
        juce::String path;                          // the file it came from, if known (6c: < > step through its folder)
        double sampleRate = 48000.0;
        std::vector<std::vector<float>> channels;   // 1 or 2, all the same length

        int length() const noexcept { return channels.empty() ? 0 : (int) channels[0].size(); }

        // DISK STREAMING (0.28.0): a streamed hit's mic holds only its head in `channels`; the rest is read from
        // `stream`'s file while it plays (its output `streamOutput` of the hit's recipe). `frames()`: its whole length.
        std::shared_ptr<const StreamSource> stream;
        int streamOutput = -1;
        juce::int64 frames() const noexcept { return stream != nullptr ? std::max<juce::int64> (length(), stream->length) : length(); }
    };

    // Samples from files (WAV, AIFF, FLAC...), and as text inside presets and projects (16-bit, gzipped, base64).
    namespace sampleFiles
    {
        inline constexpr double maxSeconds = 30.0;   // longer files are cut: these are drum hits, not songs
        std::shared_ptr<const Sample> load (const juce::File&);
        juce::String encode (const Sample&);
        std::shared_ptr<const Sample> decode (const juce::String& name, const juce::String& text);

        // Where each hit in a file starts (in samples), for a file holding several (a kick played nine ways, say):
        // a jump in level well above the quiet just before it, at least 40 ms after the last, moved back to where the
        // hit's rise begins. A single hit gives one start; a file with nothing loud gives none.
        std::vector<int> findHits (const Sample&);
    }

    // THE SAMPLE ENGINE (0.19.0, Instrument.h): a drum played from many recorded hits; and what the kit remembers of
    // each drum's last hits, to choose the next (which hit each layer played last, a random number).
    struct Instrument;
    struct HitMemory
    {
        std::array<std::int16_t, 32> last {};
        std::uint32_t random = 0x9e3779b9u;
    };

    struct SlotSettings
    {
        Model model = Model::kick;
        float gain = 1.0f;                 // VOLUME, as a gain
        float pan = 0.0f;                  // -1 left to 1 right
        float pitch = 0.0f;                // semitones
        float decay = 0.5f, tone = 0.5f, snap = 0.5f;
        bool mute = false, solo = false;
        int choke = 0;                     // 0 none, 1-4
        int insertBus = 0;                 // 0 the mix, 1-4 bus A-D
        int output = 0;                    // 0 MAIN, 1-15 the other outputs (on a bus: the bus's instead)
        std::array<float, 2> send { 1.0f, 1.0f };   // DELAY, REVERB (only while that effect is on)
        const Sample* sample = nullptr;    // SAMPLE's, or nothing
        float start = 0.0f, end = 1.0f;    // SAMPLE: the part played, as shares of the file (0.21.1)
        // REVERSE (the sequencer, 6b): what a reversed hit plays backwards. A SAMPLE slot's own sample; a drum synth's
        // hit rendered ahead at these settings ("baked": its pitch and shaping already in it), or nothing yet.
        const Sample* reverseSource = nullptr;
        bool reverseBaked = false;
        // THE SAMPLE EDITOR (6c), for every slot:
        float trim = 1.0f;                 // GAIN, as a gain (for samples recorded too loud or too quiet)
        bool mono = false;                 // a hit fades the slot's earlier ones (POLY: they overlap)
        float delay = 0.0f;                // seconds: above 0 the hit comes later; below, the sequencer starts it
                                           // that much early so that point in it (its anchor) lands on the step
        float lowPass = 20000.0f, highPass = 20.0f;   // 6 dB/oct, Hz (20 kHz / 20 Hz: off)
        const mod::Shape* envelope = nullptr;         // the drawn envelope across the hit (null: none)
        float envelopeSeconds = 0.0f;      // a drum synth's hit length, which its envelope spans (SAMPLE: START to END)
        // ARTICULATIONS (0.17.0).
        bool active = true;                // ON: off, its notes play its stand-in (standIns)
        MuteTail muteTail = MuteTail::off;
        // 0.18.0: its VELOCITY CURVE (null: straight) and VELOCITY GATE (hits outside velMin to velMax, as they come
        // in, don't play it).
        const VelocityCurve* velocityCurve = nullptr;
        float velMin = 0.0f, velMax = 1.0f;
        // THE SAMPLE ENGINE (0.19.0): SAMPLE with an instrument plays its layers and round-robins (null: `sample`, one
        // file, as before); HIT VARIATION and the limits.
        const Instrument* instrument = nullptr;
        bool randomize = true, adjacent = true, velocityToVolume = true;
        int layerLimit = 0;                // 0: every hit
        int voices = 6;                    // this drum's hits sounding at once (the oldest fades past it)
        // THE ACOUSTIC MODELS (0.24.0): where the head is struck (0 the centre, 1 the edge), the shell's ring (the
        // kick: its front head), and how much each hit differs from the last (a drummer's hits aren't identical).
        float position = 0.3f, shell = 0.4f, variation = 0.3f;
        // SIMULATED MICS (0.26.0): while the kit's are on, this drum is heard through them (else its own way).
        bool miked = true;
        // ITS LAYER (0.29.0): a sample (or folder of hits) of the user's played with every hit, at `layerGain` (velocity
        // as the drum's), into the mixer's mic channel `layerChannel` (-1: the drum's own way, its VOLUME, PAN, INSERT...).
        const Instrument* layer = nullptr;
        float layerGain = 1.0f;
        int layerChannel = -1;

        bool filtered() const noexcept { return lowPass < 19999.0f || highPass > 20.01f; }
        // The sequencer's lead for the anchor (seconds early).
        double anchorSeconds() const noexcept { return delay < 0.0f ? -(double) delay : 0.0; }
    };

    // An envelope that changes nothing (none, or flat at the top) isn't worked out at all.
    inline bool envelopeShapes (const mod::Shape* shape) noexcept
    {
        if (shape == nullptr || shape->count <= 0)
            return false;
        for (int i = 0; i < shape->count; ++i)
            if (shape->points[(size_t) i].y < 0.9999f)
                return true;
        return false;
    }

    // A hit's own touches (the sequencer's notes): added to the slot's pan and pitch, cut after a gate, played
    // backwards; velocityGain, if set (0 or more), replaces the keys' velocity curve.
    struct HitOptions
    {
        float pan = 0.0f, pitch = 0.0f;
        int gateSamples = -1;             // -1: it rings out
        drumseq::Reverse reverse = drumseq::Reverse::off;
        float velocityGain = -1.0f;
    };

    struct KitSettings
    {
        bool on = false;
        int rootKey = 36;                  // ROOT: with the ROOT layout, the keys rootKey to rootKey + 19 play the slots
        // THE NOTE MAP (0.2.0): with a layout other than ROOT, `keys` gives each MIDI note's slot (-1: none), and the
        // sequencer's KEY pattern keys are off (the layouts use those notes for drums).
        bool mapped = false;
        std::array<std::int8_t, 128> keys {};
        float velocity = 1.0f;             // how much the keys' velocity changes the level, 0 to 1
        float gain = 1.0f;                 // the kit's LEVEL
        std::array<bool, 2> sendOn {};     // DELAY, REVERB switched on
        std::array<SlotSettings, numSlots> slots;
        std::array<int, 4> pedalSteps { 102, 77, 51, 26 };   // THE HI-HAT PEDAL's steps (CC 4 values, see above)

        // In `keys`, a note that plays the hi-hat at the pedal's openness.
        static constexpr std::int8_t hatPedal = 100;

        // THE MIXER (0.20.0): the mic channels (multi-mic drums only) and the bleed from each piece into each.
        std::array<mixer::ChannelSettings, mixer::numMicChannels> channels {};   // 0.23.0: USER 1-8 too
        const mixer::BleedMatrix* bleed = nullptr;   // null: every cell 0 dB, on
        const mixer::MicRoutes* micRoutes = nullptr; // 0.23.0: ROUTE INSTRUMENT MICROPHONES (null: every mic AUTO)
        // 0.21.0: the buses, each strip's sends (null: none), and MUTE MODE.
        std::array<mixer::BusSettings, mixer::numBuses> buses {};
        const mixer::SendTable* sends = nullptr;
        mixer::MuteMode muteMode = mixer::MuteMode::preSend;
        mixer::EffectHost* effects = nullptr;   // 0.22.0: the strips' effects (null: none)
        // 0.28.0: DISK STREAMING (null: nothing streams; streamed hits play their heads only), and whether to wait for
        // the disk (bouncing) rather than play silence when it's late (live).
        Streamer* streamer = nullptr;
        bool waitForDisk = false;
        // 0.26.0: SIMULATED MICS AND ROOM: synth drums and single samples through the mixer's mic channels.
        micsim::Settings micSim;

        // MIDI IN (0.18.0): only for MIDI from the DAW or a controller (not grooves, the song or the sequencer).
        int channel = 0;                                // 0: any, 1-16
        const VelocityCurve* inputCurve = nullptr;      // every incoming note's velocity first (null: straight)
        // CC ZONES: a positional pad's note playing a different articulation by where it's hit, which the pad sends as
        // a controller (`cc`) just before the note. The controller's range is cut at `bounds` (rising) into `count`
        // zones, each playing its slot (-1: what the note plays anyway).
        struct CcZones
        {
            std::int8_t note = -1, cc = 16, count = 0;
            std::array<std::int8_t, 4> slots { -1, -1, -1, -1 };
            std::array<std::uint8_t, 3> bounds { 43, 86, 120 };

            int zoneOf (int value) const noexcept
            {
                int z = 0;
                while (z < count - 1 && value >= bounds[(size_t) z])
                    ++z;
                return z;
            }
        };
        static constexpr int maxZoneNotes = 8;
        std::array<CcZones, maxZoneNotes> zones {};
        const CcZones* zonesFor (int note) const noexcept
        {
            for (const auto& z : zones)
                if (z.note == note && z.count >= 2)
                    return &z;
            return nullptr;
        }

        // The slot a note plays (-1: none), the pedal at `pedal` (CC 4, 127 closed), before stand-ins.
        int slotFor (int note, int pedal = 127) const noexcept
        {
            if (mapped)
            {
                const auto k = note >= 0 && note < 128 ? keys[(size_t) note] : (std::int8_t) -1;
                return k == hatPedal ? pedalStepSlot (pedal, pedalSteps) : k;
            }
            return note >= rootKey && note < rootKey + numSlots ? note - rootKey : -1;
        }
        // What plays for a slot: itself if ON, else its first stand-in that's ON, else nothing (-1).
        int playsAs (int slot) const noexcept
        {
            if (slot < 0 || slot >= numSlots)
                return -1;
            if (slots[(size_t) slot].active)
                return slot;
            for (const auto s : standIns[(size_t) slot])
                if (s >= 0 && slots[(size_t) s].active)
                    return s;
            return -1;
        }
    };

    // Where the kit plays into: the mix, each send (null while its effect is off), each insert bus (null if none).
    struct Outputs
    {
        float* left = nullptr;
        float* right = nullptr;
        std::array<float*, numSends> sendLeft {}, sendRight {};
        std::array<float*, numInsertBuses> busLeft {}, busRight {};
        // The other outputs (OUT 2 to 16): null while the DAW hasn't switched that output on, and a drum sent there
        // plays in the mix instead, so nothing goes silent.
        std::array<float*, numOutputs - 1> auxLeft {}, auxRight {};
    };

    // A hit on a drum at a sample (THE GROOVE PLAYER's preview: its notes already turned into drums).
    struct SlotHit
    {
        int offset = 0;          // from the first sample of the piece being played
        int slot = 0;
        float velocity = 1.0f;   // 0 to 1, as a note's
    };

    // A zero-delay-feedback state-variable filter (Zavalishin's), set once per hit.
    struct Svf
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;

        void set (double hz, double q, double sampleRate) noexcept;
        void process (float x) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }
    };

    // One hit sounding.
    struct Voice
    {
        bool active = false;
        int slot = -1;
        int channel = -1;                  // THE MIXER (0.20.0): a multi-mic hit's mic channel (-1: the slot's own way)
        bool primary = true;               // the first of a hit's voices (a hit counts once for VOICES)
        float micGain = 1.0f;              // its bleed / close-mic level
        bool bleed = false;                // 0.21.0: bleed in its channel (not the drums it's meant for): sends tell them apart
        int releaseLeft = -1;              // the channel's RELEASE: samples until it fades (-1: never)
        Model model = Model::kick;
        std::uint64_t age = 0;
        int choke = 0;

        double sampleRate = 48000.0;
        double t = 0.0;                    // seconds since the hit
        float level = 1.0f;                // velocity, and the model's own scaling
        float fade = 1.0f, fadeStep = 0.0f;   // stolen or choked: fading out

        // The models' working parts.
        std::array<double, 6> phase {}, increment {};
        double baseHz = 0.0, bend = 0.0;
        float env = 0.0f, envCoef = 0.0f, env2 = 0.0f, env2Coef = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;
        float click = 0.0f, clickCoef = 0.0f, noiseLevel = 0.0f, mix = 0.0f;
        int burst = 0;
        Svf f1, f2, f3;
        Noise noise;
        std::array<double, 2> pingPhase {};   // RIDE's stick ping, beside the metal
        // THE ACOUSTIC MODELS (0.24.0): the head's (and shell's, front head's) modes, each a sine at its frequency
        // dying at its own rate; the stick's or beater's contact as their shared rise; the pitch glide of a hard hit;
        // the snare wires following the head.
        struct Modes
        {
            static constexpr int max = 48;   // 28 for the drums, 40 + the bell's 4 for the cymbals (0.25.0)
            int count = 0;
            // Each a decaying sine made by a two-term recurrence (y = c y1 - r2 y2): two multiplies a sample. Its
            // tuning (c, for the glide) and its envelope (env, for skipping it once inaudible) every 16 samples.
            std::array<double, max> hz {};
            std::array<float, max> amp {}, env {}, coef {}, coef16 {}, glides {}, y1 {}, y2 {}, c {}, r2 {};
            int countdown = 0;
            float rise = 0.0f, riseCoef = 0.0f, glide = 0.0f, glideEnv = 0.0f, glideCoef = 0.0f;
            float wires = 0.0f, wireThreshold = 0.0f, stick = 0.0f;
            float loudest = 1.0f;   // its loudest mode now (amplitude x envelope): it ends once that's inaudible
        };
        Modes modes;
        // THE ACOUSTIC CYMBALS (0.25.0): beside their modes, the WASH (the shimmer a struck cymbal blooms into: noise in
        // eight bands, built up after the hit, its highs dying first, slowly shimmering), the china's and stack's
        // trash (a soft clip), the hi-hat's plates rattling together.
        struct Cymbal
        {
            std::array<Svf, 8> bands;
            std::array<float, 8> weights {};
            float build = 0.0f, buildCoef = 0.0f, washEnv = 0.0f, washCoef = 0.0f, washLevel = 0.0f;
            float lp = 0.0f, lpCoef = 0.0f, lpFall = 0.0f, lpFloor = 0.0f;   // the wash's brightness, falling
            double lfoPhase = 0.0, lfoStep = 0.0;
            float shimmer = 0.0f, drive = 0.0f, highPass = 0.0f, highState = 0.0f;
            float rattle = 0.0f, rattleGain = 1.0f;
            int rattleLeft = 0, rattleMin = 1, rattleSpread = 1;
            float thump = 0.0f, thumpCoef = 0.0f;
            double thumpPhase = 0.0, thumpHz = 0.0;
        };
        Cymbal cymbal;

        // SAMPLE's.
        const Sample* sample = nullptr;
        // 0.28.0: a streamed sample's stream (-1: its head only), its mic in the hit's recipe.
        Streamer* streamer = nullptr;
        int streamId = -1, streamOutput = -1;
        bool waitForDisk = false;
        void enableStream (Streamer*, int id, bool wait) noexcept;
        void renderStreamed (float* left, float* right, int numSamples) noexcept;
        double position = 0.0, step = 1.0, endPosition = 0.0, endFade = 0.0;   // END: where it stops, fading over endFade
        float lowPole = 0.0f, highPole = 0.0f;
        std::array<float, 2> lowState {}, highState {};
        float attackTime = 0.0f, spike = 0.0f;
        bool reversed = false;
        double startPosition = 0.0;
        // The note's own pan (a balance on top of the slot's) and gate.
        float panLeft = 1.0f, panRight = 1.0f;
        int gateLeft = -1;
        // The drawn envelope (6c): where it is across the hit (0 to 1), gliding between points worked out every
        // 8 samples; DELAY's wait before the hit starts.
        const mod::Shape* shape = nullptr;
        double shapeX = 0.0, shapeDx = 0.0;
        float shapeGain = 1.0f, shapeStep = 0.0f;
        int shapeCountdown = 0, waitLeft = 0;

        void start (const SlotSettings&, int slotIndex, float velocityGain, double sampleRate, std::uint32_t seed,
                    const HitOptions& = {}) noexcept;
        // Adds this hit's next `numSamples` into left / right (the same for the mono models).
        void render (float* left, float* right, int numSamples) noexcept;
        void fadeOut (double seconds) noexcept;

    private:
        void setTone (double tone, double rate) noexcept;   // SAMPLE's TONE: a low-pass below the middle, a high-pass above
        void startAcoustic (const SlotSettings&, float velocityGain, double rate) noexcept;   // 0.24.0
        void startCymbal (const SlotSettings&, float velocityGain, double rate) noexcept;     // 0.25.0
        float nextModes() noexcept;        // the modes' next sample (both)
        void startShape (const SlotSettings&, double regionSamples) noexcept;
        bool advanceShape() noexcept;      // the envelope's next sample; false once it has ended at nothing
        float next() noexcept;             // the mono models
        bool finished() const noexcept;
    };

    class Kit
    {
    public:
        // 0.20.0: up to 48 hits at once (as before), and 192 voices for them, a multi-mic hit being a voice per mic. A
        // kit of synth drums or single samples plays as in 0.19.0 (each hit one voice, the 49th cutting the oldest).
        static constexpr int maxVoices = 192, maxHits = 48, maxPerSlot = 6;

        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // Plays the kit's notes in `midi` (positions from midiStart) over the next numSamples, adding into `outputs`,
        // and THE DRUM SEQUENCER's beat (6b): SONG from the song's position `ppq` at the first sample (below 0: the
        // song isn't playing), KEY while one of the four keys under the kit is held (they choose pattern A-D).
        // `hits` (in time order, `numHits` of them): drums to play at given samples, as notes would (the groove preview).
        void process (const Outputs&, int numSamples, const juce::MidiBuffer& midi, int midiStart, const KitSettings&,
                      const drumseq::Settings& sequencer = {}, double ppq = -1.0, double bpm = 120.0,
                      const SlotHit* hits = nullptr, int numHits = 0) noexcept;

        // The sequencer now (any thread): where it is in its pattern, in steps (-1: stopped), and which pattern.
        double getSequencerPosition() const noexcept { return sequencerPosition.load(); }
        int getSequencerPattern() const noexcept { return sequencerPattern.load(); }
        // KEY: the four keys under ROOT start pattern A to D (lowest A).
        static int patternForKey (int note, int rootKey) noexcept
        {
            const auto k = note - (rootKey - drumseq::numPatterns);
            return k >= 0 && k < drumseq::numPatterns ? k : -1;
        }
        // A reversed hit's length, in seconds (0: nothing to reverse).
        static double reverseSeconds (const SlotSettings&, float notePitch, double sampleRate) noexcept;
        // A hit straight away (for tests and the page's preview): velocity 0 to 1. Its stand-in plays if it's off.
        void trigger (int slot, float velocity, const KitSettings&) noexcept;
        // MUTE TAIL: the slot's piece's ringing hits grabbed (they stop over about 80 ms).
        void grab (int slot) noexcept;
        // THE SAMPLE ENGINE: which hit a slot's instrument played last (layer, hit), -1, -1: none.
        std::pair<int, int> getLastChoice (int slot) const noexcept
        {
            const auto v = lastChoice[(size_t) juce::jlimit (0, numSlots - 1, slot)].load() - 1;
            return v < 0 ? std::pair { -1, -1 } : std::pair { v / 1000, v % 1000 };
        }
        // THE HI-HAT PEDAL: the last CC 4 (127 closed) heard, for the page's meter.
        int getPedal() const noexcept { return pedal.load(); }

        // THE MIDI MONITOR (0.18.0): what came in from the DAW or a controller and what the kit made of it. Filled on
        // the audio thread, read on the message thread (popMidiEvents), lock-free; the oldest are lost if nobody reads.
        struct MidiEvent
        {
            enum class Type : std::uint8_t { noteOn, noteOff, controller, aftertouch } type = Type::noteOn;
            std::uint8_t channel = 1;
            std::uint8_t number = 0;     // the note, or the controller
            std::uint8_t value = 0;      // the velocity, or the controller's value
            std::int8_t slot = -1;       // the drum it played (after zones and stand-ins), -1: none
            std::int16_t valueOut = -1;  // the velocity after the incoming curve (-1: gated, or not a note-on)
        };
        int popMidiEvents (MidiEvent* into, int max) noexcept;

        int getNumActiveVoices() const noexcept;
        bool isSounding (int slot) const noexcept;
        // Where a slot's last hit is, 0 to 1 of its first second (for the page), or -1.
        float getHitAge (int slot) const noexcept;

        // One hit rendered alone, `seconds` long (for the page's display and tests): its envelope, GAIN and LP / HP
        // in it, not DELAY.
        static juce::AudioBuffer<float> renderHit (const SlotSettings&, float velocity, double sampleRate, double seconds);
        // How long a slot's hit lasts as it is (to -60 dB under its peak, at most 4 s), without its envelope: what a
        // drum synth's envelope spans (SAMPLE: exactly START to END at its pitch).
        static double hitSeconds (const SlotSettings&);

        // Seeds the hits' noise (tests), so a hit sounds the same each run.
        void seedRandom (std::uint32_t seed) noexcept { noiseSeed = seed == 0 ? 1u : seed; }

    private:
        void render (const Outputs&, int offset, int numSamples, const KitSettings&) noexcept;
        void renderChannels (const Outputs&, int offset, int numSamples, const KitSettings&) noexcept;   // THE MIXER
    public:
        // THE MIXER: each channel's peak since the page last asked (any thread), and whether it played anything.
        float takeChannelPeak (int channel) noexcept { return channelPeaks[(size_t) juce::jlimit (0, mixer::numMicChannels - 1, channel)].exchange (0.0f); }
        float takeBusPeak (int bus) noexcept { return busPeaks[(size_t) juce::jlimit (0, mixer::numBuses - 1, bus)].exchange (0.0f); }
    private:
        // Each channel's direct and bleed sound apart (left, right, bleed left, bleed right), then the buses' (0.21.0).
        std::vector<float> channelScratch, voiceScratch, busScratch;
        std::array<std::atomic<float>, mixer::numMicChannels> channelPeaks {};
        std::array<std::atomic<float>, mixer::numBuses> busPeaks {};
        // 0.22.0: samples a strip with effects keeps running after its sound stops (their tails).
        std::array<int, mixer::numStrips> stripTail {};
        // SIMULATED MICS (0.26.0): the lines and room, each piece's taps into each mic (left, right), and the lines a
        // source mic's sound is gathered in before its colour (direct L / R, bleed L / R).
        micsim::Simulator micSim;
        std::array<std::array<std::array<micsim::Tap, 2>, mixer::numChannels>, numPieces> simTaps {};
        std::vector<float> simScratch;
        void simulateMics (const KitSettings&, int numSamples, std::array<bool, mixer::numMicChannels>& used) noexcept;
        static void filterSlot (const SlotSettings&, float* left, float* right, int numSamples, std::array<float, 2>& low,
                                std::array<float, 2>& high, double sampleRate) noexcept;
        void startHit (int slot, float velocity, const KitSettings&, const HitOptions& = {}) noexcept;
        void runSequencer (const Outputs&, int from, int to, const KitSettings&, const drumseq::Settings&, double ppq, double bpm) noexcept;
        void patternKey (int pattern, bool down, float velocity) noexcept;

        struct SequencedHit
        {
            int offset = 0;
            drumseq::Note note;
        };
        std::vector<SequencedHit> sequenced = std::vector<SequencedHit> (512);
        bool keyRunning = false;
        double keyClock = 0.0;                       // KEY: steps since the key went down
        int keyPattern = 0;
        float keyVelocity = 1.0f;
        std::vector<int> heldPatterns = std::vector<int> (8);
        int numHeld = 0;
        std::atomic<double> sequencerPosition { -1.0 };
        std::atomic<int> sequencerPattern { 0 };
        static bool audible (const KitSettings&, int slot) noexcept;   // MUTE / SOLO

        double sampleRate = 48000.0;
        std::vector<Voice> voices = std::vector<Voice> (maxVoices);
        std::vector<float> scratch;                     // 2 x numSlots x the block
        std::array<float, numSlots> slotGain {};        // MUTE / SOLO, glided
        std::array<std::array<float, 2>, numSlots> lowState {}, highState {};   // LP / HP (6c)
        std::array<double, numSlots> lastHit {};        // seconds since each slot's last hit (-1: none)
        std::uint64_t ages = 0;
        std::uint32_t noiseSeed = 0x2545f491u;
        std::array<HitMemory, numSlots> hitMemory {};   // THE SAMPLE ENGINE's round-robins (0.19.0)
        std::array<HitMemory, numSlots> layerMemory {}; // 0.29.0: each drum's layer's
        std::array<std::atomic<int>, numSlots> lastChoice {};   // layer x 1000 + hit + 1 (0: none)
        std::atomic<int> pedal { 127 };   // CC 4: before any arrives, closed
        std::array<std::uint8_t, 128> controllers {};   // each controller's last value (CC ZONES)
        void pushMidiEvent (const MidiEvent&) noexcept;
        juce::AbstractFifo monitorFifo { 512 };
        std::vector<MidiEvent> monitorEvents = std::vector<MidiEvent> (512);
        int blockSize = 0;
    };
}
