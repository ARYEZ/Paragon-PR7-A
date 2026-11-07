#include "guitar/GuitarLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace guitar
{
    std::shared_ptr<const Library> Library::load (const juce::File& folder, std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        auto library = std::make_shared<Library>();
        library->folder = folder;
        juce::Array<juce::File> files;
        for (const auto* sub : { "Sustain", "Mute" })
            files.addArray (folder.getChildFile (sub).findChildFiles (juce::File::findFiles, false, "*.wav"));
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (int i = 0; i < files.size(); ++i)
        {
            if (cancel != nullptr && cancel->load())
                break;
            if (progress != nullptr)
                *progress = (float) i / (float) files.size();
            const auto id = renders::parseTakeFile (files[i]);
            if (! id)
                continue;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (files[i]));
            if (reader == nullptr || reader->lengthInSamples <= 0)
                continue;
            auto take = std::make_shared<Take>();
            take->file = files[i];
            take->sampleRate = reader->sampleRate;
            take->frames = reader->lengthInSamples;
            const auto headFrames = (int) std::min<juce::int64> (take->frames, (juce::int64) std::ceil (headSeconds * reader->sampleRate));
            juce::AudioBuffer<float> head ((int) juce::jmax (1u, reader->numChannels), headFrames);
            reader->read (&head, 0, headFrames, 0, true, true);
            take->head.assign (head.getReadPointer (0), head.getReadPointer (0) + headFrames);
            if (head.getNumChannels() > 1)
                for (int k = 0; k < headFrames; ++k)
                    take->head[(size_t) k] = 0.5f * (take->head[(size_t) k] + head.getSample (1, k));
            if (take->frames > headFrames)
            {
                auto source = std::make_shared<StreamSource>();
                source->file = files[i];
                source->sampleRate = reader->sampleRate;
                source->length = take->frames;
                source->headFrames = headFrames;
                source->numOutputs = 1;
                source->parts.push_back ({ 0, 0, 0, reader->numChannels > 1 ? 0.5f : 1.0f, 0 });
                if (reader->numChannels > 1)
                    source->parts.push_back ({ 1, 0, 0, 0.5f, 0 });
                take->stream = source;
            }
            library->add (id->articulation, id->string, id->fret, id->layer, id->stroke, take);
        }
        if (progress != nullptr)
            *progress = 1.0f;
        return library;
    }

    void Library::add (Articulation a, int string, int fret, int layer, Stroke stroke, std::shared_ptr<const Take> take)
    {
        takes[(size_t) a][(size_t) string][(size_t) fret][(size_t) layer][stroke == Stroke::down ? 0u : 1u] = std::move (take);
    }

    const Take* Library::anyLayer (int a, int s, int f, int layer, int stroke) const noexcept
    {
        // This dynamic, then nearer ones; each the stroke asked for first.
        for (int d = 0; d < renders::numLayers; ++d)
            for (const auto l : { layer - d, layer + d })
                if (l >= 0 && l < renders::numLayers)
                {
                    if (const auto* t = at (a, s, f, l, stroke)) return t;
                    if (const auto* t = at (a, s, f, l, 1 - stroke)) return t;
                }
        return nullptr;
    }

    Library::Found Library::find (Articulation articulation, int string, int fret, int layer, Stroke stroke) const noexcept
    {
        const auto a = (int) articulation, k = stroke == Stroke::down ? 0 : 1;
        layer = juce::jlimit (0, renders::numLayers - 1, layer);
        if (string < 0 || string >= numStrings || fret < 0 || fret > numFrets)
            return {};
        if (const auto* t = at (a, string, fret, layer, k))
            return { t, 1.0, true };
        if (const auto* t = anyLayer (a, string, fret, layer, k))
            return { t, 1.0, false };
        for (int d = 1; d <= numFrets; ++d)
            for (const auto f : { fret - d, fret + d })
                if (f >= 0 && f <= numFrets)
                    if (const auto* t = anyLayer (a, string, f, layer, k))
                        return { t, std::pow (2.0, (fret - f) / 12.0), false };
        const auto note = noteOf (string, fret);
        for (int s = 0; s < numStrings; ++s)
            if (const auto f = note - openNotes[(size_t) s]; s != string && f >= 0 && f <= numFrets)
                if (const auto* t = anyLayer (a, s, f, layer, k))
                    return { t, 1.0, false };
        return {};
    }

    int Library::count (Articulation a, int string) const noexcept
    {
        auto n = 0;
        for (const auto& layers : takes[(size_t) a][(size_t) string])
            for (const auto& strokes : layers)
                for (const auto& t : strokes)
                    n += t != nullptr ? 1 : 0;
        return n;
    }

    int Library::total() const noexcept
    {
        auto n = 0;
        for (int a = 0; a < numArticulations; ++a)
            for (int s = 0; s < numStrings; ++s)
                n += count ((Articulation) a, s);
        return n;
    }
}
