#include "GuitarPage.h"

#include "ui/Fretboard.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int perString = guitar::Library::takesPerString;   // a string's takes (300)

    bool isAudio (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac");
    }
}

GuitarPage::GuitarPage (ParagonX1AudioProcessor& p)
    : processor (p),
      fretMode (p.getParameters(), ParamIDs::fretMode, "FRETTING MODE",
                "NATURAL: the hand follows the notes, four frets wide. POLYPHONIC: five wide, each note of a chord on its own string."),
      pickMode (p.getParameters(), ParamIDs::pickMode, "PICKING MODE", "Which stroke each note's take is: alternating down and up, or all down or all up."),
      forceString (p.getParameters(), ParamIDs::forceString, "FORCE STRING", "Every note on this string, where it can be (keyswitches C-2 to F#-2, G-2: off)."),
      hand (p.getParameters(), ParamIDs::handPosition, "HAND POSITION", "AUTO: the hand follows the notes. Else the fret it stays at (keyswitch A#-2: its velocity; B-2: AUTO)."),
      transpose (p.getParameters(), ParamIDs::transpose, "TRANSPOSE", "Moves every note played (not the keyswitches)."),
      velToVol (p.getParameters(), ParamIDs::velocityToVolume, "VEL > VOL", "How much velocity changes the level within a dynamic (velocity always picks the dynamic)."),
      release (p.getParameters(), ParamIDs::release, "RELEASE", "How quickly a string stops when its note ends."),
      variation (p.getParameters(), ParamIDs::variation, "VARIATION",
                 "An experiment: now and then a neighbouring dynamic's or fret's take instead of the note's own, and each note "
                 "a touch later into its take, louder or quieter, sharp or flat. Fully down (the default): the takes exactly "
                 "as recorded, in turn."),
      tone (p.getParameters(), ParamIDs::tone, "TONE", "A gentle low-pass on the whole guitar; fully up: open."),
      level (p.getParameters(), ParamIDs::patchLevel, "LEVEL", "The preset's own level.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &playing, &sound, &guitarPanel, &renders })
        addAndMakeVisible (c);
    for (auto* c : { &fretMode, &pickMode, &forceString, &hand, &transpose })
        playing.addAndMakeVisible (c);
    for (auto* c : { &velToVol, &release, &variation, &tone, &level })
        sound.addAndMakeVisible (c);

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
    for (auto* c : std::initializer_list<juce::Component*> { &folderLabel, &guitarStatus, &chooseButton, &reloadButton })
        guitarPanel.addAndMakeVisible (c);

    renderHelp.setText ("Render each string's list through your guitar instrument as a DI (its effects off, one guitar, no "
                        "anti-repetition, ringing or release noises), then import the audio. Each string: 25 frets x 3 dynamics x 2 "
                        "round-robins of down and up, about 28 minutes.", juce::dontSendNotification);
    renderHelp.setColour (juce::Label::textColourId, ParagonColours::textDim);
    renderHelp.setFont (juce::FontOptions (13.0f));
    renderHelp.setJustificationType (juce::Justification::topLeft);
    renders.addAndMakeVisible (renderHelp);
    saveAllButton.setTooltip ("Save all seven strings' MIDI files into a folder");
    saveAllButton.onClick = [this] { saveAllLists(); };
    renders.addAndMakeVisible (saveAllButton);
    for (int s = guitar::numStrings - 1; s >= 0; --s)   // high E first, as the fretboard
        rows.push_back (std::make_unique<StringRow> (processor, s));
    std::reverse (rows.begin(), rows.end());           // rows[string]
    for (int s = guitar::numStrings - 1; s >= 0; --s)
        renders.addAndMakeVisible (*rows[(size_t) s]);

    processor.guitarBroadcaster.addChangeListener (this);
    refresh();
    startTimerHz (10);
}

GuitarPage::~GuitarPage()
{
    processor.guitarBroadcaster.removeChangeListener (this);
}

void GuitarPage::refresh()
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
        auto complete = 0;
        for (int s = 0; s < guitar::numStrings; ++s)
            complete += library->count (guitar::Articulation::sustain, s) >= perString ? 1 : 0;
        status = juce::String (library->total()) + " takes: sustain on " + juce::String (complete) + " of 7 strings complete"
               + (complete < guitar::numStrings ? " (the rest played by the nearest takes)" : "");
    }
    guitarStatus.setText (status, juce::dontSendNotification);
    for (auto& row : rows)
        row->refresh();
}

void GuitarPage::timerCallback()
{
    // While loading or importing, the rows' and status's progress.
    if (processor.getLoadProgress() >= 0.0f || processor.getImportProgress() >= 0.0f)
        refresh();
}

void GuitarPage::chooseFolder()
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

void GuitarPage::saveAllLists()
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

void GuitarPage::paint (juce::Graphics&) {}

void GuitarPage::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (560);
    playing.setBounds (left.removeFromTop (190));
    left.removeFromTop (8);
    sound.setBounds (left.removeFromTop (190));
    left.removeFromTop (8);
    guitarPanel.setBounds (left);
    area.removeFromLeft (8);
    renders.setBounds (area);

    SynthPanel::layoutRow (playing.getContentArea().withHeight (ParamControl::knobHeight + 20), { &fretMode, &pickMode, &forceString, &hand, &transpose });
    SynthPanel::layoutRow (sound.getContentArea().withHeight (ParamControl::knobHeight + 20), { &velToVol, &release, &variation, &tone, &level });
    {
        auto c = guitarPanel.getContentArea();
        folderLabel.setBounds (c.removeFromTop (24));
        guitarStatus.setBounds (c.removeFromTop (40));
        c.removeFromTop (8);
        auto buttons = c.removeFromTop (30);
        chooseButton.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (8);
        reloadButton.setBounds (buttons.removeFromLeft (90));
    }
    {
        auto c = renders.getContentArea();
        auto top = c.removeFromTop (54);
        saveAllButton.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
        top.removeFromRight (12);
        renderHelp.setBounds (top);
        c.removeFromTop (6);
        const auto rowHeight = juce::jmin (62, c.getHeight() / guitar::numStrings);
        for (int s = guitar::numStrings - 1; s >= 0; --s)
            rows[(size_t) s]->setBounds (c.removeFromTop (rowHeight).reduced (0, 3));
    }
}

//==============================================================================
GuitarPage::StringRow::StringRow (ParagonX1AudioProcessor& p, int s) : processor (p), string (s)
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
        const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile (guitar::renders::fileName (guitar::Articulation::sustain, string) + ".mid");
        chooser = std::make_unique<juce::FileChooser> ("Save the render list", file, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (const auto f = c.getResult(); f != juce::File())
                                      processor.saveRenderList (guitar::Articulation::sustain, string, f.withFileExtension ("mid"));
                              });
    };
    midiButton.addMouseListener (this, false);
    importButton.setTooltip ("Import this string's rendered audio (or drop the file on this row)");
    importButton.onClick = [this] { importFile(); };
    for (auto* c : std::initializer_list<juce::Component*> { &name, &status, &midiButton, &importButton })
        addAndMakeVisible (c);
}

void GuitarPage::StringRow::refresh()
{
    juce::String text;
    if (processor.getImportingString() == string)
        text = "Importing... " + juce::String (juce::roundToInt (processor.getImportProgress() * 100.0f)) + "%";
    else if (const auto report = processor.getImportReport (string); report.isNotEmpty())
        text = report;
    else
    {
        const auto n = processor.getLibrary()->count (guitar::Articulation::sustain, string);
        text = n == 0 ? juce::String ("Not imported") : juce::String (n) + " of " + juce::String (perString) + " takes";
    }
    if (status.getText() != text)
    {
        status.setText (text, juce::dontSendNotification);
        status.setTooltip (text);
    }
    importButton.setEnabled (processor.getImportingString() < 0);
}

void GuitarPage::StringRow::importFile()
{
    chooser = std::make_unique<juce::FileChooser> ("The render of " + guitar::renders::fileName (guitar::Articulation::sustain, string),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c)
                          {
                              if (const auto f = c.getResult(); f.existsAsFile())
                                  processor.startImport (f, guitar::Articulation::sustain, string);
                          });
}

void GuitarPage::StringRow::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != &midiButton || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ processor.renderListFile (guitar::Articulation::sustain, string).getFullPathName() }, false, this);
}

bool GuitarPage::StringRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isAudio (files[0]);
}

void GuitarPage::StringRow::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();
    if (files.size() == 1 && isAudio (files[0]))
        processor.startImport (juce::File (files[0]), guitar::Articulation::sustain, string);
}

void GuitarPage::StringRow::paint (juce::Graphics& g)
{
    g.setColour (dropping ? ParagonColours::accent.withAlpha (0.18f) : juce::Colour (0xff0f0b0e));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (dropping ? ParagonColours::accent : ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void GuitarPage::StringRow::resized()
{
    auto area = getLocalBounds().reduced (10, 4);
    name.setBounds (area.removeFromLeft (170));
    importButton.setBounds (area.removeFromRight (100).withSizeKeepingCentre (100, 28));
    area.removeFromRight (8);
    midiButton.setBounds (area.removeFromRight (70).withSizeKeepingCentre (70, 28));
    area.removeFromRight (10);
    status.setBounds (area);
}
