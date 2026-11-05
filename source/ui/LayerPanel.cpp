#include "ui/LayerPanel.h"
#include "dsp/synth/Instrument.h"
#include "ui/DrumSeqPage.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int numKnobs = 5;
    const char* const knobNames[numKnobs] = { "PITCH", "OFFSET", "VEL LOW", "VEL HIGH", "LENGTH" };
    // Each layer's colour in the picture (the drum's is pearl white).
    const juce::Colour layerColours[3] = { juce::Colour (0xffff7a1a), juce::Colour (0xffff3d8b), juce::Colour (0xffffb22e) };
}

LayerPanel::LayerPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    auto& state = processor.getParameters();
    for (int n = 0; n < 3; ++n)
    {
        auto& c = columns[(size_t) n];
        c.title.setText ("LAYER " + juce::String (n + 1), juce::dontSendNotification);
        c.title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        c.title.setColour (juce::Label::textColourId, layerColours[n]);
        c.title.setBorderSize ({});
        addAndMakeVisible (c.title);
        c.name.setFont (juce::FontOptions (12.0f));
        c.name.setColour (juce::Label::textColourId, ParagonColours::text);
        c.name.setMinimumHorizontalScale (0.6f);
        c.name.setBorderSize ({});
        addAndMakeVisible (c.name);
        for (auto* b : { &c.load, &c.remove, &c.alignButton, &c.invert })
        {
            DrumSeqPanel::style (*b);
            addAndMakeVisible (*b);
        }
        c.invert.setClickingTogglesState (true);
        c.load.setTooltip ("A sound for this layer: a file (one hit), a folder of hits (named by velocity layer and round robin, "
                           "\"..._V1_RR2\", or sorted by loudness), or a folder holding a folder per mic (KICK IN, SNARE TOP, OH, ROOM...: "
                           "each mic into its channel).");
        c.remove.setTooltip ("Takes this layer away (its settings back to new).");
        c.alignButton.setTooltip ("Lines this layer up with the drum's own hit: OFFSET and POLARITY set where the two match best "
                                  "(their starts found, then compared sample by sample). Two sounds a little apart or turned over "
                                  "cancel each other out: thin. Lined up, they add.");
        c.invert.setTooltip ("INVERT (polarity): the layer turned over (its waveform upside down). AUTO-ALIGN sets it.");
        c.load.onClick = [this, n] { if (onLoad) onLoad (n); };
        c.remove.onClick = [this, n]
        {
            processor.clearLayer (slot, n);
            status[(size_t) n] = {};
            refresh();
        };
        c.alignButton.onClick = [this, n] { align (n); };
        c.invert.onClick = [this, n] { tweakChanged (n, true); };
        c.level = std::make_unique<ParamControl> (state, ParamIDs::drum (0, ParamIDs::layerLevel (n)), "LEVEL",
                                                  "The layer's level (OFF: not played). Its velocity follows the drum's.");
        c.out = std::make_unique<ParamControl> (state, ParamIDs::drum (0, ParamIDs::layerOut (n)), "OUT",
                                                "Where it plays: WITH THE DRUM (its VOLUME, PAN, INSERT and OUTPUT), or a mixer channel "
                                                "(its own fader, effects and sends). A multi-mic layer: its close mics there, its "
                                                "overheads and room into theirs.");
        addAndMakeVisible (*c.level);
        addAndMakeVisible (*c.out);
        for (int k = 0; k < numKnobs; ++k)
        {
            auto& s = c.knobs[(size_t) k];
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
            s.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            auto& l = c.knobLabels[(size_t) k];
            l.setText (knobNames[k], juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            l.setColour (juce::Label::textColourId, ParagonColours::textDim);
            l.setBorderSize ({});
            l.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
            switch ((Knob) k)
            {
                case Knob::pitch:
                    s.setRange (-24.0, 24.0, 0.01);
                    s.setTextValueSuffix (" st");
                    s.setTooltip ("The layer's pitch, in semitones (and hundredths): tune it to the drum.");
                    break;
                case Knob::offset:
                    s.setRange (-50.0, 50.0, 0.01);
                    s.setTextValueSuffix (" ms");
                    s.setTooltip ("Later (+) or earlier (-, by skipping into it) than the drum. AUTO-ALIGN sets it.");
                    break;
                case Knob::velLo:
                    s.setRange (0.0, 127.0, 1.0);
                    s.setTooltip ("The softest hit it plays on (as the hit comes in): above 0, soft hits leave it out.");
                    break;
                case Knob::velHi:
                    s.setRange (0.0, 127.0, 1.0);
                    s.setTooltip ("The hardest hit it plays on: below 127, hard hits leave it out.");
                    break;
                case Knob::length:
                    s.setRange (0.0, 2000.0, 1.0);
                    s.setSkewFactorFromMidPoint (200.0);
                    s.setTooltip ("How long it plays (WHOLE: all of it); it fades out there. Just its attack: a few tens of ms.");
                    s.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("WHOLE") : juce::String (juce::roundToInt (v)) + " ms"; };
                    s.updateText();
                    break;
            }
            s.onValueChange = [this, n] { if (! updating) tweakChanged (n, false); };
            s.onDragEnd = [this, n] { if (! updating) tweakChanged (n, true); };
            addAndMakeVisible (s);
        }
    }
    setSlot (0);
    startTimerHz (8);
}

LayerPanel::~LayerPanel() = default;

void LayerPanel::setSlot (int newSlot)
{
    slot = juce::jlimit (0, synth::drums::numSlots - 1, newSlot);
    for (int n = 0; n < 3; ++n)
    {
        columns[(size_t) n].level->attach (ParamIDs::drum (slot, ParamIDs::layerLevel (n)));
        columns[(size_t) n].out->attach (ParamIDs::drum (slot, ParamIDs::layerOut (n)));
    }
    status = {};
    shownKey = {};
    refresh();
}

void LayerPanel::refresh()
{
    // Only when something changed (the timer asks often).
    juce::String key;
    key << slot;
    for (int n = 0; n < 3; ++n)
    {
        const auto& t = processor.getLayerTweak (slot, n);
        key << "|" << processor.getLayerRef (slot, n) << ":" << t.pitch << ":" << t.offsetMs << ":" << (int) t.invert << ":" << t.velLo
            << ":" << t.velHi << ":" << t.lengthMs << ":" << (int) (processor.getLayer (slot, n) != nullptr);
    }
    key << "|" << processor.getInstrumentRef (slot) << ":" << (int) (processor.getDrumInstrument (slot) != nullptr);
    if (key == shownKey)
        return;
    shownKey = key;
    updating = true;
    for (int n = 0; n < 3; ++n)
    {
        auto& c = columns[(size_t) n];
        const auto ref = processor.getLayerRef (slot, n);
        const auto has = ref.isNotEmpty();
        const auto loaded = processor.getLayer (slot, n);
        const auto path = ref.fromFirstOccurrenceOf ("||", false, false);
        auto text = ! has ? juce::String ("EMPTY: LOAD... a sound to play with this drum")
                          : juce::File (path).getFileName() + (loaded == nullptr ? "   (LOADING...)" : juce::String());
        if (loaded != nullptr)
        {
            auto hits = 0;
            for (const auto& l : loaded->layers)
                hits += (int) l.hits.size();
            text << "   " << hits << (hits == 1 ? " hit" : " hits");
            if (loaded->layers.size() > 1)
                text << ", " << (int) loaded->layers.size() << " velocity layers";
            if (loaded->hasMics())
                text << ", " << (int) loaded->micChannels().size() << " mics";
        }
        c.name.setText (text, juce::dontSendNotification);
        c.name.setColour (juce::Label::textColourId, has ? ParagonColours::text : ParagonColours::textDim);
        const auto& t = processor.getLayerTweak (slot, n);
        c.knobs[(size_t) Knob::pitch].setValue (t.pitch, juce::dontSendNotification);
        c.knobs[(size_t) Knob::offset].setValue (t.offsetMs, juce::dontSendNotification);
        c.knobs[(size_t) Knob::velLo].setValue (t.velLo, juce::dontSendNotification);
        c.knobs[(size_t) Knob::velHi].setValue (t.velHi, juce::dontSendNotification);
        c.knobs[(size_t) Knob::length].setValue (t.lengthMs, juce::dontSendNotification);
        c.invert.setToggleState (t.invert, juce::dontSendNotification);
        c.remove.setEnabled (has);
        c.alignButton.setEnabled (loaded != nullptr);
        c.invert.setEnabled (has);
        c.level->setEnabled (has);
        c.out->setEnabled (has);
        for (auto& k : c.knobs)
            k.setEnabled (has);
    }
    updating = false;
    updatePicture();
    repaint();
}

void LayerPanel::timerCallback()
{
    if (isShowing())
        refresh();
}

void LayerPanel::tweakChanged (int n, bool final)
{
    auto& c = columns[(size_t) n];
    synth::drums::LayerTweak t;
    t.pitch = (float) c.knobs[(size_t) Knob::pitch].getValue();
    t.offsetMs = (float) c.knobs[(size_t) Knob::offset].getValue();
    t.velLo = juce::roundToInt (c.knobs[(size_t) Knob::velLo].getValue());
    t.velHi = juce::roundToInt (c.knobs[(size_t) Knob::velHi].getValue());
    // The two velocity limits keep in order: the one dragged pushes the other.
    if (t.velLo > t.velHi)
    {
        if (c.knobs[(size_t) Knob::velLo].isMouseButtonDown())
            t.velHi = t.velLo;
        else
            t.velLo = t.velHi;
    }
    t.lengthMs = (float) c.knobs[(size_t) Knob::length].getValue();
    t.invert = c.invert.getToggleState();
    processor.setLayerTweak (slot, n, t, final);
    status[(size_t) n] = {};
    refresh();
}

void LayerPanel::setKnob (int n, Knob knob, double value)
{
    columns[(size_t) n].knobs[(size_t) knob].setValue (value, juce::dontSendNotification);
    tweakChanged (n, true);
}

void LayerPanel::align (int n)
{
    const auto result = processor.alignLayer (slot, n);
    if (! result.found)
        status[(size_t) n] = "CAN'T ALIGN: " + result.problem;
    else
        status[(size_t) n] = "ALIGNED: " + juce::String (result.offsetMs, 2) + " ms" + (result.invert ? ", TURNED OVER" : "")
                           + ",  MATCH " + juce::String (juce::roundToInt (result.match * 100.0)) + "%";
    shownKey = {};
    refresh();
}

void LayerPanel::updatePicture()
{
    // Each sound from 2 ms before the drum's start to 30 ms after, as it plays: a layer moved by its OFFSET (and the
    // pitch's speed), turned over by POLARITY; each scaled to its own peak (it's their shapes and timing that matter).
    const auto width = juce::jmax (1, picture.getWidth() - 20);
    juce::String key;
    key << shownKey << ":" << width;
    if (key == pictureKey)
        return;
    pictureKey = key;
    traces = {};
    const auto drum = processor.getDrumHitForAlign (slot);
    if (drum.samples.empty() || drum.sampleRate <= 0.0)
        return;
    float peak = 0.0f;
    for (const auto v : drum.samples)
        peak = juce::jmax (peak, std::abs (v));
    auto onset = 0;
    while (onset < (int) drum.samples.size() && std::abs (drum.samples[(size_t) onset]) < 0.1f * peak)
        ++onset;
    while (onset > 0 && std::abs (drum.samples[(size_t) onset - 1]) > 0.02f * peak)
        --onset;
    const auto from = (double) onset / drum.sampleRate - 0.002, span = 0.032;
    const auto draw = [width, from, span] (Trace& trace, const ParagonX1AudioProcessor::ReferenceHit& hit, double delay, double speed, float sign)
    {
        if (hit.samples.empty() || hit.sampleRate <= 0.0)
            return;
        float top = 0.0f;
        for (const auto v : hit.samples)
            top = juce::jmax (top, std::abs (v));
        if (top <= 1.0e-9f)
            return;
        trace.present = true;
        trace.lo.assign ((size_t) width, 0.0f);
        trace.hi.assign ((size_t) width, 0.0f);
        for (int x = 0; x < width; ++x)
        {
            const auto t0 = from + span * x / width, t1 = from + span * (x + 1) / width;
            // Seconds into the hit: (time - its delay) x its speed.
            const auto i0 = (int) std::floor ((t0 - delay) * speed * hit.sampleRate), i1 = (int) std::ceil ((t1 - delay) * speed * hit.sampleRate);
            float lo = 0.0f, hi = 0.0f;
            auto any = false;
            for (int i = juce::jmax (0, i0); i <= i1 && i < (int) hit.samples.size(); ++i)
            {
                const auto v = sign * hit.samples[(size_t) i] / top;
                lo = any ? juce::jmin (lo, v) : v;
                hi = any ? juce::jmax (hi, v) : v;
                any = true;
            }
            trace.lo[(size_t) x] = lo;
            trace.hi[(size_t) x] = hi;
        }
    };
    draw (traces[0], drum, 0.0, 1.0, 1.0f);
    for (int n = 0; n < 3; ++n)
        if (processor.getLayer (slot, n) != nullptr)
        {
            const auto& t = processor.getLayerTweak (slot, n);
            // An earlier layer (negative OFFSET) is skipped into: the same as starting before the drum.
            draw (traces[(size_t) n + 1], processor.getLayerHitForAlign (slot, n), t.offsetMs * 0.001, std::exp2 (t.pitch / 12.0), t.invert ? -1.0f : 1.0f);
        }
}

void LayerPanel::paint (juce::Graphics& g)
{
    g.setColour (ParagonColours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (ParagonColours::panelEdge);
    for (int n = 0; n < 3; ++n)
    {
        const auto& c = columns[(size_t) n];
        g.drawRoundedRectangle (c.area.toFloat().reduced (0.5f), 5.0f, 1.0f);
        if (status[(size_t) n].isNotEmpty())
        {
            g.setColour (status[(size_t) n].startsWith ("CAN'T") ? ParagonColours::meterRed : ParagonColours::accent);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            auto left = c.area.reduced (10, 5);
            left = left.removeFromLeft (juce::jmin (330, left.getWidth() / 3));
            g.drawFittedText (status[(size_t) n], left.removeFromBottom (16), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (ParagonColours::panelEdge);
        }
    }
    // The picture.
    const auto box = picture.toFloat();
    g.setColour (juce::Colour (0xff09070a));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    g.drawText ("THE HITS LINED UP (FROM 2 MS BEFORE THE DRUM'S START, 30 MS): DRUM", picture.reduced (10, 4).removeFromTop (14),
                juce::Justification::centredLeft);
    const auto plot = picture.reduced (10, 4).withTrimmedTop (16);
    const auto mid = (float) plot.getCentreY(), half = (float) plot.getHeight() * 0.48f;
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine ((int) mid, (float) plot.getX(), (float) plot.getRight());
    // The drum's start (2 ms in), and each 5 ms.
    for (int ms = 0; ms <= 30; ms += 5)
    {
        const auto x = (float) plot.getX() + (float) plot.getWidth() * (float) (ms + 2) / 32.0f;
        g.setColour (ms == 0 ? ParagonColours::textDim : ParagonColours::panelEdge);
        g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto& t = traces[(size_t) i];
        if (! t.present)
            continue;
        const auto colour = i == 0 ? ParagonColours::pitch : layerColours[i - 1];
        juce::Path line;
        for (size_t x = 0; x < t.hi.size(); ++x)
        {
            const auto px = (float) plot.getX() + (float) x * (float) plot.getWidth() / (float) t.hi.size();
            const auto y = mid - 0.5f * (t.hi[x] + t.lo[x]) * half;
            if (x == 0)
                line.startNewSubPath (px, y);
            else
                line.lineTo (px, y);
        }
        g.setColour (colour.withAlpha (i == 0 ? 0.95f : 0.85f));
        g.strokePath (line, juce::PathStrokeType (i == 0 ? 1.8f : 1.4f));
    }
    // The key beside the title.
    auto keyArea = picture.reduced (10, 4).removeFromTop (14);
    keyArea.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (10.5f, juce::Font::bold),
                                                                      "THE HITS LINED UP (FROM 2 MS BEFORE THE DRUM'S START, 30 MS): DRUM") + 4);
    g.setColour (ParagonColours::pitch);
    g.fillRect (keyArea.removeFromLeft (14).withSizeKeepingCentre (14, 3));
    for (int n = 0; n < 3; ++n)
    {
        keyArea.removeFromLeft (10);
        g.setColour (ParagonColours::textDim);
        const auto label = "LAYER " + juce::String (n + 1);
        g.drawText (label, keyArea.removeFromLeft (50), juce::Justification::centredLeft);
        g.setColour (layerColours[n].withAlpha (traces[(size_t) n + 1].present ? 1.0f : 0.3f));
        g.fillRect (keyArea.removeFromLeft (14).withSizeKeepingCentre (14, 3));
    }
}

void LayerPanel::resized()
{
    // A row per layer: its name, buttons and status on the left, its seven controls across the rest; the picture under.
    auto area = getLocalBounds().reduced (8);
    const auto rowHeight = juce::jlimit (64, ParamControl::knobHeight, (area.getHeight() - 90) / 3);
    for (int n = 0; n < 3; ++n)
    {
        auto& c = columns[(size_t) n];
        c.area = area.removeFromTop (rowHeight);
        area.removeFromTop (6);
        auto r = c.area.reduced (10, 5);
        auto left = r.removeFromLeft (juce::jmin (330, r.getWidth() / 3));
        r.removeFromLeft (8);
        auto top = left.removeFromTop (18);
        c.title.setBounds (top.removeFromLeft (64));
        c.name.setBounds (top);
        left.removeFromTop (4);
        auto buttons = left.removeFromTop (24);
        const auto unit = (buttons.getWidth() - 18) / 9;   // AUTO-ALIGN the widest
        for (auto [b, share] : { std::pair<juce::TextButton*, int> { &c.load, 2 }, { &c.remove, 2 }, { &c.alignButton, 3 }, { &c.invert, 2 } })
        {
            b->setBounds (buttons.removeFromLeft (b == &c.invert ? buttons.getWidth() : unit * share));
            buttons.removeFromLeft (6);
        }
        // LEVEL, OUT (wider: a channel's name), then the five knobs, sharing the rest of the row.
        const auto cell = r.getWidth() * 2 / 15;
        c.level->setBounds (r.removeFromLeft (cell).reduced (3, 0));
        c.out->setBounds (r.removeFromLeft (r.getWidth() - 5 * cell).reduced (3, 0));
        for (int k = 0; k < numKnobs; ++k)
        {
            auto b = r.removeFromLeft (cell).reduced (3, 0);
            c.knobLabels[(size_t) k].setBounds (b.removeFromTop (14));
            b.removeFromTop (2);
            c.knobs[(size_t) k].setTextBoxStyle (juce::Slider::TextBoxBelow, false, b.getWidth(), 18);
            c.knobs[(size_t) k].setBounds (b);
        }
    }
    picture = area;
    pictureKey = {};
    updatePicture();
}
