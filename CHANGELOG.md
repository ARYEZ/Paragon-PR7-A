# Changelog

Each version's notes appear on its GitHub Release page.

## 0.1.0

**PR7-I's first version: its own plugin, made from PR7-D's code.** This is the starting point for the guitar
instrument, not a guitar yet: until the guitar engine arrives (the next stage), it still plays PR7-D's drum kit.

- **Its own plugin.** It's called Paragon PR7-I, with its own plugin ID, so it installs beside PR7-D and every other
  sibling without replacing any of them. It keeps its presets in its own folder ("Paragon PR7-I") and its own
  settings, so nothing mixes with PR7-D's.
- **Its own logo.** The header reads PR7-I. The I is built from the P's own parts, a thick stem with a bar at the top
  and bottom, so it doesn't read as a 1 next to the slanted 7.
- **The groove player is gone.** The GROOVES tab, with tap-to-find and riff match, belonged to the drums. Five tabs
  are left: DRUMS, MIXER, FX, PRESETS and SETTINGS.
- **The song track is gone**, along with the song creator, the grid editor, edit play style and bounce. The strip
  along the bottom of the window is empty for now. It's kept for the guitar's note view, which will show the string
  and fret each note plays on. The on-screen keyboard stays off, as you asked in PR7-D.
- **Still there until the guitar engine replaces them:** the DRUMS page (kit, kit view, drum sequencer, drum
  machine), the MIXER and the effects. They're the only sound source for now, and they hold the parts the guitar will
  reuse: the sample engine (velocity layers, round-robins, streaming from disk), MIDI handling and velocity curves,
  and IMPORT A RENDER.
- **Use PR7-G for the amp.** PR7-I will put out a clean DI guitar; put PR7-G after it for amps, cabinets and pedals.
