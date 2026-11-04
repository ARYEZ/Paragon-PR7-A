// EDIT PLAY STYLE (PR7-D 0.46.0): changing how the drums play in a song block, drum by drum, after section 7.3.1 of the
// manual the owner sent (the song track chapter). How it works follows the manual; the calculations are our own:
//   - an INSTRUMENT is a piece of the kit (drums::pieces: the snare with its rimshot and sidestick, the hi-hat with its
//     openings and pedal...), its notes all its articulations' notes;
//   - AMOUNT thins or fills an instrument's notes. The manual's version draws on "tens of thousands of real drum
//     performances"; ours ranks each place in the bar by how strongly a drummer leans on it (the bar's first beat, the
//     half bar, the beats, the eighths, the sixteenths, on the groove's own straight or triplet grid) and by what each
//     kind of drum plays there (a kick on 1 and 3, a snare's backbeat and its ghost notes, hats and ride on the eighths,
//     tom fills at the bar's end, crashes on bar lines). Removing takes the least leaned-on, softest notes first; adding
//     fills the most leaned-on empty places first, at the instrument's own loudness (softer off the beat). Always worked
//     out from the instrument's notes as they were before (its `source`), so turning the knob back gives them back;
//   - the POWER HAND: the groove's leading instrument (the hi-hat, ride or a crash it plays most), which can be moved
//     to another instrument; the OPENING HIT: one hit at the block's start (Block::openingSlot).
#pragma once

#include "song/Song.h"

namespace song::playstyle
{
    enum class Kind { kick, snare, hats, tom, ride, crash, percussion };
    Kind kindOf (int piece) noexcept;
    SlotMask slotsOfPiece (int piece) noexcept;            // every articulation's slot
    std::vector<int> piecesIn (const Block&);              // the instruments the block plays (and its opening hit's)
    SlotMask playingDrums (const Block&);                  // SELECT INSTRUMENT's "All Playing Drums"
    SlotMask allToms() noexcept;
    SlotMask allCrashes() noexcept;

    // The power hand: where it was moved to (Block::powerHand, while that instrument plays), else the hi-hat, ride or
    // crash instrument with the most notes (-1: none); and its most-played slot.
    int powerHandPiece (const Block&);
    int powerHandSlot (const Block&);
    // POWER HAND moved to instrument `toPiece`: every note of the old one played by `toPiece`'s slot `toSlot` (its
    // articulation; -1: its first), merged with what that one already played (on the same beat: one, the louder).
    void movePowerHand (Block&, int toPiece, int toSlot = -1);

    // AMOUNT (-1 to 1): the instrument's notes in `source` (its notes before), thinned or filled in `block`'s groove.
    // `beatsPerBar` places the bar's lines. 0 gives `source` back unchanged.
    std::vector<Hit> withAmount (const std::vector<Hit>& source, int piece, double amount, const Block& block, double beatsPerBar);
    // The instrument's notes in a block (all its articulations), in time order.
    std::vector<Hit> notesOf (const Block&, int piece);
    // The block with the instrument's notes replaced by these.
    void setNotes (Block&, int piece, const std::vector<Hit>& notes);

    // ARTICULATION: every note of `piece` played on its articulation `slot` instead (merged on a beat: the louder).
    void setArticulation (Block&, int piece, int slot);
}
