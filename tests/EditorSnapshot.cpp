#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Reverb.h"
#include "rigs/FactoryRigs.h"
#include "ui/ParagonLookAndFeel.h"

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the
// UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Options (environment variables):
//   PARAGON_SNAPSHOT_WIDTH=n         the window's width (default 1600; 2560 for a sharp 2x look)
//   PARAGON_SNAPSHOT_PAGE=presets    the preset browser (with two example user presets), or =settings
//   PARAGON_SNAPSHOT_PRESET=name     that factory preset loaded
//   PARAGON_SNAPSHOT_PARAMS=id=value,...   parameters set (real units)
//   PARAGON_SNAPSHOT_PAGE=drums      THE DRUMS, the page it opens on (also with no PAGE) (PARAGON_SNAPSHOT_DRUM=n: slot n chosen;
//                                    PARAGON_SNAPSHOT_DRUMS=1: a beat just played, its rows lit; =2 a file of nine kicks, the 4th chosen;
//                                    PARAGON_SNAPSHOT_DRUMSEQ=1: the SEQUENCER view, a beat playing with the song;
//                                    PARAGON_SNAPSHOT_MACHINE=1: THE DRUM MACHINE playing Room Beat (=2: drums 19-36, REC;
//                                    PARAGON_SNAPSHOT_MACHINEFINISH=0|1|2: in that finish);
//                                    PARAGON_SNAPSHOT_DRUMEDIT=1: the clap with an envelope, an anchor, GAIN and LP, =2 with DRUMS=2 a fade;
//                                    PARAGON_SNAPSHOT_DRUMCHOSEN=1: the hats and cymbal chosen together, the tom's playhead;
//                                    any of these shows the DRUM view; with none, the KIT VIEW (0.14.0), the page's first:
//                                    PARAGON_SNAPSHOT_KIT=1: the kick, snare, crash and china just hit, glowing, the ride's bell chosen;
//                                    PARAGON_SNAPSHOT_ARTIC=1: the hi-hat's articulations, OPEN 3 off, the pedal half open;
//                                    PARAGON_SNAPSHOT_VELOCITY=1: the snare's VELOCITY curve and gate;
//                                    PARAGON_SNAPSHOT_INSTRUMENT=1: the snare from a folder of 16 hits, its layers)
//   PARAGON_SNAPSHOT_PAGE=fx         THE RACK (PARAGON_SNAPSHOT_RACK=1: at work on a beat, the sends and bus A too; PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi: that view, the inserts' on bus PARAGON_SNAPSHOT_BUS=1-4)
//   PARAGON_SNAPSHOT_PAGE=mixer      THE MIXER (0.20.0) with a multi-mic kit, a hit just played
//                                    (PARAGON_SNAPSHOT_MIXERBUS=1: buses and sends, the snare top's SENDS; =2 the buses;
//                                    PARAGON_SNAPSHOT_MIXERFX=1: effects on the snare top, its EFFECTS tab (=3: SATURATION);
//                                    PARAGON_SNAPSHOT_MIXERROUTE=1: ROUTE MICS on the snare, =2 the user channels;
//                                    PARAGON_SNAPSHOT_MICSIM=1: the Djent Kit through SIMULATED MICS, their panel open,
//                                    =2 its strips after a kick and snare)
//   PARAGON_SNAPSHOT_PACK=<folder> a sample pack (0.33.0) imported: the DRUM view on its kick, or with PAGE=mixer its
//     channels, scrolled to its USER channels.
//   PARAGON_SNAPSHOT_DGKIT=<kit file> a DrumGizmo kit (0.27.0) loaded LIGHT from your computer (none ships with the
//                                    tests): DRUMS shows the snare's layers; with PAGE=mixer its channels after a hit;
//                                    PARAGON_SNAPSHOT_LAYER=<audio file>: that sample as the kick's LAYER (0.29.0)
//   PARAGON_SNAPSHOT_NOTES=1         the kick, snare and closed hat's keys held on the keyboard (they light)
//   PARAGON_SNAPSHOT_MIDI=1          demo MIDI links and a knob learning (with PAGE=settings: the links list)
TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    const auto page = env ("PARAGON_SNAPSHOT_PAGE");
    if (page == "presets")
    {
        processor.loadRig (*processor.getRigLibrary().findFactory ("Room Beat"));
        processor.saveCurrentRigAs ("My Room Beat");
        processor.saveCurrentRigAs ("Live Funk Kit");
    }

    // THE GROOVES page: a demo pack of our own (made here, as a pack's files are written), linked and read.
    const auto groovePack = rigFolder.getChildFile ("My Grooves");
    if (page == "grooves")
    {
        const auto write = [&] (const juce::String& path, double bpm, int hat, int bars, int busy, int softness, bool swing = false)
        {
            juce::MidiMessageSequence track;
            track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
            const auto add = [&] (double beat, int note, int velocity)
            {
                track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, velocity - softness)).withTimeStamp (beat * 480));
                track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
            };
            for (int bar = 0; bar < bars; ++bar)
            {
                const auto b = bar * 4.0;
                add (b, 36, 120);
                add (b + 2.5, 36, 100);
                add (b + 1.0, 38, 115);
                add (b + 3.0, 38, 118);
                for (int e = 0; e < 8 * busy; ++e)
                    add (b + (swing ? e / 2 + (e % 2 == 1 ? 2.0 / 3.0 : 0.0) : e * 0.5 / busy), hat, e % 2 == 0 ? 100 : 70);
            }
            juce::MidiFile file;
            file.setTicksPerQuarterNote (480);
            track.updateMatchedPairs();
            file.addTrack (track);
            const auto f = groovePack.getChildFile (path);
            f.getParentDirectory().createDirectory();
            juce::FileOutputStream out (f);
            file.writeTo (out);
        };
        write ("Rock 96 BPM/Verse 01.mid", 96.0, 42, 4, 1, 0);
        write ("Rock 96 BPM/Verse 02.mid", 96.0, 42, 4, 1, 20);
        write ("Rock 96 BPM/Chorus 01.mid", 96.0, 46, 4, 1, 0);
        write ("Rock 96 BPM/Bridge 01.mid", 96.0, 51, 8, 1, 10);
        write ("Rock 96 BPM/Fill 01.mid", 96.0, 49, 1, 2, 0);
        write ("Funk 104 BPM/Verse 01.mid", 104.0, 42, 2, 2, 10);
        write ("Funk 104 BPM/Chorus 01.mid", 104.0, 22, 2, 2, 0);
        write ("Ballads/Slow 68 BPM/Verse 01.mid", 68.0, 51, 4, 1, 40);
        write ("Shuffle 124 BPM/Verse 01.mid", 124.0, 42, 2, 1, 0, true);
        write ("Shuffle 124 BPM/Chorus 01.mid", 124.0, 51, 2, 1, 0, true);
        // PARAGON_SNAPSHOT_GROOVEDB=1 (0.15.0): a groove database in the pack too (tests/data's, its grooves invented).
        if (env ("PARAGON_SNAPSHOT_GROOVEDB") == "1")
        {
            groovePack.getChildFile ("Demo Database").createDirectory();
            juce::File (PARAGON_TEST_DATA).getChildFile ("midiDB").copyFileTo (groovePack.getChildFile ("Demo Database/midiDB"));
        }
        REQUIRE (processor.getGrooveLibrary().addFolder (groovePack));
        processor.getGrooveLibrary().scanNow();
        processor.getGrooveLibrary().setFavourite (groovePack.getChildFile ("Rock 96 BPM/Chorus 01.mid"), true);
    }

    if (const auto name = env ("PARAGON_SNAPSHOT_PRESET"); name.isNotEmpty())
    {
        const auto* entry = processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
    }

    for (const auto& pair : juce::StringArray::fromTokens (env ("PARAGON_SNAPSHOT_PARAMS"), ",", {}))
    {
        const auto id = pair.upToFirstOccurrenceOf ("=", false, false).trim();
        auto* parameter = processor.getParameters().getParameter (id);
        INFO (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    // Keys held, played for a moment so the hits are sounding.
    if (env ("PARAGON_SNAPSHOT_NOTES") == "1")
    {
        for (const auto note : { 36, 38, 42 })
            processor.getKeyboardState().noteOn (1, note, 0.8f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < 40; ++block)
            processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    if (page == "fx")
    {
        // THE RACK. PARAGON_SNAPSHOT_RACK=1: all four units at work on "Room Beat" playing with the song (ECHELON's low
        // cut, a dip, a dynamic presence band and an air shelf; DOMINION; CONSTRICTOR; the LIMITER on MODERN with TRUE
        // PEAK, 6 dB in), the editor's timers run so the ladders and screens read it.
        // PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter opens that unit's page.
        view->showPage (MainView::Page::fx);
        if (env ("PARAGON_SNAPSHOT_RACK") == "1")
        {
            processor.loadRig (*processor.getRigLibrary().findFactory ("Room Beat"));
            const auto set = [&] (const juce::String& id, float value)
            {
                auto* parameter = processor.getParameters().getParameter (id);
                REQUIRE (parameter != nullptr);
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            struct Band { eq::Shape shape; float hz, db, q, dynamic; };
            const Band bands[] = { { eq::Shape::lowCut, 40.0f, 0.0f, 0.7f, 0.0f }, { eq::Shape::bell, 280.0f, -3.0f, 1.2f, 0.0f },
                                   { eq::Shape::bell, 3200.0f, 1.5f, 1.5f, -4.0f }, { eq::Shape::highShelf, 11000.0f, 2.0f, 0.7f, 0.0f } };
            for (int i = 0; i < 4; ++i)
            {
                set (ParamIDs::eqBand (i, "Used"), 1.0f);
                set (ParamIDs::eqBand (i, "Shape"), (float) (int) bands[i].shape);
                set (ParamIDs::eqBand (i, "Freq"), bands[i].hz);
                set (ParamIDs::eqBand (i, "Gain"), bands[i].db);
                set (ParamIDs::eqBand (i, "Q"), bands[i].q);
                set (ParamIDs::eqBand (i, "Dyn"), bands[i].dynamic);
            }
            set (ParamIDs::eqOn, 1.0f);
            set (ParamIDs::domOn, 1.0f);
            set (ParamIDs::domDepth, 7.0f);
            set (ParamIDs::compOn, 1.0f);
            set (ParamIDs::compThreshold, -26.0f);
            set (ParamIDs::compRatio, 3.0f);
            set (ParamIDs::limitOn, 1.0f);
            set (ParamIDs::limitTruePeak, 1.0f);
            set (ParamIDs::limitGain, 6.0f);
            // The SENDS: DELAY in ping-pong eighths dotted, REVERB a hall; the snare and clap send to the delay.
            set (ParamIDs::delayOn, 1.0f);
            set (ParamIDs::delayPingPong, 1.0f);
            set (ParamIDs::delaySync, 7.0f);
            set (ParamIDs::delayFeedback, 0.45f);
            set (ParamIDs::delayToReverb, 0.3f);
            set (ParamIDs::reverbOn, 1.0f);
            set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::hall);
            set (ParamIDs::reverbSize, 0.65f);
            for (int k = 0; k < 12; ++k)
                set (ParamIDs::drum (k, "SendDelay"), k == 2 || k == 3 ? 0.4f : 0.0f);
            // MOD / LO-FI: the Vortex in STEREO, AEON after it, a gently worn cassette.
            set (ParamIDs::modOn, 1.0f);
            set (ParamIDs::modStereo, 1.0f);
            set (ParamIDs::modMix, 0.6f);
            set (ParamIDs::mod2On, 1.0f);
            set (ParamIDs::mod2Depth, 0.6f);
            set (ParamIDs::mod2Res, 6.5f);
            set (ParamIDs::lofiOn, 1.0f);
            set (ParamIDs::lofiType, (float) (int) effects::LoFi::Type::cassette);
            set (ParamIDs::lofiAmount, 3.0f);
            set (ParamIDs::lofiNoise, 2.0f);
            // THE INSERTS: the kick and snare through bus A: SKYLINE driven, a SEQUENCE glitch.
            set (ParamIDs::drum (0, "Insert"), 1.0f);
            set (ParamIDs::drum (2, "Insert"), 1.0f);
            set (ParamIDs::ins (0, "DriveOn"), 1.0f);
            set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::skyline);
            set (ParamIDs::ins (0, "DriveGain"), 7.0f);
            set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            set (ParamIDs::ins (0, "GlitchMix"), 0.6f);
            struct Song final : juce::AudioPlayHead
            {
                double ppq = 0.0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (120.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Song song;
            processor.setPlayHead (&song);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int block = 0; block < 700; ++block)
            {
                processor.processBlock (buffer, midi);
                song.ppq += 256.0 / 48000.0 * 2.0;
                if (block > 600 && block % 10 == 0)   // the timers fire only when they're due
                {
                    juce::Thread::sleep (35);
                    juce::Timer::callPendingTimersSynchronously();
                }
            }
            processor.setPlayHead (nullptr);
        }
        const auto fx = env ("PARAGON_SNAPSHOT_FX");
        auto& fxPage = view->getFxPage();
        if (const auto bus = env ("PARAGON_SNAPSHOT_BUS").getIntValue(); bus >= 1 && bus <= 4)
            fxPage.getInsertStrip().setBus (bus - 1);
        fxPage.show (fx == "eq" ? FxPage::View::eq : fx == "dominion" ? FxPage::View::dominion : fx == "comp" ? FxPage::View::comp
                     : fx == "limiter" ? FxPage::View::limiter : fx == "sends" ? FxPage::View::sends : fx == "delay" ? FxPage::View::delay
                     : fx == "reverb" ? FxPage::View::reverb : fx == "modfx" ? FxPage::View::modFx : fx == "mod" ? FxPage::View::mod
                     : fx == "mod2" ? FxPage::View::mod2 : fx == "lofi" ? FxPage::View::loFi : fx == "inserts" ? FxPage::View::inserts
                     : fx == "insdrive" ? FxPage::View::insertDrive : fx == "insglitch" ? FxPage::View::insertGlitch
                     : fx == "insmod" ? FxPage::View::insertMod : fx == "inslofi" ? FxPage::View::insertLoFi : FxPage::View::rack);
        // A display reads its bands and analyser on its own timer, but only while it's on screen, which a snapshot
        // never is (as PR7-E's snapshots, refresh them by hand). ECHELON with its dynamic band 3 selected.
        if (fx == "eq")
        {
            fxPage.getEqPage().getDisplay().refresh();
            fxPage.getEqPage().getDisplay().select (2);
            fxPage.getEqPage().refreshControls();
            if (env ("PARAGON_SNAPSHOT_MATCH") == "1")
            {
                fxPage.getEqPage().showMatch (true);
                fxPage.getEqPage().getMatchPanel().refresh();
            }
        }
        if (fx == "dominion")
            fxPage.getDominionPage().getDisplay().refresh();
        if (fx == "comp")
            fxPage.getCompPage().getDisplay().refresh();
        if (fx == "limiter")
            fxPage.getLimiterPage().refresh();
    }
    if (env ("PARAGON_SNAPSHOT_MIDI") == "1")
    {
        // A few MIDI links on the kick's knobs and the kit's, and the snare's DECAY learning.
        MidiLinks links;
        links.link (74, ParamIDs::drum (0, "Tone"));
        links.link (71, ParamIDs::drum (0, "Decay"));
        links.link (1, ParamIDs::drumsLevel);
        links.link (7, ParamIDs::reverbMix);
        processor.setMidiLinks (links);
        processor.startMidiLearn (ParamIDs::drum (0, "Snap"));
        processor.controllerForTest (74, 0.6f);
        if (env ("PARAGON_SNAPSHOT_PAGE") == "settings")
        {
            view->showPage (MainView::Page::drums);
            view->showPage (MainView::Page::settings);
        }
    }
    if (page == "drums" || page.isEmpty())
    {
        // THE DRUMS: slot PARAGON_SNAPSHOT_DRUM (1-12) chosen; PARAGON_SNAPSHOT_DRUMS=1 plays a beat into it first (kick,
        // snare and closed hat just hit: their rows lit).
        view->showPage (MainView::Page::drums);
        auto& drums = view->getDrumPage();
        for (const auto* what : { "PARAGON_SNAPSHOT_DRUM", "PARAGON_SNAPSHOT_DRUMS", "PARAGON_SNAPSHOT_DRUMEDIT", "PARAGON_SNAPSHOT_DRUMCHOSEN" })
            if (env (what).isNotEmpty())
                drums.showView (DrumPage::View::drum);
        if (env ("PARAGON_SNAPSHOT_KIT") == "1")
        {
            drums.showView (DrumPage::View::kit);
            drums.select (13);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (const auto note : { 36, 38, 47, 51 })   // BD, SD, CY, CHN (ROOT 36)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 8; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        // 0.17.0: the hi-hat's articulations: OPEN 3 off (its stand-in named), the pedal half open, and OPEN 3's note
        // just played (OPEN 4 lights: it plays for it).
        if (env ("PARAGON_SNAPSHOT_ARTIC") == "1")
        {
            drums.showView (DrumPage::View::kit);
            drums.select (6);
            processor.getParameters().getParameter (ParamIDs::drum (22, "On"))->setValueNotifyingHost (0.0f);
            auto* map = processor.getParameters().getParameter (ParamIDs::drumsMap);
            map->setValueNotifyingHost (map->convertTo0to1 (1.0f));   // GROOVE LIBRARY
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 4, 64), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 26, 1.0f), 1);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 4; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMS") == "2")
        {
            // A file of nine kicks (each a semitone higher) in the kick's slot, its 4th kick chosen.
            auto file = std::make_shared<synth::drums::Sample>();
            file->name = "Kit_1_Kick";
            file->sampleRate = 44100.0;
            file->channels.assign (1, std::vector<float> ((size_t) (4.0 * 44100.0), 0.0f));
            for (int k = 0; k < 9; ++k)
            {
                synth::drums::SlotSettings kick;
                kick.pitch = (float) k;
                kick.decay = 0.3f;
                const auto hit = synth::drums::Kit::renderHit (kick, 1.0f, 44100.0, 0.4);
                const auto at = (int) (0.05 * 44100.0) + (int) (k * 0.45 * 44100.0);
                for (int i = 0; i < hit.getNumSamples() && at + i < file->length(); ++i)
                    file->channels[0][(size_t) (at + i)] = hit.getSample (0, i);
            }
            processor.setDrumSample (0, file);
            if (auto* m = processor.getParameters().getParameter (ParamIDs::drum (0, "Model")))
                m->setValueNotifyingHost (m->convertTo0to1 ((float) (int) synth::drums::Model::sample));
            drums.select (0);
            drums.setSize (1476, 672);
            drums.getDisplay().pickHit (3);
            if (env ("PARAGON_SNAPSHOT_DRUMEDIT") == "2")   // and a fade out drawn over it
            {
                auto matrix = processor.getModMatrix();
                matrix.drumEnvelopes[0] = { { { 0.0f, 1.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
                processor.setModMatrix (matrix);
                drums.getEnvelopeEditor().setTarget (MultiPointEditor::Kind::drumEnvelope, 0);
            }
            drums.refreshPictures();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMS") != "2" && env ("PARAGON_SNAPSHOT_KIT").isEmpty() && env ("PARAGON_SNAPSHOT_ARTIC").isEmpty()
            && env ("PARAGON_SNAPSHOT_VELOCITY").isEmpty() && env ("PARAGON_SNAPSHOT_INSTRUMENT").isEmpty())
            drums.select (juce::jmax (1, env ("PARAGON_SNAPSHOT_DRUM").getIntValue()) - 1);
        if (env ("PARAGON_SNAPSHOT_DRUMS") == "1")
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (const auto note : { 36, 38, 42 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 6; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        // 0.19.0: THE SAMPLE ENGINE: the snare from a folder of 16 hits (our snare synth rendered at four strengths, four
        // takes each), the last hit played lit.
        if (env ("PARAGON_SNAPSHOT_INSTRUMENT") == "1")
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotSnare");
            folder.deleteRecursively();
            folder.createDirectory();
            juce::WavAudioFormat wav;
            for (int v = 0; v < 4; ++v)
                for (int rr = 0; rr < 4; ++rr)
                {
                    synth::drums::SlotSettings snare;
                    snare.model = synth::drums::Model::snare;
                    snare.pitch = 0.3f * (float) rr;
                    snare.snap = 0.3f + 0.15f * (float) v;
                    const auto hit = synth::drums::Kit::renderHit (snare, 0.25f + 0.25f * (float) v, 48000.0, 0.5);
                    const auto file = folder.getChildFile ("snare_v" + juce::String (v + 1) + "_rr" + juce::String (rr + 1) + ".wav");
                    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 1, 24, {}, 0));
                    if (writer != nullptr)
                        writer->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
                }
            processor.loadInstrumentFolder (2, folder);
            processor.waitForInstruments();
            drums.showView (DrumPage::View::drum);
            drums.select (2);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 90), 0);
            processor.processBlock (buffer, midi);
            drums.resized();
            drums.refreshPictures();
        }
        // 0.33.0: a sample pack's folder (PARAGON_SNAPSHOT_PACK): the DRUM view on its kick, a hit played.
        if (const auto packFolder = env ("PARAGON_SNAPSHOT_PACK"); packFolder.isNotEmpty() && page != "mixer")
        {
            processor.importSamplePack (juce::File (packFolder));
            processor.waitForInstruments (600000);
            drums.showView (DrumPage::View::drum);
            drums.select (0);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
            processor.processBlock (buffer, midi);
            drums.resized();
            drums.refreshPictures();
        }
        // 0.27.0: a DrumGizmo kit from this computer (PARAGON_SNAPSHOT_DGKIT): the snare's layers, a hit played.
        if (const auto dg = env ("PARAGON_SNAPSHOT_DGKIT"); dg.isNotEmpty() && page != "mixer")
        {
            processor.importDrumGizmoKit (juce::File (dg), ParagonX1AudioProcessor::KitLoad::streamed);
            processor.waitForInstruments (600000);
            drums.showView (DrumPage::View::drum);
            // 0.29.0: PARAGON_SNAPSHOT_LAYER=<audio file>: that sample as the kick's LAYER, the kick shown.
            const auto layer = env ("PARAGON_SNAPSHOT_LAYER");
            if (layer.isNotEmpty())
            {
                processor.loadLayer (0, juce::File (layer));
                processor.waitForInstruments (60000);
            }
            drums.select (layer.isNotEmpty() ? 0 : 2);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, layer.isNotEmpty() ? 36 : 38, (juce::uint8) 110), 0);
            processor.processBlock (buffer, midi);
            drums.resized();
            drums.refreshPictures();
        }
        // 0.18.0: the snare's VELOCITY: a curve with soft hits lifted and a bend, SOFT on, the gate raised.
        if (env ("PARAGON_SNAPSHOT_VELOCITY") == "1")
        {
            drums.showView (DrumPage::View::drum);
            drums.select (2);
            synth::drums::VelocityCurve curve;
            curve.points[0] = { 0.08f, 0.25f };
            curve.add ({ 0.55f, 0.75f });
            curve.soft = true;
            drums.setVelocityCurve (curve, true);
            if (auto* low = processor.getParameters().getParameter (ParamIDs::drum (2, "VelMin")))
                low->setValueNotifyingHost (low->convertTo0to1 (6.0f));
            drums.showVelocity (true);
        }
        if (env ("PARAGON_SNAPSHOT_DRUMEDIT") == "1")
        {
            // THE SAMPLE EDITOR (6c): the clap with a drawn envelope (its flams held, its tail cut), an anchor on its
            // main slap, GAIN and LP.
            const auto set = [&processor] (const juce::String& id, float value)
            {
                if (auto* p = processor.getParameters().getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            };
            set (ParamIDs::drum (3, "Delay"), -22.0f);
            set (ParamIDs::drum (3, "Gain"), 3.0f);
            set (ParamIDs::drum (3, "LowPass"), 7000.0f);
            set (ParamIDs::drum (3, "Decay"), 0.7f);
            auto matrix = processor.getModMatrix();
            matrix.drumEnvelopes[3] = { { { 0.0f, 1.0f, 0.0f }, { 0.35f, 0.9f, -0.4f }, { 0.6f, 0.25f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            processor.setModMatrix (matrix);
            processor.updateDrumReverses();   // the clap's hit length, which the envelope spans
            drums.select (3);
            drums.refreshPictures();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMCHOSEN") == "1")
        {
            // 6c part 2: the hats and cymbal chosen together, a kit and sequence "loaded", the tom's hit playing.
            drums.clickRow (6, {});
            drums.clickRow (8, juce::ModifierKeys::ctrlModifier);
            drums.clickRow (10, juce::ModifierKeys::ctrlModifier);
            drums.clickRow (11, juce::ModifierKeys::ctrlModifier);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 12; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMSEQ") == "1")
        {
            // THE DRUM SEQUENCER (stage 6b): a beat in pattern A (accented hats with a roll, a pitched tom fill, a
            // reversed cymbal swelling into the bar, a short open hat), played with the song to just past beat 2, the
            // snares chosen.
            synth::drumseq::Pattern beat;
            const auto note = [&beat] (int row, double start, float velocity = 1.0f) -> synth::drumseq::Note&
            {
                synth::drumseq::Note n;
                n.row = row;
                n.start = start;
                n.velocity = velocity;
                beat.notes.push_back (n);
                return beat.notes.back();
            };
            for (const auto k : { 0.0, 3.0, 8.0, 10.0 })
                note (0, k);
            note (2, 4.0);
            note (2, 12.0);
            for (int k = 0; k < 14; k += 2)
                note (6, k, k % 4 == 0 ? 1.0f : 0.55f);
            note (6, 14.0, 0.8f).roll = 2;
            note (10, 7.0, 0.7f).gate = 0.25f;
            note (9, 13.0).pitch = 2.0f;
            note (7, 14.0);
            note (5, 15.0).pitch = -3.0f;
            note (11, 15.0).reverse = synth::drumseq::Reverse::endSync;
            note (1, 6.0, 0.6f).pan = -0.5f;
            auto matrix = processor.getModMatrix();
            matrix.drumPatterns[0] = beat;
            processor.setModMatrix (matrix);
            if (auto* on = processor.getParameters().getParameter (ParamIDs::dseqOn))
                on->setValueNotifyingHost (1.0f);
            if (auto* sh = processor.getParameters().getParameter (ParamIDs::shuffle))
                sh->setValueNotifyingHost (0.2f);
            processor.updateDrumReverses();

            struct Song final : juce::AudioPlayHead
            {
                double ppq = 0.0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (120.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Song song;
            processor.setPlayHead (&song);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 58; ++b)   // 1.14 beats: the playhead on step 5
            {
                processor.processBlock (buffer, midi);
                song.ppq += 512.0 / 48000.0 * 2.0;
            }
            processor.setPlayHead (nullptr);
            drums.showView (DrumPage::View::sequencer);
            drums.select (2);
            drums.getSequencer().getGrid().setSelection ({ 4, 5 });
            drums.updateLights();
        }
        if (env ("PARAGON_SNAPSHOT_MACHINE").isNotEmpty())
        {
            // THE DRUM MACHINE (0.38.0): "Room Beat" played by PLAY's own clock (no DAW) to bar 1's 11th step, the snare
            // chosen (its steps shown); =2: drums 19-36 on the columns, REC armed.
            processor.loadRig (*processor.getRigLibrary().findFactory ("Room Beat"));
            drums.select (2);
            drums.showView (DrumPage::View::machine);
            auto& machine = view->getMachine();
            // 0.43.0: PARAGON_SNAPSHOT_MACHINEFINISH=0|1|2: in that finish (pearl white, pearl black, pearl white + neon).
            if (const auto f = env ("PARAGON_SNAPSHOT_MACHINEFINISH"); f.isNotEmpty())
                machine.setFinish ((Finish::Type) juce::jlimit (0, 2, f.getIntValue()), false);
            machine.play();
            if (env ("PARAGON_SNAPSHOT_MACHINE") == "2")
            {
                machine.setBank (1);
                machine.toggleRecord();
            }
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 118; ++b)   // 2.5 beats at 120 BPM: the 11th step
                processor.processBlock (buffer, midi);
            machine.update();
        }
    }
    if (page == "grooves")
    {
        // Every groove listed; TYPE: verses and choruses; TEMPO: the slowest excluded; the second playing a moment in.
        view->showPage (MainView::Page::grooves);
        auto& grooves = view->getGroovesPage();
        grooves.refresh();
        if (env ("PARAGON_SNAPSHOT_GROOVEDB") == "1")
        {
            // The database's rock song chosen in the tree, its second groove playing.
            grooves.chooseFolder (groovePack.getChildFile ("Demo Database/midiDB/Demo Grooves/Straight 4-4/Rock Song 100 BPM"));
            grooves.playRow (1, true);
            grooves.getTable().selectRow (1);
        }
        else
        {
            grooves.toggleFilter (grooves::Column::section, "Verse", false);
            grooves.toggleFilter (grooves::Column::section, "Chorus", false);
            grooves.toggleFilter (grooves::Column::tempo, "Below 80", true);
            grooves.playRow (1, true);
            grooves.getTable().selectRow (1);
        }
        // TAP TO FIND: =1 the panel open, a push beat tapped (kick, snare, eighth hats) and the loop running; =2 found.
        if (const auto tap = env ("PARAGON_SNAPSHOT_TAP"); tap.isNotEmpty())
        {
            grooves.clearFilters();
            processor.stopGroove();
            grooves.openTapPanel();
            auto& panel = grooves.getTapPanel();
            for (const auto b : { 0.0, 1.5, 2.5, 4.0, 5.5, 6.5 }) panel.addTap ((int) grooves::Lane::kick, b + (b > 3 ? 0.04 : -0.03));
            for (const auto b : { 1.0, 3.0, 5.0, 7.0 }) panel.addTap ((int) grooves::Lane::snare, b + 0.05);
            for (int e = 0; e < 16; ++e) panel.addTap ((int) grooves::Lane::hats, e * 0.5 + (e % 3 == 0 ? 0.03 : -0.02));
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int b = 0; b < 230; ++b)
                processor.processBlock (buffer, midi);
            if (tap == "2")
                panel.getFindButton().onClick();
        }
        // RIFF MATCH (0.32.0): =1 a generated guitar riff (4 bars at 120 BPM, palm-muted chugs and accents, a lead-in)
        // in its panel; =2 FIND clicked.
        if (const auto riff = env ("PARAGON_SNAPSHOT_RIFF"); riff.isNotEmpty())
        {
            grooves.clearFilters();
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chug Riff.wav");
            const double rate = 48000.0, bpm = 120.0, lead = 0.35;
            juce::AudioBuffer<float> audio (1, (int) ((lead + 16 * 0.5 + 0.6) * rate));
            audio.clear();
            std::vector<std::pair<double, float>> notes;
            for (int bar = 0; bar < 4; ++bar)
                for (int s16 = 0; s16 < 16; ++s16)
                {
                    const auto accent = s16 == 0 || s16 == 3 || s16 == 6 || s16 == 10 || (bar % 2 == 1 && s16 == 13);
                    if (accent || s16 % 2 == 0)
                        notes.push_back ({ bar * 4.0 + s16 * 0.25, accent ? 1.0f : 0.35f });
                }
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const auto first = (int) ((lead + notes[k].first * 60.0 / bpm) * rate);
                const auto next = k + 1 < notes.size() ? (int) ((lead + notes[k + 1].first * 60.0 / bpm) * rate) - first : (int) (0.5 * rate);
                for (int i = 0; first + i < audio.getNumSamples() && i < juce::jmin ((int) (0.5 * rate), next + 144); ++i)
                {
                    const auto t = i / rate;
                    auto v = 0.0;
                    for (int h = 1; h <= 8; ++h)
                        v += std::sin (juce::MathConstants<double>::twoPi * 82.4 * h * t) / h * std::exp (-t * (3.0 + 2.0 * h));
                    v *= notes[k].second * std::exp (-t * (notes[k].second > 0.5f ? 4.0 : 18.0)) * juce::jmin (1.0, t / 0.002) * (i < next ? 1.0 : 1.0 - (i - next) / 144.0);
                    audio.addSample (0, first + i, (float) (std::tanh (5.0 * v) * 0.5));
                }
            }
            file.deleteFile();
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), rate, 1, 24, {}, 0));
                w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
            grooves.loadRiff (file, true);
            if (riff == "2")
                grooves.getRiffPanel().getFindButton().onClick();
        }
        // GROOVE PARTS: the playing groove's HI-HAT ticked.
        if (env ("PARAGON_SNAPSHOT_PARTS") == "1")
            grooves.toggleDrumPart ((int) grooves::DrumPart::hats);
        if (env ("PARAGON_SNAPSHOT_GROOVESEQ").isNotEmpty())
            grooves.toPattern (1);   // TO SEQUENCER B: its note in the status line
        if (env ("PARAGON_SNAPSHOT_GROOVESEQ") == "2")
        {
            // ...and the pattern it made, in the SEQUENCER.
            view->showPage (MainView::Page::drums);
            view->getDrumPage().showView (DrumPage::View::sequencer);
            view->getDrumPage().getSequencer().selectPattern (1);   // B already: its button lit now, not at the next tick
        }
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < 300; ++b)
            processor.processBlock (buffer, midi);
        grooves.refresh();
        // THE SONG TRACK: a song of the demo pack's Rock grooves (the first verse two times round, a fill, the chorus,
        // the second verse muted, the bridge), shown in the keyboard's place, playing with the DAW in the chorus.
        if (env ("PARAGON_SNAPSHOT_SONG").isNotEmpty())
        {
            processor.stopGroove();
            for (const auto* name : { "Verse 01", "Fill 01", "Chorus 01", "Verse 02", "Fill 01", "Bridge 01" })
                if (const auto g = grooves::read (groovePack.getChildFile ("Rock 96 BPM").getChildFile (juce::String (name) + ".mid"), processor.grooveLayout (0)))
                    processor.addToSong (*g, 0);
            auto edited = processor.getSong();
            edited.resize (0, 32.0);
            edited.blocks[3].muted = true;
            processor.setSong (edited);
            view->showSong (true);
            view->getSongTrack().choose (2);
            struct Host final : juce::AudioPlayHead
            {
                double ppq = 42.5;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (96.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Host host;
            processor.setPlayHead (&host);
            for (int b = 0; b < 20; ++b)
            {
                processor.processBlock (buffer, midi);
                host.ppq += 256.0 / 48000.0 * 96.0 / 60.0;
            }
            processor.setPlayHead (nullptr);
            // =3: THE SONG CREATOR on Rock's first verse, a song made with the bridge structure, its chorus heard.
            if (env ("PARAGON_SNAPSHOT_SONG") == "3")
            {
                view->openSongCreator (groovePack.getChildFile ("Rock 96 BPM/Verse 01.mid"));
                auto& creator = view->getSongCreator();
                creator.chooseStructure (1);
                creator.makeSong (false);
                creator.audition ((int) grooves::Part::chorus, 0);
                view->getSongTrack().choose (-1);
                processor.songBroadcaster.dispatchPendingMessages();   // the strip's line as the plugin shows it
            }
            // =4 (0.11.0): tempo changes (96, gliding from bar 9 to 140 at bar 13) and 3/4 from bar 19, the TEMPO view.
            if (env ("PARAGON_SNAPSHOT_SONG") == "4")
            {
                auto timed = processor.getSong();
                timed.addTempo (0.0, 96.0);
                timed.tempos[(size_t) timed.addTempo (32.0, 96.0)].ramp = true;
                timed.addTempo (48.0, 140.0);
                timed.setMeter (72.0, 3, 4);
                processor.setSong (timed);
                view->getSongTrack().setView (SongTrack::View::tempo);
                processor.songBroadcaster.dispatchPendingMessages();
            }
            // =5 (0.12.0): REC armed, a take's "Recorded" block after the song.
            if (env ("PARAGON_SNAPSHOT_SONG") == "5")
            {
                auto recorded = processor.getSong();
                song::Block take;
                take.name = "Recorded";
                take.length = take.loopLength = 8.0;
                for (int e = 0; e < 16; ++e)
                    take.hits.push_back ({ e * 0.5, e % 4 == 0 ? 0 : e % 4 == 2 ? 2 : 6, e % 2 == 0 ? 0.9f : 0.5f });
                recorded.place (take, recorded.endBeat());
                processor.setSong (recorded);
                processor.setSongRecording (true);
                view->getSongTrack().getRecordButton().setToggleState (true, juce::dontSendNotification);
                processor.songBroadcaster.dispatchPendingMessages();
            }

            // =6 (0.43.0): the song twice over (longer than the lane shows), scrolled on to bar 17: the scroll bar.
            if (env ("PARAGON_SNAPSHOT_SONG") == "6")
            {
                auto longer = processor.getSong();
                const auto count = longer.blocks.size();
                for (size_t i = 0; i < count; ++i)
                    longer.place (longer.blocks[i], longer.endBeat());
                processor.setSong (longer);
                processor.songBroadcaster.dispatchPendingMessages();
                view->getSongTrack().setViewStart (64.0);
            }
            // =2: EDIT BLOCK on the chorus, its snare chosen.
            if (env ("PARAGON_SNAPSHOT_SONG") == "2")
            {
                view->getBlockEditor().open (2);
                view->getBlockEditor().chooseDrum (2, false);
            }
        }
    }
    // A GROOVE HELD OVER THE SEQUENCER's GRID (0.13.0): the DRUMS page's SEQUENCER, the frame and what dropping would do.
    if (page == "grooves" && env ("PARAGON_SNAPSHOT_SEQDROP") == "1")
    {
        view->getGroovesPage().toggleDrumPart (-1);
        view->showPage (MainView::Page::drums);
        view->getDrumPage().showView (DrumPage::View::sequencer);
        view->getDrumPage().getSequencer().getGrid().itemDragEnter ({ "groove:1", nullptr, { 40, 40 } });
    }
    // 0.20.0: THE MIXER with a multi-mic SFZ kit of our own (kick, snare, hat; close mics, overheads, room), a hit
    // just played, the snare top chosen, a little less kick in its bleed.
    const auto micSimShot = env ("PARAGON_SNAPSHOT_MICSIM").getIntValue();
    // 0.33.0: a sample pack (PARAGON_SNAPSHOT_PACK=<folder>) in THE MIXER: kick, snare and tom 4 hit, scrolled to its
    // USER channels (its extra mics), KICK SUB chosen.
    if (const auto packFolder = env ("PARAGON_SNAPSHOT_PACK"); page == "mixer" && packFolder.isNotEmpty())
    {
        processor.importSamplePack (juce::File (packFolder));
        processor.waitForInstruments (600000);
        view->showPage (MainView::Page::mixer);
        view->getMixerPage().routingChanged();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (const auto note : { 36, 38, 36 + synth::drums::notemap::T4 })
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 115), 0);
        processor.processBlock (buffer, midi);
        for (int b = 0; b < 4; ++b)
        {
            juce::MidiBuffer none;
            processor.processBlock (buffer, none);
        }
        view->getMixerPage().selectChannel (synth::mixer::numChannels);
        view->resized();
        view->getMixerPage().scrollToStrip (synth::mixer::stripOfChannel (synth::mixer::numChannels));
        view->getMixerPage().update();
    }
    else if (const auto dg = env ("PARAGON_SNAPSHOT_DGKIT"); page == "mixer" && dg.isNotEmpty())
    {
        processor.importDrumGizmoKit (juce::File (dg), ParagonX1AudioProcessor::KitLoad::streamed);
        processor.waitForInstruments (600000);
        for (const auto& rig : makeFactoryRigs())
            if (rig.name == "Crocell Metal")
            {
                auto m = processor.getModMatrix();
                m.mixerEffects = rig.modulation.mixerEffects;
                processor.setModMatrix (m);
                for (const auto& [id, value] : rig.parameters)
                    if (id.startsWith ("mix"))
                        if (auto* parameter = processor.getParameters().getParameter (id))
                            parameter->setValueNotifyingHost (dynamic_cast<juce::RangedAudioParameter*> (parameter)->convertTo0to1 (value));
            }
        if (const auto layer = env ("PARAGON_SNAPSHOT_LAYER"); layer.isNotEmpty())
        {
            processor.loadLayer (0, juce::File (layer));
            processor.waitForInstruments (60000);
        }
        view->showPage (MainView::Page::mixer);
        view->getMixerPage().routingChanged();
        view->getMixerPage().selectChannel (synth::mixer::snareTop);
        view->getMixerPage().showTab (MixerPage::Tab::effects);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (const auto note : { 36, 38, 48 })
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 115), 0);
        processor.processBlock (buffer, midi);
        for (int b = 0; b < 4; ++b)
        {
            juce::MidiBuffer none;
            processor.processBlock (buffer, none);
        }
        if (env ("PARAGON_SNAPSHOT_LAYER").isNotEmpty())
        {
            const auto channel = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drum (0, "LayerOut"))->load()) - 1;
            view->getMixerPage().selectChannel (channel);
            view->resized();
            view->getMixerPage().scrollToStrip (synth::mixer::stripOfChannel (channel));
        }
        view->getMixerPage().update();
    }
    else if (page == "mixer" && micSimShot > 0)
    {
        // 0.26.0: SIMULATED MICS: the Djent Kit (its modelled drums through them, a Live Room).
        for (const auto& rig : makeFactoryRigs())
            if (rig.name == "Djent Kit")
                for (const auto& [id, value] : rig.parameters)
                    if (auto* parameter = processor.getParameters().getParameter (id))
                        parameter->setValueNotifyingHost (dynamic_cast<juce::RangedAudioParameter*> (parameter)->convertTo0to1 (value));
        view->showPage (MainView::Page::mixer);
        auto& mixerPage = view->getMixerPage();
        mixerPage.selectChannel (synth::mixer::snareTop);
        mixerPage.routingChanged();
        if (micSimShot == 1)
            mixerPage.openMicSim (true);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 120), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 47, (juce::uint8) 100), 0);
        processor.processBlock (buffer, midi);
        for (int b = 0; b < 6; ++b)
        {
            juce::MidiBuffer none;
            processor.processBlock (buffer, none);
        }
        mixerPage.update();
    }
    else if (page == "mixer")
    {
        const auto kitFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotMultiMic");
        kitFolder.deleteRecursively();
        juce::WavAudioFormat wav;
        juce::String sfz;
        const std::pair<int, const char*> drums[] = { { 36, "kick" }, { 38, "snare" }, { 42, "hat" } };
        const char* mics[] = { "kick in", "kick out", "snare top", "snare bottom", "hihat", "OH", "room" };
        for (const auto& [note, name] : drums)
        {
            sfz << "<group> key=" << note << "\n";
            for (const auto* mic : mics)
            {
                synth::drums::SlotSettings drum;
                drum.model = note == 36 ? synth::drums::Model::kick : note == 38 ? synth::drums::Model::snare : synth::drums::Model::closedHat;
                // Close to its own mic: loud there; elsewhere a quieter bleed; the room and overheads in between.
                const juce::String m (mic);
                const auto own = (note == 36 && m.startsWith ("kick")) || (note == 38 && m.startsWith ("snare")) || (note == 42 && m == "hihat");
                const auto level = own ? 0.9f : (m == "OH" || m == "room") ? 0.45f : 0.12f;
                const auto hit = synth::drums::Kit::renderHit (drum, level, 48000.0, 0.6);
                const auto file = kitFolder.getChildFile (m.removeCharacters (" ")).getChildFile (juce::String (name) + ".wav");
                file.getParentDirectory().createDirectory();
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 1, 24, {}, 0));
                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
                sfz << "<region> sample=" << m.removeCharacters (" ") << "/" << name << ".wav group_label=" << m.removeCharacters (" ") << "\n";
            }
        }
        kitFolder.getChildFile ("kit.sfz").replaceWithText (sfz);
        processor.importSfzKit (kitFolder.getChildFile ("kit.sfz"), synth::drums::notemap::Layout::generalMidi);
        processor.waitForInstruments();
        auto matrix = processor.getModMatrix();
        matrix.bleed (synth::mixer::snareTop, 0).gainDb = -6.0f;   // less kick in the snare mic
        processor.setModMatrix (matrix);
        view->showPage (MainView::Page::mixer);
        view->getMixerPage().selectChannel (synth::mixer::snareTop);
        // 0.21.0: PARAGON_SNAPSHOT_MIXERBUS=1 the snare's mics into BUS 1, sends to BUS 2 and the DELAY, BUS 1 to the
        // REVERB, the snare top's SENDS shown; =2 scrolled to the buses, BUS 1 chosen.
        if (const auto bus = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MIXERBUS", {}).getIntValue(); bus > 0)
        {
            using namespace synth::mixer;
            const auto set = [&processor] (const juce::String& id, float value)
            {
                if (auto* parameter = processor.getParameters().getParameter (id))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            set (ParamIDs::reverbOn, 1.0f);
            set (ParamIDs::delayOn, 1.0f);
            processor.setMixerRoute (snareTop, 1);
            processor.setMixerRoute (snareBottom, 1);
            processor.setMixerRoute (numChannels + 1, firstFxRoute);
            set (ParamIDs::mixBus (0, "Level"), -3.0f);
            set (ParamIDs::mixBus (1, "Level"), -8.0f);
            set (ParamIDs::mixBus (1, "Pan"), 0.3f);
            auto withSends = processor.getModMatrix();
            Send toBus2;
            toBus2.target = 2;
            toBus2.mode = SendMode::preFader;
            toBus2.directDb = -4.0f;
            toBus2.bleedDb = -10.0f;
            toBus2.link = false;
            toBus2.bleedPan = -0.5f;
            withSends.send (snareTop, 0) = toBus2;
            Send toDelay;
            toDelay.target = delayTarget;
            toDelay.directDb = -12.0f;
            toDelay.bleedOn = false;
            withSends.send (snareTop, 1) = toDelay;
            Send toReverb;
            toReverb.target = reverbTarget;
            toReverb.directDb = -6.0f;
            withSends.send (numChannels, 0) = toReverb;
            withSends.send (room, 0) = toReverb;
            processor.setModMatrix (withSends);
            auto& mixerPage = view->getMixerPage();
            if (bus == 1)
            {
                mixerPage.showTab (MixerPage::Tab::sends);
            }
            else
            {
                mixerPage.selectStrip (numChannels);
                view->resized();
                mixerPage.scrollToStrip (numChannels - 5);
            }
        }
        // 0.22.0: PARAGON_SNAPSHOT_MIXERFX=1 a gate, an EQ, a compressor and a transient shaper on the snare top, the
        // compressor chosen in its EFFECTS tab; a drive and a chorus on BUS 1.
        const auto mixerFx = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MIXERFX", {}).getIntValue();
        if (mixerFx > 0)
        {
            using namespace synth::mixer;
            auto withFx = processor.getModMatrix();
            auto gateFx = mixerfx::defaultSlot (mixerfx::gate);
            gateFx.values[0] = -36.0f;
            gateFx.values[5] = 180.0f;
            withFx.effect (snareTop, 0) = gateFx;
            auto eqFx = mixerfx::defaultSlot (mixerfx::eq);
            eqFx.values[0] = 80.0f;
            eqFx.values[3] = 220.0f;
            eqFx.values[4] = 3.5f;
            eqFx.values[6] = 5200.0f;
            eqFx.values[7] = 2.5f;
            eqFx.values[10] = -2.0f;
            withFx.effect (snareTop, 1) = eqFx;
            auto compFx = mixerfx::defaultSlot (mixerfx::compressor);
            compFx.values[0] = -30.0f;
            compFx.values[1] = 5.0f;
            compFx.values[2] = 3.0f;
            compFx.values[3] = 120.0f;
            compFx.values[4] = 4.0f;
            compFx.values[5] = 2.0f;
            compFx.values[6] = 120.0f;
            compFx.values[7] = 70.0f;
            withFx.effect (snareTop, 2) = compFx;
            auto transientFx = mixerfx::defaultSlot (mixerfx::transient);
            transientFx.values[0] = 40.0f;
            transientFx.values[1] = -25.0f;
            transientFx.on = false;
            withFx.effect (snareTop, 3) = transientFx;
            if (mixerFx == 3)
            {
                // 0.35.0: =3 SATURATION (MARS) in the snare top's 4th slot, three bands, chosen.
                auto sat = mixerfx::defaultSlot (mixerfx::saturation);
                sat.values[0] = 2.0f;
                sat.values[1] = 180.0f;
                sat.values[2] = 4500.0f;
                sat.values[3] = 70.0f;
                const auto at = [] (int b, int what) { return (size_t) (mixerfx::saturationBandsAt + b * mixerfx::saturationPerBand + what); };
                sat.values[at (0, 0)] = 9.0f;  sat.values[at (0, 1)] = 45.0f; sat.values[at (0, 2)] = 30.0f;
                sat.values[at (1, 0)] = 6.0f;  sat.values[at (1, 1)] = 60.0f; sat.values[at (1, 3)] = 80.0f;
                sat.values[at (2, 0)] = 1.0f;  sat.values[at (2, 1)] = 25.0f; sat.values[at (2, 4)] = -3.0f;
                withFx.effect (snareTop, 3) = sat;
            }
            withFx.effect (numChannels, 0) = mixerfx::defaultSlot (mixerfx::drive);
            withFx.effect (numChannels, 1) = mixerfx::defaultSlot (mixerfx::mod);
            processor.setModMatrix (withFx);
            processor.setMixerRoute (snareTop, 1);
            auto& mixerPage = view->getMixerPage();
            mixerPage.selectStrip (snareTop);
            mixerPage.showTab (MixerPage::Tab::effects);
            mixerPage.chooseEffectSlot (mixerFx == 3 ? 3 : mixerFx == 2 ? 1 : 2);
        }
        // 0.23.0: PARAGON_SNAPSHOT_MIXERROUTE=1 ROUTE MICS open on the snare, its room mic on a channel of its own, the
        // hi-hat's overheads on another, its kick mic off; =2 the strips with those two user channels.
        const auto mixerRoute = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MIXERROUTE", {}).getIntValue();
        if (mixerRoute > 0)
        {
            using namespace synth::mixer;
            const auto snarePiece = synth::drums::pieceOfSlot (synth::drums::notemap::SD), hatPiece = synth::drums::pieceOfSlot (synth::drums::notemap::CH);
            processor.createUserChannel (snarePiece, room);
            processor.createUserChannel (hatPiece, overheads);
            processor.setMicRoute (snarePiece, kickIn, routeNone);
            auto& mixerPage = view->getMixerPage();
            mixerPage.routingChanged();
            if (mixerRoute == 1)
            {
                mixerPage.openRouter (true);
                mixerPage.chooseRouterPiece (snarePiece);
            }
            else
            {
                mixerPage.selectChannel (numChannels);
                view->resized();
                mixerPage.scrollToStrip (8);
            }
        }
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        processor.processBlock (buffer, midi);
        if (mixerFx > 0)
            for (int b = 0; b < 8; ++b)
            {
                juce::MidiBuffer none;
                processor.processBlock (buffer, none);
            }
        view->getMixerPage().update();
    }
    if (page == "presets")
        view->showPage (MainView::Page::presets);
    else if (page == "settings")
    {
        view->showPage (MainView::Page::settings);
        // 0.18.0, MIDI IN: =1 the snare's note with 3 CC zones and a few hits in the monitor; =2 the VELOCITY tab with a
        // curve; =3 the HI-HAT tab calibrating.
        if (const auto which = env ("PARAGON_SNAPSHOT_MIDIIN").getIntValue(); which > 0)
        {
            auto& settings = view->getSettingsPage();
            auto& panel = settings.getMidiInPanel();
            auto* map = processor.getParameters().getParameter (ParamIDs::drumsMap);
            map->setValueNotifyingHost (map->convertTo0to1 (1.0f));   // GROOVE LIBRARY
            panel.setNote (38);
            settings.getMapList().select (38);
            auto in = processor.getMidiIn();
            in.curve.points[0] = { 0.0f, 0.12f };
            in.curve.add ({ 0.4f, 0.62f });
            in.curve.soft = true;
            processor.setMidiIn (in);
            panel.setZoneCount (3);
            panel.setZoneSlot (0, 2);    // centre: SD
            panel.setZoneSlot (1, 4);    // edge: the rimshot
            panel.setZoneSlot (2, 1);    // rim: the sidestick
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 70), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (10, 16, 20), 1);
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 96), 2);
            midi.addEvent (juce::MidiMessage::controllerEvent (10, 16, 100), 3);
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 40), 4);
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 5);
            midi.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 72), 6);
            processor.processBlock (buffer, midi);
            panel.pollMidi();
            if (which == 2)
                panel.showTab (MidiInPanel::Tab::velocity);
            if (which == 3)
            {
                panel.showTab (MidiInPanel::Tab::hihat);
                panel.startCalibration();
                midi.clear();
                midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 12), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 93), 1);
                processor.processBlock (buffer, midi);
                panel.pollMidi();
            }
        }
    }

    // PARAGON_SNAPSHOT_PROGRESS=1 (0.40.0): the progress box over the page, a bounce 62% done, with CANCEL.
    if (env ("PARAGON_SNAPSHOT_PROGRESS") == "1")
        view->getProgressBox().show ({ "BOUNCING THE SONG TO AUDIO", [] { return 0.62; }, [] {} });
    const auto width = juce::jmax (960, juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", "1600").getIntValue());
    editor->setSize (width, width * 9 / 16);
    editor->resized();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

// PARAGON_SNAPSHOT=menu.png ./ParagonX1Tests "[snapshot-menu]": a drop-down
// menu as the plugin draws it (menus open in their own window, so the editor
// snapshot can't show one): the drum models, SNARE ticked, one highlighted.
TEST_CASE ("Render a drop-down menu to a PNG", "[.][snapshot-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    ParagonLookAndFeel look;
    const auto& items = synth::drums::modelNames;
    constexpr int scale = 2, width = 150, itemHeight = 26;
    juce::Image image (juce::Image::ARGB, width * scale, (itemHeight * items.size() + 8) * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    look.drawPopupMenuBackground (g, width, itemHeight * items.size() + 8);
    for (int i = 0; i < items.size(); ++i)
        look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, i == 5, items[i] == "Snare", false,
                                items[i], {}, nullptr, nullptr);

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
