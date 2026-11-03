#include "RiffPanel.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

RiffPanel::RiffPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, ParagonColours::accent);
    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, ParagonColours::text);
    hint.setText ("Click or drag on the waveform to set where the part starts (its first beat). LISTEN plays your riff with "
                  "every groove you preview, in time, at its tempo.", juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, ParagonColours::textDim);
    for (auto* l : { &barsLabel, &tempoLabel, &levelLabel })
    {
        l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    for (const auto n : { 1, 2, 4, 8 })
        bars.addItem (juce::String (n) + (n == 1 ? " bar" : " bars"), n);
    bars.setTooltip ("How many bars of the riff to search with (and hear, looping), from its start.");
    bars.onChange = [this] { if (riff) setBars (bars.getSelectedId()); };

    tempo.setRange (40.0, 300.0, 0.1);
    tempo.setTextValueSuffix (" BPM");
    tempo.setTooltip ("The riff's tempo: the song's when it opens (a riff from your song is at its tempo), or the tempo in "
                      "its name or MIDI file. The grooves are played at it while you LISTEN.");
    tempo.onValueChange = [this] { if (riff && std::abs (tempo.getValue() - riff->bpm) > 1.0e-6) setTempo (tempo.getValue(), grooves::Riff::TempoFrom::chosen); };
    twice.setTooltip ("Twice the tempo: steady sixteenths sound like steady eighths at half the tempo.");
    twice.onClick = [this] { if (riff) setTempo (riff->bpm * 2.0, grooves::Riff::TempoFrom::chosen); };
    half.setTooltip ("Half the tempo.");
    half.onClick = [this] { if (riff) setTempo (riff->bpm * 0.5, grooves::Riff::TempoFrom::chosen); };
    heard.setTooltip ("The tempo heard in the riff itself (a guess: right about four times in five for short riffs).");
    heard.onClick = [this] { if (riff && riff->heardBpm > 0.0) setTempo (riff->heardBpm, grooves::Riff::TempoFrom::found); };

    listen.setClickingTogglesState (true);
    listen.setToggleState (processor.isRiffListening(), juce::dontSendNotification);
    listen.setTooltip ("Hear the riff with every groove you preview: from the groove's start, in time, at the riff's tempo.");
    listen.onClick = [this] { processor.setRiffListening (listen.getToggleState()); };
    level.setRange (-30.0, 6.0, 0.5);
    level.setValue (-6.0, juce::dontSendNotification);
    level.setTextValueSuffix (" dB");
    level.setTooltip ("How loud the riff is against the drums.");
    level.onValueChange = [this] { processor.setRiffLevel (juce::Decibels::decibelsToGain ((float) level.getValue())); };
    processor.setRiffLevel (juce::Decibels::decibelsToGain ((float) level.getValue()));

    cancel.setTooltip ("Close (the riff and its search stay; clear them with the cross in RIFF MATCH).");
    cancel.onClick = [this] { setVisible (false); if (onClose) onClose(); };
    find.setTooltip ("List the grooves that fit the riff best first, with how well each fits (MATCH): their kick and crashes "
                     "on the riff's strong notes, few kicks where it's silent.");
    find.onClick = [this] { if (riff && onFind) onFind (*riff); };

    for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &info, (juce::Component*) &hint,
                                (juce::Component*) &barsLabel, (juce::Component*) &tempoLabel, (juce::Component*) &levelLabel,
                                (juce::Component*) &bars, (juce::Component*) &tempo, (juce::Component*) &level,
                                (juce::Component*) &twice, (juce::Component*) &half, (juce::Component*) &heard,
                                (juce::Component*) &listen, (juce::Component*) &cancel, (juce::Component*) &find })
        addAndMakeVisible (c);
}

void RiffPanel::setRiff (grooves::Riff r)
{
    riff = std::move (r);
    // The waveform: the loudest and quietest of each of 2,000 pieces of the file.
    peaks.clear();
    if (riff->audio != nullptr && riff->audio->getNumSamples() > 0)
    {
        const auto& a = *riff->audio;
        const auto n = a.getNumSamples();
        constexpr int points = 2000;
        const auto* x = a.getReadPointer (0);
        for (int p = 0; p < points; ++p)
        {
            const auto from = (int) ((juce::int64) n * p / points), to = juce::jmax (from + 1, (int) ((juce::int64) n * (p + 1) / points));
            auto lo = 0.0f, hi = 0.0f;
            for (int i = from; i < juce::jmin (n, to); ++i)
            {
                lo = juce::jmin (lo, x[i]);
                hi = juce::jmax (hi, x[i]);
            }
            peaks.push_back ({ lo, hi });
        }
    }
    updateControls();
    repaint();
}

void RiffPanel::setStartSeconds (double s)
{
    if (! riff)
        return;
    riff->startSeconds = juce::jlimit (0.0, juce::jmax (0.0, riff->seconds - 0.05), s);
    changed();
}

void RiffPanel::setBars (int n)
{
    if (! riff)
        return;
    riff->bars = juce::jlimit (1, 8, n);
    changed();
}

void RiffPanel::setTempo (double bpm, grooves::Riff::TempoFrom from)
{
    if (! riff)
        return;
    riff->bpm = juce::jlimit (40.0, 300.0, bpm);
    riff->tempoFrom = from;
    changed();
}

void RiffPanel::changed()
{
    updateControls();
    repaint();
    if (onChanged)
        onChanged (*riff);
}

void RiffPanel::updateControls()
{
    if (! riff)
        return;
    title.setText ("RIFF MATCH: " + riff->name, juce::dontSendNotification);
    const auto q = grooves::queryOf (*riff);
    auto strong = 0;
    for (const auto& h : q.hits)
        strong += h.second >= 0.5f ? 1 : 0;
    info.setText (juce::String (riff->onsets.size()) + (riff->fromMidi ? " notes" : " notes heard") + " - "
                      + juce::String (riff->bpm, 1) + " BPM (" + grooves::tempoFromNames[(int) riff->tempoFrom] + "), "
                      + juce::String (riff->numerator) + "/" + juce::String (riff->denominator) + " - the part: "
                      + juce::String ((int) q.hits.size()) + " notes, " + juce::String (strong) + " strong"
                      + (q.feel == grooves::Feel::swung ? ", swung" : ""),
                  juce::dontSendNotification);
    bars.setSelectedId (riff->bars, juce::dontSendNotification);
    tempo.setValue (riff->bpm, juce::dontSendNotification);
    const auto offer = riff->heardBpm > 0.0 && std::abs (riff->heardBpm / riff->bpm - 1.0) > 0.01;
    heard.setButtonText (offer ? "HEARD: " + juce::String (riff->heardBpm, 1) : "HEARD: -");
    heard.setEnabled (offer);
    listen.setToggleState (processor.isRiffListening(), juce::dontSendNotification);
}

double RiffPanel::shownSeconds() const
{
    // The whole file when it's short; else the part and as much again after it (and a bar before).
    if (! riff)
        return 1.0;
    return juce::jmax (0.5, riff->seconds);
}

double RiffPanel::secondsAtX (int x) const
{
    return juce::jlimit (0.0, shownSeconds(), (double) (x - waveArea.getX()) / juce::jmax (1, waveArea.getWidth()) * shownSeconds());
}

void RiffPanel::mouseDown (const juce::MouseEvent& e)
{
    if (riff && waveArea.contains (e.getPosition()))
    {
        // To the nearest note heard within 12 pixels (a riff's first beat is a note), else where clicked.
        auto at = secondsAtX (e.x);
        const auto perPixel = shownSeconds() / juce::jmax (1, waveArea.getWidth());
        auto best = 12.0 * perPixel;
        for (const auto& o : riff->onsets)
            if (std::abs (o.seconds - at) < best)
            {
                best = std::abs (o.seconds - at);
                at = o.seconds;
            }
        setStartSeconds (at);
    }
}

void RiffPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (riff && waveArea.contains (e.getMouseDownPosition()))
        mouseDown (e);
}

void RiffPanel::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRect (waveArea);
    if (riff)
    {
        const auto total = shownSeconds();
        const auto xOf = [&] (double s) { return (float) waveArea.getX() + (float) (s / total) * (float) waveArea.getWidth(); };
        // The part used, lit.
        const auto partFrom = xOf (riff->startSeconds), partTo = xOf (juce::jmin (total, riff->startSeconds + riff->loopSeconds()));
        g.setColour (ParagonColours::accentDim.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (partFrom, (float) waveArea.getY(), partTo - partFrom, (float) waveArea.getHeight()));
        // Bars and beats at its tempo, from its start (both ways).
        const auto beat = 60.0 / riff->bpm;
        for (auto k = (int) std::floor (-riff->startSeconds / beat); ; ++k)
        {
            const auto s = riff->startSeconds + k * beat;
            if (s > total)
                break;
            if (s < 0.0)
                continue;
            const auto onBar = ((k % (int) std::lround (riff->beatsPerBar())) + (int) std::lround (riff->beatsPerBar())) % (int) std::lround (riff->beatsPerBar()) == 0;
            g.setColour (onBar ? ParagonColours::textDim.withAlpha (0.7f) : ParagonColours::panelEdge.withAlpha (0.8f));
            g.drawVerticalLine (juce::roundToInt (xOf (s)), (float) waveArea.getY(), (float) waveArea.getBottom());
        }
        // The waveform.
        const auto mid = (float) waveArea.getCentreY(), halfH = (float) waveArea.getHeight() * 0.42f;
        const auto scale = 1.0f / juce::Decibels::decibelsToGain (-12.0f);   // its peak at -12 dBFS: to full height
        g.setColour (ParagonColours::text.withAlpha (0.55f));
        for (size_t p = 0; p < peaks.size(); ++p)
        {
            const auto s = riff->seconds * (double) p / (double) peaks.size();
            if (s > total)
                break;
            const auto x = xOf (s);
            g.drawVerticalLine (juce::roundToInt (x), mid - peaks[p].second * scale * halfH, mid - peaks[p].first * scale * halfH + 1.0f);
        }
        // The notes heard: the strong ones (as the search reads them) tall.
        const auto q = grooves::queryOf (*riff);
        for (const auto& o : riff->onsets)
        {
            const auto b = riff->beatAt (o.seconds);
            const auto inPart = b >= -0.125 && b < riff->lengthBeats() - 0.125;
            auto strong = o.strength >= 0.5f;
            if (inPart)
                for (const auto& h : q.hits)
                    if (std::abs (h.first - juce::jmax (0.0, b)) < 1.0e-6)
                        strong = h.second >= 0.5f;
            const auto h = strong ? 14.0f : 7.0f;
            g.setColour (inPart ? ParagonColours::accent : ParagonColours::textDim);
            g.fillRect (xOf (o.seconds) - 1.0f, (float) waveArea.getBottom() - h - 2.0f, 2.0f, h);
        }
        // Its start.
        g.setColour (ParagonColours::accent);
        g.fillRect (partFrom - 1.0f, (float) waveArea.getY(), 2.0f, (float) waveArea.getHeight());
        juce::Path flag;
        flag.addTriangle (partFrom, (float) waveArea.getY(), partFrom + 9.0f, (float) waveArea.getY(), partFrom, (float) waveArea.getY() + 9.0f);
        g.fillPath (flag);
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (waveArea);
}

void RiffPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (24);
    find.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 28));
    top.removeFromRight (8);
    cancel.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 28));
    title.setBounds (top);
    info.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    auto controls = area.removeFromTop (30);
    barsLabel.setBounds (controls.removeFromLeft (40));
    controls.removeFromLeft (6);
    bars.setBounds (controls.removeFromLeft (100).reduced (0, 1));
    controls.removeFromLeft (18);
    tempoLabel.setBounds (controls.removeFromLeft (52));
    controls.removeFromLeft (6);
    tempo.setBounds (controls.removeFromLeft (220).withSizeKeepingCentre (220, 26));
    controls.removeFromLeft (6);
    twice.setBounds (controls.removeFromLeft (36).reduced (0, 1));
    controls.removeFromLeft (4);
    half.setBounds (controls.removeFromLeft (36).reduced (0, 1));
    controls.removeFromLeft (4);
    heard.setBounds (controls.removeFromLeft (110).reduced (0, 1));
    controls.removeFromLeft (18);
    listen.setBounds (controls.removeFromLeft (80).reduced (0, 1));
    controls.removeFromLeft (8);
    levelLabel.setBounds (controls.removeFromLeft (44));
    controls.removeFromLeft (6);
    level.setBounds (controls.removeFromLeft (180).withSizeKeepingCentre (180, 26));
    area.removeFromTop (8);
    hint.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (6);
    waveArea = area;
}
