// THE NOTE MAP (PR7-D 0.2.0): each layout's notes, a note playing its drum in the plugin, CUSTOM editing and saving,
// the sequencer's KEY keys, MIDI export on the map's notes.
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "dsp/synth/NoteMap.h"

#include <set>

namespace
{
    using namespace synth::drums::notemap;

    // The owner's map (from that software's published layout), drum by drum, as given: the layout must play each note
    // on the drum written here. 0.17.0 split the articulations out: the ride's edge (59), the hat's openness steps
    // (24 open 1, 25 open 2, 26 open 3) and its pedal note (7: the openness CC 4 sets, closed with no pedal).
    struct Expected { std::int8_t slot; std::vector<int> notes; };
    std::vector<Expected> ownersMap()
    {
        std::vector<int> ride { 29, 30, 51, 52, 54 };
        for (int n = 83; n <= 93; ++n) if (n != 86) ride.push_back (n);
        for (int n = 96; n <= 105; ++n) ride.push_back (n);
        for (int n = 108; n <= 118; ++n) ride.push_back (n);
        return {
            { BD, { 34, 35, 36 } },
            { SD, { 6, 33, 38, 39, 66, 68, 69, 70, 125 } },
            { SD2, { 40, 71, 126 } },
            { RIM, { 37, 67, 76, 127 } },
            { CH, { 8, 9, 11, 18, 19, 20, 22, 42, 61, 62, 63, 64, 65, 119, 122 } },
            { PEDAL, { 7 } },
            { PH, { 10, 21, 23, 44 } },
            { OH, { 12, 13, 14, 15, 16, 17, 46, 60, 120, 121, 123, 124 } },
            { HO1, { 24 } },
            { HH, { 25 } },
            { HO3, { 26 } },
            { RE, { 59 } },
            { HT, { 47, 48, 79, 80, 81, 82 } },
            { MT, { 43, 45, 74, 75, 77, 78 } },
            { LT, { 41, 72, 73 } },
            { CY, { 27, 28, 31, 32, 49, 50, 55, 56, 57, 58, 86, 94, 95, 106, 107 } },
            { RD, ride },
            { RB, { 53 } },
            { TMB, { 3 } },
            { SHK, { 2 } },
        };
    }

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
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
            CHECK (map[(size_t) n] == -1);   // 0, 1 (the one-shot), 4 and 5: nothing
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
    CHECK (p.processor.getMapLayout() == Layout::root);   // a new PR7-D plays as 0.1.0 did
    CHECK (p.sounding (36) == std::vector<int> { BD });
    CHECK (p.sounding (48) == std::vector<int> { RD });   // ROOT + 12: the first slot 0.2.0 added
    CHECK (p.sounding (22).empty());

    p.layout (Layout::grooveLibrary);
    CHECK (p.sounding (22) == std::vector<int> { CH });
    CHECK (p.sounding (24) == std::vector<int> { HO1 });   // 0.17.0: the hat's openness steps
    CHECK (p.sounding (25) == std::vector<int> { HH });
    CHECK (p.sounding (26) == std::vector<int> { HO3 });
    CHECK (p.sounding (46) == std::vector<int> { OH });
    CHECK (p.sounding (59) == std::vector<int> { RE });    // the ride's edge
    CHECK (p.sounding (7) == std::vector<int> { CH });     // HI-HAT (PEDAL), no pedal heard: closed
    CHECK (p.sounding (53) == std::vector<int> { RB });
    CHECK (p.sounding (40) == std::vector<int> { SD2 });   // the rimshot
    CHECK (p.sounding (3) == std::vector<int> { TMB });
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
        CHECK (p.processor.getNoteMap()[22] == CH);   // the rest of the layout kept
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
    CHECK (notesIn() == std::set<int> { 36, 42, 53, 3 });   // the clap has no note in this layout: left out
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
