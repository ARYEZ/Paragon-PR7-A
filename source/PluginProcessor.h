#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "LevelMeterSource.h"
#include "Parameters.h"
#include "guitar/Guitar.h"
#include "guitar/PolyWindow.h"
#include "guitar/RenderImport.h"
#include "guitar/NoiseImport.h"
#include "rigs/MidiLinks.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"

#include <mutex>

// What the preset selector shows: the preset loaded (or saved) last.
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

// PARAGON PR7-I (0.2.0): the guitar instrument. MIDI in (the DAW's), the guitar (guitar::Guitar) playing the takes of the
// guitar folder (guitar::Library), then TONE, the preset's level and OUTPUT. No effects: the owner puts PR7-G after it.
// Presets, undo and MIDI learn are the PR7 family's.
class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AsyncUpdater,
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
    double getTailLengthSeconds() const override { return 0.5; }

    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // PRESETS ("rigs" in the code, as across the family).
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }
    void useRigFolder (const juce::File& folder);   // tests: presets (and the default guitar) somewhere else
    Rig captureRig() const;
    void loadRig (const RigLibrary::Entry& entry);
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);
    void stepRig (int direction);
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);
    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;
    bool isRigModified() const noexcept { return rigModified.load(); }
    juce::ChangeBroadcaster rigBroadcaster;

    // UNDO: every change to the sound, a whole knob turn or a preset loaded, is one step.
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();
    void flushUndoStep() { recordCheckpointIfReady(); }

    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    // MIDI LEARN: right-click a knob, move a controller.
    const MidiLinks& getMidiLinks() const noexcept { return midiLinks; }
    void setMidiLinks (const MidiLinks&);
    void startMidiLearn (const juce::String& parameterId);
    void cancelMidiLearn() { learning = {}; }
    void finishMidiLearn();   // from the timer (and tests): links the knob learning to a controller moved since
    const juce::String& getMidiLearnTarget() const noexcept { return learning; }
    int getLastController() const noexcept { return lastController.load(); }
    float getLastControllerValue() const noexcept { return lastControllerValue.load(); }
    juce::File midiLinksDefaultFile() const;
    bool saveMidiLinksAsDefault() const;
    bool exportMidiLinks (const juce::File&) const;
    bool importMidiLinks (const juce::File&);
    void controllerForTest (int controller, float value);

    //==========================================================================
    // THE GUITAR (0.2.0): its folder of takes (the project's; a new instance uses the folder chosen last), loaded in the
    // background, and what's playing.
    juce::File getGuitarFolder() const;
    void setGuitarFolder (const juce::File&);   // loads it
    void reloadGuitar();                         // the folder read again (after an import)
    std::shared_ptr<const guitar::Library> getLibrary() const;
    float getLoadProgress() const noexcept { return loadProgress.load(); }   // -1: not loading
    const guitar::Guitar& getGuitar() const noexcept { return guitar; }
    guitar::Settings getGuitarSettings() const noexcept;   // the page's settings (no library or streamer)

    // PERFORMANCE (0.4.0): a style sets the fretting controls together (as the reference's performance styles do):
    // Rhythm (natural fretting, hand AUTO, mono off, span auto), Lead (the same, mono), Chords (polyphonic fretting).
    // Not a parameter: the page shows the style the controls match, or Custom.
    enum class Performance { custom, rhythm, lead, chords };
    static inline const juce::StringArray performanceNames { "Custom", "Rhythm", "Lead", "Chords" };
    void applyPerformance (Performance);
    Performance currentPerformance() const;
    int getActiveVoices() const noexcept { return guitar.getActiveVoices(); }
    juce::ChangeBroadcaster guitarBroadcaster;   // the guitar loaded, an import finished
    bool waitUntilIdle (int timeoutMs);          // tests: until loading and importing are done
    void setLibraryForTest (std::shared_ptr<const guitar::Library> l) { installLibrary (std::move (l)); }   // tests, snapshots

    // THE RENDER LIST and IMPORT A RENDER (0.2.0): the MIDI files to render through the reference instrument, and its
    // renders cut into the guitar's folder (in the background; one at a time).
    juce::File renderListFile (guitar::Articulation, int string) const;   // written to the temporary folder (to drag)
    bool saveRenderList (guitar::Articulation, int string, const juce::File&) const;
    bool saveAllRenderLists (const juce::File& folder) const;
    void startImport (const juce::File& render, guitar::Articulation, int string);
    float getImportProgress() const noexcept { return importProgress.load(); }   // -1: not importing
    void cancelImport() noexcept { importCancel = true; }
    int getImportingString() const noexcept { return importingString.load(); }   // -1: none
    guitar::Articulation getImportingArticulation() const noexcept { return (guitar::Articulation) importingArticulation.load(); }
    juce::String getImportReport (guitar::Articulation, int string) const;   // the last import's for that list (this session)
    // 0.6.0: THE NOISE LISTS, by the RENDERS page's NOISES row (noises::rowList: 0-6 a string's releases, 7 power chords,
    // 8 unpitched, 9 fret noise). An import runs as the note imports do (one at a time: getImportingString() is
    // noiseImport while it does).
    static constexpr int noiseImport = 99;
    juce::File noiseListFile (int row) const;
    bool saveNoiseList (int row, const juce::File&) const;
    void startNoiseImport (const juce::File& render, int row);
    int getImportingNoise() const noexcept { return importingNoise.load(); }   // -1: none
    juce::String getNoiseReport (int row) const;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }
    void setParameterNow (const juce::String& id, float value);   // a whole gesture, as a click

private:
    juce::AudioProcessorValueTreeState parameters;
    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    std::atomic<bool> rigModified { false }, applyingRig { false };

    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change
        bool operator== (const Snapshot& other) const { return rig.parameters == other.rig.parameters && current == other.current; }
    };
    UndoHistory<Snapshot> history;
    std::atomic<bool> checkpointWanted { false };
    std::atomic<int> activeGestures { 0 };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();
    void applyRig (const Rig&);
    void markModified() noexcept;
    void parameterChanged (const juce::String&, float) override;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool gestureIsStarting) override;
    void timerCallback() override;
    void handleAsyncUpdate() override;   // the POLY WINDOW's latency told to the host (off the audio thread)

    MidiLinks midiLinks;
    std::array<std::atomic<juce::RangedAudioParameter*>, 128> controllerTargets {};
    std::atomic<int> lastController { -1 }, controllerCount { 0 };
    std::atomic<float> lastControllerValue { 0.0f };
    juce::String learning;
    int learnFromCount = 0;

    juce::MidiKeyboardState keyboardState;
    juce::MidiBuffer keyboardMidi;

    // The guitar and its takes: the library handed to the audio thread as a plain pointer; one replaced is kept two
    // seconds (a block may still be on it, and its streams are let go a block later).
    guitar::Guitar guitar;
    guitar::Streamer streamer;
    mutable std::mutex libraryLock;
    std::shared_ptr<const guitar::Library> library = std::make_shared<guitar::Library>();
    std::atomic<const guitar::Library*> libraryForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const guitar::Library>, double>> retiredLibraries;
    juce::File guitarFolder;
    void installLibrary (std::shared_ptr<const guitar::Library>);
    juce::File guitarSettingsFile() const;
    std::atomic<float> loadProgress { -1.0f }, importProgress { -1.0f };
    std::atomic<bool> loadCancel { false }, importCancel { false };
    std::atomic<int> importingString { -1 }, importingArticulation { 0 };
    std::array<std::array<juce::String, guitar::numStrings>, guitar::numArticulations> importReports;
    std::atomic<int> importingNoise { -1 };
    std::array<juce::String, guitar::noises::numRows> noiseReports;
    juce::ThreadPool jobs { juce::ThreadPoolOptions().withThreadName ("PR7-A guitar").withNumberOfThreads (1) };

    // The audio thread's parameters.
    std::atomic<float>* toneValue = nullptr;
    std::atomic<float>* fretModeValue = nullptr;
    std::atomic<float>* pickModeValue = nullptr;
    std::atomic<float>* forceStringValue = nullptr;
    std::atomic<float>* handValue = nullptr;
    std::atomic<float>* velToVolValue = nullptr;
    std::atomic<float>* releaseValue = nullptr;
    std::atomic<float>* transposeValue = nullptr;
    std::atomic<float>* variationValue = nullptr;
    std::atomic<float>* keyMapValue = nullptr;
    std::atomic<float>* patchLevelValue = nullptr;
    std::atomic<float>* outputValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* capoValue = nullptr;
    std::atomic<float>* handSpanValue = nullptr;
    std::atomic<float>* handResetValue = nullptr;
    std::atomic<float>* monoValue = nullptr;
    std::atomic<float>* polyWindowValue = nullptr;
    std::atomic<float>* biteValue = nullptr;
    std::atomic<float>* attackValue = nullptr;
    std::atomic<float>* releaseNoiseValue = nullptr, * scrapesValue = nullptr, * fretNoiseValue = nullptr;   // 0.6.0
    guitar::PolyWindow polyWindow;
    juce::MidiBuffer windowedMidi;
    std::atomic<int> windowSamples { 0 };
    int windowSamplesFor() const noexcept;
    // BITE: a cut in the low mids (400 Hz) and a lift up top (a 3 kHz shelf), both by BITE (RBJ biquads).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};
        float run (float x, int c) noexcept
        {
            const auto y = b0 * x + z1[(size_t) c];
            z1[(size_t) c] = b1 * x - a1 * y + z2[(size_t) c];
            z2[(size_t) c] = b2 * x - a2 * y;
            return y;
        }
    };
    Biquad biteMid, biteHigh;
    float biteSetFor = -1.0f;
    void updateBite (float amount) noexcept;
    juce::SmoothedValue<float> gain;
    std::array<float, 2> toneState {};
    std::vector<float> scratch;
    LevelMeterSource outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
