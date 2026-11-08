// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Options (environment variables):
//   PARAGON_SNAPSHOT_WIDTH=n         the window's width (default 1600; 3200 for a sharp 2x look)
//   PARAGON_SNAPSHOT_PAGE=guitar|renders|presets|settings   (guitar, the default)
//   PARAGON_SNAPSHOT_RACK=fretting|noises                  (the GUITAR page's rack on its FRETTING or NOISES page)
//   PARAGON_SNAPSHOT_RENDERS=noises                        (the RENDERS page's NOISES tab)
//   PARAGON_SNAPSHOT_PRESET=name     that factory preset loaded
//   PARAGON_SNAPSHOT_PARAMS=id=value,...   parameters set (real units)
//   PARAGON_SNAPSHOT_NOTES=1         a guitar of made-up takes (every string, frets 0-24; the low A, E and B strings
//                                    complete, so RENDERS shows some imported) and a drop A power chord at the 5th fret
//                                    held, lit on the fretboard; =2 an open power chord and a note up the neck; =3 a
//                                    palm-muted D5 (rings) and an open high E (a dot)
//   PARAGON_SNAPSHOT_RENDERS=mute    RENDERS on PALM MUTE
//   PARAGON_SNAPSHOT_FINISH=0|1|2    the plugin's finish (pearl white, pearl black, pearl white + neon)
//   PARAGON_SNAPSHOT_PROGRESS=1      the progress box: a render import 62% done, with CANCEL
//   PARAGON_SNAPSHOT_MIDI=1          demo MIDI links and a knob learning (with PAGE=settings: the links list)
#include <catch2/catch_test_macros.hpp>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Finish.h"

using namespace guitar;

TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    REQUIRE (processor.waitUntilIdle (5000));

    const auto page = env ("PARAGON_SNAPSHOT_PAGE");
    if (page == "presets")
    {
        processor.loadRig (*processor.getRigLibrary().findFactory ("Chords"));
        processor.saveCurrentRigAs ("My Djent Chugs");
        processor.saveCurrentRigAs ("Clean Arps");
    }
    if (const auto name = env ("PARAGON_SNAPSHOT_PRESET"); name.isNotEmpty())
        if (const auto* entry = processor.getRigLibrary().findFactory (name))
            processor.loadRig (*entry);
    for (const auto& pair : juce::StringArray::fromTokens (env ("PARAGON_SNAPSHOT_PARAMS"), ",", {}))
        processor.setParameterNow (pair.upToFirstOccurrenceOf ("=", false, false), pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue());

    if (const auto notes = env ("PARAGON_SNAPSHOT_NOTES").getIntValue(); notes > 0)
    {
        auto library = std::make_shared<Library>();
        for (int s = 0; s < numStrings; ++s)
            for (int f = 0; f <= numFrets; ++f)
                for (int l = 0; l < renders::spec (Articulation::sustain).layers; ++l)
                    for (const auto stroke : { Stroke::down, Stroke::up })
                        for (int rr = 0; rr < renders::spec (Articulation::sustain).roundRobins; ++rr)
                        {
                            if (s != 0 && s != 1 && s != 5 && (l != 1 || stroke != Stroke::down || rr != 0))
                                continue;   // only the low A, E and B strings complete
                            auto take = std::make_shared<Take>();
                            take->sampleRate = 8000.0;
                            for (int i = 0; i < 16000; ++i)
                                take->head.push_back (0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * frequencyOf (noteOf (s, f)) * i / 8000.0));
                            take->frames = (juce::int64) take->head.size();
                            library->add (Articulation::sustain, s, f, l, stroke, rr, take);
                        }
        // Palm mutes on the low three strings (one take each degree: enough to show them).
        for (int s = 0; s < 3; ++s)
            for (int f = 0; f <= numFrets; ++f)
                for (int l = 0; l < renders::spec (Articulation::mute).layers; ++l)
                {
                    auto take = std::make_shared<Take>();
                    take->sampleRate = 8000.0;
                    for (int i = 0; i < 8000; ++i)
                        take->head.push_back (0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * frequencyOf (noteOf (s, f)) * i / 8000.0));
                    take->frames = (juce::int64) take->head.size();
                    library->add (Articulation::mute, s, f, l, Stroke::down, 0, take);
                }
        processor.setLibraryForTest (library);
        REQUIRE (processor.waitUntilIdle (5000));
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        // =3: a palm-muted D5 (the split map: velocity 30) and an open high E rung out: rings and a dot.
        if (notes == 3)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 13, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 30), 1);
            processor.processBlock (buffer, midi);
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 12, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 1);
        }
        else
            for (const auto n : notes == 1 ? std::vector<int> { 38, 45, 50 } : std::vector<int> { 33, 40, 45, 69 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
        processor.processBlock (buffer, midi);
        midi.clear();
        for (int b = 0; b < 8; ++b)
            processor.processBlock (buffer, midi);
    }
    if (env ("PARAGON_SNAPSHOT_MIDI") == "1")
    {
        MidiLinks links;
        links.link (1, ParamIDs::velocityToVolume);
        links.link (11, ParamIDs::tone);
        links.link (20, ParamIDs::release);
        processor.setMidiLinks (links);
        processor.startMidiLearn (ParamIDs::handPosition);
    }

    if (const auto f = env ("PARAGON_SNAPSHOT_FINISH"); f.isNotEmpty())
        Finish::set ((Finish::Type) juce::jlimit (0, 2, f.getIntValue()), false);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    if (env ("PARAGON_SNAPSHOT_RENDERS") == "mute")
        view->getRendersPage().showArticulation (guitar::Articulation::mute);
    if (page == "presets")
        view->showPage (MainView::Page::presets);
    else if (page == "settings")
        view->showPage (MainView::Page::settings);
    else if (page == "renders")
        view->showPage (MainView::Page::renders);
    view->getRendersPage().refresh();
    if (env ("PARAGON_SNAPSHOT_RACK") == "fretting")
        view->getGuitarView().getRack().setPage (GuitarRack::Page::fretting);
    if (env ("PARAGON_SNAPSHOT_RACK") == "noises")
        view->getGuitarView().getRack().setPage (GuitarRack::Page::noises);
    if (env ("PARAGON_SNAPSHOT_RENDERS") == "noises")
        view->getRendersPage().showNoises();
    view->getGuitarView().refresh();
    view->getFretboard().refresh();
    if (env ("PARAGON_SNAPSHOT_PROGRESS") == "1")
        view->getProgressBox().show ({ "IMPORTING STRING 7 (LOW A)", [] { return 0.62; }, [] {} });

    const auto width = juce::jmax (960, juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", "1600").getIntValue());
    editor->setSize (width, width * 9 / 16);
    editor->resized();
    view->getFretboard().refresh();
    view->getGuitarView().refresh();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
