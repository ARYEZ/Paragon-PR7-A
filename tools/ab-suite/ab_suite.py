#!/usr/bin/env python3
"""PR7-A A/B suite: one MIDI file rendered through the reference guitar and through PR7-A, then compared note by note.

    python3 ab_suite.py make  "PR7-A AB Suite.mid"
    python3 ab_suite.py compare reference.wav pr7i.wav [report.txt]

Both commands build the same list of notes (`suite()`), so the comparison knows what every note is and when it was
played. The file uses the FACTORY articulation map (C-1 sustain, C#-1 palm mute, D#-1 power chord, E-1 power-chord
palm mute), force-string keys before every string change, picking keys (C#7 down for the single notes, D7 alternate
for the runs), legato off: the reference and PR7-A (KEY MAP: Factory) should choose the same string, fret, articulation
and stroke for every note, so what differs is the sound.

What it measures (PR7-A minus the reference, per section and over all):
  - the noise floor (the silent lead-in and tail);
  - when each pick lands after its note-on;
  - levels: the attack (first 30 ms), the body (30-150 ms, 150-500 ms), how fast a held note decays, by velocity;
  - the tone: the spectrum of the attack and the body in eight bands;
  - the pitch (cents);
  - note endings: how far a note has fallen 30-80 ms and 100-300 ms after its note-off (release noise included), and
    how long it takes to fall 20 / 40 dB;
  - the repeated runs: how many different takes come round, how soon one repeats, how much hits vary in level and timing;
  - the chug patterns: how much each pick stands out above what's still ringing (the "tightness"), how fast the rests
    go quiet;
  - fret noise level by velocity.

Only numpy and mido (for `make`). Our own design, for the PR7 family; nothing of the reference's is in it.
"""

import math
import struct
import sys
import wave

import numpy as np

# ---------------------------------------------------------------------------------------------------------------------
# The guitar (PR7-A's Tuning.h) and the keys (Guitar.h's keyswitch::, RenderList.h's keys::).
OPEN = [33, 40, 45, 50, 55, 59, 64]                    # string 0 = low A (a player's 7) .. 6 = high E
STRING_NAME = ["7 (low A)", "6 (E)", "5 (A)", "4 (D)", "3 (G)", "2 (B)", "1 (high E)"]
KEY_SUSTAIN, KEY_MUTE, KEY_POWER, KEY_POWER_MUTE = 12, 13, 15, 16
KEY_FORCE_LOW_A = 0
KEY_LEGATO_OFF = 27
KEY_FRET_NOISE = 30
KEY_PICK_DOWN, KEY_PICK_ALTERNATE = 109, 110
ART_NAME = {KEY_SUSTAIN: "sustain", KEY_MUTE: "palm mute", KEY_POWER: "power chord", KEY_POWER_MUTE: "power chord mute",
            KEY_FRET_NOISE: "fret noise"}
BPM = 120.0
LEAD_IN = 4.0       # silence first: the noise floor, and room for the first keyswitches


class Note:
    def __init__(self, section, group, string, fret, key, velocity, on, off, pick):
        self.section, self.group, self.string, self.fret = section, group, string, fret
        self.key, self.velocity, self.on, self.off, self.pick = key, velocity, on, off, pick

    @property
    def pitch(self):
        return OPEN[self.string] + self.fret if self.string >= 0 else None

    @property
    def hz(self):
        return 440.0 * 2.0 ** ((self.pitch - 69) / 12.0) if self.string >= 0 else None

    def label(self):
        if self.key == KEY_FRET_NOISE:
            return f"fret noise v{self.velocity}"
        return f"str {STRING_NAME[self.string]} fret {self.fret} {ART_NAME[self.key]} v{self.velocity}"


def suite():
    """Every note of the suite, in order. Fixed: changing it means the renders must be made again."""
    notes, sections = [], []
    t = LEAD_IN

    def section(name):
        sections.append((name, t))

    def add(name, group, string, fret, key, vel, hold, slot, pick):
        nonlocal t
        notes.append(Note(name, group, string, fret, key, vel, t, t + hold, pick))
        t += slot

    # 1. Single sustained notes: strings x frets x the three dynamics, held 2 s (tone, level, decay, attack).
    name = "1 sustain notes"
    section(name)
    for s in (0, 2, 3, 5, 6):
        for f in (0, 5, 12):
            for v in (45, 85, 118):
                add(name, f"s{s}f{f}", s, f, KEY_SUSTAIN, v, 2.0, 3.0, KEY_PICK_DOWN)
    t += 1.0
    # 2. Sustain velocity sweep (how level and layer follow velocity).
    name = "2 sustain velocity"
    section(name)
    for s, f in ((0, 0), (3, 5)):
        for v in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 119):
            add(name, f"s{s}f{f}", s, f, KEY_SUSTAIN, v, 1.0, 1.8, KEY_PICK_DOWN)
    t += 1.0
    # 3. Note lengths: how notes end, with and without release noise (the reference's stops after ~2 s held).
    name = "3 note endings"
    section(name)
    for s, f in ((0, 0), (3, 7), (6, 5)):
        for hold in (0.1, 0.25, 0.5, 1.0, 1.5, 2.5):
            add(name, f"s{s}f{f}", s, f, KEY_SUSTAIN, 85, hold, hold + 2.0, KEY_PICK_DOWN)
    t += 1.0
    # 4. Palm mute: the five degrees on four strings.
    name = "4 palm mute degrees"
    section(name)
    for s, f in ((0, 0), (1, 3), (3, 5), (6, 0)):
        for v in (13, 38, 64, 89, 115):
            add(name, f"s{s}f{f}", s, f, KEY_MUTE, v, 1.0, 1.6, KEY_PICK_DOWN)
    t += 1.0
    # 5. Palm mute velocity sweep.
    name = "5 palm mute velocity"
    section(name)
    for v in range(5, 126, 10):
        add(name, "s0f0", 0, 0, KEY_MUTE, v, 0.6, 1.2, KEY_PICK_DOWN)
    t += 1.0
    # 6. Repeats: 32 eighth notes at 120 BPM, alternate picking, each note-off on the next note-on (round-robins).
    name = "6 repeated notes"
    section(name)
    for group, s, f, key, v in (("low A mute", 0, 0, KEY_MUTE, 64), ("low A sustain", 0, 0, KEY_SUSTAIN, 85),
                                ("high E mute", 6, 0, KEY_MUTE, 64), ("D fret 5 mute", 3, 5, KEY_MUTE, 100)):
        for i in range(32):
            add(name, group, s, f, key, v, 0.25, 0.25, KEY_PICK_ALTERNATE)
        t += 2.0
    # 7. Chugs: 16ths at 150 BPM (100 ms), straight, then a pattern with rests and sustained accents (djent).
    name = "7 chugs"
    section(name)
    step = 0.1
    for i in range(48):
        add(name, "straight 16ths", 0, 0, KEY_MUTE, 100, step, step, KEY_PICK_ALTERNATE)
    t += 1.5
    pattern = "MM.MM.M.MM.MS..M"   # M palm mute, S sustained accent (held 3 steps), . rest
    for bar in range(4):
        i = 0
        while i < len(pattern):
            c = pattern[i]
            if c == "M":
                add(name, "pattern", 0, 0, KEY_MUTE, 100, step, step, KEY_PICK_ALTERNATE)
            elif c == "S":
                add(name, "pattern accent", 0, 0, KEY_SUSTAIN, 110, 3 * step, step, KEY_PICK_ALTERNATE)
            else:
                t += step
            i += 1
    t += 1.5
    # 8. Power chords: held, then palm-muted 16ths, then the pattern.
    name = "8 power chords"
    section(name)
    for f in (0, 3, 5, 7):
        add(name, "held", 0, f, KEY_POWER, 85, 1.0, 1.8, KEY_PICK_DOWN)
    t += 0.5
    for i in range(32):
        add(name, "mute 16ths", 0, 0, KEY_POWER_MUTE, 100, step, step, KEY_PICK_ALTERNATE)
    t += 1.5
    for bar in range(4):
        for c in pattern:
            if c in "MS":
                add(name, "mute pattern", 0, 0, KEY_POWER_MUTE, 100, step, step, KEY_PICK_ALTERNATE)
            else:
                t += step
    t += 1.5
    # 9. Fret noise (F#0) by velocity.
    name = "9 fret noise"
    section(name)
    for v in (127, 100, 70, 40):
        for i in range(4):
            add(name, f"v{v}", -1, 0, KEY_FRET_NOISE, v, 0.1, 0.5, None)
    t += 3.0   # silence at the end: the floor again
    return notes, sections, t


# ---------------------------------------------------------------------------------------------------------------------
def make(path):
    import mido
    notes, sections, length = suite()
    tpq = 960
    tick = lambda s: int(round(s * BPM / 60.0 * tpq))
    events = []   # (time, order, message)

    def key(note, at, vel=100):
        events.append((at, 0, mido.Message("note_on", note=note, velocity=vel, channel=0)))
        events.append((at + 0.02, 1, mido.Message("note_off", note=note, velocity=0, channel=0)))

    key(KEY_LEGATO_OFF, 0.5)
    art = force = pick = None
    for k, n in enumerate(notes):
        if n.key == KEY_FRET_NOISE:
            events.append((n.on, 2, mido.Message("note_on", note=KEY_FRET_NOISE, velocity=n.velocity, channel=0)))
            events.append((n.off, 1, mido.Message("note_off", note=KEY_FRET_NOISE, velocity=0, channel=0)))
            continue
        # Keyswitches 60 ms before the note when it changes (inside a run: just before its first note); the picking
        # key down before every single note, so each starts on a down stroke whatever came before.
        gap = n.on - (notes[k - 1].off if k > 0 else 0.0)
        lead = 0.06 if gap >= 0.08 else 0.004
        if n.string != force:
            key(KEY_FORCE_LOW_A + n.string, n.on - lead - 0.002)
            force = n.string
        if n.key != art:
            key(n.key, n.on - lead)
            art = n.key
        if n.pick == KEY_PICK_DOWN or n.pick != pick:
            key(n.pick, n.on - lead + 0.001)
            pick = n.pick
        events.append((n.on, 2, mido.Message("note_on", note=n.pitch, velocity=n.velocity, channel=0)))
        events.append((n.off, 1, mido.Message("note_off", note=n.pitch, velocity=0, channel=0)))
    for name, at in sections:
        events.append((at, -1, mido.MetaMessage("marker", text=name)))
    events.sort(key=lambda e: (tick(e[0]), e[1]))
    track = mido.MidiTrack()
    track.append(mido.MetaMessage("track_name", name="PR7-A AB Suite", time=0))
    track.append(mido.MetaMessage("set_tempo", tempo=int(round(60e6 / BPM)), time=0))
    track.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
    now = 0
    for at, _, msg in events:
        tk = tick(at)
        track.append(msg.copy(time=tk - now))
        now = tk
    track.append(mido.MetaMessage("end_of_track", time=max(0, tick(length) - now)))
    f = mido.MidiFile(ticks_per_beat=tpq)
    f.tracks.append(track)
    f.save(path)
    played = sum(1 for n in notes)
    print(f"wrote {path}: {played} notes, {length / 60:.1f} minutes ({length:.1f} s); bounce {math.ceil(length)} s from bar 1")


# ---------------------------------------------------------------------------------------------------------------------
# Reading audio (PCM 16 / 24 / 32-bit or 32 / 64-bit float, any channels: mixed to mono).
def read_wav(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV file")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype="<f4" if bits == 32 else "<f8").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw[:len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / float(1 << 23)
    elif bits == 32:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / float(1 << 31)
    else:
        raise SystemExit(f"{path}: {bits}-bit audio not read")
    x = x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
    return x, rate


def resample(x, rate, to):
    if rate == to:
        return x
    n = int(len(x) * to / rate)
    return np.interp(np.arange(n) * rate / to, np.arange(len(x)), x)


def db(v):
    return 20.0 * math.log10(max(v, 1e-6))   # (floor -120 dB: digital silence isn't a level)


def rms(x):
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


BANDS = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 12000)]


def band_db(x, rate):
    """Each band's share of the window's energy, dB (so the shape, not the level)."""
    if len(x) < 64:
        return [0.0] * len(BANDS)
    w = x * np.hanning(len(x))
    spec = np.abs(np.fft.rfft(w)) ** 2
    freqs = np.fft.rfftfreq(len(w), 1.0 / rate)
    total = spec.sum() + 1e-20
    return [10 * math.log10(spec[(freqs >= lo) & (freqs < hi)].sum() / total + 1e-6) for lo, hi in BANDS]   # (-60 dB floor)


def pitch_cents(x, rate, hz):
    """The pitch near `hz` (autocorrelation within half an octave), in cents from it."""
    if hz is None or len(x) < int(rate / hz * 4):
        return None
    x = x - x.mean()
    lo, hi = int(rate / (hz * 1.41)), int(rate / (hz / 1.41))
    if hi >= len(x) // 2:
        return None
    n = len(x) - hi
    best, best_lag = -1e9, None
    for lag in range(lo, hi + 1):
        c = float(np.dot(x[:n], x[lag:lag + n]))
        if c > best:
            best, best_lag = c, lag
    if best_lag is None or best <= 0:
        return None
    # parabolic refinement
    def c(l):
        return float(np.dot(x[:n], x[l:l + n]))
    a, b, d = c(best_lag - 1), best, c(best_lag + 1)
    shift = 0.5 * (a - d) / (a - 2 * b + d) if (a - 2 * b + d) != 0 else 0.0
    f = rate / (best_lag + shift)
    return 1200 * math.log2(f / hz)


def envelope_db(x, rate, ms=2.0):
    n = max(1, int(rate * ms / 1000))
    e = np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))
    return 20 * np.log10(np.maximum(e, 1e-6))


def onset(x, rate, at, before=0.03, after=0.12):
    """A pick: the first sample after `at - before` reaching a tenth of the window's peak, or, where the note before is
    still ringing, rising clear above it (twice its level, or halfway from it to the peak)."""
    a, b = max(int((at - before) * rate), 0), int((at + after) * rate)
    w = np.abs(x[a:b])
    if len(w) == 0 or w.max() < 1e-5:
        return None
    ring = np.abs(x[max(int((at - 0.03) * rate), 0): max(int((at - 0.008) * rate), 1)])
    base = float(ring.max()) if len(ring) else 0.0
    peak = float(w.max())
    threshold = max(0.1 * peak, 2.0 * base)
    if threshold >= peak:
        threshold = base + 0.5 * (peak - base)
    start = int(max(0.0, before - 0.008) * rate) if base > 0.05 * peak else 0   # (not inside the ringing before)
    hits = np.nonzero(w[start:] >= threshold)[0]
    return (a + start + int(hits[0])) / rate if len(hits) else None


def likeness(a, b, max_shift):
    best = -1.0
    for s in range(-max_shift, max_shift + 1):
        if s >= 0:
            p, q = a[s:], b[: len(b) - s] if s else b
        else:
            p, q = a[: len(a) + s], b[-s:]
        n = min(len(p), len(q))
        p, q = p[:n], q[:n]
        d = math.sqrt(float(np.dot(p, p)) * float(np.dot(q, q)))
        if d > 0:
            best = max(best, float(np.dot(p, q)) / d)
    return best


# ---------------------------------------------------------------------------------------------------------------------
def measure(x, rate, notes, sections, length):
    """Everything about one render."""
    out = {"notes": [], "groups": {}, "floor": {}}
    # The noise floor: the lead-in (after the first keyswitch's 1 s) and the tail.
    for where, a, b in (("lead-in", 1.0, LEAD_IN - 0.3), ("tail", length - 2.5, length - 0.2)):
        seg = x[int(a * rate): int(b * rate)]
        out["floor"][where] = {"rms": db(rms(seg)), "bands": band_db(seg, rate) if rms(seg) > 1e-7 else None}
    for i, n in enumerate(notes):
        nxt = notes[i + 1].on if i + 1 < len(notes) else length
        o = onset(x, rate, n.on, before=0.03, after=min(0.12, max(0.04, nxt - n.on - 0.005)))
        m = {"onset_ms": None if o is None else (o - n.on) * 1000.0}
        if o is None:
            out["notes"].append(m)
            continue
        oi = int(o * rate)
        seg = lambda a, b: x[oi + int(a * rate): oi + int(b * rate)]
        until = nxt - o   # the next note's pick (runs) or the next slot
        m["peak"] = db(float(np.max(np.abs(seg(0, min(0.05, until))))))
        m["attack"] = db(rms(seg(0, min(0.03, until))))
        if until > 0.15:
            m["body1"] = db(rms(seg(0.03, 0.15)))
        if until > 0.5 and n.off - o > 0.5:
            m["body2"] = db(rms(seg(0.15, 0.5)))
        held = n.off - o
        if held > 0.9:
            # decay while held: dB/s from 0.2 s to the note-off
            env = envelope_db(seg(0.2, held - 0.02), rate, 20.0)
            # only while clearly above the noise: up to where it's 40 dB under its start
            below = np.nonzero(env < env[: int(0.05 * rate)].max() - 40.0)[0] if len(env) else []
            env = env[: below[0]] if len(below) else env
            if len(env) > rate * 0.2:
                tt = np.arange(len(env)) / rate
                m["decay_db_s"] = float(np.polyfit(tt, env, 1)[0])
        if n.key != KEY_FRET_NOISE:
            m["bands_attack"] = band_db(seg(0, min(0.06, until)), rate)
            if until > 0.4 and held > 0.4:
                m["bands_body"] = band_db(seg(0.06, 0.4), rate)
                m["cents"] = pitch_cents(seg(0.1, 0.4), rate, n.hz if n.key in (KEY_SUSTAIN, KEY_MUTE) else n.hz)
        # note endings: where a rest follows (a chug pattern's 100 ms rests too); 100-300 ms only with 0.35 s free
        if nxt - n.off >= 0.09 and n.key != KEY_FRET_NOISE:
            ref = m["peak"]
            off_i = int(n.off * rate)
            m["tail_30_80"] = db(rms(x[off_i + int(0.03 * rate): off_i + int(0.08 * rate)])) - ref
            if nxt - n.off >= 0.35:
                m["tail_100_300"] = db(rms(x[off_i + int(0.1 * rate): off_i + int(0.3 * rate)])) - ref
            env = envelope_db(x[off_i: off_i + int(min(1.5, nxt - n.off - 0.005) * rate)], rate, 5.0)
            start = env[: int(0.005 * rate)].max() if len(env) else -200
            for drop in (20, 40):
                idx = np.nonzero(env < start - drop)[0]
                m[f"fall{drop}_ms"] = float(idx[0] / rate * 1000) if len(idx) else None
        out["notes"].append(m)
    # Runs: distinct takes, repeats, level and timing spread; pick contrast (tightness).
    by_group = {}
    for i, n in enumerate(notes):
        if n.section[0] in "678":
            by_group.setdefault((n.section, n.group), []).append(i)
    for (sec, grp), idx in by_group.items():
        if len(idx) < 8:
            continue
        cuts, onsets, contrast = [], [], []
        for i in idx:
            m = out["notes"][i]
            if m.get("onset_ms") is None:
                continue
            o = notes[i].on + m["onset_ms"] / 1000.0
            onsets.append(m["onset_ms"])
            nxt = notes[idx[idx.index(i) + 1]].on if idx.index(i) + 1 < len(idx) else notes[i].off + 0.1
            ln = max(0.03, min(0.2, nxt - o - 0.005))
            cuts.append(x[int(o * rate) - int(0.002 * rate): int(o * rate) + int(ln * rate)])
            # how far the pick stands out above the note still ringing before it (only right after another note)
            k = idx.index(i)
            if k > 0 and notes[i].on - notes[idx[k - 1]].off <= 0.005:
                pre = x[int((notes[i].on - 0.02) * rate): int((notes[i].on - 0.002) * rate)]
                post = x[int(o * rate): int(o * rate) + int(0.02 * rate)]
                contrast.append(db(rms(post)) - db(rms(pre)))
        ln = min(len(c) for c in cuts)
        cuts = [c[:ln] for c in cuts]
        ids, firsts = [], []
        for c in cuts:
            # 0.98 here, not the import's 0.999: in a played riff a release noise, the line noise or the note before can lie
            # under a pick, so even the very same recording isn't bit-identical; different takes measured at most 0.86
            hit = next((k for k, f in enumerate(firsts) if likeness(cuts[f], c, max(1, int(0.0005 * rate))) >= 0.98), None)
            if hit is None:
                firsts.append(len(ids))
                hit = len(firsts) - 1
            ids.append(hit)
        gaps = []
        last = {}
        for k, t_id in enumerate(ids):
            if t_id in last:
                gaps.append(k - last[t_id])
            last[t_id] = k
        levels = [out["notes"][i].get("attack") for i in idx if out["notes"][i].get("attack") is not None]
        out["groups"][f"{sec}: {grp}"] = {
            "hits": len(ids), "different": len(firsts), "sequence": " ".join(str(v + 1) for v in ids[:24]),
            "shortest_repeat": min(gaps) if gaps else None,
            "level_spread_db": float(np.std(levels)) if levels else None,
            "timing_spread_ms": float(np.std(onsets)) if onsets else None,
            "pick_contrast_db": float(np.median(contrast)) if contrast else None,
        }
    return out


def compare(ref_path, ours_path, report_path=None):
    notes, sections, length = suite()
    a, ra = read_wav(ref_path)
    b, rb = read_wav(ours_path)
    rate = 48000
    a, b = resample(a, ra, rate), resample(b, rb, rate)
    lines = []
    say = lines.append
    say(f"A/B SUITE  reference: {ref_path} ({ra} Hz, {len(a) / rate:.1f} s)   PR7-A: {ours_path} ({rb} Hz, {len(b) / rate:.1f} s)")
    say(f"the suite is {length:.1f} s long; both renders should start at bar 1")
    for name, x in (("reference", a), ("PR7-A", b)):
        if len(x) < (length - 5) * rate:
            say(f"!! the {name} render is shorter than the suite: bounced from bar 1, the whole length?")
    A, B = measure(a, rate, notes, sections, length), measure(b, rate, notes, sections, length)
    diffs = []   # (size, text)

    current = [""]

    def stat(key, sel, label, unit, scale=1.0, threshold=1.0):
        va = [A["notes"][i][key] for i in sel if A["notes"][i].get(key) is not None and B["notes"][i].get(key) is not None]
        vb = [B["notes"][i][key] for i in sel if A["notes"][i].get(key) is not None and B["notes"][i].get(key) is not None]
        if not va:
            return None
        ma, mb = float(np.median(va)), float(np.median(vb))
        d = mb - ma
        txt = f"{label:34s} ref {ma * scale:8.1f}  PR7-A {mb * scale:8.1f}  diff {d * scale:+7.1f} {unit}   ({len(va)} notes)"
        say("    " + txt)
        if abs(d * scale) >= threshold:
            diffs.append((abs(d * scale) / threshold, f"{current[0]}: " + txt))
        return d

    say("")
    say("NOISE FLOOR (silence)")
    for where in ("lead-in", "tail"):
        fa, fb = A["floor"][where], B["floor"][where]
        say(f"    {where:8s} ref {fa['rms']:7.1f} dBFS   PR7-A {fb['rms']:7.1f} dBFS")
        if abs(fa["rms"] - fb["rms"]) > 6 and max(fa["rms"], fb["rms"]) > -100:
            diffs.append((abs(fa["rms"] - fb["rms"]) / 6, f"noise floor ({where}): the reference {fa['rms']:.1f} dBFS, PR7-A {fb['rms']:.1f} (line noise?)"))
        if fa["bands"]:
            say("             ref bands " + " ".join(f"{v:6.1f}" for v in fa["bands"]))
    for sec_name, _ in sections:
        sel = [i for i, n in enumerate(notes) if n.section == sec_name]
        current[0] = sec_name
        say("")
        say(f"SECTION {sec_name}  ({len(sel)} notes)")
        missing = sum(1 for i in sel if A["notes"][i].get("onset_ms") is None), sum(1 for i in sel if B["notes"][i].get("onset_ms") is None)
        if any(missing):
            say(f"    silent notes: reference {missing[0]}, PR7-A {missing[1]}")
            if missing[1] > missing[0]:
                diffs.append((missing[1] - missing[0], f"{sec_name}: {missing[1] - missing[0]} notes silent in PR7-A that the reference plays"))
        stat("onset_ms", sel, "pick lands after note-on", "ms", threshold=3)
        stat("peak", sel, "peak level", "dB", threshold=1.0)
        stat("attack", sel, "attack level (0-30 ms)", "dB", threshold=1.0)
        stat("body1", sel, "body level (30-150 ms)", "dB", threshold=1.0)
        stat("body2", sel, "body level (150-500 ms)", "dB", threshold=1.5)
        stat("decay_db_s", sel, "decay while held", "dB/s", threshold=2.0)
        stat("cents", sel, "pitch", "cents", threshold=8)
        stat("tail_30_80", sel, "after note-off 30-80 ms (re peak)", "dB", threshold=3)
        stat("tail_100_300", sel, "after note-off 100-300 ms (re peak)", "dB", threshold=4)
        stat("fall20_ms", sel, "falls 20 dB after note-off in", "ms", threshold=10)
        stat("fall40_ms", sel, "falls 40 dB after note-off in", "ms", threshold=20)
        for kind in ("bands_attack", "bands_body"):
            rows = [(A["notes"][i][kind], B["notes"][i][kind]) for i in sel if A["notes"][i].get(kind) and B["notes"][i].get(kind)]
            if rows:
                da = np.median(np.array([r[0] for r in rows]), axis=0)
                dbb = np.median(np.array([r[1] for r in rows]), axis=0)
                d = dbb - da
                say(f"    tone, {kind[6:]:6s} (PR7-A minus ref, dB per band {BANDS[0][0]}-{BANDS[-1][1]} Hz): " + " ".join(f"{v:+5.1f}" for v in d))
                worst = int(np.argmax(np.abs(d)))
                if abs(d[worst]) >= 2.0:
                    lo, hi = BANDS[worst]
                    diffs.append((abs(d[worst]) / 2.0, f"{sec_name}: tone ({kind[6:]}) {d[worst]:+.1f} dB at {lo}-{hi} Hz"))
        # Per velocity (sweeps): the level curve.
        if sec_name[0] in "25":
            say("    level by velocity (attack dB):  vel   ref   PR7-A  diff")
            for i in sel:
                la, lb = A["notes"][i].get("attack"), B["notes"][i].get("attack")
                if la is not None and lb is not None:
                    say(f"        {notes[i].label():44s} {la:6.1f} {lb:6.1f} {lb - la:+5.1f}")
        if sec_name[0] == "3":
            say("    endings by hold time:  after note-off 100-300 ms (re peak)  ref / PR7-A")
            for i in sel:
                ta, tb = A["notes"][i].get("tail_100_300"), B["notes"][i].get("tail_100_300")
                if ta is not None and tb is not None:
                    say(f"        {notes[i].label():44s} held {notes[i].off - notes[i].on:4.2f} s  {ta:6.1f} {tb:6.1f}")
        for key in [k for k in A["groups"] if k.startswith(sec_name)]:
            ga, gb = A["groups"][key], B["groups"].get(key, {})
            say(f"    run {key[len(sec_name) + 2:]}:")
            for field, label, unit, thr in (("different", "different takes", "", 1), ("shortest_repeat", "shortest repeat (hits)", "", 1),
                                            ("level_spread_db", "level spread", "dB", 0.5), ("timing_spread_ms", "timing spread", "ms", 1.5),
                                            ("pick_contrast_db", "pick stands out by", "dB", 1.0)):
                va, vb = ga.get(field), gb.get(field)
                if va is None or vb is None:
                    continue
                say(f"        {label:24s} ref {va:7.2f}  PR7-A {vb:7.2f} {unit}")
                if abs(vb - va) >= thr:
                    diffs.append((abs(vb - va) / thr, f"{key}: {label} ref {va:.2f}, PR7-A {vb:.2f} {unit}"))
            say(f"        takes in order  ref:   {ga['sequence']}")
            say(f"                        PR7-A: {gb.get('sequence', '')}")
    say("")
    say("BIGGEST DIFFERENCES (largest first; size = how many times over its threshold)")
    for size, txt in sorted(diffs, reverse=True)[:40]:
        say(f"  {size:5.1f}  {txt}")
    text = "\n".join(lines)
    print(text)
    if report_path:
        with open(report_path, "w") as fh:
            fh.write(text + "\n")


def check(csv_path):
    """What PR7-A did with each note (GuitarCoreTests "[ab-suite]"'s CSV) against what the suite meant: the string, the
    fret, the articulation, and a down stroke for every single note."""
    notes, _, _ = suite()
    rows = [line.strip().split(",") for line in open(csv_path).read().splitlines()[1:] if line.strip()]
    if len(rows) != len(notes):
        print(f"!! {len(rows)} notes played, the suite has {len(notes)}")
    wrong = 0
    for n, r in zip(notes, rows):
        if n.key == KEY_FRET_NOISE:
            continue
        string, fret, art, stroke, power = int(r[3]), int(r[4]), r[5], r[6], r[7] == "1"
        want_art = "mute" if n.key in (KEY_MUTE, KEY_POWER_MUTE) else "sustain"
        want_power = n.key in (KEY_POWER, KEY_POWER_MUTE)
        problems = []
        if int(r[1]) != n.pitch:
            problems.append(f"note {r[1]} (meant {n.pitch})")
        if (string, fret) != (n.string, n.fret):
            problems.append(f"string {string} fret {fret}")
        if art != want_art or power != want_power:
            problems.append(f"{art}{' power' if power else ''}")
        if n.pick == KEY_PICK_DOWN and stroke != "down":
            problems.append("up stroke")
        if problems:
            wrong += 1
            if wrong <= 20:
                print(f"  {n.on:8.3f} s {n.label()}: " + ", ".join(problems))
    print(f"{len(notes) - wrong} of {len(notes)} notes as the suite meant" + ("" if wrong == 0 else f", {wrong} not"))
    return wrong == 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        sys.exit(0 if check(sys.argv[2]) else 1)
    elif len(sys.argv) >= 3 and sys.argv[1] == "make":
        make(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "compare":
        compare(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        print(__doc__)
        sys.exit(1)
