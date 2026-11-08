#include "GuitarView.h"

#include "NoteNames.h"
#include "ui/Finish.h"
#include "ui/Fretboard.h"
#include "ui/LedDisplay.h"
#include "ui/Logo.h"
#include "ui/ParagonLookAndFeel.h"

#include <GuitarPicture.h>

namespace
{
    // In the 1600 x 900 canvas's pixels (the mock-up's); the view starts at row GuitarView::top.
    juce::Rectangle<int> at (int x, int y, int w, int h) { return { x, y - GuitarView::top, w, h }; }
    juce::Rectangle<float> atF (float x, float y, float w, float h) { return { x, y - (float) GuitarView::top, w, h }; }

    // Where the render's strings, frets and board edges land (guitar3d.py writes it beside the picture), in canvas pixels.
    struct Geometry
    {
        std::array<juce::Point<float>, guitar::numStrings> bridge {}, nut {};
        std::array<float, guitar::numFrets + 1> frets {};
        juce::Point<float> boardTop[2], boardBottom[2];
    };

    const Geometry& geometry()
    {
        static const Geometry g = []
        {
            Geometry result;
            const auto json = juce::JSON::parse (juce::String::fromUTF8 (GuitarPicture::guitargeometry_json, GuitarPicture::guitargeometry_jsonSize));
            const auto point = [] (const juce::var& v) { return juce::Point<float> ((float) v[0], (float) v[1]); };
            for (int s = 0; s < guitar::numStrings; ++s)
            {
                result.bridge[(size_t) s] = point (json["strings"][s]["bridge"]);
                result.nut[(size_t) s] = point (json["strings"][s]["nut"]);
            }
            for (int f = 0; f <= guitar::numFrets; ++f)
                result.frets[(size_t) f] = (float) json["frets"][f][0];
            for (int i = 0; i < 2; ++i)
            {
                result.boardTop[i] = point (json["boardTop"][i]);
                result.boardBottom[i] = point (json["boardBottom"][i]);
            }
            return result;
        }();
        return g;
    }

    // The family's colours only (PR7-D's KitView): any hue outside 318 to 42 degrees (through red) turned to the
    // nearer end, as JPEG moves a few colours a few degrees.
    void familyHues (juce::Image& image)
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x)
            {
                const auto c = data.getPixelColour (x, y);
                const auto hue = c.getHue() * 360.0f;
                if (c.getSaturation() <= 0.0f || hue <= 42.0f || hue >= 318.0f)
                    continue;
                const auto to = hue < 180.0f ? 40.0f : 320.0f;
                data.setPixelColour (x, y, c.withHue (to / 360.0f));
            }
    }

    void glow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width, float alpha = 1.0f)
    {
        for (const auto [w, a] : { std::pair { 3.6f, 0.12f }, std::pair { 1.9f, 0.35f }, std::pair { 1.0f, 1.0f } })
        {
            g.setColour (colour.withMultipliedAlpha (a * alpha));
            g.strokePath (path, juce::PathStrokeType (width * w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    juce::Path line (juce::Point<float> a, juce::Point<float> b)
    {
        juce::Path p;
        p.startNewSubPath (a);
        p.lineTo (b);
        return p;
    }

    void bloom (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float alpha)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), centre, colour.withAlpha (0.0f), centre.translated (radius, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Colour colour, float size, bool bold = true,
               juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (s, area, j, false);
    }

}

//==============================================================================
juce::Image GuitarView::picture()
{
    static const juce::Image image = []
    {
        auto loaded = juce::ImageFileFormat::loadFrom (GuitarPicture::guitar_jpg, GuitarPicture::guitar_jpgSize);
        if (loaded.isValid())
        {
            loaded = loaded.convertedToFormat (juce::Image::RGB);
            familyHues (loaded);
        }
        return loaded;
    }();
    return image;
}

//==============================================================================
GuitarView::ValueBox::ValueBox (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& labelText,
                                const juce::String& tooltip)
    : parameter (*state.getParameter (id)),
      label (labelText),
      attachment (parameter, [this] (float v) { value = v; repaint(); })
{
    isSwitch = dynamic_cast<juce::AudioParameterBool*> (&parameter) != nullptr;
    setTooltip (tooltip);
    setMouseCursor (isSwitch ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
}

juce::String GuitarView::ValueBox::getValueText() const
{
    if (isSwitch)
        return value > 0.5f ? "ON" : "OFF";
    auto t = parameter.getText (parameter.convertTo0to1 (value), 12).toUpperCase();
    return t.replace ("FRET ", "");
}

void GuitarView::ValueBox::paint (juce::Graphics& g)
{
    // 0.5.0: one of the rack's small screens (its name stamped over it by the rack), the value in orange.
    const auto glass = LedDisplay::paintScreen (g, getLocalBounds().toFloat(), 0.45f);
    g.setColour (LedDisplay::ledOrange);
    g.setFont (juce::FontOptions (glass.getHeight() * 0.62f, juce::Font::bold));
    g.drawFittedText (getValueText(), glass.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    LedDisplay::paintGlassOverlay (g, glass, 0.45f);
}

void GuitarView::ValueBox::step (int by)
{
    const auto& range = parameter.getNormalisableRange();
    const auto stepSize = range.interval >= 1.0f ? 1.0f : 0.1f;
    attachment.setValueAsCompleteGesture (range.snapToLegalValue (juce::jlimit (range.start, range.end, value + (float) by * stepSize)));
}

void GuitarView::ValueBox::mouseDown (const juce::MouseEvent&)
{
    dragged = false;
    dragStart = value;
    if (! isSwitch)
        attachment.beginGesture();
}

void GuitarView::ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (isSwitch)
        return;
    dragged = true;
    const auto& range = parameter.getNormalisableRange();
    const auto stepSize = range.interval >= 1.0f ? 1.0f : 0.1f;
    const auto steps = (float) juce::roundToInt ((float) -e.getDistanceFromDragStartY() / 8.0f);
    attachment.setValueAsPartOfGesture (range.snapToLegalValue (juce::jlimit (range.start, range.end, dragStart + steps * stepSize)));
}

void GuitarView::ValueBox::mouseUp (const juce::MouseEvent& e)
{
    if (isSwitch)
    {
        if (! e.mods.isPopupMenu())
            attachment.setValueAsCompleteGesture (value > 0.5f ? 0.0f : 1.0f);
        return;
    }
    attachment.endGesture();
}

void GuitarView::ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! isSwitch)
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

void GuitarView::ValueBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! isSwitch && wheel.deltaY != 0.0f)
        step (wheel.deltaY > 0.0f ? 1 : -1);
}

//==============================================================================
GuitarView::GuitarView (ParagonX1AudioProcessor& p)
    : processor (p),
      tone (p.getParameters(), ParamIDs::tone, "TONE", "A gentle low-pass on the whole guitar; all the way up: open"),
      bite (p.getParameters(), ParamIDs::bite, "BITE", "Brighter, with less low-mid: a cut at 400 Hz and a lift above 3 kHz"),
      attack (p.getParameters(), ParamIDs::attack, "ATTACK",
              "Each note's first few milliseconds louder, the more the harder it's played (a boost of the take's own pick, "
              "not a separate pick noise)"),
      transpose (p.getParameters(), ParamIDs::transpose, "TRANSPOSE", "Plays every note this many semitones away"),
      velToVol (p.getParameters(), ParamIDs::velocityToVolume, "VEL > VOL",
                "How much velocity sets the level within a dynamic (velocity always chooses the dynamic)"),
      release (p.getParameters(), ParamIDs::release, "RELEASE", "How quickly a let-go note is damped"),
      polyWindow (p.getParameters(), ParamIDs::polyWindow, "POLY WINDOW",
                  "Notes played this close together are fretted as one chord. It delays the guitar by the same amount, which "
                  "the DAW makes up for. 0: off"),
      variation (p.getParameters(), ParamIDs::variation, "VARIATION",
                 "Each note a touch different (now and then a neighbouring take; a little start, level and tuning). 0: "
                 "every take exactly as it was rendered"),
      level (p.getParameters(), ParamIDs::patchLevel, "LEVEL", "The preset's own level"),
      output (p.getParameters(), ParamIDs::outputGain, "OUTPUT", "The plugin's output level (not saved in presets)"),
      pickMode (p.getParameters(), ParamIDs::pickMode, "PICKING MODE",
                "Which stroke each note is: alternate; all down; all up; economy (alternate, but crossing to the next "
                "string the way the pick is moving keeps the stroke)"),
      fretMode (p.getParameters(), ParamIDs::fretMode, "FRETTING MODE",
                "Natural: the hand follows the notes, 4 frets wide. Polyphonic: 5 wide, chords over as many strings as notes"),
      forceString (p.getParameters(), ParamIDs::forceString, "FORCE STRING",
                   "Every note on that string where it can be (7: the low A ... 1: the high E). Off: the hand chooses", true),
      handPosition (p.getParameters(), ParamIDs::handPosition, "HAND POSITION",
                    "Where the hand sits on the neck. Auto: it follows the notes")
{
    addAndMakeVisible (rack);
    Finish::paintAs (Finish::Type::pearlBlack);   // (the rack is always pearl black: its lettering's ink)
    for (auto* c : { &tone, &bite, &attack, &transpose, &velToVol, &release, &polyWindow, &variation, &level, &output,
                     &forceString, &handPosition })
    {
        c->setRack (true);
        rack.addAndMakeVisible (*c);
    }
    for (auto* c : { &pickMode, &fretMode })
    {
        c->setCompact (true);
        c->setRack (true);
        rack.addAndMakeVisible (*c);
    }
    Finish::paintAs (std::nullopt);

    performance.addItemList (ParagonX1AudioProcessor::performanceNames, 1);
    performance.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);   // on the rack's screen
    performance.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    performance.setColour (juce::ComboBox::textColourId, LedDisplay::ledOrange);
    performance.setColour (juce::ComboBox::arrowColourId, LedDisplay::ledOrange.withAlpha (0.6f));
    performance.setTooltip ("A playing style: sets the fretting controls together. Rhythm: natural fretting, the hand AUTO; "
                            "Lead: the same, one note at a time (MONO); Chords: polyphonic fretting. Custom: the controls "
                            "as you've set them");
    performance.onChange = [this]
    {
        if (const auto id = performance.getSelectedId(); id > 1)
            processor.applyPerformance ((ParagonX1AudioProcessor::Performance) (id - 1));
    };
    rack.addAndMakeVisible (performance);

    variationOn.setTooltip ("VARIATION on or off (off: every take exactly as it was rendered, as 0.2.0 played them)");
    variationOn.onClick = [this]
    {
        const auto current = processor.getParameters().getRawParameterValue (ParamIDs::variation)->load();
        if (current > 0.0f)
            lastVariation = current;
        processor.setParameterNow (ParamIDs::variation, current > 0.0f ? 0.0f : lastVariation);
    };
    rack.addAndMakeVisible (variationOn);

    auto* keyMap = p.getParameters().getParameter (ParamIDs::keyMap);
    keyMapAttachment = std::make_unique<juce::ParameterAttachment> (*keyMap, [this] (float v)
    {
        splitMap.setToggleState (juce::roundToInt (v) == 0, juce::dontSendNotification);
        factoryMap.setToggleState (juce::roundToInt (v) == 1, juce::dontSendNotification);
    });
    splitMap.setTooltip ("Your own keyswitch map: C-1 plays palm mutes at velocities 1-59 and sustain at 60-119 (each scaled "
                         "over all its dynamics); C#-1 the same as power chords");
    factoryMap.setTooltip ("Your guitar instrument's factory keyswitches: C-1 sustain, C#-1 palm mute, D#-1 and E-1 their power chords");
    splitMap.onClick = [this] { keyMapAttachment->setValueAsCompleteGesture (0.0f); };
    factoryMap.onClick = [this] { keyMapAttachment->setValueAsCompleteGesture (1.0f); };
    keyMapAttachment->sendInitialUpdate();
    for (auto* b : { &splitMap, &factoryMap, &variationOn })
        b->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);   // a small key, only its stripe lettered
    rack.addAndMakeVisible (splitMap);
    rack.addAndMakeVisible (factoryMap);

    auto& state = p.getParameters();
    boxes.push_back (std::make_unique<ValueBox> (state, ParamIDs::capo, "CAPO",
                                                 "The capo's fret: nothing plays below it, and its fret rings as an open string "
                                                 "does. Or drag it on the neck, from the nut; double-click it there to take it off"));
    boxes.push_back (std::make_unique<ValueBox> (state, ParamIDs::handSpan, "HAND SPAN",
                                                 "How many frets the hand reaches without moving. Auto: the fretting mode's own (4, polyphonic 5)"));
    boxes.push_back (std::make_unique<ValueBox> (state, ParamIDs::handReset, "HAND RESET",
                                                 "Seconds with nothing held before an AUTO hand goes back down the neck"));
    boxes.push_back (std::make_unique<ValueBox> (state, ParamIDs::mono, "MONO", "One note at a time: a new note stops the others"));
    for (auto& b : boxes)
        rack.addAndMakeVisible (*b);
    rack.onPageChanged = [this] { showRackPage(); };
    showRackPage();

    setMouseCursor (juce::MouseCursor::NormalCursor);
    refresh();
    startTimerHz (30);
}

GuitarView::~GuitarView() = default;

void GuitarView::resized()
{
    // Knob row 1 (where the reference has its tone knobs), row 2, the OUTPUT box: as the approved mock-up.
    rack.setBounds (at (8, 66, 1584, 300));
    ParamControl* knobs[] = { &tone, &bite, &attack, &transpose, &velToVol, &release, &polyWindow, &variation };
    for (int i = 0; i < GuitarRack::numKnobs; ++i)
        knobs[i]->setBounds (rack.knobCell (i));
    level.setBounds (rack.sideKnobCell (0));
    output.setBounds (rack.sideKnobCell (1));
    splitMap.setBounds (rack.keyCell (0));
    factoryMap.setBounds (rack.keyCell (1));
    variationOn.setBounds (rack.keyCell (2));

    // The rack's FRETTING page: the menus, the two knobs, the value boxes.
    pickMode.setBounds (rack.menuCell (0).reduced (4, 2));
    fretMode.setBounds (rack.menuCell (1).reduced (4, 2));
    performance.setBounds (rack.menuCell (2).reduced (4, 2));
    forceString.setBounds (rack.frettingKnobCell (0));
    handPosition.setBounds (rack.frettingKnobCell (1));
    for (size_t i = 0; i < boxes.size(); ++i)
        boxes[i]->setBounds (rack.boxCell ((int) i));
}

void GuitarView::showRackPage()
{
    const auto playing = rack.getPage() == GuitarRack::Page::playing;
    for (auto* c : std::initializer_list<juce::Component*> { &tone, &bite, &attack, &transpose, &velToVol, &release, &polyWindow, &variation })
        c->setVisible (playing);
    for (auto* c : std::initializer_list<juce::Component*> { &pickMode, &fretMode, &performance, &forceString, &handPosition })
        c->setVisible (! playing);
    for (auto& b : boxes)
        b->setVisible (! playing);
}

//==============================================================================
float GuitarView::fretX (int fret) const
{
    return geometry().frets[(size_t) juce::jlimit (0, guitar::numFrets, fret)];
}

int GuitarView::fretAt (float x) const
{
    if (x >= fretX (1))
        return 0;
    for (int f = 2; f <= guitar::numFrets; ++f)
        if (x >= fretX (f))
            return f;
    return guitar::numFrets;
}

float GuitarView::stringY (int string, float x) const
{
    const auto& g = geometry();
    const auto b = g.bridge[(size_t) string], n = g.nut[(size_t) string];
    return b.y + (n.y - b.y) * (x - b.x) / (n.x - b.x) - (float) top;
}

float GuitarView::boardEdge (bool upper, float x) const
{
    const auto& e = upper ? geometry().boardTop : geometry().boardBottom;
    return e[0].y + (e[1].y - e[0].y) * (x - e[0].x) / (e[1].x - e[0].x) - (float) top;
}

juce::Point<float> GuitarView::noteCentre (int string, int fret) const
{
    const auto x = fret <= 0 ? fretX (0) + 14.0f : (fretX (fret - 1) + fretX (fret)) * 0.5f;
    return { x, stringY (string, x) };
}

juce::Rectangle<int> GuitarView::neckArea() const
{
    return juce::Rectangle<float>::leftTopRightBottom (0.0f, boardEdge (true, fretX (guitar::numFrets)) - 52.0f,
                                                       (float) getWidth(), boardEdge (false, fretX (guitar::numFrets)) + 44.0f)
        .getSmallestIntegerContainer();
}

//==============================================================================
void GuitarView::refresh()
{
    const auto& guitar = processor.getGuitar();
    const auto settings = processor.getGuitarSettings();
    auto changed = false;
    for (int s = 0; s < guitar::numStrings; ++s)
    {
        const auto sounding = guitar.getSounding (s);
        const auto level = juce::jlimit (0.0f, 1.0f, sounding.level);
        if (sounding.fret != frets[(size_t) s] || sounding.muted != muted[(size_t) s] || std::abs (level - levels[(size_t) s]) > 0.04f)
        {
            frets[(size_t) s] = sounding.fret;
            muted[(size_t) s] = sounding.muted;
            levels[(size_t) s] = level;
            changed = true;
        }
    }
    const auto newHand = guitar.getHand(), newSpan = guitar::handSpan (settings.fretMode, settings.handSpan), newForced = guitar.getForcedString (settings);
    if (newHand != hand || newSpan != span || newForced != forced || settings.capo != capo)
    {
        hand = newHand;
        span = newSpan;
        forced = newForced;
        capo = settings.capo;
        changed = true;
    }
    if (changed)
        repaint (neckArea());

    const auto n = guitar.getNowPlaying();
    if (n.count != nowCount || guitar.getArticulationKey() != latchedKey)
    {
        nowCount = n.count;
        now = n;
        latchedKey = guitar.getArticulationKey();
    }

    // The rack's screen: the key map and what's latched, then the last note; its meter; its status bar (off while bypassed).
    {
        const auto split = settings.keyMap == guitar::KeyMap::split;
        using namespace guitar::keyswitch;
        const auto power = split ? latchedKey == cSharp1 : (latchedKey == dSharp1 || latchedKey == e1);
        const auto played = now.note >= 0;
        const auto isMute = played && now.articulation == guitar::Articulation::mute;
        GuitarRack::Screen screen;
        screen.caption = split ? "KEY MAP  SPLIT  (C-1 BY VELOCITY)" : "KEY MAP  FACTORY";
        screen.articulation = juce::String (power ? "POWER " : "") + (isMute ? "PALM MUTE" : "SUSTAIN");
        screen.key = notenames::name (latchedKey);
        screen.line1 = played ? notenames::name (now.note) + "   VEL " + juce::String (now.inputVelocity) + "   TAKE "
                                    + juce::String (now.roundRobin + 1) + "/" + juce::String (now.roundRobins)
                              : juce::String ("PLAY A NOTE");
        screen.line2 = played ? "STRING " + juce::String (guitar::playerNumber (now.string)) + "  FRET " + juce::String (now.fret)
                                    + (isMute ? "  MUTE " : "  DYNAMIC ") + juce::String (now.layer + 1) + "/" + juce::String (now.layers)
                              : juce::String ("NOTHING PLAYED YET");
        rack.setScreen (screen);
        rack.setOn (processor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() < 0.5f);
        const auto peakDb = juce::Decibels::gainToDecibels (processor.getOutputMeter().takePeak(), -100.0f);
        meterDb = juce::jmax (peakDb, meterDb - 24.0f / 30.0f, -100.0f);
        rack.setMeterDb (meterDb);
    }

    performance.setSelectedId ((int) processor.currentPerformance() + 1, juce::dontSendNotification);
    const auto variationValue = processor.getParameters().getRawParameterValue (ParamIDs::variation)->load();
    variationOn.setToggleState (variationValue > 0.0f, juce::dontSendNotification);
}

//==============================================================================
void GuitarView::paint (juce::Graphics& g)
{
    const auto image = picture();
    const auto scale = (float) image.getWidth() / 1600.0f;
    if (image.isValid())
        g.drawImage (image, 0, 0, getWidth(), getHeight(), 0, juce::roundToInt ((float) top * scale), image.getWidth(),
                     juce::roundToInt ((float) getHeight() * scale));
    else
        g.fillAll (ParagonColours::background);

    paintNeck (g);
}

void GuitarView::paintNeck (juce::Graphics& g)
{
    // The hand's reach: a lit window across the board, its frets brighter, a bracket under the neck.
    const auto first = juce::jmax (capo + 1, hand), last = juce::jmin (guitar::numFrets, hand + span - 1);
    const auto xa = first <= capo + 1 ? fretX (capo) : fretX (first - 1), xb = fretX (last);   // (the nut side is on the right)
    {
        juce::Path window;
        window.startNewSubPath (xa, boardEdge (true, xa));
        window.lineTo (xb, boardEdge (true, xb));
        window.lineTo (xb, boardEdge (false, xb));
        window.lineTo (xa, boardEdge (false, xa));
        window.closeSubPath();
        g.setColour (ParagonColours::accent.withAlpha (0.10f));
        g.fillPath (window);
        for (int f = juce::jmax (1, first); f <= last; ++f)
            glow (g, line ({ fretX (f), boardEdge (true, fretX (f)) + 2.0f }, { fretX (f), boardEdge (false, fretX (f)) - 2.0f }),
                  juce::Colour (0xffff9646), 1.6f, 0.9f);
        const auto yb = juce::jmax (boardEdge (false, xa), boardEdge (false, xb)) + 16.0f;
        juce::Path bracket;
        bracket.startNewSubPath (xb, yb - 6.0f);
        bracket.lineTo (xb, yb);
        bracket.lineTo (xa, yb);
        bracket.lineTo (xa, yb - 6.0f);
        glow (g, bracket, ParagonColours::accent, 1.5f, 0.9f);
        const auto handText = juce::String ("HAND  ") + juce::String (first) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + juce::String (last)
                            + (processor.getGuitarSettings().hand == 0 ? juce::String::fromUTF8 (" \xc2\xb7 AUTO") : juce::String::fromUTF8 (" \xc2\xb7 FIXED"));
        text (g, handText, { juce::jmin (xa, xb) - 40.0f, yb + 4.0f, std::abs (xa - xb) + 80.0f, 20.0f }, ParagonColours::accent, 11.0f, true,
              juce::Justification::centred);
    }

    // Fret numbers above the board; the string names past the nut.
    for (int f = 0; f <= guitar::numFrets; ++f)
    {
        const auto x = noteCentre (0, f).x;
        const auto lit = f >= first && f <= last;
        text (g, juce::String (f), { x - 14.0f, boardEdge (true, x) - 24.0f, 28.0f, 16.0f }, lit ? ParagonColours::accent : juce::Colour (0xff8a7f84),
              10.0f, lit, juce::Justification::centred);
    }
    for (int s = 0; s < guitar::numStrings; ++s)
    {
        // (past the headstock's tip, by the window's right edge: 0.5.0's smaller guitar ends inside the window)
        const auto x = 1582.0f;
        const auto y = stringY (s, fretX (0)) + (stringY (s, fretX (0)) - stringY (3, fretX (0))) * 0.1f;
        text (g, juce::String (guitar::playerNumber (s)) + guitar::stringName (s), { x - 13.0f, y - 7.0f, 26.0f, 14.0f },
              Fretboard::stringColour (s).withAlpha (forced == s ? 1.0f : 0.8f), 9.0f, true, juce::Justification::centred);
    }

    // The capo: a pearl bar just behind its fret.
    if (capo > 0)
    {
        const auto x = fretX (capo - 1) * 0.35f + fretX (capo) * 0.65f;
        glow (g, line ({ x, boardEdge (true, x) - 4.0f }, { x, boardEdge (false, x) + 4.0f }), ParagonColours::metalLight, 7.0f, 0.9f);
        text (g, "CAPO " + juce::String (capo), { x - 40.0f, boardEdge (true, x) - 38.0f, 80.0f, 14.0f }, ParagonColours::metalLight, 10.0f, true,
              juce::Justification::centred);
    }

    // A forced string glows along its length.
    if (forced >= 0)
        glow (g, line ({ 0.0f, stringY (forced, 0.0f) }, { fretX (0), stringY (forced, fretX (0)) }), Fretboard::stringColour (forced), 1.6f, 0.85f);

    // The notes: a solid dot sustained, a ring palm-muted; the string lit from the note to the bridge (off to the left).
    for (int s = 0; s < guitar::numStrings; ++s)
    {
        const auto fret = frets[(size_t) s];
        if (fret < 0)
            continue;
        const auto colour = Fretboard::stringColour (s);
        const auto a = juce::jlimit (0.35f, 1.0f, 0.35f + levels[(size_t) s] * 1.3f);
        const auto c = noteCentre (s, fret);
        bloom (g, c, 48.0f, colour, 0.5f * a);
        glow (g, line (c, { 0.0f, stringY (s, 0.0f) }), colour, 2.2f, 0.8f * a);
        // as big as the strings' spacing allows (22 px on 0.4's full-size neck)
        const auto spacing = (stringY (guitar::numStrings - 1, c.x) - stringY (0, c.x)) / (float) (guitar::numStrings - 1);
        const auto size = juce::jlimit (12.0f, 22.0f, spacing * 0.92f);
        const auto dot = juce::Rectangle<float> (size, size).withCentre (c);
        bloom (g, c, 18.0f, colour, 0.8f * a);
        if (muted[(size_t) s])
        {
            g.setColour (juce::Colour (0xeb0a0406));
            g.fillEllipse (dot);
            g.setColour (colour.withAlpha (a));
            g.drawEllipse (dot.reduced (1.4f), 2.8f);
        }
        else
        {
            g.setColour (colour.withAlpha (a));
            g.fillEllipse (dot);
        }
        text (g, juce::String (fret), dot, muted[(size_t) s] ? colour : juce::Colour (0xff1a0508), juce::jmin (9.5f, size * 0.55f), true, juce::Justification::centred);
    }
}

//==============================================================================
// THE CAPO: grab the nut (or the capo) and drag along the neck; double-click the capo to take it off.
void GuitarView::setCapo (int fret)
{
    if (auto* p = processor.getParameters().getParameter (ParamIDs::capo))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, ParamRanges::maxCapo, fret)));
}

void GuitarView::mouseDown (const juce::MouseEvent& e)
{
    const auto x = (float) e.x, y = (float) e.y;
    const auto onBoard = y > boardEdge (true, x) - 10.0f && y < boardEdge (false, x) + 10.0f;
    const auto capoX = capo > 0 ? fretX (capo - 1) * 0.35f + fretX (capo) * 0.65f : fretX (0);
    draggingCapo = onBoard && std::abs (x - capoX) < 16.0f;
    if (draggingCapo)
        if (auto* p = processor.getParameters().getParameter (ParamIDs::capo))
            p->beginChangeGesture();
}

void GuitarView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingCapo)
        setCapo (fretAt ((float) e.x));
}

void GuitarView::mouseUp (const juce::MouseEvent&)
{
    if (draggingCapo)
        if (auto* p = processor.getParameters().getParameter (ParamIDs::capo))
            p->endChangeGesture();
    draggingCapo = false;
}

void GuitarView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (capo > 0 && std::abs ((float) e.x - (fretX (capo - 1) * 0.35f + fretX (capo) * 0.65f)) < 16.0f)
        processor.setParameterNow (ParamIDs::capo, 0.0f);
}
