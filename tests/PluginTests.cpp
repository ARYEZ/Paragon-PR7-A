// THE PLUGIN (PR7-I 0.2.0): an instrument playing the guitar folder's takes; importing through it; projects, presets,
// undo, MIDI learn; the window (its tabs, the GUITAR page, the fretboard).
#include "TestUtils.h"

#include "NoteNames.h"
#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

using namespace guitar;

namespace
{
    juce::File temp (const juce::String& name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PR7-I plugin tests").getChildFile (name);
        f.deleteRecursively();
        f.createDirectory();
        return f;
    }

    // A processor with its presets (and so its "guitar chosen last") in a folder of its own.
    struct Plugin
    {
        ParagonX1AudioProcessor p;
        juce::File folder = temp ("Plugin");
        Plugin()
        {
            p.useRigFolder (folder.getChildFile ("Rigs"));
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.setNonRealtime (true);   // as bouncing: streamed takes wait for the disk (blocks here run far faster than real time)
            p.getParameters().getParameter (ParamIDs::variation)->setValueNotifyingHost (0.0f);   // the takes exactly (levels measured)
            p.prepareToPlay (sampleRate, blockSize);
        }
        // Plays `blocks` blocks, `events` (note, velocity: 0 off) at the first block's start; MAIN's left channel back.
        std::vector<float> play (int blocks, std::vector<std::pair<int, int>> events = {})
        {
            std::vector<float> out;
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;
                if (b == 0)
                    for (const auto& [note, velocity] : events)
                        midi.addEvent (velocity > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity)
                                                    : juce::MidiMessage::noteOff (1, note), 0);
                p.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };

    // A made-up render of a string's list (tones at each take's pitch, a little late), as a WAV file at 8 kHz.
    juce::File fakeRenderFile (int string, const juce::File& file, Articulation a = Articulation::sustain)
    {
        constexpr double rate = 8000.0;
        std::vector<float> audio ((size_t) (renders::lengthSeconds (a, string) * rate), 0.0f);
        for (const auto& take : renders::takes (a, string))
        {
            const auto hz = guitar::frequencyOf (take.note());
            const auto start = (int) ((take.noteSeconds + 0.015) * rate);
            const auto overtone = hz * (2.0 + take.roundRobin);   // each round-robin its own recording
            for (int i = 0; i < (int) (renders::spec (a).holdSeconds * rate); ++i)
                audio[(size_t) (start + i)] += (0.2f + 0.3f * (float) take.layer) * std::exp (-0.6f * (float) i / (float) rate)
                                             * (float) (std::sin (juce::MathConstants<double>::twoPi * hz * i / rate)
                                                        + (overtone < 3600.0 ? 0.3 * std::sin (juce::MathConstants<double>::twoPi * overtone * i / rate) : 0.0));
        }
        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
        const float* channels[] { audio.data() };
        writer->writeFromFloatArrays (channels, 1, (int) audio.size());
        return file;
    }

    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;
            INFO (path << " / " << child->getName() << " " << child->getBounds().toString());
            CHECK (parent.getLocalBounds().contains (child->getBounds()));
            CHECK (child->getWidth() > 0);
            if (dynamic_cast<juce::ListBox*> (child) == nullptr)   // (a list box keeps a row for every row that fits, used or not)
                checkLayout (*child, path + " / " + child->getName());
        }
    }
}

TEST_CASE ("PR7-I is an instrument: no audio in, MIDI in, one output (mono or stereo)", "[plugin]")
{
    ParagonX1AudioProcessor p;
    CHECK (p.acceptsMidi());
    CHECK_FALSE (p.producesMidi());
    CHECK (p.getBusCount (true) == 0);
    CHECK (p.getBusCount (false) == 1);
    juce::AudioProcessor::BusesLayout stereo, mono, withInput;
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    withInput.inputBuses.add (juce::AudioChannelSet::stereo());
    withInput.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (stereo));
    CHECK (p.checkBusesLayoutSupported (mono));
    CHECK_FALSE (p.checkBusesLayoutSupported (withInput));
    CHECK (juce::String (JucePlugin_Name) == "Paragon PR7-I");
}

TEST_CASE ("THE PLUGIN: a render imported, the guitar folder loaded, notes played on their strings; OUTPUT, TONE, BYPASS", "[plugin]")
{
    Plugin t;
    const auto guitarFolder = t.folder.getChildFile ("My Guitar");
    t.p.setGuitarFolder (guitarFolder);
    REQUIRE (t.p.waitUntilIdle (10000));
    CHECK (t.p.getLibrary()->isEmpty());
    CHECK (t.play (40, { { 45, 100 } }) == std::vector<float> ((size_t) (40 * blockSize), 0.0f));   // nothing to play yet

    // IMPORT A RENDER through the plugin (in the background): the A string's takes in the folder, the guitar reloaded.
    const auto render = fakeRenderFile (2, t.folder.getChildFile ("A string.wav"));
    t.p.startImport (render, Articulation::sustain, 2);
    REQUIRE (t.p.waitUntilIdle (60000));
    CHECK (t.p.getImportReport (Articulation::sustain, 2) == "300 notes imported, all in tune");
    CHECK (t.p.getLibrary()->count (Articulation::sustain, 2) == 300);
    CHECK (guitarFolder.getChildFile ("Sustain/s2_f00_l1_d_r0.wav").existsAsFile());
    CHECK (t.p.getImportReport (Articulation::sustain, 3).isEmpty());

    // A2 on the A string at its pitch, at OUTPUT's -6 dB; the guitar says where it's played.
    t.play (1, { { 45, 120 } });
    const auto out = t.play (100);
    const auto atPitch = levelAt (out, 110.0, 4000, 19200);
    CHECK (atPitch > 0.1);                                    // A2's pitch...
    CHECK (levelAt (out, 116.5, 4000, 19200) < 0.2 * atPitch);   // ... not a semitone off (that would be as loud)
    const auto level = rms (out, 4000, 24000);
    CHECK (level > 0.1);
    CHECK (t.p.getGuitar().getSounding (2).fret == 0);
    CHECK (t.p.getActiveVoices() == 1);

    // OUTPUT +6 dB: twice as loud. TONE at 1 kHz: a high note quieter, the A2 hardly touched.
    t.p.setParameterNow (ParamIDs::outputGain, 0.0f);
    t.play (10);
    CHECK_THAT (rms (t.play (40), 0, 8000) / rms (out, 16000, 24000), WithinAbs (2.0 * std::exp (-0.6 * (50 * 256) / sampleRate), 0.15));
    t.p.setParameterNow (ParamIDs::tone, 1000.0f);
    t.play (10);
    CHECK (rms (t.play (40), 0, 8000) > 0.5 * rms (out, 16000, 24000));

    // BYPASS: silence.
    t.p.setParameterNow (ParamIDs::bypass, 1.0f);
    t.play (20);
    CHECK (rms (t.play (10), 0, 2560) < 1.0e-6);

    // An import of the wrong file: refused, said on the row, nothing changed.
    t.p.startImport (render, Articulation::sustain, 4);
    REQUIRE (t.p.waitUntilIdle (60000));
    CHECK (t.p.getImportReport (Articulation::sustain, 4).contains ("aren't the pitch"));
    CHECK (t.p.getLibrary()->count (Articulation::sustain, 4) == 0);
}

TEST_CASE ("THE PLUGIN: the project keeps the guitar's folder, settings, preset and MIDI links; a new one starts on the folder chosen last", "[plugin]")
{
    juce::MemoryBlock state;
    const auto folder = temp ("Project");
    {
        Plugin t;
        t.p.setGuitarFolder (folder.getChildFile ("Guitar A"));
        t.p.setParameterNow (ParamIDs::fretMode, 1.0f);
        t.p.setParameterNow (ParamIDs::handPosition, 7.0f);
        MidiLinks links;
        links.link (20, ParamIDs::tone);
        t.p.setMidiLinks (links);
        t.p.getStateInformation (state);
        // A new instance on this computer: the folder chosen last.
        ParagonX1AudioProcessor again;
        again.useRigFolder (t.folder.getChildFile ("Rigs"));
        REQUIRE (again.waitUntilIdle (5000));
    }
    Plugin t;
    t.p.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (t.p.waitUntilIdle (5000));
    CHECK (t.p.getGuitarFolder() == folder.getChildFile ("Guitar A"));
    CHECK (juce::roundToInt (t.p.getParameters().getRawParameterValue (ParamIDs::fretMode)->load()) == 1);
    CHECK (t.p.getGuitarSettings().hand == 7);
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::polyphonic);
    CHECK (t.p.getMidiLinks().controllerFor (ParamIDs::tone) == 20);
    CHECK (t.p.getGuitarSettings().forceString == -1);   // FORCE STRING's "Off"
    t.p.setParameterNow (ParamIDs::forceString, 1.0f);
    CHECK (t.p.getGuitarSettings().forceString == 0);    // "String 7 (low A)"
}

TEST_CASE ("PRESETS: their own folder; ways of playing; saved, loaded, stepped; undo", "[plugin][rigs]")
{
    Plugin t;
    CHECK (RigLibrary().getFolder().getParentDirectory().getFileName() == "Paragon PR7-I");
    auto& library = t.p.getRigLibrary();
    juce::StringArray names;
    for (const auto& e : library.getEntries())
        if (e.isFactory)
            names.add (e.rig.name);
    CHECK (names == juce::StringArray { "Init", "Tight Rhythm", "Chords", "Lead Up The Neck", "Down Picked" });

    const auto* chords = library.findFactory ("Chords");
    REQUIRE (chords != nullptr);
    t.p.loadRig (*chords);
    t.p.flushUndoStep();
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::polyphonic);
    CHECK (t.p.getCurrentRig().name == "Chords");
    CHECK_FALSE (t.p.isRigModified());

    // An edit marks it changed; undo takes it back (and the preset's name stays).
    t.p.setParameterNow (ParamIDs::release, 0.3f);
    t.p.flushUndoStep();
    CHECK (t.p.isRigModified());
    t.p.undo();
    CHECK_THAT (t.p.getGuitarSettings().releaseSeconds, WithinAbs (0.12, 0.002));
    t.p.redo();
    CHECK_THAT (t.p.getGuitarSettings().releaseSeconds, WithinAbs (0.3, 0.002));

    // SAVE AS: a user preset with the guitar's settings; loading Init and back brings them.
    REQUIRE (t.p.saveCurrentRigAs ("My Chords"));
    CHECK (t.p.getCurrentRig().isUserRig());
    t.p.loadRig (*library.findFactory ("Init"));
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::natural);
    const auto* mine = library.find (t.p.getCurrentRig().file);
    CHECK (mine == nullptr);
    for (const auto& e : library.getEntries())
        if (e.rig.name == "My Chords")
            t.p.loadRig (e);
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::polyphonic);
    CHECK_THAT (t.p.getGuitarSettings().releaseSeconds, WithinAbs (0.3, 0.002));
    // OUTPUT isn't part of a preset.
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::outputGain));
}

TEST_CASE ("MIDI LEARN: a knob learns the controller moved next", "[plugin]")
{
    Plugin t;
    t.p.startMidiLearn (ParamIDs::velocityToVolume);
    t.p.controllerForTest (21, 0.25f);
    t.p.finishMidiLearn();
    CHECK (t.p.getMidiLinks().controllerFor (ParamIDs::velocityToVolume) == 21);
    t.p.controllerForTest (21, 1.0f);
    CHECK_THAT (t.p.getGuitarSettings().velocityToVolume, WithinAbs (1.0, 0.01));
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("THE WINDOW: GUITAR, RENDERS, PRESETS and SETTINGS tabs; the GUITAR page lights the notes played; the fretboard strip", "[ui]")
{
    Plugin t;
    // A guitar of short tones in memory, every string and fret.
    auto library = std::make_shared<Library>();
    for (int s = 0; s < numStrings; ++s)
        for (int f = 0; f <= numFrets; ++f)
        {
            auto take = std::make_shared<Take>();
            take->sampleRate = 8000.0;
            for (int i = 0; i < 16000; ++i)
                take->head.push_back (0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * guitar::frequencyOf (noteOf (s, f)) * i / 8000.0));
            take->frames = (juce::int64) take->head.size();
            library->add (Articulation::sustain, s, f, 1, Stroke::down, 0, take);
        }
    t.p.setLibraryForTest (library);
    REQUIRE (t.p.waitUntilIdle (5000));

    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    CHECK (view->getPage() == MainView::Page::guitar);
    CHECK (view->getGuitarView().isVisible());
    CHECK_FALSE (view->getRendersPage().isVisible());
    CHECK_FALSE (view->getFretboard().isVisible());   // (the GUITAR page has its own neck)
    CHECK (view->findChildWithID ("drumsTab") == nullptr);
    std::vector<juce::MidiKeyboardComponent*> keyboards;
    findAll (*view, keyboards);
    CHECK (keyboards.empty());   // no on-screen keyboard (PR7-D 0.41.0, the owner's ask)
    checkLayout (*view, "guitar");

    std::vector<juce::Button*> tabs;
    for (const auto* id : { "guitarTab", "rendersTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    view->showPage (MainView::Page::renders);
    CHECK (view->getRendersPage().isVisible());
    CHECK_FALSE (view->getGuitarView().isVisible());
    CHECK (view->getFretboard().isVisible());
    CHECK (tabs[1]->getToggleState());
    checkLayout (*view, "renders");
    view->showPage (MainView::Page::presets);
    CHECK_FALSE (view->getRendersPage().isVisible());
    CHECK (tabs[2]->getToggleState());
    checkLayout (*view, "presets");
    view->showPage (MainView::Page::settings);
    CHECK (view->getSettingsPage().isVisible());
    checkLayout (*view, "settings");
    view->showPage (MainView::Page::guitar);
    CHECK (view->getGuitarView().isVisible());

    // The rows of RENDERS: each string's, what's there for it.
    CHECK (view->getRendersPage().getRow (0).getStatus() == "25 of 300 takes");
    view->getRendersPage().refresh();
    CHECK (view->getRendersPage().getGuitarStatus().contains ("175 takes"));

    // A power chord on the low strings: the fretboard lights the strings and frets it's played on.
    t.play (2, { { 33, 100 }, { 40, 100 }, { 45, 100 } });
    view->getFretboard().refresh();
    view->getGuitarView().refresh();
    CHECK (view->getFretboard().shownFret (0) == 0);
    CHECK (view->getFretboard().shownFret (1) == 0);
    CHECK (view->getFretboard().shownFret (2) == 0);
    CHECK (view->getFretboard().shownFret (3) == -1);
    for (int s = 0; s < 4; ++s)
        CHECK (view->getGuitarView().shownFret (s) == view->getFretboard().shownFret (s));
    t.play (2, { { 33, 0 }, { 40, 0 }, { 45, 0 }, { 38, 100 } });
    t.play (2);
    view->getFretboard().refresh();
    view->getGuitarView().refresh();
    CHECK (view->getFretboard().shownFret (0) == 5);
    CHECK (view->getFretboard().shownFret (2) == -1);
    CHECK (view->getGuitarView().shownFret (0) == 5);
    CHECK (view->getGuitarView().getNowPlayingText().startsWith (notenames::name (38)));
    // THE GUITAR page's neck: the render's frets as on a real neck (the 12th two-thirds of the way to the 24th), the nut
    // on the right, the low A's string on top, the notes on their strings.
    const auto& neck = view->getGuitarView();
    CHECK_THAT ((neck.fretX (12) - neck.fretX (0)) / (neck.fretX (24) - neck.fretX (0)), WithinAbs (2.0 / 3.0, 0.001));
    CHECK (neck.fretX (0) > neck.fretX (24));
    CHECK (neck.noteCentre (0, 5).y < neck.noteCentre (6, 5).y);
    CHECK (neck.fretAt (neck.noteCentre (0, 7).x) == 7);
    CHECK (neck.fretAt (neck.fretX (0) - 5.0f) == 0);
    // The fret wires as on a real neck: the 12th halfway to the bridge's end of the neck's span (2/3 of the drawn neck).
    const auto& board = view->getFretboard();
    CHECK_THAT ((board.fretX (12) - board.fretX (0)) / (board.fretX (24) - board.fretX (0)), WithinAbs (2.0 / 3.0, 0.001));

    // The colours: pink on the low A to orange on the high E, all in the family's range (318 to 42 degrees).
    for (int s = 0; s < numStrings; ++s)
    {
        const auto hue = Fretboard::stringColour (s).getHue() * 360.0f;
        INFO (s << ": " << hue);
        CHECK ((hue >= 318.0f || hue <= 42.0f));
    }
}

TEST_CASE ("0.2.2: VARIATION off by default, and in projects and presets saved before (0.2.1's 50% was never chosen)", "[plugin]")
{
    Plugin t;
    CHECK (t.p.getGuitarSettings().variation == 0.0f);
    // A 0.2.1 project: VARIATION stored at 50%, no state version.
    t.p.setParameterNow (ParamIDs::variation, 0.5f);
    juce::MemoryBlock state;
    t.p.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("stateVersion");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    Plugin again;
    again.p.setStateInformation (old.getData(), (int) old.getSize());
    CHECK (again.p.getGuitarSettings().variation == 0.0f);
    // A 0.2.2 project keeps what was chosen.
    Plugin third;
    third.p.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (third.p.getGuitarSettings().variation, WithinAbs (0.5, 0.001));
    // A preset saved by 0.2.1 (parameter version 1): VARIATION off.
    Rig rig;
    rig.parameters[ParamIDs::variation] = 0.5f;
    auto rigXml = rig.toXml();
    rigXml->setAttribute ("parameterVersion", 1);
    CHECK (Rig::fromXml (*rigXml)->parameters.at (ParamIDs::variation) == 0.0f);
    CHECK (Rig::fromXml (*rig.toXml())->parameters.at (ParamIDs::variation) == 0.5f);
}

TEST_CASE ("PALM MUTE 0.3.0: a palm-mute render imported into Mute; played palm-muted by the owner's split map; KEY MAP", "[plugin]")
{
    Plugin t;
    CHECK (t.p.getGuitarSettings().keyMap == guitar::KeyMap::split);     // the owner's own map, by default
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::keyMap));            // it belongs to the MIDI parts, not the sound
    const auto guitarFolder = t.folder.getChildFile ("Mutes");
    t.p.setGuitarFolder (guitarFolder);
    REQUIRE (t.p.waitUntilIdle (10000));
    t.p.startImport (fakeRenderFile (0, t.folder.getChildFile ("Low A mutes.wav"), Articulation::mute), Articulation::mute, 0);
    REQUIRE (t.p.waitUntilIdle (120000));
    INFO (t.p.getImportReport (Articulation::mute, 0));
    CHECK (t.p.getImportReport (Articulation::mute, 0).startsWith ("2000 notes imported, all in tune"));
    CHECK (t.p.getLibrary()->count (Articulation::mute, 0) == 2000);
    CHECK (guitarFolder.getChildFile ("Mute/s0_f00_l4_u_r7.wav").existsAsFile());
    CHECK (t.p.getImportReport (Articulation::sustain, 0).isEmpty());

    // C-1 (latched from the start) at velocity 30: the low A palm-muted; 100: sustain (none imported: the mutes stand in,
    // at the nearest... as the library finds them).
    t.play (1, { { 33, 30 } });
    t.play (4);
    CHECK (t.p.getGuitar().getSounding (0).muted);
    CHECK (t.p.getGuitar().getSounding (0).fret == 0);

    // The GUITAR page's RENDERS, on PALM MUTE: the low A's row says what came in.
    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& page = view->getRendersPage();
    page.showArticulation (Articulation::mute);
    page.refresh();
    CHECK (page.getRow (0).getStatus().startsWith ("2000 notes imported"));
    CHECK (page.getRow (1).getStatus() == "Not imported");
    page.showArticulation (Articulation::sustain);
    CHECK (page.getRow (0).getStatus() == "Not imported");
}

TEST_CASE ("THE GUITAR PAGE 0.4.0: its controls; the capo dragged on the neck; PERFORMANCE; the family's colours only", "[ui]")
{
    Plugin t;
    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& page = view->getGuitarView();
    checkLayout (page, "guitar view");
    const auto value = [&t] (const char* id) { return t.p.getParameters().getRawParameterValue (id)->load(); };

    // The value boxes: dragged (stepped) up, MONO clicked on.
    std::vector<GuitarView::ValueBox*> boxes;
    findAll (page, boxes);
    REQUIRE (boxes.size() == 4);
    boxes[0]->step (3);
    CHECK (value (ParamIDs::capo) == 3.0f);
    CHECK (boxes[0]->getValueText() == "3");
    boxes[1]->step (4);
    CHECK (t.p.getGuitarSettings().handSpan == 5);
    boxes[2]->step (-5);
    CHECK_THAT (value (ParamIDs::handReset), WithinAbs (0.5, 0.001));
    boxes[3]->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0, 0, 0, 0, boxes[3], boxes[3],
                                         juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false));
    CHECK (t.p.getGuitarSettings().mono);

    // PERFORMANCE: a style sets the fretting controls together; the page shows the one they match.
    t.p.applyPerformance (ParagonX1AudioProcessor::Performance::chords);
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::polyphonic);
    CHECK_FALSE (t.p.getGuitarSettings().mono);
    CHECK (t.p.getGuitarSettings().handSpan == 0);
    CHECK (t.p.currentPerformance() == ParagonX1AudioProcessor::Performance::chords);
    t.p.applyPerformance (ParagonX1AudioProcessor::Performance::lead);
    CHECK (t.p.currentPerformance() == ParagonX1AudioProcessor::Performance::lead);
    t.p.setParameterNow (ParamIDs::handPosition, 7.0f);
    CHECK (t.p.currentPerformance() == ParagonX1AudioProcessor::Performance::custom);

    // THE CAPO dragged from its bar along the neck, then double-clicked off.
    t.p.setParameterNow (ParamIDs::capo, 0.0f);
    page.refresh();
    const auto mouse = [&page] (juce::Point<float> at) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0,
                                                                                   &page, &page, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false); };
    const auto nut = juce::Point<float> (page.fretX (0), page.noteCentre (3, 0).y);
    page.mouseDown (mouse (nut));
    page.mouseDrag (mouse (page.noteCentre (3, 5)));
    page.mouseUp (mouse (page.noteCentre (3, 5)));
    CHECK (value (ParamIDs::capo) == 5.0f);
    page.refresh();
    page.mouseDoubleClick (mouse ({ page.fretX (4) * 0.35f + page.fretX (5) * 0.65f, page.noteCentre (3, 5).y }));
    CHECK (value (ParamIDs::capo) == 0.0f);

    // The page drawn: every coloured pixel in the family's range (318 to 42 degrees through red), the picture included.
    const auto image = page.createComponentSnapshot (page.getLocalBounds(), true, 1.0f);
    auto coloured = 0, outside = 0;
    for (int y = 0; y < image.getHeight(); y += 3)
        for (int x = 0; x < image.getWidth(); x += 3)
        {
            const auto c = image.getPixelAt (x, y);
            if (c.getSaturation() < 0.25f || c.getBrightness() < 0.15f)
                continue;   // (greys and near black: hue doesn't show)
            ++coloured;
            const auto hue = c.getHue() * 360.0f;
            if (hue > 44.0f && hue < 316.0f)
                ++outside;
        }
    INFO (outside << " of " << coloured);
    CHECK (coloured > 1000);
    // A real colour outside the range (a green, a blue) would be thousands of pixels; Windows' text smoothing and JPEG
    // decoding can leave a stray blended edge pixel or two (1 in 41972 on CI for 0.4.0), so allow 1 in 2000.
    CHECK (outside * 2000 <= coloured);
}

TEST_CASE ("0.4.0: BITE (less low-mid, more top); POLY WINDOW reported as latency; old presets get the new controls' defaults", "[plugin]")
{
    Plugin t;
    auto library = std::make_shared<Library>();
    for (int s = 0; s < numStrings; ++s)
        for (int f = 0; f <= numFrets; ++f)
        {
            auto take = std::make_shared<Take>();
            take->sampleRate = 48000.0;
            // (the high E's 24th fret holds a 5 kHz tone instead: something up where BITE lifts)
            const auto hz = s == 6 && f == numFrets ? 5000.0 : guitar::frequencyOf (noteOf (s, f));
            for (int i = 0; i < 48000; ++i)
                take->head.push_back (0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));
            take->frames = (juce::int64) take->head.size();
            library->add (Articulation::sustain, s, f, 1, Stroke::down, 0, take);
        }
    t.p.setLibraryForTest (library);
    REQUIRE (t.p.waitUntilIdle (5000));
    const auto levelOf = [&t] (int note, float bite, double hz = 0.0)
    {
        t.p.setParameterNow (ParamIDs::bite, bite);
        auto out = t.play (40, { { note, 100 } });
        t.play (40, { { note, 0 } });
        t.play (20);
        return levelAt (out, hz > 0.0 ? hz : guitar::frequencyOf (note), 4000, 4800);
    };
    const auto midDown = levelOf (64, 1.0f) / levelOf (64, 0.0f);                   // E4, 330 Hz: near the 400 Hz cut
    const auto topUp = levelOf (88, 1.0f, 5000.0) / levelOf (88, 0.0f, 5000.0);     // 5 kHz: on the shelf
    CHECK (juce::Decibels::gainToDecibels (midDown) < -3.0f);
    CHECK (juce::Decibels::gainToDecibels (topUp) > 4.0f);

    CHECK (t.p.getLatencySamples() == 0);
    t.p.setParameterNow (ParamIDs::polyWindow, 10.0f);
    t.p.prepareToPlay (sampleRate, blockSize);
    CHECK (t.p.getLatencySamples() == 480);

    // A preset saved before 0.4.0 (none of the new controls in it): their defaults.
    Rig old;
    old.name = "Old";
    old.parameters[ParamIDs::fretMode] = 1.0f;
    t.p.setParameterNow (ParamIDs::capo, 4.0f);
    t.p.setParameterNow (ParamIDs::mono, 1.0f);
    const auto file = t.p.getRigLibrary().saveAsNew (old);
    REQUIRE (t.p.getRigLibrary().find (file) != nullptr);
    t.p.loadRig (*t.p.getRigLibrary().find (file));
    CHECK (t.p.getGuitarSettings().capo == 0);
    CHECK_FALSE (t.p.getGuitarSettings().mono);
    CHECK (t.p.getGuitarSettings().fretMode == FretMode::polyphonic);
}
