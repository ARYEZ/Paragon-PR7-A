// LAYERS (PR7-D 0.50.0): what each of a drum's layers does beside its LEVEL and OUT. Its own header: the kit (Drums.h)
// and the preset data (ModMatrix.h) both use it.
#pragma once

namespace synth::drums
{
    // LAYERS (0.50.0): up to three of the user's own sounds played with each of a drum's hits (reinforcing a kit: a
    // render of another kit's snare over this one's). Beside each layer's LEVEL and OUT (parameters), its settings are
    // preset data (ModMatrix::drumLayers): PITCH (semitones), OFFSET (ms: later, or earlier by skipping into it),
    // POLARITY, the velocities it plays on (0-127, as the hit came in), and LENGTH (ms, 0: whole; else it fades there).
    constexpr int numLayers = 3;
    struct LayerTweak
    {
        float pitch = 0.0f, offsetMs = 0.0f, lengthMs = 0.0f;
        bool invert = false;
        int velLo = 0, velHi = 127;
        bool operator== (const LayerTweak&) const = default;
        bool isDefault() const noexcept { return *this == LayerTweak {}; }
    };
}
