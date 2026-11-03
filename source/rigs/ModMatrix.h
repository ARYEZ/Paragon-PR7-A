#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/DrumSequencer.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/MultiPoint.h"
#include "dsp/synth/VelocityCurve.h"

#include <vector>

// A multipoint shape as presets keep it (PR7-K's stage 4b): here, a drum's envelope (THE SAMPLE EDITOR), x 0 to 1
// across the hit.
struct ModShapeData
{
    std::vector<synth::mod::ShapePoint> points;
    int sustain = -1, loopStart = -1, loopEnd = -1;

    bool operator== (const ModShapeData& o) const noexcept
    {
        if (points.size() != o.points.size() || sustain != o.sustain || loopStart != o.loopStart || loopEnd != o.loopEnd)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (! juce::exactlyEqual (points[i].x, o.points[i].x) || ! juce::exactlyEqual (points[i].y, o.points[i].y)
                || ! juce::exactlyEqual (points[i].curve, o.points[i].curve))
                return false;
        return true;
    }

    // A new drum envelope (6c): flat at the top, changing nothing; x 0 to 1 across the hit.
    static ModShapeData flatDrumEnvelope()
    {
        return { { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 };
    }
    // The drum envelope's menu (6c): back to front (each line's curve turned round with it), and upside down.
    ModShapeData mirrored() const
    {
        ModShapeData d { {}, -1, -1, -1 };
        for (size_t i = points.size(); i-- > 0;)
        {
            const auto curve = i > 0 ? -points[i - 1].curve : 0.0f;   // the line that led to it now leads away
            d.points.push_back ({ 1.0f - points[i].x, points[i].y, curve });
        }
        return d;
    }
    ModShapeData flipped() const
    {
        auto d = *this;
        for (auto& p : d.points)
            p.y = 1.0f - p.y;
        return d;
    }
    // For the audio thread: plain data, the points sorted, at most 64.
    synth::mod::Shape toShape() const
    {
        synth::mod::Shape shape;
        shape.count = juce::jmin ((int) points.size(), synth::mod::maxShapePoints);
        for (int i = 0; i < shape.count; ++i)
            shape.points[(size_t) i] = points[(size_t) i];
        const auto valid = [&shape] (int index) { return index >= 0 && index < shape.count ? index : -1; };
        shape.sustain = valid (sustain);
        shape.loopStart = valid (loopStart);
        shape.loopEnd = valid (loopEnd);
        return shape;
    }

    // As text: "x:y:curve ..." then the flags.
    juce::String encode() const
    {
        juce::StringArray items;
        for (const auto& p : points)
            items.add (juce::String (p.x, 4) + ":" + juce::String (p.y, 4) + ":" + juce::String (p.curve, 3));
        return items.joinIntoString (" ");
    }
    // `lfo`: x kept within 0 to 1 (always, for a drum's envelope; the flag is PR7-K's, kept so the calls read the same).
    static ModShapeData decode (const juce::String& text, int sustain, int loopStart, int loopEnd, bool lfo)
    {
        ModShapeData d;
        float lastX = 0.0f;
        for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
        {
            const auto parts = juce::StringArray::fromTokens (item, ":", {});
            if (parts.size() != 3 || (int) d.points.size() >= synth::mod::maxShapePoints)
                continue;
            auto x = juce::jmax (lastX, parts[0].getFloatValue());   // never backwards
            if (lfo)
                x = juce::jlimit (0.0f, 1.0f, x);
            d.points.push_back ({ x, juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue()), juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) });
            lastX = x;
        }
        if (d.points.size() < 2)
            return flatDrumEnvelope();
        d.sustain = sustain;
        d.loopStart = loopStart;
        d.loopEnd = loopEnd;
        return d;
    }
};

// The kit's preset data that isn't parameters: THE DRUM SEQUENCER's patterns (PR7-K's 6b) and THE SAMPLE EDITOR's
// envelopes (6c). The name and the XML ("Modulation", "DrumPattern", "DrumEnvelope") are PR7-K's, where these lived in
// its mod matrix: kept so PR7-K presets' and projects' drum data reads the same here.
struct ModMatrix
{
    // THE DRUM SEQUENCER's patterns A to D; each pattern's notes on the heap.
    std::vector<synth::drumseq::Pattern> drumPatterns = std::vector<synth::drumseq::Pattern> ((size_t) synth::drumseq::numPatterns);
    // THE SAMPLE EDITOR's envelopes, one per drum slot.
    static constexpr int numDrumSlots = 36;
    std::vector<ModShapeData> drumEnvelopes = std::vector<ModShapeData> ((size_t) numDrumSlots, ModShapeData::flatDrumEnvelope());
    // 0.18.0: each drum's VELOCITY CURVE (XML "VelocityCurve", only those that aren't straight).
    std::vector<synth::drums::VelocityCurve> drumVelocityCurves = std::vector<synth::drums::VelocityCurve> ((size_t) numDrumSlots);
    // 0.20.0, THE MIXER's BLEED FROM INSTRUMENTS: each channel's level, polarity and on for each piece (channel x
    // numPieces + piece; XML "Bleed" channel / piece, only those changed).
    std::vector<synth::mixer::BleedCell> mixerBleed = std::vector<synth::mixer::BleedCell> ((size_t) (synth::mixer::numChannels * synth::mixer::numPieces));
    synth::mixer::BleedCell& bleed (int channel, int piece) { return mixerBleed[(size_t) (channel * synth::mixer::numPieces + piece)]; }
    const synth::mixer::BleedCell& bleed (int channel, int piece) const { return mixerBleed[(size_t) (channel * synth::mixer::numPieces + piece)]; }
    // 0.21.0, THE MIXER's SENDS: each strip's (the channels, then the buses) two sends (XML "Send" strip / slot, only
    // those changed).
    std::vector<synth::mixer::Send> mixerSends = std::vector<synth::mixer::Send> ((size_t) (synth::mixer::numStrips * synth::mixer::numSendSlots));
    synth::mixer::Send& send (int strip, int slot) { return mixerSends[(size_t) (strip * synth::mixer::numSendSlots + slot)]; }
    const synth::mixer::Send& send (int strip, int slot) const { return mixerSends[(size_t) (strip * synth::mixer::numSendSlots + slot)]; }
    // 0.22.0, THE MIXER's EFFECTS: each strip's four slots (XML "Effect" strip / slot / type / on / output / values,
    // only those loaded).
    std::vector<synth::mixer::FxSlot> mixerEffects = std::vector<synth::mixer::FxSlot> ((size_t) (synth::mixer::numStrips * synth::mixer::numFxSlots));
    synth::mixer::FxSlot& effect (int strip, int slot) { return mixerEffects[(size_t) (strip * synth::mixer::numFxSlots + slot)]; }
    const synth::mixer::FxSlot& effect (int strip, int slot) const { return mixerEffects[(size_t) (strip * synth::mixer::numFxSlots + slot)]; }
    // 0.23.0, ROUTE INSTRUMENT MICROPHONES: each piece's mics' channels (piece x numChannels + its mic's own channel;
    // mixer::routeAuto, routeNone or a mic channel; XML "MicRoute" piece / mic / to, only those not AUTO), and the
    // user channels' names (XML "UserChannel" index / name, only those named).
    std::vector<std::int8_t> micRoutes = std::vector<std::int8_t> ((size_t) (synth::mixer::numPieces * synth::mixer::numChannels), synth::mixer::routeAuto);
    std::int8_t& micRoute (int piece, int mic) { return micRoutes[(size_t) (piece * synth::mixer::numChannels + mic)]; }
    std::int8_t micRoute (int piece, int mic) const { return micRoutes[(size_t) (piece * synth::mixer::numChannels + mic)]; }
    std::vector<juce::String> userChannelNames = std::vector<juce::String> ((size_t) synth::mixer::numUserChannels);
    // 0.39.0: the buses' names (empty: BUS n). A fresh mixer's are the drum buses' (KICK, SNARE, TOMS); every save writes
    // all eight (XML "BusName" index / name), so one saved before 0.39.0 (none written) reads as unnamed.
    std::vector<juce::String> busNames = [] { std::vector<juce::String> n; for (const auto& b : synth::mixer::defaultBusNames) n.push_back (b); return n; }();

    bool operator== (const ModMatrix& o) const noexcept
    {
        return drumPatterns == o.drumPatterns && drumEnvelopes == o.drumEnvelopes && drumVelocityCurves == o.drumVelocityCurves
            && mixerBleed == o.mixerBleed && mixerSends == o.mixerSends && mixerEffects == o.mixerEffects
            && micRoutes == o.micRoutes && userChannelNames == o.userChannelNames && busNames == o.busNames;
    }
    bool isEmpty() const noexcept { return *this == ModMatrix {}; }

    // As XML (in presets and projects) and back: only what's changed from new.
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Modulation");
        for (int i = 0; i < (int) drumEnvelopes.size(); ++i)
            if (! (drumEnvelopes[(size_t) i] == ModShapeData::flatDrumEnvelope()))
            {
                auto* e = xml->createNewChildElement ("DrumEnvelope");
                e->setAttribute ("index", i + 1);
                e->setAttribute ("points", drumEnvelopes[(size_t) i].encode());
                e->setAttribute ("sustain", -1);
                e->setAttribute ("loopStart", -1);
                e->setAttribute ("loopEnd", -1);
            }
        for (int i = 0; i < (int) drumVelocityCurves.size(); ++i)
            if (! drumVelocityCurves[(size_t) i].isStraight())
            {
                auto* e = xml->createNewChildElement ("VelocityCurve");
                e->setAttribute ("index", i + 1);
                e->setAttribute ("curve", drumVelocityCurves[(size_t) i].encode());
            }
        for (int c = 0; c < synth::mixer::numChannels; ++c)
            for (int p = 0; p < synth::mixer::numPieces; ++p)
                if (const auto& cell = bleed (c, p); ! (cell == synth::mixer::BleedCell {}))
                {
                    auto* e = xml->createNewChildElement ("Bleed");
                    e->setAttribute ("channel", c + 1);
                    e->setAttribute ("piece", p + 1);
                    e->setAttribute ("gain", cell.gainDb);
                    e->setAttribute ("invert", cell.invert);
                    e->setAttribute ("on", cell.on);
                }
        for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
            for (int slot = 0; slot < synth::mixer::numSendSlots; ++slot)
                if (const auto& d = send (strip, slot); ! (d == synth::mixer::Send {}))
                {
                    auto* e = xml->createNewChildElement ("Send");
                    e->setAttribute ("strip", strip + 1);
                    e->setAttribute ("slot", slot + 1);
                    e->setAttribute ("target", d.target);
                    e->setAttribute ("direct", d.directDb);
                    e->setAttribute ("bleed", d.bleedDb);
                    e->setAttribute ("directPan", d.directPan);
                    e->setAttribute ("bleedPan", d.bleedPan);
                    e->setAttribute ("directOn", d.directOn);
                    e->setAttribute ("bleedOn", d.bleedOn);
                    e->setAttribute ("link", d.link);
                    e->setAttribute ("mode", (int) d.mode);
                }
        for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
            for (int slot = 0; slot < synth::mixer::numFxSlots; ++slot)
                if (const auto& fx = effect (strip, slot); fx.isLoaded())
                {
                    auto* e = xml->createNewChildElement ("Effect");
                    e->setAttribute ("strip", strip + 1);
                    e->setAttribute ("slot", slot + 1);
                    e->setAttribute ("type", fx.type);
                    e->setAttribute ("on", fx.on);
                    e->setAttribute ("output", fx.outputDb);
                    juce::StringArray values;
                    for (const auto v : fx.values)
                        values.add (juce::String (v, 4));
                    e->setAttribute ("values", values.joinIntoString (" "));
                }
        for (int piece = 0; piece < synth::mixer::numPieces; ++piece)
            for (int mic = 0; mic < synth::mixer::numChannels; ++mic)
                if (const auto to = micRoute (piece, mic); to != synth::mixer::routeAuto)
                {
                    auto* e = xml->createNewChildElement ("MicRoute");
                    e->setAttribute ("piece", piece + 1);
                    e->setAttribute ("mic", mic + 1);
                    e->setAttribute ("to", to == synth::mixer::routeNone ? 0 : to + 1);   // 0: none, else the channel + 1
                }
        for (int b = 0; b < synth::mixer::numBuses; ++b)
        {
            auto* e = xml->createNewChildElement ("BusName");
            e->setAttribute ("index", b + 1);
            e->setAttribute ("name", busNames[(size_t) b]);
        }
        for (int u = 0; u < synth::mixer::numUserChannels; ++u)
            if (userChannelNames[(size_t) u].isNotEmpty())
            {
                auto* e = xml->createNewChildElement ("UserChannel");
                e->setAttribute ("index", u + 1);
                e->setAttribute ("name", userChannelNames[(size_t) u]);
            }
        for (int p = 0; p < (int) drumPatterns.size(); ++p)
            if (! drumPatterns[(size_t) p].isEmpty())
            {
                auto* e = xml->createNewChildElement ("DrumPattern");
                e->setAttribute ("pattern", synth::drumseq::patternNames[p]);
                e->setAttribute ("notes", drumPatterns[(size_t) p].encode());
            }
        return xml;
    }

    // Anything else in it (a PR7-K preset's routes, LFO shapes, arp patterns...) is left out.
    static ModMatrix fromXml (const juce::XmlElement& xml)
    {
        ModMatrix m;
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumEnvelope"))
            if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < numDrumSlots)
            {
                auto d = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true);   // x within 0 to 1
                if (d.points.size() >= 2)
                    m.drumEnvelopes[(size_t) index] = d;
            }
        for (const auto* e : xml.getChildWithTagNameIterator ("Bleed"))
        {
            const auto c = e->getIntAttribute ("channel") - 1, p = e->getIntAttribute ("piece") - 1;
            if (c >= 0 && c < synth::mixer::numChannels && p >= 0 && p < synth::mixer::numPieces)
                m.bleed (c, p) = { (float) juce::jlimit (-60.0, 12.0, e->getDoubleAttribute ("gain")), e->getBoolAttribute ("invert"), e->getBoolAttribute ("on", true) };
        }
        for (const auto* e : xml.getChildWithTagNameIterator ("Send"))
        {
            const auto strip = e->getIntAttribute ("strip") - 1, slot = e->getIntAttribute ("slot") - 1;
            if (strip < 0 || strip >= synth::mixer::numStrips || slot < 0 || slot >= synth::mixer::numSendSlots)
                continue;
            auto& d = m.send (strip, slot);
            const auto level = [e] (const char* name) { return (float) juce::jlimit (-60.0, 12.0, e->getDoubleAttribute (name)); };
            const auto pan = [e] (const char* name) { return (float) juce::jlimit (-1.0, 1.0, e->getDoubleAttribute (name)); };
            d.target = juce::jlimit (0, synth::mixer::sendTargetNames.size() - 1, e->getIntAttribute ("target"));
            d.directDb = level ("direct");
            d.bleedDb = level ("bleed");
            d.directPan = pan ("directPan");
            d.bleedPan = pan ("bleedPan");
            d.directOn = e->getBoolAttribute ("directOn", true);
            d.bleedOn = e->getBoolAttribute ("bleedOn", true);
            d.link = e->getBoolAttribute ("link", true);
            d.mode = (synth::mixer::SendMode) juce::jlimit (0, synth::mixer::sendModeNames.size() - 1, e->getIntAttribute ("mode"));
        }
        for (const auto* e : xml.getChildWithTagNameIterator ("Effect"))
        {
            const auto strip = e->getIntAttribute ("strip") - 1, slot = e->getIntAttribute ("slot") - 1;
            if (strip < 0 || strip >= synth::mixer::numStrips || slot < 0 || slot >= synth::mixer::numFxSlots)
                continue;
            auto& fx = m.effect (strip, slot);
            fx.type = juce::jmax (0, e->getIntAttribute ("type"));   // (checked against the known types where it's used)
            fx.on = e->getBoolAttribute ("on", true);
            fx.outputDb = (float) juce::jlimit (-24.0, 12.0, e->getDoubleAttribute ("output"));
            const auto values = juce::StringArray::fromTokens (e->getStringAttribute ("values"), " ", "");
            for (int i = 0; i < values.size() && i < (int) fx.values.size(); ++i)
                fx.values[(size_t) i] = values[i].getFloatValue();
        }
        for (const auto* e : xml.getChildWithTagNameIterator ("MicRoute"))
        {
            const auto piece = e->getIntAttribute ("piece") - 1, mic = e->getIntAttribute ("mic") - 1, to = e->getIntAttribute ("to", -1);
            if (piece >= 0 && piece < synth::mixer::numPieces && mic >= 0 && mic < synth::mixer::numChannels && to >= 0 && to <= synth::mixer::numMicChannels)
                m.micRoute (piece, mic) = (std::int8_t) (to == 0 ? synth::mixer::routeNone : to - 1);
        }
        std::fill (m.busNames.begin(), m.busNames.end(), juce::String());
        for (const auto* e : xml.getChildWithTagNameIterator ("BusName"))
            if (const auto b = e->getIntAttribute ("index") - 1; b >= 0 && b < synth::mixer::numBuses)
                m.busNames[(size_t) b] = e->getStringAttribute ("name").substring (0, 24);
        for (const auto* e : xml.getChildWithTagNameIterator ("UserChannel"))
            if (const auto u = e->getIntAttribute ("index") - 1; u >= 0 && u < synth::mixer::numUserChannels)
                m.userChannelNames[(size_t) u] = e->getStringAttribute ("name").substring (0, 24);
        for (const auto* e : xml.getChildWithTagNameIterator ("VelocityCurve"))
            if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < numDrumSlots)
                m.drumVelocityCurves[(size_t) index] = synth::drums::VelocityCurve::decode (e->getStringAttribute ("curve"));
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumPattern"))
            if (const auto p = synth::drumseq::patternNames.indexOf (e->getStringAttribute ("pattern")); p >= 0)
                m.drumPatterns[(size_t) p] = synth::drumseq::Pattern::decode (e->getStringAttribute ("notes"));
        return m;
    }
};
