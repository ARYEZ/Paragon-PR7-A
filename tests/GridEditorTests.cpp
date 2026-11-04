// THE GRID EDITOR (0.47.0): the rows, the tools, choosing notes, moving and copying them, the velocity lane, the
// timing and velocity sliders, the SELECT menu, nudge, stretch, row SOLO / MUTE, the keys; after section 7.3.2 of the
// manual the owner sent.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

#include "Parameters.h"
#include "song/PlayStyle.h"
#include "ui/BlockEditor.h"
#include "ui/MainView.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;
    constexpr int kickPiece = 0, snarePiece = 1, hatPiece = 2;
    using S = BlockEditor::Slider;

    struct Plugin
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonGridEditorTests");
        ParagonX1AudioProcessor p;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            testkit::useSample (p, BD, testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate));
            testkit::useSample (p, SD, testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.25f, 1, sampleRate));
        }
        ~Plugin() { folder.deleteRecursively(); }

        std::vector<float> play (double seconds)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer none;
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
            {
                buffer.clear();
                p.processBlock (buffer, none);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };

    // One 4/4 bar: kick on 1 and 3, snare on 2 and 4 (and a rimshot on 4.5), closed hats on the eighths.
    song::Song oneBar()
    {
        song::Song s;
        song::Block b;
        b.name = "Beat";
        b.length = b.loopLength = 4.0;
        b.hits = { { 0.0, BD, 1.0f }, { 2.0, BD, 0.8f }, { 1.0, SD, 0.9f }, { 3.0, SD, 0.7f }, { 3.5, SD2, 0.6f } };
        for (int e = 0; e < 8; ++e)
            b.hits.push_back ({ e * 0.5, CH, e % 2 == 0 ? 0.7f : 0.4f });
        song::tidyHits (b);
        s.place (b, 0.0);
        return s;
    }

    std::vector<double> beatsOf (const song::Block& b, int slot)
    {
        std::vector<double> out;
        for (const auto& h : b.hits)
            if (h.slot == slot)
                out.push_back (h.beat);
        return out;
    }

    float velocityAt (const song::Block& b, int slot, double beat)
    {
        for (const auto& h : b.hits)
            if (h.slot == slot && std::abs (h.beat - beat) < 1.0e-6)
                return h.velocity;
        return -1.0f;
    }

    struct Editor
    {
        Plugin t;
        BlockEditor ed { t.p };
        Editor()
        {
            t.p.setSong (oneBar());
            t.p.flushUndoStep();
            ed.setBounds (0, 0, 1476, 672);
            ed.open (0);
            ed.setResolution (3);   // 1/16
        }
        const song::Block& block() const { return t.p.getSong().blocks[0]; }
        juce::MouseEvent mouse (juce::Point<float> at, juce::Point<float> from, juce::ModifierKeys mods, int clicks = 1)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &ed, &ed, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), clicks, false);
        }
        void dragFrom (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            ed.mouseDown (mouse (from, from, mods));
            ed.mouseDrag (mouse (from + (to - from) * 0.5f, from, mods));
            ed.mouseDrag (mouse (to, from, mods));
            ed.mouseUp (mouse (to, from, mods));
            t.p.flushUndoStep();
        }
        void click (juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            ed.mouseDown (mouse (at, at, mods));
            ed.mouseUp (mouse (at, at, mods));
            t.p.flushUndoStep();
        }
        float pxPerBeat() const { return ed.cellOf (BD, 1.0).getCentreX() - ed.cellOf (BD, 0.0).getCentreX(); }
    };
}

TEST_CASE ("GRID EDITOR 0.47.0: a row per instrument, unfolding to its articulations; reordered, shown or hidden", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    const auto& rows = ed.getRows();
    REQUIRE (rows.size() >= 3);
    CHECK (rows[0] == BlockEditor::Row { kickPiece, -1, false });
    CHECK (rows[1] == BlockEditor::Row { snarePiece, -1, false });
    CHECK (rows[2] == BlockEditor::Row { hatPiece, -1, false });
    // Folded: the rimshot on the snare's row with the centre hits.
    CHECK (ed.rowOf (SD2) == ed.rowOf (SD));
    // The snare unfolded: a heading, then its articulations (the centre, the rimshot...) each on its own row.
    ed.setExpanded (snarePiece, true);
    CHECK (ed.getRows()[1].heading);
    CHECK (ed.rowOf (SD) == 2);
    CHECK (ed.rowOf (SD2) > ed.rowOf (SD));
    CHECK (ed.getRows()[(size_t) ed.rowOf (SD2)].slot == SD2);
    ed.setAllExpanded (false);
    CHECK (ed.rowOf (SD2) == ed.rowOf (SD));
    // Dragged: the hi-hat above the kick; RESET ORDER puts it back.
    ed.moveRow (hatPiece, 0);
    CHECK (ed.getRows()[0].piece == hatPiece);
    ed.resetOrder();
    CHECK (ed.getRows()[0].piece == kickPiece);
    // A drum turned off and with no notes in the block: hidden until SHOW UNMAPPED.
    auto* on = e.t.p.getParameters().getParameter (ParamIDs::drum (TMB, "On"));   // the tambourine
    REQUIRE (on != nullptr);
    on->setValueNotifyingHost (0.0f);
    ed.setShowAll (false);
    const auto shown = [&ed] (int piece)
    {
        return std::any_of (ed.getRows().begin(), ed.getRows().end(), [piece] (const BlockEditor::Row& r) { return r.piece == piece; });
    };
    CHECK_FALSE (shown (12));
    ed.setShowAll (true);
    CHECK (shown (12));
}

TEST_CASE ("GRID EDITOR 0.47.0: click and lasso choose notes; dragged along the beat on the grid, onto another row, ALT copies; one undo step each", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    const auto left = juce::ModifierKeys::leftButtonModifier;
    const auto shift = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    const auto px = e.pxPerBeat();
    REQUIRE (px > 50.0f);

    // A click chooses one; SHIFT-click another adds it.
    e.click (ed.cellOf (BD, 0.0).getCentre());
    CHECK (ed.getSelection().size() == 1);
    e.click (ed.cellOf (SD, 1.0).getCentre(), shift);
    CHECK (ed.getSelection().size() == 2);
    // A lasso over the kick and snare rows from beat 1.9 to 3.2: the kick on 3 and the snare on 4.
    const auto from = juce::Point<float> (ed.cellOf (BD, 1.9).getCentreX(), ed.cellOf (BD, 0.0).getY() - 3.0f);
    const auto to = juce::Point<float> (ed.cellOf (SD, 3.2).getCentreX(), ed.cellOf (SD, 0.0).getBottom() + 3.0f);
    e.dragFrom (from, to);
    auto chosen = ed.getSelection();
    REQUIRE (chosen.size() == 2);
    CHECK (chosen[0].slot == BD);
    CHECK (chosen[0].beat == 2.0);
    CHECK (chosen[1].slot == SD);
    CHECK (chosen[1].beat == 3.0);

    // Dragged 0.3 beats right: on the 1/16 grid, a quarter of a beat later (the kick to 2.25, the snare to 3.25).
    const auto kick = ed.cellOf (BD, 2.0).getCentre();
    e.dragFrom (kick, kick.translated (0.3f * px, 0.0f));
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.25 });
    CHECK (std::find (beatsOf (e.block(), SD).begin(), beatsOf (e.block(), SD).end(), 3.25) != beatsOf (e.block(), SD).end());
    CHECK (velocityAt (e.block(), BD, 2.25) == 0.8f);
    e.t.p.undo();
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.0 });

    // ALT-dragged: copied, the originals stay.
    e.click (ed.cellOf (BD, 2.0).getCentre());
    e.dragFrom (ed.cellOf (BD, 2.0).getCentre(), ed.cellOf (BD, 2.0).getCentre().translated (1.0f * px, 0.0f), alt);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.0, 3.0 });
    // Dragged a row down: the kick on 3 played by the snare (its first articulation, the centre).
    e.click (ed.cellOf (BD, 3.0).getCentre());
    e.dragFrom (ed.cellOf (BD, 3.0).getCentre(), ed.cellOf (BD, 3.0).getCentre().translated (0.0f, 22.0f), left);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.0 });
    CHECK (velocityAt (e.block(), SD, 3.0) == 0.8f);   // the kick's velocity, replacing the snare that was there
}

TEST_CASE ("GRID EDITOR 0.47.0: the PENCIL draws along a row on the resolution; CTRL + ALT gives it to the pointer; double-click adds", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    const auto px = e.pxPerBeat();
    ed.setResolution (2);   // 1/8
    ed.setTool (BlockEditor::Tool::pencil);
    // Along the kick's row from 0.6 to 1.9 beats: kicks on 0.5, 1 and 1.5 (each eighth crossed, the first one under it).
    const auto y = ed.cellOf (BD, 0.0).getCentreY();
    const auto x0 = ed.cellOf (BD, 0.6).getCentreX();
    e.dragFrom ({ x0, y + 6.0f }, { x0 + 1.3f * px, y + 6.0f });
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 0.5, 1.0, 1.5, 2.0 });
    e.t.p.undo();
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.0 });

    // The pointer with CTRL + ALT held: the pencil.
    ed.setTool (BlockEditor::Tool::pointer);
    const auto ctrlAlt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier);
    e.click ({ ed.cellOf (BD, 3.1).getCentreX(), y + 6.0f }, ctrlAlt);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 2.0, 3.0 });
    // A double-click on empty space: a note there, on the 1/8.
    const auto at = juce::Point<float> (ed.cellOf (SD, 2.45).getCentreX(), ed.cellOf (SD, 0.0).getCentreY() + 7.0f);
    ed.mouseDoubleClick (e.mouse (at, at, juce::ModifierKeys::leftButtonModifier, 2));
    CHECK (velocityAt (e.block(), SD, 2.5) == 0.8f);
}

TEST_CASE ("GRID EDITOR 0.47.0: the velocity lane: a stalk dragged to a level, the other chosen with it; the pencil draws levels", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    const auto lane = ed.getLane().toFloat();
    const auto levelY = [&lane] (float v) { return lane.getBottom() - 3.0f - (lane.getHeight() - 8.0f) * v; };
    // The kick on 1 dragged down to half.
    const auto p = ed.velocityPointOf (BD, 0.0).getCentre();
    e.dragFrom (p, { p.x, levelY (0.5f) });
    CHECK_THAT (velocityAt (e.block(), BD, 0.0), WithinAbs (0.5, 0.01));
    // Both snares chosen, one dragged up by 0.1: both up 0.1.
    ed.selectRow (ed.rowOf (SD), false);
    ed.select (SD2, 3.5, true);   // (the row's rimshot taken back out)
    const auto s = ed.velocityPointOf (SD, 1.0).getCentre();
    e.dragFrom (s, { s.x, levelY (1.0f) });
    CHECK_THAT (velocityAt (e.block(), SD, 1.0), WithinAbs (1.0, 0.01));
    CHECK_THAT (velocityAt (e.block(), SD, 3.0), WithinAbs (0.8, 0.01));
    CHECK_THAT (velocityAt (e.block(), SD2, 3.5), WithinAbs (0.6, 1.0e-6));
    // The pencil across the lane over the kick on 3: drawn to 0.25.
    ed.setTool (BlockEditor::Tool::pencil);
    const auto k = ed.velocityPointOf (BD, 2.0).getCentre();
    e.dragFrom ({ k.x - 2.0f, levelY (0.25f) }, { k.x + 2.0f, levelY (0.25f) });
    CHECK_THAT (velocityAt (e.block(), BD, 2.0), WithinAbs (0.25, 0.01));
}

TEST_CASE ("GRID EDITOR 0.47.0: QUANTIZE %, SWING and RANDOMIZE move the chosen notes from where they were; back at 0 they're back", "[grideditor]")
{
    Editor e;
    auto& ed = e.ed;
    // Two late snares.
    auto s = e.t.p.getSong();
    s.blocks[0].hits = { { 1.04, SD, 0.9f }, { 3.08, SD, 0.7f } };
    e.t.p.setSong (s);
    e.t.p.flushUndoStep();
    ed.selectAll();
    ed.setSliderValue (S::quantize, 50.0);
    CHECK_THAT (beatsOf (e.block(), SD)[0], WithinAbs (1.02, 1.0e-9));
    CHECK_THAT (beatsOf (e.block(), SD)[1], WithinAbs (3.04, 1.0e-9));
    ed.setSliderValue (S::quantize, 100.0);   // (MAX)
    CHECK (beatsOf (e.block(), SD) == std::vector<double> { 1.0, 3.0 });
    ed.setSliderValue (S::quantize, 0.0);
    CHECK_THAT (beatsOf (e.block(), SD)[0], WithinAbs (1.04, 1.0e-9));
    // One undo step back to where they started.
    ed.setSliderValue (S::quantize, 100.0);
    e.t.p.flushUndoStep();
    e.t.p.undo();
    CHECK_THAT (beatsOf (e.block(), SD)[1], WithinAbs (3.08, 1.0e-9));

    // SWING 100 on eighth hats at 1/8: every second eighth a sixth of a beat later (the triplet swing).
    e.t.p.setSong (oneBar());
    e.t.p.flushUndoStep();
    ed.selectRow (ed.rowOf (CH), false);
    ed.setResolution (2);
    ed.setSliderValue (S::swing, 100.0);
    const auto hats = beatsOf (e.block(), CH);
    REQUIRE (hats.size() == 8);
    for (int k = 0; k < 8; ++k)
        CHECK_THAT (hats[(size_t) k], WithinAbs (k * 0.5 + (k % 2 == 1 ? 1.0 / 6.0 : 0.0), 1.0e-9));
    ed.setSliderValue (S::swing, -50.0);
    CHECK_THAT (beatsOf (e.block(), CH)[1], WithinAbs (0.5 - 1.0 / 12.0, 1.0e-9));
    ed.setSliderValue (S::swing, 0.0);
    // RANDOMIZE: each note within a third of the resolution, not all the same way; the same setting gives the same notes.
    ed.setSliderValue (S::randomize, 100.0);
    const auto loose = beatsOf (e.block(), CH);
    auto moved = 0;
    for (int k = 0; k < 8; ++k)
    {
        const auto off = std::remainder (loose[(size_t) k] - std::round (loose[(size_t) k] * 2.0) / 2.0, 4.0);
        CHECK (std::abs (off) <= 0.5 / 3.0 + 1.0e-9);
        moved += std::abs (off) > 1.0e-4 ? 1 : 0;
    }
    CHECK (moved >= 6);
    ed.setSliderValue (S::randomize, 0.0);
    ed.setSliderValue (S::randomize, 100.0);
    CHECK (beatsOf (e.block(), CH) == loose);
}

TEST_CASE ("GRID EDITOR 0.47.0: VELOCITY, DYNAMICS, RANDOM VEL and SLOPE on the chosen notes", "[grideditor]")
{
    Editor e;
    auto& ed = e.ed;
    ed.selectRow (ed.rowOf (CH), false);   // the hats: 0.7 on the beats, 0.4 off
    ed.setSliderValue (S::velocity, -100.0);
    CHECK_THAT (velocityAt (e.block(), CH, 0.0), WithinAbs (0.35, 1.0e-6));
    CHECK_THAT (velocityAt (e.block(), CH, 0.5), WithinAbs (0.2, 1.0e-6));
    ed.setSliderValue (S::velocity, 0.0);
    // DYNAMICS -100: all at their average; +100: twice as far from it.
    ed.setSliderValue (S::dynamics, -100.0);
    CHECK_THAT (velocityAt (e.block(), CH, 0.0), WithinAbs (0.55, 1.0e-6));
    CHECK_THAT (velocityAt (e.block(), CH, 0.5), WithinAbs (0.55, 1.0e-6));
    ed.setSliderValue (S::dynamics, 100.0);
    CHECK_THAT (velocityAt (e.block(), CH, 0.0), WithinAbs (0.85, 1.0e-6));
    CHECK_THAT (velocityAt (e.block(), CH, 0.5), WithinAbs (0.25, 1.0e-6));
    ed.setSliderValue (S::dynamics, 0.0);
    // SLOPE +100: a crescendo across them (the first beat's hats soft, the last's loud).
    ed.setSliderValue (S::slope, 100.0);
    CHECK (velocityAt (e.block(), CH, 0.0) < 0.7f * 0.5f);
    CHECK (velocityAt (e.block(), CH, 3.0) > 0.7f);
    ed.setSliderValue (S::slope, 0.0);
    // RANDOM VEL: within a quarter either way.
    ed.setSliderValue (S::randomVelocity, 100.0);
    for (int k = 0; k < 8; ++k)
        CHECK (std::abs (velocityAt (e.block(), CH, k * 0.5) - (k % 2 == 0 ? 0.7f : 0.4f)) <= 0.25f + 1.0e-6f);
    // Nothing else touched.
    CHECK (velocityAt (e.block(), BD, 0.0) == 1.0f);
}

TEST_CASE ("GRID EDITOR 0.47.0: SELECT by note value, exclude on / off beats, invert, jump; nudge; delete; copy and paste; keys", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    // The hats' quarter notes, then only the off ones of those (2 and 4).
    ed.selectRow (ed.rowOf (CH), false);
    ed.selectNoteValue (1.0);
    CHECK (ed.getSelection().size() == 4);
    ed.exclude (true);
    auto chosen = ed.getSelection();
    REQUIRE (chosen.size() == 2);
    CHECK (chosen[0].beat == 1.0);
    CHECK (chosen[1].beat == 3.0);
    // Inverted: everything else shown (16 - 2).
    ed.invertSelection();
    CHECK (ed.getSelection().size() == e.block().hits.size() - 2);
    // Jump: from nothing, the first note; then the next.
    ed.selectNone();
    ed.jump (1);
    REQUIRE (ed.getSelection().size() == 1);
    CHECK (ed.getSelection()[0].beat == 0.0);
    ed.jump (1);
    CHECK (ed.getSelection().size() == 1);
    // NUDGE the kick on 1 later by the 1/16; earlier again wraps round to the bar's end.
    ed.select (BD, 0.0, false);
    ed.nudge (1);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.25, 2.0 });
    ed.nudge (-1);
    ed.nudge (-1);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 2.0, 3.75 });
    // Keys: SHIFT + I inverts, DELETE takes them out, CTRL + C / V copies and pastes.
    ed.select (BD, 2.0, false);
    CHECK (ed.keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 'c')));
    CHECK (ed.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 3.75 });
    CHECK (ed.keyPressed (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 'v')));
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 2.0, 3.75 });
    CHECK (ed.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 'a')));
    CHECK (ed.getSelection().size() == e.block().hits.size());
}

TEST_CASE ("GRID EDITOR 0.47.0: STRETCH NOTES: the whole block or the chosen notes", "[grideditor]")
{
    Editor e;
    auto& ed = e.ed;
    // The hats on the beats (0, 1, 2, 3) at 2x tempo: half as far apart from the first (0, 0.5, 1, 1.5), merged with
    // the off-beat hats already there; the ones they left (2, 3) empty.
    ed.selectRow (ed.rowOf (CH), false);
    ed.selectNoteValue (1.0);
    ed.stretch (grooves::TempoFactor::twice);
    CHECK (beatsOf (e.block(), CH) == std::vector<double> { 0.0, 0.5, 1.0, 1.5, 2.5, 3.5 });
    // The whole block at half tempo: twice as long, every note twice as far in.
    e.t.p.setSong (oneBar());
    ed.selectNone();
    ed.stretch (grooves::TempoFactor::half);
    CHECK (e.block().loopLength == 8.0);
    CHECK (beatsOf (e.block(), BD) == std::vector<double> { 0.0, 4.0 });
}

TEST_CASE ("GRID EDITOR 0.47.0: a row's MUTE and SOLO while the song plays; CLEAR, then RECALL", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    auto s = oneBar();
    s.blocks[0].hits = { { 0.0, BD, 1.0f }, { 1.0, SD, 1.0f } };
    e.t.p.setSong (s);
    const auto kickHeard = [&e]
    {
        e.t.p.playSong (0.0);
        const auto out = e.t.play (0.6);
        return std::make_pair (std::abs (out[100]) > 0.1f, std::abs (out[24000 + 100]) > 0.05f);   // kick at 0, snare at 0.5 s
    };
    CHECK (kickHeard() == std::make_pair (true, true));
    ed.setRowMute (ed.rowOf (BD), true);
    CHECK (kickHeard() == std::make_pair (false, true));
    ed.setRowMute (ed.rowOf (BD), false);
    ed.setRowSolo (ed.rowOf (BD), true);
    CHECK (kickHeard() == std::make_pair (true, false));
    CHECK (ed.hasSoloMute());
    ed.clearSoloMute();
    CHECK_FALSE (ed.hasSoloMute());
    CHECK (kickHeard() == std::make_pair (true, true));
    ed.recallSoloMute();
    CHECK (kickHeard() == std::make_pair (true, false));
    ed.clearSoloMute();
}

//==============================================================================
// 0.48.0
TEST_CASE ("GRID EDITOR 0.48.0: SNAP TO RELATIVE GRID keeps a note's feel; the NUDGE UNIT", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    auto s = e.t.p.getSong();
    s.blocks[0].hits = { { 1.03, SD, 0.9f } };
    e.t.p.setSong (s);
    e.t.p.flushUndoStep();
    const auto px = e.pxPerBeat();
    const auto at = ed.cellOf (SD, 1.03).getCentre();
    // On the grid: the late snare dragged 0.3 beats lands on 1.25 (the 1/16 nearest).
    e.click (at);
    e.dragFrom (at, at.translated (0.3f * px, 0.0f));
    CHECK_THAT (beatsOf (e.block(), SD)[0], WithinAbs (1.25, 1.0e-9));
    e.t.p.undo();
    // RELATIVE GRID: moved by one whole 1/16 step, still 0.03 late (1.28).
    ed.setSnapMode (BlockEditor::SnapMode::relative);
    e.click (at);
    e.dragFrom (at, at.translated (0.3f * px, 0.0f));
    CHECK_THAT (beatsOf (e.block(), SD)[0], WithinAbs (1.28, 1.0e-9));

    // NUDGE UNIT: a 1/128 note (1/32 beat); 10 ms at the DAW's tempo (120 BPM here: 0.02 beats); the resolution again.
    ed.select (SD, beatsOf (e.block(), SD)[0], false);
    ed.setNudgeUnit (BlockEditor::NudgeUnit::n128);
    ed.nudge (1);
    CHECK_THAT (beatsOf (e.block(), SD)[0], WithinAbs (1.28 + 1.0 / 32.0, 1.0e-9));
    ed.setNudgeUnit (BlockEditor::NudgeUnit::ms10);
    CHECK_THAT (ed.nudgeBeats(), WithinAbs (0.01 * juce::jmax (1.0, e.t.p.getHostBpm()) / 60.0, 1.0e-9));
    ed.setNudgeUnit (BlockEditor::NudgeUnit::resolution);
    CHECK (ed.nudgeBeats() == 0.25);
}

TEST_CASE ("GRID EDITOR 0.48.0: NOTE LENGTHS drawn and dragged, saved, exported, and a cymbal let go at a note's end", "[grideditor][ui]")
{
    Editor e;
    auto& ed = e.ed;
    // SHOW NOTE LENGTH on the snare: its notes get a handle; dragging the first's to beat 2 makes it a beat long.
    CHECK (ed.lengthHandleOf (SD, 1.0).isEmpty());
    ed.setShowLength (snarePiece, true);
    const auto handle = ed.lengthHandleOf (SD, 1.0);
    REQUIRE_FALSE (handle.isEmpty());
    const auto to = juce::Point<float> (ed.cellOf (SD, 2.0).getCentreX(), handle.getCentreY());
    e.dragFrom (handle.getCentre(), to);
    auto lengthOf = [&e] (int slot, double beat)
    {
        for (const auto& h : e.block().hits)
            if (h.slot == slot && std::abs (h.beat - beat) < 1.0e-6)
                return h.length;
        return -1.0;
    };
    CHECK_THAT (lengthOf (SD, 1.0), WithinAbs (1.0, 1.0e-9));
    CHECK (lengthOf (SD, 3.0) == 0.0);
    // Several chosen: all set together.
    ed.selectRow (ed.rowOf (SD), false);
    ed.setNoteLength (SD, 3.0, 0.5);
    CHECK (lengthOf (SD, 1.0) == 0.5);
    CHECK (lengthOf (SD2, 3.5) == 0.5);
    // The pencil on a row showing lengths draws a step long.
    ed.selectNone();
    ed.setTool (BlockEditor::Tool::pencil);
    ed.setResolution (2);
    const auto y = ed.cellOf (SD, 0.0).getCentreY() + 6.0f;
    e.click ({ ed.cellOf (SD, 2.1).getCentreX(), y });
    CHECK (lengthOf (SD, 2.0) == 0.5);
    // Saved with the song, and the MIDI file holds the note that long.
    const auto back = song::Song::fromXml (*e.t.p.getSong().toXml());
    CHECK (back == e.t.p.getSong());
    const auto midi = e.t.p.getSong().toMidi (120.0, [] (int slot) { return slot == SD ? 38 : -1; });
    auto noteOn = -1.0, noteOff = -1.0;
    for (const auto* ev : *midi.getTrack (0))
    {
        if (ev->message.isNoteOn() && std::abs (ev->message.getTimeStamp() - 960.0) < 1.0)
            noteOn = ev->message.getTimeStamp();
        if (ev->message.isNoteOff() && noteOn >= 0.0 && noteOff < 0.0)
            noteOff = ev->message.getTimeStamp();
    }
    CHECK (noteOn == 960.0);
    CHECK (noteOff == 960.0 + 480.0);
}

TEST_CASE ("GRID EDITOR 0.48.0: a note's end grabs a cymbal set to MUTE TAIL > NOTE OFF, at its beat", "[grideditor]")
{
    Plugin t;
    testkit::useSample (t.p, CY, testkit::sine (440.0, 3.0, 0.5f, 1, sampleRate));
    song::Song s;
    song::Block b;
    b.name = "Crash";
    b.length = b.loopLength = 4.0;
    b.hits = { { 0.0, CY, 1.0f, 0.5 } };   // held half a beat: 0.25 s at 120 BPM
    s.place (b, 0.0);
    const auto levelBetween = [] (const std::vector<float>& out, double from, double to)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < (size_t) (to * sampleRate) && i < out.size(); ++i)
            peak = juce::jmax (peak, std::abs (out[i]));
        return peak;
    };
    // MUTE TAIL off: the length makes no difference; it rings on.
    t.p.setSong (s);
    t.p.playSong (0.0);
    auto out = t.play (0.6);
    CHECK (levelBetween (out, 0.1, 0.2) > 0.1f);
    CHECK (levelBetween (out, 0.45, 0.55) > 0.1f);
    // NOTE OFF: grabbed at 0.25 s (faded over 80 ms): ringing before, silent after.
    t.p.stopSong();
    t.play (0.1);
    auto* tail = t.p.getParameters().getParameter (ParamIDs::drum (CY, "MuteTail"));
    REQUIRE (tail != nullptr);
    tail->setValueNotifyingHost (tail->convertTo0to1 (1.0f));
    t.p.playSong (0.0);
    out = t.play (0.6);
    CHECK (levelBetween (out, 0.1, 0.2) > 0.1f);
    CHECK (levelBetween (out, 0.4, 0.55) < 0.01f);
}

TEST_CASE ("GRID EDITOR 0.48.0: OWN WINDOW puts the editor in a window of its own; BACK IN PLUGIN or its X brings it back", "[grideditor][ui]")
{
    Plugin t;
    t.p.setSong (oneBar());
    MainView::groovesWindowOnDesktop = false;   // (this test machine's display has no window manager)
    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& ed = view->getBlockEditor();
    view->openGridEditor (0);
    CHECK (ed.getParentComponent() == view);
    CHECK (ed.getWindowButton().getButtonText() == "OWN WINDOW");
    ed.getWindowButton().onClick();
    REQUIRE (view->isGridEditorInOwnWindow());
    CHECK (ed.getTopLevelComponent() == view->getGridEditorWindow());
    CHECK (ed.isVisible());
    CHECK (ed.getWindowButton().getButtonText() == "BACK IN PLUGIN");
    // Other panels opening in the plugin leave it up in its window.
    view->openSongCreator();
    CHECK (ed.isVisible());
    // Back.
    ed.getWindowButton().onClick();
    CHECK_FALSE (view->isGridEditorInOwnWindow());
    CHECK (ed.getParentComponent() == view);
    CHECK (ed.getBounds() == juce::Rectangle<int> (12, 68, 1476, 672));
    // Out again, and its X: closed, back in the plugin (after the message loop runs).
    view->setGridEditorInOwnWindow (true);
    view->hideGridEditor();
    CHECK_FALSE (view->isGridEditorInOwnWindow());
    CHECK_FALSE (ed.isVisible());
    view->setGridEditorInOwnWindow (true);
    editor.reset();   // the plugin's window closed with it out: no crash
}
