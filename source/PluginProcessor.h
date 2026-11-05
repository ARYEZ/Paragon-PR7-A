#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "LevelMeterSource.h"
#include "MixerEffects.h"
#include "Parameters.h"
#include "Rack.h"
#include "dsp/synth/Drums.h"
#include "dsp/synth/NoteMap.h"
#include "grooves/GrooveLibrary.h"
#include "song/Song.h"
#include "song/Creator.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"
#include "rigs/MidiLinks.h"

// Which preset is loaded, for the header and the Save button. (Presets are
// "rigs" in the code, as in the rest of the PR7 family, whose rig browser,
// files and undo this shares.)
struct CurrentRig
{
    juce::String name { "Init" };
    juce::File file;              // set for the user's own presets
    juce::String factoryName;     // set for factory presets
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

namespace song::playstyle { class AmountModel; }

// PR7-D: an instrument. MIDI in (from the DAW and the on-screen keyboard),
// the drum kit and its sequencer, the effects, the preset's level and OUTPUT, stereo out.
class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AudioProcessorParameter::Listener
{
public:
    ParagonX1AudioProcessor();
    ~ParagonX1AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two can't disagree.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the preset browser, so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Presets (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }

    // Points the library at another folder. For tests.
    void useRigFolder (const juce::File& folder);

    // The current sound as a preset (not saved anywhere yet).
    Rig captureRig() const;

    void loadRig (const RigLibrary::Entry& entry);

    // Saves over the current preset if it's one of the user's own and returns
    // true; returns false if it isn't (a factory preset or never saved), in
    // which case the caller should ask for a name and use saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);

    // Loads the next (+1) or previous (-1) preset in the library.
    void stepRig (int direction);

    // Rename, re-file or re-tag a user preset from the browser.
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);

    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;

    // True once anything in the preset has changed since it was loaded or saved.
    bool isRigModified() const noexcept { return rigModified.load(); }

    // Tells listeners (the editor) that the preset list or current preset changed.
    juce::ChangeBroadcaster rigBroadcaster;

    //==========================================================================
    // UNDO / REDO (message thread only)
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    // Records a finished edit now instead of on the next timer tick. For tests.
    void flushUndoStep() { recordCheckpointIfReady(); }

    //==========================================================================
    // The on-screen keyboard's keys, played into the kit with the DAW's MIDI.
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    // The kit's preset data that isn't parameters (message thread): the sequencer's patterns and the drums' drawn
    // envelopes (ModMatrix, PR7-K's name for where they lived). Changing it rebuilds what the audio thread plays.
    const ModMatrix& getModMatrix() const noexcept { return matrix; }
    void setModMatrix (const ModMatrix&);
    void modMatrixEditFinished() { requestCheckpoint(); }

    // MIDI LEARN (PR7-K's stage 4f): controllers linked to knobs. A knob asks to learn;
    // the next controller moved is linked to it (from the timer).
    const MidiLinks& getMidiLinks() const noexcept { return midiLinks; }
    void setMidiLinks (const MidiLinks&);
    void startMidiLearn (const juce::String& parameterId);
    void cancelMidiLearn() { learning = {}; }
    void finishMidiLearn();   // from the timer (and tests): links the knob learning to a controller moved since
    const juce::String& getMidiLearnTarget() const noexcept { return learning; }
    int getLastController() const noexcept { return lastController.load(); }          // -1: none yet
    float getLastControllerValue() const noexcept { return lastControllerValue.load(); }
    juce::File midiLinksDefaultFile() const;
    bool saveMidiLinksAsDefault() const;
    bool exportMidiLinks (const juce::File&) const;
    bool importMidiLinks (const juce::File&);
    void controllerForTest (int controller, float value);   // a controller, played through a short block
    // Tests: the random choices (the hits' noise, LO-FI's) the same on every run, for sample-for-sample comparisons.
    void seedRandomForTest (juce::int64 seed) noexcept
    {
        rack->loFi.seedRandom (seed);
        drumKit.seedRandom ((std::uint32_t) seed);
    }

    //==========================================================================
    // THE DRUMS (PR7-K's stage 6a): the 12 keys from ROOT play the kit (and, with the sequencer on KEY, the four under
    // it start its patterns).
    // Samples (message thread): a slot's loaded file, kept in the preset; loading one sets the slot to SAMPLE.
    bool loadDrumSample (int slot, const juce::File&);
    void setDrumSample (int slot, std::shared_ptr<const synth::drums::Sample>);
    void clearDrumSample (int slot);
    std::shared_ptr<const synth::drums::Sample> getDrumSample (int slot) const;

    // THE SAMPLE ENGINE (0.19.0, dsp/synth/Instrument.h): a slot's multi-sampled instrument (its layers and
    // round-robins), loaded on a background thread and swapped in on the message thread; presets, kits and projects
    // keep where it came from and load it again. Loading one sets the slot to SAMPLE (and drops its single sample).
    std::shared_ptr<const synth::drums::Instrument> getDrumInstrument (int slot) const;
    void setDrumInstrument (int slot, std::shared_ptr<const synth::drums::Instrument>);
    void loadInstrumentFolder (int slot, const juce::File& folder);
    void loadInstrumentSfz (int slot, const juce::File& sfz, int note = -1);   // -1: all of the file's regions
    // A whole SFZ kit: its notes onto the kit's drums through a layout (each drum takes the note that's its home in
    // that layout, else the note with the most regions). What went where, and the notes left over.
    // IMPORT A RENDER (0.51.0, dsp/synth/RenderImport.h): a drum bounced out of another drum program a mic at a time,
    // with the MIDI that played it, cut into velocity layers and round robins (written into the presets folder's
    // "Rendered Layers") and loaded as layers: each note onto the drum it plays in `layout` (no MIDI: onto `slot`),
    // into its first free layer (none free: its third, replaced). prepare runs on any thread (reads and writes files,
    // `progress` 0 to 1, false stops it); apply on the message thread.
    struct RenderImport
    {
        struct Item
        {
            int note = -1, slot = -1, layer = -1;
            int layers = 0, rounds = 0, hits = 0;
            std::vector<int> velocities;
            juce::File folder;
            juce::String problem;   // why it wasn't loaded (no drum for its note)
        };
        juce::String name;
        std::vector<Item> items;
        juce::StringArray mics, problems, leftOut;
        juce::File midi;
        double shiftMs = 0.0;
        bool stopped = false;
    };
    RenderImport prepareRenderImport (const juce::File& folder, const juce::File& midi, synth::drums::notemap::Layout, int slot,
                                      const std::function<bool (double)>& progress = {}) const;
    void applyRenderImport (RenderImport&);
    // Both on a background thread, `onDone` on the message thread; its progress for the progress box (-1: none running).
    void startRenderImport (const juce::File& folder, const juce::File& midi, synth::drums::notemap::Layout, int slot,
                            std::function<void (RenderImport&)> onDone);
    double getRenderImportProgress() const noexcept { return renderProgress.load(); }
    void cancelRenderImport() noexcept { renderCancel = true; }
    juce::File renderedLayersFolder() const;
    struct KitImport { std::vector<std::pair<int, int>> slotNotes; juce::Array<int> unused; };
    KitImport importSfzKit (const juce::File& sfz, synth::drums::notemap::Layout);
    // DRUMGIZMO KITS (0.27.0): a whole kit onto the slots (each its instrument(s), LIGHT or FULL), remembered by name
    // (where this computer keeps it: factory presets name kits as "{kit:<name>}/<file>"). Which slot took which
    // instruments, and those left out.
    struct DrumGizmoImport { juce::String kitName; std::vector<std::pair<int, juce::StringArray>> slots; juce::StringArray unused; };
    // How much of a kit: LIGHT (trimmed, in memory), FULL (all of it in memory), STREAMED (all of it, from disk; 0.28.0).
    enum class KitLoad { light, full, streamed };
    DrumGizmoImport importDrumGizmoKit (const juce::File& kitFile, KitLoad);
    // 0.33.0: A SAMPLE PACK (a folder of a multi-mic library's named WAVs: samplepack::scan) onto every drum it has,
    // streamed, its extra mics on USER channels (named, their levels set: our guesses). What went where, for a summary.
    struct SamplePackImport
    {
        juce::String name;
        std::vector<std::pair<int, int>> slots;   // slot, how many files
        juce::StringArray userChannels, unused;
    };
    SamplePackImport importSamplePack (const juce::File& folder);
    int getStreamUnderruns() const noexcept { return diskStreamer != nullptr ? diskStreamer->getUnderruns() : 0; }
    int getActiveStreams() const noexcept { return diskStreamer != nullptr ? diskStreamer->getActiveStreams() : 0; }
    bool waitForStreams (int timeoutMs = 5000) noexcept { return diskStreamer == nullptr || diskStreamer->waitUntilFilled (timeoutMs); }   // tests
    void rememberKit (const juce::String& name, const juce::File& folder);
    void loadInstrumentRef (int slot, const juce::String& ref);   // a slot's instrument by its reference (as presets keep it)
    juce::File knownKitFolder (const juce::String& name) const;
    bool isLoadingInstruments() const noexcept { return pendingLoads.load() > 0; }
    // 0.40.0: how far the latest load has got (0 to 1, by drums started), or -1 when none is loading.
    double getLoadProgress() const noexcept
    {
        if (pendingLoads.load() <= 0)
            return -1.0;
        return loadsTotal.load() > 0 ? juce::jlimit (0.0, 1.0, (double) loadsDone.load() / (double) loadsTotal.load()) : 0.0;
    }
    juce::String getInstrumentRef (int slot) const { return instrumentRefs[(size_t) juce::jlimit (0, ParamIDs::numInstruments - 1, slot)]; }
    // A DRUM'S LAYERS (0.29.0; three since 0.50.0): a sample, a folder of hits or a multi-mic folder of the user's played
    // with each of the drum's hits. Each is kept as one of the drum's instruments (index layerOf (slot, n): presets, kits
    // and projects keep its file, as an instrument's). Loading one onto a drum whose sound comes through the mixer gives
    // it a USER channel of its own (named "<PIECE> LAYER", "<PIECE> LAYER 2"...), and the first layer turns the drum's
    // own close mics down 10 dB so the layer leads (one undo step). A multi-mic layer's overheads and room go into
    // theirs; its close mics into its channel.
    static constexpr int layerOf (int slot, int n = 0) noexcept { return ParamIDs::numDrums * (1 + n) + slot; }
    static std::array<int, 1 + ParamIDs::numLayers> instrumentsOf (int slot) noexcept
    {
        std::array<int, 1 + ParamIDs::numLayers> all {};
        for (int i = 0; i < (int) all.size(); ++i)
            all[(size_t) i] = ParamIDs::numDrums * i + slot;
        return all;
    }
    void loadLayer (int slot, const juce::File& fileOrFolder, int n = 0);
    void clearLayer (int slot, int n = 0);
    int firstFreeLayer (int slot) const;   // the first layer with nothing in it (-1: all three used)
    juce::String getLayerRef (int slot, int n = 0) const { return getInstrumentRef (layerOf (juce::jlimit (0, ParamIDs::numDrums - 1, slot), juce::jlimit (0, ParamIDs::numLayers - 1, n))); }
    std::shared_ptr<const synth::drums::Instrument> getLayer (int slot, int n = 0) const { return getDrumInstrument (layerOf (juce::jlimit (0, ParamIDs::numDrums - 1, slot), juce::jlimit (0, ParamIDs::numLayers - 1, n))); }
    int createLayerChannel (int slot, int n = 0);   // the USER channel it took (-1: none free)
    // 0.50.0: a layer's PITCH, OFFSET, POLARITY, velocities and LENGTH (preset data; `final`: an undo step).
    const synth::drums::LayerTweak& getLayerTweak (int slot, int n) const { return matrix.layer (juce::jlimit (0, ParamIDs::numDrums - 1, slot), juce::jlimit (0, ParamIDs::numLayers - 1, n)); }
    void setLayerTweak (int slot, int n, const synth::drums::LayerTweak&, bool final = true);
    // AUTO-ALIGN (0.50.0): the layer's hit lined up with the drum's (its close mic's): the delay where the two match best
    // (their starts found, then compared sample by sample within 3 ms of that), and whether it's turned over there. Sets
    // the layer's OFFSET and POLARITY (one undo step). Compares the hits each plays at `velocity`.
    struct Alignment
    {
        bool found = false;
        double offsetMs = 0.0;     // what OFFSET was set to
        bool invert = false;       // and POLARITY
        double match = 0.0;        // how alike they are there (0 to 1)
        juce::String problem;      // why not, when not found
    };
    Alignment alignLayer (int slot, int n, int velocity = 100);
    // The hits AUTO-ALIGN compares (mono, at their own rate): the drum's (its close mic, or its sample / rendered hit)
    // and the layer's (its close mic), with their rates. For the LAYERS page's picture too.
    struct ReferenceHit
    {
        std::vector<float> samples;
        double sampleRate = 0.0;
    };
    ReferenceHit getDrumHitForAlign (int slot, int velocity = 100) const;
    ReferenceHit getLayerHitForAlign (int slot, int n, int velocity = 100) const;
    static Alignment findAlignment (const ReferenceHit& drum, const ReferenceHit& layer);
    juce::StringArray getInstrumentProblems() const;
    bool waitForInstruments (int timeoutMs = 60000);   // the loads finished and installed (tests; snapshots)
    // Which hit an instrument slot played last (layer, hit), for the page; -1, -1: none.
    std::pair<int, int> getLastHit (int slot) const noexcept { return drumKit.getLastChoice (slot); }
    // THE MIXER (0.20.0): a mic channel's peak since last asked (the page's meter).
    float takeMixerPeak (int channel) noexcept { return drumKit.takeChannelPeak (channel); }
    float takeMixerBusPeak (int bus) noexcept { return drumKit.takeBusPeak (bus); }   // 0.21.0
    // 0.22.0: a slot's effect's gain reduction (a compressor's or gate's meter), and the type of effect made for it.
    float getMixerEffectReduction (int strip, int slot) const noexcept { return mixerEffects.getReductionDb (strip, slot); }
    int getMixerEffectType (int strip, int slot) const noexcept { return mixerEffects.getLoadedType (strip, slot); }
    // THE MIXER's menu (0.21.0), each one undo step. STEREO REVERSE: every channel's, bus's and send's pan to the other
    // side. STEREO OUTPUTS: every channel and bus to MAIN (no buses). MULTICHANNEL OUTPUTS: the channels to outputs by
    // instrument (our own grouping: kick OUT 2, snare OUT 3, hi-hat OUT 4, toms OUT 5, cymbals OUT 6, overheads OUT 7,
    // room and ambience OUT 8, other OUT 9), the buses to MAIN.
    void mixerStereoReverse();
    // ROUTE INSTRUMENT MICROPHONES (0.23.0): a piece's mic (by the channel its name sorted it into) to a mic channel,
    // AUTO or NONE (mixer::routeAuto, routeNone); a new USER channel for it (its settings fresh, named after the piece
    // and mic, playing where the mic's own channel does; -1: all 8 in use); which user channels are in use (routed
    // to); a mic channel's name (USER ones' as named); renaming one. Each an undo step.
    int getMicRoute (int piece, int mic) const;
    void setMicRoute (int piece, int mic, int to);
    int createUserChannel (int piece, int mic);
    juce::Array<int> getUserChannelsInUse() const;
    juce::String getMixerChannelName (int channel) const;
    void renameUserChannel (int channel, const juce::String& name);
    // 0.39.0: a bus's name (its own, else BUS n), and renaming it (empty: BUS n again). One undo step.
    juce::String getMixerBusName (int bus) const;
    void renameBus (int bus, const juce::String& name);
    // THE DRUM MACHINE's pads (0.39.0): which drum each of its 36 places (2 banks x 18 columns) plays, all 36 drums in some
    // order (setting one swaps it with the place that had that drum). Kept in the project, not in presets.
    int getMachinePad (int place) const noexcept;
    void setMachinePad (int place, int slot);
    void resetMachinePads();
    void applyMixerOutputs (bool multichannel);
    static int multichannelOutputOf (int channel) noexcept;
    // A channel's or bus's (strip: the channels, then the buses) destination as its OUTPUT menu shows it: a route
    // (1-12: a bus or FX bus) or, at 0, the output; and setting it (an output: the route back to 0).
    int getMixerRoute (int strip) const;
    int getMixerOutput (int strip) const;
    void setMixerRoute (int strip, int route);
    void setMixerOutput (int strip, int output);
    // The kit's settings now (any thread; the samples as the audio thread plays them).
    synth::drums::KitSettings getKitSettings() const noexcept;
    // The page's preview: a hit in the next block.
    void previewDrum (int slot) noexcept;
    const synth::drums::Kit& getDrumKit() const noexcept { return drumKit; }
    // THE MIDI MONITOR (0.18.0): what came in since the last call (one reader: SETTINGS' MIDI IN panel).
    int popMidiEvents (synth::drums::Kit::MidiEvent* into, int max) noexcept { return drumKit.popMidiEvents (into, max); }

    // THE DRUM SEQUENCER (stage 6b). Its settings now (without the patterns, which the audio thread takes from the
    // compiled matrix). Reversed synth drums play a rendered copy of their hit, made here when a reversed note needs
    // one or the drum's sound changes (the timer; tests call it).
    synth::drumseq::Settings getDrumSeqSettings() const noexcept;
    void updateDrumReverses();   // and the drum synths' hit lengths for their envelopes (6c)
    // KITS AND SEQUENCES (6c part 2): files in the presets folder's "Drum Kits" (.pr7kit: every drum's settings,
    // samples and envelope, the kit's LEVEL and VELOCITY) and "Drum Sequences" (.pr7seq: the four patterns and the
    // sequencer's SPEED, TIMING, LENGTH, VEL AMOUNT). With LINK on, loading or saving one takes the other of the same
    // name too. Loading is one undo step.
    juce::File drumKitsFolder() const;
    juce::File drumSequencesFolder() const;
    std::unique_ptr<juce::XmlElement> captureDrumKit() const;
    std::unique_ptr<juce::XmlElement> captureDrumSequence() const;
    bool applyDrumKit (const juce::XmlElement&);
    bool applyDrumSequence (const juce::XmlElement&);
    bool saveDrumKit (const juce::String& name);
    bool saveDrumSequence (const juce::String& name);
    bool loadDrumKit (const juce::File&);
    bool loadDrumSequence (const juce::File&);
    juce::Array<juce::File> listDrumKits() const;
    juce::Array<juce::File> listDrumSequences() const;
    std::atomic<bool> drumLink { true };
    juce::String getDrumKitName() const { return drumKitName; }
    juce::String getDrumSequenceName() const { return drumSequenceName; }
    // < > (6c part 2): the next or previous audio file in the slot's sample's folder (by name), loaded into the slot.
    bool loadNeighbourSample (int slot, int direction);

    // THE NOTE MAP (0.2.0): the layout chosen (MAP, a global parameter), the notes it plays (any thread), and the
    // player's CUSTOM map (message thread; kept in the project, and in "Note Map.xml" in the presets folder as the default a
    // new PR7-D starts with). Editing a note under another layout copies that layout to CUSTOM first and chooses it.
    synth::drums::notemap::Layout getMapLayout() const noexcept;
    synth::drums::notemap::Map getNoteMap() const;
    const synth::drums::notemap::Map& getCustomMap() const noexcept { return customMap; }
    void setCustomMap (const synth::drums::notemap::Map&);
    void mapNote (int note, int slot);   // -1: nothing (switches to CUSTOM, as above)
    int homeNote (int slot) const;       // the note a slot is shown on and exported on, -1: none
    juce::File noteMapDefaultFile() const;
    bool saveNoteMapAsDefault() const;

    // MIDI IN / E-DRUMS (0.18.0): what's done to MIDI from the DAW or a controller (not grooves, the song or the
    // sequencer), besides the note map and the hi-hat pedal's steps: a VELOCITY CURVE for every incoming note, and CC
    // ZONES for positional pads. Kept in the project; with the map, channel and pedal steps, in MIDI IN presets
    // (".pr7midi" files in the presets folder's "MIDI In Presets").
    struct MidiIn
    {
        synth::drums::VelocityCurve curve;
        std::array<synth::drums::KitSettings::CcZones, synth::drums::KitSettings::maxZoneNotes> zones {};
        bool operator== (const MidiIn& o) const noexcept;
        std::unique_ptr<juce::XmlElement> toXml() const;   // "MidiIn"
        static MidiIn fromXml (const juce::XmlElement&);
        synth::drums::KitSettings::CcZones* zonesFor (int note);   // its zones, or a free entry made for it, or null
    };
    const MidiIn& getMidiIn() const noexcept { return midiIn; }
    void setMidiIn (const MidiIn&);
    juce::File midiInPresetsFolder() const;
    juce::Array<juce::File> listMidiInPresets() const;
    std::unique_ptr<juce::XmlElement> captureMidiInPreset() const;   // the map, channel, curve, zones, pedal steps
    bool applyMidiInPreset (const juce::XmlElement&);
    bool saveMidiInPreset (const juce::String& name);
    bool loadMidiInPreset (const juce::File&);
    // The built-in ones: 0 DEFAULT (the groove library's layout), 1 GENERAL MIDI; each with a straight curve, no zones,
    // any channel, the pedal's default steps.
    void applyBuiltInMidiInPreset (int which);
    juce::String getMidiInPresetName() const noexcept { return midiInPresetName; }

    // MIDI EXPORT: one pass of a pattern on the kit's notes (each drum's home note in the map), at the song's tempo;
    // click saves, drag makes a file.
    juce::MidiFile drumPatternToMidi (int pattern) const;
    bool exportDrumMidi (int pattern, const juce::File&) const;
    juce::File drumMidiDragFile (int pattern) const;
    // Hits sounding, as of the last block.
    int getActiveVoices() const noexcept { return activeVoices.load(); }

    // The DAW's tempo and time signature, as last seen by the audio thread (120, 4/4 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }
    int getHostNumerator() const noexcept { return hostNumerator.load(); }
    int getHostDenominator() const noexcept { return hostDenominator.load(); }

    //==========================================================================
    // THE GROOVE PLAYER (0.4.0): the player's groove folders, and the preview playing one through the kit.
    grooves::Library& getGrooveLibrary() noexcept { return *grooveFolders; }
    // 0.54.0: EDIT PLAY STYLE's AMOUNT learned from the library's grooves (song/AmountModel.h), learned again whenever
    // the list of grooves changes (on the next ask). Message thread.
    const song::playstyle::AmountModel& getAmountModel();
    // How a groove in linked folder `folder` is played: its notes through that folder's map.
    synth::drums::notemap::Map grooveLayout (int folder) const;
    // Plays it through the kit from its start, looping or once, at the preview's tempo and velocity (the library's
    // settings: call applyGroovePreview after changing them).
    // GROOVE PARTS (0.7.0): `parts` (grooves::DrumPart bits) the drums it plays: the rest of the groove left out.
    void playGroove (const grooves::Groove&, int folder, bool loop, unsigned parts = grooves::allParts);
    void stopGroove();
    void applyGroovePreview();
    // RIFF MATCH (0.32.0): a riff heard with the groove previews, in time: from the preview's start, looping its part
    // (`loopSeconds` from `startSeconds` of `audio`, mono, at `rate`). While it's heard the previews play at its tempo
    // (times the preview's factor). Message thread.
    void setRiff (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, double startSeconds, double loopSeconds, double bpm);
    void clearRiff();
    bool hasRiff() const noexcept { return riffPlayback != nullptr; }
    void setRiffListening (bool on) noexcept { riffListening = on; }
    bool isRiffListening() const noexcept { return riffListening.load(); }
    void setRiffLevel (float gain) noexcept { riffLevel = gain; }
    // Beats into the groove playing (its loop), or -1; and which file.
    double getGroovePosition() const noexcept { return groovePosition.load(); }
    juce::File getGrooveFile() const { return groovePreview != nullptr ? groovePreview->file : juce::File(); }
    // The tempo it plays at: the song's or its own, times the factor.
    double grooveTempo (double grooveBpm) const noexcept;
    // DRAG OUT: the groove as a MIDI file (in the temporary folder) for a DAW track or the desktop: velocity scaled, notes
    // moved for the factor, and (FOR THIS KIT) on this kit's notes in its map (as written, if the folder's map is it).
    juce::File grooveDragFile (const grooves::Groove&, int folder, unsigned parts = grooves::allParts) const;
    std::function<int (int)> grooveNoteFor (int folder, unsigned parts = grooves::allParts) const;
    // INTO THE SEQUENCER (0.5.0): the groove as drum sequencer pattern `pattern` (0-3), replacing it: each hit a note on
    // its drum's row at its exact place (off the grid too), with its velocity, the preview's factor and velocity scale;
    // LENGTH set to the groove's (at most maxBeats: hits after that are left out), the sequencer on, playing it.
    struct GrooveToPattern { int placed = 0, noDrum = 0, pastEnd = 0; int lengthBeats = 0; };
    GrooveToPattern grooveToPattern (const grooves::Groove&, int folder, int pattern);
    // (0.13.0) The same with the notes through `map` (a file from outside the linked folders: the kit's own map), and a
    // MIDI file dropped on the sequencer's grid as pattern `pattern` (nothing if it can't be read: placed 0).
    GrooveToPattern grooveToPattern (const grooves::Groove&, const synth::drums::notemap::Map& map, int pattern);
    GrooveToPattern grooveFileToPattern (const juce::File&, int pattern);
    // GROOVE PARTS (0.7.0): the groove's `parts` laid into pattern `pattern`: that pattern's notes on those parts'
    // drums replaced by the groove's (where played, with velocity, factor and velocity scale), the groove repeated to
    // fill the pattern's LENGTH; its other drums, LENGTH and the sequencer's settings left as they are. Undoable.
    GrooveToPattern layerParts (const grooves::Groove&, int folder, unsigned parts, int pattern);

    // TAP TO FIND (0.6.0): a loop of `loopBeats` at `bpm` with a metronome (on MAIN) while tapping; notes played (the
    // DAW's, the on-screen keyboard's) and the pads' taps recorded where they fall in the loop, by drum group.
    void startTapping (double bpm, double loopBeats, double beatsPerBar);
    void stopTapping();
    bool isTapping() const noexcept { return tapping.load(); }
    double getTapBeat() const noexcept { return tapPosition.load(); }   // where in the loop now (-1: not tapping)
    void tapPad (int lane) noexcept;   // a pad clicked: its drum plays and the tap is recorded (at the next block)
    struct Tap { int lane = 0; double beat = 0.0; };
    int popTaps (std::vector<Tap>& into);   // the taps recorded since the last call (message thread)
    static constexpr float metronomeLevel = 0.25f;   // the click's peak (about -12 dBFS): heard over the kit, not loud

    // THE SONG TRACK (0.8.0): grooves laid out as blocks, played with the DAW's song position (its ppq) while the DAW
    // plays, from PLAY's place while it's stopped. Kept in the project (not in presets); edits are undo steps.
    const song::Song& getSong() const noexcept { return *songData; }
    void setSong (song::Song);   // an edit: played from the next block, an undo step
    // EDIT PLAY STYLE (0.46.0): the song as it would be while a knob turns, heard at once but not yet an undo step (the
    // setSong when the knob is let go makes it one); and SOLO SELECTED: only these drums of the song heard (0: all).
    void previewSong (song::Song);
    void setSongSolo (song::SlotMask);
    song::SlotMask getSongSolo() const noexcept { return songSolo; }
    // 0.47.0, the GRID EDITOR's row SOLO / MUTE: only these drums of the song heard (none: every drum; an empty mask:
    // none at all).
    void setSongHeard (std::optional<song::SlotMask>);
    // A block from a groove in linked folder `folder` (its notes through that folder's map, at the preview's TEMPO
    // factor and VELOCITY), and that block put in the song at `beat` (-1: after the last block). Returns its index.
    song::Block songBlockFrom (const grooves::Groove&, int folder) const;
    int addToSong (const grooves::Groove&, int folder, double beat = -1.0);
    int addBlockToSong (song::Block, double beat = -1.0);   // any block (a MIDI file dropped on the strip)
    void playSong (double fromBeat = 0.0);   // with the DAW stopped: plays from there at the DAW's last tempo
    void stopSong();
    bool isSongPlaying() const noexcept { return songLocal.load(); }   // PLAY (with FOLLOW DAW off, also while the DAW plays)
    double getSongPosition() const noexcept { return songPosition.load(); }   // beats into the song (-1: not playing)
    bool isCountingIn() const noexcept { return songCountingIn.load(); }       // 0.45.0: PLAY's count-in bar sounding
    // EXPORT MIDI: the whole song on the kit's notes (each drum's home note in the map), at the DAW's tempo.
    juce::MidiFile songToMidi() const;
    juce::File songDragFile() const;
    // IMPORT MIDI FILE (0.11.0): a whole song's MIDI through the kit's note map, as blocks (one per marker in it, else one)
    // after the song there (from the next bar line; an empty song: the song); with its tempo changes and time
    // signatures if `withTempo`. Returns how many blocks it added (0: nothing the kit plays, or not a MIDI file).
    int importSongMidi (const juce::File&, bool withTempo);

    // RECORD (0.12.0): armed, the notes played (the DAW's, the on-screen keyboard's) while the song plays (with the DAW,
    // or PLAY) are written into it at their beats, through the kit's map: on top of the blocks there (a repeating block
    // written out first, so a note lands only where it was played), or in a "Recorded" block made bar by bar where there's
    // none. mergeRecordedNotes (the message thread, often) puts them in; disarming ends the take: one undo step.
    void setSongRecording (bool);
    bool isSongRecording() const noexcept { return songRecording.load(); }
    int mergeRecordedNotes();   // how many it put in

    // THE DRUM MACHINE (0.38.0): PLAY starts the DRUM SEQUENCER (on, following the song) from its first step on a clock of
    // its own at the machine's TEMPO while the DAW isn't playing (the DAW leads while it plays); STOP stops it. REC, while
    // it runs, writes the drums played (the DAW's notes, the on-screen keyboard's, the face's pads) into the pattern
    // playing, each on its nearest step (a drum already there keeps the louder hit); mergeMachineNotes (the message
    // thread, often) puts them in; disarming (or STOP) ends the take: one undo step.
    void setMachinePlaying (bool);
    bool isMachinePlaying() const noexcept { return machinePlaying.load(); }
    bool isDawPlaying() const noexcept { return dawPlaying.load(); }
    void setMachineRecording (bool);
    bool isMachineRecording() const noexcept { return machineRecording.load(); }
    int mergeMachineNotes();   // how many it put in

    // BOUNCE (0.12.0): the song rendered to WAV files, faster than real time, by a copy of the plugin made from the
    // project's state (this one plays on, untouched): from the start through the song's tempo changes (none: the DAW's
    // last tempo) to its end, then on until the kit and effects have died away (silence, or `tailSeconds`); the plugin's
    // own delay (the limiter's look-ahead) taken off, so bar 1 is at the file's start.
    struct BounceOptions
    {
        bool perOutput = false;     // a file per output used, as routed (else one file: every drum in it)
        int bits = 24;              // 16, 24, or 32 (float)
        float gainDb = 0.0f;
        double tailSeconds = 10.0;
        juce::String name = "PR7-D Song";
    };
    class SongBouncer
    {
    public:
        SongBouncer (ParagonX1AudioProcessor& from, BounceOptions, juce::File folder);   // the message thread
        ~SongBouncer();
        juce::Array<juce::File> run();   // any thread: the files written (none: an empty song, cancelled, or not written)
        std::atomic<float> progress { 0.0f };
        std::atomic<bool> cancel { false };
        juce::String error;

    private:
        std::unique_ptr<ParagonX1AudioProcessor> copy;
        BounceOptions options;
        juce::File folder;
        double sampleRate = 48000.0, bpm = 120.0;
    };
    juce::ChangeBroadcaster songBroadcaster;   // the song changed (an edit, undo, a project loaded)
    // THE SONG's CLIPBOARD (0.9.0): a block (or some of its drums) copied, from the song or from a groove (its ticked
    // GROOVE PARTS), to paste as a block or onto blocks' drums. Message thread; not saved.
    const std::optional<song::Block>& getSongClipboard() const noexcept { return songClipboard; }
    void setSongClipboard (std::optional<song::Block> b) { songClipboard = std::move (b); }
    void copyGrooveForSong (const grooves::Groove&, int folder, unsigned parts = grooves::allParts);
    // THE SONG CREATOR (0.10.0): the grooves around a source groove in columns by song part (from the linked folders'
    // index); a groove of them as a block; a whole song from a structure's sections, each its column's next groove (a
    // missing part played by the nearest kind, a missing fill by the groove before it playing on). The song made isn't
    // set: MAKE SONG / ADD TO END do that (an undo step).
    song::Columns songCreatorColumns (const juce::File& source) const;
    std::optional<song::Block> songBlockFor (const grooves::Entry&) const;
    song::Song songFromStructure (const std::vector<song::Section>&, const song::Columns&) const;
    // SONG STRUCTURES: ours, then the player's ("Song Structures.xml" in the presets folder). Saving a name that's
    // there replaces it.
    std::vector<song::Structure> getSongStructures() const;
    bool saveSongStructure (const juce::String& name, const std::vector<song::Section>&);
    bool renameSongStructure (const juce::String& from, const juce::String& to);
    bool removeSongStructure (const juce::String& name);
    juce::File songStructuresFile() const;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }

    //==========================================================================
    // THE RACK (PR7-K's stage 7a): PR7-E 0.18.1's four effects after the kit, with PR7-E's names for what its pages read.
    // ECHELON (the EQ): its settings as they are now (any thread), the audio going into and out of it (for the
    // analyser), and SOLO, which isn't a parameter: it's held while listening.
    eq::ParametricEq::Settings getEqSettings() const noexcept;
    const InputTap& getEqInputTap() const noexcept { return rack->eqInputTap; }
    const InputTap& getEqOutputTap() const noexcept { return rack->eqOutputTap; }
    void setEqSolo (int band) noexcept { rack->eqSoloBand = band; }
    int getEqSolo() const noexcept { return rack->eqSoloBand.load(); }
    double getEqAutoGainDb() const noexcept { return rack->parametricEq.getAutoGainDb(); }
    eq::ParametricEq::DynamicReading getEqDynamics (int band) const noexcept { return rack->parametricEq.getDynamics (band); }
    eq::match::Session& getEqMatch() noexcept { return rack->eqMatch; }

    // DOMINION: its settings, what it's cutting (for its page), and listening to one of its depth-curve bands (-1: none).
    dominion::Settings getDominionSettings() const noexcept;
    const dominion::Processor& getDominion() const noexcept { return rack->dominionProcessor; }
    void setDominionListen (int band) noexcept { rack->dominionListen = band; }
    int getDominionListen() const noexcept { return rack->dominionListen.load(); }
    // Its own presets (message thread): the one loaded last (and whether it's been changed since), loading one, and yours.
    DominionPresets& getDominionPresets() noexcept { return rack->dominionPresets; }
    void loadDominionPreset (const DominionPreset& preset) { DominionPresets::apply (preset, parameters); rack->dominionPreset = preset; }
    juce::String getDominionPresetName() const { return rack->dominionPreset.name; }
    bool isDominionPresetModified() { return ! DominionPresets::matches (rack->dominionPreset, parameters); }
    void rememberDominionPreset (const DominionPreset& preset) { rack->dominionPreset = preset; }   // after saving it

    // CONSTRICTOR's presets, as DOMINION's.
    CompPresets& getCompPresets() noexcept { return rack->compPresets; }
    void loadCompPreset (const CompPreset& preset) { CompPresets::apply (preset, parameters); rack->compPreset = preset; }
    juce::String getCompPresetName() const { return rack->compPreset.name; }
    bool isCompPresetModified() { return ! CompPresets::matches (rack->compPreset, parameters); }
    void rememberCompPreset (const CompPreset& preset) { rack->compPreset = preset; }

    // CONSTRICTOR's bands as set now (any thread), and each band now (the page's meters).
    effects::multiband::Settings getMultibandSettings() const noexcept;
    effects::multiband::Processor::Reading getMultibandReading (int band) const noexcept { return rack->multiband.getReading (band); }
    void setMultibandSolo (int band) noexcept { rack->multibandSolo = band; }
    // The delay CONSTRICTOR's LOOK-AHEAD and the LIMITER add, told to the DAW (the timer calls this; tests too).
    void updateLatency();

    // CONSTRICTOR's and the LIMITER's gain reduction on a channel in the last block (dB, 0 or more).
    float getCompressorReductionDb (int channel) const noexcept { return rack->compressor.getReductionDb (channel); }
    float getLimiterReductionDb (int channel) const noexcept { return rack->limiter.getReductionDb (channel); }
    effects::MasterLimiter& getLimiter() noexcept { return rack->limiter; }
    effects::MasterLimiter::Settings getLimiterSettings() const;

    // THE SENDS (stage 7b): DELAY's and REVERB's settings as they are now (as sends' returns: MIX is the level back).
    effects::Delay::Settings getDelaySettings() const noexcept;
    effects::Reverb::Settings getReverbSettings() const noexcept;
    // MOD (0) or MOD 2's (1) settings as they are now (SYNC worked out at the song's tempo), and LO-FI's (stage 7c).
    effects::Modulation::Settings getModSettings (int which) const noexcept;
    effects::LoFi::Settings getLoFiSettings() const noexcept;
    // An insert bus's (0 to 3: A to D) effects' settings as they are now (stage 7d), its sends, and its output's peak.
    effects::Drive::Settings getInsertDriveSettings (int bus) const noexcept;
    effects::Glitch::Settings getInsertGlitchSettings (int bus) const noexcept;
    effects::Modulation::Settings getInsertModSettings (int bus) const noexcept { return modSettingsFor (ParamIDs::insModIds (bus)); }
    effects::LoFi::Settings getInsertLoFiSettings (int bus) const noexcept;
    std::array<float, synth::numSends> getInsertSends (int bus) const noexcept;
    float getInsertPeakDb (int bus) const noexcept
    {
        const auto peak = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].peak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    float getInsertModSweepPhase (int bus) const noexcept { return rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].modulation.getSweepPhase(); }
    // Where MOD's (0) or MOD 2's (1) sweep is now, 0 to 1, and LO-FI's output peak in the last block (dB, -100 while off).
    float getModSweepPhase (int which) const noexcept { return rack->modulation[(size_t) juce::jlimit (0, 1, which)].getSweepPhase(); }
    float getLoFiPeakDb() const noexcept
    {
        const auto peak = rack->loFiPeak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    // Each return's peak in the last block, dB (-100: silent): DELAY 0, REVERB 1.
    float getReturnPeakDb (int send) const noexcept
    {
        const auto peak = rack->returnPeak[(size_t) juce::jlimit (0, synth::numSends - 1, send)].load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }

    // THE OUTPUTS (0.3.0): the latency of MAIN's chain, which OUT 2 to 16 are held back by (and the DAW is told).
    int mainLatencySamples() const noexcept;

private:
    static BusesProperties makeOutputBuses();
    // OUT 2 to 16 in the block being played (from a piece's first sample): null while the DAW has that output off.
    struct AuxOutputs
    {
        std::array<float*, synth::numOutputs - 1> left {}, right {};
        AuxOutputs from (int offset) const noexcept
        {
            auto moved = *this;
            for (size_t o = 0; o < left.size(); ++o)
                if (left[o] != nullptr)
                {
                    moved.left[o] += offset;
                    moved.right[o] += offset;
                }
            return moved;
        }
        // Where something set to `output` (0 MAIN) plays: its pair, or null for MAIN (also when that output is off).
        std::pair<float*, float*> at (int output) const noexcept
        {
            const auto o = output - 1;
            return o >= 0 && o < (int) left.size() && left[(size_t) o] != nullptr ? std::pair { left[(size_t) o], right[(size_t) o] }
                                                                                   : std::pair<float*, float*> { nullptr, nullptr };
        }
    };
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& drums, const synth::drums::KitSettings& kit,
                       int start, int numSamples, const AuxOutputs& aux);
    void delayAuxOutputs (const AuxOutputs& aux, int numSamples) noexcept;
    void processModFx (juce::dsp::AudioBlock<float> block);
    void processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady, const AuxOutputs& aux);
    effects::Modulation::Settings modSettingsFor (const ParamIDs::ModIds&) const noexcept;
    effects::LoFi::Settings loFiSettingsFor (int bus) const noexcept;   // -1: the whole sound's
    float param (const char* id) const;
    // 0.49.0: the parameters' values by ID (std::string keys, looked up by string_view: `TransparentHash`).
    struct TransparentHash
    {
        using is_transparent = void;
        size_t operator() (std::string_view v) const noexcept { return std::hash<std::string_view>{} (v); }
    };
    std::unordered_map<std::string, std::atomic<float>*, TransparentHash, std::equal_to<>> rawValues;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);
    // Undo snapshots: the sound and which preset it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change
        std::shared_ptr<const song::Song> song;   // THE SONG TRACK (not in presets, but its edits are undone too)

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && rig.drumSamples == other.rig.drumSamples
                && rig.modulation == other.rig.modulation && current == other.current
                && (song == other.song || (song != nullptr && other.song != nullptr && *song == *other.song));
        }
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();

    // Knob drags and button clicks start and end "gestures"; an edit is
    // finished (and worth an undo step) when its gesture ends.
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    juce::AudioProcessorValueTreeState parameters;

    // Each drum's parameters, in ParamIDs::drumSettings' order, read every block (everything else by ID).
    struct DrumParameters { std::atomic<float>* values[std::size (ParamIDs::drumSettings)] {}; };
    std::array<DrumParameters, ParamIDs::numDrums> drumParameters {};
    // THE MIXER's channels' parameters (0.20.0), in ParamIDs::mixSettings' order, read every block.
    std::array<std::array<std::atomic<float>*, std::size (ParamIDs::mixSettings)>, synth::mixer::numMicChannels> mixParameters {};
    std::array<std::array<std::atomic<float>*, std::size (ParamIDs::busSettings)>, synth::mixer::numBuses> busParameters {};   // 0.21.0
    std::atomic<float>* muteModeParameter = nullptr;
    // THE MIXER's EFFECTS (0.22.0): made as presets load or slots change, run by the kit (mutable: getKitSettings
    // hands the kit a pointer to it).
    mutable mixerfx::Host mixerEffects;
    void setParameterNow (const juce::String& id, float value);   // a whole gesture, as a click

    // THE DRUMS: the kit, the samples (the audio thread gets plain pointers;
    // replaced ones are kept a while), the page's preview hits, and the block's MIDI for the kit.
    synth::drums::Kit drumKit;
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> drumSamples;
    std::array<std::atomic<const synth::drums::Sample*>, ParamIDs::numDrums> samplesForAudio {};
    std::vector<std::pair<std::shared_ptr<const synth::drums::Sample>, double>> retiredSamples;
    // THE SAMPLE ENGINE (0.19.0).
    std::array<std::shared_ptr<const synth::drums::Instrument>, ParamIDs::numInstruments> drumInstruments;   // 0.29.0: then the layers
    std::array<std::atomic<const synth::drums::Instrument*>, ParamIDs::numInstruments> instrumentsForAudio {};
    std::vector<std::pair<std::shared_ptr<const synth::drums::Instrument>, double>> retiredInstruments;   // 0.28.0: kept until its longest hit is over
    // DISK STREAMING (0.28.0): made when the first streamed instrument arrives (its rings take memory).
    std::unique_ptr<synth::drums::Streamer> diskStreamer;
    std::atomic<synth::drums::Streamer*> streamerForAudio { nullptr };
    std::array<juce::String, ParamIDs::numInstruments> instrumentRefs;   // what each slot holds or is loading (then each layer)
    std::unique_ptr<juce::ThreadPool> instrumentPool;   // made on the first load (most instances never load one)
    mutable std::mutex loadedLock;
    std::vector<std::pair<int, std::shared_ptr<const synth::drums::Instrument>>> loadedInstruments;
    juce::StringArray loadProblems;
    std::atomic<int> pendingLoads { 0 }, loadGeneration { 0 }, loadsTotal { 0 }, loadsDone { 0 };
    // 0.50.0: the load each slot is waiting for: a newer load of the same slot replaces an older one, a load of other
    // slots leaves it be (loading layer 1 then layer 2 at once lost layer 1: every load stopped the one before).
    std::array<std::atomic<int>, ParamIDs::numInstruments> slotLoadGeneration {};
    std::vector<int> loadedGenerations;   // beside loadedInstruments
    std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
    std::atomic<double> renderProgress { -1.0 };   // 0.51.0: IMPORT A RENDER's
    std::atomic<bool> renderCancel { false };
    std::unique_ptr<juce::Thread> renderThread;
    void startLoading (std::vector<std::pair<int, juce::String>> refs);   // the slots' instruments from their refs
    void installLoadedInstruments();
    void forgetLoad (int slot);   // 0.50.0: a slot's instrument taken away (its ref emptied, any load of it dropped)
    void makeSlotPlaySample (int slot);   // SAMPLE, its shaping neutral (if it wasn't already SAMPLE)
    static juce::String refFor (const synth::drums::Instrument&);
    juce::File knownKitsFile() const;
    mutable std::mutex kitsLock;

    // THE GROOVE PLAYER: the preview, made on the message thread (its notes already drums), played by the audio
    // thread through a plain pointer; a replaced one kept a while (as samples are), in case a block is still on it.
    struct GroovePreview
    {
        struct Hit { double beat; int slot; float velocity; };
        std::vector<Hit> hits;   // in time order
        double lengthBeats = 4.0, bpm = 120.0, endBeat = 4.0;
        bool loop = true;
        juce::File file;
    };
    int collectGrooveHits (int numSamples) noexcept;   // audio thread: the preview's hits in the next numSamples
    struct RiffPlayback
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double rate = 48000.0, start = 0.0, loop = 1.0, bpm = 120.0;
    };
    std::shared_ptr<const RiffPlayback> riffPlayback;
    std::atomic<const RiffPlayback*> riffForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const RiffPlayback>, double>> retiredRiffs;
    std::atomic<bool> riffListening { true };
    std::atomic<float> riffLevel { 1.0f };
    // Audio thread: where the preview was at this block's start (beats since it began), and its beats a sample.
    double riffFrom = -1.0, riffPerSample = 0.0;
    bool riffFirstChunk = true;
    void runRiff (juce::AudioBuffer<float>&, int numSamples) noexcept;
    std::unique_ptr<grooves::Library> grooveFolders;
    std::unique_ptr<song::playstyle::AmountModel> amountModel;   // 0.54.0 (getAmountModel)
    int amountModelGeneration = -1;
    std::shared_ptr<const GroovePreview> groovePreview;
    std::atomic<const GroovePreview*> grooveForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const GroovePreview>, double>> retiredGrooves;
    const GroovePreview* grooveNow = nullptr;   // audio thread: the one it's playing, where, and whether it's done
    // Its place: beats at the last tempo change plus whole samples since, times the tempo (summing a beat-per-sample
    // block by block would drift a sample in a few seconds).
    double grooveBaseBeat = 0.0, groovePerSample = 0.0;
    juce::int64 grooveSamples = 0;
    bool grooveDone = false;
    std::atomic<double> groovePosition { -1.0 };
    std::atomic<bool> grooveSongTempo { true };
    std::atomic<double> grooveFactor { 1.0 };
    std::atomic<float> grooveVelocity { 1.0f };
    std::array<synth::drums::SlotHit, 512> grooveHits {};   // the preview's hits, then the song's (sorted by sample)

    // THE SONG TRACK: the song (message thread) and its hits for the audio thread (as the preview's, kept a while when
    // replaced); PLAY's clock (whole samples since PLAY, as the preview's).
    struct SongPlayback
    {
        std::vector<song::Hit> events;
        double endBeat = 0.0;
        song::Song timing;   // 0.11.0: only its tempo changes (PLAY's clock through them; none: the DAW's tempo)
        bool followDaw = true;   // 0.12.0: FOLLOW DAW
        // 0.44.0: the LOOP AREA (PLAY's clock only), THE CLICK, and the bar lines (for the click's accent).
        bool loopOn = false;
        double loopStart = 0.0, loopEnd = 0.0;
        song::Song::Click click = song::Song::Click::recording;
        bool countIn = true;
        std::vector<double> barLines;
        bool isBarLine (double beat) const noexcept;
        double barLengthAt (double beat) const noexcept;
    };
    void installSong (std::shared_ptr<const song::Song>);
    song::SlotMask songSolo = 0;
    std::optional<song::SlotMask> songHeard;
    int collectSongHits (int numSamples, double ppq, double bpm, synth::drums::SlotHit* into, int room) noexcept;
    std::shared_ptr<const song::Song> songData = std::make_shared<const song::Song>();
    std::optional<song::Block> songClipboard;
    // RECORD: the notes on their way from the audio thread; where the song is in the piece being played (for their beats).
    struct RecordedNote { double beat = 0.0; int slot = 0; float velocity = 1.0f; };
    juce::AbstractFifo recordFifo { 2048 };
    std::array<RecordedNote, 2048> recordBuffer {};
    std::atomic<bool> songRecording { false };
    struct SongChunk
    {
        bool active = false, timed = false;
        double from = 0.0, perSample = 0.0, samplesFrom = 0.0, bpm = 120.0;
        const SongPlayback* playback = nullptr;
        // 0.44.0: PLAY went round the LOOP AREA in this piece at sample `wrapAt`: from there on these.
        int wrapAt = 1 << 30;
        double from2 = 0.0, samplesFrom2 = 0.0;
        // 0.45.0: THE COUNT-IN in this piece: its first `startAt` samples are counting in (no song, no recording; its
        // clicks `countInSpb` samples apart, `countInFrom` samples into it at the piece's start, `countInBeats` of them).
        int startAt = 0, countInBeats = 0;
        double countInFrom = 0.0, countInSpb = 0.0;
    } songChunk;   // audio thread
    juce::int64 songCountInLeft = 0, songCountInDone = 0;   // audio thread: samples of count-in still to come, and done
    double songCountInSpb = 0.0;
    int songCountInBeats = 0;
    // THE CLICK (0.44.0): this block's clicks (their samples from the block's start, accented on a bar line), and the one
    // sounding (it carries on into the next block). Added to MAIN after everything, as TAP TO FIND's.
    struct SongClick { int sample = 0; bool accent = false; };
    std::array<SongClick, 64> songClicks {};
    int numSongClicks = 0, songClickAt = -1;
    bool songClickAccent = false;
    void noteSongClicks (int blockOffset, int numSamples, double bpm) noexcept;
    void runSongClick (juce::AudioBuffer<float>&, int numSamples) noexcept;
    void recordSongNotes (const juce::MidiBuffer&, int offset, int numSamples, const synth::drums::KitSettings&) noexcept;
    // THE DRUM MACHINE's clock and REC (audio thread but the atomics): the sequencer's song position and tempo this block
    // (the DAW's while it plays, else PLAY's own, else -1), and the notes on their way to the pattern.
    std::atomic<bool> machinePlaying { false }, machineRestart { false }, machineRecording { false }, dawPlaying { false };
    std::array<int, ParamIDs::numDrums> machinePads = [] { std::array<int, ParamIDs::numDrums> a {}; for (int i = 0; i < ParamIDs::numDrums; ++i) a[(size_t) i] = i; return a; }();
    double machineBeat = 0.0, seqPpq = -1.0, seqBpm = 120.0;
    struct MachineNote { double step = 0.0; int slot = 0; float velocity = 1.0f; int pattern = 0; };
    juce::AbstractFifo machineFifo { 1024 };
    std::array<MachineNote, 1024> machineBuffer {};
    void recordMachineNote (double ppq, double bpm, int slot, float velocity, const synth::drumseq::Settings&) noexcept;
    int takeBlock = -1;   // message thread: this take's "Recorded" block (it grows bar by bar)
    std::shared_ptr<const SongPlayback> songPlayback;
    std::atomic<const SongPlayback*> songForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const SongPlayback>, double>> retiredSongs;
    std::atomic<bool> songLocal { false }, songCountingIn { false };
    std::atomic<double> songLocalFrom { 0.0 }, songPosition { -1.0 };
    std::atomic<int> songLocalStarts { 0 };   // PLAY pressed again: the clock starts over
    int songLocalSeen = 0;
    juce::int64 songSamples = 0;   // audio thread: samples since PLAY (at songPerSample beats each, from songBase)
    double songBase = 0.0, songPerSample = 0.0;
    double songStartSeconds = 0.0;   // audio thread, with tempo changes: PLAY's start, in seconds from the song's start
    bool songTimed = false;          // ... and whether the clock goes by them

    // TAP TO FIND: the loop's clock (whole samples since it started, as the preview's), the metronome's click, the pads
    // waiting for the next block (a bit each), and what was tapped on its way to the page.
    void runTapping (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const synth::drums::KitSettings&) noexcept;
    std::atomic<bool> tapping { false };
    std::atomic<double> tapBpm { 120.0 }, tapLoopBeats { 8.0 }, tapBeatsPerBar { 4.0 }, tapPosition { -1.0 };
    std::atomic<int> tapsWaiting { 0 }, tapRestart { 0 };
    int tapRestartSeen = 0;
    juce::int64 tapSamples = 0;
    int clickAt = -1;          // samples into the click sounding (-1: none)
    bool clickAccent = false;
    juce::AbstractFifo tapFifo { 512 };
    std::array<Tap, 512> tapBuffer {};
    std::array<std::atomic<bool>, ParamIDs::numDrums> pendingPreview {};
    juce::MidiBuffer drumMidi;
    // Reversed synth drums' rendered hits (by slot), what each was made from, and the audio thread's pointers.
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> reverseHits;
    std::array<juce::String, ParamIDs::numDrums> reverseKeys;
    std::array<std::atomic<const synth::drums::Sample*>, ParamIDs::numDrums> reversesForAudio {};
    // A drum synth's hit length, which its drawn envelope spans (6c); what each was measured from.
    std::array<std::atomic<float>, ParamIDs::numDrums> hitLengths {};
    std::array<juce::String, ParamIDs::numDrums> hitLengthKeys;
    juce::String drumKitName, drumSequenceName;   // the last loaded or saved (shown on the page)
    bool saveDrumSequenceOnly (const juce::String& cleanName);

    synth::drums::notemap::Map customMap = synth::drums::notemap::grooveLibrary();
    std::array<std::atomic<int>, 128> customForAudio {};   // the audio thread's copy
    juce::MidiKeyboardState keyboardState;
    juce::MidiBuffer keyboardMidi;   // the DAW's MIDI plus the on-screen keyboard's, per block
    std::atomic<int> activeVoices { 0 };
    void retireOldData();            // lets go of replaced samples and patterns once no block can be using them

    // Smoothing stops zipper noise when OUTPUT or the preset's level moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::SmoothedValue<float> bypassAmount;   // 1: silent

    std::unique_ptr<Rack> rack = std::make_unique<Rack>();   // made first: the rig folder sets its folders
    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostNumerator { 4 }, hostDenominator { 4 };
    double hostPpq = -1.0;   // the song position at this block's start (-1: stopped); audio thread
    MidiLinks midiLinks;
    std::array<std::atomic<juce::RangedAudioParameter*>, 128> controllerTargets {};   // the audio thread's copy, by CC
    std::atomic<int> lastController { -1 }, controllerCount { 0 };
    std::atomic<float> lastControllerValue { 0.0f };
    juce::String learning;
    int learnFromCount = 0;

    // The patterns and envelopes, and the copy the audio thread plays (rebuilt from it on the message thread,
    // swapped in by pointer, the old kept 2 s).
    ModMatrix matrix;
    struct CompiledMatrix
    {
        synth::drumseq::Patterns drumPatterns {};
        std::array<synth::mod::Shape, synth::drums::numSlots> drumEnvelopes {};
        std::array<synth::drums::VelocityCurve, synth::drums::numSlots> velocityCurves {};   // 0.18.0
        std::array<synth::drums::LayerTweak, synth::drums::numSlots * synth::drums::numLayers> layers {};   // 0.50.0
        synth::mixer::BleedMatrix bleed {};                                                 // 0.20.0
        synth::mixer::SendTable sends {};                                                   // 0.21.0
        synth::mixer::FxTable effects {};                                                   // 0.22.0
        synth::mixer::MicRoutes micRoutes {};                                               // 0.23.0
    };
    std::shared_ptr<const CompiledMatrix> compiledMatrix;
    std::atomic<const CompiledMatrix*> matrixForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const CompiledMatrix>, double>> retiredMatrices;
    MidiIn midiIn;
    std::shared_ptr<const MidiIn> midiInShared;
    std::atomic<const MidiIn*> midiInForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const MidiIn>, double>> retiredMidiIns;
    juce::String midiInPresetName { "Default" };
    void compileMatrix();

    LevelMeterSource outputMeter;
    void processRack (juce::dsp::AudioBlock<float> block);   // the rack's effects before OUTPUT
    void processSends (juce::dsp::AudioBlock<float> block, const AuxOutputs& aux);  // DELAY and REVERB on their sends, their returns added in

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
