// THE DRUMS, 6c part 2: kits and sequences saved and loaded (and LINK), < > through a sample's folder, choosing
// several drums for routing, and the rows' playheads.
#include "TestUtils.h"

#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/DrumPage.h"

namespace
{
    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7k-kit-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        explicit Plugin (const juce::File& presets)
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
            processor.useRigFolder (presets);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        float get (const juce::String& id) { return processor.getParameters().getParameter (id)->convertFrom0to1 (processor.getParameters().getParameter (id)->getValue()); }
    };

    juce::File writeTone (const juce::File& file, double hz)
    {
        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 1, 16, {}, 0));
        juce::AudioBuffer<float> buffer (1, 4410);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 44100.0));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("DRUM KITS: a kit keeps every drum's settings, samples and envelopes; a sequence its patterns", "[drumfiles]")
{
    TempFolder temp;
    const auto tone = writeTone (temp.folder.getChildFile ("tone.wav"), 500.0);
    Plugin p (temp.folder);
    p.set (ParamIDs::drumsLevel, -4.0f);
    p.set (ParamIDs::drum (5, "Pitch"), 3.5f);
    p.set (ParamIDs::drum (5, "LowPass"), 3000.0f);
    p.set (ParamIDs::drum (9, "Choke"), 2.0f);
    REQUIRE (p.processor.loadDrumSample (2, tone));
    auto m = p.processor.getModMatrix();
    m.drumEnvelopes[5] = { { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
    synth::drumseq::Note n;
    n.row = 4;
    n.start = 6.0;
    m.drumPatterns[2].notes = { n };
    p.processor.setModMatrix (m);
    p.set (ParamIDs::dseqLength, 8.0f);
    p.processor.drumLink = false;
    REQUIRE (p.processor.saveDrumKit ("Room Kit"));
    REQUIRE (p.processor.saveDrumSequence ("Busy Beat"));
    CHECK (p.processor.drumKitsFolder().getChildFile ("Room Kit.pr7kit").existsAsFile());
    CHECK (p.processor.drumSequencesFolder().getChildFile ("Busy Beat.pr7seq").existsAsFile());
    CHECK_FALSE (p.processor.drumSequencesFolder().getChildFile ("Room Kit.pr7seq").existsAsFile());   // no LINK: just the kit
    CHECK (p.processor.listDrumKits().size() == 1);

    // Another instance: a kit sets the kit (the rest of its kit settings to new), and leaves the effects and the beat.
    Plugin q (temp.folder);
    q.processor.drumLink = false;
    q.set (ParamIDs::drum (7, "Decay"), 0.9f);      // not in the kit file's... it is: back to new
    q.set (ParamIDs::reverbSize, 0.9f);              // the effects: untouched
    REQUIRE (q.processor.loadDrumKit (p.processor.drumKitsFolder().getChildFile ("Room Kit.pr7kit")));
    CHECK_THAT (q.get (ParamIDs::drumsLevel), WithinAbs (-4.0, 0.01));
    CHECK_THAT (q.get (ParamIDs::drum (5, "Pitch")), WithinAbs (3.5, 0.01));
    CHECK_THAT (q.get (ParamIDs::drum (5, "LowPass")), WithinAbs (3000.0, 1.0));
    CHECK (juce::roundToInt (q.get (ParamIDs::drum (9, "Choke"))) == 2);
    CHECK_THAT (q.get (ParamIDs::drum (7, "Decay")), WithinAbs (synth::drums::slotDefaults[7].decay, 0.001));
    CHECK_THAT (q.get (ParamIDs::reverbSize), WithinAbs (0.9, 0.001));
    REQUIRE (q.processor.getDrumSample (2) != nullptr);
    CHECK (q.processor.getDrumSample (2)->path == tone.getFullPathName());
    CHECK (q.processor.getModMatrix().drumEnvelopes[5] == m.drumEnvelopes[5]);
    CHECK (q.processor.getModMatrix().drumPatterns[2].isEmpty());   // the kit doesn't bring a beat
    CHECK (q.processor.getDrumKitName() == "Room Kit");

    REQUIRE (q.processor.loadDrumSequence (p.processor.drumSequencesFolder().getChildFile ("Busy Beat.pr7seq")));
    CHECK (q.processor.getModMatrix().drumPatterns[2] == m.drumPatterns[2]);
    CHECK (juce::roundToInt (q.get (ParamIDs::dseqLength)) == 8);
    CHECK (q.processor.getModMatrix().drumEnvelopes[5] == m.drumEnvelopes[5]);   // the sequence doesn't touch the kit

    // One undo step takes a load back.
    q.processor.flushUndoStep();
    {
        auto* pitch = q.processor.getParameters().getParameter (ParamIDs::drum (5, "Pitch"));
        pitch->beginChangeGesture();   // as the page's knob does (an undo step at its end)
        pitch->setValueNotifyingHost (pitch->convertTo0to1 (0.0f));
        pitch->endChangeGesture();
    }
    q.processor.flushUndoStep();
    REQUIRE (q.processor.loadDrumKit (p.processor.drumKitsFolder().getChildFile ("Room Kit.pr7kit")));
    q.processor.flushUndoStep();
    CHECK_THAT (q.get (ParamIDs::drum (5, "Pitch")), WithinAbs (3.5, 0.01));
    q.processor.undo();
    CHECK_THAT (q.get (ParamIDs::drum (5, "Pitch")), WithinAbs (0.0, 0.01));
}

TEST_CASE ("DRUM KITS: LINK saves and loads the kit and sequence of the same name together", "[drumfiles]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    CHECK (p.processor.drumLink.load());   // on by default
    synth::drumseq::Note n;
    auto m = p.processor.getModMatrix();
    m.drumPatterns[0].notes = { n };
    p.processor.setModMatrix (m);
    p.set (ParamIDs::drum (0, "Gain"), 5.0f);
    REQUIRE (p.processor.saveDrumKit ("Song One"));
    CHECK (p.processor.drumSequencesFolder().getChildFile ("Song One.pr7seq").existsAsFile());

    Plugin q (temp.folder);
    REQUIRE (q.processor.loadDrumKit (p.processor.drumKitsFolder().getChildFile ("Song One.pr7kit")));
    CHECK (q.processor.getModMatrix().drumPatterns[0].notes.size() == 1);
    CHECK (q.processor.getDrumSequenceName() == "Song One");
    Plugin r (temp.folder);
    REQUIRE (r.processor.loadDrumSequence (p.processor.drumSequencesFolder().getChildFile ("Song One.pr7seq")));
    CHECK_THAT (r.get (ParamIDs::drum (0, "Gain")), WithinAbs (5.0, 0.05));
    CHECK (r.processor.getDrumKitName() == "Song One");

    // Names are cleaned for the file system; an empty one isn't saved.
    CHECK_FALSE (p.processor.saveDrumKit ("   "));
    REQUIRE (p.processor.saveDrumKit ("Kit: 2/3"));
    CHECK (p.processor.listDrumKits().size() == 2);
}

TEST_CASE ("DRUM KITS: < > step through the sample's folder by name, round and round", "[drumfiles]")
{
    TempFolder temp;
    const auto folder = temp.folder.getChildFile ("Kicks");
    folder.createDirectory();
    for (const auto& name : { "Kick 1.wav", "Kick 2.wav", "Kick 10.wav" })
        writeTone (folder.getChildFile (name), 100.0);
    folder.getChildFile ("notes.txt").replaceWithText ("not audio");
    Plugin p (temp.folder);
    REQUIRE (p.processor.loadDrumSample (0, folder.getChildFile ("Kick 2.wav")));
    p.set (ParamIDs::drum (0, "SampleEnd"), 0.5f);
    // In natural order, Kick 1, Kick 2, Kick 10, then round to the start.
    REQUIRE (p.processor.loadNeighbourSample (0, 1));
    CHECK (p.processor.getDrumSample (0)->name == "Kick 10");
    CHECK_THAT (p.get (ParamIDs::drum (0, "SampleEnd")), WithinAbs (1.0, 1e-4));   // a new file plays whole
    REQUIRE (p.processor.loadNeighbourSample (0, 1));
    CHECK (p.processor.getDrumSample (0)->name == "Kick 1");
    REQUIRE (p.processor.loadNeighbourSample (0, -1));
    CHECK (p.processor.getDrumSample (0)->name == "Kick 10");
    REQUIRE (p.processor.loadNeighbourSample (0, -1));
    CHECK (p.processor.getDrumSample (0)->name == "Kick 2");
    // A drum synth (no file) has no folder to step through.
    CHECK_FALSE (p.processor.loadNeighbourSample (3, 1));
}

TEST_CASE ("DRUMS page: choosing several drums sends a routing change to all of them, from the page only", "[drumfiles][ui]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    page.clickRow (6, {});
    page.clickRow (8, juce::ModifierKeys::ctrlModifier);
    page.clickRow (10, juce::ModifierKeys::ctrlModifier);
    CHECK (page.getChosen() == std::vector<int> { 6, 8, 10 });
    CHECK (page.getSelected() == 10);
    page.clickRow (4, {});
    page.clickRow (7, juce::ModifierKeys::shiftModifier);
    CHECK (page.getChosen() == std::vector<int> { 4, 5, 6, 7 });

    // A change made on the page (a gesture) to the chosen drum's HP: every chosen drum gets it.
    auto* hp = p.processor.getParameters().getParameter (ParamIDs::drum (7, "HighPass"));
    hp->beginChangeGesture();
    hp->setValueNotifyingHost (hp->convertTo0to1 (400.0f));
    hp->endChangeGesture();
    for (const auto k : { 4, 5, 6, 7 })
        CHECK_THAT (p.get (ParamIDs::drum (k, "HighPass")), WithinAbs (400.0, 1.0));
    CHECK_THAT (p.get (ParamIDs::drum (8, "HighPass")), WithinAbs (20.0, 0.01));
    // Automation (no gesture) moves only its own drum.
    hp->setValueNotifyingHost (hp->convertTo0to1 (800.0f));
    CHECK_THAT (p.get (ParamIDs::drum (6, "HighPass")), WithinAbs (400.0, 1.0));
    // The shaping knobs aren't routing: just the drum shown.
    auto* pitch = p.processor.getParameters().getParameter (ParamIDs::drum (7, "Pitch"));
    pitch->beginChangeGesture();
    pitch->setValueNotifyingHost (pitch->convertTo0to1 (5.0f));
    pitch->endChangeGesture();
    CHECK_THAT (p.get (ParamIDs::drum (6, "Pitch")), WithinAbs (synth::drums::slotDefaults[6].pitch, 0.01));
    // CTRL takes one out; one alone is plain again.
    page.clickRow (4, juce::ModifierKeys::ctrlModifier);
    CHECK (page.getChosen() == std::vector<int> { 5, 6, 7 });
    page.clickRow (2, {});
    CHECK (page.getChosen() == std::vector<int> { 2 });
}

TEST_CASE ("DRUMS page: each row's playhead follows its hit", "[drumfiles][ui]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);   // A2, the high tom: it rings
    p.processor.processBlock (buffer, midi);
    midi.clear();
    for (int b = 0; b < (int) (0.1 * sampleRate / blockSize); ++b)
        p.processor.processBlock (buffer, midi);
    page.updateLights();
    CHECK_THAT (p.processor.getDrumKit().getHitAge (9), WithinAbs (0.1, 0.01));
}
