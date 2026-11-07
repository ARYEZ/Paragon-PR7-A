#include "RendersPage.h"

#include "ui/Fretboard.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    juce::String listName (guitar::Articulation a) { return a == guitar::Articulation::mute ? "palm mute" : "sustain"; }

    bool isAudio (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac");
    }
}

RendersPage::RendersPage (ParagonX1AudioProcessor& p)
    : processor (p),
      keyMap (p.getParameters(), ParamIDs::keyMap, "KEY MAP",
              "How the articulation keyswitches work. SPLIT (your own map): C-1 plays palm mute at velocities 1-59 and sustain "
              "at 60-119, each zone spread over all its degrees; C#-1 the same as power chords. FACTORY: C-1 sustain, C#-1 "
              "palm mute, D#-1 / E-1 power chords. Not part of a preset.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &guitarPanel, &renders })
        addAndMakeVisible (c);

    folderLabel.setColour (juce::Label::textColourId, ParagonColours::text);
    folderLabel.setFont (juce::FontOptions (14.0f));
    folderLabel.setMinimumHorizontalScale (0.6f);
    guitarStatus.setColour (juce::Label::textColourId, ParagonColours::textDim);
    guitarStatus.setFont (juce::FontOptions (13.0f));
    guitarStatus.setMinimumHorizontalScale (0.7f);
    chooseButton.setTooltip ("The folder of takes to play (imported renders go into it too)");
    chooseButton.onClick = [this] { chooseFolder(); };
    reloadButton.setTooltip ("Read the folder again (after copying takes into it)");
    reloadButton.onClick = [this] { processor.reloadGuitar(); };
    for (auto* c : std::initializer_list<juce::Component*> { &folderLabel, &guitarStatus, &chooseButton, &reloadButton, &keyMap })
        guitarPanel.addAndMakeVisible (c);

    renderHelp.setColour (juce::Label::textColourId, ParagonColours::textDim);
    renderHelp.setFont (juce::FontOptions (13.0f));
    renderHelp.setJustificationType (juce::Justification::topLeft);
    renders.addAndMakeVisible (renderHelp);
    for (auto* tab : { &sustainTab, &muteTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (310);
        tab->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim);
        tab->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        renders.addAndMakeVisible (*tab);
    }
    sustainTab.setToggleState (true, juce::dontSendNotification);
    sustainTab.onClick = [this] { showArticulation (guitar::Articulation::sustain); };
    muteTab.onClick = [this] { showArticulation (guitar::Articulation::mute); };
    saveAllButton.setTooltip ("Save all seven strings' MIDI files into a folder");
    saveAllButton.onClick = [this] { saveAllLists(); };
    renders.addAndMakeVisible (saveAllButton);
    for (int s = guitar::numStrings - 1; s >= 0; --s)   // high E first, as the fretboard
        rows.push_back (std::make_unique<StringRow> (processor, s, [this] { return shown; }));
    std::reverse (rows.begin(), rows.end());           // rows[string]
    for (int s = guitar::numStrings - 1; s >= 0; --s)
        renders.addAndMakeVisible (*rows[(size_t) s]);

    processor.guitarBroadcaster.addChangeListener (this);
    showArticulation (guitar::Articulation::sustain);
    refresh();
    startTimerHz (10);
}

RendersPage::~RendersPage()
{
    processor.guitarBroadcaster.removeChangeListener (this);
}

void RendersPage::refresh()
{
    const auto library = processor.getLibrary();
    const auto folder = processor.getGuitarFolder();
    folderLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
    folderLabel.setTooltip (folder.getFullPathName());
    juce::String status;
    if (processor.getLoadProgress() >= 0.0f)
        status = "Loading...";
    else if (library->isEmpty())
        status = "No takes yet: render the lists below and import them.";
    else
    {
        // How many strings have each articulation's takes (palm mutes: some of a list's takes may have been copies).
        const auto strings = [&library] (guitar::Articulation a)
        {
            auto n = 0;
            for (int s = 0; s < guitar::numStrings; ++s)
                n += library->count (a, s) > 0 ? 1 : 0;
            return n;
        };
        status = juce::String (library->total()) + " takes: sustain on " + juce::String (strings (guitar::Articulation::sustain))
               + " strings, palm mute on " + juce::String (strings (guitar::Articulation::mute)) + " (the rest played by the nearest takes)";
    }
    guitarStatus.setText (status, juce::dontSendNotification);
    for (auto& row : rows)
        row->refresh();
}

void RendersPage::timerCallback()
{
    // While loading or importing, the rows' and status's progress.
    if (processor.getLoadProgress() >= 0.0f || processor.getImportProgress() >= 0.0f)
        refresh();
}

void RendersPage::showArticulation (guitar::Articulation a)
{
    shown = a;
    (a == guitar::Articulation::mute ? muteTab : sustainTab).setToggleState (true, juce::dontSendNotification);
    const auto& spec = guitar::renders::spec (a);
    const auto minutes = juce::roundToInt ((guitar::renders::lengthSeconds (a, 0)) / 60.0);
    renderHelp.setText ("Render each string's " + listName (a) + " list through your guitar instrument as a DI (its FACTORY articulation "
                        "map, effects off, one guitar, no anti-repetition, ringing or release noises), then import the audio. Each "
                        "string: 25 frets x " + juce::String (spec.layers) + (a == guitar::Articulation::mute ? " degrees of muting" : " dynamics")
                        + " x " + juce::String (spec.roundRobins) + " takes of each stroke, about " + juce::String (minutes) + " minutes.",
                        juce::dontSendNotification);
    for (auto& row : rows)
        row->refresh();
}

void RendersPage::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("The guitar's folder of takes", processor.getGuitarFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& c)
                          {
                              if (const auto f = c.getResult(); f != juce::File())
                              {
                                  f.createDirectory();
                                  processor.setGuitarFolder (f);
                                  refresh();
                              }
                          });
}

void RendersPage::saveAllLists()
{
    chooser = std::make_unique<juce::FileChooser> ("A folder for the render lists",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& c)
                          {
                              if (const auto f = c.getResult(); f != juce::File())
                                  processor.saveAllRenderLists (f);
                          });
}

void RendersPage::paint (juce::Graphics&) {}

void RendersPage::resized()
{
    auto area = getLocalBounds();
    guitarPanel.setBounds (area.removeFromLeft (420));
    area.removeFromLeft (8);
    renders.setBounds (area);
    {
        auto c = guitarPanel.getContentArea();
        folderLabel.setBounds (c.removeFromTop (24));
        guitarStatus.setBounds (c.removeFromTop (40));
        c.removeFromTop (8);
        auto buttons = c.removeFromTop (30);
        chooseButton.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (8);
        reloadButton.setBounds (buttons.removeFromLeft (90));
        c.removeFromTop (14);
        keyMap.setBounds (c.removeFromTop (ParamControl::knobHeight - 30).withWidth (240));
    }
    {
        auto c = renders.getContentArea();
        auto tabs = c.removeFromTop (30);
        sustainTab.setBounds (tabs.removeFromLeft (120));
        tabs.removeFromLeft (6);
        muteTab.setBounds (tabs.removeFromLeft (120));
        c.removeFromTop (6);
        auto top = c.removeFromTop (54);
        saveAllButton.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
        top.removeFromRight (12);
        renderHelp.setBounds (top);
        c.removeFromTop (6);
        const auto rowHeight = juce::jmin (58, c.getHeight() / guitar::numStrings);
        for (int s = guitar::numStrings - 1; s >= 0; --s)
            rows[(size_t) s]->setBounds (c.removeFromTop (rowHeight).reduced (0, 3));
    }
}

//==============================================================================
RendersPage::StringRow::StringRow (ParagonX1AudioProcessor& p, int s, std::function<guitar::Articulation()> a)
    : processor (p), string (s), articulation (std::move (a))
{
    name.setText (guitar::stringLabel (string), juce::dontSendNotification);
    name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, Fretboard::stringColour (string));
    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setMinimumHorizontalScale (0.6f);
    midiButton.setTooltip ("This string's render list: click to save the MIDI file, or drag it onto a track in your DAW");
    midiButton.onClick = [this]
    {
        const auto a = articulation();
        const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile (guitar::renders::fileName (a, string) + ".mid");
        chooser = std::make_unique<juce::FileChooser> ("Save the render list", file, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, a] (const juce::FileChooser& c)
                              {
                                  if (const auto f = c.getResult(); f != juce::File())
                                      processor.saveRenderList (a, string, f.withFileExtension ("mid"));
                              });
    };
    midiButton.addMouseListener (this, false);
    importButton.setTooltip ("Import this string's rendered audio (or drop the file on this row)");
    importButton.onClick = [this] { importFile(); };
    for (auto* c : std::initializer_list<juce::Component*> { &name, &status, &midiButton, &importButton })
        addAndMakeVisible (c);
}

void RendersPage::StringRow::refresh()
{
    juce::String text;
    const auto a = articulation();
    if (processor.getImportingString() == string && processor.getImportingArticulation() == a)
        text = "Importing... " + juce::String (juce::roundToInt (processor.getImportProgress() * 100.0f)) + "%";
    else if (const auto report = processor.getImportReport (a, string); report.isNotEmpty())
        text = report;
    else
    {
        const auto n = processor.getLibrary()->count (a, string);
        text = n == 0 ? juce::String ("Not imported") : juce::String (n) + " of " + juce::String (guitar::Library::takesPerString (a)) + " takes";
    }
    if (status.getText() != text)
    {
        status.setText (text, juce::dontSendNotification);
        status.setTooltip (text);
    }
    importButton.setEnabled (processor.getImportingString() < 0);
}

void RendersPage::StringRow::importFile()
{
    const auto a = articulation();
    chooser = std::make_unique<juce::FileChooser> ("The render of " + guitar::renders::fileName (a, string),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, a] (const juce::FileChooser& c)
                          {
                              if (const auto f = c.getResult(); f.existsAsFile())
                                  processor.startImport (f, a, string);
                          });
}

void RendersPage::StringRow::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != &midiButton || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ processor.renderListFile (articulation(), string).getFullPathName() }, false, this);
}

bool RendersPage::StringRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isAudio (files[0]);
}

void RendersPage::StringRow::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    if (files.size() == 1 && isAudio (files[0]))
        processor.startImport (juce::File (files[0]), articulation(), string);
}

void RendersPage::StringRow::paint (juce::Graphics& g)
{
    g.setColour (dropping ? ParagonColours::accent.withAlpha (0.18f) : juce::Colour (0xff0f0b0e));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (dropping ? ParagonColours::accent : ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void RendersPage::StringRow::resized()
{
    auto area = getLocalBounds().reduced (10, 4);
    name.setBounds (area.removeFromLeft (170));
    importButton.setBounds (area.removeFromRight (100).withSizeKeepingCentre (100, 28));
    area.removeFromRight (8);
    midiButton.setBounds (area.removeFromRight (70).withSizeKeepingCentre (70, 28));
    area.removeFromRight (10);
    status.setBounds (area);
}
