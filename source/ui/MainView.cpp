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

    const auto tabsGroup = mockF (336, 10, 1087, 55);
    const auto rigGroup = mockF (1102, 10, 1399, 55);
    // The tabs' edges in the group, in the mockup's pixels: GUITAR | PRESETS | SETTINGS, three alike (PR7-I 0.2.0).
    constexpr float tabEdges[] = { 338.0f, 587.0f, 836.0f, 1085.0f };

    // In canvas pixels.
    const juce::Rectangle<int> outputArea { 1496, 68, 92, 672 };
    const juce::Rectangle<int> stripArea { 12, 750, 1576, 138 };
    const juce::Rectangle<int> pageArea { 12, 68, 1476, 672 };   // the pages, beside the OUTPUT strip
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
      fretboard (processorToShow),
      guitarPage (processorToShow),
      rigBrowser (processorToShow),
      settingsPage (processorToShow)
{
    setOpaque (true);   // it fills itself: nothing behind it needs drawing
    for (auto* tab : { &guitarTab, &presetsTab, &settingsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (100);
        addAndMakeVisible (*tab);
    }
    guitarTab.setToggleState (true, juce::dontSendNotification);
    guitarTab.setComponentID ("guitarTab");
    presetsTab.setComponentID ("presetsTab");
    settingsTab.setComponentID ("settingsTab");
    guitarTab.onClick = [this] { showPage (Page::guitar); };
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
    settingsButton.onClick = [this] { showPage (page == Page::settings ? Page::guitar : Page::settings); };

    for (auto* component : std::initializer_list<juce::Component*> { &previousRig, &nextRig, &rigNameButton,
                                                                      &undoButton, &redoButton, &saveButton, &settingsButton,
                                                                      &outputColumn, &fretboard, &guitarPage })
        addAndMakeVisible (component);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);
    addChildComponent (progressBox);

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
    auto& tab = newPage == Page::presets ? presetsTab : newPage == Page::settings ? settingsTab : guitarTab;
    tab.setToggleState (true, juce::dontSendNotification);
    guitarPage.setVisible (newPage == Page::guitar);
    rigBrowser.setVisible (newPage == Page::presets);
    settingsPage.setVisible (newPage == Page::settings);
    if (progressBox.isVisible())
        progressBox.toFront (false);
}

void MainView::refreshRig()
{
    const auto rig = audioProcessor.getCurrentRig();
    const auto headerText = rig.name + (audioProcessor.isRigModified() ? " *" : "");
    if (rigNameButton.getButtonText() != headerText)
        rigNameButton.setButtonText (headerText);
}

void MainView::checkLongJobs (double nowMs)
{
    if (progressBox.isShowingJob())
        return;
    // IMPORT A RENDER cutting a string's takes.
    if (audioProcessor.getImportProgress() >= 0.0f)
    {
        progressBox.show ({ "IMPORTING " + guitar::stringLabel (juce::jmax (0, audioProcessor.getImportingString())),
                            [this] { return (double) audioProcessor.getImportProgress(); },
                            [this] { audioProcessor.cancelImport(); } });
        progressBox.toFront (false);
        return;
    }
    // The guitar loading: shown once it's taken half a second.
    if (audioProcessor.getLoadProgress() < 0.0f)
    {
        loadingSince = -1.0;
        return;
    }
    if (loadingSince < 0.0)
        loadingSince = nowMs;
    if (nowMs - loadingSince >= 500.0)
    {
        progressBox.show ({ "LOADING THE GUITAR", [this] { return (double) audioProcessor.getLoadProgress(); }, {} });
        progressBox.toFront (false);
    }
}

void MainView::timerCallback()
{
    checkLongJobs (juce::Time::getMillisecondCounterHiRes());
    // NOTE NAMES: every note's name follows SETTINGS' choice (middle C as C3 or C4).
    if (const auto* names = audioProcessor.getParameters().getRawParameterValue (ParamIDs::noteNames))
        if (const auto octave = notenames::octaveForChoice (juce::roundToInt (names->load())); octave != notenames::middleCOctave.load())
        {
            notenames::middleCOctave = octave;
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
    // Background: near black, with a faint red warmth behind the page.
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
    for (auto x : { tabEdges[1], tabEdges[2] })
        g.drawVerticalLine (juce::roundToInt (x * mockScale), tabsGroup.getY() + 8.0f, tabsGroup.getBottom() - 8.0f);

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (64.0f * mockScale), 0.0f, (float) getWidth());

    // The strip's frame: the fretboard's (the keyboard's until PR7-D 0.41.0, the song track's until PR7-I 0.1.0).
    DockStyle::drawPanel (g, stripArea.toFloat().expanded (0.0f, 0.0f));
}

void MainView::resized()
{
    backgroundCache.invalidate();
    {
        IconButton* tabs[] = { &guitarTab, &presetsTab, &settingsTab };
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
    fretboard.setBounds (stripArea.reduced (4, 2));
    guitarPage.setBounds (pageArea);
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
    progressBox.setCentrePosition (getLocalBounds().getCentre());
}
