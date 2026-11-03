#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/PresetButton.h"

// DOMINION's graph: what it's cutting right now, across the spectrum (filled
// down from the line, per channel: left or mid, and right or side when they
// differ), and the depth curve over it (where it works harder or leaves
// alone), with a dot for each of its bands.
//
//   - Double-click: a band there (a bell at that height; a low cut at the
//     far left, a high cut at the far right).
//   - Drag a dot: frequency and depth. Shift: finer. Mouse wheel: Q (a
//     cut's or shelf's slope).
//   - Double-click a dot: switch it on or off. Right-click: its menu.
//     Delete: remove the selected band.
class DominionDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit DominionDisplay (ParagonX1AudioProcessor&);

    int getSelected() const noexcept { return selected; }
    void select (int band);
    std::function<void (int)> onSelect;
    void refresh();

    float xForFrequency (double hz) const noexcept;
    double frequencyForX (float x) const noexcept;
    float yForDb (double db) const noexcept;
    double dbForY (float y) const noexcept;
    juce::Point<float> dotFor (const dominion::Band&) const noexcept;

    // The red to pink to orange gradient (the menus' highlight) at a
    // frequency, as the graph spreads it: red in the lows, orange in the highs.
    static juce::Colour colourAt (double hz);

    // Its bands, through their parameters (each change a gesture).
    void setBand (int band, const char* what, float plainValue);
    int createBand (dominion::Shape, double frequency, double depthDb);
    void removeBand (int band);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override { if (isShowing()) refresh(); }
    int bandAt (juce::Point<float>) const;
    void showMenu (int band);
    juce::RangedAudioParameter& parameter (int band, const char* what) const;

    ParagonX1AudioProcessor& processor;
    dominion::Settings shown;
    juce::Rectangle<float> plot;
    int selected = -1, hovered = -1, dragging = -1;
    juce::Point<float> hoverPoint;
    double dragStartFrequency = 0.0, dragStartDepth = 0.0;
};

// DOMINION's own page (stomp 5), over the pedal and dock, as the EQ's is:
// the main controls along the top, the graph, the selected band's controls
// under it, and the side panel (stereo, tilts, scale, and DELTA, MIX, OUT).
class DominionPage final : public juce::Component,
                           private juce::Timer
{
public:
    explicit DominionPage (ParagonX1AudioProcessor&);
    ~DominionPage() override;

    std::function<void()> onClose;
    DominionDisplay& getDisplay() noexcept { return display; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        Knob (juce::AudioProcessorValueTreeState&, const char* id, const juce::String& label, const juce::String& tooltip);
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };
    struct Choice
    {
        Choice (juce::AudioProcessorValueTreeState&, const char* id, const juce::String& label, const juce::String& tooltip);
        juce::ComboBox box;
        juce::Label label;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
    };

    // 0.40.0: the timer works only while the page is shown; layout calls updateControls itself.
    void timerCallback() override { if (isShowing()) updateControls(); }
    void updateControls();
    void showBand (int band);
    void step (int direction);
    std::vector<juce::Component*> bandComponents();

    ParagonX1AudioProcessor& processor;
    DominionDisplay display;

    juce::ToggleButton onButton { "ON" }, delta { "DELTA" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment, deltaAttachment;
    Choice mode, stereo;
    Knob depth, detail, attack, release, link, focus, detailLow, detailHigh, attackLow, attackHigh, releaseLow, releaseHigh,
         maxCut, wetTrim, mix, out;
    juce::TextButton back { "BACK TO RACK" };
    PresetButton preset;
    juce::Label presetLabel { {}, "PRESET" };

    // The selected band
    int band = -1;
    juce::ToggleButton bandOn { "ON" };
    juce::TextButton listen { "LISTEN" }, previous { "<" }, next { ">" }, remove { "DELETE" };
    juce::ComboBox shape, slope;
    juce::Slider frequency { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 bandDepth { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 q { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 bandFocus { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label shapeLabel { {}, "SHAPE" }, slopeLabel { {}, "SLOPE" }, frequencyLabel { {}, "FREQ" }, depthLabel { {}, "DEPTH" },
                qLabel { {}, "Q" }, focusLabel { {}, "FOCUS" };
    std::unique_ptr<juce::ButtonParameterAttachment> bandOnAttachment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> shapeAttachment, slopeAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> frequencyAttachment, depthAttachment, qAttachment, focusAttachment;

    juce::Rectangle<int> topBar, sidePanel, controlsArea, stereoArea, tiltArea, scaleArea, outputArea;
};
