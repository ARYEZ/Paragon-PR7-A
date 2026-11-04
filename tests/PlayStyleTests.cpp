// EDIT PLAY STYLE (0.46.0): AMOUNT, VELOCITY, the POWER HAND, the OPENING HIT, SOLO SELECTED, the context menu's edits and
// the window's ways in, after section 7.3.1 of the manual the owner sent.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

#include "song/PlayStyle.h"
#include "ui/MainView.h"
#include "ui/PlayStylePanel.h"
#include "ui/SongTrack.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;
    namespace ps = song::playstyle;

    constexpr int kick = 0, snare = 1, hats = 2, ride = 8;   // drums::pieces' order

    // Two bars of a rock beat: kick on 1 and 3, snare on 2 and 4, closed hat on the eighths, the pedal on 1.
    song::Block rockBeat()
    {
        song::Block b;
        b.name = "Rock";
        b.length = b.loopLength = 8.0;
        for (int bar = 0; bar < 2; ++bar)
        {
            const auto at = bar * 4.0;
            b.hits.push_back ({ at, BD, 0.9f });
            b.hits.push_back ({ at + 2.0, BD, 0.85f });
            b.hits.push_back ({ at + 1.0, SD, 0.8f });
            b.hits.push_back ({ at + 3.0, SD, 0.8f });
            b.hits.push_back ({ at, PH, 0.5f });
            for (int e = 0; e < 8; ++e)
                b.hits.push_back ({ at + e * 0.5, CH, e % 2 == 0 ? 0.7f : 0.45f });
        }
        std::stable_sort (b.hits.begin(), b.hits.end(), [] (const song::Hit& x, const song::Hit& y) { return x.beat < y.beat; });
        return b;
    }

    bool hasNoteAt (const std::vector<song::Hit>& notes, double beat)
    {
        return std::any_of (notes.begin(), notes.end(), [beat] (const song::Hit& h) { return std::abs (h.beat - beat) < 1.0e-6; });
    }

    struct Plugin
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonPlayStyleTests");
        ParagonX1AudioProcessor p;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            // Short steady bursts (20 ms, full from the first sample): each hit shows where it starts. Each drum its own level.
            testkit::useSample (p, BD, testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate));
            testkit::useSample (p, SD, testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.25f, 1, sampleRate));
            testkit::useSample (p, CY, testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.125f, 1, sampleRate));
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
}

TEST_CASE ("PLAY STYLE 0.46.0: AMOUNT takes the least leaned-on notes away first, and gives them back turned to 0", "[playstyle]")
{
    const auto b = rockBeat();
    const auto hatNotes = ps::notesOf (b, hats);
    REQUIRE (hatNotes.size() == 18);   // 16 closed + 2 pedal
    // Half the hats gone: every off-beat eighth (the softer ones) goes first; the bars' first beats and half bars stay
    // (with the pedal, 10 notes on beats for 9 places: one of the weaker beats 2 / 4 goes too).
    const auto half = ps::withAmount (hatNotes, hats, -0.5, b, 4.0);
    CHECK (half.size() == 9);
    for (const auto beat : { 0.0, 2.0, 4.0, 6.0 })
        CHECK (hasNoteAt (half, beat));
    for (const auto& h : half)
        CHECK (std::abs (h.beat - std::round (h.beat)) < 1.0e-6);
    CHECK (ps::withAmount (hatNotes, hats, -1.0, b, 4.0).empty());
    CHECK (ps::withAmount (hatNotes, hats, 0.0, b, 4.0) == hatNotes);

    // The snare thinned to one of its four: beat 2 of a bar (the backbeat on the bar's half beats the 4's? both are 1.4 x 0.75;
    // its nudge decides), never a place it didn't play.
    const auto snareNotes = ps::notesOf (b, snare);
    const auto oneSnare = ps::withAmount (snareNotes, snare, -0.75, b, 4.0);
    REQUIRE (oneSnare.size() == 1);
    CHECK (hasNoteAt (snareNotes, oneSnare.front().beat));
}

TEST_CASE ("PLAY STYLE 0.46.0: AMOUNT adds where a drummer would: the kick on the strong empty places, the snare's ghost notes soft", "[playstyle]")
{
    const auto b = rockBeat();
    const auto kicks = ps::notesOf (b, kick);
    // A little more kick: the first ones added are on eighths and beats (never a sixteenth while those are free), at the
    // kick's own loudness, a little softer off the beat.
    const auto more = ps::withAmount (kicks, kick, 0.2, b, 4.0);
    REQUIRE (more.size() > kicks.size());
    for (const auto& h : more)
    {
        CHECK (h.slot == BD);
        const auto inBeat = h.beat - std::floor (h.beat);
        CHECK ((std::abs (inBeat) < 1.0e-6 || std::abs (inBeat - 0.5) < 1.0e-6));
        CHECK (h.velocity <= 0.9f);
        CHECK (h.velocity >= 0.45f);
    }
    for (const auto& h : kicks)
        CHECK (hasNoteAt (more, h.beat));   // the groove's own kicks all kept
    // All of it: every sixteenth.
    CHECK (ps::withAmount (kicks, kick, 1.0, b, 4.0).size() == 32);

    // More snare: the backbeats kept as they were, the added ones mostly ghost notes (soft) off the beats.
    const auto snares = ps::notesOf (b, snare);
    const auto busier = ps::withAmount (snares, snare, 0.4, b, 4.0);
    REQUIRE (busier.size() > snares.size() + 4);
    int ghosts = 0;
    for (const auto& h : busier)
        if (! hasNoteAt (snares, h.beat) && h.velocity <= 0.36f)
            ++ghosts;
    CHECK (ghosts >= 3);
    for (const auto& h : snares)
        CHECK (hasNoteAt (busier, h.beat));

    // An instrument the groove doesn't play (adding percussion): the tambourine on the strongest places first.
    const auto tambourine = ps::withAmount ({}, 12, 0.25, b, 4.0);
    REQUIRE (tambourine.size() == 8);
    CHECK (tambourine.front().slot == TMB);
    for (const auto beat : { 0.0, 4.0 })
        CHECK (hasNoteAt (tambourine, beat));

    // Triplet grooves are filled on their own grid.
    song::Block shuffle;
    shuffle.length = shuffle.loopLength = 4.0;
    for (int beat = 0; beat < 4; ++beat)
    {
        shuffle.hits.push_back ({ (double) beat, CH, 0.7f });
        shuffle.hits.push_back ({ beat + 2.0 / 3.0, CH, 0.5f });
    }
    for (const auto& h : ps::withAmount ({}, kick, 1.0, shuffle, 4.0))
    {
        const auto inThirds = h.beat * 3.0;
        CHECK (std::abs (inThirds - std::round (inThirds)) < 1.0e-6);
    }
}

TEST_CASE ("PLAY STYLE 0.46.0: the POWER HAND moved to the ride plays its notes there (merged, the pedal left on the hats)", "[playstyle]")
{
    auto b = rockBeat();
    b.hits.push_back ({ 0.0, RD, 0.3f });   // a ride note where a hat note is: merged, the louder kept
    CHECK (ps::powerHandPiece (b) == hats);
    CHECK (ps::powerHandSlot (b) == CH);
    ps::movePowerHand (b, ride, RD);
    CHECK (ps::powerHandPiece (b) == ride);
    CHECK (b.powerHand == RD);
    const auto rideNotes = ps::notesOf (b, ride);
    CHECK (rideNotes.size() == 16);
    CHECK (rideNotes.front().velocity == 0.7f);
    const auto stillHats = ps::notesOf (b, hats);
    REQUIRE (stillHats.size() == 2);
    CHECK (stillHats.front().slot == PH);
    // Onto the ride's bell: its articulation.
    ps::movePowerHand (b, ride, RB);
    for (const auto& h : ps::notesOf (b, ride))
        CHECK (h.slot == RB);
    // Onto the floor tom: anything can lead; the flag stays where it was put.
    ps::movePowerHand (b, 5, -1);
    CHECK (ps::powerHandPiece (b) == 5);
    CHECK (ps::notesOf (b, ride).empty());
}

TEST_CASE ("PLAY STYLE 0.46.0: the OPENING HIT plays once at the block's start, on the sample; saved; split keeps it on the first half", "[playstyle]")
{
    Plugin t;
    song::Song s;
    auto b = song::Block();
    b.name = "Quiet";
    b.length = 8.0;
    b.loopLength = 4.0;   // played twice
    b.openingOn = true;
    b.openingSlot = CY;
    s.place (b, 4.0);   // from bar 2
    const auto events = s.events();
    REQUIRE (events.size() == 1);
    CHECK (events.front().beat == 4.0);
    CHECK (events.front().slot == CY);

    const auto back = song::Song::fromXml (*s.toXml());
    CHECK (back == s);
    auto split = s;
    split.split (0, 8.0);
    REQUIRE (split.blocks.size() == 2);
    CHECK (split.blocks[0].openingOn);
    CHECK_FALSE (split.blocks[1].openingOn);

    // Heard: at 120 BPM bar 2 starts 2 s in, on that very sample.
    t.p.setSong (s);
    t.p.playSong (0.0);
    const auto out = t.play (3.0);
    const auto first = (size_t) std::distance (out.begin(), std::find_if (out.begin(), out.end(), [] (float x) { return std::abs (x) > 1.0e-4f; }));
    CHECK (first == 96000);
    CHECK (out[96010] > 0.02f);   // (the crash's own level and the hit's velocity curve set how loud)
}

TEST_CASE ("PLAY STYLE 0.46.0: SOLO SELECTED plays only the chosen drums of the song", "[playstyle]")
{
    Plugin t;
    song::Song s;
    song::Block b;
    b.name = "KS";
    b.length = b.loopLength = 4.0;
    b.hits = { { 0.0, BD, 1.0f }, { 1.0, SD, 1.0f } };
    s.place (b, 0.0);
    t.p.setSong (s);
    t.p.setSongSolo (ps::slotsOfPiece (snare));
    t.p.playSong (0.0);
    auto out = t.play (1.0);
    CHECK (std::abs (out[100]) < 1.0e-6f);              // the kick at 0 not heard
    CHECK_THAT (out[24000 + 100], WithinAbs (0.125, 0.01));   // the snare at beat 2 (0.5 s; the kit plays a sample at half its level)
    t.p.setSongSolo (0);
    t.p.playSong (0.0);
    out = t.play (0.1);
    CHECK_THAT (out[100], WithinAbs (0.25, 0.01));
}

TEST_CASE ("PLAY STYLE 0.46.0: the window: opened from the strip, AMOUNT and VELOCITY on the chosen drums, each an undo step", "[playstyle][ui]")
{
    Plugin t;
    song::Song s;
    s.place (rockBeat(), 0.0);
    auto second = rockBeat();
    second.name = "Rock 2";
    s.place (second, 8.0);
    t.p.setSong (s);
    t.p.flushUndoStep();
    std::unique_ptr<juce::AudioProcessorEditor> editor (t.p.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& track = view->getSongTrack();
    auto& panel = view->getPlayStyle();

    // The block's menu: EDIT MIDI > EDIT PLAY STYLE.
    auto menu = track.menuFor (0);
    bool found = false;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == "Edit MIDI" && it.getItem().subMenu != nullptr)
            for (juce::PopupMenu::MenuItemIterator sub (*it.getItem().subMenu); sub.next();)
                if (sub.getItem().text == "Edit Play Style...")
                {
                    found = true;
                    sub.getItem().action();
                }
    REQUIRE (found);
    REQUIRE (panel.isVisible());
    CHECK (panel.getBlocks() == std::vector<int> { 0 });
    // Every instrument the block plays chosen; REC off meanwhile.
    CHECK (panel.getChosen() == std::vector<int> { kick, snare, hats });
    CHECK_FALSE (track.getRecordButton().isEnabled());
    CHECK (panel.getTotalHits() == 4 + 4 + 18);

    // The kick: more of it.
    panel.choose (kick);
    CHECK (panel.getChosen() == std::vector<int> { kick });
    panel.setAmount (0.5);
    t.p.flushUndoStep();
    const auto kicks = panel.getTotalHits();
    CHECK (kicks > 4);
    CHECK (ps::notesOf (t.p.getSong().blocks[1], kick).size() == 4);   // the other block untouched
    // The snare: fewer, softer.
    panel.choose (snare);
    CHECK (panel.getAmount() == 0.0);   // its own knob
    panel.setAmount (-0.5);
    t.p.flushUndoStep();
    CHECK (panel.getTotalHits() == 2);
    panel.setVelocity (-1.0);
    t.p.flushUndoStep();
    for (const auto& h : ps::notesOf (t.p.getSong().blocks[0], snare))
        CHECK_THAT (h.velocity, WithinAbs (0.4, 1.0e-6));
    // Back to the kick: its knob as left; turned back to 0, the groove's own kicks again.
    panel.choose (kick);
    CHECK (panel.getAmount() == 0.5);
    panel.resetAmount();
    t.p.flushUndoStep();
    CHECK (ps::notesOf (t.p.getSong().blocks[0], kick) == ps::notesOf (rockBeat(), kick));

    // Undo: the kick's 0.5 again, then the snare's velocity back.
    t.p.undo();
    CHECK ((int) ps::notesOf (t.p.getSong().blocks[0], kick).size() == kicks);
    t.p.undo();
    CHECK (ps::notesOf (t.p.getSong().blocks[0], snare).front().velocity == 0.8f);

    // REMOVE MIDI on the hats: none; AMOUNT back up brings them back.
    panel.choose (hats);
    panel.removeMidi();
    CHECK (panel.getTotalHits() == 0);
    CHECK (panel.getAmount() == -1.0);
    panel.setAmount (0.0);
    CHECK (panel.getTotalHits() == 18);

    // SHIFT-clicking the second block adds it: AMOUNT works on both.
    const auto at = juce::Point<float> (track.xOf (12.0), (float) track.getLane().getCentreY());
    const auto click = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                         juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier, 1.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, &track, &track, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    track.mouseDown (click);
    track.mouseUp (click);
    CHECK (panel.getBlocks() == std::vector<int> { 0, 1 });
    panel.choose (kick);
    panel.setAmount (-1.0);
    CHECK (ps::notesOf (t.p.getSong().blocks[0], kick).empty());
    CHECK (ps::notesOf (t.p.getSong().blocks[1], kick).empty());

    // POWER HAND to the ride in both; OPENING HIT on.
    panel.movePowerHand (ride, RD);
    CHECK (ps::powerHandPiece (t.p.getSong().blocks[1]) == ride);
    panel.setOpeningOn (true);
    CHECK (t.p.getSong().blocks[0].openingOn);
    CHECK (t.p.getSong().blocks[1].openingOn);
    panel.moveOpeningHit (SPL);
    CHECK (t.p.getSong().blocks[1].openingSlot == SPL);
    CHECK (panel.getPowerHandTag().getWidth() > 0);

    // Closed (X): REC back, the strip's highlight gone.
    panel.close();
    CHECK_FALSE (panel.isVisible());
    CHECK (track.getRecordButton().isEnabled());
    // A double-click opens the one used last (EDIT PLAY STYLE); P opens it too, E the grid editor.
    track.choose (0);
    CHECK (track.keyPressed (juce::KeyPress ('e')));
    CHECK (view->getBlockEditor().isVisible());
    CHECK (track.keyPressed (juce::KeyPress ('p')));
    CHECK (panel.isVisible());
    CHECK_FALSE (view->getBlockEditor().isVisible());
    editor.reset();
}

TEST_CASE ("PLAY STYLE 0.46.0: cut, copy, paste, quantize and articulation from an instrument's menu", "[playstyle][ui]")
{
    Plugin t;
    song::Song s;
    auto b = rockBeat();
    b.hits.push_back ({ 1.52, SD, 0.3f });   // a late ghost note
    std::stable_sort (b.hits.begin(), b.hits.end(), [] (const song::Hit& x, const song::Hit& y) { return x.beat < y.beat; });
    s.place (b, 0.0);
    t.p.setSong (s);
    PlayStylePanel panel (t.p);
    panel.setBounds (0, 0, 1476, 672);
    panel.open ({ 0 });

    // The snare quantized to 1/16: the ghost note on 1.5.
    panel.choose (snare);
    panel.quantize (2);
    CHECK (hasNoteAt (ps::notesOf (t.p.getSong().blocks[0], snare), 1.5));
    // Its articulation: every snare note a rimshot.
    panel.setArticulation (SD2);
    for (const auto& h : ps::notesOf (t.p.getSong().blocks[0], snare))
        CHECK (h.slot == SD2);
    // Copied onto the floor tom (replacing what it played): the same beats, on its own slot.
    panel.copyMidi();
    panel.choose (5);
    panel.pasteMidi (snare);
    const auto tom = ps::notesOf (t.p.getSong().blocks[0], 5);
    CHECK (tom.size() == 5);
    for (const auto& h : tom)
        CHECK (h.slot == LT);
    // Cut from the snare: gone from it, in the clipboard.
    panel.choose (snare);
    panel.cutMidi();
    CHECK (ps::notesOf (t.p.getSong().blocks[0], snare).empty());
    auto menu = panel.instrumentMenu();
    bool pasteOn = false;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == "Paste MIDI")
            pasteOn = it.getItem().isEnabled;
    CHECK (pasteOn);
    panel.close();
}
