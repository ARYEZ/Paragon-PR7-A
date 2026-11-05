# Changelog

Each version's notes appear on its GitHub Release page.

## 0.53.1

**No more halos round THE DRUM MACHINE's lit keys and pads** (you spotted them on 0.53.0):
- **The keys' LEDs** (MACHINE, 1-18, A, SONG, PLAY...): each lit LED had a round red blob round it. The LED's glow was sized for a lying bar (the neon stripes elsewhere), so on an upright LED it spread as wide as the key is tall. It's now sized by the LED's thickness, so it's a thin lit line with a tight glow. The lying neon bars everywhere else (the pads' stripes, the step keys, the rack units, the footswitches) draw exactly as before.
- **The pads:** a sounding pad drew four stepped rings of pink light outside its bezel, which read as a halo round the new neon bezel. They're gone: a sounding pad now lights up inside the key, and its stripe lights.

## 0.53.0

**Black keys in neon rings** on the two neon finishes, Pearl Black and Pearl White + Neon. This is the family's new look, as PR7-G 0.59.0 has it (your design, chosen from three mockups):
- **Every key and knob is black**, as on Pearl White (no more pink-to-orange keys and knobs).
- **The pink-to-red-to-orange moves to the rings round them:**
  - the bezels round the big keys (the FX rack's POWER footswitches, THE DRUM MACHINE's pads), lit along the top and darker at the front like the chrome they replace;
  - the small keys' surrounds;
  - the knobs' glowing ring (brightened a little, as it's a light);
  - THE DRUM MACHINE's fader slots (PR7-D's own: the rings round the fader caps).
- The keys' lettering is light on black, lit orange, as on Pearl White.
- **Pearl White doesn't change:** renders before and after match pixel for pixel, apart from the live displays.

Where you'll see it: THE DRUM MACHINE (its FINISH key; Pearl Black by default). The rest of the window has no finish setting of its own in PR7-D, so it stays Pearl White.

**Tests:** a pad and a knob drawn in each finish: the key and the knob's top black in all three, the bezel's rim neon on the two neon finishes and chrome on Pearl White, the knob's ring a coloured light in all three.

## 0.52.0

Two fixes from using EDIT PLAY STYLE and THE GRID EDITOR.

**POWER HAND by its list, not by dragging.** Dragging the POWER HAND flag over the kit picture caught on the wrong pieces, and some drums were hard to reach. Now **click the flag** (or its arrow):
- First, ticked: the instrument it's on, with its articulations under it (the hi-hat's Closed, Open 1 ... Shoulder; the ride's Bow, Bell, Edge). The pedal is left out: it's a foot.
- Then every other drum and cymbal your kit has, A to Z.
  - Left out: the kick (a foot), the clap, tambourine and shaker, and pieces switched off in the kit.
- Pick one and the power hand moves there: its notes go to that instrument, merged with what it already plays.
- The OPENING HIT flag has the same list. Dragging still works too, if you like it.

**G and H zoom THE GRID EDITOR**, as in a DAW's MIDI editor: G zooms out, H zooms in, by half again each press, the middle of the view kept where it is. The ZOOM slider and ALT + the wheel still work.

**Tests:** the list's order (the current instrument and its articulations first, the rest A to Z, the left-out ones missing), moving the power hand and the opening hit from it; G / H zoom steps, the middle kept, the limits, CTRL + G not a zoom.

## 0.51.0

**IMPORT A RENDER**: turns a drum you bounced out of another drum program into a layer, for the kit you reinforce (your Progressive Foundry snare under the Djent Drums snare, say). DRUMS > LOAD... > IMPORT A RENDER as layers.

**What to bounce:**
1. A MIDI clip with the drum's hits at the velocities you want (e.g. 20, 40 ... 127), each repeated a few times for round robins, spaced so each hit dies away (about 2 s for a snare, more for a cymbal). A snare's centre and its rimshot can share one clip.
2. Bounced **one file per mic channel**, named after the mic: "Snare Top", "Snare Bottom", "OH", "Room", "Kick In"... Any name the mixer knows works.
3. The bounced files and the MIDI file in one folder.

**What it does:**
- Each file goes to the mixer channel its name says. A whole-mix file ("Main", "Master", "Mix") and files with names no channel goes by are left out, and the summary says so.
- The hits are cut where the MIDI says, **every mic at exactly the same moments**, so the room stays as late after the close mic as it was recorded. 1 ms before each hit is kept.
- If the bounce started at another bar, or with silence before it, the audio and the MIDI are lined up first.
- Each hit is kept up to the next one, its silent tail trimmed (70 dB under its loudest, all mics together) and its end faded.
- Each note becomes its own multi-mic instrument: each velocity played becomes a velocity layer (up to 16), its repeats round robins. **The velocities are kept as played**: each layer covers the velocities halfway to the next.
- Written into your presets folder's "Rendered Layers", so presets and projects find them again. Each is loaded as a layer on the drum its note plays (in the layout you choose), into the first free layer.
  - Close mics go to the layer's own USER channel, overheads and room into theirs.
- **No MIDI?** It finds the hits by listening, and guesses their velocities from how loud each is. It puts them on the drum you have chosen.
- A summary after: which mics went where, each note's drum and layer, its velocity layers and round robins, anything left out. The drum's LAYERS tab opens on it (AUTO-ALIGN, LEVEL, where it plays).
- The progress box shows it working; CANCEL stops it and loads nothing.

**Tests:** a render made in the tests (three mics, the room 5 ms after the close mic, a whole mix and an unknown file beside them, two notes, three velocities, three round robins): the mics and hits found, a bounce started a quarter second or a whole bar early followed to the sample, tails trimmed, the layers' velocities, the room still 5 ms after the close mic when played, hits found with no MIDI, CANCEL.

**Not checked here:** real bounces from your drum program (send me the file names of one if anything lands in the wrong channel), and how they sound (your ears).

## 0.50.0

**THREE LAYERS per drum, built for reinforcing a kit** (the Djent Drums kit with renders bounced out of another drum program, say). Up to three of your own sounds play with each of a drum's hits, each with its own settings, and AUTO-ALIGN lines each one up with the drum.

**The LAYERS tab** (DRUMS > the DRUM view, beside ENVELOPE and VELOCITY). A row per layer:
- **LOAD... / REMOVE:** a file (one hit), a folder of hits (named by velocity layer and round robin, "..._V1_RR2", or sorted by loudness), or a folder holding a folder per mic (SNARE TOP, OH, ROOM...).
- **LEVEL** and **OUT:** WITH THE DRUM (its VOLUME, PAN, INSERT, OUTPUT), or a mixer channel with its own fader, effects and sends.
- **PITCH:** semitones and hundredths, to tune it to the drum.
- **OFFSET:** later (+) or earlier (-) than the drum, to the sample. Earlier works by starting a little way into the layer's sound, so nothing has to wait.
- **INVERT:** the layer's polarity turned over.
- **VEL LOW / VEL HIGH:** the hits it plays on (e.g. a crack layer only above 90).
- **LENGTH:** WHOLE, or it fades out after that many ms (just a layer's attack, say).
- **AUTO-ALIGN:** sets OFFSET and INVERT so the layer lines up with the drum's own hit (its close mic).
  - Two sounds a fraction of a millisecond apart, or one turned over, partly cancel each other: the drum gets thinner, not bigger.
  - It finds both hits' starts, lines up their loudness shapes, then the waveforms within 0.3 ms of that, keeping the delay where the two add up the most.
  - It reports the offset, whether it turned the layer over, and how alike the two are (MATCH).
- **The picture under the rows:** the drum's hit (white) and each layer's (orange, pink, gold) from 2 ms before the drum's start, as they'll play, their offsets and polarities included. Lined up, they rise together.

**Multi-mic layers.** A folder of mic folders (a render bounced out a channel at a time) plays like a multi-mic drum: its close mics into the layer's own channel, its overheads and room into OVERHEADS and ROOM. It gets a USER channel of its own when it's loaded ("SNARE LAYER", "SNARE LAYER 2"...), onto the drum's bus. The first layer of a drum through the mixer still turns the drum's own close mics down 10 dB so the layer leads.

**Old presets, kits and projects** load their one layer as LAYER 1, as it sounded. Presets, kits (.pr7kit) and projects keep all three layers and their settings.

**Fixed:** loading two things one straight after the other (two layers, or two drums' samples) could lose the first: every load stopped the one before it. Now a load only replaces an earlier load of the same drum or layer.

**Tests:** three layers at their levels, velocity ranges, PITCH (an octave up measured at the frequency), OFFSET to the sample both ways, INVERT cancelling an identical hit, LENGTH, AUTO-ALIGN (a hit late by 37 samples and turned over: found to the sample, then the two add up to exactly twice the drum; an early one; a different drum), multi-mic layers' channels, and saving in presets, kits and projects.

**Not checked here:** how your renders sound layered (your ears).

## 0.49.0

**A quicker plugin**, from a second round of measuring (as in 0.40.0): timings and instruction counts of each page's drawing, opening the window, and the factory presets playing. Nothing sounds different: every audio change gives the same samples, bit for bit.

**The window:**
- **Opening the plugin's window:** 357 ms -> 135 ms the first time, and about 250 ms -> 83 ms after that.
  - It used to render every drum's sound to draw its picture in the drum list, all 36 of them.
  - The window opens on the KIT VIEW, where that list isn't shown.
  - The pictures are now made when the DRUM or SEQUENCER view is first shown.
- **The MIXER page draws about five times faster** (a full redraw 60 ms -> 12 ms):
  - The unused strips, and the effect and send buttons that aren't on, were faded by making them see-through.
  - That draws each one into a picture of its own and blends it back in, and with most strips unused it was most of the page's drawing time.
  - Now a sheet of the strip's own colour is drawn over them: the same look, no blending.
- **EDIT PLAY STYLE's kit** (dimmed, with the played drums outlined) is drawn once and copied, instead of being worked out afresh on every repaint (every frame while drums glow): 20 ms -> under 9 ms the first time, and only the copy and the glows after that.

**The sound engine:**
- **The LIMITER's true-peak detector does about a third of the work.**
  - It looks between samples for peaks, through a 64-sample filter at 8 points, for every sample.
  - The samples it reads now sit side by side in memory, so it no longer works out where each one is in a ring.
  - The same numbers in the same order: bit for bit the same result.
- **Settings read every block are looked up by name in a hash table** instead of a sorted list (a few hundred lookups a block, about 3% of the work).
- **Together:** the Djent Kit playing takes 15% less work (2,765 million -> 2,354 million instructions measured over the same seconds).

**Measured and left alone:**
- The modelled drums' and cymbals' sound-making itself: the biggest share, the instrument's own work.
- The simulated mics: the room and mic delays are real work.
- The mixer's SATURATION (MARS): copied unchanged from PR7-X, so better sped up there and ported back.

**Tests:** all pass, pluginval at strictness 10. The limiter's tests compare its output as before, and the screenshots match the old drawing (MIXER, EDIT PLAY STYLE: differences of a few levels at the edges of the outlines).

## 0.48.0

**THE GRID EDITOR, part 2**: the rest of the manual's section 7.3.2, except the CC / aftertouch lanes, as you chose.

**OWN WINDOW.** A button in the editor's top bar puts the whole GRID EDITOR in a window of its own, resizable and staying above your DAW (on a second screen, say). Meanwhile the plugin's window is free for the kit, the mixer and the song.
- BACK IN PLUGIN, the window's close button, or the editor's X brings it back.
- The editor stays up in its window while EDIT PLAY STYLE, the SONG CREATOR or the other pages open in the plugin.

**NOTE LENGTHS.** Right-click a row's name for its menu, then Show Note Length.
- That row's notes are drawn with a bar to where they end.
- Drag a note's end to set how long it's held, on the grid while SNAP is on. If the note is chosen, the other chosen notes get the same length.
- The pencil on a row showing lengths draws notes one resolution step long.
- A note's end lets go of its drum. So a cymbal set to MUTE TAIL > NOTE OFF on the DRUMS page is grabbed right there, as when you let go of a key or pad: a crash held half a beat stops after half a beat.
- Lengths are saved with the song, and EXPORT MIDI writes each note that long.
- Notes with no length set play exactly as before.

**OPTIONS:**
- **Snap To:**
  - **Grid:** a dragged note lands on the grid, as before;
  - **Relative Grid:** notes move by whole steps and keep how far off the grid they were, so a late snare you played stays as late when you move it.
- **Nudge Unit:** Follow Resolution (as before), 1/128 note, or 1, 5 or 10 milliseconds at the song's tempo.

**Tests:**
- a late snare moved by grid and by relative grid;
- the nudge units;
- a note length dragged by its end, set on all the chosen notes together, drawn by the pencil, saved and exported to MIDI;
- a crash held half a beat with MUTE TAIL > NOTE OFF: ringing before its end, silent from 0.15 s after it (and ringing on with MUTE TAIL off);
- the window: out, staying up, back, its X, and the plugin closed with it out.

pluginval passes at strictness 10.

**Not checked here:** the window in a real DAW on Windows, and how a grabbed cymbal sounds (your ears).

## 0.47.0

**THE GRID EDITOR, rebuilt** after section 7.3.2 of the manual you sent: a real note-by-note editor for a song block. It works the way the manual describes; the look, the words and the swing / randomize / dynamics maths are ours. Open it as before: E, right-click a block > Edit MIDI > Grid Editor, its tab beside EDIT PLAY STYLE, or a double-click when it was the one used last.

**The rows:**
- One row per instrument, with its notes count.
- Instruments with articulations (snare, hi-hat, cymbals, ride) have an arrow that unfolds them: the snare's center, rimshot and sidestick each get their own row.
- Drag a row's name up or down to reorder.
- **M / S** mute or solo an instrument while the song plays. **CLEAR SOLO / MUTE** turns them all off, and **RECALL** brings them back.
- **ROWS:** Show Unmapped / Turned-Off Drums, Reset Order, Collapse All, Expand All.

**The notes** are diamonds, brighter the louder.
- **POINTER:**
  - click a note to choose it (SHIFT: more), drag on empty space for a lasso, click a row's name for all its notes;
  - drag the chosen notes along the beat (onto the grid while SNAP is on) or onto another row (another drum or articulation);
  - ALT-drag copies them;
  - double-click empty space adds a note.
- **PENCIL** (or CTRL + ALT held): click or drag along a row to draw notes on the resolution.
- Right-click a note to remove it. Right-click empty space for Cut / Copy / Paste / Delete / Select All.

**Timing** (on the chosen notes, or all when none):
- **SNAP** and **RESOLUTION** (Auto, 1/4 to 1/32 and triplets; Auto follows the finest grid line drawn at the zoom) set where notes go.
- **QUANTIZE** moves them part of the way to the grid (**MAX**: all the way).
- **SWING** pushes every second step later (100: a full triplet swing) or earlier.
- **RANDOMIZE** loosens them by up to a third of a step.
- **NUDGE < >** (or ALT + arrows) moves them by one resolution step.
- These sliders always work from the notes as they were when you chose them, so 0 puts them back.

**The velocity lane** under the notes has a stalk for each note. Drag a stalk to set its velocity (the other chosen notes move with it), or draw across stalks with the pencil. Four sliders work on the chosen notes:
- **VELOCITY:** half to twice as loud, balance kept;
- **DYNAMICS:** loud and soft further apart, or closer;
- **RANDOM VEL:** a human wobble;
- **SLOPE:** a crescendo or decrescendo.

**SELECT:**
- Select All, Invert (SHIFT + I);
- Exclude on beats or off beats;
- Note Values (only the quarters, eighths... of the choice);
- Jump To the next or previous note (the arrow keys).

**OPTIONS:** Stretch Notes (1/2x, 2x, 2/3x, 3/2x; on the chosen notes, or the whole block with its length), and Preview Sounds on or off.

**View:** ZOOM (ALT + wheel) and scrolling, AUTO SCROLL that follows the song as it plays, the playhead, and straight or triplet grid lines (right-click the bar numbers).

**Keys:** DELETE, CTRL + A / C / X / V, ESC.

Every change is one undo step.

**Tests:** the rows (fold-out, reorder, hidden drums), click / SHIFT / lasso, moving on the grid, ALT copy, moving onto another row, the pencil (drag and CTRL + ALT), double-click, the velocity lane (with the chosen, the pencil), QUANTIZE 50 / 100 / 0 and its undo, SWING +100 / -50, RANDOMIZE (bounded, repeatable), VELOCITY, DYNAMICS, SLOPE, RANDOM VEL, Select by note value / exclude / invert / jump, nudge with wrap, keys, copy / paste, stretch, and MUTE / SOLO / CLEAR / RECALL heard in the audio. pluginval passes at strictness 10.

**Next (0.48.0):** snap to "relative grid", the nudge unit, note lengths, and the grid editor in its own window. The CC / aftertouch lanes are left out, as you chose.

## 0.46.0

**EDIT PLAY STYLE**, after section 7.3.1 of the manual you sent. You change how a song block's drums play, one instrument at a time, on the drawn kit. It works the way the manual describes. The look, the words and the way AMOUNT decides where notes go are ours.

This release also carries 0.45.0 (the count-in, moving notes sideways, the GROOVES window). 0.45.0 never published on its own because CI cancelled its macOS build.

**Windows and Linux only from now on.** GitHub stopped giving the macOS build a machine (twice it waited 15 minutes and was cancelled), and that held back every release. So releases no longer include a macOS zip. It can come back later.

**Opening it:**
- double-click a song block;
- click the small arrow at a block's top left (click it again to close);
- right-click a block > Edit MIDI > Edit Play Style;
- choose a block and press P (E opens the GRID EDITOR, the old EDIT BLOCK).

The window has two tabs, EDIT PLAY STYLE and GRID EDITOR, to switch between the two views of the same block. A double-click opens whichever you used last. X closes it, and the song plays on.

**The kit:**
- The instruments the block plays stay lit and outlined; the rest are dimmed.
- Click an instrument to choose it (SHIFT: more than one).
- SELECT INSTRUMENT lists every instrument, with the ones the block plays marked. It also has All Playing Drums, All Toms and All Crashes.
- Instruments that aren't in the picture (clap, tambourine, shaker, crashes 3-6, the FX crash, china 2, tom 5) have buttons under it.
- The chosen instruments' notes light up in the song strip.

**AMOUNT** turns left for fewer hits and right for more, with TOTAL HITS showing the count. Its arrow, or CTRL / CMD-click on the knob, resets it.
- Taking away removes the least leaned-on, softest notes first: off-beat eighths before beats, beats before the bar's first.
- Adding fills the strongest empty places first, on the groove's own straight or triplet grid, at the instrument's own loudness (softer off the beat). Each kind of drum adds what it would play:
  - the kick on the downbeats and eighths;
  - the snare its backbeats, then soft ghost notes;
  - hats, ride and percussion on the eighths;
  - toms at the bar's end;
  - crashes on bar lines.
- It always works from the instrument's notes as they were, so turning it back to 0 gives the groove back exactly.
- It works on instruments the block doesn't play too, to add percussion.

The manual's version learns from thousands of real performances; ours is our own set of rules about where drummers put their notes.

**The other controls:**
- **VELOCITY**: softer (down to half) or louder (up to twice), with the balance between hits kept.
- **SOLO SELECTED**: only the chosen instruments heard.

**Right-click an instrument:**
- Cut MIDI, Copy MIDI;
- Paste MIDI (the copied instrument's notes onto it, replacing its own);
- Remove MIDI (AMOUNT turned back up brings them back);
- Quantize;
- Articulation (for one instrument, e.g. every snare note a rimshot).

**The POWER HAND flag** sits on the groove's leading instrument.
- Drag it onto another one (the ride, its bell, a crash, even a tom) and every note moves there, merged with what that instrument played.
- The hi-hat's pedal stays: that's the foot.
- Its arrow chooses the articulation.

**The OPENING HIT flag** adds one hit at the block's start (only the first time round), switched with OPENING HIT ON / OFF. Drag it onto any instrument; its arrow chooses the articulation.

**Several blocks at once:** SHIFT-click more blocks in the strip while the window is open, and every change applies to all of them.

Each change is one undo step; while you turn a knob, you hear it straight away. Recording into the song is off while the window is open, as in the manual.

**Tests:**
- AMOUNT keeps the beats and drops the off-beats first, all the way down removes everything, and 0 gives the groove back;
- the kick fills eighths and beats, the snare gets ghost notes under 0.36, the tambourine lands on bar starts, and a triplet groove is filled on triplets;
- the power hand moves to the ride (merged, louder kept, pedal left), to its bell and to a tom;
- the opening hit lands on its exact sample, once per block, and survives saving and splitting;
- SOLO SELECTED silences the kick;
- the window: the menu and keys, the chosen instruments, each knob per instrument, undo, REMOVE MIDI then AMOUNT, two blocks with SHIFT, REC off and back;
- quantize, articulation, copy / paste onto a tom, cut.

pluginval passes at strictness 10.

**Not checked here:** how the added notes sound (your ears), and dragging the flags with a real mouse in a DAW.

## 0.45.0

Three from the GROOVES list, as you asked.

**A COUNT-IN before recording.** Press PLAY with REC armed and the song waits one bar while the click counts you in (four clicks in 4/4, the first higher), then starts on the very sample the bar ends.
- Nothing plays and nothing records during the count-in. A note you hit early isn't written into the song.
- It counts at the tempo where PLAY starts, and in that bar's time signature.
- It's on by default. Turn it off in CLICK... > Count In One Bar Before Recording.
- It clicks whatever CLICK is set to (even OFF), since the count-in is how you hear it.
- Only for PLAY: while your DAW drives the song, use your DAW's own count-in.

**Move notes sideways in EDIT BLOCK.** Grab a note and drag it left or right: it moves along the beat, snapping to GRID.
- Hold ALT for a free move, off the grid.
- Dragging up or down still sets its velocity; the first few pixels of the drag decide which.
- Its velocity stays as it was. Dropped onto another note of the same drum, it replaces it.
- Each move is one undo step.

**GROOVES in a window of its own.** OWN WINDOW (on the filter bar, beside GROOVE PARTS) puts the whole GROOVES page in a separate, resizable window, on a second screen say, leaving the plugin's window free for the kit, the mixer and the song.
- Drag grooves from it onto the song strip, the sequencer or a DAW track as before.
- The GROOVES tab in the plugin says it's away (SHOW ITS WINDOW / BACK IN PLUGIN).
- BACK IN PLUGIN, or closing the window, brings it back.
- The window stays above your DAW so clicking the DAW doesn't hide it, and it closes with the plugin's window.

**Tests:** the count-in's four clicks and the song's first kick on their samples, a note during the count-in not recorded and one after it at its beat, off = no wait; a note dragged 1.3 beats landing on 2.25 (1/16 GRID), ALT free, up / down still velocity, one undo step, the move's wrapping and replacing; the GROOVES page into its window and back, and the plugin's window closed with it out. pluginval passes at strictness 10.

**Not checked here:** the window in a real DAW on Windows (only the desktop of this test machine), and dragging from it into PR7-S.

## 0.44.0

**THE SONG TRACK's LOOP AREA.** PLAY can now go round a part of the song while you work on it, or record over it take after take.

- **Set it:** drag along the bar numbers. It snaps to whole bars and shows as an orange band, with the lane lightly washed between its edges. Or right-click a block (or the bar numbers): LOOP THIS BLOCK.
- **LOOP** (a new button on the strip) turns it on and off. With no loop set yet, it loops the chosen block's bars, or four bars from PLAY's start.
- **Exact:** PLAY jumps back to the loop's start on the very sample the loop ends, even in the middle of an audio block at an odd tempo, so the groove doesn't stumble at the seam.
- **Recording in a loop:** notes you play after the loop goes round land in the loop's bars, where you played them.
- **The loop is PLAY's.** While your DAW plays the song (FOLLOW DAW), your DAW's own cycle decides the looping. A bounce to audio always plays the whole song once, with no loop.
- Right-click the bar numbers for: Loop, Loop This Block, Clear the Loop Area, PLAY From Here. A plain click on the bar numbers still sets PLAY's start.

**THE CLICK: a metronome for the song.** CLICK... (beside LOOP) sets it:
- **While Recording** (the default): it clicks only while REC is armed.
- **Always:** whenever the song plays.
- **Off.**

It clicks on every beat, higher on each bar's first beat (it follows the song's time signature changes). It's the same short click as tap-to-find's metronome, about -12 dB. It goes into the main output only, never into a bounce.

**Kept with your project:** the loop and the click setting are saved with the song, and changing them can be undone.

**Not in this version:** a count-in before recording (ask if you'd like it next).

**Tests:** the loop's wrap to the sample at 110 BPM (mid-block), playing on past the song's end inside a loop, no loop = plays to the end and stops; the click on every beat to the sample, its higher bar click (measured by pitch), each of its three settings; a note recorded after the wrap landing at its beat in the loop; the loop and click saved with the song. pluginval passes at strictness 10.

## 0.43.0

**The KIT VIEW is your 12-piece kit, laid out like your reference picture.** The Tron kit is rendered again with exactly the 12 pieces your Djent Drums pack records:
- three rack toms and one floor tom
- the hi-hat, snare and kick
- Crash 1, Crash 2, the splash, the ride and the china

- **The layout follows your picture, with your tweaks:** every piece was placed by measuring where it sits in your picture and working back to 3D. Then, with you:
  - The rack toms sit on the kick, straight and level, with a little room between them, and the top of the kick shows below them.
  - Crash 2 and the splash are set back from the toms.
  - Every cymbal stand stands on clear floor (none passes through a drum).
- **The camera** sits far back with a long lens, about 38 degrees down, so the kit looks tight and level as in your picture.
- **Crashes 3-6, the FX crash, China 2 and Tom 5 left the picture.** They're still in the plugin and still play their recorded stand-ins (Crash 1 / 2, the china, Tom 4). They're still on the DRUM and SEQUENCER lists and the DRUM MACHINE; they just can't be clicked in the KIT VIEW.

**The SONG track scrolls.** A song longer than 32 bars no longer squeezes into the strip:
- A scroll bar under the lane shows which part you're seeing; drag it, or use the mouse wheel.
- Ctrl (Cmd on a Mac) + the wheel zooms, from 4 to 256 bars across.
- While the song plays, the view turns the page with it.
- A song of 32 bars or less still fits whole, as before.

**THE DRUM MACHINE's FINISH key.** A new FINISH... key on the machine's face (right-hand column, under SEQUENCE) gives it Pearl White, Pearl Black or Pearl White + Neon:
- It's the machine's own finish, apart from the rest of the plugin's (SETTINGS).
- It's remembered on your computer.
- Pearl Black is still the default.

**Tests:** the kit view's pieces, click map, glows and colours on the new pictures; the song strip's scrolling (32 bars shown, the scroll bar, the wheel, zoom, a bar's place after scrolling); the machine's three finishes (the face really changes, the plugin's own finish untouched). pluginval passes at strictness 10.

## 0.42.0

**The KIT VIEW has the whole page, as you asked.** The list of drums down the left is gone from the KIT VIEW, and the Tron kit spreads across the full width of the page.

- **The same shape as your reference picture:** the kit is rendered again at 1896 x 744, the picture you sent's own size, instead of the old squarer 1280 x 924. It's the same kit from the same seat, with a wider lens: the whole kit fits top to bottom, with the glowing floor running out on both sides. The page's height is what limits the kit's size, so the drums themselves are only about 5% taller than before; what changes is the room around them: the picture is now 1448 pixels wide on a 1600-pixel window instead of 848.
- **Still there:** KITS, SEQUENCES and LINK on the KIT VIEW's title line, the articulations panel (now on the open floor at the bottom left), the OUTPUT strip and the SONG track.
- **The drum list** is still on the DRUM and SEQUENCER views. The kit's own knobs (LEVEL and the rest) are there too. Clap, shaker and tambourine have no piece in the picture: choose them there or on the DRUM MACHINE.

- **Fix that came with it:** the picture is now placed on whole screen pixels at 100-200% display scaling, so where you click always matches the drum drawn there.

**Tests:** the kit view's click map, glows and colours checked on the new pictures; every test passes. pluginval passes at strictness 10.

## 0.41.0

**The on-screen keyboard is gone, as you asked.** The strip under the pages is now the SONG track on its own, always there, using the whole strip. The KEYS | SONG switch has gone with the keyboard.

- **Playing a drum by mouse:** click it on the KIT VIEW, a pad on the DRUM MACHINE, or a row of the drum list on the DRUMS page.
- **Your DAW, MIDI controller or e-kit** still plays the kit exactly as before.
- **The DRUM MACHINE** still covers the strip while it's shown.

**Tests:** no keyboard anywhere in the window; the song strip on every page; notes still play the kit. pluginval passes at strictness 10.

## 0.40.0

**A lighter plugin, and PR7-S's progress box.** You asked for a check of the whole code for ways to run better, starting with the drum machine. Every change below was measured before and after; the sound is unchanged (every test still matches its expected sound sample for sample).

**The window (where most of the waste was):**
- **The FX page:** each rack unit redrew itself completely 30 times a second, about 7 ms a time. With four units on screen that was nearly a whole core here. Now only what changes is redrawn: the screen's readout every time, and the meter lights, key lights, knob values and the small screen only when they change. The screen's chrome frame and glass are drawn once and kept. **A unit's 30 Hz redraw: 6.2 ms → 0.36 ms (17× less).**
- **THE DRUM MACHINE:** a glowing pad or the meter used to redraw the screen, every drum name and the step numbers too. Now each part is drawn only when it's in the area being redrawn, and each pad's chrome and key are drawn once and kept. **A pad's glow: 1.25 ms → 0.13 ms (9× less).** Also fixed: changing the chosen drum didn't fully redraw the top of the old drum's name.
- **The SEQUENCER:** the whole grid redrew 30 times a second while a beat played, just to move the playhead. Now only its old and new places are redrawn (2.6 ms → about 1 ms).
- **The SONG strip:** the same for its playhead (it redrew every hit of every block while the song played).
- **The compressor, DOMINION and EQ pages and their PRESET buttons** kept checking their settings 30 times a second even while another page was shown. They now wait until they're shown.
- **The MIXER:** a slip had every strip set its four effect buttons up again 30 times a second. Removed (it did nothing visible).

**The sound engine (it was already light: 2-7% of a core on the factory kits here):**
- **Modelled drums and cymbals:** each of their 16-48 ringing modes was checked for being audible on every sample. The check is now made once every 16 samples, when the levels actually change.
- **The mixer:** all 29 mic channels' working memory was cleared on every pass, used or not. Now only the channels in use, and only the samples needed.
- **Mixer settings** were looked up by name every block. They now go straight to the setting.
- **Together: 11% less work on the Djent Kit** (measured instruction by instruction).

**THE PROGRESS BOX (PR7-S's):** BOUNCE TO AUDIO and loading a kit (once it takes over half a second) now show PR7-S's progress box over the window: what's being done, the bar drawn as PR7-S's meter (lit orange to pink-red), the percentage, and CANCEL while bouncing. It sits inside the plugin's window rather than opening one of its own (a plugin's own windows can misbehave in some DAWs), and the plugin keeps playing meanwhile.

**Found and left for later:**
- **Streamed kits:** reading their samples could be quicker. That's a bigger change, and it can't be measured fairly here, because the result depends on what the computer has cached.
- **The KIT VIEW's hover** redraws the whole picture when the mouse moves from one drum to another.
- **The modelled drums' modes** could add up faster if their summing order were allowed to change. That would change the sound in its last decimal places, so it's left for you to decide.

**Tests:** the progress box (shown, its CANCEL stopping a bounce, gone when done, its bar's colours); everything else as before. pluginval passes at strictness 10.

**Not checked:** a real window on Windows. The timings are from this Linux machine, whose drawing is slower than Windows', so expect smaller numbers there; the share saved should be similar.

## 0.39.0

**This release also carries 0.38.0, THE DRUM MACHINE**, which was never published on its own: one of its new tests compared a knob's value too exactly and failed on the Mac build machine (5 read back as 4.9999995). The plugin itself was fine; the test now allows for that rounding. 0.38.0's notes are below.

**Drum buses, set up for you.** A new project's mixer now groups each drum's channels into a bus of its own:
- **KICK** (BUS 1): KICK IN and KICK OUT.
- **SNARE** (BUS 2): SNARE TOP and SNARE BOTTOM.
- **TOMS** (BUS 3): TOM 1, TOM 2 and TOM 3.
- **Nothing sounds different until you touch them.** A bus at 0 dB adds its channels up exactly (tested: identical to the old routing to a millionth). Now one fader, mute or effect chain moves the whole kick, snare or toms. The overheads and room stay on their own channels, and the kick's level in them doesn't change with the KICK bus.
- **Channels added later follow their drum.** A drum's layer channel and a sample pack's extra mics (KICK SUB, SNARE TOP 2, TOM 4...) start on the same bus as their drum.
- **The buses show their names** on their strips and in the OUTPUT and SEND menus. **Right-click a bus's name to rename it.** Leave it empty and it goes back to its number.
- **Your older projects and presets stay as you left them.** Channels that went straight to their outputs still do, and their buses keep their numbers. BUS 4 to BUS 8 are free as before.

**THE DRUM MACHINE: choose the drum on each pad.** Right-click a pad (or its name) and pick any of the 36 drums for that column. The drum that was there moves to the pad the new one came from, so no drum ever drops off the machine. **Every pad back in the kit's order** is at the bottom of the menu. The TUNE and DECAY knobs and the fader follow the drum you put there. Your pad order is saved with the project.

**Tests:** the default buses and their names, renaming (with undo), a kit played through the buses identical to before, a bus fader moving only its drum, older presets and projects loading as they were, a layer's channel joining its drum's bus, the pads swapping and saved, the face's knobs following a moved drum. pluginval passes at strictness 10.

**Not checked:** a real window on Windows.

## 0.38.0

**THE DRUM MACHINE: a new view on DRUMS, as in the mockup you approved.** Click **MACHINE** (left of KIT VIEW) and the whole window under the tabs becomes a drum machine's face: PR7-K's pearl black, its neon knobs and keys, and PARAGON PR7-D with **ADVANCED DRUM COMPUTER** under it. The drum list, the keyboard and OUTPUT step aside while it's shown; its **VIEW** keys (KIT, DRUM, SEQ) bring them back.

**A column per drum, 18 at a time** (**BANK** 1-18 and 19-36 reach all 36):
- **TUNE**: the drum's pitch (the same as PITCH on the DRUM view). **DECAY**: how long it rings. **The fader**: its level.
- **The pad:** click it to choose the drum and hear it. It glows whenever the drum plays (the beat, your DAW, a groove, the song), and its stripe stays lit while it's the chosen drum. The chosen drum is the same one the DRUM view edits.

**Playing a beat, with or without the DAW:**
- **PLAY** starts the pattern from its first step at the machine's own **TEMPO**, so you can play a beat with the DAW stopped. While the DAW plays, the beat follows the DAW's tempo and position instead, as it always has. **STOP** stops it.
- **REC:** while the beat runs, whatever you play (the pads, the DAW, a MIDI controller) is written into the pattern on its nearest step. A drum already on that step keeps the louder hit. Each take is one undo step.
- **TAP** the tempo in. **SWING** is the sequencer's SHUFFLE, shown the drum machine way (50% straight, up to 75%). **MASTER** is OUTPUT, with its meter.
- **The screen:** the pattern, the time signature, the bar and step, the preset's name, the tempo (marked DAW when the DAW is leading) and the swing.

**Programming the chosen drum:**
- **The 16 step keys** are the chosen drum's steps in the pattern. Click one to add or remove a hit. A longer pattern pages 16 steps at a time with **< >**, and the page follows the beat while it plays.
- **PATTERN A-D** picks the pattern. **COPY** then a letter copies the pattern there.
- **CLEAR** removes the chosen drum's hits (SHIFT: the whole pattern). **FILL** puts it on every step shown.
- **MUTE / SOLO** the chosen drum. **SONG** lit means the beat follows the song (the DAW, or PLAY); off, the patterns play from keys as before.

**How it's built:**
- **Our own design:** the column-per-drum layout of the classic early-80s machines, drawn with the PR7 family's own parts. PR7-K's chassis code is copied in, with one colour changed: a lavender sheen in its pearl white finish is now a pearl pink.
- **Its own finish:** the machine always draws in pearl black. The rest of PR7-D keeps its pearl white.
- **One new setting:** the machine's **TEMPO** (40-300 BPM, 120 to start), saved with presets. Everything else on the face is a control PR7-D already had.

**Tests:**
- PLAY's hits land on the exact sample at TEMPO, from the first step, and stop with STOP; the DAW takes over while it plays.
- REC puts notes on their nearest steps with their velocities; a take undoes in one step.
- The face covers and gives back the window. The step keys, FILL, CLEAR, COPY, the pages, the banks, the knobs and faders, a pad, TAP, MUTE and the screen all do what they should.
- pluginval passes at strictness 10.

**Not checked:**
- A real window on Windows.
- How REC feels when played live. A hit a little before a step is moved onto that step, so on the same pass the step can play it again just after you (more likely at slow tempos).

## 0.37.0

**The KIT VIEW is your bigger kit now.** It's laid out like the kit in the picture you sent, still drawn and lit in our Tron look, and every piece you can see plays.

**What's in it** (left to right, as you sit):
- **Cymbals:** the FX crash with holes, Crash 1, Crash 3, the splash over Tom 1, Crash 4 high at the back, Crash 2 in the middle over the toms, Crash 5 and Crash 6 on the right, the china, the ride over the floor toms, and China 2 at the far right. All on boom stands, the back ones standing behind the toms.
- **Drums:** three rack toms (10", 12", 13"), two floor toms (14", 16"), the snare, the kick, the hi-hat and both pedals.
- **Which drum is which:** the rack toms play HT, MT, LT (high to low, left to right), the floor toms Tom 4 and Tom 5. 0.36.0's Crash 3–6, FX Crash and China 2 are the new cymbals. Each glows when it plays and chooses its drum when clicked, as before.

**How it was matched:**
- **The camera:** I worked out the camera of your picture from where its pieces appear and how flat its round shapes look. Your picture looks down at about 26° from 6–7 m away with a long lens, and our render uses the same view.
- **The pieces:** each one stands where it shows in your picture, at a believable height, sized as a real kit: 22" kick, 14" snare, 17"–20" crashes, a 21" ride, 16"–18" chinas, a 10" splash.
- **Our own design:** our own model and rendering, not a copy of the picture.

**Also:**
- **The ARTICULATIONS panel** moved to the kit view's bottom-left corner. The new kit fills the top-left with crashes; the bottom-left has only stands, so a short panel covers no drum and the hi-hat's tall one only its stands.
- **The toms are named TOM 1 to TOM 5** (the articulations panel, the mixer's bleed list), for the three rack toms and two floor toms. They were RACK TOM, FLOOR TOM 1 and FLOOR TOM 2. Nothing saved changes.
- **A sample pack's Tom 5** close mic gets a USER channel of its own, as Tom 4's does.
- **No tambourine** in the picture: your kit has none. It still plays from its key.

**Tests:** every drum on the picture has its part, and clicking it chooses it (the 23 drums on it, the clap, tambourine and shaker none); the glows cover only their own piece; the pictures keep to the PR7 colours. pluginval passes at strictness 10.

**Not checked:** a real window on Windows. Tell me if anything looks off, or if a cymbal should move.

## 0.36.0

**Seven more drums, for the bigger kit.** You asked for the kit view to match a big kit: six crashes, an FX crash with holes, two chinas, a splash, a ride, three rack toms and two floor toms. PR7-D had drums for only five of those cymbals and four toms. These are the missing ones, first. The new kit picture comes next, in 0.37.0.

**The new drums** (each its own sound, keys, sequencer row, mixer bleed row and settings):
- **Crash 3, Crash 4, Crash 5, Crash 6:** tuned apart from Crash 1 and 2 (higher, lower, brighter, darker), so a row of crashes doesn't sound like one cymbal.
- **FX Crash:** the one with holes, short and trashy.
- **China 2:** a second, slightly higher china.
- **Tom 5:** the lowest tom, below Tom 4.

**Where they are:** the kit list on DRUMS (at the bottom: C3, C4, C5, C6, CFX, CH2, T5), the sequencer, the song editor and the mixer's BLEED table. In ROOT they're the seven keys after the splash's choke, so the kit now spans **C1 to B3**. GROOVE LIBRARY and GENERAL MIDI have no notes for them; give them notes in CUSTOM if your grooves use them.

**Your kits:**
- **Sample packs:** a pack with `CRASH3` to `CRASH6`, `CHINA2`, `TOM5` or an FX crash in its file names now fills these. On import, each new cymbal gets its own channel, as Crash 1 and 2 do.
- **DrumGizmo kits:** a kit with more crashes, chinas or toms than before fills them in order. Any drum a kit has no sound for is switched off, so it plays the nearest drum the kit does have rather than a synth sound among recorded ones.

**Older presets, kits and projects** load with the seven off. Each plays its stand-in instead (Crash 3 plays Crash 1, China 2 plays China, Tom 5 plays Tom 4...), so nothing sounds different. A drum's layer, saved under a number that moved with the new drums, is read back the old way, so it stays on its drum.

**Also:** the song track's drum choices (Remove Drums, Paste Drums, the block editor) handle more than 32 drums now. The mixer's BLEED list fits its 22 pieces on the panel. The DRUMS list's M and S buttons stay readable at 36 rows.

**Tests:** each new drum plays its own sound at its key; Crash 3 switched off plays Crash 1; the new names, channels, groove parts and sample-pack rules; presets from 0.33.0–0.35.x and from before 0.33.0 load with the seven off and their layers on the right drums. pluginval passes at strictness 10.

**Not yet:** the KIT VIEW picture doesn't show the new pieces (0.37.0 brings the new kit). They play from their keys, the list and the sequencer.

## 0.35.1

**Note names now match your DAW: the kick is C1, the snare D1.** You saw your MIDI editor show the snare on D1 and the kick on C1 while PR7-D called them D2 and C2. Nothing was mapped wrong: both send and play the same note numbers (kick 36, snare 38), which is why the right drums played. The names differ because there are two ways to number octaves:
- **Cubase and Yamaha** (and so your PR7-S) call middle C (note 60) **C3**, so note 36 is C1.
- **Roland and most other software**, and PR7-D until now, call it **C4**, so note 36 is C2.

**Now:** a **NOTE NAMES** setting in SETTINGS (beside MAP and CHANNEL), **C3 Cubase** by default, or **C4 Roland**. Every note name in PR7-D follows it: the kit list, the on-screen keyboard, the KIT VIEW, the articulations, THE KEYS list, MIDI IN and its monitor, the sequencer's KEY hints, and the knob that shows ROOT. It isn't part of a preset, so switching presets never changes it.

Only the names change. No note plays anything different, and your saved projects, presets and grooves are untouched.

**Tests:** both conventions name note 36, 38 and 60 correctly; the setting starts at C3, isn't kept in presets, and changing it leaves the note map as it was.

## 0.35.0

**SATURATION: PR7-X's MARS in every mixer strip.** A new effect type, **Saturation**, sits beside Drive in every channel's, bus's and USER channel's four effect slots. It's MARS, the multiband saturator from PR7-X (inspired by FabFilter Saturn 2; our own design), with the same code and sound. Drive is one kind of distortion with a tone knob; MARS colours a drum properly. Warm a kick's low end without fuzzing its click, put tape on the snare's body, or give the room mics some grit.

**What it has** (5 controls a row in PROPERTIES > EFFECTS):
- **BANDS** 1, 2 or 3, split at **X 1** and **X 2** (the crossovers; the bands add back up flat).
- **Each band:** **STYLE** (21: clean / warm / saturated / broken tube, four tapes, three transformers, five amps, and FX styles like Smudge, Foldback and Destroy), **DRIVE** (the level stays even as you turn it up), **DYN** (left: a gate / expander; right: compression that pumps), **MIX** (that band's clean against saturated) and **LEVEL**.
- **The whole slot:** **MIX** (parallel saturation), **QUALITY** (oversampling 1x / 4x / 8x: less harshness on hard drive, more CPU), and the slot's own ON and OUTPUT.
- **Left out of MARS:** its tone knobs per band (use an EQ slot), feedback and modulation.

**CPU** (measured here, 3 bands is the heavy one): about 3% of a core per sounding channel for 1 band at 4x, 7.5% for 3 bands at 4x, 16% for 3 bands at 8x. A strip only works while it has sound (and 3 s of tail), so a quiet channel costs nothing. Put it on a few channels or a bus rather than all 37.

**Also:** a slot now holds 20 settings (12 before). Older presets load as they were.

**Tests:** harmonics appear and the level stays within 3 dB from 30% to 100% DRIVE; all 21 styles play and none blows up; MIX 0 is dry again; with 3 bands, a 300 Hz sine in band 1 picks up harmonics only when band 1 is driven; band 1's LEVEL -6 dB measured; QUALITY 1x and 8x; a preset keeps all 20 settings; the CPU above. pluginval passes at strictness 10.

**Not checked:** listening. The styles are PR7-X's, tuned there, not by ear on drums here.

## 0.34.0

**16 USER channels, and each cymbal on its own channel.** You noticed your Djent Drums cymbals only came through the overheads and one shared CYMBALS channel. The library gives every cymbal its own spot mic (Crash 1, Crash 2, China, Splash and the Ride each have a SPOT file), and PR7-D played them all, but on one channel: you couldn't fader, EQ or compress the china apart from the crashes.

**Now:**
- **16 USER channels** (was 8). Each is a full strip like the others: fader, pan, mute / solo / polarity, its 4 effect slots (EQ, compressor, gate, transient, drive, lo-fi, mod), 2 sends, its own output to the DAW, and routing to a bus.
- **A sample pack's cymbals each get a channel:** importing a pack (LOAD... > A sample pack...) puts each cymbal's spot mic on a USER channel named for it: **CRASH 1, CRASH 2, CHINA, SPLASH, RIDE**. The ride's tip, bell and crash share RIDE, and each choke plays on its cymbal's channel. Their OH and RM CLOSE mics still go to OVERHEADS and ROOM.
- **Your Djent Drums import:** KICK FX, KICK SUB, KICK TRIGGER, SNARE FX, SNARE TOP 2 and TOM 4 stay on USER 1-6 as before; the five cymbals are USER 7-11, leaving 5 free for ROUTE MICS.
- **The hats** were already on their own HI-HAT channel, and still are.

**To get it:** import the library again (DRUMS > LOAD... > A sample pack...). Presets and projects saved with 0.33.0's import still load exactly as they were, with the cymbals on CYMBALS, so nothing you've mixed changes by itself.

**Tests:** the import with the cymbals on their own channel (the crash's spot mic heard only there, its choke too, its OH still on OVERHEADS); 0.33.0's USER 1-4 keeping their numbers; a 0.33.0 import still on CYMBALS; USER 16 playing what's routed to it, through its fader (-6 dB measured) and an effect in its strip (-6 dB). pluginval passes at strictness 10.

## 0.33.2

**The folder you open is read first, and the status line shows the scan working.** You saw GROOVES sit at "0 grooves (checking for new ones)" for five minutes on your Folk Metal folder.

**What I found:**
- **New files went unchecked:** once a DAW session had checked your folders, opening GROOVES again in that session (another PR7-D, or the window reopened) only listed what it already knew. Files copied in since then weren't looked for until MENU > FOLDERS > CHECK ALL FOLDERS FOR CHANGES. That came from 0.15.2, so 10,000 grooves aren't rechecked by every PR7-D you add.
- **A scan walks your folders in order:** a new folder near the end of a big library came in last.
- **Long grooves aren't the cause** (your idea, and worth checking). A 2,000-bar song file, a file with one stray note a million beats in, and a 20,000-groove database all read here within seconds. Only the first 8 bars of a groove are used for searching, so long ones cost nothing extra.

**Now:**
- **The folder you open first:** when you open a folder in the browser that has MIDI files the list doesn't have yet, PR7-D scans, starting with that folder. Its grooves come in within a moment, then the rest of your library is checked as before. This works even if this DAW session already checked your folders.
- **A count you can watch:** while checking, the status line says "N grooves (checking for new ones: X files looked at, Y new)". If X is climbing, it's walking your folders; if it sits still, something is stuck, and that number tells me where to look.
- **Faster scans:** 0.33.1's "list as it reads" redid every groove's details twice a second during a big scan. Now only the new grooves' details are worked out; the rest just get their place in name order.

**Not reproduced here:** a scan that doesn't move at all. I couldn't make one happen with packs laid out like yours. If the count stops moving on your computer, tell me its numbers and which folder you linked (MENU > FOLDERS).

**Tests:** 1,500 new fills and 5 new Folk Metal grooves copied in after the folders were checked; a second PR7-D in the same session doesn't find them on opening, but opening Folk Metal does, and its 5 come in before the fills are read; every file read once.

## 0.33.1

**GROOVES lists new grooves as it reads them.** Before, when you linked a folder (or added files to one), PR7-D read every new file first and only put them in the list at the very end. On a big library that meant a blank list saying "0 grooves (checking for new ones)" for a long time, which looked the same as "it doesn't recognize my MIDI files". Your Loudnoise Folk Metal folder showed that. `.mid` and `.midi` files were always recognized; they just weren't shown yet.

**Now:** while a scan runs, the grooves it has read so far go into the list every half second. A new folder fills in as it's read. The filter columns' counts and the status line ("N grooves (checking for new ones: M read)") keep up too.

**Under the hood:**
- **No extra work:** only the new grooves are worked out each time. Grooves already listed keep their column values and search text; only their place in name order is redone.
- **Unchanged:** the index file is still written once, when the scan ends.

**Tests:** a pack of 400 grooves scanned in the background. Some are listed while the scan is still running, all 400 once it ends, each one once, in name order, with its values worked out.

## 0.33.0

**Your Just Add Drums "Djent Drums" library as a kit, and six new drums for it.** You chose the library's individual mics, so each one mixes on its own channel, plus all of its sounds. A new **LOAD... > A SAMPLE PACK...** reads a multi-mic library's folder by its file names and puts every sound on its drum, every velocity layer and round robin in place, and every mic on its mixer channel, streamed from disk.

**Using it:** DRUMS > LOAD... > **A sample pack: a multi-mic library's folder (onto every drum)...**, and choose the library's top folder (`JUST ADD DJENT DRUMS (WAV & ONE-SHOTS) 071025`) or its `INDIVIDUAL MICS` folder. A summary lists what went where. Then save it as a preset: the preset remembers where the library is, never its audio.

**What goes where** (from the file names you sent: `JAD_DJENT_<PIECE>_<ARTICULATION>_<MIC>_V<layer>_RR<round>.wav`):

| The library's | PR7-D's |
|---|---|
| KICK | BD |
| SNR, SNR CROSS (cross stick) | SD, RIM |
| TOM 1, 2, 3, 4 | HT, MT, LT, **T4** (new) |
| HATS CLOSED, OPEN HALF, OPEN, PEDAL, SHOULDER | CH, HH, OH, PH, **HSH** (new) |
| RIDE TIP, BELL, CRASH | RD, RB, RE |
| CRASH 1, CRASH 2, CHINA, SPLASH | CY, CR2, CHN, SPL |
| ...and each one's CHOKE | **C1C, C2C, CHC, SPC** (new) |

**The mics:**
- **Named channels:**
  - **Overheads and room:** OH → OVERHEADS, RM (and the cymbals' RM CLOSE) → ROOM.
  - **Kick:** DYN → KICK IN, CON → KICK OUT.
  - **Snare (and the cross stick):** DYN1 → SNARE TOP, BTM → SNARE BOTTOM.
  - **Toms 1–3:** DYN → TOM 1–3.
  - **Cymbals and hats:** the hats' close mic → HI-HAT, the cymbals' SPOT → CYMBALS.
- **Its own USER channel each,** named, for the extra mics: KICK FX, KICK SUB, KICK TRIGGER, SNARE FX, SNARE TOP 2, TOM 4.
  - **Starting levels:** FX and TRIGGER start 12 dB down, SUB 6 dB down. These are my guesses, and you mix from there.
- **Levels:** as recorded, so the library's own balance between its drums and mics is kept.

**The six new drums** (slots 24–29), for this library and any kit that has them:
- **T4 (Tom 4):** the lowest tom, with its own mixer piece (TOM 4). General MIDI's note 41 (low floor tom) now plays it.
- **HSH (the hi-hat's shoulder):** the stick's side on the hat's edge, now in the hi-hat's articulations.
- **The chokes (C1C, C2C, CHC, SPC):** a choke stops what's still ringing of its cymbal, then plays its own sound (hit and grabbed).

**Under the hood:**
- **Streaming:** disk streaming now reads hits whose mics are each their own file (this library's way), not only one file holding every mic (DrumGizmo's way). Up to 192 files are kept open; a kit like this plays 18+ hits at once, each up to 7 files.
- **A fix found on the way:** a drum's mic going straight to a USER channel read its bleed setting from past the end of the bleed table, and counted as bleed. It now counts as the drum's own sound.
- **Your older presets, kits and projects** load with the six new drums off, so they sound as they did (their notes play the nearest drum instead).
  - **Layers kept:** a drum's layer (0.29.0) was saved under a number that moved with the new drums. Older files are read the old way, so your kick layer stays your kick's layer.

**Honest limits:**
- **Not heard here:** I have only the library's file names, not its audio. The import was tested on a pack made here with the same names and folders: every drum, mic channel, layer and round robin, the stream past each hit's head, presets. Your ears decide the mix.
- **No rimshot slot:** the library plays its rimshot as the snare's hardest layer (its page says so), so SD2 is switched off and plays the snare.
- **The kit view's picture** has no fourth tom (it's a fixed render). Tom 4 plays from its keys and notes.
- **GROOVE LIBRARY layout:** it has no notes for the shoulder, Tom 4 or the chokes yet. ROOT and CUSTOM layouts play them. Tell me which notes your grooves use for them and I'll add them.
- **The pre-mixed one-shots** (BEEF MEISTER, NATURAL, DRY, BEEF MISTRESS) aren't imported. You chose the individual mics.

**Tests:** our naming rules (every sound and mic word in the library's pattern); a pack scanned (its INDIVIDUAL MICS only, the extra mics in a fixed order, stray files left out); imported and played (each mic alone on its channel at its pitch, the USER channels and their levels, past the 0.2 s head from disk, velocity layers, round robins in turn); a preset keeping and reloading it; the chokes stopping their cymbal; an older preset (the new drums off, the kick's layer kept); every test that counted 23 drums now counts 29.

## 0.32.0

**RIFF MATCH: grooves that fit your riff.** Drop a recording of your guitar, bass or keys (WAV, AIFF, FLAC, OGG or MP3), or a MIDI file of one, and the grooves that would play best with it come first. You hear your riff with every groove you preview, in time. It's our own design, inspired by the riff-to-groove search in commercial drum software.

**Where it is:** GROOVES, beside FIND SIMILAR: **LOAD RIFF...**, or drop the file on the RIFF MATCH box (an audio file dropped anywhere on the page works too). The riff opens in its panel:

- **The waveform:** with the bars at its tempo and the notes heard in it (the strong ones tall).
- **The part used:** lit. Click or drag on the waveform to move its start (its first beat). **BARS** sets how many (1, 2, 4 or 8).
- **TEMPO:** the song's when it opens (a riff from your song is at its tempo), or the one in its name or MIDI file.
  - **x2** and **/2:** steady sixteenths sound exactly like steady eighths at half the tempo, so you may need one of these.
  - **HEARD:** offers the tempo found in the riff itself.
- **LISTEN:** your riff with every groove you preview, from the groove's start, at the riff's tempo. **LEVEL** sets how loud it is against the drums.
- **FIND:** the list sorted by fit (MATCH), with a **FITS RIFF** chip. Filters, folders and preview all still work. The cross on the RIFF MATCH box puts the riff away.

**How it works:**
- **An audio riff's notes:** found by spectral flux, a standard method written here, which hears each note's attack.
  - Tested on generated clean and distorted riffs at 44.1, 48 and 96 kHz: every note found, within about 3 ms, with no false notes.
  - Its strong notes are told from its weaker ones by their level. A heavily distorted riff squeezes its accents; below a clear difference every note counts as strong.
- **A MIDI riff's notes:** its own notes (a chord is one hit), its velocities as how hard.
- **The fit:** a groove fits when its kick lands on the riff's strong notes and few of its kicks land where the riff is silent.
  - A crash on a strong note counts as meeting it, and a backbeat snare on one counts partly. A ridden crash or a snare roll doesn't count: they'd meet every note.
  - Another feel (swung against straight) counts 0.8, another bar length 0.75.
  - The balance between meeting the riff's accents and avoiding stray kicks was chosen by measuring (below), not guessed.

**Measured on your two groove packs (705 grooves with a kick).** For each groove, a riff following its kick, with weaker notes on half the empty eighths and 10 ms of human wobble, rendered as distorted guitar:

| The riff as | Top result right (the same 2-bar kick) | In the top 20 |
|---|---|---|
| Audio | 62% | 88% |
| MIDI | 66% | 89% |

**Honest limits:**
- **Generated riffs:** these riffs are generated, and built from the grooves' own kicks, which is kind to the search. Real riffs aren't always played with the kick; your ears decide. Send me a riff or two and I'll measure with those.
- **Tempo:** the song's tempo is trusted, since a riff from your song is at it. The tempo heard in a short, sparse riff was right only about four times in five (it's offered as HEARD, not used).
- **The first beat:** it's the riff's first clear note. A riff that opens with a rest needs its start moved.
- **Hearing the riff:** a MIDI riff is heard as plain plucked tones. Riffs longer than 5 minutes are cut there.
- **Feel and accents:** swing is judged from where the notes fall. A riff's accents come from level only (not pitch or note length).

**Tests:** notes found in clean and distorted riffs at three sample rates (count, timing, accents); tempo from what's heard, the length, the name and the song; a MIDI riff (chords, tempo, time signature, sound); the fit (a locked groove first, crashes, a ridden crash, feel, bar length); the whole path from audio; the riff heard from the preview's start, looping its part to the sample, at its tempo and its own sample rate, silent with LISTEN off; the page (a riff finds the groove that follows it, its chip, searched again when the part changes, put away).

## 0.31.0

**FIND SIMILAR, retuned on real grooves.** You asked for an audit of FIND SIMILAR (tapping a rhythm, dropping a MIDI file, or searching with a groove's rhythm). I measured it on your two groove packs (Heavy Rock Grooves and EZX Electronic, 762 grooves) by simulating a player tapping each groove's first two bars: exactly, with a human wobble of 15-25 ms, late by 30-40 ms as audio latency makes taps, and as a sketch without the ghost notes. Then I checked whether the top result had the same main kick and snare pattern. This release fixes every weakness the audit found.

**The results, top result right, before → after** (QUANTIZE 1/8, the default, unless it says otherwise):

| What you do | Before | After |
|---|---|---|
| Drop a MIDI file (or search with a groove) | 72% | 100% |
| Tap kick + snare like a person | 45% | 85% |
| Tap kick + snare on 1/16 | 84% | 94% |
| Tap kick + snare, 40 ms late (latency), 1/16 | 61% | 91% |
| Tap kick + snare + hats or ride | 49% | 90% |
| Tap a sketch: the main hits, no ghost notes (in the top 20) | 58% | 62% (78% on 1/16) |

**What changed and why:**
- **Taps are compared where you played them.** Before, your taps and the grooves were both rounded onto the QUANTIZE grid. On 1/8 every sixteenth sat exactly on the edge between two cells, so a few milliseconds decided where it went, and many grooves tied. Now a groove's hit counts as found when it's within half a QUANTIZE step of your tap (on 1/8: a sixteenth either way), the closest pairs first.
- **Hits you didn't tap count lightly.** Before, every groove hit you didn't tap counted against it as much as a tap it lacked, so a groove with your backbeat plus ghost notes lost to a plain one.
  - **Your taps are a sketch:** a groove's other hits now count a quarter.
  - **Ghost notes count a tenth:** a ghost note is under 55% of that drum's loudest hit.
  - **Detail finer than QUANTIZE** (sixteenths when you tap on 1/8) also counts a tenth.
- **Latency is forgiven.** You hear the metronome late by your audio interface's delay, so taps land late. Your taps may now all be up to 40 ms late: the search tries them as played and up to 40 ms earlier, keeping the best. Only lateness is forgiven, not earliness.
- **Grooves keep their velocities.** The groove index now stores each hit's velocity, which is how ghost notes are known. Your folders are read again once, in the background, the first time GROOVES opens (packs too).
- **The clap counts as a snare.** Electronic grooves often put the backbeat on a clap. The pedal hat still isn't counted: it's a foot, not the hand you'd tap.
- **Time signatures:** a groove in another bar length (3/4 against your 4/4) counts three quarters as much, so grooves in your time signature come first when they're equally close.
- **Playing a drum again in the same place** on a later pass round the loop now replaces the earlier tap instead of adding a second, slightly different one.
- **Dropped files and "search with its rhythm" use up to four bars** (two before).
- **Speed:** a search over the 762 grooves takes 6-20 ms here (about a quarter of a second for 10,000). Clicking filters or folders afterwards reuses the scores, since they're remembered until you search again.

**Honest limits:**
- **Simulated taps:** the taps are simulated. A real player may be messier, or steadier.
- **Ghost notes in your packs:** your packs' velocities are our guess (they mark accents, not velocities), so the ghost-note rule only helps with MIDI files that have real velocities.
- **Sketches:** a sketch of only the main hits finds grooves close to that sketch first. A busier groove that contains your sketch (with double kicks, say) is usually in the top 20, not first. That's partly right, since a groove that *is* your sketch is a good answer, but tapping a little more of the detail finds it sooner.
- **Our own design:** Toontrack doesn't publish how Tap2Find scores, so this is measured against your packs, not against theirs.

**Tests:** sixteenths no longer on a knife edge at 1/8; ghost notes and loud extra hits scored as above; taps 39 ms late at 200 BPM found only with the allowance, early ones not moved; the clap as a snare; 3/4 against 4/4; velocities in the index (and an older index read as full); four bars searched; a repeated tap replacing the first.

## 0.30.0

**A quicker window, GROOVES above all.** You found the whole interface sticky, the GROOVES tab worst. I measured it here with a library of 3,000 grooves (a test that opens the window, plays a groove and times every click and repaint), and found three causes:

- **Every click on GROOVES redid everything from scratch.**
  - **Folders:** it read every folder in the browser from the disk again, several times a click.
  - **Filter counts and sorting:** it worked out each groove's filter values (GENRE, TYPE, TEMPO...) and its place in the list again, for every groove.
  - **Now:** a folder is read once (again when the library changes, or on REFRESH), and each groove's values, search text and place by name are worked out once, when the library lists it.
- **The window's background was drawn again with every small repaint.** A meter moving, a playing groove's progress line or a hovered button repainted the gradient, the logo and the frames under it: the window's own drawing was most of every full repaint. Now it is drawn once into a picture at your screen's own pixel size (HiDPI too) and copied; the GROOVES page's panels likewise. It looks the same: compared pixel for pixel with the old drawing at 1x and 2x, no pixel differs by more than 3/255.
- **Hidden pages kept working.** The FX pages' meters and displays, the MIDI monitor and the level meters ran their timers while another page was showing. Now they rest while hidden.
- **Also:** the playing groove's row is remembered instead of being searched for 15 times a second, and only its row repaints (the whole list did when a groove started or stopped).

**Measured (3,000 grooves, this machine), before → after:**

| What | Before | After |
|---|---|---|
| Opening a folder you've opened before | 74 ms | 4 ms |
| Opening a folder the first time | 74 ms | 35-48 ms (the disk's part) |
| Ticking a filter | 10-11 ms | 3-5 ms |
| CLEAR ALL FILTERS | 19 ms | 3.5-5 ms |
| Searching | 6 ms | 3.3-4.5 ms |
| Redrawing the whole window | 24-31 ms | 14 ms (mostly the test's own picture-taking) |

**Honest limits:**
- **Measured on Linux without a real window** (the container can't open one): the numbers are the work done on the window's thread. Windows draws differently (on the graphics card), so your gain may differ.
- **Folders the first time:** opening a folder for the first time still reads its subfolders from the disk. On a slow or network drive that's the part you may still feel.
- **Tell me where it still sticks:** the page and the click. With that I can measure the exact case.

**Tests:** the cached values against the ones worked out each time (every column, the filters, the name order).

## 0.29.0

**LAYER: your own sample with any drum.** You found the snare very good but the kick "still not there", and sent your perfect kick. Modern metal kicks are almost always the real kick plus a sample layered on it, so any drum can now have a **LAYER**: a sample of yours, or a folder of several hits, played with every one of its hits.

- **Loading one:** DRUMS > choose the drum > LOAD... > **A LAYER WITH THIS DRUM...**: a sample, or a folder of several takes (they play in turn).
- **Its knobs** (on the DRUM view):
  - **LAYER** sets its level.
  - **LAYER OUT** sets where it plays: with the drum, or a mixer channel of its own.
  - Remove it from the same LOAD... menu.
- **On a kit that plays through the mixer** (the CrocellKit, or SIMULATED MICS), loading a layer:
  - gives it a **USER channel of its own**, named after the drum ("KICK LAYER"), with its own fader, effects, sends and output;
  - turns the drum's own **close mics down 10 dB**, so your sample leads while the real kick's overheads, room and bleed stay as they are (one undo puts it back).
- **How it plays:**
  - **Velocity:** it follows the drum's (soft hits quieter).
  - **Timing:** it's sample-aligned with the drum.
  - **Choke and voices:** it's cut, choked and faded with the hit it belongs to.
  - **Saving:** presets, kits and projects keep it (where its file is, like an instrument; not the audio inside the preset).
- **Measured with your kick on the Crocell Metal kit:**
  - **Pitch:** 90 → 69 → 63 Hz, against your sample's 89 → 67 → 63.
  - **Click:** 3.5 kHz, as yours.
  - **Body:** 44 Hz, as yours.
  - **Spectrum:** within a few dB of your sample band by band.
  - **The tail:** it rings longer than your sample alone (the real kick's room, which the metal mix compresses). The ROOM fader sets how much.
- **Honest limits:**
  - **One sample:** every hit is the same take (only louder or softer), which can sound machine-like on fast double kick. A folder of several takes of your kick fixes that: they play in turn.
  - **LAYER OUT names:** it lists the user channels by number ("User 1"); the mixer shows their names.
  - I can't listen. The numbers are measured; your ears decide the balance between the layer and the real kick.
- **Tests:**
  - the layer plays with each hit, at LAYER level and the hit's velocity, OFF silent, removed;
  - through the mixer: its own USER channel (its name, fader, mute, solo) and the drum's close mics 10 dB down;
  - a folder's hits in turn;
  - presets, a preset's file and the project keeping it.

## 0.28.0

**Disk streaming: the whole kit, every hit, at full quality.** You asked for the highest quality we can get. A DrumGizmo kit can now load **STREAMED**: every recorded hit, every mic, every cymbal's full ring (the CrocellKit's ride rings for 31 s), with only each hit's first 0.2 s held in memory. The rest is read from disk while it plays, as big sampled drum libraries do.

- **The CrocellKit streamed:**
  - **All 655 recorded hits** of the drums it plays, against 254 in 0.27.0's LIGHT.
  - **Memory:** 377 MB, against about 6 GB with the whole kit in memory.
  - **Load time:** under 2 s here.
- **A07 Crocell Metal now streams.** LOAD... > A DRUMGIZMO KIT... offers STREAMED (best), LIGHT (trimmed, all in memory) and FULL IN MEMORY.
- **How it works** (our own design):
  - When a hit starts, a background thread reads the rest of its file ahead of where it's playing, about a third of a second ahead.
  - A hit's 15 mics come from one file, so the file is read once and split into all its mics.
  - Up to 48 hits stream at once, as many as the kit plays.
  - Each stream is freed as soon as its hit has finished.
- **Live playback never waits for the disk.** If the disk ever fell behind, the missing bit would be silent and counted, never a stall.
- **Bouncing waits for the disk** (BOUNCE TO AUDIO and any offline render), so a bounce is always complete.
- **Fixed:**
  - A replaced kit was never freed from memory (since 0.19.0): loading kits again and again kept every old one. Now each is freed once its longest hit can no longer be playing, plus 2 s.
  - The bounce now runs as an offline render.
- **Measured here:**
  - A 180 BPM double-kick beat with crashes, china and ride, played four times faster than real time: no late samples, up to 18 hits streaming at once.
  - About 11% of one core for that beat. At its own pitch each sample is read once, without interpolation; it's the same sound for a quarter of the work.
- **Honest limits:**
  - This computer's disk is fast. On yours, keep the kit on an SSD if you can: a busy beat reads about 50 MB/s from 15-mic files, and a spinning hard disk may fall behind.
  - As before, I can't listen. The streamed sound is identical to the in-memory sound, which the tests measure.
- **Tests:**
  - streamed hits hold only their heads but play exactly as the same kit in memory (to float rounding), both bouncing and live with the disk thread reading;
  - no late samples live;
  - every stream freed afterwards.

## 0.27.0

**A real recorded metal kit: DrumGizmo kits, and the CrocellKit.** You said the modelled drums, the snare and cymbals worst, sound fake and thin, and chose samples. PR7-D now loads **DrumGizmo kits**: free, multi-mic drum kits recorded in real studios, in an open format. The one to start with is the **CrocellKit**, the kit of the Danish metal band Crocell, recorded with 15 mics through a studio's setup.

- **Getting the kit:**
  - DRUMS > LOAD... > **GET FREE KITS...** opens drumgizmo.org's kits page. Download the CrocellKit (5.6 GB) and unzip it anywhere.
  - Then DRUMS > LOAD... > **A DRUMGIZMO KIT...**: choose `CrocellKit_full.xml`, then LIGHT or FULL.
  - PR7-D doesn't include the kit. It loads it from your computer, which keeps the plugin small and within the kit's licence.
- **LIGHT or FULL:**
  - **LIGHT** (about 1.1 GB of memory, a few seconds to load): up to 8 velocity layers of 2 hits each (cymbals 6 layers). Tails are trimmed to 4.5 s (cymbals 5 s) with a smooth fade. Bleed into other drums' mics is kept for its first second. Mics that hardly hear a hit are left out.
  - **FULL:** every hit, every mic, full length (about 6 GB for this kit).
  - Disk streaming comes next, so the full kit can play without filling memory.
- **What goes where** (our rules, from the kit's names and its General MIDI note map):
  - **Kick:** both feet of its double kick, alternating, so double-kick runs don't machine-gun.
  - **Snare:** center, rimshot (SD2) and sidestick (RIM).
  - **Hi-hat:** closed, loose ("closed, no pedal": OPEN 1), half open, open and pedal.
  - **Ride:** bow and bell.
  - **Cymbals:** two crashes, china and splash.
  - **Toms:** three of its four toms.
  - Articulations it has no hits for are switched off, so their nearest sibling plays.
  - Left out: its stopped crashes, the hat's pedal splash, the snare's hand-rest hits, its second china and splash, and the fourth tom.
- **Its 15 mics go into THE MIXER's channels**, with the real bleed between them:
  - the kick in and out;
  - the snare top and bottom;
  - the hi-hat and ride mics (the ride into CYMBALS);
  - the toms;
  - the overheads as a stereo pair, with the centre overhead in both sides;
  - the ambience pair as ROOM.
- **Level:** kits are recorded with headroom (the CrocellKit's loudest hits peak at -12 to -16 dBFS), so each drum is brought up as it loads until its hardest hit peaks at -8 dBFS. Every mic of a drum gets the same gain, so its bleed and close mics keep their recorded balance.
- **A07 Crocell Metal**, a new factory preset: the CrocellKit with a metal mix of our own on THE MIXER, and the Djent Kit's four grooves.
  - **The mix:** the kick's click up and its boxiness cut, a compressed kick; the snare bright and compressed; gates on the toms (less bleed ring); the overheads high-passed; the room squashed underneath; a master compressor and LIMITER.
  - Load the kit once with LOAD... > A DRUMGIZMO KIT... and the preset finds it after that. PR7-D remembers where each kit is, in `Sample Kits.xml` in the presets folder.
- **Credit:** the CrocellKit is CC-BY 4.0 (kit by Crocell, mics by JBOSound, sampled by Lars Muldjord). It's credited in SETTINGS > ABOUT and in the preset's notes; credit them when you release music made with it.
- **Honest limits:**
  - These are real recordings, but I can't hear the result. The levels are measured and I rendered demos for you; the metal mix is my starting point, so tune it by ear.
  - It's one kit's sound, a 2017 studio metal kit, not a djent kit tuned for you.
- **Tests (a tiny DrumGizmo kit of test tones, made in the test):**
  - each mic channel to its mixer channel and side;
  - which instruments go to which drums;
  - layers by power, velocities rising;
  - a stereo pair and a centre mic;
  - LIGHT's trimming (bleed cut to 1 s, silent mics left out);
  - the level;
  - the whole kit through the processor and mixer;
  - a preset finding a kit by name, and saying so when it's missing.

## 0.26.0

**Simulated mics and a room for the synth drums.** The modelled kit (and any synth drum or single sample) can now be heard the way a recorded kit is: through close mics, a pair of overheads, a room pair and a far ambience pair, each in its own MIXER channel, with the other drums bleeding into each close mic and a modelled room around it all. Every mixer tool now works on synth drums too: faders, bleed, sends, effects, buses and ROUTE MICS.

- **SIM MICS...** (on the MIXER page, beside ROUTE MICS) opens the panel:
  - **SIMULATED MICS:** on or off. It's off unless you switch it on, so your presets and projects sound as they did.
  - **ROOM:** Small Room, Studio, Live Room or Big Hall.
  - **DECAY:** the room's ring, from 0.3 to 3 times its own.
  - **TONE:** the room dark or bright.
  - **DRUMS THROUGH THE MICS:** a button per drum. Switch one off (an 808 clap, say) and it plays its own way again, with its OUTPUT, INSERT and sends.
- **How it works** (our own design, nothing measured from a real room or mic):
  - Every piece and mic has a place around a right-handed kit, in metres from the drummer's seat. Sound reaches each mic late by its distance (343 m/s) and quieter by it.
  - The overheads hear the hi-hat on the left first. The room is about 3 m away, the ambience about 5.5 m. TIME OFFSET in the mixer still shifts a channel.
  - **Bleed:** a close mic hears the drums it isn't pointed at quieter and duller (the snare in the hi-hat mic is about 10-20 dB down). The channel's BLEED and the BLEED FROM INSTRUMENTS table set it, as for a sampled kit.
  - **Each mic has its own colour**, our choices:
    - kick in: the beater's click, less box;
    - kick out: the boom;
    - snare bottom: the wires;
    - hi-hat and cymbal mics: low end cut;
    - overheads: airy;
    - room: warm;
    - ambience: dark.
  - **The room:** early reflections, then a reverb tail. The room mics hear both; the ambience hears the tail later and louder.
- **Level:** a hit through every mic, with every fader at 0 dB, is about as loud as it was without them.
- **The Djent Kit (A06)** now plays through the simulated mics in a Live Room, with the room 6 dB and the ambience 12 dB down (close and tight, as djent usually is). Move the ROOM and AMBIENCE faders for more room.
- **Fixed:** a USER channel's level meter (0.23.0) wrote past the end of the meters' table. It now shows its own level.
- **CPU:** the Djent Kit's beat through every mic and the room takes about 8% of one core here.
- **Honest limits:**
  - I can't listen to it. The timings and levels are measured, but whether it sounds like a real room is up to your ears.
  - The room's sound stays in the ROOM and AMBIENCE channels even if ROUTE MICS moves a drum's room mic elsewhere (that only moves its direct sound).
  - The snare bottom mic comes in phase with the top, as if already flipped.
- **Tests:**
  - off by default (nothing changes);
  - each mic hearing the drum on time to the sample;
  - the overheads' left before right;
  - bleed quieter and duller, set by BLEED and BLEED FROM INSTRUMENTS;
  - the room's tail longer for a bigger room and more DECAY, brighter with TONE;
  - the level kept;
  - a drum left out;
  - routing to none and to a USER channel (its meter);
  - kits and presets keeping it;
  - the panel;
  - the CPU.

## 0.25.0

**Modelled cymbals: crash, ride, china, splash, stack, hi-hat and hat pedal, all built from scratch** (the second part of building drums from the physics). The Djent Kit now has them too. **This release includes 0.24.0** (the modelled kick, snare and toms, below). 0.24.0's own build was stopped by a known crash in the Linux test host after every test had passed, so it was never published on its own.

- **Seven new models:** ACOUSTIC CRASH, RIDE, CHINA, SPLASH, STACK, HI-HAT and HAT PEDAL. They're our own design from how cymbals behave, with no recordings:
  - **The plate's tones:** 40 inharmonic frequencies, each cymbal type with its own fixed "voice". They're loudest in the cymbal's bright region, and the high ones die first, so a crash darkens as it rings.
  - **The wash:** struck hard, a real cymbal's energy spreads into a dense shimmer that blooms just after the hit. Here that's noise in eight bands, built up over tens of milliseconds (faster on a harder hit), shimmering slowly, its brightness falling as it dies.
  - **The bell:** four strong partials that ring long, the ride bell's "ding".
- **What the knobs do on a cymbal:**
  - **STICK:** a soft or a hard tip.
  - **PING:** the stick's click, clearest on the bow. On the china it's **TRASH**.
  - **BELL > EDGE:** the bell's ding, the bow's defined ping, or the edge's big wash.
  - **WASH:** how much it blooms.
  - **OPEN** (hi-hat): from tight and closed to fully open.
  - **DECAY:** how long it rings.
  - **VARIATION:** small differences between hits.
- **Each type:**
  - **Crash:** a fast bloom and a long wash, about 6 s to silence.
  - **Ride:** a defined ping over a quieter wash, about 8 s.
  - **China:** a honky middle and a hard bloom, soft-clipped into trash.
  - **Splash:** small, high, about 2 s.
  - **Stack:** two cymbals clamped together, a short trashy burst.
  - **Hi-hat:** OPEN sets how long it rings (closed about 0.15 s, open about 3 s), the two plates rattling most when half open.
  - **Hat pedal:** the foot's chick, with a little low thump.
- **The Djent Kit (A06)** now uses them:
  - the six hi-hat slots at their openings (closed, open 1, half, open 3, open), with the pedal; the hi-hat pedal (CC 4) steps through them as before;
  - two crashes;
  - the ride's bow, bell and edge;
  - a trashy china;
  - a stack in the splash's slot (SPL).
- **Every hit is still -3 dBFS at the defaults**, soft hits darker and quieter.
- **CPU:** hats, ride and china on eighths with an open hat, crash and stack on half notes take about 9% of one core here.
- **Honest limits:** I can't listen; the measurements match how cymbals behave, but your ears decide. Tell me what's off (too dark, too long, not trashy enough...) and I'll tune them.
- **Tests:**
  - a crash ringing for seconds and darkening as it dies; a stack, a splash and a closed hat short;
  - the hat longer as it opens;
  - WASH blooming after the strike;
  - a soft hit darker;
  - the ride's bell partials standing out when struck there;
  - the china's trash squashing its peaks;
  - the pedal's thump;
  - the CPU.

## 0.24.0

**Modelled acoustic drums, built from the physics, and a djent kit made from scratch** (the start of your plan to rely on nobody's samples). As you chose: drums first, added as new models, so every existing preset sounds exactly as before.

- **Three new drum models: ACOUSTIC KICK, ACOUSTIC SNARE, ACOUSTIC TOM.** Choose them in any drum's DRUM menu. They aren't samples or analog-style tones. Each is computed from how a real drum vibrates (our own design, built from published drum physics, no recordings):
  - **The head rings in many frequencies at once** (up to 24), in the ratios a real circular drumhead has, pulled a little closer together as the air and the second head make them.
  - **Where you hit matters (POSITION):** the centre is fat and round (the overtones with a node through the middle stay quiet); towards the edge it gets thinner and ringier.
  - **What you hit with matters (BEATER on the kick, STICK on snare and toms):** soft felt is dark and round; hard plastic or wood is bright and clicky.
  - **How hard you hit matters:** a harder hit is brighter, and its pitch starts a little high and settles (a struck head tightens as it swings), as a hard-hit tom does.
  - **DECAY is the muffling:** less of it is a pillow in the kick or a towel on the snare, the high overtones dying first.
  - **The kick:** its beater CLICK, and its FRONT HEAD ringing on beside the batter. Less front head is like a port cut in it.
  - **The snare:** its WIRES buzz with the head and stop once its swing falls below their tension, before the head stops ringing, as real wires do; the shell's own ring.
  - **The toms:** their stick ATTACK, the shell's ring.
  - **VARIATION:** each hit lands a little differently (where, how hard, the contact, a few cents of tuning), so fast fills don't sound like a machine gun. 0 makes every hit identical.
- **The DRUM view** shows POSITION, SHELL (FRONT HEAD on the kick) and VARIATION while one of these is chosen, and names TONE and SNAP for what they do on that drum.
- **A06 Djent Kit** (a new factory preset):
  - a tight, clicky kick (hard beater, heavily muffled, tuned up a little);
  - a high, cracking snare, with a rimshot (the same snare hit at the edge, harder);
  - three deep toms;
  - the synth cymbals with a china (modelled cymbals come next);
  - the master compressor (FET, letting the attack through) and the limiter.
  - Its four grooves: A syncopated half time, B double-kick sixteenths, C a ghost-note groove, D a tom fill.
- **CPU:** six acoustic drums each hit every sixteenth note take about 7% of one core here. Each mode is computed with two multiplies a sample, and a hit stops once it's inaudible.
- **Honest limits:**
  - I can't listen. Everything is checked against the physics with measurements, but whether it sounds right is your call. Tell me in plain words what's off ("kick too boomy", "snare needs more crack") and I'll adjust.
  - The djent kit's settings are my starting point, not tuned by ear.
  - Cymbals and hi-hats are still the old synth models until 0.25.0; synth drums still don't go through the mixer's channels until 0.26.0 (mics and room).
- **Tests:**
  - the snare's mode frequencies, and PITCH moving them;
  - a centre strike missing the off-centre modes;
  - a harder striker and a harder hit being brighter;
  - a hard hit's pitch glide (and none on a soft one);
  - muffling killing the high modes first;
  - the kick's front head ringing on, its click;
  - the snare wires stopping while the head still rings;
  - VARIATION, the preset, the CPU, every model at -3 dBFS.

## 0.23.0

**Route Instrument Microphones, and channels of your own** (from chapter 9.4.1 you sent; our own look and words). As you chose: up to 8 new user channels.

- **ROUTE MICS...** (on the MIXER page, beside MICS / BUSES): the manual's three columns over the strips.
  1. **The drum:** every drum that has a multi-mic sound loaded.
  2. **Its mics:** the close mics first, then the bleed mics.
  3. **Where each mic goes:**
     - **Auto:** its own channel, as before.
     - **None:** that mic isn't heard at all, e.g. the snare in the kick mic.
     - **Any other channel.**
     - **Create New Channel:** a channel of its own, e.g. the ride's overhead mics apart from the rest of the kit (the manual's second example).
- **USER CHANNELS** (up to 8, after the mic channels, before the buses). A strip appears once a mic is routed to it. Each is a full channel: fader, pan, mute / solo / polarity, bleed, time offset, release, 4 effects, 2 sends, a bus or an output.
  - A new one is named after the drum and mic (e.g. SNARE ROOM), and starts going where that mic's own channel went (its output or bus). Right-click its name to rename it.
- **A routed mic stays what it was.** A close mic stays direct, a bleed mic stays bleed: the sends' DIRECT / BLEED and the channel's BLEED knob treat it the same. It also keeps the bleed level set in its own channel's BLEED FROM INSTRUMENTS. These are my own rules; the manual doesn't say.
- **Routing is per drum, not per articulation:** routing the ride routes its bow, bell and edge together.
- **Presets** keep the routes and the user channels' names. Their outputs stay with the project, as the other channels' do.
- **Not here yet:**
  - the manual's "X-Drums" (adding a second kick to the kit) and stacked drums;
  - routing a synth drum (they don't have mics yet).
- **Tests:**
  - a mic to NONE, then back to AUTO;
  - a mic into another channel (muting its old channel no longer touches it);
  - a new channel: its name, its output taken from the mic's channel, its own fader exactly -6 dB, its own EQ +6 dB;
  - a bleed mic staying bleed in a user channel;
  - renaming, presets;
  - the panel's drums and mics (close first), and the strips appearing.

## 0.22.0

**Effects on every mixer channel and bus** (from the Mixer chapter's effects pages; our own effects, look and words). As you chose: the mix core plus the colour effects, in 4 slots per strip.

- **Four EFFECT slots** on every mic channel and bus (the buttons under BLEED). They work in order from the top, before the pan and fader, as in the manual. Click a slot to choose an effect, switch it ON or off (bypassed without a click, its settings kept), copy, paste or remove it.
- **PROPERTIES > EFFECTS**: the four slots with their ON and effect, a compressor's or gate's gain reduction beside it, and the chosen slot's knobs and OUTPUT.
- **The effects:**
  - **EQ**: LOW CUT, a LOW shelf, two bells (MID 1, MID 2, each with Q), a HIGH shelf, HIGH CUT. These are six bands of the plugin's own parametric EQ, with no latency.
  - **COMPRESSOR**: the plugin's compressor (VCA / OPTO / FET) with THRESHOLD, RATIO, ATTACK, RELEASE and MAKEUP.
    - **SC HPF**, as in the manual: it filters only what the compressor hears, so a kick's lows don't pump it.
    - **MIX**: parallel compression.
  - **GATE** (new, our own design):
    - opens above THRESHOLD, stays open for HOLD, then closes over RELEASE, down by RANGE (less than 80 dB means it only turns down);
    - its side-chain HPF and LPF decide what opens it, e.g. the snare and not the hats in its mic;
    - it starts shut, and moves in decibels: opening the whole RANGE takes ATTACK, closing it takes RELEASE.
    - The manual's gate also has an expander (a second threshold and ratio); that's left out for now.
  - **TRANSIENT** (new, our own design): ATTACK lifts (or softens) each hit's front, SUSTAIN lengthens (or shortens) its ring. It compares fast and slow followers of the sound's level, as transient shapers do; the exact amounts are our own.
  - **DRIVE**: the plugin's drive pedals (one a side, with the same DC blocker as the insert buses).
  - **LO-FI** and **MOD** (chorus, flanger, phaser...): the plugin's own.
- **A new send mode, PRE EFFECTS:** the send taps before the strip's effects and fader. PRE FADER now means after the effects, before the fader.
  - Once a strip has an effect loaded, its mic's direct and bleed sound go through the effect together. So only a PRE EFFECTS send can still send them apart; later sends take the processed sound at the DIRECT on, level and pan. Without effects, nothing changes.
- **Tails:** a strip with effects keeps running for 3 s after its sound stops, so a chorus or a compressor's release isn't cut off.
- **Presets** keep every slot (effect, on, output and settings), as preset data like the sends, not as automation parameters.
- **CPU:** a multi-mic kit hit every sixteenth note, with gate, EQ, compressor and transient on 4 channels and 2 buses, measured about 8% of one core here. Empty slots cost nothing.
- **Not copied:** the manual's 25-odd effects (reverbs, delays, tape, exciters, the named compressor models), effect presets, side-chaining from another channel. Reverb and delay stay as sends.
- **Not checked here:** how they sound to your ears on real kits.
- **Tests:**
  - the EQ's bell exactly +6 dB and its low cut;
  - the compressor's reduction and meter, MIX 0 dry;
  - OUTPUT -6 dB, and bypass sounding as if it weren't there;
  - the gate shut, open, its RANGE, and staying shut when its side chain is high-passed above the sound;
  - the drive's 3rd harmonic;
  - a chorus's tail after the sound stops;
  - the transient shaper's front and ring;
  - PRE EFFECTS against PRE FADER sends;
  - a bus's EQ;
  - presets, the page, and the CPU.

## 0.21.0

**The MIXER's buses and sends** (from the Mixer chapter's channel strip, buses and Mixer menu; our own look and words). As you chose: 8 buses that also reach the plugin's own effects, and the Mixer menu's options.

- **8 BUSES**, after the mic channels (golden orange). Each has a fader with a meter, PAN, M / S / Ø, an OUTPUT and two sends. Use them to group channels, e.g. both snare mics into one bus with one fader.
  - **A bus goes only into a later bus** (BUS 1 into BUS 2, never back), so nothing can feed itself. That's our rule; the manual doesn't say.
  - **SOLO works among the channels and among the buses apart.** Soloing a channel doesn't silence the buses its sends reach; soloing a bus silences only the other buses. Also our own rule.
- **OUTPUT** on every channel and bus now lists:
  - MAIN and OUT 2 to 16, as before;
  - BUS 1 to 8;
  - FX BUS A to D: the FX page's insert buses, with their DRIVE / GLITCH / MOD / LO-FI. An FX bus goes on to its own output, as set on the FX page.
  - Presets keep the buses a channel goes to; the OUT 2-16 choices stay with the project, as before.
- **Two SENDS** on every channel and bus (the buttons under BLEED). Click one to choose where it goes: a bus, an FX bus, the DELAY or the REVERB (the send only sounds while that effect is on). Its settings are in PROPERTIES > SENDS:
  - **POST FADER** (it follows the channel's fader and mute) or **PRE FADER** (before them).
  - **DIRECT** (the drums the mic is meant for) and **BLEED** (the rest of the kit in it): each with its own on, level and pan, and **LINK** to move the two levels together. Turn the bleed off to compress a bus hard on the close sound alone, as the manual suggests.
  - A send has its own pan, not the channel's. Pre-effects / post-effects sends come with effects per channel.
- **MENU...** (the Mixer menu):
  - **Channel mute: before the sends (PRE SEND, the default) or after them (POST SEND).** With POST SEND, a muted channel's pre-fader sends go on, so you can hear only the snare's reverb.
  - **Stereo reverse:** every pan, the buses' and sends' too, to the other side.
  - **Stereo outputs:** every channel and bus to MAIN, off the buses.
  - **Multichannel outputs:** the channels to OUT 2-9 by instrument: kick OUT 2, snare OUT 3, hi-hat OUT 4, toms OUT 5, cymbals OUT 6, overheads OUT 7, room and ambience OUT 8, other OUT 9. This grouping is my own choice. Switch those outputs on in your DAW; until then they play in MAIN.
- **MICS / BUSES** show or hide each group. The strips scroll sideways when they don't all fit.
- **Not checked here:** how it sounds in a real mix, and how it feels to use in your DAW.
- **Tests:**
  - a channel into a bus: the bus's fader exactly -6 dB, its pan, mute, polarity sample for sample, and its output;
  - bus into a later bus, and never back;
  - solo among buses;
  - sends: pre / post fader at their levels, the mute modes, direct and bleed apart (on, level, pan);
  - the REVERB heard after the hit only with a send to it;
  - an FX bus routed to OUT 2;
  - stereo reverse, stereo / multichannel outputs;
  - presets keeping routes, sends and the mute mode but not outputs;
  - the page's buttons and menu.

## 0.20.0

**The sample engine, part 2: mic channels, bleed and the MIXER page** (from the Mixer chapter you sent; our own look, channels and words).

- **Multi-mic kits keep their mics apart.** In 0.19.0 a hit's mic recordings were mixed into one. Now each mic plays through its own channel:
  - kick in, kick out, snare top, snare bottom, hi-hat;
  - toms 1-3, cymbals;
  - overheads, room, ambience, other.
- **Where each mic goes.** Recognised from its SFZ label (group / region / master label), or the sample's folder and file name ("Kick In", "OH", "Room", "SnareBottom"...).
  - **The words it recognises are my own rules.** If a kit's mic lands in the wrong channel, send me its names and I'll teach it.
  - **Folders work too:** a folder with no hits of its own, but a subfolder per mic ("Tom 1", "OH", "Room"), is a multi-mic drum. Each hit is matched by its file name in every mic's folder.
- **The MIXER page** (a new tab after GROOVES): a strip per channel with:
  - **BLEED:** on, and its level;
  - **PAN** and a **fader** with a meter;
  - **M** (mute), **S** (solo) and **Ø** (polarity);
  - **OUTPUT:** MAIN or OUT 2 to 16, kept with the project like the drums' outputs;
  - its name: click it to choose the channel.
  - Channels no loaded kit plays into are dimmed.
- **PROPERTIES** (the chosen channel):
  - **CLOSE MIC AUDIO:** the drums the mic is meant for. Overheads, room and ambience hear the whole kit, so nothing is bleed there.
  - **BLEED FROM INSTRUMENTS:** every other drum heard through that mic. Each has on, level and polarity, so you can take the snare out of the kick mic, or flip one to line it up.
  - **TIME OFFSET:** the channel later, up to 10 ms, to line the overheads up with the close mics.
  - **RELEASE:** each hit in the channel faded after that long, for shorter room tails or a gated sound. It only shortens, as the manual's own control does without an envelope.
- **Only drums from multi-mic kits use the mixer**, as you chose. Synth drums and single samples go their own way (their INSERT, sends and OUTPUT), so your presets sound exactly as before. A multi-mic drum's own INSERT, sends, LP / HP and OUTPUT don't apply to it now; its sound goes through the mixer. Sends, buses and effects per channel come later.
- **Presets** keep the channels' settings and the bleed table. Each channel's OUTPUT stays with the project.
- **VOICES** counts hits, not mics: a hit through seven mics is still one hit.
- **Up to 48 hits at once**, as before; the kit has 192 voices for them, since a multi-mic hit is a voice per mic. A kit of synth drums or single samples plays (and costs) exactly as in 0.19.0. The kit also draws its drums in one pass over its voices now, not one per drum.
- **Not checked here:**
  - how a real multi-mic kit sounds through it;
  - how well my mic-name rules match real kits' names;
  - the CPU with big multi-mic kits (48 hits through seven mics would be 336 voices wanted: past 192, the oldest are cut).
- **Tests:**
  - mic names into channels;
  - multi-mic SFZ kits and mic folders keeping each mic apart, and single-mic ones not;
  - each channel's pitch measured in the output: the fader exactly -6 dB, mute, solo, the channel's bleed off and at -12 dB, a drum's bleed at -6 dB and its polarity, a channel's polarity sample for sample;
  - time offset 240 samples late, release silent after its fade, OUT 2 instead of MAIN;
  - a synth drum still playing with every channel muted;
  - VOICES counting hits;
  - presets keeping channels and bleed;
  - the page's channels in use.

## 0.19.0

**The sample engine, part 1: velocity layers, round-robins and hit variation, from SFZ kits or folders of your own hits** (inspired by the reference's sampled kits and its Voice and Layer and Hit Variation boxes; our own design and code, and your own sounds, never theirs).

- **Any drum can now be an instrument:** many recorded hits instead of one.
  - **Velocity layers:** hits recorded at different strengths, so a soft note plays a soft hit, not just a quieter hard one.
  - **Round-robins:** several takes of each, so fast repeated notes don't sound like a machine gun.
- **Loading** (DRUM view, **LOAD...**):
  - **An audio file:** as before, one hit.
  - **A folder of hits.** If the file names number their layers and takes ("snare_v2_rr3.wav", "vel3", "rr2"), they're used as named. Otherwise the hits are sorted by loudness and shared into layers. The number of layers is about the square root of the number of hits (16 hits make 4 layers of 4), our own rule.
  - **An SFZ instrument:** one SFZ file into this drum. SFZ is an open, plain-text format many free and paid multi-sampled kits come in.
  - **A whole SFZ kit:** every note of it onto our drums, through General MIDI (most kits), the groove library's layout, or your current map. A summary says which note went to which drum and which notes were left over.
- **What's read from SFZ:** layers (lovel / hivel), round-robins (seq_position, lorand), volume, amplitude, pan, offset, #define and #include, and the default path. Release regions are left out. Multi-mic kits: a hit's mic files are mixed into one stereo hit for now; the next release keeps them apart as mic channels. Anything controller-driven (a kit's own mic faders), filters, envelopes and tuning isn't read yet.
- **HIT VARIATION and limits** (under the layers picture, with an instrument loaded):
  - **RANDOMIZE:** a random hit from the layer, never the same one twice in a row. Off: the layer's hits in turn.
  - **ADJACENT:** a note near the top or bottom of its layer sometimes takes a hit from the next layer, its level matched. More variety from the same hits.
  - **VEL > VOL** (velocity to volume): a softer note within a layer also plays a little quieter. Off: each hit exactly as recorded.
  - **LAYER LIMIT:** use only the first N hits of each layer.
  - **VOICES:** how many of this drum's hits ring at once. It works on every drum, synths too.
- **The layers picture** (DRUM view): each layer across the velocities, as tall as it's loud, its hits stacked in it, and the last hit played lit.
- **Presets, kits and projects** remember where each instrument came from (the folder or SFZ file, and which note), not the audio: a sampled kit can be gigabytes. They load it again in the background when you open them. If you move or delete the files, the drum says "COULDN'T LOAD IT" and names what's missing.
- **Your old presets, kits and projects are unchanged.** A drum with one sample still keeps that sample inside the preset, as before.
- **Not checked here:**
  - how a real multi-sampled kit sounds, and how long a big one takes to load;
  - memory: the whole kit loads into memory (no streaming from disk yet). LAYER LIMIT doesn't save memory yet, only variety. If a big kit is too heavy, tell me and I'll make it stream.
- **Tests:**
  - folders sorted into layers by loudness, and by numbered names;
  - choosing hits: the velocity's layer, never the same take twice running, in turn, the layer limit, adjacent layers with their level matched;
  - an SFZ kit's notes, layers, round-robins, mics summed with their level and pan, #define / #include, release regions left out, a missing file reported;
  - importing a kit through General MIDI, then hearing the right layer's takes in turn (each test hit at its own pitch);
  - velocity to volume on and off;
  - voices;
  - projects, presets and kit files loading an instrument again, and a missing folder reported.

## 0.18.0

**Articulations, part 2: velocity curves, MIDI IN / E-DRUMS, CC zones, the MIDI monitor and hi-hat calibration** (the rest of what we agreed from the reference's Drums chapter and chapter 9; our own look and words).

- **Each drum's VELOCITY CURVE and VELOCITY GATE** (DRUMS, the DRUM view, the new **VELOCITY** button beside ENVELOPE):
  - The curve: how hard each hit comes in (along the bottom) against how hard it plays (up the side).
    - Drag the end points in to set **limits**. For example, drag the left one up and soft hits come out harder; drag the right one down and nothing plays harder than that.
    - Double-click the line to add a bend (as in the manual: 91 in, 114 out). Right-click a point to remove it.
    - **SOFT** rounds the corners. **GATE** stops hits softer than the left end point from playing at all. **STRAIGHT** goes back to a straight line.
  - It shapes every hit on that drum, from grooves, the song, the sequencer and your keys. With several drums chosen (or **ALL** in the articulation list), it goes to all of them.
  - **GATE LOW / GATE HIGH:** only hits in that range (as they come in) play the drum. For example, raise GATE LOW on a layer you want for hard hits only.
  - It's kept in presets and kit files.
- **SETTINGS: MIDI IN / E-DRUMS** (THE KEYS, grown). Click a note's number in the list to edit it, beside the list:
  - **MAPPING:** the note's drum, and **LEARN**: hit a pad and its note plays this drum.
  - **CC ZONES** (for pads that sense where they're hit):
    - Choose 2, 3 or 4 zones and the pad's controller (**LEARN CC** finds it, often CC 16).
    - Pick what each zone plays, for example the snare's centre, rimshot and sidestick on one pad.
    - Drag the marks to set where each zone begins; the bar shows the pad's last value live.
  - **VELOCITY:** one curve for every note from your DAW or controller (your pads' feel), with SOFT, GATE and STRAIGHT. The last hit shows on it. Grooves and the song aren't affected.
  - **HI-HAT:** the pedal's steps with a live meter, three **STARTING POINTS**, and **CALIBRATE**. Press the pedal right down and let it right up a few times, then DONE: the steps spread over the range your pedal really sends. Some pedals never reach 127, so the hat never fully closed; this fixes that.
  - **CHANNEL:** which MIDI channel plays the kit (ANY by default).
  - **PRESETS:** DEFAULT (the Groove Library layout), GENERAL MIDI, and your own: the map, channel, curve, zones and hi-hat steps saved together (SAVE AS..., in the presets folder's "MIDI In Presets"), one for each e-kit or controller you use. They're also kept in your project.
- **THE MIDI MONITOR** (under those tabs): what came in (notes, controllers, aftertouch; channel, note, velocity) and what it played. It shows the drum after zones and stand-ins, and the velocity after the curve, or "gated". Click a note in it to edit that note.
- **Not copied:** the reference's ready-made presets for e-drum brands. I don't have their maps, and they're the reference's work. If you send your kit's note list, I'll make one from it.
- **My guesses:**
  - SOFT is my reading of the manual's "soft curve"; it doesn't say more.
  - The calibration puts "closed" in the top fifth of your pedal's range.
- **Tests:**
  - the curve's limits, points, gate, soft and saving;
  - a drum's curve making a mid hit exactly as loud as a hard one, only on that drum, kept in presets;
  - the gate's range;
  - the incoming curve changing your keys but not a groove's hits, and its gate;
  - the channel;
  - three CC zones at their boundaries;
  - the monitor's events, with stand-ins and gated notes;
  - presets saved, loaded and built in, and kept in the project;
  - LEARN, LEARN CC, the zone marks kept in order, and calibration's steps (12 to 93 gives 77, 61, 44, 28);
  - the DRUM view's curve going to every chosen drum.

**Not checked here:** how the curves feel with your own pads, and calibration with a real pedal.

## 0.17.0

**Articulations: each drum of the kit is now a piece played several ways**, as you asked (inspired by the reference drum software's articulation list, from the Drums chapter and chapter 9's MIDI In / E-Drums section; our own look and words).

- **The ARTICULATIONS list**, over the KIT VIEW's top left, shows the chosen drum's piece:
  - **SNARE:** center, rimshot, sidestick. **HI-HAT:** closed, open 1, open 2, open 3, open 4, pedal. **RIDE:** bow, bell, edge. The other pieces have one articulation each.
  - Click a name to choose it and hear it. **ALL** chooses every one, so the DRUM page's settings go to all of them (as the reference's Select All does).
  - Each has its note in the current map and a **level** (drag it; double-click for 0 dB).
  - **The dot switches one off.** Its notes then play the nearest one that's on, named beside it ("OPEN 3 → OPEN 4"), and that row lights as it plays. This is our version of the reference's substitution: a groove asking for an articulation you've switched off still plays something sensible.
- **Three new drums** (the kit is now 23):
  - **RE**, the ride's edge (crashing the ride): darker and longer than a crash. On the kit view, it's the outer ring of the ride.
  - **HO1** and **HO3**: two more steps of hat openness. The hat now goes closed, open 1, open 2 (the half-open HH), open 3, open 4 (fully open OH). Each step rings longer than the one before.
- **GROOVE LIBRARY map:**
  - Note 59 now plays the ride's edge. Your packs use it in their "ride crash" grooves, so this one is checked.
  - Notes 24, 25 and 26 play open 1, open 2 and open 3. **That order is my guess:** your rock pack rides mostly on 25, then 24, then 26. If you can send the map with each note's name (the one you sent before only grouped them), I'll put them right.
- **THE HI-HAT PEDAL (CC 4),** as in chapter 9:
  - A controller's hi-hat pedal opens the hat step by step on the **pedal note**: note 7 in Groove Library (the reference's default "edge trigger"), or any note you set to **HI-HAT (PEDAL)** in a Custom map.
  - The list's **PEDAL** bar shows where the pedal is, live. Drag its marks to set where each step begins (127 is closed).
  - With no pedal sending, the pedal note plays closed, exactly as before. Grooves aren't affected: as the manual says, groove packs use a note for each openness, not CC 4.
  - The steps aren't saved in presets. They suit your pedal, like the note map does.
- **MUTE TAIL (grab the cymbal),** in the list for cymbals:
  - **NOTE OFF:** letting the key go grabs the cymbal. **AFTERTOUCH:** pressing an e-drum pad harder grabs it. **OFF** (the default) lets it ring as before.
  - A grab stops the whole cymbal: grabbing the ride stops its bell and edge too.
  - Unlike the reference, which plays a recorded grab, ours is a quick fade (80 ms). A grab sound needs the sample engine.
- **Your older presets, kits and projects sound exactly as before.** They load with the three new drums off, so their notes play what they played before. Your **Groove Library** grooves change in one way: notes 24 and 25 now play the half-open hat (open 2) instead of the fully open one. Switch on HO1 and HO3 in the hi-hat's list to hear all five steps.
- **Also:** the kit view names the articulation under the mouse ("RD RIDE EDGE"), and says what plays when one is off. A Custom map's note menu lists articulation names.
- **Tests:** every drum in exactly one piece; a switched-off articulation sounds identical to its stand-in, down the stand-in chain and across pieces; the pedal's steps at their boundaries, remembered between blocks, moved, kept out of presets; a grab stopping the ride and its bell, over 60 dB down, by note-off or aftertouch, and OFF ringing on; old presets, projects and kits loading with the three off and the edge note playing the bow; the new drums' lengths and levels; the list's rows, ALL, ON, stand-in names, pedal marks that can't pass each other, and MUTE TAIL for a whole piece.

**Left for the next articulations release, as agreed:**
- CC zones for positional pads;
- a velocity curve per articulation;
- MIDI mapping presets and the analyzer;
- calibrating the hi-hat pedal.

**Not checked here:** how the new drums sound to your ears, and the pedal and aftertouch with your own controller.

## 0.16.0

**GROOVES rebuilt to work the way the reference drum software's groove tab works** (our own look and words). Everything the page did before still works; it's arranged and browsed differently.

- **The header:** MENU, back and forward, ORIGINAL TEMPO (grooves at the tempo they were played at, or your song's), TEMPO (1/2x to 3/2x), VELOCITY, STOP.
  - **MENU:** FOLDERS (show MIDI from subfolders, add a linked folder, check all folders), VISIBLE FILTER COLUMNS, VISIBLE RESULT COLUMNS, SORTING, GROOVES DRAGGED OUT (for this kit, or as written).
  - **Back and forward** retrace the folders you've browsed, like a web browser.
- **The browser, in columns, left to right** (in place of the tree):
  - the first: GROOVE PACKS (each pack in your groove databases), USER LIBRARIES (User MIDI) and LINKED FOLDERS;
  - then each chosen folder's folders, two levels to a column: a folder with folders inside is a heading with them listed under it (a pack's styles, each with its songs; a song's sections). Three columns show at once.
  - Each folder is a filter: choosing one lists everything in it (and its folders, with SHOW MIDI FROM SUBFOLDERS on).
  - The chosen folder's arrow (or a right-click) opens its menu: NOTE MAP, LEAVE OUT OF SEARCHES, ADD FOLDER... (in User MIDI), OPEN IN EXPLORER, SYNC FILE CHANGES, REMOVE LINKED FOLDER.
- **USER MIDI**, PR7-D's own folder (Documents\Paragon PR7-D\User MIDI): drop MIDI files (or folders of them) on it, or on a folder inside it, and they're copied in and read. Files put there another way show an orange dot beside it until you SYNC FILE CHANGES.
- **FIND SIMILAR:** TAP TO FIND... and a drop zone for a MIDI file. After a search by rhythm, the drop zone shows the rhythm searched with, and its cross clears it.
- **The filter columns:** GENRE, PLAY STYLE, TYPE, TIME and POWER HAND (TEMPO and SECTION when you show them).
  - **Your packs' own labels:** genre, play style (Standard, Double Kick...), type (Beat, Fill, Straight, Swing, Half Time, Soft / Medium / Hard Hits...) and power hand come from your groove databases, as you asked. Plain MIDI files have none of those: their TYPE is ours (Straight or Swing, and Beat, Fill or Ending), their power hand worked out from the notes.
  - Each column shows how many of its values are chosen and how many the grooves here have. Values none of them has stay listed, greyed. Right-click a column's name to show or hide columns.
  - In TYPE, every value you choose must be there (its values describe different things: swung AND a fill); in the others, any one.
  - Our former TYPE (Intro, Verse, Chorus... from the names) is now SECTION; POWER HAND says "Hi-Hat Closed" / "Hi-Hat Open", as the packs do.
- **The active filters:** SEARCH, CLEAR ALL FILTERS, and a chip for each filter at work, in the order you chose them: the folder, a rhythm search, the search words, each value (excluded ones red, as NOT ...), favourites. Click a chip to take it away. When TEMPO or VELOCITY differs from as played, a chip offers RESET.
- **SEARCH suggests as you type:** matching filter values first (TYPE: Fill, GENRE: Rock...), then grooves' names. Pick one, or press Enter on an exact match, and it becomes a filter. Words also search the grooves' folders and labels.
- **The results:**
  - the row under the mouse shows a play button: click to loop it (again to stop); double-click a groove to play it once; a plain click chooses it (for GROOVE PARTS);
  - FAMILY, INTENSITY, POWER HAND, SIGN., BARS, TEMPO, and GENRE, PLAY STYLE, TYPE, SECTION when shown (MENU > VISIBLE RESULT COLUMNS); MATCH during a search by rhythm;
  - click a column's name to sort by it; click the star's column to list only favourites;
  - **SORTING** (MENU), ticks that combine: your song's time signature first, nearest your song's tempo, the packs' own order (a pack's simpler grooves first; for MIDI files, the gentler first);
  - **right-click:** COPY, SEARCH WITH TAGS (the groove's labels as the filters), SEARCH WITH TAP TO FIND (its rhythm), SHOW IN BROWSER (the browser opened to its folder), USE WITH SONG CREATOR, and ours: ADD TO SONG, TO SEQUENCER, FAVOURITE.
- **GROOVE PARTS** beside the results, with TO SEQUENCER and LAYER ONTO under it; the GROOVE PARTS button folds it away.
- **Left for later, as agreed:** opening the page in its own window. Not copied: the reference's web shop.
- **Your packs are read again once** after installing (for their labels): only the groove databases, not your MIDI files.
- **Tests:** the browser's columns (a pack, a folder with folders, two levels deep), back and forward, SHOW IN BROWSER; chips in order and taken away; greyed values; the RESET chip; suggestions and taking one; SEARCH WITH TAGS; User MIDI (files copied in, the out-of-step check, it can't be removed, leaving a folder out of searches); a test database's labels in the columns and kept in the index, a label filter, the packs' order, the song-first sorting.

Not checked here: how it feels with your real packs and 10,000 grooves (the browser lists folders as you open them; the filter values are gathered once each time the library changes).

## 0.15.2

**Fixed: every PR7-D added to a song read your whole groove library again** (10,000 files in your folders). Each PR7-D keeps its own list of grooves, and the first time its GROOVES page opened it checked every file in your linked folders against the saved list, though the PR7-D before it had just done the same. It also loaded that saved list on the window's own thread, so the window could freeze while it did.

- **Your folders are checked once each time your DAW starts**, by the first PR7-D whose GROOVES page opens, in the background, with the list shown at once from what was read before. Every PR7-D you add after that lists the grooves straight away and checks nothing.
  - Measured here with 10,000 grooves: a new PR7-D used to hold the window for about 0.2 s and then check for about a second (several times that on a Windows disk with a virus scanner looking at each file); now it takes under a millisecond of the window's time and lists all 10,000 a quarter of a second later, reading none.
- **Only new or changed files are read** (as before), and now the status line says so: "10000 grooves (checking for new ones)", instead of counting all 10,000 as if reading them.
- **A groove database that hasn't changed isn't even opened** during the check: its grooves come from the saved list.
- **The saved list isn't rewritten when nothing changed** (with 10,000 grooves it's megabytes).
- **A first read cut short isn't lost:** if a PR7-D is removed while reading your folders for the first time, what it read is kept, and the next one carries on from there.
- **New: REFRESH** (beside SUBFOLDERS): check your folders now, if you add grooves while your DAW is open. Adding or removing a folder, or changing its map, checks as before.

Not checked here: the time on your computer (it depends on your disk and virus scanner). The first PR7-D after starting your DAW still checks once, in the background, while the list is already there to use.

## 0.15.1

**Fixed: the plugin's window used far too much CPU while drums played** (since 0.14.0's KIT VIEW). The sound was never the problem (measured: well under 3% of real time for a beat on the default kit); the drawing was.

- **The kit view** shrank its 1280-pixel picture and each glowing piece's picture to fit the window, with high-quality smoothing, **every time it redrew**: 30 times a second for each piece glowing, more pixels again with Windows' display scaling. It also had the window's background painted underneath it each time, though it covers it.
  - Now the pictures are shrunk **once**, to the window's real size in the screen's own pixels (150% or 200% display scaling included), and only copied after that, lined up on whole pixels (between two, every pixel would be smoothed again). The view tells the window it covers what's behind it.
  - Measured here: redrawing a glowing piece went from about 1 ms to under 0.1 ms (0.17 ms at 150% scaling), the whole kit view from 10 ms to under 1 ms, the whole window on the KIT VIEW from 36 ms to 19 ms. A beat lighting four drums cost about 12% of a core in drawing; now about 1%.
  - It looks the same: a test compares it, at 100%, 150% and 200% scaling, with the pictures drawn the old way (a half-pixel shift, now on whole pixels, is the only change).
- **The GROOVES list** redrew every row 15 times a second while a groove played; now only the playing row (the whole list when another groove starts).
- **The OUTPUT meter** redraws only its column of lights, not its numbers.

Not checked here: the real figure on your computer. If your DAW's or Windows' CPU meter still runs high, tell me which (the DAW's audio meter, or Task Manager) and with which page showing, and whether it drops when the plugin's window is closed.

## 0.15.0

**GROOVES reads groove databases.** Some drum software installs its MIDI grooves not as .mid files but inside one database file per pack, named `midiDB`. Until now GROOVES only looked for .mid files, so a folder of those packs listed nothing. It reads them now, from your own computer when you link the folder; nothing of them is copied into the plugin or your presets.

- **Link the folder that holds the packs** (ADD FOLDER...): each `midiDB` in it, or in any folder below, shows up in the tree as its pack, with the pack's own folders inside, their sorting codes taken off and the song's name added where they don't say it. For example: **Heavy Rock Grooves > Straight 4-4 > Ordinary Man 72 BPM > Fills > Fills 01**.
- **Everything works on them as on .mid files:** preview, the filter columns, search, favourites, tap to find, groove parts, to the sequencer (buttons or dragged onto its grid), the song track and song creator, and dragging a groove out to your DAW (as a real MIDI file).
- **Tempo, time signature and length** come from the database; power hand, feel and the rest are worked out from the notes as for any groove.
- **Velocities are our guess.** The databases keep each note's time and drum but no velocity. They do mark the strong hits of each groove's first two bars, so a hit on a marked step plays at **112** and the others at **80** (the two bars' marks repeat through longer grooves); a groove with no marks plays at 100. Use the preview's VELOCITY to scale them.
- **Electronic packs** that use their own near-General-MIDI note layout want the folder's map set to **General MIDI** (FILES' MAP), as found when we checked your EZX Electronic database for 0.8.1.
- **Checked against your two databases** (Heavy Rock Grooves, EZX Electronic; privately, nothing of them committed): all 495 and 267 grooves listed, every note read (29,458 and 10,902), every length right, the same as a standard database reader gets from the same files.
- **How:** a small reader of the database file format of our own (read-only; it never changes your files), so no extra library goes into the plugin. A damaged or unreadable file lists fewer grooves or none, never a crash.
- **Fixed: tempos read from MIDI files** came back a hair off (90 BPM as 89.99996, as MIDI files keep microseconds a beat): rounded to a thousandth now.
- **Tests:** a small database made for the tests (laid out like a real one, its grooves invented) read through its tables, several pages deep and spread over overflow pages; the accent velocities; 43 grooves listed in their folders with tempo, length, time signature (a 3/4 one) and FILL from the folder; a groove's 26 notes and their velocities; a 64-bar groove's 576 notes; the folder tree; preview through the kit and a dragged MIDI file; not-a-database, cut-short and scrambled files listing fewer or none without crashing.

Not checked here: where your packs keep their `midiDB` files on Windows (link the folder you found them in, or one above it), and how the guessed velocities sound to you.

## 0.14.0

**The KIT VIEW: a drawn kit to click and play.** The first step towards a kit page like the drum software's (our own artwork, look and design: only the idea of a clickable kit is shared).

- **A new KIT VIEW tab on DRUMS** (first in the right-hand panel, beside DRUM and SEQUENCER; the page opens on it): the kit seen from the drummer's seat.
  - It's **our own 3D kit**, modelled and rendered for the plugin: gloss-black drums and black hardware, each hoop, cymbal edge and bell, pedal and stand lit by a glowing ring, the colour running across the kit from pink (hi-hat side) through red to orange (ride side), on a dark red grid floor. Only the PR7 family's colours (a test checks every coloured pixel as the plugin draws it).
  - 11 pieces: kick (with its pedal), snare, two rack toms, floor tom, hi-hat (with its pedal), two crashes, ride, china, splash, and a tambourine on the hi-hat stand.
- **Click a drum to choose it and hear it**, as clicking its row does (SHIFT / CTRL choose several). Where you click picks the articulation:
  - snare: the head plays **SD**, the hoop **SD2** (the rimshot), the shell **RIM** (the sidestick);
  - ride: the bow **RD**, the bell **RB**;
  - hi-hat: the cymbals **CH**, the pedal **PH** (the open and half-open hats are on their rows);
  - the clap and the shaker have no piece: they're on the rows only.
  - The piece under the mouse lights a little and its drum, model and note show at the bottom; the chosen drum's piece stays lit a little.
- **Each piece glows as it plays,** whatever plays it (your DAW, the keyboard, the sequencer, a groove preview, the song), brightest on the hit and fading over its first second.
- **How it's drawn:** the kit was rendered once (Blender, from our own model) into pictures built into the plugin: the kit, each piece glowing, and a hidden click map with each part in its own value, so a click is matched to the exact part under it. The scripts that make them are in `resources/kit/`. It adds about 2.4 MB to the plugin.
- **Tests:** the pictures load and fit together; all 16 drums on the kit have a part where a click lands on them, the clap and shaker none, the empty floor nothing; a click on the snare's head chooses SD and plays it, CTRL-click on the ride's bell adds RB; the china and the snare (from SD2) glow on their hits, nothing else, a quarter as bright half a second later, dark after a second; the pictures' colours.

Not checked here: how smoothly the glow fades in your DAW on Windows (it redraws only the pieces that change, 30 times a second).

## 0.13.0

**Drag a groove straight onto the drum sequencer.** The last piece of the groove player's plan.

- **Drop a groove on the SEQUENCER's grid** (DRUMS page): it becomes the pattern shown (A to D), as TO SEQUENCER does: its notes on the kit's drums at their steps, the pattern's LENGTH set to it, the sequencer on.
  - **From the GROOVES list.** With GROOVE PARTS ticked, only those parts are laid onto the pattern (its other drums kept), as LAYER ONTO does.
  - **A GROOVE PARTS row** (say HI-HAT) dragged onto the grid: just that part of the chosen groove, laid onto the pattern.
  - **From the SONG CREATOR's columns.**
  - **A MIDI file from your computer**, through the kit's note map.
  - While a groove is over the grid, it's framed orange and says what dropping it will do ("DROP: THE GROOVE REPLACES PATTERN A", "... HI-HAT LAID ONTO PATTERN A"). Each drop is one undo step.
- **The DRUMS tab opens for a drag:** hold a groove over it for half a second and the DRUMS page comes up with the SEQUENCER showing, to drop it on the grid. (The page you dragged from stays underneath until the drag ends, then the window settles on DRUMS; hiding it during the drag would cancel the drag.)
- **Tests:** a groove dropped as the pattern (kicks on steps 0 and 8, snares on 4 and 12, eight hats); ticked KICK laid onto it, the snares and hats kept; undo back to the pattern before; a HI-HAT row laid onto another groove's pattern, its kicks kept; a song creator groove; a MIDI file through the kit's map; drags it doesn't take refused; the DRUMS tab: nothing at 0.2 s, the sequencer in front at 0.5 s with the GROOVES page still under it, the window on DRUMS when the drag ends, nothing if the drag moved off the tab first.

Not checked here: how a real drag feels in your DAW's window (the tests drive the drop and the tab's timer directly).

## 0.12.0

**The song track, part 4b: RECORD and BOUNCE.** Our own design, inspired by the transport's record button and the bounce in chapter 7 of the drum software manual you sent. That completes the song track as planned.

**0.11.0 was never published** (its macOS build crashed in a test, so no release was made). **This release is the first with 0.11.0's changes too:** tempo and time signature changes (the TEMPO and TIME views), TRACK... > IMPORT MIDI FILE..., and **only the PR7 family's colours** everywhere (song parts, sliders, highlights and the rest: see 0.11.0 below).

- **Fixed: the crash.** Adding a tempo change worked out its place in the song's list of tempo changes in the same step that inserted it. Which half a compiler does first is up to the compiler: the Mac's measured from where the list had been before the insert moved it in memory, read a nonsense place and crashed. (Linux's did it the other way round, so the tests there passed.) It's two steps now, the same on every computer. Checked by building the tests with the compiler macOS uses and a memory checker on (clang with AddressSanitizer): the song tests pass with no memory errors.

- **REC** (beside PLAY): armed, the notes you play while the song plays go into it, at the beats you play them.
  - From your DAW's MIDI (a controller through it too) or the on-screen keyboard, each through the kit's note map. Notes the kit doesn't play are left out.
  - With your DAW playing, or with PLAY (through the song's tempo changes too).
  - **On top of the blocks there** (overdub): a block that repeats its groove is written out first, so a note lands only where you played it. Where there's no block, a **RECORDED** block is made, growing bar by bar as you play on; after a gap, a new one.
  - Click REC again to stop: **the take is one undo step.** Then QUANTIZE, EDIT BLOCK and the rest work on it like any block.
  - Armed, REC is red and the song lane is framed red.
- **FOLLOW DAW** (on the song strip, lit when on): the song plays with your DAW (as before), or, switched off, **only with PLAY**, your DAW playing or not.
  - Switch it off when the song's MIDI is on a DAW track too (EXPORT MIDI dragged in), or every hit plays twice (the manual warns about the same thing). After EXPORT MIDI with it on, the status line reminds you.
  - Kept with the song in your project; undo takes it back.
- **TRACK... > BOUNCE TO AUDIO...:** the song rendered to WAV files, faster than real time, while the plugin plays on (the status line shows how far).
  - **One file: the whole kit**, or **a file per output, as routed** (MAIN, and each of OUT 2-16 a drum, an insert bus or a send return is routed to).
  - 24-bit, 16-bit or 32-bit float, at the plugin's sample rate; a gain (dB).
  - From bar 1 through the song's tempo changes (none: your DAW's tempo) to its end, then on until the drums and effects have died away (silence, or 10 s).
  - **The plugin's own delay** (the limiter's and CONSTRICTOR's look-ahead) **is taken off**, so the files start on bar 1 and line up in your DAW.
  - It's rendered by a copy of the plugin made from your settings as they are, so the one you're playing isn't touched.
- **Tests:**
  - RECORD with the DAW playing: notes at their exact beats, a take's block growing bar by bar to 3 bars, a new one after a gap, a note the kit doesn't play left out, nothing recorded when not armed, the take undone in one step;
  - on top of a repeating block (its repeats written out, the note only where played), and with PLAY through a tempo change (a note 2.5 s in at beat 4.5: 4 beats at 120, half a beat at 60);
  - BOUNCE: the hits on their samples (0, 36000, 96000) with the limiter's look-ahead on, the file ending quiet, 24-bit at 48 kHz; +6 dB exactly twice as loud (32-bit float); a file per output (the snare on OUT 3: in its file only); an empty song: no files;
  - REC and the TRACK menu on the strip;
  - FOLLOW DAW off: your DAW playing plays nothing of the song; PLAY plays it on its own clock with the DAW playing (each hit on its sample); kept in the project; undone; the button and the reminder after EXPORT MIDI.

Not checked here: how it sounds, recording from your controller and DAW live, and the files in your DAW. Left out (that software's, needing its microphones): bouncing microphone channels and bleed; a loop area.

## 0.11.0

**The song track, part 4a: tempo and time signature changes, and IMPORT MIDI FILE. And the family's colours only.** Our own design, inspired by the track menu in chapter 7 of the drum software manual you sent.

- **The family's colours only** (as you asked): every colour in the plugin is now in the PR7 red / pink / orange range, or pearl white and onyx.
  - **Song parts:** Intro pearl, Verse orange, Pre-Chorus amber, Chorus hot pink, Bridge light pink, Fill red, Ending oxblood, Count-In onyx, Beat salmon. They were cyan, yellow, purple, green and blue.
  - **Sliders, toggles, selected text, scroll bars:** were JUCE's built-in blue (the VELOCITY sliders on GROOVES and EDIT BLOCK, the limiter's), now orange.
  - **The limiter's "under the ceiling" readout and the sample editor's anchor:** were green, now pearl white. The level meter's warning segment and the lit key's text: amber and warm pearl instead of yellow.
  - Checked on screenshots of every page (no pixel outside the range), and a test keeps it so.
- **The song strip's views: BLOCKS, TEMPO and TIME** (beside TRACK...).
  - **TEMPO:** the song's tempo as a line over the blocks. **Double-click** to add a tempo change (on the nearest beat, at the height you click), **drag** one to move it, **right-click** it: RAMP TO THE NEXT (a smooth glide to the next one's tempo), a TEMPO from 60 to 180, REMOVE.
  - **TIME:** **double-click or right-click a bar:** BEATS A BAR (1 to 15) and BEAT LENGTH (2, 4, 8, 16), REMOVE THIS CHANGE. The bar numbers follow (a change shows its time signature by its bar's number), and blocks snap, lengthen and split on those bars. As in the manual, a time signature change doesn't change the grooves' notes.
  - **Where the tempo changes count:** PLAY (with your DAW stopped) plays through them, each hit on its sample, a glide too; EXPORT MIDI writes them (a glide as steps a 16th note apart) with the time signatures. **While your DAW plays, its tempo leads** (draw tempo changes in your DAW for that).
- **TRACK... > IMPORT MIDI FILE...:** a whole song's MIDI (all its tracks) through the kit's note map. It asks whether to take the file's **tempo and time signatures** too.
  - **Markers** in the file (the "Verse", "Chorus"... a DAW writes) cut it into blocks, each named and coloured by its part; without markers it's one block (SPLIT HERE cuts it).
  - An empty song becomes the file's song; otherwise it goes after the song, from the next bar line (its tempo changes moved along with it).
  - TRACK... also has REMOVE ALL TEMPO CHANGES (back to your DAW's tempo) and REMOVE ALL TIME SIGNATURE CHANGES.
- **Fixed: a song hit exactly on the line between two of the DAW's audio blocks** could be played one sample early (on the earlier block's last sample) when the DAW's position, rounded, landed a hair before it. Each hit is now placed by its own sample, in the block that sample is in.
- **Tests:**
  - time signature changes: bar lines, snapping, a change put on the nearest bar line, a change repeating the one before left out, later changes moved onto bar lines when the first bars change; resize and split on them;
  - tempo: 8 beats at 120 and 4 at 60 = 8 s exactly; a glide 60 to 120 over 4 beats = 4 ln 2 s, half way 90 BPM, beats to seconds and back; saved and read back; in the MIDI file;
  - heard: PLAY's kicks at 0, 2 s and 4 s through a step from 120 to 60; the glide's second kick at 4 ln 2 s; from bar 2; the DAW's 120 leading while it plays;
  - import: markers to named, coloured blocks; with and without the tempo and time signatures; no markers; a file timed in SMPTE frames refused; after a song, its tempo moved along;
  - the strip: a tempo change double-clicked in, dragged, ramped, set, removed; a time signature set from the menu and removed; the TRACK menu;
  - the colours.

Not checked here: how it sounds and feels; DAWs' MIDI files beyond ours (their markers, tempo maps). Next, part 4b: recording into the song, and bouncing it to audio.

## 0.10.0

**The song track, part 3: the SONG CREATOR.** A whole drum track from one groove, in a couple of clicks. Our own design, inspired by the song creator in chapter 7 of the drum software manual you sent. That one finds a groove's "variations" in its makers' own database, which we can't read or ship; ours come from your linked folders.

- **CREATOR...** on the song track opens it over the page (the song track stays below). Also: right-click a groove in GROOVES, or a block in the song: **USE WITH SONG CREATOR**; or drop a groove from the GROOVES list (or a MIDI file in one of your linked folders) on it.
- **The source's song, in columns:** the grooves in the source's folder, by song part: INTRO, VERSE, PRE-CHORUS, CHORUS, BRIDGE, FILL, ENDING, BEAT (from the names, as GROOVES' TYPE). If that folder has only one kind (a pack keeping its fills in "Fills" beside "Grooves"), it looks one folder up, never past your linked folder.
  - **Click** a groove to hear it, **double-click** to add it to the song, or **drag** it onto the song track.
  - **Right-click:** ADD TO SONG, COPY, **REPLACE ALL "CHORUS" ON TRACK** (every chorus block plays it now, each keeping its length), **REPLACE THE CHOSEN BLOCK**, USE AS SOURCE.
- **SONG STRUCTURES** (left), each drawn as its sections in their colours. Ours: Verse / Chorus (ABAB), With Bridge (ABABCB), With Pre-Chorus, AABA, Chorus First (BABAB), 12-Bar Three Times (A the verse, B the chorus, C the bridge; an intro, a fill's bar before each chorus, an ending).
  - **MAKE SONG:** the song made from the chosen structure, in place of the one there (undo brings it back). **ADD TO END:** after it.
  - Each section takes the next groove in its column (two verses: Verse 01, then Verse 02). A part the folder hasn't is played by the nearest kind, coloured as the part it plays (a pre-chorus by a verse, a bridge by a chorus, an ending by a fill, an intro by a verse). A missing **fill** isn't played by another groove: the groove before it plays on through its bar.
  - **SAVE SONG'S STRUCTURE...:** your song track's parts and lengths (no notes) as a structure of your own, to make songs from any groove. Right-click yours to RENAME or REMOVE them. They're kept in "Song Structures.xml" in your presets folder.
- **Tests:**
  - structures written and read; ours each whole four-bar phrases and an ending's bar; a song's structure read off it;
  - the stand-ins and the next groove each time (a fill missing: none);
  - the grooves round a source: its folder only (not the next song's), one up for fills kept apart, never past the linked folder;
  - a song made from real files: its blocks, places, lengths and colours, the missing fill's bar given to the groove before; heard: the chorus's snare on its sample;
  - your structures saved (the same name replaced), renamed, removed, in the presets folder;
  - the panel: a click playing a groove, MAKE SONG and ADD TO END (undone one at a time), REPLACE ALL, REPLACE THE CHOSEN BLOCK, ADD TO SONG, COPY, SAVE SONG'S STRUCTURE.

Our own choices (tell me if you'd like them otherwise): which part stands in for a missing one; the fill's bar before each chorus in our structures; the structures themselves. Not checked here: how the made songs sound, and real packs' folders (which grooves count as one song's comes from the folders).

## 0.9.0

**The song track, part 2: editing inside a block.** Change a block's drums, notes and feel without touching the groove file it came from. Our own design, inspired by the block editing in chapter 7 of the drum software manual you sent (its block menu, play-style editor and grid editor, in one simpler panel).

- **Right-click a block**, new:
  - **Edit Block...** (or double-click the block): the block editor, below.
  - **Copy**, then **Paste After** (a copy after this block, the ones after it moved along).
  - **Paste Drums Onto It:** the copied block's drums, all or one group at a time (KICK, SNARE, HI-HAT...), in place of this block's own on those drums. A shorter part repeats to fill.
  - **Remove Drums:** a group of drums taken out (each listed with how many notes it has).
  - **Quantize** (1/4, 1/8, 1/16, 1/8 or 1/16 triplets) and **Velocity** (50% to 150%, the balance between hits kept).
  - **Split Here** (at the bar line nearest where you clicked) and **Merge With Next** (a gap between them kept as rests).
- **GROOVES: right-click a groove, Copy for Song.** With GROOVE PARTS ticked, only those parts: copy one groove's hats, then paste them onto any block (PASTE DRUMS ONTO IT).
- **EDIT BLOCK**, over the page (the song plays on below it): a row for each drum the block plays, its notes on a grid with the bars numbered, each note as tall as it's loud.
  - **Click** an empty place: a note there, on the GRID (1/4 to 1/16 triplets). **Drag a note up or down**: louder or softer. **Right-click** a note: gone.
  - **ADD DRUM:** a row for a drum the block doesn't play yet.
  - **Click a drum's name** to choose it (CTRL or SHIFT for more). The buttons work on the chosen drums, or on all of them when none is chosen:
    - VELOCITY (25% to 200%: let go of the slider and it's done; it goes back to 100% for the next change);
    - QUANTIZE (to the GRID), REMOVE, COPY and PASTE;
    - **MOVE TO:** one drum's notes played by another (the hats' pattern on the ride, say), merged with that drum's own.
  - PASTE puts each copied drum's notes on the same drum here. If you copied one drum and have one drum chosen, they go onto the chosen drum.
- A block's edits stay in the song (not the groove file), are saved with your project, and **undo** takes back each one.
- **Tests:**
  - split and merge (the song playing the same), removing and pasting drums (a one-bar part repeated over two), quantize (a note pushed past the end going round to the start; two on one beat becoming one, the louder), velocity (balance kept, never past full or silent), moving a drum (merged, no doubled hits), adding and removing notes;
  - heard: a late snare (1.06 beats) quantized to 1/16 lands exactly on beat 2 (sample 24000 at 120 BPM); a block at 50% VELOCITY plays sample for sample as the same notes written at half velocity;
  - the block menu's items, each doing what it says, and the seven edits undone one by one back to the song as it was;
  - the editor: a click adding a note on the grid, a drag setting its velocity, a right-click on its drawn edge removing it, and each button.

Not checked here: how it sounds and feels in your DAW. Not in this part: adding or removing hits "like a drummer would" (that software uses its makers' statistics from real performances, which we don't have), articulations, an opening hit, and moving single notes sideways in the editor (remove and click it in again).

## 0.8.1

**Groove TYPE: fills found by more names.** From a check against two real groove packs you sent (Heavy Rock Grooves and EZX Electronic: 762 grooves, compared with their own labels; the packs stay with you, nothing from them is in the plugin).

- **"Break" is a fill** ("909 Break 2", "606 Clean Break"), as a word of its own, with or without a number after it ("Break3"). "Breakdown", "Breakbeat", "Breaks" and "Jailbreak" aren't.
- **"Fill" wins over the song's sections:** "Introfill", "Verse Fill 02" and "Pre-Chorus Fill" are fills now (they were Intro, Verse, Pre-Chorus).
- TYPE is read from the names each time, so your linked folders show it at once: no rescan.
- In the check, TYPE now agrees with the packs' own Beat / Fill labels on every groove (it missed 23 of the electronic pack's fills before).

What the check also found (no change needed):

- **The GROOVE LIBRARY map is right** for the rock pack's notes: closed hat 22 and 61, open hat 24-26, pedal 21, ride 51, bell 53, crashes 49 and 57, kick, snare and toms. The power hand agreed on all 250 grooves labelled with a hat, ride or cymbal; the bar counts on all 762.
- **EZX Electronic uses its own layout,** close to General MIDI (52 china, 54 tambourine, 56 cowbell): set that folder's **FILES' MAP to General MIDI**.
- **FEEL** agreed on 91% (rock) and 98% (electronic). The rest are mostly triplet grooves the packs call straight; two stricter rules tried on your grooves did worse, so it stays as it is.
- Not checked: how hard a groove is (the packs keep no velocities we could compare), half / double time (not measured yet).

## 0.8.0

**The song track, part 1.** Lay grooves out as a whole song, bar by bar, and it plays along with your DAW. Our own design, inspired by the song track in chapter 7 of the drum software manual you sent.

- **SONG** (left of the on-screen keyboard): **KEYS** shows the keyboard, **SONG** the song track in its place.
- **Putting grooves in:**
  - **Drag a groove** from the GROOVES list onto the song track: it becomes a block (its name, coloured by its kind).
  - Or **right-click a groove: ADD TO SONG**. It goes after the last block, and the song track opens.
  - Or **drop MIDI files** on it from your computer. They play through the kit's own note map.
  - A block is a copy of the groove as the kit's drums, at the GROOVES page's TEMPO factor (1/2x, 2x...) and VELOCITY. Changing the groove file later doesn't change the song.
  - The song takes your DAW's time signature when its first block goes in.
- **Arranging:**
  - Blocks snap to bars and never overlap. **Dropped on a block**, a groove goes before or after it (whichever edge is nearer), and the blocks after it move along to make room. **Past the end**, it stays where you drop it (a gap before it).
  - **Drag a block** to move it.
  - **Drag its right edge** to make it longer: the groove repeats (a faint line shows where it starts again), and the blocks after it move along. Shorter leaves a gap.
  - **Click a block** to choose it; **DELETE** removes it.
  - **Right-click a block:**
    - DUPLICATE (the copy right after it) and REMOVE;
    - MUTE (greyed, silent);
    - SONG PART (Intro, Verse, Chorus, Fill... each its own colour);
    - CHANGE TEMPO (1/2x, 2/3x, 3/2x, 2x: its notes closer together or further apart, its length too).
- **Playing:**
  - **While your DAW plays**, the song plays with it: bar 1 of the song is your DAW's bar 1, each hit on its exact sample, and it follows when you jump or loop.
  - With the DAW stopped, **PLAY** plays it from the mark on the bar numbers (click them to move it), at the DAW's last tempo; it stops at the song's end. Starting the DAW ends PLAY: the DAW leads.
  - The red line shows where it's playing. The drum sequencer and the groove preview can play at the same time (your choice).
- **EXPORT MIDI:** the whole song as one MIDI file, each drum on its note in the kit's map (as the sequencer's export). Click to save, or drag it onto a DAW track.
- **Saved with your project**, not in presets (loading a preset keeps the song). **Undo** takes back each edit.
- **Tests:**
  - the placing rules (dropped on a block's first or second half, past the end, longer, shorter, moved, duplicated, removed, in 3/4);
  - each block's groove repeated to fill it and cut at its end; MUTE; CHANGE TEMPO;
  - a groove as a block through the folder's map, with the factor and velocity, its kind from its name;
  - the song saved and read back exactly; its MIDI file (notes on the right ticks, tempo, time signature);
  - played with the DAW: a block at bar 3 heard exactly 4 s in at 120 BPM (sample 192000), the snare 1.5 beats later on its sample, and the DAW starting part way in at 90 BPM;
  - PLAY from bar 2 with the DAW stopped, each hit on its sample, stopping at the end; STOP; the DAW starting ending PLAY;
  - undo and redo; the project saved and loaded; a preset loaded keeping the song; EXPORT MIDI's notes;
  - the strip: a MIDI file dropped, DELETE, the menu's items each doing what it says, and ADD TO SONG from the GROOVES page.

What can't be checked here: how it sounds, how it feels to drag blocks in your DAW, and real groove packs.

Not in this part (next, if you want them, in this order): editing inside a block (its parts from other grooves, a drum taken out, velocity, quantize, cutting a block in two); making a song from one groove folder's verses, choruses and fills (a song made for you); tempo and time-signature changes within the song. The manual's chapter 7 describes all of these.

## 0.7.0

**The groove player, part 4: GROOVE PARTS.** Hear, drag or lay in just part of a groove: the hats of one over the kick and snare of another. Our own design, inspired by the groove parts of commercial drum software.

- **GROOVE PARTS** (right of the groove list): the chosen groove's parts, each with how many hits it has: KICK, SNARE, HI-HAT (closed, open and half-open), HI-HAT PEDAL, RIDE (and its bell), TOMS, CRASH (the crashes, china and splash), PERCUSSION (clap, tambourine, shaker). Parts the groove doesn't use are dimmed.
  - **Click a part to tick it.** From ALL, a click picks just that part; click more to add them; ALL ticks them all again.
  - **The preview plays only the ticked parts.** Tick or untick while it plays and you hear the change at once.
  - **Dragging a groove** out takes only its ticked parts. **Drag a part's row** to take just that part. Its file is named for it ("... Verse 01 HI-HAT").
- **LAYER ONTO A B C D:** the ticked parts go into that drum sequencer pattern.
  - Only the pattern's notes on those drums are replaced; its other drums stay as they are. A part shorter than the pattern repeats to fill its LENGTH.
  - LENGTH and the sequencer's other settings don't change. TEMPO's factor and VELOCITY apply, as with TO SEQUENCER.
  - For example: TO SEQUENCER B with one groove, then choose another groove, tick HI-HAT, LAYER ONTO B: its hats over the first groove's kick and snare.
  - **Undo** takes it back.
- **Fixed: undo after TO SEQUENCER.** Putting a second groove into the same pattern with nothing else changing (the same LENGTH, the same pattern, the sequencer already on) didn't make an undo step of its own, so undo took back both grooves. Each is its own undo step now.
- **The groove list** now fits its width (no scrolling sideways to see MATCH).
- **Tests:**
  - every drum in a part;
  - the preview playing only the ticked parts, and changing as you tick;
  - a dragged file with only those notes, named for them;
  - hats laid onto a pattern: its kick and snare kept, its old hats gone, a one-bar part repeated to fill 8 beats (32 sixteenths, the last at 7.75 beats), its velocity, LENGTH unchanged, and undo bringing the pattern back exactly;
  - TO SEQUENCER twice into the same pattern undoing one step;
  - the page's counts, ticking and LAYER ONTO.

What can't be checked here: how it sounds, and real groove packs' parts (which notes a pack uses for each drum comes from the folder's FILES' MAP).

Next: the song track (grooves arranged into a song). It's in the manual's chapter 7: please send it when you can, so it's designed from what it describes.

## 0.6.0

**The groove player, part 3: TAP TO FIND.** Tap a rhythm, and the grooves closest to it come first. Our own design, inspired by the tap-a-rhythm search of commercial drum software.

- **TAP TO FIND...** (GROOVES, left of the filter columns) opens the tap panel over the list:
  - **Six pads:** KICK, SNARE, HI-HAT, RIDE, TOMS, CRASH. Clicking one plays its drum and taps it. Your controller or the on-screen keyboard taps too: each note taps the drum it plays (through the kit's map).
  - **A two-bar loop** runs while the panel is open, with a **metronome** (a click on each beat, higher on the first of the bar) and a playhead. Tap one drum a time round if you like: every tap stays.
  - **TEMPO:** your song's when it opens; change it and the loop starts again. **QUANTIZE:** 1/4, 1/8 (as it opens: right for most beats), 1/16, 1/8 or 1/16 triplets (for swing and shuffle). Each tap is drawn where it lands on the grid, with a small mark where you played it.
  - **CLEAR** clears every tap, a row's **x** just that drum's. **CLOSE** puts it away (your taps stay for next time). **FIND** lists the grooves closest to your rhythm first.
- **MATCH** (a new column): how close each groove is, 0 to 100%. **CLEAR TAP** goes back to the list as it was. The folder, search, favourites and filter columns still narrow what's listed.
- **How grooves are compared** (our own measure):
  - **Only the drums you tapped count.** Tap kick and snare, and the hats don't matter.
  - Each drum scores by the hits both rhythms have on the QUANTIZE grid, against all the hits either has. The drums are weighted by how many hits you tapped on each.
  - **HI-HAT and RIDE stand in for each other** when a groove has only one of them, since the riding hand can be on either.
  - A groove is tried from **each of its bars** (looped if shorter than two bars), and its best two bars count, so a rhythm in bars 3-4 is found too.
- **Drop a MIDI file** on the GROOVES page: its first two bars are the rhythm searched with.
- **Right-click a groove: FIND SIMILAR** searches with its first two bars. FAVOURITE is there too.
- **Your library is read once more** the first time you open GROOVES after updating: the index now keeps each groove's rhythm, so a search never opens the files. Then it's quick again.
- **Tests:**
  - the comparison: the same rhythm 100%; drums not tapped not counting; closer rhythms higher (one kick different above four-on-the-floor above kick and snare swapped); QUANTIZE taking taps a little off (and one a hair early for beat 1); hi-hat and ride standing in; a rhythm found in bars 3-4; a one-bar groove looped;
  - the rhythm kept in the index;
  - the metronome on each beat to the sample at 120, higher on the bar, silent after;
  - notes as taps where they fall (1.25 beats at sample 30000, and round the loop), by drum group through the map; a pad's tap and drum;
  - the library sorted by match;
  - the page: tapping, FIND, FIND SIMILAR, a dropped file, CLEAR TAP.

What can't be checked here: how tapping feels in your DAW (a pad's tap is placed at the start of the next audio block, a few milliseconds; QUANTIZE takes that up), how the metronome sounds, and how well the comparison ranks real groove packs: tell us when a groove you'd expect near the top isn't.

Next, your choice: groove parts (take one drum's part from one groove and lay it over another), or the song track (needs the manual's chapter 7).

## 0.5.0

**The groove player, part 2: filter columns, and grooves into the drum sequencer.**

- **Filter columns** above the groove list: TYPE, FEEL, TIME, POWER HAND, TEMPO. Each lists the values the grooves you're browsing have (in the folder you've chosen, with your search and favourites), and how many have each.
  - **Click** a value: only grooves with it. Several in one column: grooves with any of them. **Right-click**: none with it (shown red, struck through). Click again to let it go.
  - The columns work together: a groove is listed if it passes every column. **CLEAR** lets them all go. A column's title shows how many of its values are in use.
  - **TYPE:** Intro, Verse, Pre-Chorus, Chorus, Bridge, Fill, Ending, Count-In, or Beat. Read from the groove's name, else its folder's, else the one above ("Fills/Var 2" is a fill), the way groove packs name them; Beat when none of those words is there.
  - **FEEL:** Straight or Swung. Our own measure, from the notes: whether the hits between the beats fall on the half and quarter beats (straight) or on the triplet grid (a swing or shuffle).
  - **TIME:** the time signatures there are. **POWER HAND:** as in the list. **TEMPO:** below 80, 80-99, 100-119, 120-139, 140-159, 160 and up, or Unknown (a file that doesn't say).
- **TO SEQUENCER A B C D:** the chosen groove becomes that drum sequencer pattern (replacing it).
  - Each hit is a note on its drum's row, where it was played (between the grid's steps too, so a laid-back snare stays laid back), with its velocity.
  - TEMPO's factor and VELOCITY go with it (as when you drag a groove out).
  - LENGTH becomes the groove's, and the sequencer comes on playing that pattern (with your song, on SONG).
  - A pattern holds 16 beats at most, so a longer groove brings its first 16. LENGTH is one setting for all four patterns.
  - Notes no drum plays in the folder's FILES' MAP are left out. The status line says what was made ("Verse 01 is pattern B now (8 beats)").
  - **Undo** takes it back.
  - SHUFFLE still applies to what's in the pattern: leave it at 0% to hear the groove as played.
- **Also:** tempos read from files show as whole numbers (104, not 104.0: a MIDI file keeps tempo in millionths of a second a beat).
- **Your library is read once more** the first time you open GROOVES after updating: the index now remembers FEEL. Then it's quick again.
- **Tests:** FEEL (straight and shuffle beats, a stray triplet note not making a beat swung); TYPE from names and folders (PRE-CHORUS not CHORUS, "Country" not a count-in); every column's values; choosing, several in a column, excluding, columns together, CLEAR; a groove put in pattern B and played with the song at 120 hitting the same samples as the groove (its off-grid snare 25440 samples in); the factor, velocity, a 32-beat groove cut to 16, notes with no drum; undo; the page's columns, counts and buttons.

What can't be checked here: real groove packs (TYPE depends on how a pack names its files: if one names them differently, tell us the words), and how it sounds.

Next, your choice: Tap-to-find (tap a rhythm, get the closest grooves), groove parts (one drum's part from one groove over another), or the song track (needs the manual's chapter 7).

## 0.4.0

**The groove player, part 1: your MIDI grooves, browsed, played through the kit and dragged into your DAW.** A new **GROOVES** tab (after DRUMS). Our own design, inspired by the groove browsers of commercial drum software. It reads only your own MIDI files, from where they are on your computer: no grooves, sounds or tags from anyone else ship with it.

- **Your folders:** ADD FOLDER... links a folder of MIDI grooves (and everything in its subfolders); REMOVE unlinks it (the files stay). The folders are listed on the left, opening as you go: choose one and the list shows only its grooves (with SUBFOLDERS on, its subfolders' too; off, just the files right in it). ALL GROOVES lists everything.
  - **FILES' MAP**, for each linked folder: which drum each note in its files is. GROOVE LIBRARY (the layout most commercial groove packs use: the map from 0.2.0), GENERAL MIDI, CUSTOM (your own map from SETTINGS), or ROOT (files made for this kit's keys).
  - The folders are read in the background the first time you open the page; what was read is remembered, so a big library comes up at once next time (only new or changed files are read again).
- **The list:** each groove's name, its folder, time signature, bars, tempo, **power hand** (what the riding hand plays most: closed hat, open hat, ride, crash or percussion) and **intensity** (how hard and how busy it's played).
  - These come from the file alone: its tempo (or "... 102 BPM" in its name or its folder's), its time signature, its notes. Power hand and intensity are our own measures, worked out from the notes through the folder's map.
  - **SEARCH**: words in a groove's name or its folder's (all of them). **ONLY**: just your favourites (click a groove's star). **SORT**: by name, nearest your song's tempo first, or your song's time signature first.
- **Preview:** click a groove to play it through the kit, round and round; double-click to play it once. **STOP** stops it. It plays your kit as it is (your sounds, effects and outputs), on the beat to the sample.
  - **TEMPO:** your song's, or the groove's own. Then **1/2x, 1x, 2x, 2/3x** (a 4/4 groove felt in 6/8) or **3/2x** (6/8 to 4/4): only how fast it plays, the playing stays as it was.
  - **VELOCITY:** every hit's velocity scaled together (25% to 175%), so the accents stay.
- **Drag a groove** onto a MIDI track in your DAW, or to a folder. The dragged file has the TEMPO factor and VELOCITY in it (at 2x the notes are twice as close, since your DAW places it at the song's tempo; at 2/3x a 4/4 groove is marked 6/8).
  - **DRAG: FOR THIS KIT** puts each note on this kit's note for that drum in its map (SETTINGS), so the groove plays right back through PR7-D; if the folder's map is already the kit's, the notes stay exactly as written. **AS WRITTEN** keeps the file's own notes (for other drum software).
- **Where they're kept:** your folders, favourites and these settings are in the presets folder (Grooves.xml, Groove Index.xml), shared by every PR7-D you open.
- **Grooves from other drum software:** PR7-D reads standard MIDI files. Grooves inside another program's own library: drag them out of that program into a folder first, then link the folder.
- **Tests:** reading a groove (notes, tempo from the file or a name, time signature, bars, a closing crash ending the last bar, power hand, intensity, files that aren't grooves); the library (folders as filters with and without subfolders, words, favourites, the three sorts, what's kept, a changed file read again, the background read); the preview (each hit on its sample at the song's tempo and its own, all five factors, looping and once, the velocity scale measured at -12 dB for half, STOP, a folder's map deciding which drum a note plays); the dragged file (this kit's notes, as written, velocity and factor in it); the page (the folder tree, filtering, search, playing).

What can't be checked here: how real groove packs' files read (none are here: only MIDI files made in the tests), and how the preview sounds. If a pack's tempo, bars or power hand look wrong in the list, send one or two of its files and we'll check them.

Next, part 2 (your choice): the filter columns (type: beat / fill / intro... from the names; time signature; power hand; tempo; each can be excluded) and dropping a groove into the drum sequencer, or Tap-to-find (tap a rhythm, get the closest grooves).

## 0.3.0

**Multiple outputs.** Stage 4 of the plan: each drum (or group of drums) can play out of its own stereo output, so your DAW mixes the kit like a recorded one, each drum on its own channel with its own plugins.

- **16 stereo outputs:** MAIN, then OUT 2 to OUT 16. Your DAW shows OUT 2 to 16 once you switch them on there (they start off, as most DAWs expect of multi-output instruments: Cubase, for one, would otherwise make 15 empty mixer channels the moment you load it).
- **OUTPUT on each drum** (DRUMS page, beside INSERT): MAIN or OUT 2 to 16. Choose several drums first (CTRL / SHIFT) to set them all at once: the three toms to OUT 5, say.
- **An insert bus has its own OUTPUT** (FX, INSERTS, beside its sends): a drum on a bus plays where the bus does, after its effects.
- **DELAY's and REVERB's returns have their own OUTPUT** too (FX, SENDS, at the end of each row): put the room on a channel of its own. The drums still feed DELAY and REVERB with their sends wherever they play.
- **What reaches OUT 2 to 16:** each drum as you've shaped it on the DRUMS page (VOLUME, PAN, the sample editor), its insert bus if it has one, and **OUTPUT** (the big knob: it's the whole plugin's volume, so a drum moved off MAIN keeps its level). Not MAIN's effects (MOD / LO-FI, the MASTER page's ECHELON, DOMINION, CONSTRICTOR) and not the LIMITER: those are MAIN's, and the other outputs are for your DAW to mix. BYPASS silences them all.
- **Nothing goes silent by mistake:** a drum, bus or return set to an output your DAW hasn't switched on plays in MAIN instead, so the kit always sounds whole.
- **In time:** CONSTRICTOR's LOOK-AHEAD and the LIMITER delay MAIN (your DAW makes up for it). OUT 2 to 16 are held back by the same amount, so a kick on OUT 2 and its room on MAIN stay together to the sample.
- **Presets and kits leave the outputs alone:** the routing belongs to your DAW project (it's kept there), so changing preset or loading a kit never moves drums between your mixer's channels.
- **Also:** ROOT's tip now says 19 keys above it (it still said 11).
- **Tests:** a drum on OUT 2 coming out there at its level and nowhere else; an output that's off sending its drum to MAIN, identical to MAIN; a bus's and the reverb's outputs; OUT 2 lined up with MAIN to the sample with CONSTRICTOR's and the LIMITER's look-ahead on (1149 samples of latency); OUTPUT turning every output down together; BYPASS; a mono MAIN with a stereo OUT 2; presets, kits and projects and the routing; the layouts the plugin accepts (MAIN mono or stereo, the others stereo or off).

What can't be checked here: how each Windows DAW shows the extra outputs (no DAW runs here). In Cubase you switch them on with the instrument's "Activate Outputs"; Reaper lists every output's channels. For Ableton Live and FL Studio we found no clear documentation of how they treat outputs that start off. Tell us which DAW you use, and whether OUT 2 to 16 show up: if one doesn't, the outputs can be made to start on.

## 0.2.0

**A bigger kit and note maps.** Stages 2 and 3 of the plan: 20 drums instead of 12, and a choice of which MIDI notes play which drum, so grooves written for other drum software (and General MIDI files) play on the right drums.

- **Eight new drums**, on the keys after the cymbal (C3 to G3 under ROOT C2):
  - **RD**, a ride (new RIDE model: a long, washy metal with a ping you set with SNAP);
  - **RB**, the ride's bell (new BELL model: four bell-like partials; TONE brings in the upper ones);
  - **CR2**, a second crash; **CHN**, a china (lower, trashier); **SPL**, a splash (higher, shorter): our CYMBAL model, tuned apart;
  - **HH**, a half-open hat (OPEN HAT, shorter), in the hats' CHOKE group, so the closed and pedal hats stop it;
  - **TMB**, a tambourine (new TAMBOURINE model: jingles in three quick strikes); **SHK**, a shaker (new SHAKER model: a soft swell; SNAP makes it sharper).
  - Like every drum, each can be a SAMPLE of your own instead. The sends, the insert bus strip and the sequencer's grid have all 20.
  - The 12 drums you had are unchanged, on the same keys, so your projects, kits and beats sound as before. Kits and sequences saved with 12 drums load; the new 8 start at their defaults.
- **Note maps** (SETTINGS, THE KEYS): MAP chooses which notes play which drum.
  - **Root:** as before, 20 keys from ROOT. With the sequencer on KEY, the four keys below still start the patterns (only in this map: the other maps use those notes for drums).
  - **Groove Library:** the layout most commercial drum-groove libraries use, so their MIDI grooves play as written: kick on 36 (and 34, 35), snare on 38, sidestick 37, rimshot 40, hats closed on 42 and 22, pedal 44 and 21, open 46 and 24, the toms, crashes on 49 and 57, ride on 51 and 29, bell 53, tambourine 3, shaker 2, and every variant note in between (about 110 notes in all).
  - **General MIDI:** the standard layout (35-36 kick, 38 snare, 42 / 44 / 46 hats, 49 / 57 crashes, 51 ride, 52 china, 53 bell, 54 tambourine, 55 splash, 69-70 shaker...).
  - **Custom:** the list below MAP shows every note (0 to 127) and the drum it plays. Change any, and the map you were on is copied into Custom with your change. **SAVE AS DEFAULT** keeps it for new instances; your project keeps it too. Presets don't change the map.
  - The DRUMS page shows each drum's note in the map you're on ("-" if it has none), and the keyboard tints the notes that play.
  - **EXPORT MIDI** writes each drum on its note in the map you're on, so the file plays back right in that map.
- **Not known yet about the Groove Library map** (our guess until we can check a real groove file): which of the ride's notes are its edge and mute hits (all play RD), which hat notes are the in-between openings (they play the closed or open hat), and which crash notes are mutes (they play CY). Note 86 plays a crash: the published tables we found say crash, where the list we started from had it under ride. The china, splash, second crash and half-open hat have no note in it yet. Custom lets you set all of these yourself.
- **Tests** check, by measurement:
  - each new drum: the ride's ping frequency and SNAP, the bell's partials and TONE, the tambourine's three strikes at 0, 6 and 14 ms, the shaker's swell time with SNAP, the half-open hat choked by the closed one, each new drum's level;
  - the maps: every note of the Groove Library list, General MIDI's notes, each map playing the right drum in the plugin, Custom edits kept by the project and the default file and untouched by presets, the KEY pattern keys only under Root, MIDI export on the map's notes, and 12-drum kits loading.

What can't be checked here: how the new drums sound. They're new synth models, set by measurement, not by ear yet.

## 0.1.0

**The first Paragon PR7-D: the PR7 family's drum instrument.** It starts from PR7-K 0.24.0's drum section (the kit, the drum sequencer, the sample editor, kits and sequences) and its effects, as an instrument of its own. PR7-K keeps its drum tab as it is. PR7-D installs beside PR7-K and the other PR7 plugins, with its own name, plugin ID and presets folder.

- **The synth is gone.** No oscillators, filter, envelopes, modulation, arpeggiator or step sequencer, and none of their pages, settings or presets. What's left is all drums.
- **DRUMS is the main page.** The window opens on it. The header has four tabs: DRUMS, FX, PRESETS, SETTINGS.
- **The keyboard plays the kit.** The 12 keys from ROOT (C2 at first) play the drums, C to B, as on General MIDI kits. There's no DRUMS ON switch any more: the kit always plays.
  - With the sequencer's TRIGGER on KEY, the four keys below the kit start patterns A to D. The on-screen keyboard tints them faintly while KEY is on.
  - Keys outside the kit do nothing (in PR7-K they played the synth).
- **The effects work on the drums.** Everything from PR7-K's FX page is here:
  - **INSERTS:** buses A to D, with DRIVE, GLITCH, MOD and LO-FI. The bus strip shows the 12 drums by name (BD, SD, CH...): click one to route it to the bus shown.
  - **SENDS:** DELAY and REVERB. The send matrix and each effect's own page have a knob for each of the 12 drums.
  - **MOD / LO-FI** on the whole kit, and **MASTER:** ECHELON, DOMINION, CONSTRICTOR, the LIMITER.
  - EQ Match's button to record your own sound is now RECORD KIT.
- **Your PR7-K files load here.** Every setting kept from PR7-K has the same name inside the plugin, so files saved in PR7-K open in PR7-D:
  - kits (.pr7kit) and sequences (.pr7seq): copy them from PR7-K's "Drum Kits" and "Drum Sequences" folders into PR7-D's (SETTINGS shows where PR7-D's presets folder is);
  - CONSTRICTOR's and DOMINION's own presets.
  - PR7-K's presets (.pr7rig) aren't read: PR7-D keeps its own presets folder, "Paragon PR7-D".
- **New factory presets:** our own kits and beats, grown from PR7-K's A40-A42 with their bass taken out. Not tuned by ear yet.
  - **A01 Init:** the drum synths, dry, no beat.
  - **A02 Room Kit:** the snare, clap, toms, hats and cymbal in a small room; the kick dry.
  - **A03 Room Beat:** the room kit with four beats (straight, busy, half time, a fill) that play with your song.
  - **A04 Tight Beat:** the room beat tightened with the sample editor: the clap's slap on the beat, a shorter open hat, a darker kick, hats thinned.
  - **A05 Dirty Beat:** the tight beat with the kick, snare and clap through insert bus A, driven (RABID) and worn (LO-FI cassette), the hats clean.
- **Logo:** "PR7-D", its D built from the P's parts in the same slanted, speed-cut lettering.
- **SETTINGS:** TUNE is gone (it tuned the synth). In its place, THE KEYS says which keys play the kit. MIDI links work as before.
- **Your DAW** sees PR7-D as an instrument in the "Drum" category.
- **Tests:** the effect and plugin tests that played synth notes now play steady test tones loaded as drum samples, so levels and pitches are still measured exactly. New tests check:
  - each of the 12 keys playing its own drum, ROOT moving them, keys outside the kit silent;
  - hits as one-shots (a note-off doesn't stop them);
  - each drum's sends, the Room Kit's reverb on the snare and none on the kick, and Dirty Beat's bus;
  - every factory preset sounding, staying clean, going quiet, and playing its beat with the song only if it has one;
  - the window's four tabs and pages, and the keyboard's tinted keys.
  - Every test also passes with Windows' smaller stack (1 MB), and pluginval passes at strictness 10.

What can't be checked here: how it sounds. The drums are PR7-K 0.24.0's drums, unchanged.

Next, your choice of stage 2 (see the plan): the bigger kit and the note mapping first is my suggestion, since the groove player needs both.
