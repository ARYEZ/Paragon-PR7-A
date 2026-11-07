#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Presets ("rigs" in the code, as across the PR7 family): files, the
// library, loading and saving, and the factory presets.
#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/FactoryRigs.h"

using Catch::Matchers::WithinAbs;

namespace
{
    // A fresh, empty rigs folder for each test.
    struct TempRigFolder
    {
        TempRigFolder() { folder.deleteRecursively(); }
        ~TempRigFolder() { folder.deleteRecursively(); }
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1RigTests")
                                .getChildFile (juce::Uuid().toString());
    };

    // Stand-ins for the tests below: a number in the preset (with a default),
    // another (that can go below zero), an on/off that's off by default.
    const juce::String someTime = ParamIDs::drum (2, "Delay");    // ms, -1000 to 1000 (0 by default)
    const juce::String someFine = ParamIDs::drum (2, "Pitch");    // semitones, -24 to 24 (0 by default)
    const juce::String someSwitch = ParamIDs::drum (2, "Mono");   // off by default

    struct RigRig   // a processor with its presets in a temporary folder
    {
        RigRig()
        {
            processor.setPlayConfigDetails (1, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.useRigFolder (temp.folder);
        }

        void set (const juce::String& id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        float get (const juce::String& id) { return processor.getParameters().getRawParameterValue (id)->load(); }

        const RigLibrary::Entry& entryNamed (const juce::String& name)
        {
            for (const auto& entry : processor.getRigLibrary().getEntries())
                if (entry.rig.name == name)
                    return entry;
            FAIL ("No preset called " << name);
            return processor.getRigLibrary().getEntries().front();
        }

        TempRigFolder temp;
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("A rig survives being written to a file and read back", "[rigs]")
{
    Rig rig;
    rig.name = "AC/DC: \"Back in Black\" & <more>";
    rig.bank = 2;
    rig.number = 17;
    rig.favourite = true;
    rig.tags = { "Rock", "High Gain" };
    rig.parameters = { { "drum3LowPass", 650.0f }, { "drum1Pitch", -4.0f } };

    const auto restored = Rig::fromXml (*juce::XmlDocument::parse (rig.toXml()->toString()));
    REQUIRE (restored.has_value());
    CHECK (restored->name == rig.name);
    CHECK (restored->getSlotName() == "C17");
    CHECK (restored->favourite);
    CHECK (restored->tags == rig.tags);
    CHECK (restored->parameters == rig.parameters);
}

TEST_CASE ("Files that aren't rigs, or are from a newer version, are refused", "[rigs]")
{
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<SomethingElse/>")).has_value());
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<ParagonRig version=\"99\" name=\"x\"/>")).has_value());
}

TEST_CASE ("Every factory rig is valid", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names, slots;

    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        names.addIfNotAlreadyThere (rig.name);
        slots.addIfNotAlreadyThere (rig.getSlotName());

        for (const auto& [id, value] : rig.parameters)
        {
            INFO (id);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);                          // no typos in parameter IDs
            CHECK_FALSE (ParamIDs::notInRigs.contains (id));        // rigs don't touch global settings
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("PR7-D's factory presets: Init at A01, then kits and beats that say what they are; PR7-E's mixing rigs are gone", "[rigs]")
{
    const auto rigs = makeFactoryRigs();
    REQUIRE (rigs.size() >= 5);
    CHECK (rigs.front().name == "Init");
    CHECK (rigs.front().getSlotName() == "A01");
    CHECK (rigs.front().parameters.empty());   // everything at its default: the drum synths, dry, no beat
    for (const auto& rig : rigs)
        for (const auto& [id, value] : rig.parameters)
        {
            INFO (rig.name << ": " << id);
            CHECK_FALSE (id.startsWith ("osc"));   // nothing of PR7-K's synth
            CHECK_FALSE (id.startsWith ("flt"));
        }

    for (const auto& rig : rigs)
    {
        INFO (rig.name);
        CHECK (rig.notes.isNotEmpty());
        CHECK_FALSE (rig.tags.isEmpty());
    }

    RigRig r;
    for (const auto* mixingRig : { "Lead Vocal", "Kick Punch", "Mix Brighter", "Master Gentle Polish", "Modern Metal Vocal" })
        CHECK (r.processor.getRigLibrary().findFactory (mixingRig) == nullptr);
}

TEST_CASE ("Every factory preset plays: its drums sound, stay clean, and fade away; its beat plays with the song", "[rigs]")
{
    // A song playing at 120 BPM, for the presets whose sequencer is on.
    struct Song final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            return info;
        }
        bool playing = false;
        double ppq = 0.0;
    };
    RigRig r;
    Song song;
    r.processor.setPlayHead (&song);
    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        r.processor.loadRig (r.entryNamed (rig.name));
        r.processor.prepareToPlay (48000.0, 256);   // fresh

        juce::AudioBuffer<float> buffer (2, 256);
        float loudest = 0.0f;
        auto finite = true;
        const auto run = [&] (int blocks, bool hits)
        {
            for (int block = 0; block < blocks; ++block)
            {
                juce::MidiBuffer midi;   // every drum in turn, one every quarter of a second
                if (hits && block % 47 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (block / 47) % 12, (juce::uint8) 110), 0);
                r.processor.processBlock (buffer, midi);
                if (song.playing)
                    song.ppq += 256.0 / 48000.0 * 2.0;
                loudest = juce::jmax (loudest, buffer.getMagnitude (0, 0, 256), buffer.getMagnitude (1, 0, 256));
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
            }
        };
        run (48000 * 3 / 256, true);
        CHECK (finite);
        CHECK (juce::Decibels::gainToDecibels (loudest) > -24.0f);   // it sounds
        CHECK (juce::Decibels::gainToDecibels (loudest) < 3.0f);     // and isn't wildly hot (OUTPUT at its -6 dB)
        run (48000 * 8 / 256, false);
        // Quiet 8 seconds after the last hit (the cymbal, hit last, rings longest). Its hit is still running, far
        // below hearing: the kit ends a hit only once it's down to -240 dB (PR7-K's kit, kept as it is).
        CHECK (buffer.getMagnitude (0, 0, 256) < 1.0e-3f);

        // With the song playing: a preset with the sequencer on plays its beat by itself; one without stays silent.
        const auto beat = r.processor.getDrumSeqSettings().on;
        song.playing = true;
        song.ppq = 0.0;
        loudest = 0.0f;
        run (48000 * 2 / 256, false);
        song.playing = false;
        // 0.27.0: a preset whose drums are a downloaded kit's (Crocell Metal) has nothing to play until the kit's here.
        const auto needsKit = std::any_of (rig.drumInstruments.begin(), rig.drumInstruments.end(),
                                           [] (const auto& ref) { return ref.second.contains ("{kit:"); });
        CHECK ((juce::Decibels::gainToDecibels (loudest) > -24.0f) == (beat && ! needsKit));
        run (48000 * 8 / 256, false);
    }
    r.processor.setPlayHead (nullptr);
}

TEST_CASE ("A rig's notes are saved with it and kept by Save As", "[rigs]")
{
    Rig rig;
    rig.name = "Noted";
    rig.notes = "For the mix bus.\nTrim first, then limit.";
    const auto loaded = Rig::fromXml (*rig.toXml());
    REQUIRE (loaded.has_value());
    CHECK (loaded->notes == rig.notes);

    // Saving a copy of a factory rig keeps its notes.
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("My Init"));
    const auto* saved = r.processor.getRigLibrary().find (r.processor.getCurrentRig().file);
    REQUIRE (saved != nullptr);
    CHECK (saved->rig.notes == r.entryNamed ("Init").rig.notes);
}

TEST_CASE ("The rig library saves, finds, renames and deletes rigs", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);
    const auto factoryCount = library.getEntries().size();

    Rig rig;
    rig.name = "Cyber Drive";
    const auto first = library.saveAsNew (rig);
    REQUIRE (first.existsAsFile());
    REQUIRE (library.find (first) != nullptr);

    SECTION ("A taken name gets a number")
    {
        const auto second = library.saveAsNew (rig);
        REQUIRE (library.find (second) != nullptr);
        CHECK (library.find (second)->rig.name == "Cyber Drive 2");
        CHECK (second != first);
    }

    SECTION ("Names that can't be file names still save")
    {
        rig.name = "AC/DC: Live?";
        const auto file = library.saveAsNew (rig);
        REQUIRE (file.existsAsFile());
        CHECK (library.find (file)->rig.name == "AC/DC: Live?");
    }

    SECTION ("Overwrite and delete")
    {
        auto changed = library.find (first)->rig;
        changed.tags = { "Lead" };
        REQUIRE (library.overwrite (first, changed));
        CHECK (library.find (first)->rig.tags == juce::StringArray { "Lead" });

        REQUIRE (library.remove (first));
        CHECK (library.find (first) == nullptr);
        CHECK (library.getEntries().size() == factoryCount);
    }

    SECTION ("A corrupt rig file is skipped, not fatal")
    {
        temp.folder.getChildFile (juce::String ("broken") + Rig::fileExtension).replaceWithText ("<ParagonRig name=");
        library.rescan();
        CHECK (library.getEntries().size() == factoryCount + 1);
    }
}

TEST_CASE ("Favourites stick, including on factory rigs", "[rigs]")
{
    TempRigFolder temp;
    {
        RigLibrary library (temp.folder);
        library.setFavourite (*library.findFactory ("Init"), true);
    }
    {
        RigLibrary reopened (temp.folder);
        CHECK (reopened.findFactory ("Init")->rig.favourite);
        reopened.setFavourite (*reopened.findFactory ("Init"), false);
    }

    RigLibrary reopenedAgain (temp.folder);
    CHECK_FALSE (reopenedAgain.findFactory ("Init")->rig.favourite);
}

TEST_CASE ("Rigs are sorted by bank and number, and < > wrap round", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);

    Rig late;
    late.name = "Late";
    late.bank = Rig::numBanks - 1;   // the last bank, after every factory rig
    late.number = 50;
    library.saveAsNew (late);

    const auto& entries = library.getEntries();
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (entries[i - 1].rig.bank * 100 + entries[i - 1].rig.number <= entries[i].rig.bank * 100 + entries[i].rig.number);

    CHECK (entries.back().rig.name == "Late");
    CHECK (library.step (&entries.back(), 1) == &entries.front());
    CHECK (library.step (&entries.front(), -1) == &entries.back());
    CHECK (library.step (nullptr, 1) == &entries.front());

    CHECK (library.nextFreeNumber (0) == 8);   // the factory presets fill A01 to A07 (0.27.0: A07 Crocell Metal)
}

TEST_CASE ("Saving and loading a preset restores the sound but not the global settings", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::reverbOn, 1.0f);
    r.set (someTime, 7.5f);
    r.set (someFine, 6.0f);
    r.set (ParamIDs::patchLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global OUTPUT.
    r.set (ParamIDs::reverbOn, 0.0f);
    r.set (someTime, 1.0f);
    r.set (someFine, -2.0f);
    r.set (ParamIDs::patchLevel, 0.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::reverbOn) > 0.5f);
    CHECK_THAT (r.get (someTime), WithinAbs (7.5, 0.05));
    CHECK_THAT (r.get (someFine), WithinAbs (6.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::patchLevel), WithinAbs (-3.0, 0.01));
    // Global settings are left alone.
    CHECK_THAT (r.get (ParamIDs::outputGain), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("A preset that doesn't mention a parameter sets it to its default", "[rigs]")
{
    // E.g. a preset saved by an older version, before that parameter existed.
    RigRig r;
    r.set (someSwitch, 1.0f);
    r.set (someFine, 6.0f);

    Rig old;
    old.name = "Old Preset";
    old.parameters = { { ParamIDs::reverbOn, 1.0f } };
    r.processor.getRigLibrary().saveAsNew (old);
    r.processor.loadRig (r.entryNamed ("Old Preset"));

    CHECK (r.get (ParamIDs::reverbOn) > 0.5f);
    CHECK (r.get (someSwitch) < 0.5f);
    CHECK_THAT (r.get (someFine), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The modified marker tracks changes since loading or saving", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (someTime, 9.0f);
        CHECK (r.processor.isRigModified());
    }

    SECTION ("The global settings don't count")
    {
        r.set (ParamIDs::outputGain, -3.0f);
        CHECK_FALSE (r.processor.isRigModified());
    }

    SECTION ("Saving clears it")
    {
        r.set (someTime, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (someTime, 2.0f);
        CHECK (r.processor.isRigModified());
        REQUIRE (r.processor.saveCurrentRig());   // a user preset now, so plain save works
        CHECK_FALSE (r.processor.isRigModified());
    }
}

TEST_CASE ("Factory presets can't be saved over", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    r.set (someFine, 9.0f);
    CHECK_FALSE (r.processor.saveCurrentRig());   // the UI then asks for a new name
    CHECK (r.entryNamed ("Init").rig.parameters.count (someFine) == 0);
}

TEST_CASE ("< and > step through the presets in order", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("Second"));   // filed after bank A's factory presets, at A08
    REQUIRE (r.processor.saveCurrentRigAs ("Third"));    // A09
    CHECK (r.entryNamed ("Second").rig.getSlotName() == "A08");
    r.processor.loadRig (r.entryNamed ("Crocell Metal"));   // A07, the last factory one
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Second");
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Third");
    r.processor.stepRig (1);   // wraps round to the first
    CHECK (r.processor.getCurrentRig().name == "Init");
    r.processor.stepRig (-1);   // and back to the last
    CHECK (r.processor.getCurrentRig().name == "Third");
}

TEST_CASE ("Deleting the loaded preset keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (someTime, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (someTime), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which preset was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (someTime, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Preset"));
        r.set (someTime, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Preset");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (someTime)->load(), WithinAbs (3.0, 0.01));
}

TEST_CASE ("Each preset's LEVEL adds to OUTPUT", "[rigs]")
{
    const auto play = [] (float levelDb)
    {
        RigRig r;
        r.set (ParamIDs::patchLevel, levelDb);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 127), 0);   // the high tom (no noise: the same both times)
        double sum = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            r.processor.processBlock (buffer, midi);
            midi.clear();
            if (block >= 20)
                sum += buffer.getRMSLevel (0, 0, 256);
        }
        return sum;
    };
    CHECK_THAT (play (6.0f) / play (0.0f), WithinAbs (1.9953, 0.01));
}

TEST_CASE ("Presets live in their own Paragon PR7-I folder, apart from its siblings' rigs", "[rigs]")
{
    const auto folder = RigLibrary::defaultFolder();
    CHECK (folder.getFileName() == "Rigs");
    CHECK (folder.getParentDirectory().getFileName() == "Paragon PR7-I");
    CHECK (folder.getParentDirectory().getParentDirectory().getFileName() == "Paragon");

    // PR7-E's (mixing), PR7-B's (bass), PR7-G's (guitar) and PR7-K's (synth) rigs folders sit next to it, and are
    // different folders.
    for (const auto* sibling : { "Paragon PR7-E", "Paragon PR7-B", "Paragon PR7-G", "Paragon PR7-K" })
    {
        const auto siblingFolder = folder.getParentDirectory().getSiblingFile (sibling).getChildFile ("Rigs");
        CHECK (folder != siblingFolder);
        CHECK_FALSE (folder.isAChildOf (siblingFolder.getParentDirectory()));
    }
}

TEST_CASE ("Only .pr7rig files are rigs: the old Paragon X1 .x1rig files aren't read", "[rigs]")
{
    TempRigFolder temp;
    REQUIRE (temp.folder.createDirectory());
    Rig old;
    old.name = "From X1";
    REQUIRE (old.toXml()->writeTo (temp.folder.getChildFile ("From X1.x1rig")));

    RigLibrary library (temp.folder);
    CHECK (std::none_of (library.getEntries().begin(), library.getEntries().end(),
                         [] (const auto& entry) { return entry.rig.name == "From X1"; }));
}
