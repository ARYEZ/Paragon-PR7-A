#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Finish.h"
#include "ui/PaintCache.h"

// THE DRUM MACHINE (0.38.0): the DRUMS page's MACHINE view, a drum machine's face over the whole window under the
// header (the kit list, the keyboard and OUTPUT give way to it). Laid out like the early-80s sampled drum machines
// (a column per drum: TUNE, DECAY, a level fader and a pad; a tempo screen; real-time and step programming),
// reimagined in the PR7 face: PR7-K's pearl chassis (PedalBody), the floor unit's screen (LedDisplay), the pedal
// knobs and keys. Our own design; every control is one PR7-D already has (the drum's PITCH, DECAY, LEVEL, MUTE, SOLO;
// the DRUM SEQUENCER's patterns and SHUFFLE; OUTPUT), but for TEMPO (machineTempo: PLAY's own clock) and REC.
//
//   top:    badge | the screen (pattern, time, preset, tempo, swing) | TEMPO SWING, PLAY STOP REC TAP | MASTER + meter | VIEW
//   middle: 18 drums (a BANK: slots 1-18 or 19-36): TUNE, DECAY, LEVEL fader, pad, name    | BANK, MUTE / SOLO, PATTERN A-D, SEQUENCE
//   bottom: the chosen drum's 16 steps of the pattern (a page of it), < >
class MachineView final : public juce::Component,
                          private juce::Timer
{
public:
    explicit MachineView (ParagonX1AudioProcessor&);
    ~MachineView() override;

    static constexpr int numColumns = 18, numSteps = 16;
    static constexpr float refWidth = 1900.0f, refHeight = 990.0f;   // the layout's own units (scaled to fit)

    // The DRUMS page's other views (VIEW: KIT, DRUM, SEQ), the chosen drum (shared with the page), choosing one.
    enum class OtherView { kit, drum, sequencer };
    std::function<void (OtherView)> onView;
    std::function<int()> getChosen;
    std::function<void (int slot)> onChoose;

    void setBank (int bank);   // 0: drums 1-18, 1: 19-36
    int getBank() const noexcept { return bank; }
    // 0.39.0: the drum on each column is the project's pad order (right-click a pad or its name: pick another).
    int slotOfColumn (int column) const noexcept;
    void showPadMenu (int column);
    void assignPad (int column, int slot);   // swaps with the pad that had it
    static juce::String nameOf (int slot);   // the pad's label

    // The steps shown: a page of 16 of the pattern's steps (it follows the playhead while the beat runs).
    int getStepPage() const noexcept { return stepPage; }
    void setStepPage (int);
    int numStepPages() const;
    bool isStepOn (int step) const;   // the chosen drum on that step of the page (of the pattern shown)
    void toggleStep (int step);       // one undo step

    // The keys' actions (tests and the keys).
    void play();
    void stop();
    void toggleRecord();
    void tap (double nowMs);
    void choosePattern (int pattern);   // after COPY: copies the pattern there first
    void armCopy (bool);
    bool isCopyArmed() const noexcept { return copyArmed; }
    void clearChosen (bool wholePattern);   // the chosen drum's notes in the pattern (or all of them)
    void fillChosen();                      // the chosen drum on every step of the page
    // 0.43.0: FINISH: the machine's own finish (Finish::getMachine), from its menu.
    void showFinishMenu();
    void setFinish (Finish::Type, bool remember = true);

    juce::TextButton& getKey (const juce::String& name);
    juce::TextButton& getStepKey (int step) { return *stepKeys[(size_t) step]; }
    juce::Component& getPad (int column);
    juce::Slider& getTune (int column) { return *tunes[(size_t) column]; }
    juce::Slider& getDecay (int column) { return *decays[(size_t) column]; }
    juce::Component& getFader (int column);
    juce::String getScreenText() const;   // what the screen says (tests)

    void update();   // the timer's work, now (tests and snapshots)

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;

private:
    class Fader;
    class Pad;
    void timerCallback() override;
    juce::Rectangle<float> at (float x, float y, float w, float h) const;
    static float column (int c) noexcept { return 62.0f + (float) c * 86.0f; }
    static float stepX (int s) noexcept { return 44.0f + (float) s * 94.0f + (float) (s / 4) * 10.0f; }
    void paintFace (juce::Graphics&);
    void paintScreen (juce::Graphics&, juce::Rectangle<float>) const;
    void paintMeter (juce::Graphics&, juce::Rectangle<float>) const;
    void stamp (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float size, juce::Colour) const;
    void bindColumns();
    void refreshKeys();
    int shownPattern() const;
    int chosenSlot() const;
    double stepsPerBar() const;
    void setParameter (const juce::String& id, float value);
    void editPattern (const std::function<void (synth::drumseq::Pattern&)>&);

    ParagonX1AudioProcessor& processor;
    PaintCache faceCache { true };
    juce::Image grain;
    float scale = 1.0f;
    juce::Point<float> origin;
    int bank = 0, stepPage = 0;
    bool copyArmed = false;
    std::vector<double> taps;

    juce::Slider tempo, swing, master;
    std::array<std::unique_ptr<juce::Slider>, numColumns> tunes, decays;
    std::array<std::unique_ptr<Fader>, numColumns> faders;
    std::array<std::unique_ptr<Pad>, numColumns> pads;
    std::vector<std::unique_ptr<juce::TextButton>> keys;
    std::array<std::unique_ptr<juce::TextButton>, numSteps> stepKeys;
    // After the knobs, so they go first (an attachment lets go of its knob as it goes).
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> fixedAttachments, columnAttachments;

    // What's drawn, as last drawn (a change repaints its part).
    juce::String shownScreen;
    std::optional<Finish::Type> shownFinish;
    float meterLevel = 0.0f, shownMeter = -1.0f;
    int shownPlayStep = -2, shownChosen = -1;
    std::array<int, numColumns> shownSlots {};
    juce::Rectangle<int> screenArea, meterArea, stepLabels, namesArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachineView)
};
