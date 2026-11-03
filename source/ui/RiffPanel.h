// RIFF MATCH (PR7-D 0.32.0): the panel a riff is set up in, over the groove list. Our own design: the riff's waveform
// with its bars (at its tempo) and the hits heard in it (the strong ones tall), the part used lit (click or drag to
// move its start), BARS, TEMPO (with x2, /2 and the tempo heard in it), LISTEN (the riff with every groove previewed,
// in time) and its LEVEL, FIND and CLOSE.
#pragma once

#include "PluginProcessor.h"
#include "grooves/Riff.h"

class RiffPanel final : public juce::Component
{
public:
    explicit RiffPanel (ParagonX1AudioProcessor&);

    void setRiff (grooves::Riff);
    void clear() { riff.reset(); peaks.clear(); repaint(); }
    bool hasRiff() const noexcept { return riff.has_value(); }
    const grooves::Riff& getRiff() const { return *riff; }

    // Changes to the part (also what the controls do).
    void setStartSeconds (double);
    void setBars (int);
    void setTempo (double bpm, grooves::Riff::TempoFrom from);

    std::function<void (const grooves::Riff&)> onFind;      // FIND: search with the part as it is
    std::function<void (const grooves::Riff&)> onChanged;   // the part, tempo or bars changed (heard at once)
    std::function<void()> onClose;

    juce::TextButton& getFindButton() noexcept { return find; }
    juce::Rectangle<int> getWaveArea() const noexcept { return waveArea; }
    double secondsAtX (int x) const;   // where a click on the waveform is

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void updateControls();
    void changed();
    double shownSeconds() const;   // how much of the file the waveform shows (the part and some after it)

    ParagonX1AudioProcessor& processor;
    std::optional<grooves::Riff> riff;
    std::vector<std::pair<float, float>> peaks;   // the waveform, min / max per point of the whole file
    juce::ComboBox bars;
    juce::Slider tempo { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton twice { "x2" }, half { "/2" }, heard, listen { "LISTEN" }, cancel { "CLOSE" }, find { "FIND" };
    juce::Label title, info, barsLabel { {}, "BARS" }, tempoLabel { {}, "TEMPO" }, levelLabel { {}, "LEVEL" }, hint;
    juce::Rectangle<int> waveArea;
};
