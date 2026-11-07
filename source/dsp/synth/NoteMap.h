#pragma once

#include "dsp/synth/Drums.h"

#include <array>
#include <cstdint>

// THE NOTE MAP (PR7-D 0.2.0): which MIDI note plays which drum.
//
//   ROOT          the 36 keys from ROOT play the slots in order (C the kick ... the first 12 as on General MIDI kits'
//                 octave, as PR7-K played them); the four keys under ROOT start the sequencer's patterns on KEY.
//   GROOVE LIBRARY  the extended layout of popular drum software's groove libraries, so their MIDI grooves play as
//                 written: several notes per drum (its articulations), from note 6 to 127. Since 0.58.0 the owner's
//                 full map of that software's big progressive kit, note for note (each note's name kept), our
//                 nearest drum where we have no such articulation.
//   GENERAL MIDI  the General MIDI percussion map (35 to 81).
//   CUSTOM        the player's own, edited note by note (and saved as their default).
//
// Notes a layout doesn't use play nothing.
namespace synth::drums::notemap
{
    // Saved by index (drumsMap): only ever append.
    enum class Layout { root, grooveLibrary, generalMidi, custom };
    inline const juce::StringArray layoutNames { "Root", "Groove Library", "General MIDI", "Custom" };

    using Map = std::array<std::int8_t, 128>;   // each note's slot, -1: none

    // The slots by name (slotDefaults' order).
    enum Slot : std::int8_t { BD, RIM, SD, CLP, SD2, LT, CH, MT, PH, HT, OH, CY, RD, RB, CR2, CHN, SPL, HH, TMB, SHK,
                              RE, HO1, HO3,              // 0.17.0
                              T4, HSH, C1C, C2C, CHC, SPC,     // 0.33.0
                              C3, C4, C5, C6, CFX, CH2, T5 };  // 0.36.0
    static_assert (T5 == numSlots - 1);
    // A note that plays the hi-hat at the pedal's openness (CC 4): KitSettings::hatPedal.
    inline constexpr std::int8_t PEDAL = KitSettings::hatPedal;

    // The slot a note plays in a map (-1: none), for grooves and songs (no pedal there: HI-HAT (PEDAL) is closed).
    inline int slotOf (const Map& m, int note) noexcept
    {
        if (note < 0 || note >= 128)
            return -1;
        const auto k = m[(size_t) note];
        return k == PEDAL ? (int) CH : (int) k;
    }

    inline Map empty()
    {
        Map m;
        m.fill (-1);
        return m;
    }

    inline Map rootLayout (int root)
    {
        auto m = empty();
        for (int k = 0; k < numSlots; ++k)
            if (root + k >= 0 && root + k < 128)
                m[(size_t) (root + k)] = (std::int8_t) k;
        return m;
    }

    namespace detail
    {
        inline void set (Map& m, std::int8_t slot, std::initializer_list<int> notes)
        {
            for (const auto n : notes)
                m[(size_t) n] = slot;
        }
        inline void range (Map& m, std::int8_t slot, int from, int to)
        {
            for (int n = from; n <= to; ++n)
                m[(size_t) n] = slot;
        }
    }

    // The groove libraries' layout. 0.58.0: the owner's full map, note for note, from their screenshots of a big
    // progressive-metal kit's MIDI mapping in that software (0.2.0-0.57.0 had our reading of a shorter list). Each
    // note keeps the name that map gives it (`grooveLibraryName`, shown beside the note in SETTINGS), and plays our
    // nearest slot where we have no such articulation (OUR CHOICES, said so in the table):
    //   - the hats' TIP / EDGE TRIGGER notes (meant to follow the pedal) play HI-HAT (PEDAL); CLOSED / TIGHT EDGE the
    //     SHOULDER (HSH); OPEN 0 plays OPEN 1 and OPEN 5 OPEN 4 (we have four open steps, it has six); OPEN PEDAL (the
    //     foot splash) the PEDAL (PH); SEQ HITS closed;
    //   - the snare's EDGE, FLAMS, RUFFS and ZONE TRIGGER the snare; RIM ONLY the sidestick (RIM: a rim click);
    //   - every tom's RIMSHOT and RIM ONLY that tom; KICK OPEN the kick;
    //   - the ride's BOW TIP / SHANK the bow (RD), BELL TIP / SHANK the bell (RB), MUTE HIT the bow;
    //   - MUTE HITs: cymbal 1, cymbal 2, china 1 and the splash their CHOKE slots; cymbals 3-6, china 2 and the
    //     holed cymbal (its "Spock", our FX CRASH) have no choke: their crash.
    // Cymbal 1 is CRASH (CY), cymbal 2 CRASH 2, cymbals 3-6 CRASH 3-6; racktom 1-3 TOM 1-3 (HT MT LT), floortom 1-2
    // FLOOR (T4) and FLOOR 2 (T5). Notes 0-5 play nothing (as in that map).
    struct LibraryNote { int note; std::int8_t slot; const char* name; };
    inline constexpr LibraryNote libraryNotes[] = {
        { 6, SD, "Snare: Zone Trigger" },      { 7, PEDAL, "Hats: Edge Trigger" },   { 8, PEDAL, "Hats: Tip Trigger" },
        { 9, PEDAL, "Hats: Tip Trigger" },     { 10, PH, "Hats: Closed Pedal" },     { 11, HSH, "Hats: Closed Edge" },
        { 12, HO1, "Hats: Open 0" },           { 13, HO1, "Hats: Open 1" },          { 14, HH, "Hats: Open 2" },
        { 15, HO3, "Hats: Open 3" },           { 16, OH, "Hats: Open 4" },           { 17, OH, "Hats: Open 5" },
        { 18, PEDAL, "Hats: Edge Trigger" },   { 19, PEDAL, "Hats: Tip Trigger" },   { 20, PEDAL, "Hats: Tip Trigger" },
        { 21, PH, "Hats: Closed Pedal" },      { 22, HSH, "Hats: Closed Edge" },     { 23, PH, "Hats: Open Pedal" },
        { 24, HO1, "Hats: Open 1" },           { 25, HH, "Hats: Open 2" },           { 26, HO3, "Hats: Open 3" },
        { 27, CHN, "China 1: Crash" },         { 28, CY, "Cymbal 1: Crash" },        { 29, CFX, "Spock: Crash" },
        { 30, C3, "Cymbal 3: Crash" },         { 31, C4, "Cymbal 4: Crash" },        { 32, C6, "Cymbal 6: Crash" },
        { 33, SD, "Snare: Edge" },             { 34, BD, "Kick: Open" },             { 35, BD, "Kick: Hit (Stack)" },
        { 36, BD, "Kick: Hit (Stack)" },               { 37, RIM, "Snare: Sidestick" },      { 38, SD, "Snare: Center (Stack)" },
        { 39, SD, "Snare: Ruffs" },            { 40, SD2, "Snare: Rimshot" },        { 41, T5, "Floortom 2: Center" },
        { 42, CH, "Hats: Closed Tip" },        { 43, T4, "Floortom 1: Center" },     { 44, PH, "Hats: Closed Pedal" },
        { 45, LT, "Racktom 3: Center" },       { 46, HH, "Hats: Open 2" },           { 47, MT, "Racktom 2: Center" },
        { 48, HT, "Racktom 1: Center" },       { 49, CR2, "Cymbal 2: Crash" },       { 50, C2C, "Cymbal 2: Mute Hit" },
        { 51, RD, "Ride: Bow Tip" },           { 52, CH2, "China 2: Crash" },        { 53, RB, "Ride: Bell Shank" },
        { 54, CH2, "China 2: Mute Hit" },      { 55, SPL, "Splash: Crash" },         { 56, SPC, "Splash: Mute Hit" },
        { 57, C5, "Cymbal 5: Crash" },         { 58, C5, "Cymbal 5: Mute Hit" },     { 59, RE, "Ride: Edge" },
        { 60, OH, "Hats: Open 4" },            { 61, CH, "Hats: Closed Tip" },       { 62, HSH, "Hats: Tight Edge" },
        { 63, CH, "Hats: Tight Tip" },         { 64, HO1, "Hats: Open 0" },          { 65, CH, "Hats: Seq Hits" },
        { 66, SD, "Snare: Center (Stack)" },           { 67, RIM, "Snare: Sidestick" },      { 68, SD, "Snare: Center (Stack)" },
        { 69, SD, "Snare: Flams" },            { 70, SD, "Snare: Center (Stack)" },          { 71, RIM, "Snare: Rim Only" },
        { 72, T5, "Floortom 2: Rim Only" },    { 73, T5, "Floortom 2: Rimshot" },    { 74, T4, "Floortom 1: Rim Only" },
        { 75, T4, "Floortom 1: Rimshot" },     { 76, CFX, "Spock: Mute Hit" },       { 77, LT, "Racktom 3: Rim Only" },
        { 78, LT, "Racktom 3: Rimshot" },      { 79, MT, "Racktom 2: Rim Only" },    { 80, MT, "Racktom 2: Rimshot" },
        { 81, HT, "Racktom 1: Rim Only" },     { 82, HT, "Racktom 1: Rimshot" },     { 83, CHC, "China 1: Mute Hit" },
        { 84, RD, "Ride: Bow Shank" },         { 85, RB, "Ride: Bell Shank" },       { 86, RE, "Ride: Edge" },
        { 87, RD, "Ride: Bow Tip" },           { 88, RB, "Ride: Bell Tip" },         { 89, RD, "Ride: Bow Shank" },
        { 90, RB, "Ride: Bell Shank" },        { 91, RE, "Ride: Edge" },             { 92, RD, "Ride: Bow Tip" },
        { 93, RB, "Ride: Bell Tip" },          { 94, C1C, "Cymbal 1: Mute Hit" },    { 95, C3, "Cymbal 3: Mute Hit" },
        { 96, RD, "Ride: Bow Shank" },         { 97, RB, "Ride: Bell Shank" },       { 98, RE, "Ride: Edge" },
        { 99, RD, "Ride: Bow Tip" },           { 100, RB, "Ride: Bell Tip" },        { 101, RD, "Ride: Bow Tip" },
        { 102, RB, "Ride: Bell Shank" },       { 103, RE, "Ride: Edge" },            { 104, RD, "Ride: Bow Shank" },
        { 105, RB, "Ride: Bell Tip" },         { 106, C4, "Cymbal 4: Mute Hit" },    { 107, C6, "Cymbal 6: Mute Hit" },
        { 108, RD, "Ride: Bow Tip" },          { 109, RB, "Ride: Bell Shank" },      { 110, RE, "Ride: Edge" },
        { 111, RD, "Ride: Bow Shank" },        { 112, RB, "Ride: Bell Tip" },        { 113, RD, "Ride: Bow Tip" },
        { 114, RB, "Ride: Bell Shank" },       { 115, RE, "Ride: Edge" },            { 116, RD, "Ride: Bow Shank" },
        { 117, RB, "Ride: Bell Tip" },         { 118, RD, "Ride: Mute Hit" },        { 119, CH, "Hats: Closed Tip" },
        { 120, HO1, "Hats: Open 1" },          { 121, HO3, "Hats: Open 3" },         { 122, HSH, "Hats: Closed Edge" },
        { 123, HO1, "Hats: Open 0" },          { 124, OH, "Hats: Open 5" },          { 125, SD, "Snare: Center (Stack)" },
        { 126, SD2, "Snare: Rimshot" },        { 127, RIM, "Snare: Sidestick" },
    };
    static_assert (std::size (libraryNotes) == 122);

    inline Map grooveLibrary()
    {
        auto m = empty();
        for (const auto& n : libraryNotes)
            m[(size_t) n.note] = n.slot;
        return m;
    }

    // The name the groove libraries' map gives a note ("" for a note it leaves empty).
    inline const char* grooveLibraryName (int note) noexcept
    {
        for (const auto& n : libraryNotes)
            if (n.note == note)
                return n.name;
        return "";
    }

    // General MIDI's percussion map, on the slots nearest each sound (no slot for the cowbell, the bongos and the
    // rest of the Latin percussion: those notes play nothing).
    inline Map generalMidi()
    {
        using namespace detail;
        auto m = empty();
        set (m, BD, { 35, 36 });
        set (m, RIM, { 37 });
        set (m, SD, { 38 });
        set (m, CLP, { 39 });
        set (m, SD2, { 40 });
        set (m, LT, { 43 });
        set (m, T4, { 41 });   // 0.33.0: the low floor tom (it played LT before; T4's stand-in is LT)
        set (m, CH, { 42 });
        set (m, MT, { 45, 47 });
        set (m, PH, { 44 });
        set (m, HT, { 48, 50 });
        set (m, OH, { 46 });
        set (m, CY, { 49 });
        set (m, RD, { 51, 59 });
        set (m, CHN, { 52 });
        set (m, RB, { 53 });
        set (m, TMB, { 54 });
        set (m, SPL, { 55 });
        set (m, CR2, { 57 });
        set (m, SHK, { 69, 70 });
        return m;
    }

    // The note a slot is written on in a MIDI export (and shown beside its row): a layout's main note for it (the
    // General MIDI one where it has one), else the lowest note that plays it, else -1.
    inline int homeNote (const Map& m, Layout layout, int slot, int root)
    {
        if (layout == Layout::root)
            return root + slot >= 0 && root + slot < 128 ? root + slot : -1;
        if (layout == Layout::grooveLibrary || layout == Layout::generalMidi)
        {
            // Each slot's main note: General MIDI's where it has one; GROOVE LIBRARY's (0.58.0) the owner's map's
            // first-octave note for that drum (its crashes and toms are numbered differently from General MIDI's).
            constexpr std::array<int, numSlots> gm { 36, 37, 38, 39, 40, 43, 42, 45, 44, 48, 46, 49, 51, 53, 57, 52, 55, -1, 54, 69,
                                                     -1, -1, -1, 41, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
            constexpr std::array<int, numSlots> library { 36, 37, 38, -1, 40, 45, 42, 47, 44, 48, 60, 28, 51, 53, 49, 27, 55, 25, -1, -1,
                                                          59, 24, 26, 43, 22, 94, 50, 83, 56, 30, 31, 57, 32, 29, 52, 41 };
            const auto& preferred = layout == Layout::grooveLibrary ? library : gm;
            const auto n = preferred[(size_t) juce::jlimit (0, numSlots - 1, slot)];
            if (n >= 0 && n < 128 && m[(size_t) n] == slot)
                return n;
        }
        for (int n = 0; n < 128; ++n)
            if (m[(size_t) n] == slot)
                return n;
        return -1;
    }

    // As text for the project and the default file: "note:slot" for each note that plays one.
    inline juce::String encode (const Map& m)
    {
        juce::StringArray items;
        for (int n = 0; n < 128; ++n)
            if (m[(size_t) n] >= 0)
                items.add (juce::String (n) + ":" + juce::String ((int) m[(size_t) n]));
        return items.joinIntoString (" ");
    }
    inline Map decode (const juce::String& text)
    {
        auto m = empty();
        for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
        {
            const auto note = item.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const auto slot = item.fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (note >= 0 && note < 128 && ((slot >= 0 && slot < numSlots) || slot == PEDAL) && item.containsChar (':'))
                m[(size_t) note] = (std::int8_t) slot;
        }
        return m;
    }
}
