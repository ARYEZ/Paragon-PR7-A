#pragma once

#include "ui/LayerPanel.h"

#include "PluginProcessor.h"
#include "dsp/synth/Drums.h"
#include "ui/ArticulationPanel.h"
#include "ui/DrumSeqPage.h"
#include "ui/KitView.h"
#include "ui/MultiPointEditor.h"
#include "ui/SynthControls.h"
#include "ui/VelocityCurveEditor.h"

// THE DRUMS page (stage 6a): the kit's 12 slots down the left (a row each: its key, its drum, a picture of its hit,
// MUTE and SOLO; it lights when it plays; click it to choose it and hear it), and the chosen drum's settings on the
// right, with a large picture of its hit. Its pictures come from rendering a copy of the hit (as DRIVE's page does),
// redrawn when a setting changes. Drop an audio file on a row or on the picture to load it into that slot.

// A drum's hit as a picture: the peak level per column, and how long it lasts (to -60 dB, at most 4 s).
struct HitPicture
{
    std::vector<float> peaks;
    double seconds = 0.0;

    static HitPicture make (const synth::drums::SlotSettings&, int columns);
};

class DrumRow final : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::SettableTooltipClient
{
public:
    DrumRow (ParagonX1AudioProcessor&, int slot);
    std::function<void (int slot, juce::ModifierKeys)> onSelect;

    void setSelected (bool);
    void setChosen (bool);              // one of several drums chosen together (SHIFT / CTRL), 6c part 2
    bool isChosen() const noexcept { return chosen; }
    void setPlayhead (float seconds);   // where its last hit is now (-1: none), drawn across the picture
    void setPicture (HitPicture);
    void setLit (float amount);        // 0 to 1: the hit's flash
    int getSlot() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    ParagonX1AudioProcessor& processor;
    int slot;
    bool selected = false, chosen = false;
    float lit = 0.0f, playhead = -1.0f;
    HitPicture picture;
    ParamControl mute, solo;
};

class DrumHitDisplay final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::SettableTooltipClient
{
public:
    explicit DrumHitDisplay (ParagonX1AudioProcessor&);
    void setSlot (int);
    void setPicture (HitPicture);
    const HitPicture& getPicture() const noexcept { return picture; }

    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // SAMPLE (0.21.1): the whole file, its hits found and numbered; click one to play just it, or drag START (S) and
    // END (E). For tests: the hits found (in samples), and choosing one as a click would.
    bool showsFile() const;
    const std::vector<int>& getHits();
    void pickHit (int index);
    void playWholeFile();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Handle { none, start, end };
    void updateFile();
    juce::Rectangle<float> waveArea() const;
    Handle handleAt (float x) const;
    void setShare (const char* what, float value, bool gesture);
    float share (const char* what) const;

    ParagonX1AudioProcessor& processor;
    int slot = 0;
    HitPicture picture;
    std::shared_ptr<const synth::drums::Sample> file;   // the file shown, its picture and hits
    std::vector<float> filePeaks;
    std::vector<int> hits;
    Handle dragging = Handle::none;
};

class DrumPage final : public juce::Component,
                       private juce::Timer,
                       private juce::AudioProcessorParameter::Listener
{
public:
    explicit DrumPage (ParagonX1AudioProcessor&);
    ~DrumPage() override;

    void select (int slot);
    int getSelected() const noexcept { return selected; }
    // Several drums at once (6c part 2): a click chooses one, SHIFT a run from the last chosen, CTRL adds or takes
    // one away. A change to the routing row (CHOKE, INSERT, sends, MONO, DELAY, LP, HP) from the page then goes to
    // every chosen drum.
    void clickRow (int slot, juce::ModifierKeys);
    std::vector<int> getChosen() const;
    DrumRow& getRow (int slot) noexcept { return *rows[(size_t) slot]; }
    DrumHitDisplay& getDisplay() noexcept { return display; }
    MultiPointEditor& getEnvelopeEditor() noexcept { return envelope; }   // THE SAMPLE EDITOR's envelope (6c)
    // The chosen drum's controls (for tests): whatever parameter each is pointed at now.
    std::vector<ParamControl*> getSlotControls();
    void refreshPictures();   // now, not on the next tick (tests and snapshots)
    void updateLights();      // the rows' hit lights, from the kit (the timer; tests and snapshots)

    // The right-hand panel: the chosen DRUM, the SEQUENCER (stage 6b), or the KIT VIEW (0.14.0: the page opens on it).
    // 0.38.0: THE DRUM MACHINE (machine): MainView puts its face over the whole window (`onViewChanged` tells it).
    enum class View { drum, sequencer, kit, machine };
    void showView (View);
    std::function<void (View)> onViewChanged;
    View getView() const noexcept { return view; }
    DrumSeqPanel& getSequencer() noexcept { return sequencer; }
    // 0.18.0: the DRUM view's strip shows the ENVELOPE (and the hit) or the chosen drum's VELOCITY curve and gate.
    void showVelocity (bool);
    bool isShowingVelocity() const noexcept { return velocityMode; }
    void showLayers (bool);   // 0.50.0: the LAYERS tab
    void showRenderImport (ParagonX1AudioProcessor::RenderImport&);   // 0.51.0: what IMPORT A RENDER did
    bool isShowingLayers() const noexcept { return layersMode; }
    LayerPanel& getLayerPanel() noexcept { return layerPanel; }
    VelocityCurveEditor& getVelocityEditor() noexcept { return velocityEditor; }
    // The chosen drums' curve changed (every chosen drum gets it; `final`: one undo step).
    void setVelocityCurve (const synth::drums::VelocityCurve&, bool final);
    KitView& getKitView() noexcept { return kitView; }
    ArticulationPanel& getArticulations() noexcept { return articulations; }   // over the KIT VIEW (0.17.0)
    void chooseSlots (const std::vector<int>& slots);   // several drums chosen at once (ALL in the articulations)

    void paint (juce::Graphics&) override;
    void resized() override;

    // Whether a file looks like audio the kit can load.
    static bool isAudioFile (const juce::String& path);

private:
    void timerCallback() override;
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override;
    void listenToRouting();
    void showKitMenu (bool sequences);
    void chooseSample();
    juce::String keyFor (int slot) const;   // what a slot's picture was made from
    void placeEnvelope();                   // over the part of the picture it shapes (a sample's START to END)
    juce::Rectangle<int> envelopeStrip;

    ParagonX1AudioProcessor& processor;
    int selected = 0;
    std::vector<std::unique_ptr<DrumRow>> rows;
    std::array<juce::String, synth::drums::numSlots> pictureKeys;
    std::vector<std::unique_ptr<ParamControl>> kitControls, slotControls, routingControls, velocityControls, instrumentControls,
                                                acousticControls;   // 0.24.0: POSITION, SHELL, VARIATION (the acoustic models)
    bool showingAcoustic = false;
    int labelledModel = -1;   // the model the shaping knobs' names are for
    bool showingInstrument = false;
    void showLoadMenu();
    void importSfzKit();
    void importDrumGizmoKit();
    void importSamplePack();   // 0.33.0   // 0.27.0
    void importRender();   // 0.51.0: IMPORT A RENDER
    void chooseLayer (int layer = -1);   // 0.29.0 (0.50.0: which of the three; -1: the first free)
    DrumHitDisplay display;
    MultiPointEditor envelope;
    juce::TextButton play { "PLAY" }, load { "LOAD SAMPLE..." }, clear { "CLEAR" }, wholeFile { "WHOLE FILE" };
    juce::TextButton previous { "<" }, next { ">" };                                    // the folder's other samples
    juce::TextButton kitsButton { "KITS" }, sequencesButton { "SEQUENCES" }, linkButton { "LINK" };
    std::vector<juce::AudioProcessorParameter*> listened;   // the chosen drum's routing, while several are chosen
    bool routingGesture = false, copying = false;
    int anchor = 0;                                         // where a SHIFT run starts
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> kitArea, drumArea;
    int tick = 0;
    View view = View::kit;
    DrumSeqPanel sequencer;
    KitView kitView;
    ArticulationPanel articulations;
    juce::TextButton envelopeTab { "ENVELOPE" }, velocityTab { "VELOCITY" }, softButton { "SOFT" }, gateButton { "GATE" },
                     resetCurve { "STRAIGHT" };
    VelocityCurveEditor velocityEditor;
    bool velocityMode = false;
    juce::Rectangle<int> velocityArea;
    // THE LAYERS (0.50.0): the third tab, in the strip and picture's place.
    juce::TextButton layersTab { "LAYERS" };
    LayerPanel layerPanel;
    bool layersMode = false;
    void updateDrumPanels();
    void placeArticulations();
    juce::TextButton kitTab { "KIT VIEW" }, drumTab { "DRUM" }, sequencerTab { "SEQUENCER" }, machineTab { "MACHINE" };
};
