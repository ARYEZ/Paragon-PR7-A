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
            take->loudness = Take::loudnessOf (take->head, take->sampleRate);
            library->add (id->articulation, id->string, id->fret, id->layer, id->stroke, id->roundRobin, take);
        }
        if (progress != nullptr)
            *progress = 1.0f;
        return library;
    }

    float Take::loudnessOf (const std::vector<float>& head, double sampleRate)
    {
        const auto n = std::min (head.size(), (size_t) std::lround (0.1 * sampleRate));
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i)
            sum += (double) head[i] * head[i];
        return n > 0 ? (float) std::sqrt (sum / (double) n) : 0.0f;
    }

    const Take* Library::exact (Articulation a, int s, int f, int l, Stroke stroke, int rr) const noexcept
    {
        if (s < 0 || s >= numStrings || f < 0 || f > numFrets || l < 0 || l >= renders::spec (a).layers || rr < 0 || rr >= renders::spec (a).roundRobins)
            return nullptr;
        return takes[(size_t) a][(size_t) s][(size_t) f][(size_t) l][stroke == Stroke::down ? 0u : 1u][(size_t) rr].get();
    }

    void Library::add (Articulation a, int string, int fret, int layer, Stroke stroke, int roundRobin, std::shared_ptr<const Take> take)
    {
        if (take != nullptr && take->loudness <= 0.0f)
        {
            auto measured = std::make_shared<Take> (*take);   // (takes made in memory: measured here)
            measured->loudness = Take::loudnessOf (measured->head, measured->sampleRate);
            take = measured;
        }
        takes[(size_t) a][(size_t) string][(size_t) fret][(size_t) layer][stroke == Stroke::down ? 0u : 1u]
             [(size_t) juce::jlimit (0, renders::maxRoundRobins - 1, roundRobin)] = std::move (take);
    }

    const Take* Library::at (int a, int s, int f, int l, int k, int rr) const noexcept
    {
        // The round-robin asked for, else another of the same stroke.
        const auto& rrs = takes[(size_t) a][(size_t) s][(size_t) f][(size_t) l][(size_t) k];
        const auto n = renders::spec ((Articulation) a).roundRobins;
        for (int i = 0; i < n; ++i)
            if (const auto* t = rrs[(size_t) ((rr + i) % n)].get())
                return t;
        return nullptr;
    }

    const Take* Library::anyLayer (int a, int s, int f, int layer, int stroke, int rr) const noexcept
    {
        // This dynamic, then nearer ones; each the stroke asked for first.
        const auto layers = renders::spec ((Articulation) a).layers;
        for (int d = 0; d < layers; ++d)
            for (const auto l : { layer - d, layer + d })
                if (l >= 0 && l < layers)
                {
                    if (const auto* t = at (a, s, f, l, stroke, rr)) return t;
                    if (const auto* t = at (a, s, f, l, 1 - stroke, rr)) return t;
                }
        return nullptr;
    }

    Library::Found Library::find (Articulation articulation, int string, int fret, int layer, Stroke stroke, int rr) const noexcept
    {
        const auto a = (int) articulation, k = stroke == Stroke::down ? 0 : 1;
        layer = juce::jlimit (0, renders::spec (articulation).layers - 1, layer);
        if (string < 0 || string >= numStrings || fret < 0 || fret > numFrets)
            return {};
        if (const auto* t = at (a, string, fret, layer, k, rr))
            return { t, 1.0, true };
        if (const auto* t = anyLayer (a, string, fret, layer, k, rr))
            return { t, 1.0, false };
        for (int d = 1; d <= numFrets; ++d)
            for (const auto f : { fret - d, fret + d })
                if (f >= 0 && f <= numFrets)
                    if (const auto* t = anyLayer (a, string, f, layer, k, rr))
                        return { t, std::pow (2.0, (fret - f) / 12.0), false };
        const auto note = noteOf (string, fret);
        for (int s = 0; s < numStrings; ++s)
            if (const auto f = note - openNotes[(size_t) s]; s != string && f >= 0 && f <= numFrets)
                if (const auto* t = anyLayer (a, s, f, layer, k, rr))
                    return { t, 1.0, false };
        return {};
    }

    int Library::count (Articulation a, int string) const noexcept
    {
        auto n = 0;
        for (const auto& layers : takes[(size_t) a][(size_t) string])
            for (const auto& strokes : layers)
                for (const auto& rrs : strokes)
                    for (const auto& t : rrs)
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
