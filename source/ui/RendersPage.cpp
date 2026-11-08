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
    for (auto* tab : { &sustainTab, &muteTab, &noisesTab })
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
    noisesTab.onClick = [this] { showNoises(); };
    saveAllButton.setTooltip ("Save every render list (sustain and palm mute, all seven strings) and the noise lists into a folder");
    saveAllButton.onClick = [this] { saveAllLists(); };
    renders.addAndMakeVisible (saveAllButton);
    for (int s = guitar::numStrings - 1; s >= 0; --s)   // high E first, as the fretboard
        rows.push_back (std::make_unique<StringRow> (processor, s, [this] { return shown; }));
    std::reverse (rows.begin(), rows.end());           // rows[string]
    for (int s = guitar::numStrings - 1; s >= 0; --s)
        renders.addAndMakeVisible (*rows[(size_t) s]);
    for (int r = 0; r < guitar::noises::numRows; ++r)
    {
        noiseRows.push_back (std::make_unique<NoiseRow> (processor, r));
        renders.addChildComponent (*noiseRows.back());
    }

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
    if (const auto noiseCount = library->noiseTotal(); noiseCount > 0)
        status << "; " << noiseCount << " noises";
    guitarStatus.setText (status, juce::dontSendNotification);
    for (auto& row : rows)
        row->refresh();
    for (auto& row : noiseRows)
        row->refresh();
}

void RendersPage::timerCallback()
{
    // While loading or importing, the rows' and status's progress.
    if (processor.getLoadProgress() >= 0.0f || processor.getImportProgress() >= 0.0f)
        refresh();
}

void RendersPage::showNoises()
{
    showingNoises = true;
    noisesTab.setToggleState (true, juce::dontSendNotification);
    for (auto& row : rows)
        row->setVisible (false);
    for (auto& row : noiseRows)
    {
        row->setVisible (true);
        row->refresh();
    }
    renderHelp.setText ("The noises: a release at each note-off, scrapes, fret noise (F#0). Render each list as a DI with your guitar instrument's "
                        "FACTORY map and release noises on, with Volume all the way down in the SOUND tab for: Sustain and Unpitched "
                        "Release (each string's releases); Power Chord Sustain and Unpitched Release (power chords); Sustain and Pitched "
                        "Release (unpitched); nothing (fret noise). Only the noises are left; import each.", juce::dontSendNotification);
    resized();
}

void RendersPage::showArticulation (guitar::Articulation a)
{
    shown = a;
    showingNoises = false;
    for (auto& row : rows)
        row->setVisible (true);
    for (auto& row : noiseRows)
        row->setVisible (false);
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
        tabs.removeFromLeft (6);
        noisesTab.setBounds (tabs.removeFromLeft (120));
        c.removeFromTop (6);
        auto top = c.removeFromTop (54);
        saveAllButton.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
        top.removeFromRight (12);
        renderHelp.setBounds (top);
        c.removeFromTop (6);
        const auto noiseArea = c;
        const auto rowHeight = juce::jmin (58, c.getHeight() / guitar::numStrings);
        for (int s = guitar::numStrings - 1; s >= 0; --s)
            rows[(size_t) s]->setBounds (c.removeFromTop (rowHeight).reduced (0, 3));
        // NOISES: the high E's releases first, as the strings; then power chords, unpitched, fret noise.
        auto n = noiseArea;
        const auto noiseHeight = juce::jmin (58, n.getHeight() / guitar::noises::numRows);
        for (int s = guitar::numStrings - 1; s >= 0; --s)
            noiseRows[(size_t) s]->setBounds (n.removeFromTop (noiseHeight).reduced (0, 2));
        for (int r = guitar::numStrings; r < guitar::noises::numRows; ++r)
            noiseRows[(size_t) r]->setBounds (n.removeFromTop (noiseHeight).reduced (0, 2));
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

//==============================================================================
RendersPage::NoiseRow::NoiseRow (ParagonX1AudioProcessor& p, int r)
    : processor (p), row (r)
{
    const auto which = guitar::noises::rowList (row);
    name.setText (which.list == guitar::noises::List::pitchedRelease ? "Releases: " + guitar::noises::rowName (row) : guitar::noises::rowName (row),
                  juce::dontSendNotification);
    name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, which.list == guitar::noises::List::pitchedRelease ? Fretboard::stringColour (which.string) : ParagonColours::text);
    name.setMinimumHorizontalScale (0.7f);
    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setMinimumHorizontalScale (0.6f);
    midiButton.setTooltip ("This noise list: click to save the MIDI file, or drag it onto a track in your DAW");
    midiButton.onClick = [this]
    {
        const auto w = guitar::noises::rowList (row);
        const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile (guitar::noises::fileName (w.list, w.string) + ".mid");
        chooser = std::make_unique<juce::FileChooser> ("Save the noise list", file, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (const auto f = c.getResult(); f != juce::File())
                                      processor.saveNoiseList (row, f.withFileExtension ("mid"));
                              });
    };
    midiButton.addMouseListener (this, false);
    importButton.setTooltip ("Import this list's rendered audio (or drop the file on this row)");
    importButton.onClick = [this]
    {
        const auto w = guitar::noises::rowList (row);
        chooser = std::make_unique<juce::FileChooser> ("The render of " + guitar::noises::fileName (w.list, w.string),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (const auto f = c.getResult(); f.existsAsFile())
                                      processor.startNoiseImport (f, row);
                              });
    };
    for (auto* c : std::initializer_list<juce::Component*> { &name, &status, &midiButton, &importButton })
        addAndMakeVisible (c);
}

void RendersPage::NoiseRow::refresh()
{
    juce::String text;
    const auto which = guitar::noises::rowList (row);
    if (processor.getImportingNoise() == row)
        text = "Importing... " + juce::String (juce::roundToInt (juce::jmax (0.0f, processor.getImportProgress()) * 100.0f)) + "%";
    else if (const auto report = processor.getNoiseReport (row); report.isNotEmpty())
        text = report;
    else
    {
        const auto n = processor.getLibrary()->noiseCount (which.list, which.string);
        text = n == 0 ? juce::String ("Not imported") : juce::String (n) + " imported";
    }
    if (status.getText() != text)
    {
        status.setText (text, juce::dontSendNotification);
        status.setTooltip (text);
    }
    importButton.setEnabled (processor.getImportingString() < 0);
}

void RendersPage::NoiseRow::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != &midiButton || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ processor.noiseListFile (row).getFullPathName() }, false, this);
}

bool RendersPage::NoiseRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isAudio (files[0]);
}

void RendersPage::NoiseRow::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    if (files.size() == 1 && isAudio (files[0]))
        processor.startNoiseImport (juce::File (files[0]), row);
}

void RendersPage::NoiseRow::paint (juce::Graphics& g)
{
    g.setColour (dropping ? ParagonColours::accent.withAlpha (0.18f) : juce::Colour (0xff0f0b0e));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (dropping ? ParagonColours::accent : ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void RendersPage::NoiseRow::resized()
{
    auto area = getLocalBounds().reduced (10, 2);
    name.setBounds (area.removeFromLeft (200));
    importButton.setBounds (area.removeFromRight (100).withSizeKeepingCentre (100, 26));
    area.removeFromRight (8);
    midiButton.setBounds (area.removeFromRight (70).withSizeKeepingCentre (70, 26));
    area.removeFromRight (10);
    status.setBounds (area);
}
