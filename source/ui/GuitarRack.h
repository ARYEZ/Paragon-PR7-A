#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// PR7-I 0.5.0: the GUITAR page's controls as one of the family's rack units across the top of the window (the owner:
// "use the rack images from the PR7-G line ... so they look in line with the rest of the family"), drawn with PR7-G's
// own parts: the pearl black chassis with the red light round it (PedalBody::paintUnit), the PARAGON PR7-I badge,
// stamped lettering and printed red lines, the orange LED screens, the big knobs with their values on small screens,
// the small keys with neon stripes, a neon status bar. Always pearl black (PR7-G's Obsidian), whatever the finish, as
// it sits on the dark guitar page.
//
// Two pages, as PR7-G's units have, chosen by the PAGE keys under the badge: PLAYING (TONE ... VARIATION) and
// FRETTING (the picking, fretting and performance menus, FORCE STRING, HAND POSITION, CAPO, HAND SPAN, HAND RESET,
// MONO). Left to right: the badge and the page keys; the screen (the articulation latched, the last note) over the
// KEY MAP and VARIATION keys; the page; OUTPUT (a meter screen, LEVEL, OUTPUT).
//
// Only the unit's face and its page keys: the controls on it (GuitarView's) are its children, laid out in the areas
// it gives, shown and hidden by GuitarView as the page changes (onPageChanged).
class GuitarRack final : public juce::Component
{
public:
    GuitarRack();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    struct Screen
    {
        juce::String caption, articulation, key, line1, line2;
        bool operator== (const Screen&) const = default;
    };
    void setScreen (const Screen&);
    const Screen& getScreen() const noexcept { return screen; }
    void setOn (bool);
    void setMeterDb (float);   // the output's level, for the meter screen (dB)

    enum class Page { playing, fretting };
    void setPage (Page);
    Page getPage() const noexcept { return page; }
    std::function<void()> onPageChanged;
    juce::TextButton& getPageKey (Page p) noexcept { return p == Page::playing ? playingKey : frettingKey; }

    // Where the controls go, in the unit's pixels.
    juce::Rectangle<int> knobCell (int index) const;            // PLAYING: the 8 knobs in a row
    juce::Rectangle<int> menuCell (int index) const;            // FRETTING: the 3 menus (their screens)
    juce::Rectangle<int> frettingKnobCell (int index) const;    // FRETTING: FORCE STRING, HAND POSITION
    juce::Rectangle<int> boxCell (int index) const;             // FRETTING: CAPO, HAND SPAN, HAND RESET, MONO (their screens)
    juce::Rectangle<int> keyCell (int index) const;             // 0 SPLIT, 1 FACTORY, 2 VARIATION
    juce::Rectangle<int> sideKnobCell (int index) const;        // 0 LEVEL, 1 OUTPUT
    juce::Rectangle<int> screenArea() const;
    juce::Rectangle<int> meterArea() const;

    static constexpr int numKnobs = 8;
    static inline const juce::StringArray menuNames { "PICKING MODE", "FRETTING MODE", "PERFORMANCE" },
                                          boxNames { "CAPO", "HAND SPAN", "HAND RESET", "MONO" };

private:
    juce::Rectangle<float> face() const;
    juce::Rectangle<float> content() const;
    juce::Rectangle<float> badgeColumn() const;
    juce::Rectangle<float> displayColumn() const;
    juce::Rectangle<float> pageArea() const;     // under the page's title
    juce::Rectangle<float> sideColumn() const;
    juce::Rectangle<int> pageKeyCell (int index) const;
    void paintScreen (juce::Graphics&);
    void paintMeter (juce::Graphics&);

    juce::Image satin;
    Screen screen;
    bool on = true;
    float meterDb = -100.0f;
    Page page = Page::playing;
    juce::TextButton playingKey, frettingKey;
};
