// THE NOTE MAP (PR7-D 0.2.0): each layout's notes, a note playing its drum in the plugin, CUSTOM editing and saving,
// the sequencer's KEY keys, MIDI export on the map's notes.
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "dsp/synth/NoteMap.h"

#include <set>

namespace
{
    using namespace synth::drums::notemap;

    // The owner's map (0.58.0: their screenshots of that software's full MIDI mapping for a big progressive kit, every
    // note 0-127), as each named articulation reaches our drums: the layout must play each note on the drum written
    // here, and the notes the map leaves empty (0-5) play nothing.
    struct Expected { std::int8_t slot; std::vector<int> notes; };
    std::vector<Expected> ownersMap()
    {
        return {
            { BD, { 34, 35, 36 } },                                   // Kick: Open, Hit, Hit
            { SD, { 6, 33, 38, 39, 66, 68, 69, 70, 125 } },           // Zone Trigger, Edge, Center, Ruffs, Center, Flams...
            { SD2, { 40, 126 } },                                     // Rimshot
            { RIM, { 37, 67, 71, 127 } },                             // Sidestick, Rim Only
            { PEDAL, { 7, 8, 9, 18, 19, 20 } },                       // Hats: Edge / Tip Trigger
            { PH, { 10, 21, 23, 44 } },                               // Closed Pedal, Open Pedal
            { HSH, { 11, 22, 62, 122 } },                             // Closed / Tight Edge
            { CH, { 42, 61, 63, 65, 119 } },                          // Closed / Tight Tip, Seq Hits
            { HO1, { 12, 13, 24, 64, 120, 123 } },                    // Open 0, Open 1
            { HH, { 14, 25, 46 } },                                   // Open 2
            { HO3, { 15, 26, 121 } },                                 // Open 3
            { OH, { 16, 17, 60, 124 } },                              // Open 4, Open 5
            { HT, { 48, 81, 82 } },                                   // Racktom 1
            { MT, { 47, 79, 80 } },                                   // Racktom 2
            { LT, { 45, 77, 78 } },                                   // Racktom 3
            { T4, { 43, 74, 75 } },                                   // Floortom 1
            { T5, { 41, 72, 73 } },                                   // Floortom 2
            { CY, { 28 } }, { C1C, { 94 } },                          // Cymbal 1: Crash, Mute Hit
            { CR2, { 49 } }, { C2C, { 50 } },                         // Cymbal 2
            { C3, { 30, 95 } }, { C4, { 31, 106 } }, { C5, { 57, 58 } }, { C6, { 32, 107 } },
            { CHN, { 27 } }, { CHC, { 83 } },                         // China 1
            { CH2, { 52, 54 } },                                      // China 2
            { SPL, { 55 } }, { SPC, { 56 } },                         // Splash
            { CFX, { 29, 76 } },                                      // the holed cymbal ("Spock")
            { RD, { 51, 84, 87, 89, 92, 96, 99, 101, 104, 108, 111, 113, 116, 118 } },   // Bow Tip / Shank, Mute Hit
            { RB, { 53, 85, 88, 90, 93, 97, 100, 102, 105, 109, 112, 114, 117 } },       // Bell Tip / Shank
            { RE, { 59, 86, 91, 98, 103, 110, 115 } },                                    // Edge
        };
    }

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
            processor.seedRandomForTest (1);
        }
        void set (const juce::String& id, float value) { testkit::set (processor, id, value); }
        void layout (Layout l) { set (ParamIDs::drumsMap, (float) l); }
        // Which drums sound after `note` is hit (a short block).
        std::vector<int> sounding (int note)
        {
            processor.prepareToPlay (sampleRate, blockSize);   // a fresh kit: nothing still ringing
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buffer (2, blockSize);
            processor.processBlock (buffer, midi);
            std::vector<int> slots;
            for (int k = 0; k < synth::drums::numSlots; ++k)
                if (processor.getDrumKit().isSounding (k))
                    slots.push_back (k);
            return slots;
        }
    };
}

TEST_CASE ("NOTE MAP: the groove libraries' layout plays every note of the owner's map on its drum, and nothing else", "[notemap]")
{
    const auto map = grooveLibrary();
    std::set<int> listed;
    for (const auto& [slot, notes] : ownersMap())
        for (const auto n : notes)
        {
            INFO ("note " << n);
            CHECK (map[(size_t) n] == slot);
            listed.insert (n);
        }
    for (int n = 0; n < 128; ++n)
        if (! listed.contains (n))
        {
            INFO ("note " << n);
            CHECK (map[(size_t) n] == -1);   // 0 to 5: nothing
        }
}

TEST_CASE ("NOTE MAP: General MIDI's drums on their notes", "[notemap]")
{
    const auto map = generalMidi();
    const std::pair<int, Slot> expected[] = { { 35, BD }, { 36, BD }, { 37, RIM }, { 38, SD }, { 39, CLP }, { 40, SD2 }, { 41, T4 },   // 0.33.0: the low floor tom, tom 4
                                             
                                              { 42, CH }, { 43, LT }, { 44, PH }, { 45, MT }, { 46, OH }, { 47, MT }, { 48, HT },
                                              { 49, CY }, { 50, HT }, { 51, RD }, { 52, CHN }, { 53, RB }, { 54, TMB }, { 55, SPL },
                                              { 57, CR2 }, { 59, RD }, { 69, SHK }, { 70, SHK } };
    for (const auto& [note, slot] : expected)
    {
        INFO ("note " << note);
        CHECK (map[(size_t) note] == slot);
    }
    CHECK (map[56] == -1);   // the cowbell: no slot for it
    CHECK (map[34] == -1);
}

TEST_CASE ("NOTE MAP in the plugin: each layout's notes play their drums; ROOT as before", "[notemap]")
{
    Plugin p;
    {
        ParagonX1AudioProcessor fresh;
        CHECK (fresh.getMapLayout() == Layout::grooveLibrary);   // 0.59.0: a new PR7-D starts on the owner's map
    }
    CHECK (p.processor.getMapLayout() == Layout::root);   // (the helper put it on ROOT)
    CHECK (p.sounding (36) == std::vector<int> { BD });
    CHECK (p.sounding (48) == std::vector<int> { RD });   // ROOT + 12: the first slot 0.2.0 added
    CHECK (p.sounding (22).empty());

    p.layout (Layout::grooveLibrary);
    CHECK (p.sounding (22) == std::vector<int> { HSH });   // 0.58.0: the owner's map's closed edge (our SHOULDER)
    CHECK (p.sounding (42) == std::vector<int> { CH });    // closed tip
    CHECK (p.sounding (24) == std::vector<int> { HO1 });   // 0.17.0: the hat's openness steps
    CHECK (p.sounding (25) == std::vector<int> { HH });
    CHECK (p.sounding (26) == std::vector<int> { HO3 });
    CHECK (p.sounding (46) == std::vector<int> { HH });    // 0.58.0: Hats: Open 2
    CHECK (p.sounding (60) == std::vector<int> { OH });    // Hats: Open 4
    CHECK (p.sounding (28) == std::vector<int> { CY });    // Cymbal 1
    CHECK (p.sounding (49) == std::vector<int> { CR2 });   // Cymbal 2
    CHECK (p.sounding (48) == std::vector<int> { HT });    // Racktom 1
    CHECK (p.sounding (47) == std::vector<int> { MT });    // Racktom 2
    CHECK (p.sounding (43) == std::vector<int> { T4 });    // Floortom 1
    CHECK (p.sounding (41) == std::vector<int> { T5 });    // Floortom 2
    CHECK (p.sounding (59) == std::vector<int> { RE });    // the ride's edge
    CHECK (p.sounding (7) == std::vector<int> { CH });     // HI-HAT (PEDAL), no pedal heard: closed
    CHECK (p.sounding (53) == std::vector<int> { RB });
    CHECK (p.sounding (40) == std::vector<int> { SD2 });   // the rimshot
    CHECK (p.sounding (3).empty());   // 0.58.0: nothing on 0-5 (the tambourine and shaker had 3 and 2)
    CHECK (p.sounding (4).empty());

    p.layout (Layout::generalMidi);
    CHECK (p.sounding (49) == std::vector<int> { CY });
    CHECK (p.sounding (57) == std::vector<int> { CR2 });
    CHECK (p.sounding (22).empty());
}

TEST_CASE ("NOTE MAP: the sequencer's KEY keys work with ROOT only (the other layouts play drums on those notes)", "[notemap]")
{
    Plugin p;
    p.set (ParamIDs::dseqOn, 1.0f);
    p.set (ParamIDs::dseqTrigger, 1.0f);   // KEY
    CHECK (p.sounding (33).empty());   // ROOT: pattern B's key (an empty pattern): no drum
    p.layout (Layout::grooveLibrary);
    CHECK (p.sounding (33) == std::vector<int> { SD });   // the layout's snare
}

TEST_CASE ("NOTE MAP: CUSTOM: a change copies the layout you're on, chooses CUSTOM, and is kept by the project and as the default", "[notemap]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonNoteMapTests");
    folder.deleteRecursively();
    juce::MemoryBlock state;
    {
        Plugin p;
        p.processor.useRigFolder (folder);
        p.layout (Layout::grooveLibrary);
        p.processor.mapNote (4, HH);   // the half-open hat on 4 (nothing there in the layout)
        CHECK (p.processor.getMapLayout() == Layout::custom);
        CHECK (p.processor.getNoteMap()[4] == HH);
        CHECK (p.processor.getNoteMap()[22] == HSH);   // the rest of the layout kept
        CHECK (p.sounding (4) == std::vector<int> { HH });
        p.processor.mapNote (22, -1);
        CHECK (p.sounding (22).empty());
        REQUIRE (p.processor.saveNoteMapAsDefault());
        CHECK (! p.processor.isRigModified());   // the map isn't part of a preset
        p.processor.getStateInformation (state);
    }
    {
        Plugin restored;
        restored.processor.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (restored.processor.getMapLayout() == Layout::custom);
        CHECK (restored.processor.getCustomMap()[4] == HH);
        CHECK (restored.processor.getCustomMap()[22] == -1);
    }
    {
        // A new instance with that presets folder starts with the saved CUSTOM map (here: loaded from the file directly,
        // as the constructor does from the user's folder).
        const auto xml = juce::XmlDocument::parse (folder.getChildFile ("Note Map.xml"));
        REQUIRE (xml != nullptr);
        CHECK (decode (xml->getStringAttribute ("keys"))[4] == HH);
    }
    folder.deleteRecursively();

    // Text round trip.
    CHECK (decode (encode (grooveLibrary())) == grooveLibrary());
    CHECK (decode ("4:17 999:3 12:99 x") [4] == HH);   // bad entries ignored
}

TEST_CASE ("NOTE MAP: a preset load leaves the map alone", "[notemap][rigs]")
{
    Plugin p;
    p.layout (Layout::generalMidi);
    p.processor.loadRig (*p.processor.getRigLibrary().findFactory ("Room Kit"));
    CHECK (p.processor.getMapLayout() == Layout::generalMidi);
}

TEST_CASE ("NOTE MAP: MIDI EXPORT writes each drum on its note in the map, so the file plays back as written", "[notemap]")
{
    Plugin p;
    auto m = p.processor.getModMatrix();
    double step = 0.0;   // one note per step, inside the pattern's 16
    for (const auto row : { (int) BD, (int) CH, (int) RB, (int) TMB, (int) CLP })
    {
        synth::drumseq::Note n;
        n.row = row;
        n.start = step++;
        m.drumPatterns[0].notes.push_back (n);
    }
    p.processor.setModMatrix (m);
    const auto notesIn = [&p]
    {
        std::set<int> notes;
        const auto file = p.processor.drumPatternToMidi (0);
        for (const auto* e : *file.getTrack (0))
            if (e->message.isNoteOn())
                notes.insert (e->message.getNoteNumber());
        return notes;
    };
    CHECK (notesIn() == std::set<int> { 36 + BD, 36 + CH, 36 + RB, 36 + TMB, 36 + CLP });   // ROOT: root + row
    p.layout (Layout::grooveLibrary);
    CHECK (notesIn() == std::set<int> { 36, 42, 53 });   // the clap and (0.58.0) the tambourine have no note in this layout: left out
    p.layout (Layout::generalMidi);
    CHECK (notesIn() == std::set<int> { 36, 42, 53, 54, 39 });
    // Each played back through the map hits the drum it was written for.
    for (const auto note : notesIn())
    {
        INFO (note);
        CHECK (p.sounding (note).size() == 1);
    }
}

TEST_CASE ("NOTE MAP: an old kit file (PR7-K's 12 drums) loads; the 8 new drums go to their defaults", "[notemap][drumfiles]")
{
    Plugin p;
    p.set (ParamIDs::drum (12, "Pitch"), 7.0f);
    p.set (ParamIDs::drum (2, "Pitch"), 0.0f);
    juce::XmlElement kit ("DrumKit");
    kit.setAttribute ("version", 1);
    auto* e = kit.createNewChildElement ("Param");
    e->setAttribute ("id", ParamIDs::drum (2, "Pitch"));
    e->setAttribute ("value", 3.0);
    REQUIRE (p.processor.applyDrumKit (kit));
    const auto value = [&p] (const juce::String& id) { return p.processor.getParameters().getRawParameterValue (id)->load(); };
    CHECK_THAT (value (ParamIDs::drum (2, "Pitch")), WithinAbs (3.0, 0.01));
    CHECK_THAT (value (ParamIDs::drum (12, "Pitch")), WithinAbs (synth::drums::slotDefaults[12].pitch, 0.01));
}

#include "NoteNames.h"

TEST_CASE ("NOTE NAMES 0.35.1: middle C as C3 (Cubase, the default) or C4; only the names change, not what plays", "[notemap]")
{
    const auto before = notenames::middleCOctave.load();
    notenames::middleCOctave = notenames::octaveForChoice (0);
    CHECK (notenames::name (36) == "C1");   // the kick, as Cubase / PR7-S shows it
    CHECK (notenames::name (38) == "D1");   // the snare
    CHECK (notenames::name (60) == "C3");
    notenames::middleCOctave = notenames::octaveForChoice (1);
    CHECK (notenames::name (36) == "C2");
    CHECK (notenames::name (38) == "D2");
    CHECK (notenames::name (60) == "C4");
    notenames::middleCOctave = before;

    // The setting: C3 by default, not kept in presets, and it doesn't move a single note.
    ParagonX1AudioProcessor p;
    auto* names = p.getParameters().getParameter (ParamIDs::noteNames);
    REQUIRE (names != nullptr);
    CHECK (p.getParameters().getRawParameterValue (ParamIDs::noteNames)->load() == 0.0f);
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::noteNames));
    CHECK (names->getText (names->getValue(), 32).startsWith ("C3"));
    const auto map = p.getNoteMap();
    names->setValueNotifyingHost (1.0f);
    CHECK (p.getNoteMap() == map);
}

TEST_CASE ("NOTE MAP 0.58.0: the groove libraries' layout keeps the owner's map's name for each note; home notes", "[notemap]")
{
    CHECK (juce::String (grooveLibraryName (36)) == "Kick: Hit (Stack)");
    CHECK (juce::String (grooveLibraryName (38)) == "Snare: Center (Stack)");
    CHECK (juce::String (grooveLibraryName (34)) == "Kick: Open");
    CHECK (juce::String (grooveLibraryName (7)) == "Hats: Edge Trigger");
    CHECK (juce::String (grooveLibraryName (29)) == "Spock: Crash");
    CHECK (juce::String (grooveLibraryName (118)) == "Ride: Mute Hit");
    CHECK (juce::String (grooveLibraryName (127)) == "Snare: Sidestick");
    for (int n = 0; n <= 5; ++n)
        CHECK (juce::String (grooveLibraryName (n)).isEmpty());
    // Every note from 6 up named and played.
    const auto map = grooveLibrary();
    for (int n = 6; n < 128; ++n)
    {
        INFO ("note " << n);
        CHECK (juce::String (grooveLibraryName (n)).isNotEmpty());
        CHECK (map[(size_t) n] >= 0);
    }
    // Each drum's export note is the map's first-octave note for it.
    const std::pair<int, int> home[] = { { BD, 36 }, { SD, 38 }, { CH, 42 }, { OH, 60 }, { HH, 25 }, { CY, 28 }, { CR2, 49 },
                                         { HT, 48 }, { MT, 47 }, { LT, 45 }, { T4, 43 }, { T5, 41 }, { RD, 51 }, { RB, 53 },
                                         { RE, 59 }, { HSH, 22 }, { CFX, 29 }, { C1C, 94 }, { CLP, -1 }, { TMB, -1 } };
    for (const auto& [slot, note] : home)
    {
        INFO ("slot " << slot);
        CHECK (homeNote (map, Layout::grooveLibrary, slot, 36) == note);
    }
    // General MIDI's as before.
    CHECK (homeNote (generalMidi(), Layout::generalMidi, MT, 36) == 45);
    CHECK (homeNote (generalMidi(), Layout::generalMidi, LT, 36) == 43);
    CHECK (homeNote (generalMidi(), Layout::generalMidi, T4, 36) == 41);
}
