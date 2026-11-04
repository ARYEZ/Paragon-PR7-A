// THE SONG TRACK's strip (PR7-D 0.8.0): the song's blocks in a row, bar by bar, where the on-screen keyboard is (KEYS |
// SONG beside it switches). Our own design, inspired by the song track of commercial drum software.
//
// Drag a groove from the GROOVES list (or a MIDI file) onto it: a block. Drag a block to move it; drag its right edge to
// make it longer (the groove repeats) or shorter. Click a block to choose it (DELETE removes it); right-click for
// DUPLICATE, REMOVE, MUTE, SONG PART and CHANGE TEMPO. Click the bar numbers to put PLAY's start there. While the DAW
// plays, the song plays with it (bar 1 is the DAW's bar 1); PLAY plays it with the DAW stopped. EXPORT MIDI: the song
// as one MIDI file (click to save, drag to a DAW track).
#pragma once

#include "PluginProcessor.h"
#include "ui/DrumSeqPage.h"

class SongTrack final : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener,
                        private juce::Timer
{
public:
    explicit SongTrack (ParagonX1AudioProcessor&);
    ~SongTrack() override;

    // A groove dragged from the GROOVES list ("groove:<row>") dropped at a beat: MainView asks the page for it.
    std::function<void (int row, double beat)> onGrooveDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // For tests and snapshots.
    int getChosen() const noexcept { return chosen; }
    void choose (int block) { chosen = block; repaint(); }
    double beatAt (float x) const noexcept;      // on the lane, not snapped
    float xOf (double beat) const noexcept;
    juce::Rectangle<int> getLane() const noexcept { return lane; }
    void showMenuFor (int block, double atBeat = -1.0);   // as right-clicking it (at that beat: SPLIT HERE's place)
    juce::PopupMenu menuFor (int block, double atBeat = -1.0);
    // EDIT BLOCK... (0.9.0), or a double-click on a block: MainView opens the block editor on it.
    std::function<void (int block)> onEditBlock;
    // THE SONG CREATOR (0.10.0): CREATOR opens it; a groove dragged from its columns ("creator:<part>:<index>") dropped at
    // a beat; USE WITH SONG CREATOR on a block (its groove's file).
    std::function<void()> onOpenCreator;
    std::function<void (int part, int index, double beat)> onCreatorDropped;
    std::function<void (const juce::File&)> onUseWithCreator;
    juce::TextButton& getCreatorButton() noexcept { return creator; }

    // TEMPO AND TIME (0.11.0): the strip shows the BLOCKS (to arrange), the TEMPO (the song's tempo changes as a line over
    // them: double-click adds one, drag one to move it, right-click for RAMP TO THE NEXT / REMOVE) or the TIME (the
    // time signature changes: double-click or right-click a bar to set its time signature). TRACK...: IMPORT MIDI FILE...,
    // REMOVE ALL TEMPO CHANGES, REMOVE ALL TIME SIGNATURE CHANGES.
    enum class View { blocks, tempo, time };
    void setView (View);
    View getView() const noexcept { return view; }
    juce::PopupMenu trackMenu();
    juce::PopupMenu tempoMenu (int index);
    juce::PopupMenu meterMenu (double beat);
    int tempoAt (juce::Point<float>) const;          // the tempo change drawn there (-1: none)
    juce::Point<float> pointOf (const song::TempoPoint&) const;
    double bpmAtY (float y) const noexcept;
    void importMidi (const juce::File&, bool withTempo);

    // RECORD and BOUNCE (0.12.0): REC arms recording (the notes played while the song plays go into it); TRACK... > BOUNCE
    // TO AUDIO... renders the song to WAV files (on a thread of its own: the plugin plays on; the status line shows how
    // far), in the folder chosen.
    juce::TextButton& getRecordButton() noexcept { return record; }
    // FOLLOW DAW (0.12.0): the song plays with the DAW (on) or only with PLAY (off); after EXPORT MIDI with it on, a
    // reminder in the status line (the song on a DAW track as well would play every hit twice).
    juce::TextButton& getFollowButton() noexcept { return follow; }
    // THE LOOP AREA and THE CLICK (0.44.0): LOOP turns PLAY's loop on or off (with none set yet: the chosen block's bars,
    // else four bars from PLAY's start); drag along the bar numbers to set it; right-click them, or a block (LOOP THIS
    // BLOCK), for more. CLICK...: the metronome while the song plays: off, while REC is armed, or always.
    juce::TextButton& getLoopButton() noexcept { return loop; }
    juce::TextButton& getClickButton() noexcept { return click; }
    void toggleLoop();
    void setLoop (double fromBeat, double toBeat);   // on bar lines, on (one undo step)
    juce::PopupMenu clickMenu();
    juce::PopupMenu rulerMenu (double atBeat);
    void syncButtons();   // LOOP and CLICK as the song has them (the timer's; snapshots)
    juce::String getStatusText() const { return status.getText(); }
    void startBounce (const ParagonX1AudioProcessor::BounceOptions&, const juce::File& folder);
    bool isBouncing() const noexcept { return bounceThread != nullptr; }
    // 0.40.0, for the progress box: how far the bounce has got (-1: none running), and stopping it.
    double getBounceProgress() const;
    void cancelBounce();
    double getStartBeat() const noexcept { return startBeat; }

    // 0.43.0: a long song scrolls. The lane shows up to `barsShown` bars (32 at first; Ctrl / Cmd + the wheel zooms,
    // 4 to 256); the scroll bar under it, or the wheel, moves along; while the song plays the view follows it.
    double totalBeats() const noexcept;   // the song and two bars after it, at least 16 bars, to a bar line
    double getViewStart() const noexcept { return viewStart; }
    void setViewStart (double beat);
    void setBarsShown (double bars);
    double getBarsShown() const noexcept { return barsShown; }
    juce::ScrollBar& getScrollBar() noexcept { return scroll; }
    juce::TextButton& getPlayButton() noexcept { return play; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void syncScroll();
    void edit (const std::function<void (song::Song&)>&);   // the song changed by `f`, as an undo step
    bool nearRightEdge (int block, juce::Point<float>) const;
    double beatsShown() const noexcept;
    void updateStatus();

    ParagonX1AudioProcessor& processor;
    juce::TextButton play { "PLAY" };
    MidiExportButton exportButton;
    juce::TextButton creator { "CREATOR..." }, track { "TRACK..." }, blocksView { "BLOCKS" }, tempoView { "TEMPO" }, timeView { "TIME" };
    View view = View::blocks;
    int dragTempo = -1;                       // a tempo change being dragged
    std::optional<song::Song> tempoDraft;     // ... the song as it would be (drawn while dragging)
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton record { "REC" }, follow { "FOLLOW DAW" }, loop { "LOOP" }, click { "CLICK..." };
    double rulerFrom = -1.0;                      // a drag along the bar numbers: where it began (-1: none)
    std::optional<std::pair<double, double>> loopDraft;   // ... and the loop it would set
    juce::String note;          // a few seconds' note in the status line
    double noteUntil = 0.0;
    void showNote (const juce::String&, double seconds);
    struct BounceThread;
    std::unique_ptr<BounceThread> bounceThread;
    juce::String bounceNote;   // the last bounce's result, for the status line
    void askBounce();
    juce::Label status;
    juce::Rectangle<int> lane, ruler;
    int chosen = -1;
    double startBeat = 0.0;   // where PLAY starts (the bar numbers clicked)
    // A block being moved or made longer, and where it would go.
    enum class Drag { none, move, resize } drag = Drag::none;
    double grabOffset = 0.0, dragBeat = 0.0;
    double dropBeat = -1.0;   // a groove or file over the lane: where it would go (-1: none)
    double shownPosition = -2.0;
    double viewStart = 0.0, barsShown = 32.0;
    juce::ScrollBar scroll { false };
};
