#include "DrumPage.h"
#include "NoteNames.h"

#include "dsp/synth/DrumGizmo.h"
#include "dsp/synth/Instrument.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    using synth::drums::Model;
    constexpr double pictureRate = 24000.0;   // plenty for a picture, half the work
    constexpr double pictureMaxSeconds = 4.0;

    juce::String noteName (int note) { return notenames::name (note); }

    juce::String drumName (const synth::drums::SlotSettings& s, const ParagonX1AudioProcessor& p, int slot)
    {
        if (s.model != Model::sample)
            return synth::drums::modelNames[(int) s.model].toUpperCase();
        // THE SAMPLE ENGINE (0.19.0): an instrument by its name (or LOADING...).
        if (p.getInstrumentRef (slot).isNotEmpty())
        {
            const auto instrument = p.getDrumInstrument (slot);
            return instrument != nullptr ? instrument->name.toUpperCase() : juce::String ("LOADING...");
        }
        const auto sample = p.getDrumSample (slot);
        return sample != nullptr ? sample->name : juce::String ("SAMPLE (EMPTY)");
    }

    // DELAY to the left (6c): a green line where the sequencer's step lands in the hit, as on Avenger's editor.
    const juce::Colour anchorColour { 0xfff4eef1 };   // pearl white (the family's colours only), against the orange wave
    void drawAnchor (juce::Graphics& g, float x, juce::Rectangle<float> area, const synth::drums::SlotSettings& s)
    {
        g.setColour (anchorColour.withAlpha (0.12f));
        g.fillRect (area.withRight (x));
        g.setColour (anchorColour);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("ANCHOR  " + juce::String (s.anchorSeconds() * 1000.0, 0) + " ms", juce::Rectangle<float> (x + 4.0f, area.getY() + 2.0f, 140.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }
    // DELAY to the right: said in the top corner (the picture is the hit itself).
    void drawLate (juce::Graphics& g, juce::Rectangle<float> area, const synth::drums::SlotSettings& s)
    {
        if (s.delay <= 0.0f)
            return;
        g.setColour (anchorColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("PLAYS " + juce::String (s.delay * 1000.0f, 0) + " ms LATE", area.withHeight (14.0f).translated (0.0f, 2.0f),
                    juce::Justification::centredRight, false);
    }

    // The waveform mirrored about the middle, filled in the plugin's highlight.
    void drawPeaks (juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> area, bool bright)
    {
        if (peaks.empty())
            return;
        juce::Path shape;
        const auto centre = area.getCentreY(), half = area.getHeight() * 0.5f;
        const auto x = [&] (size_t i) { return area.getX() + area.getWidth() * (float) i / (float) std::max<size_t> (1, peaks.size() - 1); };
        shape.startNewSubPath (x (0), centre);
        for (size_t i = 0; i < peaks.size(); ++i)
            shape.lineTo (x (i), centre - half * juce::jmin (1.0f, peaks[i]));
        for (size_t i = peaks.size(); i-- > 0;)
            shape.lineTo (x (i), centre + half * juce::jmin (1.0f, peaks[i]));
        shape.closeSubPath();
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.setOpacity (bright ? 1.0f : 0.55f);
        g.fillPath (shape);
        g.setOpacity (1.0f);
    }
}

//==============================================================================
HitPicture HitPicture::make (const synth::drums::SlotSettings& settings, int columns)
{
    HitPicture picture;
    columns = juce::jmax (2, columns);
    const auto hit = synth::drums::Kit::renderHit (settings, 1.0f, pictureRate, pictureMaxSeconds);
    const auto* left = hit.getReadPointer (0);
    const auto* right = hit.getReadPointer (1);
    auto peak = 0.0f;
    for (int i = 0; i < hit.getNumSamples(); ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    if (peak <= 0.0f)
        return picture;
    auto last = 0;
    for (int i = hit.getNumSamples(); --i > 0;)
        if (juce::jmax (std::abs (left[i]), std::abs (right[i])) > peak * 0.001f)   // -60 dB under its peak
        {
            last = i;
            break;
        }
    last = juce::jmax (last, (int) (0.05 * pictureRate));
    // A drum synth with a drawn envelope: the picture spans what the envelope does (the hit's own length), so the
    // two line up.
    if (settings.model != Model::sample && synth::drums::envelopeShapes (settings.envelope) && settings.envelopeSeconds > 0.0f)
        last = juce::jmin (hit.getNumSamples() - 1, (int) (settings.envelopeSeconds * pictureRate));
    picture.seconds = last / pictureRate;
    picture.peaks.assign ((size_t) columns, 0.0f);
    for (int i = 0; i <= last; ++i)
    {
        auto& column = picture.peaks[(size_t) juce::jmin (columns - 1, i * columns / (last + 1))];
        column = juce::jmax (column, std::abs (left[i]), std::abs (right[i]));
    }
    return picture;
}

//==============================================================================
DrumRow::DrumRow (ParagonX1AudioProcessor& p, int s)
    : processor (p), slot (s),
      mute (p.getParameters(), ParamIDs::drum (s, "Mute"), "M", "MUTE: this drum silent."),
      solo (p.getParameters(), ParamIDs::drum (s, "Solo"), "S", "SOLO: only the soloed drums play.")
{
    for (auto* c : { &mute, &solo })
    {
        c->setCompact (true);
        addAndMakeVisible (*c);
    }
    setTooltip ("Click to choose this drum (and hear it). Drop an audio file here to load it into this slot.");
}

void DrumRow::setSelected (bool s)
{
    if (selected != s)
    {
        selected = s;
        repaint();
    }
}

void DrumRow::setPicture (HitPicture p)
{
    picture = std::move (p);
    repaint();
}

void DrumRow::setLit (float amount)
{
    if (std::abs (amount - lit) > 0.02f || (amount <= 0.0f) != (lit <= 0.0f))
    {
        lit = amount;
        repaint();
    }
}

void DrumRow::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (selected ? juce::Colour (0xff1b1418) : chosen ? juce::Colour (0xff17111a) : juce::Colour (0xff0f0c10));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (selected ? ParagonColours::accent : chosen ? ParagonColours::accent.withAlpha (0.55f) : ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, selected || chosen ? 1.4f : 1.0f);

    // The hit light.
    const auto led = juce::Rectangle<float> (10.0f, bounds.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (juce::Colour (0xff2a2226));
    g.fillEllipse (led);
    if (lit > 0.0f)
    {
        g.setColour (ParagonColours::glow.withAlpha (lit));
        g.fillEllipse (led);
        g.setColour (ParagonColours::glow.withAlpha (0.35f * lit));
        g.fillEllipse (led.expanded (4.0f));
    }

    const auto kit = processor.getKitSettings();
    const auto& s = kit.slots[(size_t) slot];
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (ParagonColours::textDim);
    const auto home = processor.homeNote (slot);   // its note in the map (none: a dash)
    g.drawText (home >= 0 ? noteName (home) : juce::String ("-"), juce::Rectangle<int> (28, 0, 36, getHeight()), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (selected ? ParagonColours::text : ParagonColours::text.withAlpha (0.85f));
    g.drawText (synth::drums::slotDefaults[(size_t) slot].key, juce::Rectangle<int> (66, 0, 44, getHeight()), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (s.model == Model::sample && s.sample == nullptr ? ParagonColours::textDim : ParagonColours::accent);
    g.drawFittedText (drumName (s, processor, slot), juce::Rectangle<int> (112, 0, 112, getHeight()), juce::Justification::centredLeft, 1, 0.8f);

    // The hit's picture: its first second (longer hits run off the end).
    auto thumb = juce::Rectangle<float> (230.0f, 6.0f, (float) getWidth() - 230.0f - 96.0f, (float) getHeight() - 12.0f);
    g.setColour (juce::Colour (0xff09070a));
    g.fillRoundedRectangle (thumb, 3.0f);
    if (! picture.peaks.empty())
    {
        const auto shown = juce::jmin (1.0, picture.seconds);
        const auto columns = (size_t) juce::jmax (2.0, picture.peaks.size() * shown / picture.seconds);
        const std::vector<float> first (picture.peaks.begin(), picture.peaks.begin() + (std::ptrdiff_t) juce::jmin (columns, picture.peaks.size()));
        drawPeaks (g, first, thumb.reduced (2.0f, 1.0f).withWidth ((thumb.getWidth() - 4.0f) * (float) (shown / 1.0)), ! s.mute);
        // Where its last hit is now (6c part 2), while it's within the picture.
        if (playhead >= 0.0f && playhead < (float) shown)
        {
            g.setColour (ParagonColours::text.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (thumb.getX() + 2.0f + (thumb.getWidth() - 4.0f) * playhead - 0.75f, thumb.getY() + 1.0f, 1.5f,
                                                thumb.getHeight() - 2.0f));
        }
    }
}

void DrumRow::setChosen (bool c)
{
    if (c != chosen)
    {
        chosen = c;
        repaint();
    }
}

void DrumRow::setPlayhead (float seconds)
{
    if (std::abs (seconds - playhead) > 0.002f)
    {
        playhead = seconds;
        repaint();
    }
}

void DrumRow::resized()
{
    // (0.36.0: 36 rows make them short: the M and S buttons keep their height.)
    auto area = getLocalBounds().reduced (4, getHeight() >= 22 ? 4 : 1);
    solo.setBounds (area.removeFromRight (44));
    mute.setBounds (area.removeFromRight (44));
}

void DrumRow::mouseDown (const juce::MouseEvent& e)
{
    if (onSelect)
        onSelect (slot, e.mods);
    processor.previewDrum (slot);
}

bool DrumRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && DrumPage::isAudioFile (files[0]);
}

void DrumRow::filesDropped (const juce::StringArray& files, int, int)
{
    if (processor.loadDrumSample (slot, juce::File (files[0])))
    {
        if (onSelect)
            onSelect (slot, {});
        processor.previewDrum (slot);
    }
}

//==============================================================================
DrumHitDisplay::DrumHitDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("The chosen drum's hit, from a copy of it at these settings (both sides, peak level). Drop an audio file "
                "here to load it into this slot.");
}

void DrumHitDisplay::setSlot (int s)
{
    slot = s;
    repaint();
}

void DrumHitDisplay::setPicture (HitPicture p)
{
    picture = std::move (p);
    repaint();
}

juce::Rectangle<float> DrumHitDisplay::waveArea() const
{
    auto area = getLocalBounds().toFloat().reduced (20.0f, 16.0f);
    area.removeFromTop (18.0f);
    area.removeFromBottom (20.0f);
    return area.withTrimmedTop (16.0f);   // room for the hits' numbers
}

bool DrumHitDisplay::showsFile() const
{
    const auto kit = processor.getKitSettings();
    return kit.slots[(size_t) slot].model == Model::sample && processor.getDrumSample (slot) != nullptr;
}

void DrumHitDisplay::updateFile()
{
    const auto sample = processor.getDrumSample (slot);
    const auto columns = juce::jmax (2, (int) waveArea().getWidth());
    if (sample == file && (int) filePeaks.size() == columns)
        return;
    file = sample;
    filePeaks.assign ((size_t) columns, 0.0f);
    hits.clear();
    if (file == nullptr || file->length() < 2)
        return;
    const auto length = file->length();
    for (const auto& channel : file->channels)
        for (int i = 0; i < length; ++i)
        {
            auto& column = filePeaks[(size_t) juce::jmin (columns - 1, (int) ((juce::int64) i * columns / length))];
            column = juce::jmax (column, std::abs (channel[(size_t) i]));
        }
    auto peak = *std::max_element (filePeaks.begin(), filePeaks.end());
    if (peak > 0.0f)
        for (auto& p : filePeaks)
            p /= peak;
    hits = synth::drums::sampleFiles::findHits (*file);
}

const std::vector<int>& DrumHitDisplay::getHits()
{
    updateFile();
    return hits;
}

float DrumHitDisplay::share (const char* what) const
{
    const auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, what));
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : 0.0f;
}

void DrumHitDisplay::setShare (const char* what, float value, bool gesture)
{
    if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, what)))
    {
        if (gesture)
            parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0.0f, 1.0f, value)));
        if (gesture)
            parameter->endChangeGesture();
    }
    repaint();
}

void DrumHitDisplay::pickHit (int index)
{
    updateFile();
    if (file == nullptr || index < 0 || index >= (int) hits.size())
        return;
    // From this hit's start to just before the next one's (1 ms short), or to the end of the file.
    const auto length = (float) file->length();
    const auto next = index + 1 < (int) hits.size() ? (float) hits[(size_t) index + 1] - (float) (0.001 * file->sampleRate) : length;
    setShare ("SampleStart", (float) hits[(size_t) index] / length, true);
    setShare ("SampleEnd", juce::jmax ((float) hits[(size_t) index] + 1.0f, next) / length, true);
    processor.previewDrum (slot);
}

void DrumHitDisplay::playWholeFile()
{
    setShare ("SampleStart", 0.0f, true);
    setShare ("SampleEnd", 1.0f, true);
    processor.previewDrum (slot);
}

DrumHitDisplay::Handle DrumHitDisplay::handleAt (float x) const
{
    const auto area = waveArea();
    const auto startX = area.getX() + area.getWidth() * share ("SampleStart");
    const auto endX = area.getX() + area.getWidth() * share ("SampleEnd");
    if (std::abs (x - endX) <= 7.0f && std::abs (x - endX) <= std::abs (x - startX))
        return Handle::end;
    if (std::abs (x - startX) <= 7.0f)
        return Handle::start;
    return Handle::none;
}

void DrumHitDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (showsFile() && handleAt ((float) e.x) != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                                                                          : juce::MouseCursor::NormalCursor);
}

void DrumHitDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! showsFile())
        return;
    updateFile();
    dragging = handleAt ((float) e.x);
    if (dragging != Handle::none)
    {
        if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, dragging == Handle::start ? "SampleStart" : "SampleEnd")))
            parameter->beginChangeGesture();
        return;
    }
    // A click elsewhere chooses the hit under it (the last one starting at or before it).
    const auto area = waveArea();
    const auto at = (double) (e.x - area.getX()) / area.getWidth() * file->length();
    auto index = 0;
    for (int i = 0; i < (int) hits.size(); ++i)
        if (hits[(size_t) i] <= at)
            index = i;
    pickHit (index);
}

void DrumHitDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;
    const auto area = waveArea();
    const auto value = juce::jlimit (0.0f, 1.0f, ((float) e.x - area.getX()) / area.getWidth());
    const auto least = 0.002f;   // START stays before END
    if (dragging == Handle::start)
        setShare ("SampleStart", juce::jmin (value, share ("SampleEnd") - least), false);
    else
        setShare ("SampleEnd", juce::jmax (value, share ("SampleStart") + least), false);
}

void DrumHitDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::none)
        return;
    if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, dragging == Handle::start ? "SampleStart" : "SampleEnd")))
        parameter->endChangeGesture();
    dragging = Handle::none;
    processor.previewDrum (slot);
}

void DrumHitDisplay::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    auto area = getLocalBounds().toFloat().reduced (20.0f, 16.0f);
    const auto kit = processor.getKitSettings();
    const auto& s = kit.slots[(size_t) slot];

    // THE SAMPLE ENGINE (0.19.0): an instrument's layers across the velocities, each as tall as it's loud, its hits
    // in it, the last one played lit.
    if (const auto ref = processor.getInstrumentRef (slot); ref.isNotEmpty())
    {
        const auto instrument = processor.getDrumInstrument (slot);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (ParagonColours::accent);
        if (instrument == nullptr)
        {
            const auto problems = processor.getInstrumentProblems();
            g.drawText (processor.isLoadingInstruments() ? "LOADING..." : "COULDN'T LOAD IT", area.removeFromTop (18.0f), juce::Justification::topLeft, false);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (ParagonColours::textDim);
            g.drawFittedText (ref.fromLastOccurrenceOf ("|", false, false) + (problems.isEmpty() ? juce::String() : "\n" + problems.joinIntoString ("\n")),
                              area.toNearestInt(), juce::Justification::topLeft, 6);
            return;
        }
        auto title = instrument->name + "   " + juce::String ((int) instrument->layers.size()) + " LAYERS, " + juce::String (instrument->numHits()) + " HITS";
        if (s.layerLimit > 0)
            title << "   (USING UP TO " << s.layerLimit << " A LAYER)";
        auto titleRow = area.removeFromTop (18.0f);
        g.drawText (title, titleRow, juce::Justification::topLeft, false);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        g.drawText ("LAYERS BY VELOCITY, EACH AS TALL AS IT'S LOUD", titleRow, juce::Justification::topRight, false);
        area.removeFromTop (8.0f);
        auto axis = area.removeFromBottom (16.0f);
        const auto xOf = [&area] (int velocity) { return area.getX() + area.getWidth() * (float) velocity / 128.0f; };
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (ParagonColours::textDim);
        for (const auto v : { 1, 32, 64, 96, 127 })
            g.drawText (juce::String (v), juce::Rectangle<float> (xOf (v) - 15.0f, axis.getY() + 2.0f, 30.0f, 12.0f), juce::Justification::centred, false);
        auto loudest = 1.0e-6f;
        for (const auto& l : instrument->layers)
            loudest = juce::jmax (loudest, l.level);
        const auto last = processor.getLastHit (slot);
        for (int i = 0; i < (int) instrument->layers.size(); ++i)
        {
            const auto& l = instrument->layers[(size_t) i];
            // Height: its loudness against the loudest, over 48 dB.
            const auto db = juce::Decibels::gainToDecibels (l.level / loudest, -48.0f);
            const auto h = area.getHeight() * juce::jlimit (0.06f, 1.0f, 1.0f + db / 48.0f);
            const juce::Rectangle<float> column (xOf (l.lo) + 1.0f, area.getBottom() - h, juce::jmax (2.0f, xOf (l.hi + 1) - xOf (l.lo) - 2.0f), h);
            const auto used = s.layerLimit > 0 ? juce::jmin (s.layerLimit, (int) l.hits.size()) : (int) l.hits.size();
            const auto rows = juce::jmax (1, (int) l.hits.size());
            for (int hIndex = 0; hIndex < rows; ++hIndex)
            {
                const auto cell = column.withHeight (column.getHeight() / (float) rows).translated (0.0f, column.getHeight() * (float) hIndex / (float) rows).reduced (0.0f, 1.0f);
                const auto isLast = last.first == i && last.second == hIndex;
                g.setColour (isLast ? ParagonColours::text : ParagonColours::accent.withAlpha (hIndex < used ? 0.75f : 0.18f));
                g.fillRect (cell);
            }
            if (column.getWidth() > 26.0f)
            {
                g.setColour (ParagonColours::background.withAlpha (0.85f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (juce::String ((int) l.hits.size()), column.withHeight (14.0f), juce::Justification::centred, false);
            }
        }
        return;
    }

    if (showsFile())
    {
        // The whole file: the part played bright, the rest dim; its hits numbered along the top.
        updateFile();
        const auto wave = waveArea();
        const auto seconds = file->length() / file->sampleRate;
        const auto startShare = juce::jlimit (0.0f, 1.0f, s.start), endShare = juce::jlimit (startShare, 1.0f, s.end);
        const auto startX = wave.getX() + wave.getWidth() * startShare, endX = wave.getX() + wave.getWidth() * endShare;
        auto chosen = -1;
        for (int i = 0; i < (int) hits.size(); ++i)
            if (std::abs ((float) hits[(size_t) i] / (float) file->length() - startShare) < 0.0005f)
                chosen = i;

        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (ParagonColours::accent);
        auto title = file->name + "   " + juce::String ((endShare - startShare) * seconds, 2) + " s of " + juce::String (seconds, 1) + " s";
        if (chosen >= 0)
            title << "   (hit " << (chosen + 1) << " of " << (int) hits.size() << ")";
        g.drawText (title, area.removeFromTop (18.0f), juce::Justification::topLeft, false);

        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (wave.getCentreY()), wave.getX(), wave.getRight());
        drawPeaks (g, filePeaks, wave, false);
        g.setColour (juce::Colour (0xff060407).withAlpha (0.55f));   // the part not played, dimmed further
        g.fillRect (wave.withRight (startX));
        g.fillRect (wave.withLeft (endX));
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (wave.withLeft (startX).withRight (endX).toNearestInt());
            drawPeaks (g, filePeaks, wave, ! s.mute);
        }

        // The hits' numbers.
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (int i = 0; i < (int) hits.size(); ++i)
        {
            const auto x = wave.getX() + wave.getWidth() * (float) hits[(size_t) i] / (float) file->length();
            g.setColour (i == chosen ? ParagonColours::accent : ParagonColours::textDim);
            g.drawVerticalLine (juce::roundToInt (x), wave.getY() - 14.0f, wave.getY() - 2.0f);
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x + 2.0f, wave.getY() - 16.0f, 24.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
        // The ANCHOR (DELAY to the left): that far into the part played lands on the sequencer's step.
        if (s.delay < 0.0f)
        {
            const auto anchorShare = startShare + (float) (s.anchorSeconds() * file->sampleRate * std::exp2 (s.pitch / 12.0) / file->length());
            if (anchorShare <= endShare)
                drawAnchor (g, wave.getX() + wave.getWidth() * anchorShare, wave, s);
        }
        drawLate (g, wave, s);
        // The envelope (6c) over the part it shapes.
        if (const auto& shape = processor.getModMatrix().drumEnvelopes[(size_t) slot]; synth::drums::envelopeShapes (s.envelope))
        {
            const auto curve = shape.toShape();
            juce::Path path;
            const auto width = juce::jmax (2.0f, endX - startX);
            for (int i = 0; i <= (int) width; ++i)
            {
                const juce::Point<float> p (startX + (float) i, wave.getBottom() - wave.getHeight() * curve.valueAt ((float) i / width));
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            g.setColour (ParagonColours::text.withAlpha (0.8f));
            g.strokePath (path, juce::PathStrokeType (1.5f));
        }
        // START and END.
        for (const auto& [x, label] : { std::pair { startX, "S" }, std::pair { endX, "E" } })
        {
            g.setColour (ParagonColours::text);
            g.drawLine (x, wave.getY(), x, wave.getBottom(), 1.5f);
            const auto flag = juce::Rectangle<float> (label[0] == 'S' ? x : x - 14.0f, wave.getBottom() - 16.0f, 14.0f, 16.0f);
            g.fillRect (flag);
            g.setColour (juce::Colours::black);
            g.drawText (label, flag, juce::Justification::centred, false);
        }
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        const auto hint = hits.size() > 1 ? "Click a hit to play just that one; drag S and E to fine-tune"
                                          : "Drag S and E to choose the part played";
        g.drawText (hint, getLocalBounds().toFloat().reduced (20.0f, 14.0f).removeFromBottom (14.0f), juce::Justification::centredRight, false);
        return;
    }

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);
    auto title = drumName (s, processor, slot);
    if (! picture.peaks.empty())
        title << "   " << juce::String (picture.seconds, picture.seconds < 1.0 ? 2 : 1) << " s";
    g.drawText (title, area.removeFromTop (18.0f), juce::Justification::topLeft, false);
    auto axis = area.removeFromBottom (16.0f);
    area.removeFromBottom (4.0f);

    if (picture.peaks.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (s.model == Model::sample ? "No sample yet: LOAD SAMPLE, or drop an audio file here" : "Silent at these settings",
                    area, juce::Justification::centred, false);
        return;
    }

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    drawPeaks (g, picture.peaks, area, ! s.mute);
    if (s.delay < 0.0f && s.anchorSeconds() <= picture.seconds)
        drawAnchor (g, area.getX() + area.getWidth() * (float) (s.anchorSeconds() / picture.seconds), area, s);
    drawLate (g, area, s);

    // Time marks: 10, 4 or so along, at a round step.
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (ParagonColours::textDim);
    double step = 0.01;
    for (const auto candidate : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1.0 })
        if (picture.seconds / candidate <= 10.0)
        {
            step = candidate;
            break;
        }
    for (double t = step; t < picture.seconds - step * 0.25; t += step)
    {
        const auto x = area.getX() + area.getWidth() * (float) (t / picture.seconds);
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim);
        const auto label = step < 0.1 ? juce::String (juce::roundToInt (t * 1000.0)) + " ms" : juce::String (t, step < 1.0 ? 2 : 0) + " s";
        g.drawText (label, juce::Rectangle<float> (x - 30.0f, axis.getY(), 60.0f, axis.getHeight()), juce::Justification::centred, false);
    }
}

bool DrumHitDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && DrumPage::isAudioFile (files[0]);
}

void DrumHitDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    if (processor.loadDrumSample (slot, juce::File (files[0])))
        processor.previewDrum (slot);
}

//==============================================================================
DrumPage::DrumPage (ParagonX1AudioProcessor& p) : processor (p), display (p), envelope (p), sequencer (p), kitView (p), articulations (p)
{
    auto& state = p.getParameters();
    const auto kitControl = [&] (const char* id, const juce::String& label, const juce::String& tip)
    {
        kitControls.push_back (std::make_unique<ParamControl> (state, id, label, tip));
    };
    kitControl (ParamIDs::drumsRoot, "ROOT", "With the ROOT note map (SETTINGS): the kit's lowest key (its kick); the 19 keys above it play "
                                             "the rest, the first 12 C to B as on General MIDI kits.");
    kitControl (ParamIDs::drumsLevel, "LEVEL", "The whole kit's level.");
    kitControl (ParamIDs::drumsVelocity, "VELOCITY", "How much the keys' velocity changes the level: 0 every hit the same, 100% a soft hit "
                                                     "(half velocity) 12 dB down.");

    const auto slotControl = [&] (auto& list, const char* what, const juce::String& label, const juce::String& tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, ParamIDs::drum (0, what), label, tip));
    };
    slotControl (slotControls, "Model", "DRUM", "What this slot is: one of our own drum synths, or SAMPLE (a file you load). Our own "
                                                "designs from the classic analog ideas, not copies of any drum machine.");
    slotControl (slotControls, "Pitch", "PITCH", "Its pitch, in semitones (and hundredths).");
    slotControl (slotControls, "Decay", "DECAY", "How long it rings. SAMPLE: the whole sample from the middle up; below, it's cut shorter. "
                                                 "ACOUSTIC drums: the muffling (left: a pillow or a towel on the head, the high overtones dying first).");
    slotControl (slotControls, "Tone", "TONE", "Its brightness. KICK and TOM: how far the pitch drops at the hit. RIM: high ring against low. "
                                               "SAMPLE: darker below the middle, thinner above. ACOUSTIC drums: what strikes the head (BEATER / STICK): soft felt "
                                               "(dark, round) to hard plastic or wood (bright, clicky).");
    slotControl (slotControls, "Snap", "SNAP", "Its attack: the kick's beater click, the snare's wires, the clap's slaps, the hats' sizzle, "
                                               "the cowbell's strike. SAMPLE: a softer attack below the middle, a spike above. ACOUSTIC drums: the kick's "
                                               "beater CLICK, the snare's WIRES, the tom's stick ATTACK.");
    slotControl (slotControls, "Level", "VOLUME", "This drum's level.");
    slotControl (slotControls, "Pan", "PAN", "Left to right.");
    slotControl (slotControls, "Gain", "GAIN", "More level than VOLUME gives (or less): for samples recorded very loud or very quiet.");
    slotControl (routingControls, "Choke", "CHOKE", "A hit stops the other drums in its group: the closed and pedal hats stop the open hat (group 1).");
    slotControl (routingControls, "Insert", "INSERT", "NONE: straight into the mix, through the sends. A to D: through that insert bus "
                                                      "(its DRIVE, GLITCH, MOD, LO-FI), whose own sends then feed DELAY and REVERB.");
    slotControl (routingControls, "Output", "OUTPUT", "Which of the plugin's outputs it plays into: MAIN (with the FX page's MASTER "
                                                      "effects and the LIMITER), or OUT 2 to 16, to mix it on its own channel in your DAW "
                                                      "(switch that output on there first; until then it plays in MAIN). On an insert bus: "
                                                      "the bus's OUTPUT instead. Several drums chosen: all of them.");
    slotControl (routingControls, "SendDelay", "TO DELAY", "How much goes to DELAY (while DELAY is on, and the drum isn't on a bus).");
    slotControl (routingControls, "SendReverb", "TO REVERB", "How much goes to REVERB (while REVERB is on, and the drum isn't on a bus).");
    slotControl (routingControls, "Mono", "MONO", "On: a new hit cuts this drum's last one short (a quick fade). Off (POLY): fast hits, "
                                                  "like a snare roll, ring on over each other.");
    slotControl (routingControls, "Delay", "DELAY", "Right: the hit comes later. Left: the ANCHOR, the green line: the sequencer starts the "
                                                    "hit that much early, so that point in it lands on the step (a clap's main slap after its "
                                                    "flams, the peak of a swoosh). Keys you play still start it at once.");
    slotControl (routingControls, "LowPass", "LP", "Low-pass, 6 dB an octave: darkens the drum (damping a kick, softening hats). "
                                                   "All the way up: off.");
    slotControl (routingControls, "HighPass", "HP", "High-pass, 6 dB an octave: thins the drum (hats out of the kick's way). "
                                                    "All the way down: off.");

    // THE ACOUSTIC MODELS (0.24.0): shown beside the shaping knobs while the drum is one of them.
    slotControl (acousticControls, "Position", "POSITION", "Where the stick or beater lands: the centre (left: fat, round, fewer overtones) "
                                                           "to near the edge (right: thinner, ringier, more overtones). CYMBALS: the bell "
                                                           "(left: its ding), the bow (a defined ping) or the edge (right: the most wash).");
    slotControl (acousticControls, "Shell", "SHELL", "The kick: its FRONT HEAD (more: a longer, rounder boom; less: like a port cut in it). "
                                                     "Snare and toms: the shell's own ring. CYMBALS: the WASH they bloom into. HI-HAT: "
                                                     "how OPEN it is, from tight and closed to fully open.");
    slotControl (acousticControls, "Variation", "VARIATION", "How much each hit differs from the last, as a drummer's do: where it lands, how "
                                                             "hard, the contact, the tuning. 0: every hit the same.");

    // THE VELOCITY GATE (0.18.0), beside the curve.
    slotControl (velocityControls, "VelMin", "GATE LOW", "The softest hit (as it comes in, 0 to 127) that plays this drum: softer ones don't. "
                                                         "Raise it to keep a layer for hard hits only.");
    slotControl (velocityControls, "VelMax", "GATE HIGH", "The hardest hit (as it comes in) that plays this drum: harder ones don't.");
    // THE SAMPLE ENGINE (0.19.0): an instrument's HIT VARIATION and limits.
    slotControl (instrumentControls, "Randomize", "RANDOMIZE", "On: each hit picked at random from its layer, never the one just played. "
                                                               "Off: the layer's hits in turn.");
    slotControl (instrumentControls, "Adjacent", "ADJACENT", "ADJACENT LAYERS: a velocity near the top or bottom of its layer sometimes takes "
                                                             "a hit from the layer next to it (its level matched): more hits to choose from.");
    slotControl (instrumentControls, "VelToVol", "VEL > VOL", "VELOCITY TO VOLUME: a softer note within a layer also plays a little quieter. "
                                                              "Off: each hit as loud as it was recorded.");
    slotControl (instrumentControls, "LayerLimit", "LAYER LIMIT", "How many of each layer's hits are used (ALL: every one). Fewer: more repeats.");
    slotControl (instrumentControls, "Voices", "VOICES", "How many of this drum's hits ring at once; past that the oldest fades.");

    // ITS LAYER (0.29.0).
    slotControl (layerControls, "LayerLevel", "LAYER", "The level of this drum's LAYER: your own sample played with every hit "
                                                       "(LOAD... > A LAYER...). OFF: not played.");
    slotControl (layerControls, "LayerOut", "LAYER OUT", "Where the layer plays: WITH THE DRUM (its VOLUME, PAN, INSERT and OUTPUT), or "
                                                         "a mixer channel, with its own fader, effects and sends (a kit through the "
                                                         "mixer gives it a USER channel of its own when it's loaded).");
    for (auto* list : { &kitControls, &slotControls, &routingControls, &velocityControls, &instrumentControls, &acousticControls, &layerControls })
        for (auto& c : *list)
            addAndMakeVisible (*c);

    // THE VELOCITY CURVE (0.18.0): ENVELOPE / VELOCITY over the strip.
    for (auto* b : { &envelopeTab, &velocityTab })
    {
        DrumSeqPanel::style (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (964);
        addAndMakeVisible (*b);
    }
    envelopeTab.setToggleState (true, juce::dontSendNotification);
    envelopeTab.setTooltip ("The drum's ENVELOPE and its hit.");
    velocityTab.setTooltip ("The drum's VELOCITY CURVE (how hard each hit plays, whatever plays it: grooves, the song, the sequencer, "
                            "your keys) and its VELOCITY GATE. Several drums chosen: all of them.");
    envelopeTab.onClick = [this] { showVelocity (false); };
    velocityTab.onClick = [this] { showVelocity (true); };
    velocityEditor.getCurve = [this] { return processor.getModMatrix().drumVelocityCurves[(size_t) selected]; };
    velocityEditor.onChange = [this] (const synth::drums::VelocityCurve& c, bool final) { setVelocityCurve (c, final); };
    addChildComponent (velocityEditor);
    for (auto* b : { &softButton, &gateButton })
    {
        DrumSeqPanel::style (*b);
        b->setClickingTogglesState (true);
        addChildComponent (*b);
    }
    softButton.setTooltip ("SOFT: the curve rounded between its points, no corners.");
    gateButton.setTooltip ("GATE: hits softer than the left end point don't play at all (off: they play at its height).");
    softButton.onClick = [this]
    {
        auto c = processor.getModMatrix().drumVelocityCurves[(size_t) selected];
        c.soft = softButton.getToggleState();
        setVelocityCurve (c, true);
    };
    gateButton.onClick = [this]
    {
        auto c = processor.getModMatrix().drumVelocityCurves[(size_t) selected];
        c.gate = gateButton.getToggleState();
        setVelocityCurve (c, true);
    };
    DrumSeqPanel::style (resetCurve);
    resetCurve.setTooltip ("Back to a straight line: every hit as hard as it comes.");
    resetCurve.onClick = [this] { setVelocityCurve ({}, true); };
    addChildComponent (resetCurve);

    for (int k = 0; k < synth::drums::numSlots; ++k)
    {
        rows.push_back (std::make_unique<DrumRow> (p, k));
        rows.back()->onSelect = [this] (int slot, juce::ModifierKeys mods) { clickRow (slot, mods); };
        addAndMakeVisible (*rows.back());
    }
    addAndMakeVisible (display);
    envelope.setTarget (MultiPointEditor::Kind::drumEnvelope, 0);
    envelope.caption = "ENVELOPE: ITS LEVEL ACROSS THE HIT";
    envelope.setTooltip ("ENVELOPE: the drum's level across its hit, start to end (a sample: from START to END). Drag a point; drag a "
                         "line up or down to curve it; double-click to add a point (or, on one, to delete it). Right-click: flat, back "
                         "to front, upside down, fades, a gate, a swell. Flat at the top changes nothing.");
    addAndMakeVisible (envelope);

    play.setTooltip ("Hear the chosen drum.");
    play.onClick = [this] { processor.previewDrum (selected); };
    load.setButtonText ("LOAD...");
    load.setTooltip ("Load into the chosen slot: one audio file (kept with the preset); a folder of hits (velocity layers and "
                     "round-robins, sorted by loudness); or an SFZ instrument. Folders and SFZ files stay where they are: the "
                     "preset remembers where.");
    load.onClick = [this] { showLoadMenu(); };
    wholeFile.setTooltip ("Play the whole file again (after choosing one hit out of it).");
    wholeFile.onClick = [this] { display.playWholeFile(); };
    addChildComponent (wholeFile);
    clear.setTooltip ("Take the chosen slot's sample out.");
    clear.onClick = [this] { processor.clearDrumSample (selected); };
    for (auto* b : { &play, &load, &clear })
        addAndMakeVisible (*b);

    // < > (6c part 2): the next or previous audio file in the sample's folder.
    previous.setTooltip ("The previous audio file in this sample's folder (by name): audition a folder of kicks one by one.");
    next.setTooltip ("The next audio file in this sample's folder (by name).");
    previous.onClick = [this] { processor.loadNeighbourSample (selected, -1); refreshPictures(); repaint(); };
    next.onClick = [this] { processor.loadNeighbourSample (selected, 1); refreshPictures(); repaint(); };
    addChildComponent (previous);
    addChildComponent (next);

    // KITS and SEQUENCES (6c part 2), and LINK.
    for (auto* b : { &kitsButton, &sequencesButton, &linkButton })
    {
        DrumSeqPanel::style (*b);
        addAndMakeVisible (*b);
    }
    kitsButton.setTooltip ("Save the kit (every drum's settings, samples and envelope) or load one from the presets folder's Drum Kits.");
    sequencesButton.setTooltip ("Save the drum sequencer's four patterns (and its SPEED, TIMING, LENGTH, VEL AMOUNT) or load some.");
    linkButton.setTooltip ("LINK: loading or saving a kit takes the sequence of the same name with it, and the other way round.");
    linkButton.setClickingTogglesState (true);
    linkButton.setToggleState (processor.drumLink.load(), juce::dontSendNotification);
    linkButton.onClick = [this] { processor.drumLink = linkButton.getToggleState(); };
    kitsButton.onClick = [this] { showKitMenu (false); };
    sequencesButton.onClick = [this] { showKitMenu (true); };

    addChildComponent (sequencer);
    sequencer.getGrid().onRowTouched = [this] (int row) { select (row); };
    addChildComponent (kitView);
    kitView.onChoose = [this] (int slot, juce::ModifierKeys mods) { clickRow (slot, mods); };
    addChildComponent (articulations);
    articulations.onChoose = [this] (int slot) { clickRow (slot, {}); };
    articulations.onChooseAll = [this] (const std::vector<int>& slots) { chooseSlots (slots); };
    for (auto* tab : { &kitTab, &drumTab, &sequencerTab })
    {
        DrumSeqPanel::style (*tab);
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (963);
        addAndMakeVisible (*tab);
    }
    kitTab.setTooltip ("The KIT VIEW: the kit from the drummer's seat. Click a drum to choose it and hear it; each glows as it plays.");
    drumTab.setTooltip ("The chosen drum's sound, its sample and where it goes.");
    sequencerTab.setTooltip ("The DRUM SEQUENCER: beats for the kit, played with your song or from keys.");
    kitTab.onClick = [this] { showView (View::kit); };
    drumTab.onClick = [this] { showView (View::drum); };
    sequencerTab.onClick = [this] { showView (View::sequencer); };

    select (0);
    showView (View::kit);
    startTimerHz (30);
}

void DrumPage::showView (View v)
{
    view = v;
    const auto drum = v == View::drum;
    for (auto* list : { &slotControls, &routingControls })
        for (auto& c : *list)
            c->setVisible (drum);
    for (juce::Component* c : { (juce::Component*) &play, (juce::Component*) &load, (juce::Component*) &clear,
                                (juce::Component*) &envelopeTab, (juce::Component*) &velocityTab })
        c->setVisible (drum);
    updateDrumPanels();
    wholeFile.setVisible (drum && display.showsFile());
    refreshPictures();
    sequencer.setVisible (v == View::sequencer);
    kitView.setVisible (v == View::kit);
    articulations.setVisible (v == View::kit);
    (v == View::kit ? kitTab : drum ? drumTab : sequencerTab).setToggleState (true, juce::dontSendNotification);
    kitTab.toFront (false);
    drumTab.toFront (false);
    sequencerTab.toFront (false);
    repaint();
}

DrumPage::~DrumPage()
{
    for (auto* p : listened)
        p->removeListener (this);
}

void DrumPage::clickRow (int slot, juce::ModifierKeys mods)
{
    slot = juce::jlimit (0, synth::drums::numSlots - 1, slot);
    if (mods.isShiftDown())
    {
        for (auto& row : rows)
            row->setChosen (row->getSlot() >= juce::jmin (anchor, slot) && row->getSlot() <= juce::jmax (anchor, slot));
    }
    else if (mods.isCommandDown() || mods.isCtrlDown())
    {
        auto& row = *rows[(size_t) slot];
        if (! row.isChosen() || getChosen().size() > 1)
            row.setChosen (! row.isChosen());
        anchor = slot;
        if (! row.isChosen())
        {
            // Taken out: the drum shown moves to one still chosen.
            select (getChosen().front());
            return;
        }
    }
    else
    {
        for (auto& row : rows)
            row->setChosen (row->getSlot() == slot);
        anchor = slot;
    }
    select (slot);
}

void DrumPage::chooseSlots (const std::vector<int>& slots)
{
    if (slots.empty())
        return;
    const auto has = [&slots] (int slot) { return std::find (slots.begin(), slots.end(), slot) != slots.end(); };
    for (auto& row : rows)
        row->setChosen (has (row->getSlot()));
    anchor = slots.front();
    select (has (selected) ? selected : slots.front());
}

std::vector<int> DrumPage::getChosen() const
{
    std::vector<int> chosen;
    for (const auto& row : rows)
        if (row->isChosen())
            chosen.push_back (row->getSlot());
    if (chosen.empty())
        chosen.push_back (selected);
    return chosen;
}

void DrumPage::listenToRouting()
{
    for (auto* p : listened)
        p->removeListener (this);
    listened.clear();
    routingGesture = false;
    if (getChosen().size() < 2)
        return;
    for (auto& c : routingControls)
        if (auto* p = processor.getParameters().getParameter (c->getParameterId()))
        {
            p->addListener (this);
            listened.push_back (p);
        }
}

void DrumPage::parameterGestureChanged (int, bool starting)
{
    routingGesture = starting;   // a change made on the page (automation makes none)
}

void DrumPage::parameterValueChanged (int index, float value)
{
    if (! routingGesture || copying)
        return;
    for (auto* p : listened)
        if (p->getParameterIndex() == index)
        {
            const auto id = dynamic_cast<juce::RangedAudioParameter*> (p)->paramID;
            const auto what = id.fromFirstOccurrenceOf ("drum", false, false).trimCharactersAtStart ("0123456789");
            const juce::ScopedValueSetter<bool> guard (copying, true);
            for (const auto k : getChosen())
                if (k != selected)
                    if (auto* other = processor.getParameters().getParameter (ParamIDs::drum (k, what.toRawUTF8())))
                        other->setValueNotifyingHost (value);
            return;
        }
}

void DrumPage::showKitMenu (bool sequences)
{
    juce::PopupMenu menu;
    const auto files = sequences ? processor.listDrumSequences() : processor.listDrumKits();
    const auto current = sequences ? processor.getDrumSequenceName() : processor.getDrumKitName();
    menu.addItem (1, sequences ? "Save the sequence as..." : "Save the kit as...");
    menu.addSeparator();
    if (files.isEmpty())
        menu.addItem (2, sequences ? "(no sequences saved yet)" : "(no kits saved yet)", false);
    for (int i = 0; i < files.size(); ++i)
        menu.addItem (100 + i, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == current);
    menu.addSeparator();
    menu.addItem (3, "Show the folder");
    juce::Component::SafePointer<DrumPage> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sequences ? &sequencesButton : &kitsButton),
                        [self, sequences, files, current] (int result)
    {
        if (self == nullptr || result <= 0)
            return;
        auto& p = self->processor;
        if (result == 1)
        {
            auto* window = new juce::AlertWindow (sequences ? "Save the sequence" : "Save the kit",
                                                  "Its name (with LINK on, the other is saved under it too):", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", current.isNotEmpty() ? current : juce::String (sequences ? "My Beat" : "My Kit"));
            window->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, sequences] (int button)
            {
                if (self != nullptr && button == 1)
                {
                    const auto name = window->getTextEditorContents ("name");
                    sequences ? self->processor.saveDrumSequence (name) : self->processor.saveDrumKit (name);
                    self->repaint();
                }
            }), true);
            return;
        }
        if (result == 3)
        {
            const auto folder = sequences ? p.drumSequencesFolder() : p.drumKitsFolder();
            folder.createDirectory();
            folder.startAsProcess();
            return;
        }
        if (result >= 100 && result - 100 < files.size())
        {
            sequences ? p.loadDrumSequence (files[result - 100]) : p.loadDrumKit (files[result - 100]);
            self->select (self->selected);
            self->refreshPictures();
            self->repaint();
        }
    });
}

bool DrumPage::isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

std::vector<ParamControl*> DrumPage::getSlotControls()
{
    std::vector<ParamControl*> result;
    for (auto* list : { &slotControls, &routingControls, &acousticControls })
        for (auto& c : *list)
            result.push_back (c.get());
    return result;
}

void DrumPage::select (int slot)
{
    selected = juce::jlimit (0, synth::drums::numSlots - 1, slot);
    if (! rows[(size_t) selected]->isChosen())
        for (auto& row : rows)
            row->setChosen (row->getSlot() == selected);   // chosen from elsewhere (the sequencer): alone
    for (auto* list : { &slotControls, &routingControls, &velocityControls, &instrumentControls, &acousticControls, &layerControls })
        for (auto& c : *list)
        {
            const auto what = c->getParameterId().fromFirstOccurrenceOf ("drum", false, false).trimCharactersAtStart ("0123456789");
            c->attach (ParamIDs::drum (selected, what.toRawUTF8()));
        }
    for (auto& row : rows)
        row->setSelected (row->getSlot() == selected);
    sequencer.getGrid().setHighlightedRow (selected);
    kitView.setSelected (selected);
    articulations.showSlot (selected, getChosen());
    placeArticulations();
    updateDrumPanels();
    envelope.setTarget (MultiPointEditor::Kind::drumEnvelope, selected);
    listenToRouting();
    display.setSlot (selected);
    pictureKeys[(size_t) selected] = {};   // the large picture for this drum
    refreshPictures();
    repaint();
}

void DrumPage::showLoadMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("An audio file...", [this] { chooseSample(); });
    menu.addItem ("A folder of hits (velocity layers)...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("A folder of hits for " + juce::String (synth::drums::slotDefaults[(size_t) selected].key));
        const auto slot = selected;
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, slot] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().isDirectory())
                                      processor.loadInstrumentFolder (slot, fc.getResult());
                              });
    });
    menu.addItem ("An SFZ instrument...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("An SFZ instrument for " + juce::String (synth::drums::slotDefaults[(size_t) selected].key),
                                                       juce::File(), "*.sfz");
        const auto slot = selected;
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      processor.loadInstrumentSfz (slot, fc.getResult());
                              });
    });
    // 0.29.0: A LAYER of the user's own with this drum.
    menu.addSeparator();
    menu.addItem ("A LAYER with this drum (your sample, or a folder of hits)...", [this] { chooseLayer(); });
    menu.addItem ("Remove its layer", processor.getLayerRef (selected).isNotEmpty(), false, [this]
    {
        processor.clearLayer (selected);
        updateDrumPanels();
    });
    menu.addSeparator();
    menu.addItem ("A whole SFZ kit (onto every drum)...", [this] { importSfzKit(); });
    // 0.27.0: DRUMGIZMO KITS: free multi-mic kits recorded in real studios.
    menu.addItem ("A DrumGizmo kit (onto every drum)...", [this] { importDrumGizmoKit(); });
    // 0.33.0: a multi-mic sample library's folder of named WAVs (e.g. "..._KICK_DYN_KICK_V01_RR1.wav").
    menu.addItem ("A sample pack: a multi-mic library's folder (onto every drum)...", [this] { importSamplePack(); });
    menu.addItem ("Get free kits (drumgizmo.org)...", []
    {
        juce::URL ("https://drumgizmo.org/wiki/doku.php?id=kits").launchInDefaultBrowser();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&load));
}

void DrumPage::importSfzKit()
{
    chooser = std::make_unique<juce::FileChooser> ("An SFZ drum kit", juce::File(), "*.sfz");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        // Which layout its notes are in: most kits follow General MIDI.
        juce::PopupMenu layouts;
        juce::Component::SafePointer<DrumPage> self (this);
        const auto import = [self, file] (synth::drums::notemap::Layout layout)
        {
            if (self == nullptr)
                return;
            const auto result = self->processor.importSfzKit (file, layout);
            juce::String text;
            for (const auto& [slot, note] : result.slotNotes)
                text << synth::drums::slotDefaults[(size_t) slot].key << " " << synth::drums::articulationName (slot) << ": note " << note << "\n";
            if (! result.unused.isEmpty())
            {
                juce::StringArray notes;
                for (const auto n : result.unused)
                    notes.add (juce::String (n));
                text << "\nNot used (no drum for them in that layout, or a drum took another): " << notes.joinIntoString (", ");
            }
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "SFZ kit: " + file.getFileNameWithoutExtension(),
                                                   result.slotNotes.empty() ? "None of its notes play a drum in that layout." : text);
        };
        layouts.addSectionHeader ("ITS NOTES ARE IN...");
        layouts.addItem ("General MIDI (most kits)", [import] { import (synth::drums::notemap::Layout::generalMidi); });
        layouts.addItem ("The groove library's layout", [import] { import (synth::drums::notemap::Layout::grooveLibrary); });
        layouts.addItem ("The map I'm using now", [import, self] { if (self != nullptr) import (self->processor.getMapLayout()); });
        layouts.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&load));
    });
}

void DrumPage::chooseLayer()
{
    const auto slot = selected;
    chooser = std::make_unique<juce::FileChooser> ("A layer for " + synth::drums::articulationName (slot) + ": a sample, or a folder of hits",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.exists())
                                  return;
                              processor.loadLayer (slot, f);
                              updateDrumPanels();
                          });
}

void DrumPage::importSamplePack()
{
    chooser = std::make_unique<juce::FileChooser> ("A multi-mic sample library's folder (its top folder, or its INDIVIDUAL MICS)", juce::File());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;
        const auto result = processor.importSamplePack (folder);
        if (result.slots.empty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No drums found",
                                                   "None of its files' names could be read as drum sounds. A pack's files are named piece, "
                                                   "articulation, mic, then _V<layer>_RR<round> (e.g. JAD_DJENT_SNR_DYN1_V03_RR2.wav).");
            return;
        }
        juce::String text;
        for (const auto& [slot, files] : result.slots)
            text << synth::drums::slotDefaults[(size_t) slot].key << " " << synth::drums::articulationName (slot) << ": " << files << " files\n";
        if (! result.userChannels.isEmpty())
            text << "\nIts extra mics, each on a USER channel in the MIXER: " << result.userChannels.joinIntoString (", ") << "\n";
        if (! result.unused.isEmpty())
            text << "\nNot used (no drum for them): " << result.unused.joinIntoString (", ").substring (0, 400) << "\n";
        text << "\nStreamed from disk as it plays. Save it as a preset to keep it (the preset remembers where the pack is).";
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Sample pack: " + result.name, text);
    });
}

void DrumPage::importDrumGizmoKit()
{
    chooser = std::make_unique<juce::FileChooser> ("A DrumGizmo kit's file (e.g. CrocellKit_full.xml)", juce::File(), "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        if (! synth::drums::drumgizmo::isKit (file))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Not a DrumGizmo kit",
                                                   "Choose the kit's own file: the .xml beside its instrument folders whose name is "
                                                   "the kit's (e.g. CrocellKit_full.xml), not a midimap or an instrument's file.");
            return;
        }
        // How much of it to load: LIGHT fits in about a gigabyte; FULL is every hit (several gigabytes).
        juce::PopupMenu sizes;
        juce::Component::SafePointer<DrumPage> self (this);
        using Load = ParagonX1AudioProcessor::KitLoad;
        const auto import = [self, file] (Load load)
        {
            if (self == nullptr)
                return;
            const auto result = self->processor.importDrumGizmoKit (file, load);
            juce::String text;
            for (const auto& [slot, names] : result.slots)
                text << synth::drums::slotDefaults[(size_t) slot].key << " " << synth::drums::articulationName (slot) << ": "
                     << names.joinIntoString (" + ") << "\n";
            if (! result.unused.isEmpty())
                text << "\nNot used (no drum of ours for them, or another took its place): " << result.unused.joinIntoString (", ");
            text << "\n\nIts mics play through THE MIXER's channels. Loading takes a few seconds.";
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "DrumGizmo kit: " + result.kitName,
                                                   result.slots.empty() ? "None of its instruments matched a drum." : text);
        };
        sizes.addSectionHeader ("HOW MUCH OF IT...");
        sizes.addItem ("STREAMED (best): every hit, every mic, full length; their starts in memory (about 0.5 GB), the rest "
                       "read from disk as they play", [import] { import (Load::streamed); });
        sizes.addItem ("LIGHT: up to 8 layers of 2 hits, tails trimmed, all in memory (about 1 GB for a big kit)", [import] { import (Load::light); });
        sizes.addItem ("FULL IN MEMORY: every hit, every mic, full length (several GB of memory)", [import] { import (Load::full); });
        sizes.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&self->load));
    });
}

void DrumPage::showVelocity (bool on)
{
    velocityMode = on;
    (on ? velocityTab : envelopeTab).setToggleState (true, juce::dontSendNotification);
    updateDrumPanels();
    repaint();
}

void DrumPage::updateDrumPanels()
{
    const auto drum = view == View::drum;
    // THE SAMPLE ENGINE's controls under the picture while the chosen drum holds an instrument (or one is loading).
    const auto hasInstrument = processor.getInstrumentRef (selected).isNotEmpty();
    if (hasInstrument != showingInstrument)
    {
        showingInstrument = hasInstrument;
        resized();
    }
    const auto hasLayer = processor.getLayerRef (selected).isNotEmpty();
    if (hasLayer != showingLayer)
    {
        showingLayer = hasLayer;
        resized();
    }
    for (auto& c : instrumentControls)
        c->setVisible (drum && ! velocityMode && showingInstrument);
    for (auto& c : layerControls)
        c->setVisible (drum && ! velocityMode && showingLayer);
    // THE ACOUSTIC MODELS (0.24.0): their knobs while the drum is one of them, and TONE / SNAP named for what they are.
    {
        using synth::drums::Model;
        const auto model = (Model) juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::drum (selected, "Model"))->load());
        const auto acoustic = synth::drums::isAcoustic (model);
        if (acoustic != showingAcoustic)
        {
            showingAcoustic = acoustic;
            resized();
        }
        for (auto& c : acousticControls)
            c->setVisible (drum && acoustic);
        if ((int) model != labelledModel)
        {
            labelledModel = (int) model;
            const auto kick = model == Model::acousticKick, snare = model == Model::acousticSnare;
            const auto cymbal = synth::drums::isAcousticCymbal (model);
            const auto hat = model == Model::acousticHat || model == Model::acousticHatPedal;
            slotControls[3]->setLabelText (! acoustic ? "TONE" : kick ? "BEATER" : "STICK");
            slotControls[4]->setLabelText (! acoustic ? "SNAP" : kick ? "CLICK" : snare ? "WIRES" : model == Model::acousticChina ? "TRASH"
                                                                    : cymbal ? "PING" : "ATTACK");
            acousticControls[0]->setLabelText (cymbal && ! hat ? "BELL > EDGE" : "POSITION");
            acousticControls[1]->setLabelText (kick ? "FRONT HEAD" : hat ? "OPEN" : cymbal ? "WASH" : "SHELL");
        }
    }
    display.setVisible (drum && ! velocityMode);
    envelope.setVisible (drum && ! velocityMode);
    for (juce::Component* c : { (juce::Component*) &velocityEditor, (juce::Component*) &softButton, (juce::Component*) &gateButton,
                                (juce::Component*) &resetCurve })
        c->setVisible (drum && velocityMode);
    for (auto& c : velocityControls)
        c->setVisible (drum && velocityMode);
    const auto& curve = processor.getModMatrix().drumVelocityCurves[(size_t) selected];
    softButton.setToggleState (curve.soft, juce::dontSendNotification);
    gateButton.setToggleState (curve.gate, juce::dontSendNotification);
    velocityEditor.setLabel (juce::String (synth::drums::slotDefaults[(size_t) selected].key) + "  " + synth::drums::articulationName (selected)
                             + (getChosen().size() > 1 ? "  (+ " + juce::String ((int) getChosen().size() - 1) + " MORE CHOSEN)" : juce::String()));
    velocityEditor.repaint();
}

void DrumPage::setVelocityCurve (const synth::drums::VelocityCurve& curve, bool final)
{
    auto m = processor.getModMatrix();
    for (const auto k : getChosen())
        m.drumVelocityCurves[(size_t) k] = curve;
    processor.setModMatrix (m);
    if (final)
        processor.modMatrixEditFinished();
    updateDrumPanels();
}

void DrumPage::placeEnvelope()
{
    // The editor's plot is 14 px in from its sides, the picture 20 px in from the display's: a drum synth's hit lines
    // up under it. A sample's file is shown whole, so the picture draws the curve over START to END itself (lining the
    // strip up with one short hit out of a file left it too narrow to edit).
    const auto bounds = envelopeStrip.reduced (6, 0);
    if (envelope.getBounds() != bounds)
        envelope.setBounds (bounds);
}

juce::String DrumPage::keyFor (int slot) const
{
    const auto s = processor.getKitSettings().slots[(size_t) slot];
    return juce::String ((int) s.model) + ":" + juce::String (s.pitch) + ":" + juce::String (s.decay) + ":" + juce::String (s.tone) + ":"
         + juce::String (s.snap) + ":" + juce::String (s.gain) + ":" + juce::String ((juce::pointer_sized_int) s.sample)
         + ":" + juce::String ((int) s.mute) + ":" + juce::String (processor.getKitSettings().rootKey) + ":" + juce::String (s.start)
         + ":" + juce::String (s.end) + ":" + juce::String (s.trim) + ":" + juce::String (s.lowPass) + ":" + juce::String (s.highPass)
         + ":" + juce::String (s.envelopeSeconds) + ":" + juce::String (s.delay) + ":"
         + processor.getModMatrix().drumEnvelopes[(size_t) slot].encode();
}

void DrumPage::refreshPictures()
{
    wholeFile.setVisible (view == View::drum && display.showsFile());
    {
        const auto sample = processor.getDrumSample (selected);
        const auto stepping = view == View::drum && display.showsFile() && sample != nullptr && sample->path.isNotEmpty();
        previous.setVisible (stepping);
        next.setVisible (stepping);
    }
    {
        const auto kitName = processor.getDrumKitName(), seqName = processor.getDrumSequenceName();
        kitsButton.setButtonText (kitName.isEmpty() ? juce::String ("KITS") : "KIT: " + kitName);
        sequencesButton.setButtonText (seqName.isEmpty() ? juce::String ("SEQUENCES") : "SEQ: " + seqName);
    }
    placeEnvelope();
    const auto kit = processor.getKitSettings();
    for (int k = 0; k < synth::drums::numSlots; ++k)
    {
        const auto key = keyFor (k);
        if (key == pictureKeys[(size_t) k])
            continue;
        pictureKeys[(size_t) k] = key;
        auto& row = *rows[(size_t) k];
        row.setPicture (HitPicture::make (kit.slots[(size_t) k], juce::jmax (40, row.getWidth() - 330)));
        if (k == selected)
        {
            display.setPicture (HitPicture::make (kit.slots[(size_t) k], juce::jmax (100, display.getWidth() - 40)));
            repaint (drumArea);
        }
    }
}

void DrumPage::timerCallback()
{
    if (! isShowing())
        return;
    updateLights();
    if (++tick % 6 == 0)
    {
        refreshPictures();
        if (view == View::drum)
            updateDrumPanels();   // an undo or a preset may have changed the curve; an instrument may have loaded
    }
}

void DrumPage::updateLights()
{
    const auto& kit = processor.getDrumKit();
    for (auto& row : rows)
    {
        const auto age = kit.getHitAge (row->getSlot());
        row->setLit (age >= 0.0f ? juce::jmax (0.0f, 1.0f - age / 0.25f) : 0.0f);
        row->setPlayhead (age >= 0.0f && kit.isSounding (row->getSlot()) ? age : -1.0f);
    }
    kitView.updateLights();
    articulations.updateLights();
}

void DrumPage::placeArticulations()
{
    // In the kit view's bottom left corner, above its line naming the chosen drum: there the picture has only the
    // cymbals' stands (0.37.0's bigger kit fills the top left with crashes; measured on the click map: a short panel
    // covers no piece there, the hi-hat's tall one a fifth of the hats' stand and a little of the FX crash).
    const auto area = kitView.getBounds();
    const auto h = articulations.wantedHeight();
    articulations.setBounds (area.getX() + 6, juce::jmax (area.getY() + 4, area.getBottom() - 30 - h), juce::jmin (236, area.getWidth() / 3), h);
}

void DrumPage::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into " + juce::String (synth::drums::slotDefaults[(size_t) selected].key),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    const auto slot = selected;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && processor.loadDrumSample (slot, file))
                                  processor.previewDrum (slot);
                          });
}

void DrumPage::paint (juce::Graphics& g)
{
    for (const auto& area : { kitArea, drumArea })
        DockStyle::drawPanel (g, area.toFloat());

    const auto title = [&g] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area.toFloat().withWidth (140.0f)));
        g.drawText (text, area, juce::Justification::centredLeft, false);
    };
    const auto kit = processor.getKitSettings();
    title ("KIT", kitArea.reduced (18, 0).withHeight (40));
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const auto layout = processor.getMapLayout();
    g.drawText (layout == synth::drums::notemap::Layout::root
                    ? "The keys " + noteName (kit.rootKey) + " to " + noteName (kit.rootKey + synth::drums::numSlots - 1) + " play the drums"
                    : "MAP: " + synth::drums::notemap::layoutNames[(int) layout].toUpperCase() + " (SETTINGS)",
                kitArea.reduced (18, 0).withHeight (40).withTrimmedLeft (60), juce::Justification::centredLeft, false);

    if (view == View::sequencer)
    {
        title ("SEQUENCER", drumArea.reduced (18, 0).withHeight (40));
        return;
    }
    if (view == View::kit)
    {
        title ("KIT VIEW", drumArea.reduced (18, 0).withHeight (40));
        return;
    }
    const auto& slotKit = kit.slots[(size_t) selected];
    title ("DRUM", drumArea.reduced (18, 0).withHeight (40));

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const auto home = processor.homeNote (selected);
    g.drawText ((home >= 0 ? noteName (home) : juce::String ("-")) + "  " + synth::drums::slotDefaults[(size_t) selected].key + "  "
                    + drumName (slotKit, processor, selected),
                drumArea.reduced (18, 0).withHeight (40).withTrimmedLeft (70), juce::Justification::centredLeft, false);
}

void DrumPage::resized()
{
    auto area = getLocalBounds();
    kitArea = area.removeFromLeft (590);
    area.removeFromLeft (10);
    drumArea = area;

    // KIT: KITS, SEQUENCES and LINK at the end of the title line, the kit's own settings, then the 12 rows.
    auto k = kitArea.reduced (14, 10);
    {
        auto titleLine = k.removeFromTop (30);
        linkButton.setBounds (titleLine.removeFromRight (54).withSizeKeepingCentre (50, 24));
        titleLine.removeFromRight (4);
        sequencesButton.setBounds (titleLine.removeFromRight (124).withSizeKeepingCentre (120, 24));
        titleLine.removeFromRight (4);
        kitsButton.setBounds (titleLine.removeFromRight (110).withSizeKeepingCentre (106, 24));
    }
    std::vector<juce::Component*> globals;
    for (auto& c : kitControls)
        globals.push_back (c.get());
    SynthPanel::layoutRow (k.removeFromTop (ParamControl::knobHeight), globals, 110);
    k.removeFromTop (6);
    const auto rowHeight = k.getHeight() / synth::drums::numSlots;
    const auto rowsTop = k.getY();
    for (auto& row : rows)
        row->setBounds (k.removeFromTop (rowHeight).reduced (0, 2));

    // The view switch at the right of the panel's title line; the SEQUENCER over the panel, its grid's rows level
    // with the kit's.
    auto tabs = drumArea.reduced (14, 10).removeFromTop (30);
    sequencerTab.setBounds (tabs.removeFromRight (104).withSizeKeepingCentre (100, 24));
    drumTab.setBounds (tabs.removeFromRight (74).withSizeKeepingCentre (70, 24));
    kitTab.setBounds (tabs.removeFromRight (94).withSizeKeepingCentre (90, 24));
    // The DRUM view's own pair (0.18.0): ENVELOPE / VELOCITY, left of the view tabs.
    tabs.removeFromRight (18);
    velocityTab.setBounds (tabs.removeFromRight (94).withSizeKeepingCentre (90, 24));
    envelopeTab.setBounds (tabs.removeFromRight (94).withSizeKeepingCentre (90, 24));
    kitView.setBounds (drumArea.reduced (14, 10).withTrimmedTop (36));
    placeArticulations();
    sequencer.setBounds (drumArea);
    const auto inner = drumArea.reduced (14, 0);
    sequencer.setGridArea ({ inner.getX() - drumArea.getX(), rowsTop - drumArea.getY(), inner.getWidth(), rowHeight * synth::drums::numSlots });

    // DRUM: the drum, its buttons, the picture, its shaping, its routing.
    auto d = drumArea.reduced (14, 10);
    d.removeFromTop (30);
    auto bar = d.removeFromTop (54);
    slotControls[0]->setBounds (bar.removeFromLeft (200));
    bar.removeFromLeft (16);
    play.setBounds (bar.removeFromLeft (90).withSizeKeepingCentre (90, 30).translated (0, 8));
    bar.removeFromLeft (10);
    wholeFile.setBounds (bar.removeFromLeft (120).withSizeKeepingCentre (120, 30).translated (0, 8));

    clear.setBounds (bar.removeFromRight (90).withSizeKeepingCentre (90, 30).translated (0, 8));
    bar.removeFromRight (10);
    load.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 30).translated (0, 8));
    bar.removeFromRight (6);
    next.setBounds (bar.removeFromRight (32).withSizeKeepingCentre (30, 30).translated (0, 8));
    previous.setBounds (bar.removeFromRight (34).withSizeKeepingCentre (30, 30).translated (0, 8));
    d.removeFromTop (8);
    auto routing = d.removeFromBottom (ParamControl::knobHeight);
    d.removeFromBottom (14);
    auto knobs = d.removeFromBottom (ParamControl::knobHeight);
    d.removeFromBottom (10);
    // THE SAMPLE EDITOR's envelope (6c) above the hit, as Avenger has it.
    // Its plot (14 px in from its sides) lined up with the hit's picture (20 px in from the display's), so each point
    // sits over the moment it shapes.
    velocityArea = d;
    envelopeStrip = d.removeFromTop (96);
    d.removeFromTop (6);
    // THE SAMPLE ENGINE: a row of its controls under the picture.
    if (showingInstrument || showingLayer)
    {
        auto row = d.removeFromBottom (ParamControl::knobHeight);
        d.removeFromBottom (6);
        if (showingInstrument)
            for (auto& c : instrumentControls)
                c->setBounds (row.removeFromLeft (110));
        if (showingLayer)
            for (auto& c : layerControls)
                c->setBounds (row.removeFromRight (120));
    }
    display.setBounds (d);
    placeEnvelope();
    // VELOCITY (0.18.0) in the strip and picture's place: the curve, square-ish, then the gate and its switches.
    {
        auto v = velocityArea;
        velocityEditor.setBounds (v.removeFromLeft (juce::jmin (v.getWidth() - 260, juce::roundToInt ((float) v.getHeight() * 1.5f))));
        v.removeFromLeft (20);
        auto gateKnobs = v.removeFromTop (ParamControl::knobHeight);
        for (auto& c : velocityControls)
            c->setBounds (gateKnobs.removeFromLeft (110));
        v.removeFromTop (16);
        for (auto* b : { &softButton, &gateButton, &resetCurve })
        {
            b->setBounds (v.removeFromTop (30).withWidth (180));
            v.removeFromTop (8);
        }
    }
    std::vector<juce::Component*> shaping, routes;
    for (size_t i = 1; i < slotControls.size(); ++i)
        shaping.push_back (slotControls[i].get());
    if (showingAcoustic)   // after TONE and SNAP: POSITION, SHELL, VARIATION
        for (size_t i = 0; i < acousticControls.size(); ++i)
            shaping.insert (shaping.begin() + 4 + (std::ptrdiff_t) i, acousticControls[i].get());
    for (auto& c : routingControls)
        routes.push_back (c.get());
    SynthPanel::layoutRow (knobs, shaping, juce::jmin (110, knobs.getWidth() / juce::jmax (1, (int) shaping.size())));
    SynthPanel::layoutRow (routing, routes);   // nine: shared out across the row

    std::fill (pictureKeys.begin(), pictureKeys.end(), juce::String());   // sized afresh
    refreshPictures();
}
