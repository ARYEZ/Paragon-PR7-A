// THE LAYERS (PR7-D 0.50.0): the DRUM view's LAYERS tab. Up to three of your own sounds played with each of the chosen
// drum's hits (reinforcing a kit: renders of another kit's snare over this one's). A row per layer: what it holds
// (LOAD... a file, a folder of hits, or a folder of mic folders; REMOVE), its LEVEL and OUT (parameters), PITCH, OFFSET
// (ms: later, or earlier), INVERT (polarity), the velocities it plays on (VEL LOW / HIGH) and LENGTH (0: whole), and AUTO-ALIGN
// (OFFSET and POLARITY set so it lines up with the drum's own hit). Under them, the drum's hit and each layer's drawn
// over each other from the drum's start, as they'll play (their offsets and polarities in): lined up, they rise
// together.
#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

class LayerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit LayerPanel (ParagonX1AudioProcessor&);
    ~LayerPanel() override;

    void setSlot (int slot);
    int getSlot() const noexcept { return slot; }
    void paint (juce::Graphics&) override;
    void resized() override;

    // For tests and snapshots, as the buttons do (LOAD... opens a file chooser: `onLoad` asks MainView's page for it).
    std::function<void (int layer)> onLoad;
    void align (int layer);
    juce::String getStatus (int layer) const { return status[(size_t) juce::jlimit (0, 2, layer)]; }
    juce::Rectangle<int> getPictureArea() const noexcept { return picture; }
    void refresh();   // what's loaded and the knobs, from the processor

    enum class Knob { pitch, offset, velLo, velHi, length };
    void setKnob (int layer, Knob, double value);   // as dragging that knob and letting go

private:
    void timerCallback() override;
    void tweakChanged (int layer, bool final);
    void updatePicture();

    struct Column
    {
        juce::Label title, name;
        juce::TextButton load { "LOAD..." }, remove { "REMOVE" }, alignButton { "AUTO-ALIGN" }, invert { "INVERT" };
        std::unique_ptr<ParamControl> level, out;
        std::array<juce::Slider, 5> knobs;
        std::array<juce::Label, 5> knobLabels;
        juce::Rectangle<int> area;
    };

    ParagonX1AudioProcessor& processor;
    int slot = 0;
    std::array<Column, 3> columns;
    std::array<juce::String, 3> status;
    juce::Rectangle<int> picture;
    juce::String shownKey;
    // The picture: each sound's first 30 ms from the drum's start, as a peak line per pixel.
    struct Trace { std::vector<float> lo, hi; bool present = false; };
    std::array<Trace, 4> traces;   // the drum, then the layers
    juce::String pictureKey;
    bool updating = false;
};
