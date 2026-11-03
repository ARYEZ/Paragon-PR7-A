#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "rigs/StompPresets.h"

// A stomp page's PRESET button (DOMINION's, COMP's): it shows the preset's
// name, with * once its settings have been changed, and opens the menu of
// the factory presets by category, yours, SAVE AS, delete and the folder.
class PresetButton final : public juce::TextButton,
                           private juce::Timer
{
public:
    // What the button works with (the processor's preset API for one stomp).
    struct Hooks
    {
        juce::String stompName;                                    // "DOMINION", "COMP": for the save dialog
        std::function<const std::vector<StompPreset>&()> factory;
        std::function<std::vector<StompPreset>()> user;
        std::function<juce::String()> currentName;
        std::function<bool()> modified;
        std::function<void (const StompPreset&)> load;
        std::function<bool (const juce::String& name)> saveAs;      // captures, saves and remembers it
        std::function<bool (const StompPreset&)> remove;            // deletes one of yours (and forgets it)
        std::function<juce::File()> folder;
    };

    explicit PresetButton (Hooks);

    void showMenu();
    void refresh();

private:
    void timerCallback() override { if (isShowing()) refresh(); }   // (0.40.0: only while shown)
    void askForName();

    Hooks hooks;
};
