#include "DockPanels.h"
#include "NoteNames.h"

#include "Parameters.h"
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
NoteMapList::NoteMapList (ParagonX1AudioProcessor& p) : processor (p)
{
    for (int n = 0; n < 128; ++n)
    {
        auto row = std::make_unique<Row>();
        row->note.setText (juce::String (n).paddedLeft (' ', 3) + "   " + notenames::name (n), juce::dontSendNotification);
        row->note.setFont (juce::FontOptions (13.0f));
        row->note.setColour (juce::Label::textColourId, ParagonColours::textDim);
        row->box = std::make_unique<juce::ComboBox>();
        row->box->addItem ("-", 1);
        for (int k = 0; k < synth::drums::numSlots; ++k)
            row->box->addItem (juce::String (synth::drums::slotDefaults[(size_t) k].key) + "   " + synth::drums::articulationName (k), k + 2);
        // 0.17.0: the hi-hat at the openness the pedal (CC 4) is at.
        row->box->addItem ("HI-HAT (PEDAL)   by CC 4", synth::drums::notemap::PEDAL + 2);
        row->box->setTooltip ("The drum note " + juce::String (n) + " plays. Changing it makes the map CUSTOM: a copy of the layout "
                              "you're on, with your change.");
        row->box->onChange = [this, n]
        {
            const auto slot = rows[(size_t) n]->box->getSelectedId() - 2;
            if (slot != shown[(size_t) n])
            {
                processor.mapNote (n, slot);
                refresh();
            }
        };
        row->note.setInterceptsMouseClicks (false, false);
        row->source.setFont (juce::FontOptions (12.0f));
        row->source.setColour (juce::Label::textColourId, ParagonColours::textDim);
        row->source.setInterceptsMouseClicks (false, false);
        row->addAndMakeVisible (row->source);
        row->onClick = [this, n] { select (n); if (onSelect) onSelect (n); };
        row->chosen = n == selected;
        row->addAndMakeVisible (row->note);
        row->addAndMakeVisible (*row->box);
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    shown.fill (-2);
    refresh();
}

void NoteMapList::refresh()
{
    const auto map = processor.getNoteMap();
    const auto layout = (int) processor.getMapLayout();
    if (map == shown && notenames::middleCOctave.load() == shownOctave && layout == shownLayout)
        return;
    shown = map;
    if (layout != shownLayout)
    {
        // 0.58.0: on GROOVE LIBRARY each note's name in the map it follows ("Hats: Open 2"), beside the drum we play.
        shownLayout = layout;
        const auto library = layout == (int) synth::drums::notemap::Layout::grooveLibrary;
        for (size_t n = 0; n < rows.size(); ++n)
        {
            rows[n]->source.setText (library ? juce::String (synth::drums::notemap::grooveLibraryName ((int) n)) : juce::String(),
                                     juce::dontSendNotification);
            rows[n]->showsNames = library;
            rows[n]->resized();
        }
    }
    if (const auto octave = notenames::middleCOctave.load(); octave != shownOctave)
    {
        shownOctave = octave;   // 0.35.1: the names follow NOTE NAMES
        for (size_t n = 0; n < rows.size(); ++n)
            rows[n]->note.setText (juce::String ((int) n).paddedLeft (' ', 3) + "   " + notenames::name ((int) n), juce::dontSendNotification);
    }
    for (size_t n = 0; n < rows.size(); ++n)
    {
        rows[n]->box->setSelectedId (map[n] + 2, juce::dontSendNotification);
        rows[n]->mapped = map[n] >= 0;
        rows[n]->note.setColour (juce::Label::textColourId, map[n] >= 0 ? ParagonColours::text : ParagonColours::textDim);
        rows[n]->repaint();
    }
}

void NoteMapList::select (int note)
{
    note = juce::jlimit (0, 127, note);
    if (note == selected)
        return;
    rows[(size_t) selected]->chosen = false;
    rows[(size_t) selected]->repaint();
    selected = note;
    rows[(size_t) selected]->chosen = true;
    rows[(size_t) selected]->repaint();
}

void NoteMapList::resized()
{
    for (size_t n = 0; n < rows.size(); ++n)
        rows[n]->setBounds (0, (int) n * rowHeight, getWidth(), rowHeight - 2);
}

void NoteMapList::Row::paint (juce::Graphics& g)
{
    if (mapped || chosen)
    {
        g.setColour (ParagonColours::accent.withAlpha (chosen ? 0.22f : 0.08f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    }
    if (chosen)
    {
        g.setColour (ParagonColours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
}

void NoteMapList::Row::resized()
{
    auto area = getLocalBounds();
    // 0.58.0: with the layout's own names (GROOVE LIBRARY) the note column narrows and the name sits before the drum.
    if (showsNames)
    {
        note.setBounds (area.removeFromLeft (64));
        source.setBounds (area.removeFromLeft (122));
        box->setBounds (area.reduced (0, 1));
    }
    else
    {
        source.setBounds ({});
        note.setBounds (area.removeFromLeft (110));
        box->setBounds (area.removeFromLeft (220).reduced (0, 1));
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      mapChoice (p.getParameters(), ParamIDs::drumsMap, "MAP",
                 "Which notes play which drum. ROOT: the 36 keys from ROOT, in order. GROOVE LIBRARY: the layout popular drum "
                 "software's groove libraries are written in. GENERAL MIDI: the General MIDI drum map. CUSTOM: your own (change "
                 "any note in the list). Not part of a preset: it suits your controller and grooves."),
      channelChoice (p.getParameters(), ParamIDs::drumsMidiChannel, "CHANNEL",
                     "MIDI IN CHANNEL: which channel your DAW or controller plays the kit on (ANY: all of them). Not part of a preset."),
      namesChoice (p.getParameters(), ParamIDs::noteNames, "NOTE NAMES",
                   "How notes are named. C3: middle C (note 60) is C3, the kick (36) C1, as in Cubase and Yamaha. C4: middle C "
                   "is C4, the kick C2, as in Roland and most other software. Only the names change, never which note plays "
                   "what. Not part of a preset."),
      mapList (p),
      midiInPanel (p)
{
    addAndMakeVisible (mapChoice);
    addAndMakeVisible (channelChoice);
    addAndMakeVisible (namesChoice);
    // MIDI IN / E-DRUMS (0.18.0): the preset menu, and the panel beside the list.
    styleButton (presetButton, false);
    presetButton.setTooltip ("MIDI IN presets: the map, channel, incoming velocity curve, CC zones and hi-hat steps together, "
                             "for each controller or e-drum kit you use.");
    presetButton.onClick = [this] { showPresetMenu(); };
    presetButton.setButtonText ("PRESET: " + processor.getMidiInPresetName().toUpperCase());
    addAndMakeVisible (presetButton);
    addAndMakeVisible (midiInPanel);
    mapList.onSelect = [this] (int n) { midiInPanel.setNote (n); };
    midiInPanel.onChooseNote = [this] (int n) { chooseNote (n); };
    styleButton (saveMap, true);
    saveMap.setTooltip ("A new PR7-D (in any project) starts with this CUSTOM map");
    saveMap.onClick = [this] { processor.saveNoteMapAsDefault(); };
    addAndMakeVisible (saveMap);
    mapView.setViewedComponent (&mapList, false);
    mapView.setScrollBarsShown (true, false);
    addAndMakeVisible (mapView);
    mapList.setSize (400, 128 * NoteMapList::rowHeight);
    mapView.setViewPosition (0, 33 * NoteMapList::rowHeight);   // opens on the kick and snare (the busy part of every layout)
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
    saveDefault.setTooltip ("A new PR7-D (in any project) starts with these links");
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

void SettingsPage::chooseNote (int note)
{
    mapList.select (note);
    // Shown in the list: scrolled to it if it's out of sight.
    const auto y = note * NoteMapList::rowHeight;
    const auto view = mapView.getViewArea();
    if (y < view.getY() || y + NoteMapList::rowHeight > view.getBottom())
        mapView.setViewPosition (0, juce::jmax (0, y - view.getHeight() / 2));
}

void SettingsPage::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MIDI IN PRESETS");
    menu.addItem ("Default (Groove Library layout)", [this] { processor.applyBuiltInMidiInPreset (0); });
    menu.addItem ("General MIDI", [this] { processor.applyBuiltInMidiInPreset (1); });
    const auto files = processor.listMidiInPresets();
    if (! files.isEmpty())
    {
        juce::PopupMenu user;
        for (const auto& f : files)
            user.addItem (f.getFileNameWithoutExtension(), [this, f] { processor.loadMidiInPreset (f); });
        menu.addSubMenu ("Your presets", user);
    }
    menu.addSeparator();
    menu.addItem ("Save As...", [this]
    {
        auto* window = new juce::AlertWindow ("Save MIDI In preset", "A name for these settings (the map, channel, velocity curve, "
                                              "CC zones and hi-hat steps):", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", processor.getMidiInPresetName() == "Default" ? juce::String ("My Kit") : processor.getMidiInPresetName());
        window->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result == 1)
                processor.saveMidiInPreset (window->getTextEditorContents ("name"));
        }), true);
    });
    menu.addItem ("Open the presets' folder", [this]
    {
        processor.midiInPresetsFolder().createDirectory();
        processor.midiInPresetsFolder().startAsProcess();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton));
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
    {
        repaint (keysArea);   // THE KEYS names the notes ROOT gives
        mapList.refresh();
        presetButton.setButtonText ("PRESET: " + processor.getMidiInPresetName().toUpperCase() + "  " + juce::String (juce::CharPointer_UTF8 ("â¾")));
    }
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

    const auto root = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drumsRoot)->load());
    const auto note = [] (int n) { return notenames::name (n); };
    section (keysArea, "MIDI IN / E-DRUMS", "Which notes from your DAW or controller play which drum. ROOT: the 36 keys from ROOT (now "
                                                + note (root) + " to " + note (root + synth::drums::numSlots - 1)
                                                + "), and with the sequencer on KEY the four below start its patterns. Click a note's "
                                                  "number to edit it on the right. Grooves and the song aren't changed by any of this.");
    section (rigsArea, "PRESETS", "Your presets are files in this folder, so you can back them up or share them:\n"
                                  + processor.getRigLibrary().getFolder().getFullPathName());
    section (midiArea, "MIDI LINKS", shownLinks.isEmpty()
                 ? juce::String ("None yet. Right-click a knob and choose MIDI learn, then move a knob or fader on your controller.")
                 : juce::String ("Your controller's knobs and faders, and the knobs they turn. Kept with the project; "
                                 "SAVE AS DEFAULT for every new PR7-D."));
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-D v") + JucePlugin_VersionString
                 + ". The PR7 family's drum instrument: drum synths and your own samples, a drum sequencer and effects, "
                   "grown from PR7-K's drum section (our own design and code). "
                   "Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).\n"
                   "Loads DrumGizmo kits (drumgizmo.org) from your computer; none ship with it. The Crocell Metal preset uses the "
                   "CrocellKit (CC-BY 4.0): kit by Crocell, mics by JBOSound, sampled by Lars Muldjord. Credit them when you "
                   "release music made with it.");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    // THE KEYS down the left; PRESETS, MIDI LINKS and ABOUT down the right.
    keysArea = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    rigsArea = area.removeFromTop (170);
    area.removeFromTop (12);
    aboutArea = area.removeFromBottom (150);
    area.removeFromBottom (12);
    midiArea = area;
    {
        auto keys = keysArea.reduced (24, 18).withTrimmedTop (84);
        auto bar = keys.removeFromTop (ParamControl::knobHeight - 30);
        mapChoice.setBounds (bar.removeFromLeft (190));
        bar.removeFromLeft (10);
        channelChoice.setBounds (bar.removeFromLeft (90));
        bar.removeFromLeft (10);
        namesChoice.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (10);
        saveMap.setBounds (bar.removeFromLeft (150).withSizeKeepingCentre (150, 30).translated (0, 8));
        bar.removeFromLeft (10);
        presetButton.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 30).translated (0, 8));
        keys.removeFromTop (10);
        // The list on the left, MIDI IN's panel beside it.
        auto list = keys.removeFromLeft (juce::jmin (360, keys.getWidth() / 2));
        keys.removeFromLeft (14);
        midiInPanel.setBounds (keys);
        mapView.setBounds (list);
        mapList.setSize (juce::jmax (10, list.getWidth() - mapView.getScrollBarThickness()), 128 * NoteMapList::rowHeight);
    }
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
