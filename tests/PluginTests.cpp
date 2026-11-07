// THE PLUGIN (PR7-I 0.2.0): an instrument playing the guitar folder's takes; importing through it; projects, presets,
// undo, MIDI learn; the window (its tabs, the GUITAR page, the fretboard).
#include "TestUtils.h"

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
    juce::File fakeRenderFile (int string, const juce::File& file)
    {
        constexpr double rate = 8000.0;
        std::vector<float> audio ((size_t) (renders::lengthSeconds (Articulation::sustain, string) * rate), 0.0f);
        for (const auto& take : renders::takes (Articulation::sustain, string))
        {
            const auto hz = guitar::frequencyOf (take.note());
            const auto start = (int) ((take.noteSeconds + 0.015) * rate);
            const auto overtone = hz * (2.0 + take.roundRobin);   // each round-robin its own recording
            for (int i = 0; i < (int) (renders::holdSeconds * rate); ++i)
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
    CHECK (t.p.getImportReport (2) == "300 notes imported, all in tune");
    CHECK (t.p.getLibrary()->count (Articulation::sustain, 2) == 300);
    CHECK (guitarFolder.getChildFile ("Sustain/s2_f00_l1_d_r0.wav").existsAsFile());
    CHECK (t.p.getImportReport (3).isEmpty());

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
    CHECK (t.p.getImportReport (4).contains ("aren't the pitch"));
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

TEST_CASE ("THE WINDOW: GUITAR, PRESETS and SETTINGS tabs; the GUITAR page; the fretboard lights the notes played", "[ui]")
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
    CHECK (view->getGuitarPage().isVisible());
    CHECK (view->findChildWithID ("drumsTab") == nullptr);
    std::vector<juce::MidiKeyboardComponent*> keyboards;
    findAll (*view, keyboards);
    CHECK (keyboards.empty());   // no on-screen keyboard (PR7-D 0.41.0, the owner's ask)
    checkLayout (*view, "guitar");

    std::vector<juce::Button*> tabs;
    for (const auto* id : { "guitarTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    view->showPage (MainView::Page::presets);
    CHECK_FALSE (view->getGuitarPage().isVisible());
    CHECK (tabs[1]->getToggleState());
    checkLayout (*view, "presets");
    view->showPage (MainView::Page::settings);
    CHECK (view->getSettingsPage().isVisible());
    checkLayout (*view, "settings");
    view->showPage (MainView::Page::guitar);
    CHECK (view->getFretboard().isVisible());

    // The rows of RENDERS: each string's, what's there for it.
    CHECK (view->getGuitarPage().getRow (0).getStatus() == "25 of 300 takes");
    view->getGuitarPage().refresh();
    CHECK (view->getGuitarPage().getGuitarStatus().contains ("175 takes"));

    // A power chord on the low strings: the fretboard lights the strings and frets it's played on.
    t.play (2, { { 33, 100 }, { 40, 100 }, { 45, 100 } });
    view->getFretboard().refresh();
    CHECK (view->getFretboard().shownFret (0) == 0);
    CHECK (view->getFretboard().shownFret (1) == 0);
    CHECK (view->getFretboard().shownFret (2) == 0);
    CHECK (view->getFretboard().shownFret (3) == -1);
    t.play (2, { { 33, 0 }, { 40, 0 }, { 45, 0 }, { 38, 100 } });
    t.play (2);
    view->getFretboard().refresh();
    CHECK (view->getFretboard().shownFret (0) == 5);
    CHECK (view->getFretboard().shownFret (2) == -1);
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
