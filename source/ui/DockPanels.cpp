#include "DockPanels.h"
#include "NoteNames.h"

#include "Parameters.h"
#include "guitar/Tuning.h"
#include "ui/ParagonLookAndFeel.h"

namespace DockStyle
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, area.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (ParagonColours::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}

namespace
{
    void styleButton (juce::TextButton& button, bool primary)
    {
        button.setColour (juce::TextButton::buttonColourId, primary ? ParagonColours::accentDim.withAlpha (0.6f) : juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, primary ? ParagonColours::accent : ParagonColours::text);
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      namesChoice (p.getParameters(), ParamIDs::noteNames, "NOTE NAMES",
                   "How notes are named. C3: middle C (note 60) is C3, the low A (33) A0, as in Cubase and Yamaha. C4: middle C "
                   "is C4, the low A A1, as in Roland and most other software. Only the names change, never which note plays "
                   "what. Not part of a preset.")
{
    addAndMakeVisible (namesChoice);
    styleButton (openFolder, false);
    openFolder.onClick = [this]
    {
        processor.getRigLibrary().getFolder().createDirectory();
        processor.getRigLibrary().getFolder().startAsProcess();
    };

    for (auto* component : std::initializer_list<juce::Component*> { &openFolder })
        addAndMakeVisible (component);
    processor.rigBroadcaster.addChangeListener (this);

    // MIDI LINKS.
    for (auto* button : { &saveDefault, &exportLinks, &importLinks, &clearLinks })
    {
        styleButton (*button, button == &saveDefault);
        addAndMakeVisible (*button);
    }
    saveDefault.setTooltip ("A new PR7-I (in any project) starts with these links");
    exportLinks.setTooltip ("Save these links to a file, to back them up or use on another computer");
    importLinks.setTooltip ("Load links from a file saved with EXPORT");
    clearLinks.setTooltip ("Forget every link (undo isn't kept for links: EXPORT first if you might want them back)");
    saveDefault.onClick = [this] { processor.saveMidiLinksAsDefault(); };
    clearLinks.onClick = [this] { processor.setMidiLinks ({}); rebuildLinks(); };
    exportLinks.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export MIDI links", processor.getRigLibrary().getFolder().getChildFile ("MIDI Links.xml"), "*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) processor.exportMidiLinks (c.getResult().withFileExtension ("xml")); });
    };
    importLinks.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI links", processor.getRigLibrary().getFolder(), "*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) { processor.importMidiLinks (c.getResult()); rebuildLinks(); } });
    };
    linkView.setViewedComponent (&linkList, false);
    linkView.setScrollBarsShown (true, false);
    addAndMakeVisible (linkView);
    rebuildLinks();
    startTimerHz (4);
}

void SettingsPage::LinkRow::resized()
{
    auto area = getLocalBounds();
    remove->setBounds (area.removeFromRight (28).reduced (2));
    text.setBounds (area);
}

void SettingsPage::visibilityChanged()
{
    if (isVisible() && ! (processor.getMidiLinks() == shownLinks))
        rebuildLinks();
}

void SettingsPage::timerCallback()
{
    if (! (processor.getMidiLinks() == shownLinks))
        rebuildLinks();
    if (isVisible())
        repaint (keysArea);   // THE KEYSWITCHES name their notes as NOTE NAMES says
}

void SettingsPage::rebuildLinks()
{
    shownLinks = processor.getMidiLinks();
    linkRows.clear();
    for (const auto& link : shownLinks.links)
    {
        auto row = std::make_unique<LinkRow>();
        const auto* parameter = processor.getParameters().getParameter (link.parameterId);
        row->text.setText ("CC " + juce::String (link.controller) + "   " + juce::String::fromUTF8 ("\xe2\x86\x92") + "   "
                           + (parameter != nullptr ? parameter->getName (60) : link.parameterId), juce::dontSendNotification);
        row->text.setColour (juce::Label::textColourId, ParagonColours::text);
        row->text.setFont (juce::FontOptions (13.0f));
        row->remove = std::make_unique<juce::TextButton> ("X");
        row->remove->setTooltip ("Remove this link");
        const auto id = link.parameterId;
        row->remove->onClick = [this, id]
        {
            auto links = processor.getMidiLinks();
            links.forget (id);
            processor.setMidiLinks (links);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SettingsPage> (this)] { if (safe != nullptr) safe->rebuildLinks(); });
        };
        row->addAndMakeVisible (row->text);
        row->addAndMakeVisible (*row->remove);
        linkList.addAndMakeVisible (*row);
        linkRows.push_back (std::move (row));
    }
    resized();
    repaint();
}

SettingsPage::~SettingsPage()
{
    stopTimer();
    processor.rigBroadcaster.removeChangeListener (this);
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto section = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& body)
    {
        DockStyle::drawPanel (g, area.toFloat());
        auto inner = area.reduced (24, 18);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (body, inner.removeFromTop (64), juce::Justification::topLeft, 4, 1.0f);
    };

    const auto note = [] (int n) { return notenames::name (n); };
    section (keysArea, "THE KEYSWITCHES", "Below the guitar's lowest note and above its highest, as your guitar instrument's "
                                          "defaults, so parts written for it play the same here:");
    {
        auto list = keysArea.reduced (24, 18).withTrimmedTop (84).withTrimmedBottom (ParamControl::knobHeight);
        const std::pair<juce::String, juce::String> keys[] {
            { note (0) + " to " + note (6), "Force string: " + note (0) + " the low A (string 7) ... " + note (6) + " the high E (string 1)" },
            { note (7), "Force string off" },
            { note (10), "Hand position: the fret is the key's velocity" },
            { note (11), "Hand position AUTO" },
            { note (12), "Sustain (the only articulation yet; palm mute comes next)" },
            { note (108) + " / " + note (109) + " / " + note (110), "Picking mode: up / down / alternate" },
            { note (guitar::lowestNote) + " to " + note (guitar::highestNote), "The notes: low A open to the high E's 24th fret" } };
        g.setFont (juce::FontOptions (13.5f));
        for (const auto& [k, what] : keys)
        {
            auto row = list.removeFromTop (26);
            g.setColour (ParagonColours::accent);
            g.drawText (k, row.removeFromLeft (150), juce::Justification::centredLeft, false);
            g.setColour (ParagonColours::text);
            g.drawFittedText (what, row, juce::Justification::centredLeft, 1, 0.8f);
        }
    }
    section (rigsArea, "PRESETS", "Your presets are files in this folder, so you can back them up or share them:\n"
                                  + processor.getRigLibrary().getFolder().getFullPathName());
    section (midiArea, "MIDI LINKS", shownLinks.isEmpty()
                 ? juce::String ("None yet. Right-click a knob and choose MIDI learn, then move a knob or fader on your controller.")
                 : juce::String ("Your controller's knobs and faders, and the knobs they turn. Kept with the project; "
                                 "SAVE AS DEFAULT for every new PR7-I."));
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-I v") + JucePlugin_VersionString
                 + ". The PR7 family's guitar instrument: a 7-string in drop A played from MIDI, from takes you render or record "
                   "and import (none ship with it). Our own design and code, inspired by how commercial guitar instruments play. "
                   "Put PR7-G after it for amps, cabinets and effects. Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    // THE KEYSWITCHES (and NOTE NAMES) down the left; PRESETS, MIDI LINKS and ABOUT down the right.
    keysArea = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    rigsArea = area.removeFromTop (170);
    area.removeFromTop (12);
    aboutArea = area.removeFromBottom (150);
    area.removeFromBottom (12);
    midiArea = area;
    namesChoice.setBounds (keysArea.reduced (24, 18).removeFromBottom (ParamControl::knobHeight - 20).withWidth (140));
    auto midi = midiArea.reduced (24, 18).withTrimmedTop (84);
    auto buttons = midi.removeFromBottom (30);
    for (auto* button : { &saveDefault, &exportLinks, &importLinks, &clearLinks })
    {
        button->setBounds (buttons.removeFromLeft (button == &saveDefault ? 150 : 100));
        buttons.removeFromLeft (8);
    }
    midi.removeFromBottom (8);
    linkView.setBounds (midi);
    linkView.setVisible (! linkRows.empty());   // (nothing to show: the text above says how to make one)
    linkList.setSize (juce::jmax (10, midi.getWidth() - linkView.getScrollBarThickness()), (int) linkRows.size() * 26);
    for (size_t i = 0; i < linkRows.size(); ++i)
        linkRows[i]->setBounds (0, (int) i * 26, linkList.getWidth(), 24);

    openFolder.setBounds (rigsArea.reduced (24, 18).withTrimmedTop (98).removeFromTop (32).withWidth (220));
}
