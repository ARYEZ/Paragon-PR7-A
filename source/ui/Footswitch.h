#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// One of the footswitches on the unit, laid out as on a DigiTech GSP21's
// foot controller: its number printed beside a status LED above a big pad,
// and its name printed underneath. A stomp's pad turns its block on or off;
// clicking its name selects the block so its knobs show (or opens its page).
//
// The bottom row uses the same switch for rigs and banks: give it onPress
// (what the pad does) and isLit (when its LED is on) instead of parameters.
//
// A footswitch can control several on/off parameters at once. If any of
// them is on, the LED is lit and pressing turns them all off; otherwise
// pressing turns them all on.
//
// With no parameters it's a stomp still to be built: it shows its name, its
// LED stays dark and pressing it does nothing.
class Footswitch final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    Footswitch (int number, const juce::String& name, juce::AudioProcessorValueTreeState& parameters,
                juce::StringArray onParameterIds);

    // Returns the text under the name, e.g. "Drive 5.0  Tone 5.0".
    std::function<juce::String()> describe;
    std::function<void()> onSelect;
    // For a switch that isn't a stomp (a rig, BANK): what the pad does, and when its LED is lit.
    std::function<void()> onPress;
    std::function<bool()> isLit;

    // What's printed: the number beside the LED, and the name under the pad.
    void setNumberText (const juce::String&);
    void setName (const juce::String&);
    const juce::String& getName() const noexcept { return name; }

    void setSelected (bool shouldBeSelected);

    // Re-reads the parameters now (also happens on a timer).
    void refresh();
    bool isAnyOn() const;
    bool isPlaceholder() const { return onParameterIds.isEmpty() && ! onPress; }
    void toggle();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { if (isShowing()) refresh(); }

    juce::String number, name;
    juce::AudioProcessorValueTreeState& parameters;
    juce::StringArray onParameterIds;
    bool selected = false, lastOn = false;
    juce::String summary;
    juce::Rectangle<int> ledArea, switchArea, nameArea;
};
