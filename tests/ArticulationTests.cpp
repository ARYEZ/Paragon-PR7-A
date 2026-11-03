// ARTICULATIONS (PR7-D 0.17.0): the pieces and their articulations, an articulation switched off playing its stand-in,
// the hi-hat pedal (CC 4) choosing the openness, a cymbal grabbed (MUTE TAIL), older presets / kits / projects loading
// with the new articulations off, the new drums' sound, and the articulation list on the KIT VIEW.
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "dsp/synth/NoteMap.h"
#include "rigs/Rig.h"
#include "ui/DrumPage.h"

#include <set>

namespace
{
    using namespace synth::drums;
    using namespace synth::drums::notemap;

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
        // `seconds` of the plugin's left output, with `midi` (positions in samples from the start).
        std::vector<float> play (double seconds, const juce::MidiBuffer& midi = {})
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto blocks = (int) (seconds * sampleRate / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer chunk;
                for (const auto m : midi)
                    if (m.samplePosition >= b * blockSize && m.samplePosition < (b + 1) * blockSize)
                        chunk.addEvent (m.getMessage(), m.samplePosition - b * blockSize);
                buffer.clear();
                processor.processBlock (buffer, chunk);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
        // Which drums sound after `midi`.
        std::vector<int> sounding (const juce::MidiBuffer& midi)
        {
            processor.prepareToPlay (sampleRate, blockSize);
            play (0.02, midi);
            std::vector<int> slots;
            for (int k = 0; k < numSlots; ++k)
                if (processor.getDrumKit().isSounding (k))
                    slots.push_back (k);
            return slots;
        }
        std::vector<int> sounding (int note) { return sounding (hit (note)); }
        static juce::MidiBuffer hit (int note, int at = 0)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), at);
            return midi;
        }
    };

    double hz (const std::vector<float>& x) { return frequencyOf (x, (size_t) (0.01 * sampleRate), (size_t) (0.2 * sampleRate)); }
}

TEST_CASE ("ARTICULATIONS: every drum is one articulation of one piece; stand-ins are other drums", "[articulations]")
{
    std::set<int> seen;
    for (int p = 0; p < numPieces; ++p)
    {
        const auto& piece = pieces[(size_t) p];
        CHECK (piece.count >= 1);
        for (int a = 0; a < piece.count; ++a)
        {
            const auto slot = piece.articulations[(size_t) a].slot;
            CHECK (seen.insert (slot).second);   // in one piece only
            CHECK (pieceOfSlot (slot) == p);
        }
    }
    CHECK ((int) seen.size() == numSlots);
    for (int k = 0; k < numSlots; ++k)
        for (const auto s : standIns[(size_t) k])
            if (s >= 0)
            {
                CHECK (s != k);
                CHECK (s < numSlots);
            }
    // The pieces the owner named: the snare's center, rimshot and sidestick; the hat's openness steps and pedal; the
    // ride's bow, bell and edge.
    CHECK (articulationName (SD2) == "SNARE RIMSHOT");
    CHECK (articulationName (RE) == "RIDE EDGE");
    CHECK (articulationName (HO1) == "HI-HAT OPEN 1");
    CHECK (articulationName (BD) == "KICK");
}

TEST_CASE ("ARTICULATIONS: one switched off plays its stand-in, sample for sample; all off, nothing", "[articulations]")
{
    Plugin p;
    p.layout (Layout::grooveLibrary);
    testkit::useSample (p.processor, HH, testkit::sine (300.0));    // open 2
    testkit::useSample (p.processor, HO1, testkit::sine (900.0));   // open 1

    // On: note 24 plays open 1 (900 Hz).
    p.processor.prepareToPlay (sampleRate, blockSize);
    CHECK (std::abs (hz (p.play (0.25, Plugin::hit (24))) - 900.0) < 5.0);

    // Off: open 2 plays for it, exactly as note 25 (open 2's own note) does.
    p.set (ParamIDs::drum (HO1, "On"), 0.0f);
    p.processor.prepareToPlay (sampleRate, blockSize);
    const auto standIn = p.play (0.25, Plugin::hit (24));
    p.processor.prepareToPlay (sampleRate, blockSize);
    const auto own = p.play (0.25, Plugin::hit (25));
    CHECK (std::abs (hz (standIn) - 300.0) < 5.0);
    REQUIRE (standIn.size() == own.size());
    auto worst = 0.0f;
    for (size_t i = 0; i < own.size(); ++i)
        worst = std::max (worst, std::abs (standIn[i] - own[i]));
    CHECK (worst < 1.0e-6f);
    CHECK (p.sounding (24) == std::vector<int> { HH });

    // Down the list: open 2 off too, then closed (HO1's next stand-in).
    p.set (ParamIDs::drum (HH, "On"), 0.0f);
    CHECK (p.sounding (24) == std::vector<int> { CH });

    // Across pieces: the ride's edge off plays its bow; the bow off too, its bell... then the crashes.
    CHECK (p.sounding (59) == std::vector<int> { RE });
    p.set (ParamIDs::drum (RE, "On"), 0.0f);
    CHECK (p.sounding (59) == std::vector<int> { RD });
    p.set (ParamIDs::drum (RD, "On"), 0.0f);
    CHECK (p.sounding (59) == std::vector<int> { CR2 });

    // A drum with no stand-in (the kick) off: silent.
    p.set (ParamIDs::drum (BD, "On"), 0.0f);
    CHECK (p.sounding (36).empty());
}

TEST_CASE ("ARTICULATIONS: the hi-hat pedal (CC 4) picks the openness of the pedal note; grooves' own notes don't follow it", "[articulations]")
{
    Plugin p;
    p.layout (Layout::grooveLibrary);
    const auto withPedal = [&p] (int cc, int note)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, cc), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), 1);
        return p.sounding (midi);
    };
    // The default steps: 102, 77, 51, 26.
    CHECK (withPedal (127, 7) == std::vector<int> { CH });
    CHECK (withPedal (102, 7) == std::vector<int> { CH });
    CHECK (withPedal (101, 7) == std::vector<int> { HO1 });
    CHECK (withPedal (60, 7) == std::vector<int> { HH });
    CHECK (withPedal (30, 7) == std::vector<int> { HO3 });
    CHECK (withPedal (0, 7) == std::vector<int> { OH });
    // The pedal is remembered between blocks: the note alone, after the pedal opened.
    CHECK (p.processor.getDrumKit().getPedal() == 0);
    p.play (0.01);
    CHECK (p.sounding (7) == std::vector<int> { OH });
    // A groove's open-hat note plays its own step whatever the pedal says.
    CHECK (withPedal (127, 26) == std::vector<int> { HO3 });

    // The steps moved: closed only at the very top.
    p.set (ParamIDs::drumsPedalSteps[0], 120.0f);
    CHECK (withPedal (110, 7) == std::vector<int> { HO1 });
    // Kept out of presets (they suit the player's pedal).
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::drumsPedalSteps[0]));
}

TEST_CASE ("ARTICULATIONS: MUTE TAIL grabs a cymbal on note-off or aftertouch, the whole piece; OFF lets it ring", "[articulations]")
{
    const auto levelAfter = [] (MuteTail tail, juce::MidiMessage release, int note, int checkSlot)
    {
        Plugin p;
        p.layout (Layout::grooveLibrary);
        for (const auto slot : { RD, RB, RE, CY })
            p.set (ParamIDs::drum (slot, "MuteTail"), (float) tail);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 110), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 53, (juce::uint8) 110), 0);   // the ride's bell too
        release.setChannel (10);
        midi.addEvent (release, (int) (0.2 * sampleRate));
        const auto out = p.play (0.6, midi);
        return std::pair { rms (out, (size_t) (0.45 * sampleRate), std::min (out.size(), (size_t) (0.58 * sampleRate))),
                           p.processor.getDrumKit().isSounding (checkSlot) };
    };
    // The ride's bow, let go at 0.2 s.
    const auto [ringing, bellRings] = levelAfter (MuteTail::off, juce::MidiMessage::noteOff (10, 51), 51, RB);
    const auto [grabbed, bellGrabbed] = levelAfter (MuteTail::noteOff, juce::MidiMessage::noteOff (10, 51), 51, RB);
    CHECK (ringing > 0.002);
    CHECK (bellRings);
    CHECK (grabbed < ringing * 0.001);   // over 60 dB down a quarter second later
    CHECK_FALSE (bellGrabbed);            // the bell stopped with it: one cymbal
    // Aftertouch (a pad squeezed): the same, and a note-off then does nothing.
    const auto [squeezed, _] = levelAfter (MuteTail::aftertouch, juce::MidiMessage::aftertouchChange (10, 51, 100), 51, RB);
    CHECK (squeezed < ringing * 0.001);
    const auto [letGo, __] = levelAfter (MuteTail::aftertouch, juce::MidiMessage::noteOff (10, 51), 51, RB);
    CHECK (std::abs (letGo - ringing) < ringing * 0.01);
}

TEST_CASE ("ARTICULATIONS: older presets, kits and projects load with the three new drums off and sound as before", "[articulations]")
{
    // A preset saved before 0.17.0 (parameterVersion 1) without drum21On...
    juce::XmlElement old ("ParagonRig");
    old.setAttribute ("version", 1);
    old.setAttribute ("parameterVersion", 1);
    old.setAttribute ("name", "Old");
    auto* param = old.createNewChildElement ("Param");
    param->setAttribute ("id", "drum1Level");
    param->setAttribute ("value", -3.0);
    const auto rig = Rig::fromXml (old);
    REQUIRE (rig.has_value());
    for (const auto& id : ParamIDs::articulationsAddedOff)
        CHECK (rig->parameters.at (id) == 0.0f);
    // ...and one saved now keeps them as they were.
    Rig current;
    current.name = "New";
    current.parameters["drum21On"] = 1.0f;
    const auto reloaded = Rig::fromXml (*current.toXml());
    REQUIRE (reloaded.has_value());
    CHECK (reloaded->parameters.at ("drum21On") == 1.0f);

    // A project saved before: its state's parameterVersion 1.
    Plugin saved;
    juce::MemoryBlock state;
    saved.processor.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    xml->setAttribute ("parameterVersion", 1);
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldState);
    Plugin loaded;
    loaded.processor.setStateInformation (oldState.getData(), (int) oldState.getSize());
    const auto kit = loaded.processor.getKitSettings();
    CHECK_FALSE (kit.slots[(size_t) RE].active);
    CHECK_FALSE (kit.slots[(size_t) HO1].active);
    CHECK_FALSE (kit.slots[(size_t) HO3].active);
    CHECK (kit.slots[(size_t) RD].active);
    // So the ride's edge note plays the bow, as it did before 0.17.0.
    loaded.layout (Layout::grooveLibrary);
    CHECK (loaded.sounding (59) == std::vector<int> { RD });
    // A project saved now keeps them on.
    Plugin again;
    again.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (again.processor.getKitSettings().slots[(size_t) RE].active);
}

TEST_CASE ("ARTICULATIONS: the new drums: the ride's edge darker and longer than a crash, the hat's steps between closed and open", "[articulations]")
{
    const auto settings = [] (int slot)
    {
        SlotSettings s;
        const auto& d = slotDefaults[(size_t) slot];
        s.model = d.model;
        s.pitch = d.pitch;
        s.decay = d.decay;
        s.tone = d.tone;
        s.snap = d.snap;
        return s;
    };
    const auto ring = [] (const juce::AudioBuffer<float>& b)
    {
        // Seconds until it stays 40 dB under its peak.
        const auto* x = b.getReadPointer (0);
        auto peak = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        auto last = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (x[i]) > peak * 0.01f)
                last = i;
        return last / sampleRate;
    };
    const auto hitOf = [&settings] (int slot) { return Kit::renderHit (settings (slot), 1.0f, sampleRate, 4.0); };
    CHECK (ring (hitOf (RE)) > ring (hitOf (CY)));
    // The hat opens step by step: each rings longer than the one before.
    const int steps[] = { CH, HO1, HH, HO3, OH };
    for (int i = 1; i < 5; ++i)
    {
        INFO (slotDefaults[(size_t) steps[i]].key);
        CHECK (ring (hitOf (steps[i])) > ring (hitOf (steps[i - 1])));
    }
    // Each near the kit's -3 dBFS default hit.
    for (const auto slot : { RE, HO1, HO3 })
    {
        const auto b = hitOf (slot);
        CHECK (std::abs (juce::Decibels::gainToDecibels (b.getMagnitude (0, 0, b.getNumSamples())) + 3.0f) < 3.0f);
    }
}

TEST_CASE ("ARTICULATIONS: the list on the KIT VIEW: the chosen drum's piece, ALL, ON, stand-ins, the pedal's steps, MUTE TAIL", "[articulations]")
{
    Plugin p;
    p.layout (Layout::grooveLibrary);
    DrumPage page (p.processor);
    page.setSize (900, 700);
    auto& list = page.getArticulations();

    page.clickRow (SD2, {});
    CHECK (list.getPiece() == pieceOfSlot (SD));
    CHECK (list.describeRows() == juce::StringArray { "Center D2 ON", "Rimshot E2 ON", "Sidestick C#2 ON" });

    // ALL chooses the piece's three; the DRUM page's settings then go to all of them.
    list.clickAll();
    const auto chosen = page.getChosen();
    CHECK (std::set<int> (chosen.begin(), chosen.end()) == std::set<int> { SD, SD2, RIM });

    // A name chooses that one alone (and plays it).
    list.clickName (RIM);
    CHECK (page.getChosen() == std::vector<int> { RIM });

    // ON off: the stand-in named.
    list.toggleOn (SD2);
    CHECK (list.describeRows()[1] == "Rimshot E2 off -> SNARE CENTER");
    CHECK (p.processor.getKitSettings().playsAs (SD2) == SD);
    list.toggleOn (SD2);
    CHECK (p.processor.getKitSettings().slots[(size_t) SD2].active);

    // The hat: its steps in order; a mark can't pass its neighbour.
    page.clickRow (CH, {});
    CHECK (list.getPiece() == pieceOfSlot (OH));
    CHECK (list.describeRows().size() == 7);   // (0.33.0: and its shoulder)
    list.setPedalStep (1, 120);   // open 1's mark dragged past closed's (102): stops just under it
    CHECK (p.processor.getKitSettings().pedalSteps[1] == 101);
    list.setPedalStep (1, 80);
    CHECK (p.processor.getKitSettings().pedalSteps[1] == 80);

    // The ride: MUTE TAIL for all of it.
    page.clickRow (RB, {});
    list.setMuteTail (MuteTail::noteOff);
    const auto kit = p.processor.getKitSettings();
    for (const auto slot : { RD, RB, RE })
        CHECK (kit.slots[(size_t) slot].muteTail == MuteTail::noteOff);
    CHECK (kit.slots[(size_t) CY].muteTail == MuteTail::off);
    CHECK (list.getHeight() == list.wantedHeight());
}
