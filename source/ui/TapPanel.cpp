#include "TapPanel.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

TapPanel::TapPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    for (int l = 0; l < grooves::numLanes; ++l)
    {
        auto& pad = pads[(size_t) l];
        pad.setButtonText (grooves::laneNames[l]);
        pad.setTooltip ("Tap: plays the drum and puts a hit where the loop is. Or play your controller or the keyboard: its notes "
                        "tap the drums they play.");
        pad.setTriggeredOnMouseDown (true);   // a tap is the press, not the release
        pad.onClick = [this, l] { processor.tapPad (l); };
        addAndMakeVisible (pad);
        auto& x = clears[(size_t) l];
        x.setButtonText ("x");
        x.setTooltip ("Clear this row's taps.");
        x.onClick = [this, l] { clearLane (l); };
        addAndMakeVisible (x);
    }
    tempo.setRange (40.0, 240.0, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.setTooltip ("The loop's tempo (your song's when it opens). Changing it starts the loop again from its first beat.");
    tempo.onDragEnd = [this] { processor.startTapping (tempo.getValue(), loopBeats, beatsPerBar); };
    quantize.addItemList (grooves::quantizeNames, 1);
    quantize.setSelectedItemIndex (1, juce::dontSendNotification);   // 1/8, as is usual for a tapped rhythm
    quantize.setTooltip ("The grid taps land on (and grooves are compared on): 1/8 for most beats, 1/16 for busy ones, the "
                         "triplets for swing and shuffle.");
    quantize.onChange = [this] { repaint(); };
    for (auto* l : { &tempoLabel, &quantizeLabel })
    {
        l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    hint.setText ("Tap a rhythm over the two bars: one drum a time round if you like, every tap stays. Only the drums you "
                  "tap are compared.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, ParagonColours::textDim);
    hint.setFont (juce::FontOptions (12.0f));
    clearAll.setTooltip ("Clear every tap.");
    clearAll.onClick = [this] { clearLane (-1); };
    cancel.setTooltip ("Close (the taps stay for next time; the list is as it was).");
    cancel.onClick = [this] { close(); if (onClose) onClose(); };
    find.setTooltip ("List the grooves closest to this rhythm first, with how close each is (MATCH).");
    find.onClick = [this]
    {
        if (onFind)
            onFind (getRhythm(), getGrid());
    };
    for (juce::Component* c : { (juce::Component*) &tempo, (juce::Component*) &quantize, (juce::Component*) &tempoLabel,
                                (juce::Component*) &quantizeLabel, (juce::Component*) &hint, (juce::Component*) &clearAll,
                                (juce::Component*) &cancel, (juce::Component*) &find })
        addAndMakeVisible (c);
}

TapPanel::~TapPanel()
{
    processor.stopTapping();
}

void TapPanel::open (double bpm, double perBar)
{
    beatsPerBar = juce::jmax (0.5, perBar);
    loopBeats = 2.0 * beatsPerBar;
    tempo.setValue (juce::jlimit (40.0, 240.0, (double) juce::roundToInt (bpm)), juce::dontSendNotification);
    processor.startTapping (tempo.getValue(), loopBeats, beatsPerBar);
    std::vector<ParagonX1AudioProcessor::Tap> stale;
    processor.popTaps (stale);   // nothing from before it opened
    setVisible (true);
    startTimerHz (30);
}

void TapPanel::close()
{
    stopTimer();
    processor.stopTapping();
    setVisible (false);
}

grooves::Rhythm TapPanel::getRhythm() const
{
    grooves::Rhythm r;
    r.lengthBeats = loopBeats;
    r.beatsPerBar = beatsPerBar;
    for (const auto& t : taps)
        r.add (t.lane, t.beat);
    // 0.31.0: you hear the click late by your interface's latency, so taps land late: they may all be up to 40 ms late.
    r.slackBeats = grooves::tapSlackBeats (tempo.getValue());
    return r;
}

double TapPanel::getGrid() const
{
    return grooves::gridOf (juce::jmax (0, quantize.getSelectedItemIndex()));
}

void TapPanel::addTap (int lane, double beat)
{
    // Into the loop (a hair before its start is its end: it rounds to beat 1 on the grid).
    if (lane >= 0 && lane < grooves::numLanes)
    {
        const auto at = std::fmod (std::fmod (beat, loopBeats) + loopBeats, loopBeats);
        // 0.31.0: a tap where this drum already has one (the same place on the grid, a later time round the loop)
        // replaces it: playing it again corrects it rather than adding a second, slightly different hit.
        const auto grid = getGrid();
        const auto cells = juce::jmax (1, juce::roundToInt (loopBeats / grid));
        const auto cellOf = [grid, cells] (double b) { return ((juce::roundToInt (b / grid) % cells) + cells) % cells; };
        std::erase_if (taps, [&] (const auto& t) { return t.lane == lane && cellOf (t.beat) == cellOf (at); });
        taps.push_back ({ lane, at });
    }
    repaint();
}

void TapPanel::clearLane (int lane)
{
    std::erase_if (taps, [lane] (const auto& t) { return lane < 0 || t.lane == lane; });
    repaint();
}

void TapPanel::timerCallback()
{
    std::vector<ParagonX1AudioProcessor::Tap> fresh;
    processor.popTaps (fresh);
    for (const auto& t : fresh)
        addTap (t.lane, t.beat);
    find.setEnabled (! taps.empty());
    repaint (loopArea.expanded (2));
}

juce::Rectangle<int> TapPanel::laneArea (int lane) const
{
    const auto h = loopArea.getHeight() / grooves::numLanes;
    return { loopArea.getX(), loopArea.getY() + lane * h, loopArea.getWidth(), h };
}

void TapPanel::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("TAP TO FIND", 16, 10, 200, 18, juce::Justification::centredLeft, false);

    // The loop: a row a drum, bars and beats marked, QUANTIZE's grid faint, each tap where it lands on the grid (a
    // small mark where it was played), the playhead.
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRect (loopArea);
    const auto x = [this] (double beat) { return (float) loopArea.getX() + (float) (beat / loopBeats) * (float) loopArea.getWidth(); };
    const auto grid = getGrid();
    for (auto b = 0.0; b < loopBeats - 1.0e-9; b += grid)
    {
        const auto onBeat = std::abs (b - std::round (b)) < 1.0e-6;
        const auto onBar = onBeat && std::fmod (std::round (b), beatsPerBar) < 1.0e-6;
        g.setColour (onBar ? ParagonColours::textDim : onBeat ? ParagonColours::panelEdge.brighter (0.3f) : ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (x (b)), (float) loopArea.getY(), (float) loopArea.getBottom());
    }
    for (int l = 0; l < grooves::numLanes; ++l)
    {
        const auto row = laneArea (l);
        g.setColour (ParagonColours::panelEdge.withAlpha (0.7f));
        g.drawHorizontalLine (row.getBottom() - 1, (float) row.getX(), (float) row.getRight());
        for (const auto& t : taps)
            if (t.lane == l)
            {
                const auto cells = juce::jmax (1, juce::roundToInt (loopBeats / grid));
                const auto snapped = (double) (((juce::roundToInt (t.beat / grid) % cells) + cells) % cells) * grid;
                const auto w = juce::jmax (6.0f, (float) loopArea.getWidth() * (float) (grid / loopBeats) - 3.0f);
                g.setColour (ParagonColours::accent);
                g.fillRoundedRectangle (x (snapped) + 1.5f, (float) row.getY() + 4.0f, w, (float) row.getHeight() - 9.0f, 3.0f);
                g.setColour (ParagonColours::text.withAlpha (0.5f));
                g.fillRect (x (t.beat) - 0.5f, (float) row.getBottom() - 4.0f, 1.5f, 3.0f);
            }
    }
    if (const auto at = processor.getTapBeat(); at >= 0.0)
    {
        g.setColour (ParagonColours::text);
        g.drawVerticalLine (juce::roundToInt (x (at)), (float) loopArea.getY(), (float) loopArea.getBottom());
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (loopArea);
}

void TapPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (34);
    top.removeFromLeft (150);
    tempoLabel.setBounds (top.removeFromLeft (56));
    top.removeFromLeft (6);
    tempo.setBounds (top.removeFromLeft (220).withSizeKeepingCentre (220, 26));
    top.removeFromLeft (20);
    quantizeLabel.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (6);
    quantize.setBounds (top.removeFromLeft (140).withSizeKeepingCentre (140, 28));
    find.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 30));
    top.removeFromRight (8);
    cancel.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 30));
    top.removeFromRight (8);
    clearAll.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 30));
    area.removeFromTop (6);
    hint.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (6);

    // The pads on the left, a row each, the loop beside them, each row's x at its end.
    auto padColumn = area.removeFromLeft (120);
    auto clearColumn = area.removeFromRight (34);
    area.removeFromLeft (10);
    area.removeFromRight (6);
    loopArea = area;
    for (int l = 0; l < grooves::numLanes; ++l)
    {
        const auto row = laneArea (l);
        pads[(size_t) l].setBounds (padColumn.withY (row.getY()).withHeight (row.getHeight()).reduced (0, 3));
        clears[(size_t) l].setBounds (clearColumn.withY (row.getY()).withHeight (row.getHeight()).withSizeKeepingCentre (26, 24));
    }
}
