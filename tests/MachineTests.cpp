// THE DRUM MACHINE (0.38.0): PLAY's own clock (hits on their samples at TEMPO, the DAW leading while it plays), REC
// into the pattern, and the face (the view over the window, its keys, steps, columns, TAP, COPY).
#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/Finish.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int BD = 0, SD = 2;

    struct Host final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;
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
        ParagonX1AudioProcessor p;
        Host host;
        Plugin()
        {
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            // The kick and snare play a short steady burst (20 ms, full from its first sample): each hit shows where it starts.
            const auto burst = testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate);
            testkit::useSample (p, BD, burst);
            testkit::useSample (p, SD, burst);
            p.setPlayHead (&host);
        }
        ~Plugin() { p.setPlayHead (nullptr); }

        void pattern (std::vector<std::pair<int, double>> notes, int index = 0)
        {
            auto m = p.getModMatrix();
            m.drumPatterns[(size_t) index].notes.clear();
            for (const auto& [row, start] : notes)
            {
                synth::drumseq::Note n;
                n.row = row;
                n.start = start;
                m.drumPatterns[(size_t) index].notes.push_back (n);
            }
            p.setModMatrix (m);
        }

        std::vector<float> play (double seconds, const std::function<void (int done, juce::MidiBuffer&)>& midiAt = {})
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (midiAt)
                    midiAt (done, midi);
                p.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
                if (host.playing)
                    host.ppq += blockSize / sampleRate * host.bpm / 60.0;
            }
            return out;
        }
    };

    // Where each burst starts (a sample after at least 5 ms of silence).
    std::vector<int> onsets (const std::vector<float>& x)
    {
        std::vector<int> result;
        int quiet = 1 << 20;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            if (std::abs (x[(size_t) i]) > 1.0e-4f)
            {
                if (quiet >= 240)
                    result.push_back (i);
                quiet = 0;
            }
            else
                ++quiet;
        }
        return result;
    }

    float value (ParagonX1AudioProcessor& p, const juce::String& id) { return p.getParameters().getRawParameterValue (id)->load(); }

    // Every visible control on the face sits inside it and has some size.
    void checkInside (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible())
                continue;
            INFO (child->getName() << " " << typeid (*child).name() << " at " << child->getBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().contains (child->getBounds()));
        }
    }
}

TEST_CASE ("THE DRUM MACHINE: PLAY runs the beat on its own clock at TEMPO, from its first step, each hit on its sample", "[machine]")
{
    Plugin t;
    t.pattern ({ { BD, 0.0 }, { SD, 4.0 }, { BD, 8.0 }, { SD, 12.0 } });
    testkit::set (t.p, ParamIDs::machineTempo, 150.0f);
    CHECK_FALSE (t.p.getParameters().getRawParameterValue (ParamIDs::dseqOn)->load() > 0.5f);

    // Stopped (no DAW, no PLAY): nothing.
    CHECK (onsets (t.play (0.5)).empty());

    t.p.setMachinePlaying (true);
    CHECK (value (t.p, ParamIDs::dseqOn) > 0.5f);   // PLAY switches the sequencer on, following the song
    CHECK (juce::roundToInt (value (t.p, ParamIDs::dseqTrigger)) == (int) synth::drumseq::Trigger::song);
    const auto out = t.play (2.0);
    // 150 BPM: a beat is 19200 samples, a 1/16 step 4800; the pattern (4 beats) loops after 76800.
    const auto hits = onsets (out);
    REQUIRE (hits.size() >= 6);
    const std::vector<int> expected { 0, 19200, 38400, 57600, 76800, 96000 };
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK (std::abs (hits[i] - expected[i]) <= 1);
    CHECK (t.p.getDrumKit().getSequencerPosition() >= 0.0);

    // STOP: no more hits; the beat's position gone. PLAY again starts from the first step.
    t.p.setMachinePlaying (false);
    t.play (0.1);
    CHECK (onsets (t.play (1.0)).empty());
    CHECK (t.p.getDrumKit().getSequencerPosition() < 0.0);
    t.p.setMachinePlaying (true);
    const auto again = onsets (t.play (0.5));
    REQUIRE_FALSE (again.empty());
    CHECK (again.front() == 0);
}

TEST_CASE ("THE DRUM MACHINE: while the DAW plays, the beat follows the DAW (its tempo and place), PLAY or not", "[machine]")
{
    Plugin t;
    t.pattern ({ { BD, 0.0 }, { SD, 4.0 } });
    testkit::set (t.p, ParamIDs::machineTempo, 90.0f);
    t.p.setMachinePlaying (true);
    t.host.playing = true;
    t.host.bpm = 120.0;
    t.host.ppq = 1.0;   // a beat in: the snare (step 4) is due at once
    const auto hits = onsets (t.play (2.0));
    REQUIRE (hits.size() >= 2);
    CHECK (hits[0] <= 1);
    CHECK (std::abs (hits[1] - 72000) <= 1);   // the kick 3 beats later at 120 BPM (24000 a beat), not at 90
    CHECK (t.p.isDawPlaying());
}

TEST_CASE ("THE DRUM MACHINE: REC writes the drums played into the pattern on their nearest steps; one undo step", "[machine]")
{
    Plugin t;
    t.pattern ({ { BD, 0.0 } });
    t.p.modMatrixEditFinished();
    t.p.flushUndoStep();   // the pattern before the take: what undo goes back to
    testkit::set (t.p, ParamIDs::machineTempo, 120.0f);   // a 1/16 step: 6000 samples
    t.p.setMachinePlaying (true);
    t.p.setMachineRecording (true);
    // The snare a little early for step 6 (35900 is 100 samples before it), soft; a second, late, on step 6's neighbour 7.
    t.play (1.0, [] (int done, juce::MidiBuffer& midi)
            {
                for (const auto [at, velocity] : { std::pair { 35900, 0.5f }, std::pair { 42400, 0.9f } })
                    if (at >= done && at < done + blockSize)
                        midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (SD), velocity), at - done);
            });
    t.p.mergeMachineNotes();
    const auto& notes = t.p.getModMatrix().drumPatterns[0].notes;
    std::vector<std::pair<int, double>> found;
    for (const auto& n : notes)
        found.emplace_back (n.row, n.start);
    CHECK (std::count (found.begin(), found.end(), std::pair { SD, 6.0 }) == 1);
    CHECK (std::count (found.begin(), found.end(), std::pair { SD, 7.0 }) == 1);
    CHECK (std::count (found.begin(), found.end(), std::pair { BD, 0.0 }) == 1);
    for (const auto& n : notes)
        if (n.row == SD && n.start == 6.0)
            CHECK (std::abs (n.velocity - 0.5f) < 0.01f);

    // A drum already on that step keeps the louder hit (no second note).
    t.p.setMachinePlaying (true);   // from the first step again
    t.play (0.2, [] (int done, juce::MidiBuffer& midi)
            {
                if (done == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (BD), 0.3f), 10);
            });
    t.p.mergeMachineNotes();
    CHECK (std::count_if (notes.begin(), notes.end(), [] (const auto& n) { return n.row == BD; }) == 1);

    // Disarmed: the take is one undo step.
    t.p.setMachineRecording (false);
    t.p.flushUndoStep();
    REQUIRE (t.p.canUndo());
    t.p.undo();
    CHECK (t.p.getModMatrix().drumPatterns[0].notes.size() == 1);

    // Not recording: nothing written.
    t.play (0.5, [] (int done, juce::MidiBuffer& midi)
            {
                if (done == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (SD), 1.0f), 5);
            });
    CHECK (t.p.mergeMachineNotes() == 0);
}

TEST_CASE ("THE DRUM MACHINE's face: over the window, its steps, columns, keys, TAP and COPY", "[machine][ui]")
{
    ParagonX1AudioProcessor p;
    p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& drums = view->getDrumPage();
    auto& machine = view->getMachine();
    CHECK_FALSE (view->isShowingMachine());

    // The MACHINE view covers the page, the keyboard and OUTPUT (hidden under it); KIT goes back.
    drums.select (SD);
    drums.showView (DrumPage::View::machine);
    CHECK (view->isShowingMachine());
    CHECK_FALSE (drums.isVisible());
    CHECK_FALSE (view->getSongTrack().isVisible());
    CHECK (machine.getBounds().getY() >= 64);
    checkInside (machine);
    view->showPage (MainView::Page::grooves);
    CHECK_FALSE (view->isShowingMachine());
    view->showPage (MainView::Page::drums);
    CHECK (view->isShowingMachine());   // back on DRUMS: the machine again
    machine.getKey ("KIT").onClick();
    CHECK_FALSE (view->isShowingMachine());
    CHECK (drums.isVisible());
    CHECK (view->getSongTrack().isVisible());
    CHECK (drums.getView() == DrumPage::View::kit);
    drums.showView (DrumPage::View::machine);

    // Its finish is its own: pearl black while it draws, the rest of the plugin's left as it was.
    const auto before = Finish::get();
    editor->createComponentSnapshot (editor->getLocalBounds());
    CHECK (Finish::get() == before);

    // A step key: the chosen drum (the snare) on that step of the pattern shown; again: off. One undo step each.
    machine.update();
    CHECK_FALSE (machine.isStepOn (4));
    machine.getStepKey (4).onClick();
    {
        const auto& notes = p.getModMatrix().drumPatterns[0].notes;
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].row == SD);
        CHECK (notes[0].start == 4.0);
    }
    CHECK (machine.isStepOn (4));
    CHECK (machine.getStepKey (4).getToggleState());
    machine.getStepKey (4).onClick();
    CHECK (p.getModMatrix().drumPatterns[0].notes.empty());

    // FILL: the snare on all 16; CLEAR: its notes gone (the kick's stay).
    machine.getStepKey (0).onClick();
    drums.select (BD);
    machine.getStepKey (0).onClick();   // the kick on step 1
    drums.select (SD);
    machine.fillChosen();
    CHECK (p.getModMatrix().drumPatterns[0].notes.size() == 17);
    machine.clearChosen (false);
    CHECK (p.getModMatrix().drumPatterns[0].notes.size() == 1);

    // COPY, then B: pattern A copied to B, B chosen.
    machine.getKey ("COPY").onClick();
    CHECK (machine.isCopyArmed());
    machine.getKey ("B").onClick();
    CHECK_FALSE (machine.isCopyArmed());
    CHECK (p.getModMatrix().drumPatterns[1] == p.getModMatrix().drumPatterns[0]);
    CHECK (juce::roundToInt (value (p, ParamIDs::dseqPattern)) == 1);

    // A longer pattern: pages of 16 steps.
    testkit::set (p, ParamIDs::dseqLength, 8.0f);
    machine.update();
    CHECK (machine.numStepPages() == 2);
    machine.getKey (">").onClick();
    CHECK (machine.getStepPage() == 1);
    machine.getStepKey (0).onClick();
    bool onSeventeen = false;
    for (const auto& n : p.getModMatrix().drumPatterns[1].notes)
        onSeventeen = onSeventeen || (n.row == SD && n.start == 16.0);
    CHECK (onSeventeen);

    // The columns: bank 1 is drums 1-18, bank 2 19-36; TUNE is the drum's PITCH, its fader its LEVEL.
    machine.getTune (2).setValue (5.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, ParamIDs::drum (2, "Pitch")) - (5.0f)) < 1.0e-4f);
    machine.getKey ("19-36").onClick();
    CHECK (machine.slotOfColumn (0) == 18);
    machine.getTune (0).setValue (-3.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, ParamIDs::drum (18, "Pitch")) - (-3.0f)) < 1.0e-4f);
    CHECK (std::abs (value (p, ParamIDs::drum (0, "Pitch")) - (0.0f)) < 1.0e-4f);
    CHECK (MachineView::nameOf (35) == "TOM 5");

    // A pad: chooses its drum (the page's chosen one) and plays it.
    machine.getPad (5).mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                    &machine.getPad (5), &machine.getPad (5), juce::Time(), {}, juce::Time(), 1, false));
    CHECK (drums.getSelected() == 23);

    // TAP: four taps 0.6 s apart: 100 BPM.
    for (int i = 0; i < 4; ++i)
        machine.tap (10000.0 + i * 600.0);
    CHECK (std::abs (value (p, ParamIDs::machineTempo) - 100.0f) < 0.1f);
    machine.tap (20000.0);   // after a pause: a new count (no tempo from one tap)
    CHECK (std::abs (value (p, ParamIDs::machineTempo) - 100.0f) < 0.1f);

    // MUTE the chosen drum; the screen.
    machine.getKey ("MUTE").onClick();
    CHECK (value (p, ParamIDs::drum (23, "Mute")) > 0.5f);
    machine.update();
    CHECK (machine.getKey ("MUTE").getToggleState());
    CHECK (machine.getScreenText().startsWith ("PATTERN B|4/4|STOPPED|"));
    CHECK (machine.getScreenText().contains ("|100.0|BPM|"));
}

TEST_CASE ("THE DRUM MACHINE 0.39.0's face: a pad given another drum, its knobs and fader that drum's", "[machine][ui]")
{
    ParagonX1AudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->getDrumPage().showView (DrumPage::View::machine);
    auto& machine = view->getMachine();

    machine.assignPad (1, 30);   // CRASH 4 where the sidestick was
    CHECK (machine.slotOfColumn (1) == 30);
    machine.getTune (1).setValue (4.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, ParamIDs::drum (30, "Pitch")) - (4.0f)) < 1.0e-4f);
    CHECK (std::abs (value (p, ParamIDs::drum (1, "Pitch")) - (0.0f)) < 1.0e-4f);
    // The sidestick went to CRASH 4's pad (bank 19-36's 13th column).
    machine.setBank (1);
    CHECK (machine.slotOfColumn (12) == 1);
    machine.setBank (0);

    // A project loaded with its own order: the face follows it.
    p.setMachinePad (1, 1);
    machine.update();
    CHECK (machine.slotOfColumn (1) == 1);
    machine.getTune (1).setValue (-2.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, ParamIDs::drum (1, "Pitch")) - (-2.0f)) < 1.0e-4f);
}

//==============================================================================
// 0.40.0: THE PROGRESS BOX (PR7-S's), while a long job runs.
TEST_CASE ("THE PROGRESS BOX: a kit loading for over half a second, then gone; a bounce with CANCEL", "[progress][ui]")
{
    ParagonX1AudioProcessor p;
    p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& box = view->getProgressBox();
    CHECK_FALSE (box.isVisible());

    // Nothing running: no box.
    view->checkLongJobs (0.0);
    view->checkLongJobs (1000.0);
    CHECK_FALSE (box.isVisible());

    // The bar: lit up to the progress, orange to pink-red (the family's colours only).
    juce::Image bar (juce::Image::ARGB, 200, 20, true);
    {
        juce::Graphics g (bar);
        ProgressBox::paintBar (g, { 0.0f, 0.0f, 200.0f, 20.0f }, 0.5);
    }
    CHECK (bar.getPixelAt (30, 10).getBrightness() > 0.6f);    // lit
    CHECK (bar.getPixelAt (170, 10).getBrightness() < 0.3f);   // not yet
    CHECK (bar.getPixelAt (30, 10).getRed() > 200);

    // A bounce: the box at once, its CANCEL stops it; the box goes when the bounce has.
    song::Song s;
    song::Block b;
    b.length = b.loopLength = 64.0;
    for (int beat = 0; beat < 64; ++beat)
        b.hits.push_back ({ (double) beat, 0, 1.0f });
    s.blocks.push_back (b);
    p.setSong (s);
    auto& track = view->getSongTrack();
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pr7d-progress-test");
    folder.deleteRecursively();
    folder.createDirectory();
    ParagonX1AudioProcessor::BounceOptions options;
    options.tailSeconds = 1.0;
    track.startBounce (options, folder);
    view->checkLongJobs (5000.0);
    CHECK (box.isVisible());
    CHECK (box.getWhat() == "BOUNCING THE SONG TO AUDIO");
    CHECK (box.getCancelKey().isVisible());
    box.getCancelKey().onClick();
    for (int i = 0; i < 500 && track.getBounceProgress() >= 0.0; ++i)
        juce::Thread::sleep (10);
    box.update();
    CHECK_FALSE (box.isVisible());
    folder.deleteRecursively();
}

TEST_CASE ("THE DRUM MACHINE 0.43.0: FINISH gives the face its own finish (pearl white, black, white + neon), the plugin's untouched", "[machine][ui]")
{
    ParagonX1AudioProcessor p;
    p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->getDrumPage().showView (DrumPage::View::machine);
    auto& machine = view->getMachine();
    CHECK (machine.getKey ("FINISH...").isVisible());
    const auto plugin = Finish::get();
    const auto was = Finish::getMachine();

    // The face's brightness in each finish (a patch of chassis by the badge, away from keys and screen).
    const auto chassisBrightness = [&machine]
    {
        const auto image = machine.createComponentSnapshot (machine.getLocalBounds(), true, 1.0f);
        float total = 0.0f;
        int count = 0;
        for (int y = 200; y < 230; ++y)
            for (int x = 440; x < 470; ++x)
            {
                total += image.getPixelAt (x, y).getBrightness();
                ++count;
            }
        return total / (float) count;
    };
    machine.setFinish (Finish::Type::pearlBlack, false);
    const auto black = chassisBrightness();
    machine.setFinish (Finish::Type::pearlWhite, false);
    const auto white = chassisBrightness();
    machine.setFinish (Finish::Type::pearlWhiteNeon, false);
    const auto neon = chassisBrightness();
    INFO ("black " << black << ", white " << white << ", neon " << neon);
    CHECK (black < 0.3f);
    CHECK (white > 0.6f);
    CHECK (neon > 0.6f);
    CHECK (Finish::getMachine() == Finish::Type::pearlWhiteNeon);
    CHECK (Finish::get() == plugin);   // the rest of the plugin as it was
    machine.setFinish (was, false);
}

TEST_CASE ("FINISH 0.53.0: black keys and knobs on every finish; the neon finishes' colour is in the rings round them", "[machine][ui]")
{
    // A pad (bezel + key) and a knob drawn in each finish: the key's face and the knob's top black in all three; the
    // bezel's rim the neon (saturated) on pearl black and pearl white + neon, chrome (grey) on pearl white.
    struct Look { float keyBrightness, rimSaturation, knobTopBrightness, ringSaturation; };
    const auto draw = [] (Finish::Type type)
    {
        Finish::paintAs (type);
        juce::Image image (juce::Image::ARGB, 300, 140, true);
        {
            juce::Graphics g (image);
            g.fillAll (juce::Colour (0xff202020));
            const auto well = ParagonLookAndFeel::drawChromeBezel (g, { 10.0f, 10.0f, 120.0f, 80.0f }, 9.0f);
            ParagonLookAndFeel::drawPianoKey (g, well.reduced (1.5f), 7.0f, 4.5f, false, false);
            ParagonLookAndFeel::drawPedalKnob (g, { 160.0f, 0.0f, 120.0f, 120.0f }, 0.0f);
        }
        Finish::paintAs (std::nullopt);
        Look look {};
        look.keyBrightness = image.getPixelAt (70, 45).getBrightness();       // the key's face
        look.rimSaturation = image.getPixelAt (70, 12).getSaturation();       // the bezel's top rim
        look.knobTopBrightness = image.getPixelAt (220, 44).getBrightness();  // the knob's top, off its pointer
        // The ring's most coloured bright pixel (its sides and front: the knob hides its back).
        float best = 0.0f;
        for (int x = 178; x < 262; ++x)
            for (int y = 45; y < 100; ++y)
                if (const auto c = image.getPixelAt (x, y); c.getBrightness() > 0.5f)
                    best = juce::jmax (best, c.getSaturation());
        look.ringSaturation = best;
        return look;
    };
    for (const auto type : { Finish::Type::pearlWhite, Finish::Type::pearlBlack, Finish::Type::pearlWhiteNeon })
    {
        Finish::paintAs (type);
        const auto rings = Finish::neonRings();
        Finish::paintAs (std::nullopt);
        CHECK (rings == (type != Finish::Type::pearlWhite));
        const auto look = draw (type);
        INFO ("finish " << (int) type << ": key " << look.keyBrightness << ", rim saturation " << look.rimSaturation << ", knob top "
                        << look.knobTopBrightness << ", ring saturation " << look.ringSaturation);
        CHECK (look.keyBrightness < 0.25f);          // black keys everywhere
        CHECK (look.knobTopBrightness < 0.6f);       // the knob's top dark metal everywhere
        if (rings)
            CHECK (look.rimSaturation > 0.3f);       // the neon rim
        else
            CHECK (look.rimSaturation < 0.1f);       // chrome
        CHECK (look.ringSaturation > 0.3f);          // the knob's ring a coloured light on every finish
    }
}
