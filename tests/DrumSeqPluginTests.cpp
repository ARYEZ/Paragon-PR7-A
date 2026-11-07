// THE DRUM SEQUENCER (stage 6b) in the plugin: SONG following the DAW, KEY's pattern keys, the
// patterns kept by presets, the project and undo, reversed synth drums, and MIDI export. (The sequencer itself:
// DrumSeqTests.cpp.)
#include "TestUtils.h"

#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/DrumPage.h"
#include "rigs/FactoryRigs.h"

namespace
{
    struct Song final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Song song;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
            processor.setPlayHead (&song);
            processor.seedRandomForTest (1);
            set (ParamIDs::dseqOn, 1.0f);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        void addNote (int pattern, int row, double start, synth::drumseq::Reverse reverse = synth::drumseq::Reverse::off)
        {
            auto m = processor.getModMatrix();
            synth::drumseq::Note n;
            n.row = row;
            n.start = start;
            n.reverse = reverse;
            m.drumPatterns[(size_t) pattern].notes.push_back (n);
            processor.setModMatrix (m);
        }
        // Plays `seconds` with these keys (note, on at, off at: seconds), the song moving on if it's playing.
        std::vector<float> play (double seconds, std::vector<std::tuple<int, double, double>> keys = {})
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                for (const auto& [note, on, off] : keys)
                {
                    const auto onAt = (int) (on * sampleRate) - b * blockSize, offAt = (int) (off * sampleRate) - b * blockSize;
                    if (onAt >= 0 && onAt < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), onAt);
                    if (offAt >= 0 && offAt < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOff (1, note), offAt);
                }
                buffer.clear();
                processor.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
                if (song.playing)
                    song.ppq += blockSize / sampleRate * song.bpm / 60.0;
            }
            return out;
        }
    };

    double peakOf (const std::vector<float>& x, double from = 0.0, double to = 1.0e9)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < std::min (x.size(), (size_t) (to * sampleRate)); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }

    int firstSound (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != 0.0f)
                return (int) i;
        return -1;
    }
}

TEST_CASE ("DRUM SEQUENCER in the plugin: SONG plays the pattern with the DAW, on its beat; stopped, nothing", "[drumseqplugin]")
{
    Plugin p;
    p.addNote (0, 2, 4.0);   // the snare on beat 2 (step 4 of 1/16s)
    const auto played = p.play (1.0);
    CHECK (firstSound (played) == 24000);   // half a second in, at 120 BPM
    CHECK (peakOf (played, 0.5, 0.6) > 0.1);

    // From beat 1 on: it's right there.
    Plugin later;
    later.addNote (0, 2, 4.0);
    later.song.ppq = 1.0;
    CHECK (firstSound (later.play (0.2)) == 0);

    // At 90 BPM, beat 2 is 2/3 s in.
    Plugin slower;
    slower.addNote (0, 2, 4.0);
    slower.song.bpm = 90.0;
    CHECK (std::abs (firstSound (slower.play (1.0)) - 32000) <= 1);   // (the test's song position adds up a hair short)

    // Stopped, or the sequencer or the kit off: silence.
    Plugin stopped;
    stopped.addNote (0, 2, 0.0);
    stopped.song.playing = false;
    CHECK (peakOf (stopped.play (0.5)) == 0.0);
    Plugin off;
    off.addNote (0, 2, 0.0);
    off.set (ParamIDs::dseqOn, 0.0f);
    CHECK (peakOf (off.play (0.5)) == 0.0);

    // PATTERN chooses which plays.
    Plugin b;
    b.addNote (1, 2, 0.0);
    CHECK (peakOf (b.play (0.3)) == 0.0);
    b.set (ParamIDs::dseqPattern, 1.0f);
    CHECK (peakOf (b.play (2.0), 1.7, 1.8) > 0.1);   // B at the next bar (beat 4: 1.7 s on from beat 0.6)
}

TEST_CASE ("DRUM SEQUENCER in the plugin: KEY: the four keys under the kit play patterns", "[drumseqplugin]")
{
    Plugin p;
    p.set (ParamIDs::dseqTrigger, 1.0f);
    p.song.playing = false;
    p.addNote (1, 2, 0.0);   // B: the snare at once
    // A (key 32) is empty: silence.
    CHECK (peakOf (p.play (0.4, { { 32, 0.1, 0.3 } })) == 0.0);
    // B (key 33): the snare from the key, each bar while held.
    const auto played = p.play (4.5, { { 33, 0.1, 2.2 } });
    CHECK (firstSound (played) == (int) (0.1 * sampleRate));
    CHECK (peakOf (played, 2.1, 2.15) > 0.05);   // the next bar, 2 s on
    CHECK (peakOf (played, 4.05) < 1.0e-4);      // let go at 2.2 s: no bar after (the last hit rang out by then)

    // On SONG, those keys do nothing (they're below the kit).
    Plugin song;
    song.song.playing = false;
    song.addNote (1, 2, 0.0);
    CHECK (peakOf (song.play (0.3, { { 33, 0.0, 0.2 } })) == 0.0);
}

TEST_CASE ("DRUM SEQUENCER in the plugin: the patterns are kept by presets, the project and undo", "[drumseqplugin]")
{
    Plugin p;
    p.processor.flushUndoStep();
    p.addNote (0, 0, 0.0);
    p.addNote (3, 11, 6.5, synth::drumseq::Reverse::endSync);
    p.processor.modMatrixEditFinished();
    p.processor.flushUndoStep();
    const auto rig = p.processor.captureRig();
    const auto reread = Rig::fromXml (*rig.toXml());
    REQUIRE (reread.has_value());
    CHECK (reread->modulation.drumPatterns == p.processor.getModMatrix().drumPatterns);
    CHECK (reread->modulation.drumPatterns[3].notes.size() == 1);

    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin other;
    other.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.processor.getModMatrix().drumPatterns == p.processor.getModMatrix().drumPatterns);

    auto cleared = p.processor.getModMatrix();
    cleared.drumPatterns[3].notes.clear();
    p.processor.setModMatrix (cleared);
    p.processor.modMatrixEditFinished();
    p.processor.flushUndoStep();
    CHECK (p.processor.getModMatrix().drumPatterns[3].isEmpty());
    p.processor.undo();
    CHECK (p.processor.getModMatrix().drumPatterns[3].notes.size() == 1);
    // A new preset has none.
    CHECK (ModMatrix {}.drumPatterns.size() == 4);
    CHECK (ModMatrix {}.isEmpty());
}

TEST_CASE ("DRUM SEQUENCER in the plugin: a reversed synth drum plays its hit backwards, swelling to its end", "[drumseqplugin]")
{
    Plugin p;
    p.addNote (0, 2, 0.0, synth::drumseq::Reverse::reverse);   // the snare
    CHECK (p.processor.getKitSettings().slots[2].reverseSource == nullptr);   // made by the timer
    p.processor.updateDrumReverses();
    const auto* source = p.processor.getKitSettings().slots[2].reverseSource;
    REQUIRE (source != nullptr);
    const auto seconds = source->length() / sampleRate;
    CHECK (seconds > 0.1);
    CHECK (seconds < 1.5);
    // Only reversed rows get one.
    CHECK (p.processor.getKitSettings().slots[0].reverseSource == nullptr);

    const auto played = p.play (seconds + 0.2);
    CHECK (peakOf (played, 0.0, 0.03) < 0.2 * peakOf (played, seconds - 0.03, seconds));
    CHECK (peakOf (played, seconds + 0.01) < 1.0e-6);

    // Changing the drum makes it again.
    p.set (ParamIDs::drum (2, "Decay"), 0.1f);
    p.processor.updateDrumReverses();
    CHECK (p.processor.getKitSettings().slots[2].reverseSource->length() < source->length());
}

TEST_CASE ("DRUM SEQUENCER in the plugin: MIDI export puts the pattern on the kit's keys", "[drumseqplugin]")
{
    Plugin p;
    p.set (ParamIDs::drumsRoot, 48.0f);
    p.addNote (2, 0, 0.0);
    p.addNote (2, 3, 2.0);
    const auto file = p.processor.drumPatternToMidi (2);
    std::vector<std::pair<int, double>> ons;
    for (const auto* e : *file.getTrack (0))
        if (e->message.isNoteOn())
            ons.emplace_back (e->message.getNoteNumber(), e->message.getTimeStamp());
    CHECK (ons == std::vector<std::pair<int, double>> { { 48, 0.0 }, { 51, 480.0 } });
    // A drum with an anchor (6c) is written early: 62.5 ms at 120 BPM is an eighth of a beat.
    p.set (ParamIDs::drum (3, "Delay"), -62.5f);
    ons.clear();
    const auto early = p.processor.drumPatternToMidi (2);
    for (const auto* e : *early.getTrack (0))
        if (e->message.isNoteOn())
            ons.emplace_back (e->message.getNoteNumber(), e->message.getTimeStamp());
    CHECK (ons == std::vector<std::pair<int, double>> { { 48, 0.0 }, { 51, 360.0 } });
    const auto drag = p.processor.drumMidiDragFile (2);
    CHECK (drag.existsAsFile());
    CHECK (drag.getFileName() == "Drums C.mid");
}

TEST_CASE ("DRUMS page, SEQUENCER: the grid adds, moves, chooses and removes notes; the fields set the chosen ones", "[drumseqplugin][ui]")
{
    Plugin p;
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    page.showView (DrumPage::View::sequencer);
    CHECK (page.getSequencer().isVisible());
    CHECK_FALSE (page.getDisplay().isVisible());
    auto& grid = page.getSequencer().getGrid();
    // Its rows line up with the kit's.
    for (int r = 0; r < 12; ++r)
    {
        const auto row = page.getRow (r).getBoundsInParent();
        const auto cell = page.getLocalArea (&grid, grid.rowArea (r)).toNearestInt();
        CHECK (std::abs (row.getY() - cell.getY()) <= 1);
        CHECK (std::abs (row.getBottom() - cell.getBottom()) <= 1);
    }

    const auto pattern = [&p] (int k = 0) { return p.processor.getModMatrix().drumPatterns[(size_t) k]; };
    const auto mouse = [&grid] (juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &grid, &grid, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    const auto click = [&] (juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        grid.mouseDown (mouse (at, mods));
        grid.mouseUp (mouse (at, mods));
    };

    // PEN: a click on the snare's row, step 4, adds a note there (and chooses the snare on the left).
    click (grid.cell (2, 4.0).getCentre());
    REQUIRE (pattern().notes.size() == 1);
    CHECK (pattern().notes[0].row == 2);
    CHECK (pattern().notes[0].start == 4.0);
    CHECK (page.getSelected() == 2);
    CHECK (grid.getSelection() == std::vector<int> { 0 });

    // Dragged along the hats' row: a note in each step passed.
    grid.mouseDown (mouse (grid.cell (6, 0.0).getCentre()));
    for (int k = 1; k <= 3; ++k)
        grid.mouseDrag (mouse (grid.cell (6, k).getCentre()));
    grid.mouseUp (mouse (grid.cell (6, 3.0).getCentre()));
    CHECK (pattern().notes.size() == 5);

    // The snare dragged a step later and to the clap's row; with ALT, a quarter of a step off the grid.
    grid.mouseDown (mouse (grid.cell (2, 4.0).getCentre()));
    grid.mouseDrag (mouse (grid.cell (3, 5.0).getCentre()));
    grid.mouseUp (mouse (grid.cell (3, 5.0).getCentre()));
    CHECK (pattern().notes[0].row == 3);
    CHECK (pattern().notes[0].start == 5.0);
    const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    grid.mouseDown (mouse (grid.cell (3, 5.0).getCentre(), alt));
    grid.mouseDrag (mouse (grid.cell (3, 5.25).getCentre(), alt));
    grid.mouseUp (mouse (grid.cell (3, 5.25).getCentre(), alt));
    CHECK (pattern().notes[0].start == 5.25);

    // SELECT: a frame round the first two hats chooses them; the fields set both.
    grid.tool = DrumSeqGrid::Tool::select;
    grid.mouseDown (mouse (grid.cell (5, 0.0).getTopLeft()));
    grid.mouseDrag (mouse (grid.cell (7, 1.0).getBottomRight()));
    grid.mouseUp (mouse (grid.cell (7, 1.0).getBottomRight()));
    CHECK (grid.getSelection() == std::vector<int> { 1, 2 });
    auto& velocity = page.getSequencer().getField (0);
    CHECK_THAT (velocity.get(), WithinAbs (1.0, 1e-6));
    velocity.set (0.5f, true);
    CHECK (pattern().notes[1].velocity == 0.5f);
    CHECK (pattern().notes[2].velocity == 0.5f);
    CHECK (pattern().notes[3].velocity == 1.0f);
    page.getSequencer().getField (4).set (2.0f, true);   // ROLL 3x
    CHECK (pattern().notes[1].rollCount() == 3);
    page.getSequencer().getField (5).set (1.0f, true);   // REVERSE
    CHECK (pattern().notes[2].reverse == synth::drumseq::Reverse::reverse);

    // DELETE removes them; a right-click removes one; undo brings it back.
    grid.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (pattern().notes.size() == 3);
    p.processor.flushUndoStep();
    click (grid.cell (6, 3.0).getCentre(), juce::ModifierKeys::rightButtonModifier);
    CHECK (pattern().notes.size() == 2);
    p.processor.flushUndoStep();
    p.processor.undo();
    CHECK (pattern().notes.size() == 3);

    // Nothing chosen: the fields show nothing.
    grid.setSelection ({});
    CHECK (std::isnan (velocity.get()));

    // Pattern B: an empty grid; its button sets PATTERN.
    page.getSequencer().selectPattern (1);   // what its button does
    CHECK (page.getSequencer().getPatternButton (1).getToggleState());
    CHECK (grid.getPattern() == 1);
    CHECK (grid.getShown().isEmpty());
    grid.tool = DrumSeqGrid::Tool::pen;
    click (grid.cell (0, 0.0).getCentre());
    CHECK (pattern (1).notes.size() == 1);
    CHECK (pattern (0).notes.size() == 3);

    page.showView (DrumPage::View::drum);
    CHECK_FALSE (page.getSequencer().isVisible());
    CHECK (page.getDisplay().isVisible());
}

TEST_CASE ("DRUM SEQUENCER in the plugin: preset A03 plays its beat with the song, and nothing while the song is stopped", "[drumseqplugin]")
{
    const auto rigs = makeFactoryRigs();
    const auto found = std::find_if (rigs.begin(), rigs.end(), [] (const Rig& r) { return r.name == "Room Beat"; });
    REQUIRE (found != rigs.end());
    CHECK (found->getSlotName() == "A03");
    for (const auto& pattern : found->modulation.drumPatterns)
        CHECK (pattern.notes.size() >= 10);

    Plugin p;
    RigLibrary::Entry entry;
    entry.rig = *found;
    entry.isFactory = true;
    p.processor.loadRig (entry);
    p.processor.updateDrumReverses();   // D's reversed cymbal
    CHECK (p.processor.getKitSettings().slots[11].reverseSource != nullptr);
    for (int pattern = 0; pattern < 4; ++pattern)
    {
        p.set (ParamIDs::dseqPattern, (float) pattern);
        p.song.ppq = 0.0;
        const auto played = p.play (2.0);
        CHECK (firstSound (played) == 0);               // each starts on the kick
        CHECK (peakOf (played, 0.5, 0.6) > 0.05);      // and a snare (or the half-time's hats) on beat 2
    }
    p.song.playing = false;
    p.play (3.0);   // the last hits ring out
    CHECK (peakOf (p.play (1.0)) < 1.0e-5);
}
