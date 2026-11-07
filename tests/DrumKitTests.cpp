// THE DRUMS (PR7-K's stage 6a) in the plugin: the kit's keys, its sound in the mix, a bus and the sends, samples kept
// through presets, the project and undo, and the page's preview. (The kit itself: DrumTests.cpp.)
#include "TestUtils.h"

#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/DrumPage.h"
#include "dsp/effects/Drive.h"

namespace
{
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
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        // Plays these notes (note, at seconds; each held 0.1 s) for `seconds`, returning both sides.
        std::array<std::vector<float>, 2> play (std::vector<std::pair<int, double>> notes, double seconds)
        {
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                for (const auto& [note, at] : notes)
                {
                    const auto on = (int) (at * sampleRate) - b * blockSize, off = on + (int) (0.1 * sampleRate);
                    if (on >= 0 && on < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), on);
                    if (off >= 0 && off < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOff (1, note), off);
                }
                buffer.clear();
                processor.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
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

    juce::File writeSineWav (double hz, double seconds)
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pr7d-drum-test.wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 1, 24, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> buffer (1, (int) (seconds * 44100.0));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 44100.0));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("DRUMS in the plugin: the kit's 12 keys play the drums, each its own; MUTE silences one", "[drumkit]")
{
    // C2 (MIDI 36) plays the kick; muted, nothing; the snare's key still plays.
    Plugin p;
    CHECK (peakOf (p.play ({ { 36, 0.0 } }, 0.3)[0]) > 0.05);
    Plugin muted;
    muted.set (ParamIDs::drum (0, "Mute"), 1.0f);
    CHECK (peakOf (muted.play ({ { 36, 0.0 } }, 0.3)[0]) == 0.0);
    CHECK (peakOf (muted.play ({ { 38, 0.0 } }, 0.3)[0]) > 0.05);

    // A key outside the kit plays nothing at all (PR7-K played its synth there).
    Plugin outside;
    const auto out = outside.play ({ { 36 + synth::drums::numSlots, 0.0 }, { 30, 0.1 } }, 0.5);
    CHECK (peakOf (out[0]) == 0.0);
    CHECK (peakOf (out[1]) == 0.0);
}

TEST_CASE ("DRUMS in the plugin: the kit plays at its level through OUTPUT, and ROOT moves its keys", "[drumkit]")
{
    Plugin p;
    p.set (ParamIDs::outputGain, 0.0f);
    p.set (ParamIDs::drumsRoot, 48.0f);
    p.play ({}, 0.2);   // OUTPUT glides to its new setting first
    const auto hit = p.play ({ { 48 + 5, 0.0 } }, 1.0);   // F: the low tom
    // Exactly the kit's own hit (the rack is neutral by default): the same peak as the slot rendered alone.
    const auto alone = synth::drums::Kit::renderHit (p.processor.getKitSettings().slots[5], 1.0f, sampleRate, 1.0);
    const auto alonePeak = db (alone.getMagnitude (0, 0, alone.getNumSamples()));
    CHECK_THAT (db (peakOf (hit[0])), WithinAbs (alonePeak, 0.5));
    CHECK_THAT (frequencyOf (hit[0], (size_t) (0.3 * sampleRate), (size_t) (0.6 * sampleRate)), WithinAbs (100.0 * std::exp2 (-5.0 / 12.0), 3.0));

    p.set (ParamIDs::drumsLevel, -12.0f);
    p.play ({}, 1.0);
    CHECK_THAT (db (peakOf (p.play ({ { 48 + 5, 0.0 } }, 1.0)[0])), WithinAbs (alonePeak - 12.0, 0.5));
}

TEST_CASE ("DRUMS in the plugin: a drum on a bus goes through its effects; the sends take the rest", "[drumkit]")
{
    Plugin plain, bused;
    for (auto* p : { &plain, &bused })
    {
        p->set (ParamIDs::outputGain, 0.0f);
    }
    bused.set (ParamIDs::drum (5, "Insert"), 1.0f);   // the low tom on bus A
    // With nothing switched on in the bus, it comes out exactly as straight into the mix.
    CHECK (plain.play ({ { 41, 0.0 } }, 0.6)[0] == bused.play ({ { 41, 0.0 } }, 0.6)[0]);
    // Through a DRIVE (RABID: a full-range fuzz; BANSHEE cuts the lows before it clips).
    bused.set (ParamIDs::ins (0, "DriveOn"), 1.0f);
    bused.set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::rabid);
    bused.set (ParamIDs::ins (0, "DriveGain"), 8.0f);
    // The tom rings at about 84 Hz; the drive adds its 3rd harmonic (not one of the tom's own partials).
    const auto hz = 100.0 * std::exp2 (-5.0 / 12.0);
    const auto third = [hz] (const std::vector<float>& x)
    {
        return db (levelAt (x, 3.0 * hz, (size_t) (0.2 * sampleRate), (size_t) (0.3 * sampleRate)) / levelAt (x, hz, (size_t) (0.2 * sampleRate), (size_t) (0.3 * sampleRate)));
    };
    const auto clean = third (plain.play ({ { 41, 0.0 } }, 0.6)[0]);
    const auto driven = third (bused.play ({ { 41, 0.0 } }, 0.6)[0]);
    CHECK (driven > clean + 20.0);

    // REVERB on: the rim's send fills the room; with its send at 0, nothing rings on after it.
    Plugin wet, dry;
    for (auto* p : { &wet, &dry })
    {
        p->set (ParamIDs::reverbOn, 1.0f);
    }
    dry.set (ParamIDs::drum (1, "SendReverb"), 0.0f);
    CHECK (peakOf (wet.play ({ { 37, 0.0 } }, 1.0)[0], 0.5) > 1.0e-4);
    CHECK (peakOf (dry.play ({ { 37, 0.0 } }, 1.0)[0], 0.5) < 1.0e-6);
}

TEST_CASE ("DRUMS in the plugin: a loaded sample plays, and is kept by presets, the project and undo", "[drumkit]")
{
    const auto file = writeSineWav (1000.0, 0.4);
    Plugin p;
    p.processor.flushUndoStep();
    REQUIRE (p.processor.loadDrumSample (2, file));
    p.processor.flushUndoStep();
    CHECK (juce::roundToInt (p.processor.getParameters().getParameter (ParamIDs::drum (2, "Model"))->convertFrom0to1 (
               p.processor.getParameters().getParameter (ParamIDs::drum (2, "Model"))->getValue())) == (int) synth::drums::Model::sample);
    const auto played = p.play ({ { 38, 0.0 } }, 0.6);
    CHECK_THAT (frequencyOf (played[0], (size_t) (0.1 * sampleRate), (size_t) (0.3 * sampleRate)), WithinAbs (1000.0, 2.0));
    CHECK (peakOf (played[0], 0.45) < 1.0e-6);   // it ends with the sample

    // Loaded into a drum synth's slot (the snare, PITCH 0 DECAY 50%...): its shaping went to neutral, to play it as recorded.
    CHECK_THAT (p.processor.getKitSettings().slots[2].decay, WithinAbs (0.5, 1.0e-4));   // (through the parameter's 0-1 range:
    CHECK_THAT (p.processor.getKitSettings().slots[2].pitch, WithinAbs (0.0, 1.0e-4));   // a hair off on Apple's compiler)

    // The preset carries it (written out and read back).
    const auto rig = p.processor.captureRig();
    REQUIRE (rig.drumSamples.count (2) == 1);
    const auto reread = Rig::fromXml (*rig.toXml());
    REQUIRE (reread.has_value());
    REQUIRE (reread->drumSamples.count (2) == 1);
    CHECK (reread->drumSamples.at (2)->length() == rig.drumSamples.at (2)->length());
    CHECK (reread->drumSamples.at (2)->sampleRate == 44100.0);

    // The project carries it.
    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin other;
    other.processor.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (other.processor.getDrumSample (2) != nullptr);
    const auto& a = p.processor.getDrumSample (2)->channels[0];
    const auto& b = other.processor.getDrumSample (2)->channels[0];
    REQUIRE (a.size() == b.size());
    auto worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    CHECK (worst < 1.0e-4f);   // 16-bit

    // Undo brings it back after it's cleared.
    p.processor.clearDrumSample (2);
    p.processor.flushUndoStep();
    CHECK (p.processor.getDrumSample (2) == nullptr);
    p.processor.undo();
    CHECK (p.processor.getDrumSample (2) != nullptr);
    file.deleteFile();
}

TEST_CASE ("DRUMS in the plugin: the page's preview plays a slot even with DRUMS off", "[drumkit]")
{
    Plugin p;
    p.processor.previewDrum (2);
    CHECK (peakOf (p.play ({}, 0.2)[0]) > 0.05);
}

TEST_CASE ("DRUMS in the plugin: hits are one-shots: a note-off doesn't stop them; they end by themselves", "[drumkit]")
{
    Plugin p;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);   // the high tom: it rings
    midi.addEvent (juce::MidiMessage::noteOff (1, 45), 10);
    p.processor.processBlock (buffer, midi);
    midi.clear();
    for (int b = 0; b < 10; ++b)
        p.processor.processBlock (buffer, midi);
    CHECK (p.processor.getActiveVoices() == 1);   // still ringing after its note-off
    for (int b = 0; b < (int) (4.5 * sampleRate) / blockSize; ++b)
        p.processor.processBlock (buffer, midi);
    CHECK (p.processor.getActiveVoices() == 0);   // and gone by itself
}

TEST_CASE ("DRUMS page: choosing a row points the drum's controls at it; every row has its hit's picture", "[drumkit][ui]")
{
    Plugin p;
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    // 0.49.0: on the KIT VIEW (where the page opens) the rows are hidden and no pictures are made; the DRUM view makes them.
    CHECK (page.getDisplay().getPicture().seconds == 0.0);
    page.showView (DrumPage::View::drum);
    page.select (3);
    CHECK (page.getSelected() == 3);
    for (auto* control : page.getSlotControls())
        CHECK (control->getParameterId().startsWith ("drum4"));
    page.select (11);
    for (auto* control : page.getSlotControls())
        CHECK (control->getParameterId().startsWith ("drum12"));
    CHECK (page.getDisplay().getPicture().seconds > 2.0);   // the cymbal rings long
    page.select (6);
    CHECK (page.getDisplay().getPicture().seconds < 0.3);   // the closed hat doesn't

    // A changed setting redraws the picture.
    p.set (ParamIDs::drum (6, "Decay"), 1.0f);
    page.refreshPictures();
    CHECK (page.getDisplay().getPicture().seconds > 0.3);

    CHECK (DrumPage::isAudioFile ("kick.wav"));
    CHECK (DrumPage::isAudioFile ("Snare.AIFF"));
    CHECK_FALSE (DrumPage::isAudioFile ("notes.txt"));
}

TEST_CASE ("DRUMS page: a file of several hits is shown whole; clicking one plays just it", "[drumkit][ui]")
{
    Plugin p;
    auto file = std::make_shared<synth::drums::Sample>();
    file->name = "Kit_Kick";
    file->sampleRate = 44100.0;
    file->channels.assign (1, std::vector<float> ((size_t) (2.0 * 44100.0), 0.0f));
    for (int k = 0; k < 4; ++k)   // four bursts of a decaying 100 Hz tone, half a second apart
        for (int i = 0; i < 15000; ++i)
            file->channels[0][(size_t) (k * 22050 + 1000 + i)] = (float) (std::exp (-i / 2000.0) * std::sin (juce::MathConstants<double>::twoPi * 100.0 * i / 44100.0));
    p.processor.setDrumSample (0, file);
    p.set (ParamIDs::drum (0, "Model"), (float) (int) synth::drums::Model::sample);

    DrumPage page (p.processor);
    page.setSize (1476, 672);
    page.select (0);
    auto& display = page.getDisplay();
    CHECK (display.showsFile());
    REQUIRE (display.getHits().size() == 4);

    display.pickHit (2);   // the third
    const auto kit = p.processor.getKitSettings();
    CHECK_THAT (kit.slots[0].start, WithinAbs ((double) display.getHits()[2] / file->length(), 1.0e-4));
    CHECK_THAT (kit.slots[0].end, WithinAbs ((double) (display.getHits()[3] - 44) / file->length(), 1.0e-4));

    display.playWholeFile();
    CHECK_THAT (p.processor.getKitSettings().slots[0].start, WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (p.processor.getKitSettings().slots[0].end, WithinAbs (1.0, 1.0e-6));

    // A drum synth's slot shows its hit, not a file.
    page.select (2);
    CHECK_FALSE (page.getDisplay().showsFile());
}

TEST_CASE ("DRUMS in the plugin: a newly loaded file plays whole (START and END reset)", "[drumkit]")
{
    const auto file = writeSineWav (500.0, 0.3);
    Plugin p;
    p.set (ParamIDs::drum (2, "SampleStart"), 0.4f);
    p.set (ParamIDs::drum (2, "SampleEnd"), 0.6f);
    REQUIRE (p.processor.loadDrumSample (2, file));
    CHECK_THAT (p.processor.getKitSettings().slots[2].start, WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (p.processor.getKitSettings().slots[2].end, WithinAbs (1.0, 1.0e-6));
    file.deleteFile();
}

TEST_CASE ("DRUMS in the plugin: THE SAMPLE EDITOR's settings and envelope play, and are kept by presets, the project and undo", "[drumkit]")
{
    Plugin p;
    p.set (ParamIDs::drum (2, "Gain"), 6.0f);
    p.set (ParamIDs::drum (2, "Mono"), 1.0f);
    p.set (ParamIDs::drum (2, "Delay"), -30.0f);
    p.set (ParamIDs::drum (2, "LowPass"), 2000.0f);
    p.set (ParamIDs::drum (2, "HighPass"), 100.0f);
    const auto kit = p.processor.getKitSettings();
    const auto& s = kit.slots[2];
    CHECK_THAT (s.trim, WithinAbs (juce::Decibels::decibelsToGain (6.0), 1.0e-3));
    CHECK (s.mono);
    CHECK_THAT (s.anchorSeconds(), WithinAbs (0.03, 1.0e-4));
    CHECK_THAT (s.lowPass, WithinAbs (2000.0, 1.0));
    CHECK_THAT (s.highPass, WithinAbs (100.0, 0.1));
    CHECK_FALSE (synth::drums::envelopeShapes (s.envelope));   // new: flat

    // A gate drawn on the snare: its hit ends at half its length (measured by the processor, for the drum synth).
    p.processor.flushUndoStep();
    auto matrix = p.processor.getModMatrix();
    matrix.drumEnvelopes[2] = { { { 0.0f, 1.0f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.26f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
    p.processor.setModMatrix (matrix);
    p.processor.modMatrixEditFinished();
    p.processor.flushUndoStep();
    p.processor.updateDrumReverses();
    const auto seconds = p.processor.getKitSettings().slots[2].envelopeSeconds;
    REQUIRE (seconds > 0.1f);
    p.set (ParamIDs::drum (2, "Delay"), 0.0f);
    p.set (ParamIDs::drum (2, "LowPass"), 20000.0f);   // (the filters' own short tails aside)
    p.set (ParamIDs::drum (2, "HighPass"), 20.0f);
    const auto played = p.play ({ { 38, 0.0 } }, 1.0);
    CHECK (peakOf (played[0], 0.0, 0.25 * seconds - 0.005) > 0.05);
    CHECK (peakOf (played[0], 0.26 * seconds + 0.003) < 1.0e-6);

    // Presets and the project keep the envelope; undo takes it back.
    const auto reread = Rig::fromXml (*p.processor.captureRig().toXml());
    REQUIRE (reread.has_value());
    CHECK (reread->modulation.drumEnvelopes[2] == matrix.drumEnvelopes[2]);
    CHECK (reread->modulation.drumEnvelopes[0] == ModShapeData::flatDrumEnvelope());
    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin other;
    other.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.processor.getModMatrix().drumEnvelopes[2] == matrix.drumEnvelopes[2]);
    p.processor.undo();
    CHECK (p.processor.getModMatrix().drumEnvelopes[2] == ModShapeData::flatDrumEnvelope());
}

TEST_CASE ("DRUMS page: the envelope strip edits the chosen drum's; its menu's shapes", "[drumkit][ui]")
{
    Plugin p;
    DrumPage page (p.processor);
    page.setSize (1476, 672);
    page.select (5);
    auto& editor = page.getEnvelopeEditor();
    CHECK (editor.getKind() == MultiPointEditor::Kind::drumEnvelope);
    CHECK (editor.getIndex() == 5);
    editor.applyDrumShape (5);   // fade out
    const auto fade = p.processor.getModMatrix().drumEnvelopes[5];
    REQUIRE (fade.points.size() == 2);
    CHECK (fade.points[0].y == 1.0f);
    CHECK (fade.points[1].y == 0.0f);
    editor.applyDrumShape (2);   // back to front: a fade in, curved the other way
    const auto mirrored = p.processor.getModMatrix().drumEnvelopes[5];
    CHECK (mirrored.points[0].y == 0.0f);
    CHECK (mirrored.points[1].y == 1.0f);
    CHECK (mirrored.points[0].curve == -fade.points[0].curve);
    const auto a = fade.toShape(), b = mirrored.toShape();
    for (const auto x : { 0.1, 0.3, 0.5, 0.8 })
        CHECK_THAT (b.valueAt (x), WithinAbs (a.valueAt (1.0 - x), 0.02));
    editor.applyDrumShape (3);   // upside down
    CHECK (p.processor.getModMatrix().drumEnvelopes[5].points[0].y == 1.0f);
    editor.applyDrumShape (1);
    CHECK (p.processor.getModMatrix().drumEnvelopes[5] == ModShapeData::flatDrumEnvelope());
    CHECK (p.processor.getModMatrix().drumEnvelopes[4] == ModShapeData::flatDrumEnvelope());   // others untouched

    // Its plot lines up with the hit's picture.
    const auto plot = page.getLocalArea (&editor, editor.getPlotArea());
    const auto picture = page.getDisplay().getBounds().toFloat().reduced (20.0f, 0.0f);
    CHECK_THAT (plot.getX(), WithinAbs (picture.getX(), 0.5));
    CHECK_THAT (plot.getRight(), WithinAbs (picture.getRight(), 0.5));
}
