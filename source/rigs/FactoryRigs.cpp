#include "FactoryRigs.h"

#include "Parameters.h"

// PR7-I's factory presets (0.2.0): ways of playing the guitar, not sounds (the sound is the guitar folder's takes, and
// the owner's amp on PR7-G after it). Our own starting points for djent and progressive metal parts.
namespace
{
    struct Builder
    {
        Rig rig;
        Builder (const juce::String& name, int number, const juce::StringArray& tags, const juce::String& notes)
        {
            rig.name = name;
            rig.bank = 0;
            rig.number = number;
            rig.tags = tags;
            rig.notes = notes;
        }
        Builder& set (const juce::String& id, float value) { rig.parameters[id] = value; return *this; }
    };
}

std::vector<Rig> makeFactoryRigs()
{
    using namespace ParamIDs;
    std::vector<Rig> rigs;
    rigs.push_back (Builder ("Init", 1, { "Init" }, "Every setting at its default.").rig);
    rigs.push_back (Builder ("Tight Rhythm", 2, { "Rhythm", "Djent" },
                             "Low on the neck, a quick release so stopped notes stop dead, and a steadier level for chugs.")
                        .set (release, 0.03f).set (velocityToVolume, 0.25f).rig);
    rigs.push_back (Builder ("Chords", 3, { "Chords" }, "POLYPHONIC fretting: each note of a chord on its own string.")
                        .set (fretMode, 1.0f).set (release, 0.12f).rig);
    rigs.push_back (Builder ("Lead Up The Neck", 4, { "Lead" }, "The hand held at the 12th fret, for leads up the neck.")
                        .set (handPosition, 12.0f).set (velocityToVolume, 0.7f).set (release, 0.15f).rig);
    rigs.push_back (Builder ("Down Picked", 5, { "Rhythm" }, "Every stroke a down stroke, for driving riffs.")
                        .set (pickMode, 1.0f).set (release, 0.05f).rig);
    return rigs;
}
