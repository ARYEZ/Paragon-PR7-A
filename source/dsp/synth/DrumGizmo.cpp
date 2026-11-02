#include "dsp/synth/DrumGizmo.h"

#include "dsp/synth/NoteMap.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace synth::drums::drumgizmo
{
    namespace
    {
        // Lower case, spaces, dashes and underscores out ("OH Left" -> "ohleft").
        juce::String compact (const juce::String& s)
        {
            return s.toLowerCase().removeCharacters (" _-.");
        }

        std::unique_ptr<juce::XmlElement> parse (const juce::File& f)
        {
            if (! f.existsAsFile())
                return {};
            return juce::XmlDocument::parse (f);
        }

        // The midimap beside a kit file: "Midimap_<suffix>.xml" for "<Kit>_<suffix>.xml", else any midimap there.
        juce::File midimapFor (const juce::File& kitFile)
        {
            const auto dir = kitFile.getParentDirectory();
            const auto name = kitFile.getFileNameWithoutExtension();
            if (name.contains ("_"))
            {
                const auto suffix = name.fromLastOccurrenceOf ("_", false, false);
                for (const auto* stem : { "Midimap_", "midimap_", "MidiMap_" })
                    if (const auto f = dir.getChildFile (juce::String (stem) + suffix + ".xml"); f.existsAsFile())
                        return f;
            }
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
                if (f.getFileName().toLowerCase().startsWith ("midimap"))
                    return f;
            return {};
        }

        bool hasAny (const juce::String& s, std::initializer_list<const char*> words)
        {
            for (const auto* w : words)
                if (s.contains (w))
                    return true;
            return false;
        }
    }

    bool isKit (const juce::File& f)
    {
        if (! f.hasFileExtension ("xml"))
            return false;
        const auto xml = parse (f);
        return xml != nullptr && xml->hasTagName ("drumkit");
    }

    KitInfo read (const juce::File& kitFile)
    {
        KitInfo info;
        const auto xml = parse (kitFile);
        if (xml == nullptr || ! xml->hasTagName ("drumkit"))
            return info;
        info.name = xml->getStringAttribute ("name", kitFile.getFileNameWithoutExtension());
        info.description = xml->getStringAttribute ("description");
        info.sampleRate = xml->getDoubleAttribute ("samplerate", 48000.0);
        if (const auto* channels = xml->getChildByName ("channels"))
            for (const auto* c : channels->getChildWithTagNameIterator ("channel"))
                info.channels.add (c->getStringAttribute ("name"));
        std::map<juce::String, int> notes;
        if (const auto map = parse (midimapFor (kitFile)))
            for (const auto* m : map->getChildWithTagNameIterator ("map"))
            {
                const auto instr = m->getStringAttribute ("instr");
                const auto note = m->getIntAttribute ("note", -1);
                if (note >= 0 && note < 128 && notes.find (instr) == notes.end())
                    notes[instr] = note;
            }
        if (const auto* instruments = xml->getChildByName ("instruments"))
            for (const auto* i : instruments->getChildWithTagNameIterator ("instrument"))
            {
                KitInfo::Entry e;
                e.name = i->getStringAttribute ("name");
                e.file = kitFile.getParentDirectory().getChildFile (i->getStringAttribute ("file"));
                if (const auto it = notes.find (e.name); it != notes.end())
                    e.note = it->second;
                if (e.name.isNotEmpty())
                    info.instruments.push_back (e);
            }
        return info;
    }

    MicPlace micFor (const juce::String& kitChannel)
    {
        const auto c = compact (kitChannel);
        const auto sideOf = [&c]
        {
            if (c.endsWith ("left")) return -1;
            if (c.endsWith ("right")) return 1;
            if (c.endsWith ("center") || c.endsWith ("centre") || c.endsWith ("mid") || c.endsWith ("mono")) return 0;
            if (c.endsWith ("l")) return -1;
            if (c.endsWith ("r")) return 1;
            return 0;
        };
        if (hasAny (c, { "amb", "room" }))
            return { mixer::room, sideOf() };
        if (c.startsWith ("oh") || hasAny (c, { "overhead" }))
            return { mixer::overheads, sideOf() };
        if (hasAny (c, { "kdrum", "kick", "bassdrum" }) || c.startsWith ("bd"))
            return { hasAny (c, { "out", "front", "sub" }) ? mixer::kickOut : mixer::kickIn, 0 };
        if (hasAny (c, { "snare" }) || c.startsWith ("sn") || c.startsWith ("sd"))
            return { hasAny (c, { "bot", "under" }) ? mixer::snareBottom : mixer::snareTop, 0 };
        if (hasAny (c, { "hihat", "hat" }) || c.startsWith ("hh"))
            return { mixer::hiHat, 0 };
        if (hasAny (c, { "ftom2", "floortom2", "floor2" }))
            return { mixer::tom3, 0 };
        if (hasAny (c, { "ftom", "floor" }))
            return { mixer::tom2, 0 };
        if (hasAny (c, { "tom" }))
            return { mixer::tom1, 0 };
        if (hasAny (c, { "ride", "crash", "china", "splash", "cym" }))
            return { mixer::cymbals, 0 };
        const auto named = mixer::channelForName (kitChannel);
        return { named >= 0 ? named : mixer::other, 0 };
    }

    namespace
    {
        struct HitSource
        {
            float power = 0.0f;
            struct File { juce::String channel; juce::File file; int fileChannel = 1; };
            std::vector<File> files;
        };

        // An instrument file's hits.
        std::vector<HitSource> hitsOf (const juce::File& file, juce::StringArray& problems)
        {
            std::vector<HitSource> hits;
            const auto xml = parse (file);
            if (xml == nullptr)
            {
                problems.add ("couldn't read " + file.getFullPathName());
                return hits;
            }
            const auto* samples = xml->getChildByName ("samples");
            if (samples == nullptr)
                return hits;
            for (const auto* s : samples->getChildWithTagNameIterator ("sample"))
            {
                HitSource h;
                h.power = (float) s->getDoubleAttribute ("power", 0.0);
                for (const auto* a : s->getChildWithTagNameIterator ("audiofile"))
                    h.files.push_back ({ a->getStringAttribute ("channel"), file.getParentDirectory().getChildFile (a->getStringAttribute ("file")),
                                         a->getIntAttribute ("filechannel", 1) });
                if (! h.files.empty())
                    hits.push_back (std::move (h));
            }
            return hits;
        }

        // The last sample above `threshold` (+1), at least 1.
        int soundingLength (const float* x, int n, float threshold)
        {
            for (int i = n - 1; i >= 0; --i)
                if (std::abs (x[i]) > threshold)
                    return i + 1;
            return 1;
        }

        void fadeEnd (std::vector<float>& x, int fadeSamples)
        {
            const auto n = (int) x.size();
            fadeSamples = juce::jmin (fadeSamples, n);
            for (int i = 0; i < fadeSamples; ++i)
            {
                const auto g = (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) (i + 1) / (double) fadeSamples));
                x[(size_t) (n - fadeSamples + i)] *= g;
            }
        }

        // One hit: its file read once, its mics gathered into the mixer's channels, trimmed.
        bool makeHit (const HitSource& source, const Trim& trim, juce::AudioFormatManager& formats, Instrument::Hit& hit,
                      juce::StringArray& problems)
        {
            // Every file this hit uses (usually one, a channel per mic). STREAMED (0.28.0): a hit from one file holds
            // only its head here; the rest is read from the file while it plays.
            std::map<juce::String, std::unique_ptr<juce::AudioBuffer<float>>> buffers;
            double rate = 48000.0;
            auto streaming = trim.stream && ! source.files.empty()
                             && std::all_of (source.files.begin(), source.files.end(), [&] (const HitSource::File& f) { return f.file == source.files.front().file; });
            std::shared_ptr<StreamSource> stream;
            if (streaming)
            {
                stream = std::make_shared<StreamSource>();
                stream->file = source.files.front().file;
            }
            for (const auto& f : source.files)
            {
                const auto key = f.file.getFullPathName();
                if (buffers.count (key) != 0)
                    continue;
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f.file));
                if (reader == nullptr)
                {
                    problems.add ("couldn't read " + key);
                    buffers[key] = nullptr;
                    continue;
                }
                rate = reader->sampleRate;
                const auto frames = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                   (juce::int64) ((streaming ? trim.headSeconds : trim.maxSeconds) * rate));
                if (stream != nullptr)
                {
                    stream->sampleRate = rate;
                    stream->length = reader->lengthInSamples;
                    stream->headFrames = frames;
                }
                auto b = std::make_unique<juce::AudioBuffer<float>> ((int) reader->numChannels, juce::jmax (1, frames));
                b->clear();
                reader->read (b.get(), 0, frames, 0, true, true);
                buffers[key] = std::move (b);
            }

            // The mics by mixer channel: each source's samples and side.
            struct Source { const float* data; int length; int side; int fileChannel; };
            std::map<int, std::vector<Source>> byChannel;
            float hitPeak = 0.0f;
            for (const auto& f : source.files)
            {
                const auto& b = buffers[f.file.getFullPathName()];
                if (b == nullptr || f.fileChannel < 1 || f.fileChannel > b->getNumChannels())
                    continue;
                const auto place = micFor (f.channel);
                const auto* data = b->getReadPointer (f.fileChannel - 1);
                byChannel[place.channel].push_back ({ data, b->getNumSamples(), place.side, f.fileChannel - 1 });
                for (int i = 0; i < b->getNumSamples(); ++i)
                    hitPeak = juce::jmax (hitPeak, std::abs (data[i]));
            }
            if (hitPeak <= 0.0f)
                return false;

            // Its own close mic: the loudest that isn't over the whole kit (and its partner: kick in / out, snare top /
            // bottom). Those and the whole-kit mics keep their length; the rest (bleed) are cut shorter.
            int own = -1;
            float ownPeak = 0.0f;
            for (const auto& [channel, sources] : byChannel)
            {
                if (mixer::hearsWholeKit (channel))
                    continue;
                float p = 0.0f;
                for (const auto& s : sources)
                    for (int i = 0; i < s.length; ++i)
                        p = juce::jmax (p, std::abs (s.data[i]));
                if (p > ownPeak)
                {
                    ownPeak = p;
                    own = channel;
                }
            }
            const auto isOwn = [own] (int channel)
            {
                if (mixer::hearsWholeKit (channel) || channel == own)
                    return true;
                const auto kick = [] (int c) { return c == mixer::kickIn || c == mixer::kickOut; };
                const auto snare = [] (int c) { return c == mixer::snareTop || c == mixer::snareBottom; };
                return (kick (own) && kick (channel)) || (snare (own) && snare (channel));
            };

            auto mix = std::make_shared<Sample>();
            mix->sampleRate = rate;
            mix->channels.assign (2, {});
            const auto mixLength = (int) (2.0 * rate);   // the whole kit's mix of it: its first 2 s (pictures, levels)
            for (const auto& [channel, sources] : byChannel)
            {
                const auto stereo = std::any_of (sources.begin(), sources.end(), [] (const Source& s) { return s.side != 0; });
                auto length = 0;
                for (const auto& s : sources)
                    length = juce::jmax (length, s.length);
                auto cap = streaming ? length : (int) ((isOwn (channel) ? trim.maxSeconds : trim.bleedSeconds) * rate);
                const auto cut = length > cap;
                length = juce::jmin (length, cap);
                auto sample = std::make_shared<Sample>();
                sample->sampleRate = rate;
                sample->channels.assign (stereo ? 2 : 1, std::vector<float> ((size_t) juce::jmax (1, length), 0.0f));
                for (const auto& s : sources)
                {
                    const auto n = juce::jmin (s.length, length);
                    if (! stereo)
                    {
                        for (int i = 0; i < n; ++i)
                            sample->channels[0][(size_t) i] += s.data[i];
                        continue;
                    }
                    const auto gl = s.side > 0 ? 0.0f : s.side < 0 ? 1.0f : 0.7071f;
                    const auto gr = s.side < 0 ? 0.0f : s.side > 0 ? 1.0f : 0.7071f;
                    for (int i = 0; i < n; ++i)
                    {
                        sample->channels[0][(size_t) i] += s.data[i] * gl;
                        sample->channels[1][(size_t) i] += s.data[i] * gr;
                    }
                }
                // A mic that hardly hears it (60 dB under the hit's loudest) left out; the rest's silence after the
                // sound trimmed, and a cut tail faded.
                float peak = 0.0f;
                for (const auto& c : sample->channels)
                    for (const auto v : c)
                        peak = juce::jmax (peak, std::abs (v));
                if (! trim.full && peak < hitPeak * 0.001f)
                    continue;
                if (stream != nullptr)
                {
                    // Streamed: the head as it is (the rest follows from the file), and how the file makes this mic.
                    const auto output = (int) hit.mics.size();
                    for (const auto& src : sources)
                    {
                        if (! stereo)
                            stream->parts.push_back ({ src.fileChannel, output, 0, 1.0f });
                        else
                        {
                            if (src.side <= 0)
                                stream->parts.push_back ({ src.fileChannel, output, 0, src.side < 0 ? 1.0f : 0.7071f });
                            if (src.side >= 0)
                                stream->parts.push_back ({ src.fileChannel, output, 1, src.side > 0 ? 1.0f : 0.7071f });
                        }
                    }
                    sample->streamOutput = output;
                }
                int sounding = 1;
                for (const auto& c : sample->channels)
                    sounding = juce::jmax (sounding, soundingLength (c.data(), (int) c.size(), juce::jmax (1.0e-6f, peak * 1.0e-4f)));
                if (stream != nullptr)
                    sounding = length;   // no trimming: its end comes from the file
                for (auto& c : sample->channels)
                {
                    if (stream != nullptr)
                        break;
                    c.resize ((size_t) sounding);
                    fadeEnd (c, cut && sounding == length ? juce::jmin ((int) (0.25 * rate), sounding / 5) : juce::jmin ((int) (0.005 * rate), sounding));
                }
                for (int side = 0; side < 2; ++side)
                {
                    const auto& from = sample->channels[(size_t) juce::jmin (side, (int) sample->channels.size() - 1)];
                    auto& to = mix->channels[(size_t) side];
                    const auto n = juce::jmin ((int) from.size(), mixLength);
                    if ((int) to.size() < n)
                        to.resize ((size_t) n, 0.0f);
                    for (int i = 0; i < n; ++i)
                        to[(size_t) i] += from[(size_t) i] * (sample->channels.size() == 1 ? 0.7071f : 1.0f);
                }
                hit.mics.push_back ({ channel, std::move (sample) });
            }
            if (hit.mics.empty())
                return false;
            if (stream != nullptr)
            {
                stream->numOutputs = (int) hit.mics.size();
                for (auto& m : hit.mics)
                    const_cast<Sample&> (*m.sample).stream = stream;   // made just above, held nowhere else
            }
            const auto mixSize = juce::jmax (mix->channels[0].size(), mix->channels[1].size());
            for (auto& c : mix->channels)
                c.resize (mixSize, 0.0f);
            hit.sample = std::move (mix);
            for (const auto& f : source.files)
                hit.files.addIfNotAlreadyThere (f.file.getFullPathName());
            return true;
        }
    }

    std::shared_ptr<const Instrument> load (const juce::File& kitFile, const juce::StringArray& names, const Trim& trim,
                                            juce::StringArray& problems)
    {
        const auto info = read (kitFile);
        if (! info.valid())
        {
            problems.add ("not a DrumGizmo kit: " + kitFile.getFullPathName());
            return {};
        }
        std::vector<HitSource> all;
        for (const auto& name : names)
        {
            const auto it = std::find_if (info.instruments.begin(), info.instruments.end(), [&name] (const KitInfo::Entry& e) { return e.name == name; });
            if (it == info.instruments.end())
            {
                problems.add ("no instrument \"" + name + "\" in " + kitFile.getFileName());
                continue;
            }
            auto hits = hitsOf (it->file, problems);
            all.insert (all.end(), std::make_move_iterator (hits.begin()), std::make_move_iterator (hits.end()));
        }
        if (all.empty())
            return {};
        std::stable_sort (all.begin(), all.end(), [] (const HitSource& a, const HitSource& b) { return a.power < b.power; });

        // LIGHT: a cymbal's long hits take most of the memory, so cymbals get 6 layers (of 2 hits) and 5 s, not 8 and 4.5.
        auto use = trim;
        if (! trim.full)
        {
            const auto joined = compact (names.joinIntoString (" "));
            if (hasAny (joined, { "ride", "crash", "china", "splash", "cymbal" }))
            {
                use.maxLayers = juce::jmin (use.maxLayers, 6);
                use.maxSeconds = 5.0;
            }
        }
        // Layers: the hits, softest first, shared out evenly; each layer's chosen hits spread across its share.
        const auto n = (int) all.size();
        const auto numLayers = juce::jlimit (1, n, use.maxLayers);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto inst = std::make_shared<Instrument>();
        inst->name = names.joinIntoString (" + ");
        inst->kind = Instrument::Kind::drumGizmo;
        inst->source = kitFile;
        std::vector<double> centres;
        for (int k = 0; k < numLayers; ++k)
        {
            const auto from = k * n / numLayers, to = (k + 1) * n / numLayers;
            const auto count = to - from;
            const auto take = juce::jmin (count, juce::jmax (1, use.hitsPerLayer));
            Instrument::Layer layer;
            double logPower = 0.0;
            for (int j = 0; j < take; ++j)
            {
                const auto index = from + (take == 1 ? count / 2 : j * (count - 1) / (take - 1));
                logPower += std::log (juce::jmax (1.0e-9, (double) all[(size_t) index].power));
                Instrument::Hit hit;
                if (makeHit (all[(size_t) index], use, formats, hit, problems))
                    layer.hits.push_back (std::move (hit));
            }
            if (layer.hits.empty())
                continue;
            centres.push_back (logPower / take);
            inst->layers.push_back (std::move (layer));
        }
        if (inst->layers.empty())
            return {};

        // Its level: kits are recorded with headroom (the CrocellKit's hardest hits peak at -12 to -16 dBFS in their
        // loudest mic), so each drum is brought up until its hardest hit's whole-kit mix peaks at -8 dBFS (at most
        // +18 dB), every mic by the same gain, so its bleed and its close mics stay as recorded. We made these samples
        // just now and nothing else holds them yet, so they're scaled in place.
        {
            float loudest = 0.0f;
            for (const auto& layer : inst->layers)
                for (const auto& h : layer.hits)
                    for (const auto& c : h.sample->channels)
                        for (const auto v : c)
                            loudest = juce::jmax (loudest, std::abs (v));
            if (loudest > 0.0f)
            {
                const auto gain = juce::jmin (juce::Decibels::decibelsToGain (18.0f), juce::Decibels::decibelsToGain (-8.0f) / loudest);
                const auto scale = [gain] (const std::shared_ptr<const Sample>& sample)
                {
                    for (auto& c : const_cast<Sample&> (*sample).channels)
                        for (auto& v : c)
                            v *= gain;
                };
                for (const auto& layer : inst->layers)
                    for (const auto& h : layer.hits)
                    {
                        scale (h.sample);
                        for (const auto& m : h.mics)
                            scale (m.sample);
                        // A streamed hit's file is read at the same gain (each hit's recipe once: its mics share it).
                        if (! h.mics.empty() && h.mics.front().sample->stream != nullptr)
                            for (auto& part : const_cast<StreamSource&> (*h.mics.front().sample->stream).parts)
                                part.gain *= gain;
                    }
            }
        }

        // Velocities from power: each layer centred where its hits' power sits between the softest and hardest (log
        // scale), split halfway between neighbours; evenly if the powers don't spread.
        const auto L = (int) inst->layers.size();
        const auto span = centres.back() - centres.front();
        std::vector<double> at ((size_t) L);
        auto even = span <= 1.0e-6;
        for (int k = 0; k < L && ! even; ++k)
        {
            at[(size_t) k] = 1.0 + 126.0 * (centres[(size_t) k] - centres.front()) / span;
            even = k > 0 && at[(size_t) k] - at[(size_t) k - 1] < 1.0;
        }
        for (int k = 0; k < L; ++k)
        {
            auto& layer = inst->layers[(size_t) k];
            if (even)
            {
                layer.lo = 1 + k * 127 / L;
                layer.hi = (k + 1) * 127 / L;
            }
            else
            {
                layer.lo = k == 0 ? 1 : (int) std::floor (0.5 * (at[(size_t) k - 1] + at[(size_t) k])) + 1;
                layer.hi = k == L - 1 ? 127 : (int) std::floor (0.5 * (at[(size_t) k] + at[(size_t) k + 1]));
            }
            double sum = 0.0;
            for (const auto& h : layer.hits)
                sum += instruments::loudness (*h.sample);
            layer.level = (float) (sum / (double) layer.hits.size());
        }
        return inst;
    }

    Assignment assign (const KitInfo& info)
    {
        using namespace notemap;
        Assignment result;
        // Each instrument's kind: the slots it may take, in order of preference (empty: left out).
        const auto slotsFor = [] (const juce::String& name) -> std::vector<int>
        {
            const auto c = compact (name);
            if (hasAny (c, { "kdrum", "kick", "bassdrum" }) || c.startsWith ("bd"))
                return { BD };
            if (hasAny (c, { "snare" }) || c.startsWith ("sd"))
            {
                if (hasAny (c, { "rest", "flam", "roll", "buzz", "ghost", "drag", "off" }))
                    return {};
                if (hasAny (c, { "rimshot" }))
                    return { SD2 };
                if (hasAny (c, { "rim", "stick", "xstick" }))
                    return { RIM };
                return { SD };
            }
            if (hasAny (c, { "hihat", "hat" }) || c.startsWith ("hh"))
            {
                if (hasAny (c, { "pedalhit", "splash" }))
                    return {};
                if (hasAny (c, { "nopedal", "loose" }))
                    return { HO1, HH };
                if (hasAny (c, { "pedal", "foot", "chick" }))
                    return { PH };
                if (hasAny (c, { "semi", "half" }))
                    return { HH, HO1, HO3 };
                if (hasAny (c, { "open" }))
                    return { OH, HO3, HH };
                return { CH };
            }
            if (hasAny (c, { "ride" }))
            {
                if (hasAny (c, { "bell" }))
                    return { RB };
                if (hasAny (c, { "edge", "crash" }))
                    return { RE };
                return { RD, RE };
            }
            if (hasAny (c, { "china" }))
                return { CHN };
            if (hasAny (c, { "splash" }))
                return { SPL };
            if (hasAny (c, { "crash", "cymbal" }))
            {
                if (hasAny (c, { "stop", "choke", "mute", "grab" }))
                    return {};
                return { CY, CR2 };
            }
            if (hasAny (c, { "ftom", "floor" }))
                return { LT, MT, HT };
            if (hasAny (c, { "tom" }))
                return { HT, MT, LT };
            if (hasAny (c, { "clap" }))
                return { CLP };
            if (hasAny (c, { "tamb" }))
                return { TMB };
            if (hasAny (c, { "shaker", "shake" }))
                return { SHK };
            return { -2 };   // unknown: by its note, if General MIDI has a slot for it
        };

        const auto gm = generalMidi();
        std::map<int, juce::String> taken;
        std::vector<std::pair<const KitInfo::Entry*, std::vector<int>>> later;
        // First every instrument whose note (General MIDI) is one of its kind's slots; then the rest, in turn, into
        // their kind's free slots.
        for (const auto& e : info.instruments)
        {
            auto allowed = slotsFor (e.name);
            if (allowed.empty())
            {
                result.unused.add (e.name);
                continue;
            }
            const auto bySlot = e.note >= 0 ? slotOf (gm, e.note) : -1;
            if (allowed.front() == -2)
                allowed = bySlot >= 0 ? std::vector<int> { bySlot } : std::vector<int> {};
            const auto kick = allowed.size() == 1 && allowed.front() == BD;
            if (kick || (bySlot >= 0 && std::find (allowed.begin(), allowed.end(), bySlot) != allowed.end() && taken.count (bySlot) == 0))
            {
                const auto slot = kick ? (int) BD : bySlot;
                taken[slot] = e.name;
                result.slots[slot].add (e.name);
                continue;
            }
            later.emplace_back (&e, allowed);
        }
        for (const auto& [e, allowed] : later)
        {
            auto placed = false;
            for (const auto slot : allowed)
                if (taken.count (slot) == 0)
                {
                    taken[slot] = e->name;
                    result.slots[slot].add (e->name);
                    placed = true;
                    break;
                }
            if (! placed)
                result.unused.add (e->name);
        }
        return result;
    }
}
