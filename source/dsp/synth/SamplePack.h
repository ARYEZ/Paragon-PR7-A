// SAMPLE PACKS (PR7-D 0.33.0): a folder of a multi-mic drum library's WAV files, named piece by piece, mic by mic,
// velocity layer by layer and round-robin by round-robin, as one kit: every sound on its slot, every mic on the mixer's
// channel for it. Our own reader; first written for the owner's copy of one commercial djent library (its INDIVIDUAL
// MICS folder), whose names go "<PREFIX>_<PIECE>[_<ARTICULATION>][_<MIC>]_V<layer>_RR<round>.wav":
//
//   KICK_DYN_KICK_V01_RR1   SNR_CROSS_OH_V02_RR5   TOM3_RM_V04_RR2   HATS_OPEN_HALF_OH_V01_RR8   CRASH1_CHOKE_SPOT_V03_RR4
//
//   - The prefix all its files share ("JAD_DJENT") is dropped; the mic words are taken out (OH, RM [CLOSE], SPOT, DYN,
//     DYN1, DYN2, CON, SUB, FX, TRIGGER, BTM...; a hi-hat with none: its close mic); what's left names the sound
//     (KICK, SNR CROSS, TOM3, HATS OPEN HALF, CRASH1 CHOKE...), which our rules put on a slot.
//   - Mics to channels: OH the overheads, RM / ROOM the room, AMB the ambience, the sound's own close mic its channel
//     (kick DYN: KICK IN, CON: KICK OUT; snare DYN1 / TOP: SNARE TOP, BTM: SNARE BOTTOM; toms 1-3: TOM 1-3; hats: HI-HAT;
//     cymbals' SPOT: CYMBALS); the rest (a kick's SUB, FX and TRIGGER, a snare's second top mic and FX, tom 4's close
//     mic) each a USER channel of its own, named (in a fixed order, so a preset finds them where it left them).
//   - V01 the softest layer (checked by loudness as it loads: turned round if the pack counts the other way); the
//     velocities shared out evenly; the rounds each layer's hits.
//   - Streamed (each hit's first 0.2 s in memory, the rest read from its files as it plays), as the DrumGizmo kits.
// A pack's own folder of one-shots (pre-mixed) is left alone when it has an INDIVIDUAL MICS folder.
#pragma once

#include "dsp/synth/Instrument.h"

#include <map>
#include <optional>

namespace synth::drums::samplepack
{
    struct File
    {
        juce::File file;
        int channel = -1;      // mixer channel (0-12 named, 13-20 USER)
        int layer = 1, round = 1;
    };

    struct PackInfo
    {
        juce::String name;                         // the pack's folder's name
        juce::File folder;                         // where its files were found (its INDIVIDUAL MICS folder, if it has one)
        std::map<int, std::vector<File>> slots;    // by slot
        juce::StringArray userChannels;            // USER 1, 2... : their names ("KICK SUB", "SNARE TOP 2"...)
        juce::StringArray unused;                  // names our rules didn't place (a sound, or a mic past the last USER channel)
        bool valid() const noexcept { return ! slots.empty(); }
    };

    // The folder's files, by name only (no audio read): what goes where.
    // `cymbalChannels` (0.34.0, new imports): each cymbal's spot mic on a USER channel of its own (else on CYMBALS, as
    // 0.33.0's imports, which load as they were made).
    PackInfo scan (const juce::File& folder, bool cymbalChannels = false);

    // A slot's sounds as an instrument (streamed; `headSeconds` of each in memory): null if it has none, or none
    // could be read (why in `problems`).
    std::shared_ptr<const Instrument> load (const PackInfo&, int slot, juce::StringArray& problems, double headSeconds = 0.2);

    // Our rules, for tests and the import's summary: the slot a sound's name plays (-1: none), and a mic word's channel
    // for that slot (-1: a USER channel of its own; -2: not a mic word).
    int slotFor (const juce::StringArray& soundWords);
    int channelFor (const juce::String& micWords, int slot);
}
