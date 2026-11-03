#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/PresetButton.h"

// COMP's bands, drawn across the spectrum: each band its part of it, in
// the menus' red to pink to orange at its frequency. Faint: how far it can
// move the level (its RANGE); solid: how far it's moving it now; the white
// line: everything the bands are doing together.
//
//   - Double-click: a band there (an octave wide; reaching the end when
//     near either end of the spectrum). Double-click a band's dot: on or off.
//   - Drag a dot: sideways moves the band, up and down its RANGE (down:
//     turns down, up: brings up). Shift: finer.
//   - Drag either edge of a band: that edge.
//   - Mouse wheel over a band: its THRESHOLD. Right-click: its menu.
//     Delete: remove the selected band.
class CompDisplay final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CompDisplay (ParagonX1AudioProcessor&);

    int getSelected() const noexcept { return selected; }
    void select (int band);
    std::function<void (int)> onSelect;
    void refresh();

    float xForFrequency (double hz) const noexcept;
    double frequencyForX (float x) const noexcept;
    float yForDb (double db) const noexcept;
    double dbForY (float y) const noexcept;
    juce::Point<float> dotFor (const effects::multiband::Band&) const noexcept;

    // Its bands, through their parameters (each change a gesture).
    void setBand (int band, const char* what, float plainValue);
    int createBand (double lowHz, double highHz);
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
    enum class Grab { none, dot, low, high };

    void timerCallback() override { if (isShowing()) refresh(); }
    int dotAt (juce::Point<float>) const;
    std::pair<int, Grab> edgeAt (juce::Point<float>) const;
    int regionAt (juce::Point<float>) const;
    void showMenu (int band);
    juce::RangedAudioParameter& parameter (int band, const char* what) const;
    void beginDrag (int band, Grab);

    ParagonX1AudioProcessor& processor;
    effects::multiband::Settings shown;
    juce::Rectangle<float> plot;
    int selected = -1, hovered = -1, dragging = -1;
    Grab grab = Grab::none, hoverGrab = Grab::none;
    double startLow = 0.0, startHigh = 0.0, startRange = 0.0;
};

// COMP's own page (stomp 2), over the pedal and dock: ON, LOOK-AHEAD and
// MIX along the top; the bands' graph; the selected band's controls under
// it; and the full-band compressor (PR7-C's) in the side panel.
class CompPage final : public juce::Component,
                       private juce::Timer
{
public:
    explicit CompPage (ParagonX1AudioProcessor&);
    ~CompPage() override;

    std::function<void()> onClose;
    CompDisplay& getDisplay() noexcept { return display; }

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
    struct BandKnob
    {
        BandKnob (const juce::String& label, const char* what, const juce::String& tooltip);
        const char* what;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };
    struct BandChoice
    {
        BandChoice (const juce::String& label, const char* what, const juce::StringArray& items, const juce::String& tooltip);
        const char* what;
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    // 0.40.0: the timer works only while the page is shown; layout calls updateControls itself.
    void timerCallback() override { if (isShowing()) updateControls(); }
    void updateControls();
    void showBand (int band);
    void step (int direction);
    std::vector<juce::Component*> bandComponents();

    ParagonX1AudioProcessor& processor;
    CompDisplay display;

    juce::ToggleButton onButton { "ON" }, lookahead { "LOOK-AHEAD" }, wide { "FULL BAND" }, autoRelease { "AUTO" }, link { "LINK" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment, lookaheadAttachment, wideAttachment, autoAttachment, linkAttachment;
    Knob mix, threshold, ratio, attack, release, makeUp;
    juce::ComboBox character;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment characterAttachment;
    juce::TextButton back { "BACK TO RACK" };
    PresetButton preset;
    juce::Label presetLabel { {}, "PRESET" };

    // The selected band
    int band = -1;
    juce::ToggleButton bandOn { "ON" };
    juce::TextButton listen { "LISTEN" }, previous { "<" }, next { ">" }, remove { "DELETE" };
    BandChoice mode, stereo, bandCharacter, slope;
    BandKnob bandThreshold, range, bandRatio, knee, bandAttack, bandRelease, bandLink, out, look;
    std::unique_ptr<juce::ButtonParameterAttachment> bandOnAttachment;

    juce::Rectangle<int> topBar, sidePanel, controlsArea, meterArea;
};
