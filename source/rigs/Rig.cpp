#include "Rig.h"

#include "Parameters.h"

namespace
{
    // Bump when the file layout changes in a way older code can't read.
    constexpr int fileVersion = 1;

    // Which plugin version's parameters the preset was saved with, for the
    // occasional parameter whose default changes (older plugins ignore it).
    // PR7-D counts afresh from its siblings' numbering:
    // 1: PR7-D 0.1.0 (PR7-K 0.24.0's drum and effect parameters).
    // 2: PR7-D 0.17.0 (the articulations: older presets load with drums 21 to 23 off).
    // 3: PR7-D 0.33.0 (drums 24 to 29: older presets load with them off; their layers' instrument numbers moved).
    // 4: PR7-D 0.36.0 (drums 30 to 36, likewise).
    // 5: PR7-D 0.39.0 (the drum buses: older presets keep their channels' routes, their buses unnamed).
    constexpr int parameterVersion = 5;

    const juce::Identifier rigTag { "ParagonRig" };
    const juce::Identifier parameterTag { "Param" };
    const juce::Identifier drumSampleTag { "DrumSample" };
}

juce::String Rig::bankName (int bankIndex)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numBanks - 1, bankIndex)));
}

juce::String Rig::getSlotName() const
{
    return bankName (bank) + juce::String (number).paddedLeft ('0', 2);
}

std::unique_ptr<juce::XmlElement> Rig::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (rigTag);
    xml->setAttribute ("version", fileVersion);
    xml->setAttribute ("parameterVersion", parameterVersion);
    xml->setAttribute ("name", name);
    xml->setAttribute ("bank", bankName (bank));
    xml->setAttribute ("number", number);
    xml->setAttribute ("favourite", favourite);
    xml->setAttribute ("tags", tags.joinIntoString (","));
    if (notes.isNotEmpty())
        xml->setAttribute ("notes", notes);

    for (const auto& [id, value] : parameters)
    {
        auto* parameter = xml->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    for (const auto& [slot, sample] : drumSamples)
    {
        if (sample == nullptr || sample->length() < 2)
            continue;
        auto* element = xml->createNewChildElement (drumSampleTag);
        element->setAttribute ("slot", slot + 1);
        element->setAttribute ("name", sample->name);
        if (sample->path.isNotEmpty())
            element->setAttribute ("path", sample->path);
        element->addTextElement (synth::drums::sampleFiles::encode (*sample));
    }
    if (! modulation.isEmpty())
        xml->addChildElement (modulation.toXml().release());
    for (const auto& [slot, ref] : drumInstruments)
    {
        auto* e = xml->createNewChildElement ("DrumInstrument");
        e->setAttribute ("slot", slot + 1);
        e->setAttribute ("ref", ref);
    }
    return xml;
}

std::optional<Rig> Rig::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (rigTag) || xml.getIntAttribute ("version", 0) > fileVersion)
        return std::nullopt;

    Rig rig;
    rig.name = xml.getStringAttribute ("name", "Untitled").trim();
    if (rig.name.isEmpty())
        rig.name = "Untitled";

    const auto bankLetter = xml.getStringAttribute ("bank", "A").toUpperCase();
    rig.bank = bankLetter.isEmpty() ? 0 : juce::jlimit (0, numBanks - 1, (int) (bankLetter[0] - 'A'));
    rig.number = juce::jlimit (1, maxNumber, xml.getIntAttribute ("number", 1));
    rig.favourite = xml.getBoolAttribute ("favourite");
    rig.tags = juce::StringArray::fromTokens (xml.getStringAttribute ("tags"), ",", {});
    rig.tags.trim();
    rig.tags.removeEmptyStrings();
    rig.notes = xml.getStringAttribute ("notes");

    for (const auto* parameter : xml.getChildWithTagNameIterator (parameterTag))
        if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
            rig.parameters[id] = (float) parameter->getDoubleAttribute ("value");

    for (const auto* element : xml.getChildWithTagNameIterator (drumSampleTag))
    {
        const auto slot = element->getIntAttribute ("slot") - 1;
        if (slot >= 0 && slot < synth::drums::numSlots)
            if (auto sample = synth::drums::sampleFiles::decode (element->getStringAttribute ("name"), element->getAllSubText()))
            {
                std::const_pointer_cast<synth::drums::Sample> (sample)->path = element->getStringAttribute ("path");
                rig.drumSamples[slot] = std::move (sample);
            }
    }
    if (const auto* modulation = xml.getChildByName ("Modulation"))
        rig.modulation = ModMatrix::fromXml (*modulation);
    const auto version = xml.getIntAttribute ("parameterVersion", 1);
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumInstrument"))
        if (const auto slot = ParamIDs::instrumentIndexFrom (e->getIntAttribute ("slot") - 1, version);
            slot >= 0 && slot < 2 * synth::drums::numSlots)   // 0.29.0: then layers
            rig.drumInstruments[slot] = e->getStringAttribute ("ref");

    if (version < 2)
        for (const auto& id : ParamIDs::articulationsAddedOff)
            rig.parameters[id] = 0.0f;
    if (version < 3)
        for (const auto& id : ParamIDs::slotsAdded033Off)
            rig.parameters[id] = 0.0f;
    if (version < 4)
        for (const auto& id : ParamIDs::slotsAdded036Off)
            rig.parameters[id] = 0.0f;
    if (version < 5)
    {
        for (const auto& id : ParamIDs::routesOnBuses039)
            if (rig.parameters.find (id) == rig.parameters.end())
                rig.parameters[id] = 0.0f;
        std::fill (rig.modulation.busNames.begin(), rig.modulation.busNames.end(), juce::String());
    }

    return rig;
}
