#include "SongTrack.h"

#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int barHeight = 24, rulerHeight = 16;
    const juce::String groovePrefix = "groove:", creatorPrefix = "creator:";

    bool isMidiFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("mid;midi");
    }
}

// BOUNCE (0.12.0): the bounce on a thread of its own (the plugin plays on).
struct SongTrack::BounceThread final : juce::Thread
{
    BounceThread (ParagonX1AudioProcessor& p, const ParagonX1AudioProcessor::BounceOptions& o, const juce::File& folder)
        : juce::Thread ("PR7-D bounce"), bouncer (p, o, folder) {}
    ~BounceThread() override
    {
        bouncer.cancel = true;
        stopThread (10000);
    }
    void run() override { files = bouncer.run(); }
    ParagonX1AudioProcessor::SongBouncer bouncer;   // made here, on the message thread
    juce::Array<juce::File> files;
};

double SongTrack::getBounceProgress() const
{
    if (bounceThread == nullptr || ! bounceThread->isThreadRunning())
        return -1.0;
    return (double) bounceThread->bouncer.progress.load();
}

void SongTrack::cancelBounce()
{
    if (bounceThread != nullptr)
        bounceThread->bouncer.cancel = true;
}

SongTrack::SongTrack (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    play.setClickingTogglesState (false);
    play.setTooltip ("Play the song with your DAW stopped, from the place marked on the bar numbers (click them to move it). "
                     "While your DAW plays, the song plays with it.");
    play.onClick = [this]
    {
        if (processor.isSongPlaying())
            processor.stopSong();
        else
            processor.playSong (startBeat);
    };
    addAndMakeVisible (play);
    exportButton.setTooltip ("The whole song as one MIDI file on the kit's notes: click to save it, drag it onto a DAW track.");
    exportButton.fileName = "PR7-D Song";
    // EXPORT MIDI with FOLLOW DAW on: a reminder (the song on a DAW track as well plays every hit twice).
    const auto remind = [this]
    {
        if (processor.getSong().followDaw)
            showNote ("Playing the song from a DAW track? Turn FOLLOW DAW off, or each hit plays twice.", 10.0);
    };
    exportButton.makeDragFile = [this, remind]
    {
        remind();
        return processor.songDragFile();
    };
    exportButton.exportTo = [this, remind] (const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        const auto ok = out.openedOk() && processor.songToMidi().writeTo (out);
        remind();
        return ok;
    };
    addAndMakeVisible (exportButton);
    creator.setTooltip ("The song creator: a whole song from one groove and the grooves round it, by song structure.");
    creator.onClick = [this] { if (onOpenCreator != nullptr) onOpenCreator(); };
    addAndMakeVisible (creator);
    track.setTooltip ("IMPORT MIDI FILE... (a whole song, its tempo and time signatures if you like), and removing all tempo "
                      "or time signature changes.");
    track.onClick = [this] { trackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&track)); };
    addAndMakeVisible (track);
    record.setClickingTogglesState (true);
    record.setTooltip ("Record: the notes you play (your DAW's MIDI, a controller, the keyboard) while the song plays go into it, "
                       "on top of the blocks there (or into a new RECORDED block). Click again to stop: the take is one undo step.");
    record.setColour (juce::TextButton::textColourOnId, ParagonColours::meterRed);   // armed: red (and the lane framed red)
    record.onClick = [this] { processor.setSongRecording (record.getToggleState()); };
    addAndMakeVisible (record);
    follow.setClickingTogglesState (true);
    follow.setToggleState (processor.getSong().followDaw, juce::dontSendNotification);
    follow.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    follow.setTooltip ("On: the song plays with your DAW (bar 1 is your DAW's bar 1). Off: only with PLAY, your DAW playing or not. "
                       "Turn it off when the song's MIDI is on a DAW track (EXPORT MIDI dragged in), or every hit plays twice.");
    follow.onClick = [this]
    {
        const auto on = follow.getToggleState();
        edit ([on] (song::Song& s) { s.followDaw = on; });
        if (! on)
            showNote ("FOLLOW DAW off: the song plays only with PLAY.", 5.0);
    };
    addAndMakeVisible (follow);
    // THE LOOP AREA and THE CLICK (0.44.0).
    loop.setClickingTogglesState (false);
    loop.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    loop.setTooltip ("LOOP: PLAY goes round the loop area (shown on the bar numbers). Drag along the bar numbers to set it; "
                     "right-click a block: LOOP THIS BLOCK. With none set, the chosen block's bars (else four bars from PLAY's start).");
    loop.onClick = [this] { toggleLoop(); };
    addAndMakeVisible (loop);
    click.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    click.setTooltip ("THE CLICK: a metronome while the song plays (higher on each bar's first beat): off, only while REC is "
                      "armed, or always.");
    click.onClick = [this] { clickMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&click)); };
    addAndMakeVisible (click);
    blocksView.setTooltip ("The blocks: arrange the song.");
    tempoView.setTooltip ("The song's tempo changes (used by PLAY and EXPORT MIDI; while your DAW plays, its tempo leads): "
                          "double-click to add one, drag one to move it, right-click it to glide to the next or remove it.");
    timeView.setTooltip ("The song's time signature changes: double-click or right-click a bar to set its time signature.");
    for (auto* b : { &blocksView, &tempoView, &timeView })
    {
        b->setRadioGroupId (0x7e40);
        b->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        addAndMakeVisible (b);
    }
    blocksView.setToggleState (true, juce::dontSendNotification);
    blocksView.onClick = [this] { setView (View::blocks); };
    tempoView.onClick = [this] { setView (View::tempo); };
    timeView.onClick = [this] { setView (View::time); };
    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (status);
    processor.songBroadcaster.addChangeListener (this);
    updateStatus();
    // 0.43.0: the scroll bar under the lane (shown when the song is longer than the lane shows).
    scroll.setAutoHide (true);
    scroll.addListener (this);
    scroll.setColour (juce::ScrollBar::thumbColourId, ParagonColours::accent.withAlpha (0.6f));
    scroll.setColour (juce::ScrollBar::trackColourId, juce::Colour (0xff0d0a0e));
    addAndMakeVisible (scroll);   // (shown only while there's more song than the lane shows: auto-hide)
    syncScroll();
    startTimerHz (30);
}

SongTrack::~SongTrack()
{
    processor.songBroadcaster.removeChangeListener (this);
}

//==============================================================================
double SongTrack::totalBeats() const noexcept
{
    // At least 16 bars; the song and two bars after it when it's longer; to a bar line.
    const auto& s = processor.getSong();
    auto want = juce::jmax (16.0 * s.beatsPerBar(), s.endBeat() + 2.0 * s.beatsPerBarAt (s.endBeat()));
    for (const auto& t : s.tempos)
        want = juce::jmax (want, t.beat + s.beatsPerBarAt (t.beat));
    for (const auto line : s.barLines (want))
        if (line >= want - 1.0e-9)
            return line;
    return want;
}

double SongTrack::beatsShown() const noexcept
{
    // 0.43.0: what the lane shows: the whole song while it fits in `barsShown` bars (as before), else that many bars.
    return juce::jmin (totalBeats(), barsShown * processor.getSong().beatsPerBar());
}

double SongTrack::beatAt (float x) const noexcept
{
    return juce::jmax (0.0, viewStart + (double) (x - (float) lane.getX()) / juce::jmax (1, lane.getWidth()) * beatsShown());
}

float SongTrack::xOf (double beat) const noexcept
{
    return (float) lane.getX() + (float) ((beat - viewStart) / beatsShown()) * (float) lane.getWidth();
}

void SongTrack::setViewStart (double beat)
{
    const auto most = juce::jmax (0.0, totalBeats() - beatsShown());
    beat = juce::jlimit (0.0, most, beat);
    if (std::abs (beat - viewStart) < 1.0e-9)
        return;
    viewStart = beat;
    syncScroll();
    repaint();
}

void SongTrack::setBarsShown (double bars)
{
    // Zoomed about the lane's middle.
    const auto middle = viewStart + 0.5 * beatsShown();
    barsShown = juce::jlimit (4.0, 256.0, bars);
    viewStart = juce::jmax (0.0, middle - 0.5 * beatsShown());
    syncScroll();
    setViewStart (viewStart);
    repaint();
}

void SongTrack::syncScroll()
{
    // The song's length changes with every edit (and undo): the bar's range follows, and the view stays inside it.
    const auto total = totalBeats(), span = beatsShown();
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - span), viewStart);
    scroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    scroll.setCurrentRange (viewStart, span, juce::dontSendNotification);
    scroll.setSingleStepSize (processor.getSong().beatsPerBar());
}

void SongTrack::scrollBarMoved (juce::ScrollBar*, double start)
{
    viewStart = start;
    repaint();
}

void SongTrack::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const auto step = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        setBarsShown (barsShown * (step > 0.0f ? 0.8 : 1.25));
        return;
    }
    const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    if (beatsShown() < totalBeats() - 1.0e-9)
        setViewStart (viewStart - (double) amount * (wheel.isReversed ? -1.0 : 1.0) * 0.5 * beatsShown());
    else
        Component::mouseWheelMove (e, wheel);
}

void SongTrack::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (barHeight);
    top.removeFromLeft (52);   // "SONG"
    play.setBounds (top.removeFromLeft (64).reduced (2, 2));
    record.setBounds (top.removeFromLeft (52).reduced (2, 2));
    exportButton.setBounds (top.removeFromLeft (104).reduced (2, 2));
    creator.setBounds (top.removeFromLeft (96).reduced (2, 2));
    track.setBounds (top.removeFromLeft (80).reduced (2, 2));
    top.removeFromLeft (8);
    blocksView.setBounds (top.removeFromLeft (62).reduced (2, 2));
    tempoView.setBounds (top.removeFromLeft (62).reduced (2, 2));
    timeView.setBounds (top.removeFromLeft (56).reduced (2, 2));
    top.removeFromLeft (8);
    follow.setBounds (top.removeFromLeft (98).reduced (2, 2));
    loop.setBounds (top.removeFromLeft (58).reduced (2, 2));    // 0.44.0
    click.setBounds (top.removeFromLeft (92).reduced (2, 2));
    status.setBounds (top.reduced (8, 0));
    area.removeFromTop (2);
    ruler = area.removeFromTop (rulerHeight);
    scroll.setBounds (area.removeFromBottom (10).reduced (0, 1));   // 0.43.0
    lane = area;
    syncScroll();
}

void SongTrack::visibilityChanged()
{
    // Edits made while it was hidden (from the GROOVES page, undo, a project loaded) reached it as change messages; shown
    // now, it reads the song as it is.
    if (isVisible())
    {
        updateStatus();
        repaint();
    }
}

void SongTrack::updateStatus()
{
    const auto& s = processor.getSong();
    if (bounceThread != nullptr || bounceNote.isNotEmpty())
    {
        status.setText (bounceNote, juce::dontSendNotification);
        return;
    }
    if (note.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < noteUntil)
    {
        status.setText (note, juce::dontSendNotification);
        return;
    }
    if (s.isEmpty())
    {
        status.setText ("Drag grooves here from the GROOVES list (or MIDI files), or right-click a groove: ADD TO SONG.", juce::dontSendNotification);
        return;
    }
    // Its bars (whole ones, with the time signature changes), its time signatures and tempo.
    auto bars = 0;
    for (const auto line : s.barLines (s.endBeat()))
        bars += line < s.endBeat() - 1.0e-9 ? 1 : 0;
    juce::String text;
    text << (int) s.blocks.size() << (s.blocks.size() == 1 ? " block, " : " blocks, ") << bars << " bars in " << s.numerator << "/"
         << s.denominator;
    if (! s.meters.empty())
        text << " (" << (int) s.meters.size() << (s.meters.size() == 1 ? " time change)" : " time changes)");
    if (s.tempos.empty())
        text << ", the DAW's tempo";
    else
    {
        auto low = 1.0e9, high = 0.0;
        for (const auto& t : s.tempos)
        {
            low = juce::jmin (low, t.bpm);
            high = juce::jmax (high, t.bpm);
        }
        text << ", " << (juce::roundToInt (low) == juce::roundToInt (high) ? juce::String (juce::roundToInt (low))
                                                                            : juce::String (juce::roundToInt (low)) + "-" + juce::String (juce::roundToInt (high)))
             << " BPM";
    }
    text << (view == View::tempo ? ". Double-click: a tempo change; drag one; right-click it for more."
             : view == View::time ? ". Double-click or right-click a bar: its time signature."
                                  : ". Drag a block to move it, its right edge to lengthen it; right-click for more.");
    status.setText (text, juce::dontSendNotification);
}

//==============================================================================
void SongTrack::paint (juce::Graphics& g)
{
    const auto& s = processor.getSong();
    const auto bar = s.beatsPerBar();
    const auto shown = totalBeats();   // (0.43.0: the whole song; the lane shows a part of it, clipped)
    const auto pxPerBar = (float) lane.getWidth() * (float) (bar / beatsShown());

    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SONG", 2, 0, 50, barHeight, juce::Justification::centredLeft, false);
    juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (ruler.getUnion (lane));
    const auto firstShown = viewStart - bar, lastShown = viewStart + beatsShown() + bar;

    // The lane and the bar lines (with the time signature changes), numbered (every 2nd or 4th when they're close); a
    // change's time signature by its bar's number.
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (lane.toFloat(), 4.0f);
    const auto every = pxPerBar < 14.0f ? 4 : pxPerBar < 28.0f ? 2 : 1;
    g.setFont (juce::FontOptions (10.0f));
    const auto lines = s.barLines (shown);
    for (int b = 0; b < (int) lines.size() && lines[(size_t) b] < shown + 1.0e-9; ++b)
    {
        const auto at = lines[(size_t) b];
        if (at < firstShown || at > lastShown)
            continue;
        const auto x = xOf (at);
        const auto change = std::any_of (s.meters.begin(), s.meters.end(), [at] (const song::Meter& m) { return std::abs (m.beat - at) < 1.0e-9; })
                         || (b == 0 && (view == View::time || ! s.meters.empty()));
        g.setColour (change ? ParagonColours::text.withAlpha (0.7f) : ParagonColours::panelEdge.withAlpha (b % every == 0 ? 1.0f : 0.45f));
        g.drawVerticalLine (juce::roundToInt (x), (float) lane.getY(), (float) lane.getBottom());
        if ((b % every == 0 || change) && at < shown - 1.0e-9)
        {
            g.setColour (ParagonColours::textDim);
            g.drawText (juce::String (b + 1), juce::roundToInt (x) + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft, false);
            if (change)
            {
                const auto m = s.meterAt (at);
                g.setColour (ParagonColours::accent);
                g.drawText (juce::String (m.numerator) + "/" + juce::String (m.denominator), juce::roundToInt (x) + 18, ruler.getY(), 34,
                            ruler.getHeight(), juce::Justification::centredLeft, false);
            }
        }
    }

    // The blocks: their SONG PART's colour, their name, their hits (a tick each: kicks low, cymbals high), where the
    // groove starts again (a faint line), MUTED greyed.
    for (int i = 0; i < (int) s.blocks.size(); ++i)
    {
        const auto& b = s.blocks[(size_t) i];
        if (b.end() < firstShown || b.start > lastShown)
            continue;
        const auto r = juce::Rectangle<float> (xOf (b.start), (float) lane.getY() + 3.0f, xOf (b.end()) - xOf (b.start), (float) lane.getHeight() - 6.0f)
                           .reduced (1.0f, 0.0f);
        const auto colour = b.muted ? juce::Colour (0xff5a5559) : song::colourOf (b.part);
        g.setColour (colour.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colour.withAlpha (0.8f));
        for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
        {
            if (repeat > 0.0)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.drawVerticalLine (juce::roundToInt (xOf (b.start + repeat)), r.getY() + 16.0f, r.getBottom());
                g.setColour (colour.withAlpha (0.8f));
            }
            for (const auto& h : b.hits)
            {
                if (repeat + h.beat >= b.length - 1.0e-9)
                    break;
                const auto part = juce::jmax (0, grooves::drumPartOf (h.slot));
                const auto y = r.getBottom() - 4.0f - (r.getHeight() - 22.0f) * (float) part / (float) (grooves::numDrumParts - 1);
                g.fillRect (xOf (b.start + repeat + h.beat), y - 2.0f, 2.0f, 4.0f);
            }
        }
        g.setColour (i == chosen ? ParagonColours::text : colour);
        g.drawRoundedRectangle (r, 4.0f, i == chosen ? 2.0f : 1.0f);
        g.setColour (b.muted ? ParagonColours::textDim : ParagonColours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ((b.muted ? "MUTED  " : "") + b.name, r.reduced (5.0f, 2.0f).removeFromTop (14.0f), juce::Justification::centredLeft, true);
    }

    // A block being moved or lengthened: where it would go.
    if (drag != Drag::none && chosen >= 0 && chosen < (int) s.blocks.size())
    {
        const auto& b = s.blocks[(size_t) chosen];
        const auto from = drag == Drag::move ? s.snap (dragBeat) : b.start;
        auto trial = s;
        trial.resize (chosen, dragBeat - b.start);
        const auto to = drag == Drag::move ? from + b.length : b.start + trial.blocks[(size_t) chosen].length;
        g.setColour (ParagonColours::accent);
        g.drawRoundedRectangle ({ xOf (from), (float) lane.getY() + 1.0f, xOf (to) - xOf (from), (float) lane.getHeight() - 2.0f }, 4.0f, 2.0f);
    }
    // A groove or a file over the lane: where it would go.
    if (dropBeat >= 0.0)
    {
        auto trial = s;
        song::Block probe;
        probe.length = s.beatsPerBarAt (dropBeat);
        const auto at = trial.blocks[(size_t) trial.place (probe, dropBeat)].start;
        g.setColour (ParagonColours::accent);
        g.fillRect (xOf (at) - 1.0f, (float) lane.getY(), 3.0f, (float) lane.getHeight());
    }

    // TEMPO: the blocks dimmed under the song's tempo line (its changes as handles, their tempos by them; a glide
    // sloping to the next). TIME: the blocks dimmed.
    if (view != View::blocks)
    {
        g.setColour (juce::Colour (0xff0d0a0e).withAlpha (0.6f));
        g.fillRoundedRectangle (lane.toFloat(), 4.0f);
    }
    if (view == View::tempo)
    {
        const auto& t = tempoDraft.has_value() ? *tempoDraft : s;
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        if (t.tempos.empty())
        {
            g.setColour (ParagonColours::textDim);
            const auto y = (float) lane.getCentreY();
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine ({ (float) lane.getX(), y, (float) lane.getRight(), y }, dashes, 2, 1.0f);
            g.drawText ("THE DAW'S TEMPO  (double-click to add a tempo change)", lane.withTrimmedLeft (8).withHeight (lane.getHeight() / 2 - 2),
                        juce::Justification::bottomLeft, false);
        }
        else
        {
            juce::Path line;
            line.startNewSubPath ((float) lane.getX(), pointOf ({ 0.0, t.tempos.front().bpm }).y);
            for (size_t i = 0; i < t.tempos.size(); ++i)
            {
                const auto p = pointOf (t.tempos[i]);
                line.lineTo (p.x, line.getCurrentPosition().y);   // steady up to it (a step, or the glide's end)
                line.lineTo (p);
                if (t.tempos[i].ramp && i + 1 < t.tempos.size())
                    line.lineTo (pointOf (t.tempos[i + 1]));
            }
            line.lineTo ((float) lane.getRight(), line.getCurrentPosition().y);
            g.setColour (ParagonColours::accent);
            g.strokePath (line, juce::PathStrokeType (2.0f));
            for (size_t i = 0; i < t.tempos.size(); ++i)
            {
                const auto p = pointOf (t.tempos[i]);
                g.setColour ((int) i == dragTempo ? ParagonColours::glow : ParagonColours::accent);
                g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
                g.setColour (ParagonColours::text);
                g.drawText (juce::String (juce::roundToInt (t.tempos[i].bpm)) + (t.tempos[i].ramp ? " >" : ""), juce::roundToInt (p.x) + 7,
                            juce::roundToInt (p.y) - 16, 60, 14, juce::Justification::centredLeft, false);
            }
        }
    }

    // RECORD armed: the lane framed red.
    if (processor.isSongRecording())
    {
        g.setColour (ParagonColours::meterRed);
        g.drawRoundedRectangle (lane.toFloat().reduced (1.0f), 4.0f, 2.0f);
    }

    // THE LOOP AREA (0.44.0): a band on the bar numbers (lit while on) and a faint wash over the lane; a drag's draft.
    {
        const auto& range = loopDraft.has_value() ? *loopDraft : std::make_pair (s.loopStart, s.loopEnd);
        if (range.second > range.first + 1.0e-9)
        {
            const auto on = loopDraft.has_value() || s.loopOn;
            const auto x0 = xOf (range.first), x1 = xOf (range.second);
            g.setColour (ParagonColours::accent.withAlpha (on ? 0.45f : 0.15f));
            g.fillRect (juce::Rectangle<float> (x0, (float) ruler.getY() + 1.0f, x1 - x0, (float) ruler.getHeight() - 2.0f));
            if (on)
            {
                g.setColour (ParagonColours::accent.withAlpha (0.07f));
                g.fillRect (juce::Rectangle<float> (x0, (float) lane.getY(), x1 - x0, (float) lane.getHeight()));
                g.setColour (ParagonColours::accent.withAlpha (0.7f));
                g.drawVerticalLine (juce::roundToInt (x0), (float) ruler.getY(), (float) lane.getBottom());
                g.drawVerticalLine (juce::roundToInt (x1), (float) ruler.getY(), (float) lane.getBottom());
            }
        }
    }

    // PLAY's start (a mark on the bar numbers) and where the song is playing.
    g.setColour (ParagonColours::accent);
    const auto sx = xOf (startBeat);
    juce::Path mark;
    mark.addTriangle (sx, (float) ruler.getBottom(), sx - 5.0f, (float) ruler.getY() + 3.0f, sx + 5.0f, (float) ruler.getY() + 3.0f);
    g.fillPath (mark);
    if (const auto at = processor.getSongPosition(); at >= 0.0 && at <= shown)
    {
        g.setColour (ParagonColours::glow);
        g.fillRect (xOf (at) - 0.75f, (float) ruler.getY(), 1.5f, (float) (lane.getBottom() - ruler.getY()));
    }
}

//==============================================================================
void SongTrack::syncButtons()
{
    // LOOP and CLICK as the song has them (undo, a project loaded).
    loop.setToggleState (processor.getSong().hasLoop(), juce::dontSendNotification);
    const auto clickText = juce::String ("CLICK: ") + (processor.getSong().click == song::Song::Click::off ? "OFF"
                                                     : processor.getSong().click == song::Song::Click::always ? "ON" : "REC");
    if (click.getButtonText() != clickText)
        click.setButtonText (clickText);
    click.setToggleState (processor.getSong().click != song::Song::Click::off, juce::dontSendNotification);
}

void SongTrack::setLoop (double fromBeat, double toBeat)
{
    edit ([&] (song::Song& s)
    {
        s.loopStart = s.snap (juce::jmax (0.0, fromBeat));
        s.loopEnd = juce::jmax (s.snap (toBeat), s.loopStart + s.beatsPerBarAt (s.loopStart));
        s.loopOn = true;
    });
    startBeat = processor.getSong().loopStart;   // PLAY starts at the loop
    repaint();
}

void SongTrack::toggleLoop()
{
    const auto& s = processor.getSong();
    if (s.loopEnd <= s.loopStart + 1.0e-9)
    {
        // None set yet: the chosen block's bars, else four bars from PLAY's start.
        if (chosen >= 0 && chosen < (int) s.blocks.size())
            setLoop (s.blocks[(size_t) chosen].start, s.blocks[(size_t) chosen].end());
        else
            setLoop (startBeat, startBeat + 4.0 * s.beatsPerBarAt (startBeat));
        return;
    }
    const auto on = ! s.loopOn;
    edit ([on] (song::Song& song) { song.loopOn = on; });
    repaint();
}

juce::PopupMenu SongTrack::clickMenu()
{
    juce::PopupMenu menu;
    const auto now = processor.getSong().click;
    const auto set = [this] (song::Song::Click c) { edit ([c] (song::Song& s) { s.click = c; }); };
    menu.addSectionHeader ("THE CLICK");
    menu.addItem ("Off", true, now == song::Song::Click::off, [set] { set (song::Song::Click::off); });
    menu.addItem ("While Recording", true, now == song::Song::Click::recording, [set] { set (song::Song::Click::recording); });
    menu.addItem ("Always (while the song plays)", true, now == song::Song::Click::always, [set] { set (song::Song::Click::always); });
    return menu;
}

juce::PopupMenu SongTrack::rulerMenu (double atBeat)
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    const auto has = s.loopEnd > s.loopStart + 1.0e-9;
    menu.addSectionHeader ("THE LOOP AREA");
    menu.addItem ("Loop", has, s.loopOn, [this] { toggleLoop(); });
    const auto block = s.blockAt (atBeat);
    menu.addItem ("Loop This Block", block >= 0, false, [this, block]
    {
        const auto& song = processor.getSong();
        if (block >= 0 && block < (int) song.blocks.size())
            setLoop (song.blocks[(size_t) block].start, song.blocks[(size_t) block].end());
    });
    menu.addItem ("Clear the Loop Area", has, false, [this]
    {
        edit ([] (song::Song& song) { song.loopOn = false; song.loopStart = song.loopEnd = 0.0; });
        repaint();
    });
    menu.addSeparator();
    menu.addItem ("PLAY From Here", [this, atBeat]
    {
        startBeat = processor.getSong().snap (atBeat);
        if (processor.isSongPlaying())
            processor.playSong (startBeat);
        repaint();
    });
    return menu;
}

bool SongTrack::nearRightEdge (int block, juce::Point<float> p) const
{
    const auto& s = processor.getSong();
    return block >= 0 && block < (int) s.blocks.size() && std::abs (p.x - xOf (s.blocks[(size_t) block].end())) <= 6.0f;
}

void SongTrack::edit (const std::function<void (song::Song&)>& f)
{
    auto s = processor.getSong();
    f (s);
    processor.setSong (std::move (s));
}

void SongTrack::mouseMove (const juce::MouseEvent& e)
{
    const auto& s = processor.getSong();
    auto block = s.blockAt (beatAt (e.position.x));
    // Just past a block's end counts too (its edge is grabbed from either side).
    if (block < 0)
        block = s.blockAt (beatAt (e.position.x - 6.0f));
    setMouseCursor (lane.contains (e.getPosition()) && nearRightEdge (block, e.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                                                       : juce::MouseCursor::NormalCursor);
}

void SongTrack::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto& s = processor.getSong();
    if (ruler.contains (e.getPosition()))
    {
        // 0.44.0: right-click: the loop's menu; a drag: the loop area (set on release); a click: PLAY's start.
        if (e.mods.isPopupMenu())
        {
            rulerMenu (beatAt (e.position.x)).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        rulerFrom = beatAt (e.position.x);
        loopDraft.reset();
        return;
    }
    if (! lane.contains (e.getPosition()))
        return;
    if (view == View::tempo)
    {
        // A tempo change: dragged, or its menu.
        if (const auto index = tempoAt (e.position); index >= 0)
        {
            if (e.mods.isPopupMenu())
                tempoMenu (index).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            else
            {
                dragTempo = index;
                tempoDraft = s;
            }
        }
        return;
    }
    if (view == View::time)
    {
        if (e.mods.isPopupMenu())
            meterMenu (beatAt (e.position.x)).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    auto block = s.blockAt (beatAt (e.position.x));
    if (block < 0 && nearRightEdge (s.blockAt (beatAt (e.position.x - 6.0f)), e.position))
        block = s.blockAt (beatAt (e.position.x - 6.0f));
    chosen = block;
    repaint();
    if (block < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        showMenuFor (block, beatAt (e.position.x));
        return;
    }
    const auto& b = s.blocks[(size_t) block];
    drag = nearRightEdge (block, e.position) ? Drag::resize : Drag::move;
    grabOffset = beatAt (e.position.x) - b.start;
    dragBeat = drag == Drag::move ? b.start : b.end();
}

void SongTrack::mouseDrag (const juce::MouseEvent& e)
{
    if (rulerFrom >= 0.0)
    {
        if (e.getDistanceFromDragStart() >= 4)
        {
            const auto& s = processor.getSong();
            const auto a = juce::jmin (rulerFrom, beatAt (e.position.x)), b = juce::jmax (rulerFrom, beatAt (e.position.x));
            auto from = s.snap (a), to = s.snap (b);
            if (to <= from + 1.0e-9)
                to = from + s.beatsPerBarAt (from);
            loopDraft = std::make_pair (from, to);
            repaint();
        }
        return;
    }
    if (dragTempo >= 0 && tempoDraft.has_value())
    {
        // To the beat nearest, a whole BPM (between its neighbours: it can't pass them).
        *tempoDraft = processor.getSong();
        tempoDraft->moveTempo (dragTempo, std::round (beatAt (e.position.x)), std::round (bpmAtY (e.position.y)));
        repaint();
        return;
    }
    if (drag == Drag::none || e.getDistanceFromDragStart() < 3)
        return;
    dragBeat = drag == Drag::move ? beatAt (e.position.x) - grabOffset : beatAt (e.position.x);
    repaint();
}

void SongTrack::mouseUp (const juce::MouseEvent& e)
{
    if (rulerFrom >= 0.0)
    {
        const auto from = rulerFrom;
        rulerFrom = -1.0;
        if (auto draft = std::move (loopDraft); draft.has_value())
        {
            loopDraft.reset();
            setLoop (draft->first, draft->second);
        }
        else
        {
            startBeat = processor.getSong().snap (from);
            if (processor.isSongPlaying())
                processor.playSong (startBeat);
        }
        repaint();
        return;
    }
    if (dragTempo >= 0)
    {
        auto draft = std::move (tempoDraft);
        tempoDraft.reset();
        dragTempo = -1;
        if (draft.has_value() && e.getDistanceFromDragStart() >= 2 && ! (*draft == processor.getSong()))
            processor.setSong (std::move (*draft));
        repaint();
        return;
    }
    const auto was = drag;
    drag = Drag::none;
    if (was == Drag::none || e.getDistanceFromDragStart() < 3 || chosen < 0)
    {
        repaint();
        return;
    }
    const auto index = chosen;
    const auto to = dragBeat;
    if (was == Drag::move)
        edit ([&] (song::Song& s) { chosen = s.move (index, to); });
    else
        edit ([&] (song::Song& s) { s.resize (index, to - s.blocks[(size_t) index].start); });
    repaint();
}

bool SongTrack::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && chosen >= 0 && chosen < (int) processor.getSong().blocks.size())
    {
        const auto index = chosen;
        chosen = -1;
        edit ([index] (song::Song& s) { s.remove (index); });
        return true;
    }
    return false;
}

juce::PopupMenu SongTrack::menuFor (int block, double atBeat)
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    if (block < 0 || block >= (int) s.blocks.size())
        return menu;
    const auto& b = s.blocks[(size_t) block];
    menu.addSectionHeader (b.name);
    menu.addItem ("Duplicate", [this, block] { edit ([&] (song::Song& song) { chosen = song.duplicate (block); }); });
    menu.addItem ("Remove", [this, block]
    {
        chosen = -1;
        edit ([block] (song::Song& song) { song.remove (block); });
    });
    menu.addItem ("Mute", true, b.muted, [this, block] { edit ([block] (song::Song& song) { song.blocks[(size_t) block].muted = ! song.blocks[(size_t) block].muted; }); });
    juce::PopupMenu parts;
    for (int k = 0; k < grooves::partNames.size(); ++k)
    {
        juce::PopupMenu::Item item (grooves::partNames[k]);
        item.colour = song::colourOf ((grooves::Part) k);
        item.isTicked = (int) b.part == k;
        item.action = [this, block, k] { edit ([block, k] (song::Song& song) { song.blocks[(size_t) block].part = (grooves::Part) k; }); };
        parts.addItem (item);
    }
    menu.addSubMenu ("Song Part", parts);
    // CHANGE TEMPO: the block's notes closer together or further apart (its length too, so the blocks after it move).
    juce::PopupMenu tempo;
    for (const auto f : { grooves::TempoFactor::half, grooves::TempoFactor::twoThirds, grooves::TempoFactor::threeHalves, grooves::TempoFactor::twice })
        tempo.addItem (grooves::tempoFactorNames[(int) f], [this, block, f]
        {
            edit ([block, f] (song::Song& song)
            {
                song::changeTempo (song.blocks[(size_t) block], f);
                song.resize (block, song.blocks[(size_t) block].length);   // to whole bars, the blocks after it moved along
            });
        });
    menu.addSubMenu ("Change Tempo", tempo);

    // EDITING INSIDE IT (0.9.0): its drums by GROOVE PARTS group (each with how many notes it has there).
    const auto partsMenu = [] (const song::Block& of, const std::function<void (song::SlotMask slots)>& act, bool withAll)
    {
        juce::PopupMenu m;
        std::array<int, grooves::numDrumParts> counts {};
        for (const auto& h : of.hits)
            if (const auto part = grooves::drumPartOf (h.slot); part >= 0)
                ++counts[(size_t) part];
        if (withAll)
            m.addItem ("All Its Drums", ! of.hits.empty(), false, [act] { act (song::allSlots); });
        for (int k = 0; k < grooves::numDrumParts; ++k)
            if (counts[(size_t) k] > 0)
                m.addItem (grooves::drumPartNames[k] + " (" + juce::String (counts[(size_t) k]) + ")", [act, k] { act (song::slotsOf (1u << k)); });
        return m;
    };
    menu.addSeparator();
    menu.addItem ("Loop This Block", [this, block]
    {
        const auto& song = processor.getSong();
        if (block < (int) song.blocks.size())
            setLoop (song.blocks[(size_t) block].start, song.blocks[(size_t) block].end());
    });
    menu.addItem ("Edit Block...", onEditBlock != nullptr, false, [this, block] { if (onEditBlock != nullptr) onEditBlock (block); });
    menu.addItem ("Use with Song Creator", onUseWithCreator != nullptr && b.source.existsAsFile(), false,
                  [this, file = b.source] { onUseWithCreator (file); });
    menu.addItem ("Copy", [this, b] { processor.setSongClipboard (b); });
    const auto& clip = processor.getSongClipboard();
    menu.addItem ("Paste After", clip.has_value(), false, [this, block]
    {
        if (const auto& c = processor.getSongClipboard())
            edit ([&] (song::Song& song) { chosen = song.place (*c, song.blocks[(size_t) block].end()); });
    });
    if (clip.has_value())
        menu.addSubMenu ("Paste Drums Onto It", partsMenu (*clip, [this, block] (song::SlotMask slots)
        {
            if (const auto& c = processor.getSongClipboard())
                edit ([&] (song::Song& song) { song::pasteDrums (song.blocks[(size_t) block], *c, slots); });
        }, true));
    else
        menu.addItem ("Paste Drums Onto It", false, false, nullptr);
    menu.addSubMenu ("Remove Drums", partsMenu (b, [this, block] (song::SlotMask slots)
    {
        edit ([&] (song::Song& song) { song::removeDrums (song.blocks[(size_t) block], slots); });
    }, false), ! b.hits.empty());
    juce::PopupMenu grid;
    for (int q = 0; q < grooves::quantizeNames.size(); ++q)
        grid.addItem (grooves::quantizeNames[q], [this, block, q] { edit ([&] (song::Song& song) { song::quantize (song.blocks[(size_t) block], grooves::gridOf (q)); }); });
    menu.addSubMenu ("Quantize", grid, ! b.hits.empty());
    juce::PopupMenu velocity;
    for (const auto percent : { 50, 75, 90, 110, 125, 150 })
        velocity.addItem (juce::String (percent) + "%", [this, block, percent]
        {
            edit ([&] (song::Song& song) { song::scaleVelocity (song.blocks[(size_t) block], (float) percent / 100.0f); });
        });
    menu.addSubMenu ("Velocity", velocity, ! b.hits.empty());
    const auto splitAt = atBeat >= 0.0 ? atBeat : b.start + b.length * 0.5;
    auto trial = s;
    menu.addItem ("Split Here", trial.split (block, splitAt) >= 0, false, [this, block, splitAt]
    {
        edit ([&] (song::Song& song) { song.split (block, splitAt); });
    });
    menu.addItem ("Merge With Next", block + 1 < (int) s.blocks.size(), false, [this, block]
    {
        edit ([&] (song::Song& song) { chosen = song.mergeWithNext (block); });
    });
    return menu;
}

void SongTrack::showMenuFor (int block, double atBeat)
{
    menuFor (block, atBeat).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void SongTrack::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! lane.contains (e.getPosition()))
        return;
    if (view == View::tempo)
    {
        // A tempo change on the nearest beat, at the tempo where it's clicked.
        if (tempoAt (e.position) < 0)
        {
            const auto beat = std::round (beatAt (e.position.x));
            const auto bpm = std::round (bpmAtY (e.position.y));
            edit ([beat, bpm] (song::Song& s) { s.addTempo (beat, bpm); });
        }
        return;
    }
    if (view == View::time)
    {
        meterMenu (beatAt (e.position.x)).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    if (const auto block = processor.getSong().blockAt (beatAt (e.position.x)); block >= 0 && onEditBlock != nullptr)
    {
        chosen = block;
        onEditBlock (block);
    }
}

//==============================================================================
bool SongTrack::isInterestedInDragSource (const SourceDetails& details)
{
    const auto d = details.description.toString();
    return d.startsWith (groovePrefix) || d.startsWith (creatorPrefix);
}

void SongTrack::itemDragMove (const SourceDetails& details)
{
    dropBeat = lane.contains (details.localPosition) ? beatAt ((float) details.localPosition.x) : -1.0;
    repaint();
}

void SongTrack::itemDragExit (const SourceDetails&)
{
    dropBeat = -1.0;
    repaint();
}

void SongTrack::itemDropped (const SourceDetails& details)
{
    dropBeat = -1.0;
    const auto d = details.description.toString();
    const auto beat = beatAt ((float) details.localPosition.x);
    if (d.startsWith (creatorPrefix))
    {
        const auto f = juce::StringArray::fromTokens (d.fromFirstOccurrenceOf (creatorPrefix, false, false), ":", {});
        if (onCreatorDropped != nullptr && f.size() == 2)
            onCreatorDropped (f[0].getIntValue(), f[1].getIntValue(), beat);
    }
    else if (onGrooveDropped != nullptr)
        onGrooveDropped (d.fromFirstOccurrenceOf (groovePrefix, false, false).getIntValue(), beat);
    repaint();
}

bool SongTrack::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), isMidiFile);
}

void SongTrack::fileDragMove (const juce::StringArray&, int x, int y)
{
    dropBeat = lane.contains (x, y) ? beatAt ((float) x) : -1.0;
    repaint();
}

void SongTrack::fileDragExit (const juce::StringArray&)
{
    dropBeat = -1.0;
    repaint();
}

void SongTrack::filesDropped (const juce::StringArray& files, int x, int)
{
    // Each MIDI file a block (its notes through the kit's own map: a file from outside the groove folders has no
    // FILES' MAP), one after another from the drop.
    dropBeat = -1.0;
    auto beat = beatAt ((float) x);
    const auto map = processor.getNoteMap();
    for (const auto& path : files)
        if (isMidiFile (path))
            if (const auto g = grooves::read (juce::File (path), map))
            {
                const auto index = processor.addBlockToSong (song::blockFrom (*g, map), beat);
                chosen = index;
                beat = processor.getSong().blocks[(size_t) index].end();
            }
    repaint();
}

//==============================================================================
void SongTrack::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (chosen >= (int) processor.getSong().blocks.size())
        chosen = -1;
    syncScroll();
    updateStatus();
    repaint();
}

void SongTrack::timerCallback()
{
    // A note's time up: the usual line again. FOLLOW DAW as the song has it (undo, a project loaded).
    if (note.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() >= noteUntil)
    {
        note.clear();
        updateStatus();
    }
    follow.setToggleState (processor.getSong().followDaw, juce::dontSendNotification);
    syncButtons();
    // RECORD: the notes played since, into the song (and REC as the processor has it).
    if (processor.isSongRecording() && processor.mergeRecordedNotes() > 0)
        updateStatus();
    if (record.getToggleState() != processor.isSongRecording())
    {
        record.setToggleState (processor.isSongRecording(), juce::dontSendNotification);
        repaint();   // the lane's red frame
    }
    // BOUNCE: how far; done: what it wrote.
    if (bounceThread != nullptr)
    {
        if (bounceThread->isThreadRunning())
            bounceNote = "BOUNCING... " + juce::String (juce::roundToInt (bounceThread->bouncer.progress.load() * 100.0f)) + "%";
        else
        {
            const auto files = bounceThread->files;
            bounceNote = files.isEmpty() ? "Bounce: " + bounceThread->bouncer.error
                                         : "Bounced " + juce::String (files.size()) + (files.size() == 1 ? " file" : " files") + " to "
                                               + files[0].getParentDirectory().getFullPathName();
            bounceThread.reset();
        }
        updateStatus();
    }
    play.setButtonText (processor.isSongPlaying() ? "STOP" : "PLAY");
    if (const auto at = processor.getSongPosition(); at != shownPosition)
    {
        // 0.43.0: playing past the lane's end (or before its start): the next page of the song.
        if (at >= 0.0 && (at < viewStart || at >= viewStart + beatsShown()) && drag == Drag::none)
        {
            const auto bar = processor.getSong().beatsPerBar();
            setViewStart (std::floor (at / bar) * bar);
            shownPosition = at;
            return;
        }
        // 0.40.0: only the line's old and new places drawn again (the whole strip, every hit of every block, redrew 30
        // times a second while the song played).
        for (const auto beat : { shownPosition, at })
            if (beat >= 0.0)
                repaint (juce::Rectangle<int> ((int) std::floor (xOf (beat)) - 3, 0, 7, getHeight()));
        shownPosition = at;
    }
}

//==============================================================================
// TEMPO AND TIME (0.11.0)
namespace
{
    constexpr double lowestBpm = 40.0, highestBpm = 240.0;   // the tempo view's range, bottom to top
}

void SongTrack::setView (View v)
{
    view = v;
    blocksView.setToggleState (v == View::blocks, juce::dontSendNotification);
    tempoView.setToggleState (v == View::tempo, juce::dontSendNotification);
    timeView.setToggleState (v == View::time, juce::dontSendNotification);
    dragTempo = -1;
    tempoDraft.reset();
    updateStatus();
    repaint();
}

juce::Point<float> SongTrack::pointOf (const song::TempoPoint& t) const
{
    const auto top = (float) lane.getY() + 18.0f, bottom = (float) lane.getBottom() - 8.0f;
    const auto y = bottom - (bottom - top) * (float) ((juce::jlimit (lowestBpm, highestBpm, t.bpm) - lowestBpm) / (highestBpm - lowestBpm));
    return { xOf (t.beat), y };
}

double SongTrack::bpmAtY (float y) const noexcept
{
    const auto top = (float) lane.getY() + 18.0f, bottom = (float) lane.getBottom() - 8.0f;
    return juce::jlimit (lowestBpm, highestBpm, lowestBpm + (highestBpm - lowestBpm) * (double) ((bottom - y) / juce::jmax (1.0f, bottom - top)));
}

int SongTrack::tempoAt (juce::Point<float> p) const
{
    const auto& s = processor.getSong();
    for (int i = 0; i < (int) s.tempos.size(); ++i)
        if (pointOf (s.tempos[(size_t) i]).getDistanceFrom (p) <= 8.0f)
            return i;
    return -1;
}

juce::PopupMenu SongTrack::tempoMenu (int index)
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    if (index < 0 || index >= (int) s.tempos.size())
        return menu;
    const auto& t = s.tempos[(size_t) index];
    menu.addSectionHeader (juce::String (juce::roundToInt (t.bpm)) + " BPM from bar " + juce::String (s.barAt (t.beat) + 1)
                           + ", beat " + juce::String (t.beat - s.barLines (t.beat)[(size_t) s.barAt (t.beat)] + 1.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."));
    menu.addItem ("Ramp to the Next", index + 1 < (int) s.tempos.size(), t.ramp, [this, index]
    {
        edit ([index] (song::Song& song) { song.tempos[(size_t) index].ramp = ! song.tempos[(size_t) index].ramp; });
    });
    juce::PopupMenu set;
    for (const auto bpm : { 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 180 })
        set.addItem (juce::String (bpm) + " BPM", true, juce::roundToInt (t.bpm) == bpm, [this, index, bpm]
        {
            edit ([index, bpm] (song::Song& song) { song.moveTempo (index, song.tempos[(size_t) index].beat, bpm); });
        });
    menu.addSubMenu ("Tempo", set);
    menu.addItem ("Remove", [this, index] { edit ([index] (song::Song& song) { song.removeTempo (index); }); });
    return menu;
}

juce::PopupMenu SongTrack::meterMenu (double beat)
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    const auto at = s.snap (beat);
    const auto m = s.meterAt (at);
    menu.addSectionHeader ("From bar " + juce::String (s.barAt (at) + 1) + ": " + juce::String (m.numerator) + "/" + juce::String (m.denominator));
    juce::PopupMenu beats, length;
    for (int n = 1; n <= 15; ++n)
        beats.addItem (juce::String (n), true, n == m.numerator, [this, at, n, den = m.denominator]
        {
            edit ([at, n, den] (song::Song& song) { song.setMeter (at, n, den); });
        });
    for (const auto d : { 2, 4, 8, 16 })
        length.addItem (juce::String (d) + (d == 2 ? " (half notes)" : d == 4 ? " (quarter notes)" : d == 8 ? " (eighth notes)" : " (16th notes)"),
                        true, d == m.denominator, [this, at, d, num = m.numerator]
        {
            edit ([at, d, num] (song::Song& song) { song.setMeter (at, num, d); });
        });
    menu.addSubMenu ("Beats a Bar", beats);
    menu.addSubMenu ("Beat Length", length);
    const auto changeHere = std::any_of (s.meters.begin(), s.meters.end(), [at] (const song::Meter& x) { return std::abs (x.beat - at) < 1.0e-9; });
    menu.addItem ("Remove This Change", changeHere, false, [this, at] { edit ([at] (song::Song& song) { song.removeMeter (at); }); });
    return menu;
}

juce::PopupMenu SongTrack::trackMenu()
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    menu.addItem ("Import MIDI File...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file as the song (or after it)", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
        {
            const auto file = c.getResult();
            if (! file.existsAsFile())
                return;
            const auto options = juce::MessageBoxOptions()
                                     .withTitle ("Import " + file.getFileName())
                                     .withMessage ("Use the file's tempo and time signatures? (Without them the song keeps its own, "
                                                   "or your DAW's.)")
                                     .withButton ("With Them")
                                     .withButton ("Without")
                                     .withButton ("Cancel")
                                     .withAssociatedComponent (this);
            juce::AlertWindow::showAsync (options, [this, file] (int result)
            {
                if (result == 1 || result == 2)
                    importMidi (file, result == 1);
            });
        });
    });
    menu.addItem ("Bounce to Audio...", ! s.isEmpty() && bounceThread == nullptr, false, [this] { askBounce(); });
    menu.addSeparator();
    menu.addItem ("Remove All Tempo Changes (the DAW's tempo)", ! s.tempos.empty(), false, [this] { edit ([] (song::Song& song) { song.tempos.clear(); }); });
    menu.addItem ("Remove All Time Signature Changes", ! s.meters.empty(), false, [this] { edit ([] (song::Song& song) { song.meters.clear(); }); });
    return menu;
}

void SongTrack::importMidi (const juce::File& file, bool withTempo)
{
    if (processor.importSongMidi (file, withTempo) > 0)
        chosen = -1;
    updateStatus();
    repaint();
}

//==============================================================================
// BOUNCE (0.12.0)

void SongTrack::startBounce (const ParagonX1AudioProcessor::BounceOptions& options, const juce::File& folder)
{
    if (bounceThread != nullptr)
        return;
    bounceNote = "BOUNCING... 0%";
    bounceThread = std::make_unique<BounceThread> (processor, options, folder);
    bounceThread->startThread();
    updateStatus();
}

void SongTrack::askBounce()
{
    // What to bounce (one file, or one per output used), the bits, the gain; then where.
    auto window = std::make_shared<juce::AlertWindow> ("Bounce the song to audio",
                                                       "WAV files at the plugin's sample rate, from bar 1 through the song's tempo "
                                                       "changes, until the drums and effects have died away.",
                                                       juce::MessageBoxIconType::NoIcon, this);
    window->addComboBox ("what", { "One file: the whole kit", "A file per output, as routed (MAIN, OUT 2-16)" }, "Files");
    window->addComboBox ("bits", { "24-bit", "16-bit", "32-bit float" }, "Bits");
    window->addTextEditor ("gain", "0", "Gain (dB)");
    window->addButton ("Bounce...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result != 1)
            return;
        ParagonX1AudioProcessor::BounceOptions o;
        o.perOutput = window->getComboBoxComponent ("what")->getSelectedItemIndex() == 1;
        o.bits = std::array<int, 3> { 24, 16, 32 }[(size_t) juce::jlimit (0, 2, window->getComboBoxComponent ("bits")->getSelectedItemIndex())];
        o.gainDb = juce::jlimit (-48.0f, 24.0f, window->getTextEditorContents ("gain").getFloatValue());
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the bounced files", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, o] (const juce::FileChooser& c)
        {
            if (const auto folder = c.getResult(); folder != juce::File())
                startBounce (o, folder);
        });
    }));
}

void SongTrack::showNote (const juce::String& text, double seconds)
{
    note = text;
    noteUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    updateStatus();
}
