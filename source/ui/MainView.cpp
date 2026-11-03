#include "MainView.h"
#include "NoteNames.h"

#include "Parameters.h"
#include "ui/Logo.h"

namespace
{
    // The header was measured off the PR7 family's mockup (1672 px wide);
    // our canvas is 1600. Header positions below are in the mockup's pixels, scaled by this.
    constexpr float mockScale = (float) MainView::width / 1672.0f;

    juce::Rectangle<float> mockF (float left, float top, float right, float bottom)
    {
        return juce::Rectangle<float>::leftTopRightBottom (left, top, right, bottom) * mockScale;
    }

    juce::Rectangle<int> mock (float left, float top, float right, float bottom)
    {
        return mockF (left, top, right, bottom).toNearestInt();
    }

    const auto tabsGroup = mockF (336, 10, 1087, 55);   // PR7-K's group, for four tabs here
    const auto rigGroup = mockF (1102, 10, 1399, 55);
    // The tabs' edges in the group, in the mockup's pixels: DRUMS | GROOVES | FX | PRESETS | SETTINGS (five alike).
    // Six tabs (0.20.0: MIXER) share the group the five did, equally.
    constexpr float tabEdges[] = { 338.0f, 462.5f, 587.0f, 711.5f, 836.0f, 960.5f, 1085.0f };

    // In canvas pixels.
    const juce::Rectangle<int> outputArea { 1496, 68, 92, 672 };
    const juce::Rectangle<int> keyboardArea { 12, 750, 1576, 138 };
    const juce::Rectangle<int> pageArea { 12, 68, 1476, 672 };   // the pages, beside the OUTPUT strip
}

//==============================================================================
KitKeyboard::KitKeyboard (ParagonX1AudioProcessor& p, juce::MidiKeyboardState& state, Orientation orientation)
    : juce::MidiKeyboardComponent (state, orientation), processor (p)
{
    followKit();
    poll.callback = [this] { followKit(); };
    poll.startTimerHz (4);
}

void KitKeyboard::followKit()
{
    const auto root = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drumsRoot)->load());
    const auto sequencer = processor.getDrumSeqSettings();
    const auto keys = sequencer.on && sequencer.trigger == synth::drumseq::Trigger::key
                   && processor.getMapLayout() == synth::drums::notemap::Layout::root;
    const auto newMap = processor.getNoteMap();
    if (root != kitRoot || keys != patternKeys || newMap != map)
    {
        kitRoot = root;
        patternKeys = keys;
        map = newMap;
        repaint();
    }
}

void KitKeyboard::paint (juce::Graphics& g)
{
    followKit();   // as it is now (the timer only notices changes to repaint)
    juce::MidiKeyboardComponent::paint (g);
}

void KitKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
    if ((isKitKey (note) || isPatternKey (note)) && ! isDown)
    {
        g.setColour (ParagonColours::accent.withAlpha (isKitKey (note) ? 0.28f : 0.12f));
        g.fillRect (area.withTrimmedTop (area.getHeight() * 0.62f).reduced (1.0f, 0.0f));
    }
}

void KitKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, noteFillColour);
    if ((isKitKey (note) || isPatternKey (note)) && ! isDown)
    {
        g.setColour (ParagonColours::accent.withAlpha (isKitKey (note) ? 0.55f : 0.25f));
        g.fillRect (area.withTrimmedTop (area.getHeight() * 0.7f).reduced (area.getWidth() * 0.2f, 2.0f));
    }
}

//==============================================================================
GainColumn::GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                        const juce::String& parameterId, LevelMeterSource& meterSource)
    : attachment (parameters, parameterId, knob),
      meter (meterSource)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (14.5f, juce::Font::bold));

    // Set on the knob itself: the editor's look-and-feel is attached after the
    // knob's value box has already been created, so it wouldn't pick these up.
    knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    auto* parameter = parameters.getParameter (parameterId);
    knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (knob);
    addAndMakeVisible (meter);
}

void GainColumn::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
}

void GainColumn::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds (area.withHeight (26).withY (14));
    knob.setBounds (juce::Rectangle<int> (76, 92).withCentre ({ area.getCentreX(), 86 }));
    meter.setBounds (juce::Rectangle<int> (46, 0).withX (area.getCentreX() - 26).withY (146).withBottom (area.getBottom() - 22));
}

//==============================================================================
MainView::MainView (ParagonX1AudioProcessor& processorToShow)
    : audioProcessor (processorToShow),
      outputColumn ("OUTPUT", processorToShow.getParameters(), ParamIDs::outputGain, processorToShow.getOutputMeter()),
      keyboard (processorToShow, processorToShow.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      songTrack (processorToShow),
      drumPage (processorToShow),
      groovesPage (processorToShow),
      mixerPage (processorToShow),
      fxPage (processorToShow),
      rigBrowser (processorToShow),
      settingsPage (processorToShow),
      blockEditor (processorToShow),
      songCreator (processorToShow)
{
    setOpaque (true);   // it fills itself: nothing behind it needs drawing
    // Header
    for (auto* tab : { &drumsTab, &groovesTab, &mixerTab, &fxTab, &presetsTab, &settingsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (100);
        addAndMakeVisible (*tab);
    }
    drumsTab.setToggleState (true, juce::dontSendNotification);
    drumsTab.setComponentID ("drumsTab");
    presetsTab.setComponentID ("presetsTab");
    settingsTab.setComponentID ("settingsTab");
    drumsTab.onClick = [this] { showPage (Page::drums); };
    groovesTab.setComponentID ("groovesTab");
    groovesTab.onClick = [this] { showPage (Page::grooves); };
    mixerTab.setComponentID ("mixerTab");
    mixerTab.onClick = [this] { showPage (Page::mixer); };
    fxTab.setComponentID ("fxTab");
    fxTab.onClick = [this] { showPage (Page::fx); };
    presetsTab.onClick = [this] { showPage (Page::presets); };
    settingsTab.onClick = [this] { showPage (Page::settings); };

    previousRig.onClick = [this] { audioProcessor.stepRig (-1); };
    nextRig.onClick = [this] { audioProcessor.stepRig (1); };
    rigNameButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 19.0f);
    rigNameButton.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    rigNameButton.setTooltip ("The current preset. * means it has changed since it was loaded or saved. Click to browse presets.");
    rigNameButton.onClick = [this] { showPage (Page::presets); };

    undoButton.onClick = [this] { audioProcessor.undo(); };
    redoButton.onClick = [this] { audioProcessor.redo(); };
    saveButton.onClick = [this] { saveRig(); };
    settingsButton.onClick = [this] { showPage (page == Page::settings ? Page::drums : Page::settings); };

    for (auto* component : std::initializer_list<juce::Component*> { &previousRig, &nextRig, &rigNameButton,
                                                                      &undoButton, &redoButton, &saveButton, &settingsButton,
                                                                      &outputColumn })
        addAndMakeVisible (component);

    // The keyboard, in the plugin's colours: dark keys lit red-orange as they're played.
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffdcd7da));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff141115));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6b6569));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ParagonColours::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, ParagonColours::glow.withAlpha (0.85f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.5f));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour (0xff151217));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, ParagonColours::text);
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, ParagonColours::inkDim);
    keyboard.setAvailableRange (24, 107);   // seven octaves shown (notes outside it from the DAW still play, in the kit's keys)
    keyboard.setOctaveForMiddleC (notenames::middleCOctave.load());
    keyboard.setWantsKeyboardFocus (false);   // computer-key playing would fight the DAW's own shortcuts
    keyboard.setScrollButtonsVisible (false);  // all seven octaves fit
    addAndMakeVisible (keyboard);
    // THE SONG TRACK: in the keyboard's place (KEYS | SONG switches); a groove dropped on it from the list is a block.
    addChildComponent (songTrack);
    songTrack.onGrooveDropped = [this] (int row, double beat) { groovesPage.addRowToSong (row, beat); };
    songTrack.onEditBlock = [this] (int block)
    {
        showSong (true);
        songCreator.setVisible (false);
        blockEditor.open (block);
    };
    addChildComponent (blockEditor);
    // THE SONG CREATOR: CREATOR... on the strip (again: closes it), USE WITH SONG CREATOR on a groove or a block, a groove
    // from the GROOVES list dropped on it; its grooves dragged onto the strip.
    addChildComponent (songCreator);
    songTrack.onOpenCreator = [this]
    {
        if (songCreator.isVisible())
            songCreator.setVisible (false);
        else
            openSongCreator();
    };
    songTrack.onUseWithCreator = [this] (const juce::File& f) { openSongCreator (f); };
    songTrack.onCreatorDropped = [this] (int part, int index, double beat) { songCreator.addToSong (part, index, beat); };
    groovesPage.onUseWithCreator = [this] (const juce::File& f) { openSongCreator (f); };
    songCreator.chosenBlock = [this] { return songTrack.getChosen(); };
    // A GROOVE DROPPED ON THE SEQUENCER's GRID (0.13.0): from the GROOVES list (a row, or a GROOVE PARTS row), or the
    // song creator's columns.
    auto& grid = drumPage.getSequencer().getGrid();
    grid.describeDrop = [this, &grid] (const juce::String& d) -> juce::String
    {
        const auto letter = synth::drumseq::patternNames[grid.getPattern()];
        if (d.startsWith ("groove:"))
            return groovesPage.describeRowDrop (grid.getPattern());
        if (d.startsWith ("part:"))
            return "DROP: ITS " + grooves::drumPartNames[juce::jlimit (0, grooves::numDrumParts - 1, d.fromFirstOccurrenceOf (":", false, false).getIntValue())]
                 + " LAID ONTO PATTERN " + letter;
        if (d.startsWith ("creator:"))
            return "DROP: THE GROOVE REPLACES PATTERN " + letter;
        return {};
    };
    grid.onDrop = [this, &grid] (const juce::String& d)
    {
        const auto value = d.fromFirstOccurrenceOf (":", false, false);
        if (d.startsWith ("groove:"))
            return groovesPage.rowToPattern (value.getIntValue(), grid.getPattern());
        if (d.startsWith ("part:"))
            return groovesPage.partToPattern (value.getIntValue(), grid.getPattern());
        if (d.startsWith ("creator:"))
            return songCreator.toPattern (value.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                          value.fromFirstOccurrenceOf (":", false, false).getIntValue(), grid.getPattern());
        return false;
    };
    songCreator.onGrooveRowDropped = [this] (int row)
    {
        const auto& results = groovesPage.getResults();
        if (row >= 0 && row < (int) results.size())
            songCreator.setSource (results[(size_t) row].file);
    };
    for (auto* tab : { &keysTab, &songTab })
    {
        tab->setClickingTogglesState (false);
        tab->setRadioGroupId (0x50e6);
        tab->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);   // the one showing, lit as the page tabs are
        addAndMakeVisible (tab);
    }
    keysTab.setTooltip ("The on-screen keyboard: play the kit with the mouse.");
    songTab.setTooltip ("The song track: grooves laid out as a song, played with your DAW.");
    keysTab.onClick = [this] { showSong (false); };
    songTab.onClick = [this] { showSong (true); };
    keysTab.setToggleState (true, juce::dontSendNotification);
    groovesPage.onAddedToSong = [this] { showSong (true); };

    addAndMakeVisible (drumPage);
    addChildComponent (groovesPage);
    addChildComponent (mixerPage);
    addChildComponent (fxPage);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);

    audioProcessor.rigBroadcaster.addChangeListener (this);
    refreshRig();
    startTimerHz (5);
    setWantsKeyboardFocus (true);
}

MainView::~MainView()
{
    audioProcessor.rigBroadcaster.removeChangeListener (this);
}

void MainView::showPage (Page newPage)
{
    page = newPage;
    auto& tab = newPage == Page::presets ? presetsTab : newPage == Page::settings ? settingsTab : newPage == Page::fx ? fxTab
              : newPage == Page::grooves ? groovesTab : newPage == Page::mixer ? mixerTab : drumsTab;
    tab.setToggleState (true, juce::dontSendNotification);
    fxPage.setVisible (newPage == Page::fx);
    drumPage.setVisible (newPage == Page::drums);
    groovesPage.setVisible (newPage == Page::grooves);
    mixerPage.setVisible (newPage == Page::mixer);
    rigBrowser.setVisible (newPage == Page::presets);
    settingsPage.setVisible (newPage == Page::settings);
}

void MainView::refreshRig()
{
    const auto rig = audioProcessor.getCurrentRig();
    const auto headerText = rig.name + (audioProcessor.isRigModified() ? " *" : "");
    if (rigNameButton.getButtonText() != headerText)
        rigNameButton.setButtonText (headerText);
}

void MainView::timerCallback()
{
    updateDragHover (juce::Time::getMillisecondCounterHiRes(), groovesPage.isDragAndDropActive() || isDragAndDropActive());
    // NOTE NAMES (0.35.1): every note's name follows SETTINGS' choice (middle C as C3 or C4).
    if (const auto* names = audioProcessor.getParameters().getRawParameterValue (ParamIDs::noteNames))
        if (const auto octave = notenames::octaveForChoice (juce::roundToInt (names->load())); octave != notenames::middleCOctave.load()
                                                                                                 || octave != keyboard.getOctaveForMiddleC())
        {
            notenames::middleCOctave = octave;
            keyboard.setOctaveForMiddleC (octave);
            repaint();
        }
    refreshRig();
    undoButton.setEnabled (audioProcessor.canUndo());
    redoButton.setEnabled (audioProcessor.canRedo());

    // The knobs' MIDI marks follow the links and learning.
    if (! (audioProcessor.getMidiLinks() == shownLinks) || audioProcessor.getMidiLearnTarget() != shownLearning)
    {
        shownLinks = audioProcessor.getMidiLinks();
        shownLearning = audioProcessor.getMidiLearnTarget();
        repaint();
    }
}

//==============================================================================
int MainView::linkedController (const juce::String& parameterId) const { return audioProcessor.getMidiLinks().controllerFor (parameterId); }
bool MainView::isLearning (const juce::String& parameterId) const { return audioProcessor.getMidiLearnTarget() == parameterId; }

void MainView::learn (const juce::String& parameterId)
{
    if (isLearning (parameterId))
        audioProcessor.cancelMidiLearn();
    else
        audioProcessor.startMidiLearn (parameterId);
    repaint();
}

void MainView::forget (const juce::String& parameterId)
{
    auto links = audioProcessor.getMidiLinks();
    links.forget (parameterId);
    audioProcessor.setMidiLinks (links);
    repaint();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshRig();
}

void MainView::saveRig()
{
    if (! audioProcessor.saveCurrentRig())
        rigBrowser.promptSaveAs();
    refreshRig();
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    if (! modifiers.isCommandDown())
        return false;

    const auto keyCode = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (keyCode == 'Z')
    {
        if (modifiers.isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        return true;
    }
    if (keyCode == 'Y')
    {
        audioProcessor.redo();
        return true;
    }
    return false;
}

void MainView::paint (juce::Graphics& g)
{
    // 0.30.0: drawn once into a picture and copied (every child's repaint repainted it).
    backgroundCache.draw (g, getLocalBounds(), [this] (juce::Graphics& bg) { paintBackground (bg); });
}

void MainView::paintBackground (juce::Graphics& g)
{
    // Background: near black, with a faint red warmth behind the kit.
    g.fillAll (ParagonColours::background);
    const auto centre = juce::Point<float> (450.0f, 254.0f);
    g.setGradientFill (juce::ColourGradient (ParagonColours::glow.withAlpha (0.06f), centre,
                                             ParagonColours::glow.withAlpha (0.0f), centre.translated (900.0f, 0.0f), true));
    g.fillRect (getLocalBounds());

    // Header: the emblem and name.
    auto emblem = mockF (33, 13, 73, 50);
    juce::Path triangle;
    triangle.addTriangle (emblem.getTopLeft(), emblem.getTopRight(), { emblem.getCentreX(), emblem.getBottom() });
    g.setColour (ParagonColours::glow.withAlpha (0.3f));
    g.strokePath (triangle, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ParagonColours::glow);
    g.strokePath (triangle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    triangle.applyTransform (juce::AffineTransform::scale (0.5f, 0.5f, emblem.getCentreX(), emblem.getCentreY() - 3.0f));
    g.strokePath (triangle, juce::PathStrokeType (1.5f));

    const auto nameArea = mockF (84, 10, 330, 55);
    const auto nameFont = juce::FontOptions (21.0f);
    g.setFont (nameFont);
    g.setColour (ParagonColours::text);
    g.drawText ("PARAGON", nameArea, juce::Justification::centredLeft, false);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (nameFont, "PARAGON ");
    Logo::drawMark (g, nameArea.withTrimmedLeft (nameWidth).withSizeKeepingCentre (nameArea.getWidth() - nameWidth, 17.0f),
                   juce::Justification::centredLeft, { ParagonColours::glow, ParagonColours::glow, {}, {}, 1.0f, true });

    // Framed groups: the tabs and the preset selector.
    for (const auto& group : { tabsGroup, rigGroup })
    {
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (group, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (group.reduced (0.5f), 5.0f, 1.0f);
    }
    for (auto x : { tabEdges[1], tabEdges[2], tabEdges[3], tabEdges[4], tabEdges[5] })
        g.drawVerticalLine (juce::roundToInt (x * mockScale), tabsGroup.getY() + 8.0f, tabsGroup.getBottom() - 8.0f);

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (64.0f * mockScale), 0.0f, (float) getWidth());

    // The keyboard's frame.
    DockStyle::drawPanel (g, keyboardArea.toFloat().expanded (0.0f, 0.0f));
}

void MainView::resized()
{
    backgroundCache.invalidate();
    // Header
    {
        IconButton* tabs[] = { &drumsTab, &groovesTab, &mixerTab, &fxTab, &presetsTab, &settingsTab };
        for (size_t i = 0; i < std::size (tabs); ++i)
            tabs[i]->setBounds (mock (tabEdges[i] + (i > 0 ? 1.0f : 0.0f), 12, tabEdges[i + 1] - (i + 1 < std::size (tabs) ? 1.0f : 0.0f), 53));
    }
    previousRig.setBounds (mock (1110, 14, 1150, 51));
    nextRig.setBounds (mock (1351, 14, 1391, 51));
    rigNameButton.setBounds (mock (1152, 14, 1349, 51));
    undoButton.setBounds (mock (1444, 14, 1482, 50));
    redoButton.setBounds (mock (1496, 14, 1534, 50));
    saveButton.setBounds (mock (1551, 14, 1589, 50));
    settingsButton.setBounds (mock (1611, 14, 1649, 50));

    outputColumn.setBounds (outputArea);

    // Seven octaves from MIDI note 24, filling the strip (the width first: changing it moves the view).
    auto keys = keyboardArea.reduced (8, 6);
    auto switches = keys.removeFromLeft (64);
    keys.removeFromLeft (8);
    keysTab.setBounds (switches.removeFromTop (switches.getHeight() / 2).reduced (0, 3));
    songTab.setBounds (switches.reduced (0, 3));
    songTrack.setBounds (keys);
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 49.0f);   // seven octaves: 49 white keys
    keyboard.setLowestVisibleKey (24);

    fxPage.setBounds (pageArea);
    drumPage.setBounds (pageArea);
    groovesPage.setBounds (pageArea);
    mixerPage.setBounds (pageArea);
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
    blockEditor.setBounds (pageArea);
    songCreator.setBounds (pageArea);
}

void MainView::showSong (bool show)
{
    songTrack.setVisible (show);
    keyboard.setVisible (! show);
    keysTab.setToggleState (! show, juce::dontSendNotification);
    songTab.setToggleState (show, juce::dontSendNotification);
}

void MainView::openSongCreator (const juce::File& source)
{
    showSong (true);
    blockEditor.setVisible (false);
    if (source != juce::File())
        songCreator.setSource (source);
    songCreator.open();
}

//==============================================================================
// A GROOVE DRAGGED OVER THE DRUMS TAB (0.13.0)
bool MainView::isInterestedInDragSource (const SourceDetails& details)
{
    const auto d = details.description.toString();
    return d.startsWith ("groove:") || d.startsWith ("part:") || d.startsWith ("creator:");
}

void MainView::itemDragMove (const SourceDetails& details)
{
    if (drumsTab.getBounds().contains (details.localPosition) && ! dragPeek && page != Page::drums)
    {
        if (hoverSince < 0.0)
            hoverSince = juce::Time::getMillisecondCounterHiRes();
    }
    else
        hoverSince = -1.0;
}

void MainView::itemDragExit (const SourceDetails&)
{
    hoverSince = -1.0;
}

void MainView::itemDropped (const SourceDetails&)
{
    hoverSince = -1.0;   // dropped somewhere that takes nothing
}

void MainView::updateDragHover (double nowMs, bool dragStillOn)
{
    // Half a second over DRUMS: its SEQUENCER in front, the page the drag came from left under it (hiding that would end
    // the drag).
    if (hoverSince >= 0.0 && dragStillOn && nowMs - hoverSince >= 500.0)
    {
        hoverSince = -1.0;
        dragPeek = true;
        drumPage.showView (DrumPage::View::sequencer);
        drumPage.setVisible (true);
        drumPage.toFront (false);
        drumsTab.setToggleState (true, juce::dontSendNotification);
    }
    // The drag over: the window settles on DRUMS.
    if (dragPeek && ! dragStillOn)
    {
        dragPeek = false;
        songCreator.setVisible (false);
        blockEditor.setVisible (false);
        showPage (Page::drums);
    }
    if (! dragStillOn)
        hoverSince = -1.0;
}
