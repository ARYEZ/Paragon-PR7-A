// THE SONG TRACK (PR7-D 0.8.0, plan stage d, the song track part 1): grooves laid out as a song. Our own design,
// inspired by the song track of commercial drum software (chapter 7 of the manual the owner sent): blocks, each a
// groove copied in (its notes already as the kit's drums), placed bar by bar, played with the DAW's song position.
//
// Placing (our rule): a block dropped inside another goes before or after it (the nearer edge), and the blocks after it
// move along to make room, so nothing overlaps and nothing is cut; dropped past the end it stays where it's dropped (a
// gap before it). Lengthening a block pushes the ones after it; shortening leaves a gap.
#pragma once

#include <cstdint>

#include "grooves/Groove.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace song
{
    struct Hit
    {
        double beat = 0.0;   // quarter notes (in a block: from its start; in a song's events: from the song's)
        int slot = 0;        // the kit's drum
        float velocity = 1.0f;
        bool operator== (const Hit& o) const noexcept
        {
            return juce::exactlyEqual (beat, o.beat) && slot == o.slot && juce::exactlyEqual (velocity, o.velocity);
        }
    };

    struct Block
    {
        juce::String name;
        juce::File source;                       // the groove it came from (for FIND SIMILAR and the like)
        grooves::Part part = grooves::Part::beat;   // SONG PART: its colour and kind
        double start = 0.0;                      // beats from the song's start
        double length = 4.0;                     // beats on the track
        double loopLength = 4.0;                 // the groove's own length: played round and round to fill `length`
        std::vector<Hit> hits;                   // within [0, loopLength), in time order
        bool muted = false;

        double end() const noexcept { return start + length; }
        bool operator== (const Block& o) const noexcept
        {
            return name == o.name && source == o.source && part == o.part && juce::exactlyEqual (start, o.start)
                && juce::exactlyEqual (length, o.length) && juce::exactlyEqual (loopLength, o.loopLength) && hits == o.hits
                && muted == o.muted;
        }
    };

    // TEMPO CHANGES (0.11.0): from `beat`, `bpm`; with `ramp`, gliding (evenly, beat by beat) to the next one's tempo.
    struct TempoPoint
    {
        double beat = 0.0, bpm = 120.0;
        bool ramp = false;
        bool operator== (const TempoPoint& o) const noexcept
        {
            return juce::exactlyEqual (beat, o.beat) && juce::exactlyEqual (bpm, o.bpm) && ramp == o.ramp;
        }
    };
    // TIME SIGNATURE CHANGES (0.11.0): from `beat` (a bar line), bars of numerator / denominator.
    struct Meter
    {
        double beat = 0.0;
        int numerator = 4, denominator = 4;
        double beatsPerBar() const noexcept { return numerator * 4.0 / juce::jmax (1, denominator); }
        bool operator== (const Meter& o) const noexcept
        {
            return juce::exactlyEqual (beat, o.beat) && numerator == o.numerator && denominator == o.denominator;
        }
    };

    struct Song
    {
        int numerator = 4, denominator = 4;      // the first bars' time signature
        std::vector<Block> blocks;               // in time order, never overlapping
        std::vector<Meter> meters;               // time signature changes after the start, in order, each on a bar line
        std::vector<TempoPoint> tempos;          // in order; none: the DAW's tempo
        // FOLLOW DAW (0.12.0): on, the song plays with the DAW's song position while it plays; off, only with PLAY (the
        // DAW playing or not): for when the song's MIDI is on a DAW track, which would play every hit a second time.
        bool followDaw = true;
        // 0.44.0: THE LOOP AREA (PLAY goes back to `loopStart` when it reaches `loopEnd`, on bar lines; off or empty: no
        // loop) and THE CLICK (a metronome while the song plays: off, only while REC is armed, or always).
        bool loopOn = false;
        double loopStart = 0.0, loopEnd = 0.0;
        enum class Click { off, recording, always };
        Click click = Click::recording;
        // 0.45.0: THE COUNT-IN: PLAY pressed with REC armed waits a bar (the click counting it in) before the song starts.
        bool countIn = true;
        bool hasLoop() const noexcept { return loopOn && loopEnd > loopStart + 1.0e-9; }

        double beatsPerBar() const noexcept { return numerator * 4.0 / juce::jmax (1, denominator); }   // the first bars'
        double endBeat() const noexcept { return blocks.empty() ? 0.0 : blocks.back().end(); }
        bool isEmpty() const noexcept { return blocks.empty(); }
        bool operator== (const Song& o) const noexcept
        {
            return numerator == o.numerator && denominator == o.denominator && blocks == o.blocks && meters == o.meters
                && tempos == o.tempos && followDaw == o.followDaw && loopOn == o.loopOn && loopStart == o.loopStart
                && loopEnd == o.loopEnd && click == o.click && countIn == o.countIn;
        }

        // BARS (0.11.0), with the time signature changes: the time signature at a beat; every bar line from the start
        // to `upTo` (and the one after it); which bar a beat is in (0: the first).
        Meter meterAt (double beat) const noexcept;
        double beatsPerBarAt (double beat) const noexcept { return meterAt (beat).beatsPerBar(); }
        std::vector<double> barLines (double upTo) const;
        int barAt (double beat) const;
        // A time signature change at the bar line nearest `beat` (one there: changed); removed; the first bars' set.
        void setMeter (double beat, int numerator, int denominator);
        void removeMeter (double beat);

        // TEMPO (0.11.0): the tempo at a beat, and the time at a beat and back (seconds from the start); `daw`: the
        // tempo when the song has no tempo changes.
        double bpmAt (double beat, double daw) const noexcept;
        double secondsAt (double beat, double daw) const noexcept;
        double beatAtSeconds (double seconds, double daw) const noexcept;
        int addTempo (double beat, double bpm);           // (one on that beat: changed) returns its index
        void moveTempo (int index, double beat, double bpm);   // kept between its neighbours
        void removeTempo (int index);

        // EDITING (each keeps the blocks in order and apart, as above). Beats are snapped to whole bars.
        int place (Block, double atBeat);                 // returns where it went (its index)
        int move (int index, double toBeat);              // returns where it went
        void resize (int index, double newLength);        // at least a bar
        int duplicate (int index);                        // a copy right after it
        void remove (int index);
        int blockAt (double beat) const noexcept;         // -1: none there
        double snap (double beat) const noexcept;         // to the nearest bar line, not before 0

        // SPLIT (0.9.0): the block cut in two at the bar line nearest `atBeat` (inside it), each half its own notes as
        // played there (no longer repeating); returns the second half's index (-1: no bar line inside it).
        int split (int index, double atBeat);
        // MERGE WITH NEXT (0.9.0): the block and the one after it as one (named and coloured as the first; a gap between
        // them kept as rests); returns its index (-1: no block after it).
        int mergeWithNext (int index);

        // Every hit the song plays (muted blocks left out), in time order, each block's groove repeated to fill it.
        std::vector<Hit> events() const;
        // The whole song as a MIDI file: its tempo changes (none: `bpm`; a glide written as steps a 16th apart), its time
        // signatures, each drum on `noteFor`'s note (below 0: left out).
        juce::MidiFile toMidi (double bpm, const std::function<int (int slot)>& noteFor) const;

        std::unique_ptr<juce::XmlElement> toXml() const;
        static Song fromXml (const juce::XmlElement&);

    private:
        void settle (int keepFirst = -1);   // in order, then each block after the one before it
        void tidyMeters();                  // in order, each on a bar line, none repeating the one before
    };

    // IMPORT MIDI FILE (0.11.0): a whole song's MIDI (every track) through `layout` as the kit's drums: one block, or one a
    // marker (a DAW's "Verse", "Chorus"...: named after it, its song part from the name); its tempo changes and time
    // signatures if `withTempo`. Empty if the file has no notes the kit plays or isn't timed in beats.
    Song importMidi (const juce::MidiFile&, const synth::drums::notemap::Map& layout, const juce::String& name, bool withTempo);

    // A block from a groove: its notes through `layout` as drums (those no drum plays left out), at `factor` (2x: the
    // notes twice as close) and each velocity times `velocityScale`; named after the file; its SONG PART from the names.
    Block blockFrom (const grooves::Groove&, const synth::drums::notemap::Map& layout, grooves::TempoFactor factor = grooves::TempoFactor::normal,
                     float velocityScale = 1.0f);
    // CHANGE TEMPO on a block: its notes and lengths times 1 / factor (2x: twice as fast, half as long).
    void changeTempo (Block&, grooves::TempoFactor);

    // EDITING INSIDE A BLOCK (0.9.0, the song track part 2). Our own design, inspired by the block editing in chapter 7
    // of the manual the owner sent. Each works on the block's groove (one time round: `loopLength`), on the drums in
    // `slots` (bit n: the kit's drum n; allSlots: every drum), and keeps its hits in time order.
    // 0.36.0: 64 bits (36 drums no longer fit 32).
    using SlotMask = std::uint64_t;
    inline constexpr SlotMask slotBit (int slot) noexcept { return slot >= 0 && slot < 64 ? SlotMask (1) << slot : 0; }
    inline constexpr SlotMask allSlots = (SlotMask (1) << synth::drums::numSlots) - 1u;
    static_assert (synth::drums::numSlots < 64);
    inline int highestSlot (SlotMask m) noexcept { for (int k = 63; k >= 0; --k) if ((m & slotBit (k)) != 0) return k; return -1; }
    SlotMask slotsOf (unsigned parts) noexcept;                  // grooves::DrumPart bits -> the drums in them
    SlotMask slotsIn (const Block&) noexcept;                    // the drums the block plays
    std::vector<Hit> unrolled (const Block&);                    // every hit over its whole length (repeats written out)
    void flatten (Block&);                                       // the repeats written out: loopLength = length
    void removeDrums (Block&, SlotMask slots);
    // The clip's notes on those drums in place of the block's own (the clip repeated, or cut, to the block's groove).
    void pasteDrums (Block&, const Block& clip, SlotMask slots);
    // Each note to the nearest `grid` beats (a note pushed to the groove's end goes round to its start); two on the same
    // drum and beat become one (the louder).
    void quantize (Block&, double grid, SlotMask slots = allSlots);
    // Each velocity times `factor` (the balance between them kept), between 1/127 and full.
    void scaleVelocity (Block&, float factor, SlotMask slots = allSlots);
    // Every note on drum `from` played by drum `to` instead, with `to`'s own (on the same beat: one, the louder).
    void moveDrum (Block&, int from, int to);
    void addHit (Block&, Hit);                                   // one more (replacing one on that drum and beat)
    // 0.45.0: a note moved along its row to `toBeat` (wrapped into the loop); one already there is replaced. False: none at `fromBeat`.
    bool moveHit (Block&, int slot, double fromBeat, double toBeat);
    bool removeHitNear (Block&, int slot, double beat, double within);   // the nearest within that many beats

    // SONG PART colours (our own palette, a colour each kind).
    juce::Colour colourOf (grooves::Part) noexcept;
}
