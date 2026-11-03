#include "dsp/synth/SamplePack.h"

#include "dsp/synth/DiskStreamer.h"
#include "dsp/synth/NoteMap.h"

#include <set>

namespace synth::drums::samplepack
{
    namespace
    {
        using namespace notemap;

        bool isMicWord (const juce::String& w)
        {
            static const juce::StringArray words { "OH", "OVERHEAD", "OVERHEADS", "RM", "ROOM", "AMB", "AMBIENCE", "SPOT", "DYN", "DYN1",
                                                   "DYN2", "CON", "SUB", "FX", "TRIGGER", "TRIG", "BTM", "BOTTOM", "TOP", "TOP2", "INSIDE",
                                                   "OUTSIDE" };
            return words.contains (w);
        }

        // A file's name as (sound words, mic words, layer, round): false if it isn't "..._V<n>_RR<n>".
        bool parse (const juce::String& base, juce::StringArray& tokens, int& layer, int& round)
        {
            const auto upper = base.toUpperCase();
            const auto rr = upper.lastIndexOf ("_RR");
            if (rr < 0)
                return false;
            const auto v = upper.substring (0, rr).lastIndexOf ("_V");
            if (v < 0)
                return false;
            const auto layerText = upper.substring (v + 2, rr), roundText = upper.substring (rr + 3);
            if (layerText.isEmpty() || roundText.isEmpty() || ! layerText.containsOnly ("0123456789") || ! roundText.containsOnly ("0123456789"))
                return false;
            layer = layerText.getIntValue();
            round = roundText.getIntValue();
            tokens = juce::StringArray::fromTokens (upper.substring (0, v), "_", {});
            tokens.removeEmptyStrings();
            return ! tokens.isEmpty() && layer > 0 && round > 0;
        }

        juce::String pieceName (int slot)
        {
            const auto p = pieceOfSlot (slot);
            return p >= 0 ? juce::String (pieces[(size_t) p].name) : juce::String (slotDefaults[(size_t) slot].key);
        }

        // A USER channel's name for a mic of a slot (one channel per piece and mic, articulations sharing it).
        juce::String userName (const juce::String& mic, int slot)
        {
            if (slot == T4)
                return "TOM 4";
            const auto piece = pieceName (slot);
            if (piece == "SNARE" && (mic == "DYN2" || mic == "TOP2"))
                return "SNARE TOP 2";
            if (mic == "TRIG")
                return piece + " TRIGGER";
            return (piece + " " + mic).substring (0, 24);
        }

        struct Parsed
        {
            juce::File file;
            int slot = -1;
            juce::String mic;      // its words, joined ("" for a close mic named by no word)
            int layer = 1, round = 1;
        };
    }

    int slotFor (const juce::StringArray& w)
    {
        const auto has = [&w] (std::initializer_list<const char*> any)
        {
            for (const auto* a : any)
                if (w.contains (a))
                    return true;
            return false;
        };
        const auto joined = w.joinIntoString (" ");
        if (has ({ "KICK", "BD", "BASSDRUM", "KIK" }))
            return BD;
        if (has ({ "SNR", "SNARE", "SD" }))
        {
            if (has ({ "CROSS", "SIDESTICK", "XSTICK", "STICK", "CROSSSTICK" }))
                return RIM;
            if (has ({ "RIMSHOT", "RIM" }))
                return SD2;
            return SD;
        }
        for (int t = 1; t <= 4; ++t)
            if (w.contains ("TOM" + juce::String (t)) || joined.contains ("TOM " + juce::String (t)))
                return t == 1 ? HT : t == 2 ? MT : t == 3 ? LT : T4;
        if (has ({ "HATS", "HAT", "HH", "HIHAT" }))
        {
            if (has ({ "PEDAL", "FOOT" }))
                return PH;
            if (has ({ "SHOULDER", "TIP" }))
                return HSH;
            if (has ({ "OPEN" }))
                return has ({ "HALF" }) ? HH : OH;
            return CH;
        }
        if (has ({ "RIDE" }))
            return has ({ "BELL" }) ? RB : has ({ "CRASH", "EDGE" }) ? RE : RD;
        const auto choke = has ({ "CHOKE", "CHOKED", "STOP", "GRAB" });
        if (has ({ "CRASH2" }) || joined.contains ("CRASH 2"))
            return choke ? C2C : CR2;
        if (has ({ "CRASH1", "CRASH" }) || joined.contains ("CRASH 1"))
            return choke ? C1C : CY;
        if (has ({ "CHINA" }))
            return choke ? CHC : CHN;
        if (has ({ "SPLASH" }))
            return choke ? SPC : SPL;
        if (has ({ "TAMB", "TAMBOURINE" }))
            return TMB;
        if (has ({ "SHAKER" }))
            return SHK;
        if (has ({ "CLAP" }))
            return CLP;
        return -1;
    }

    int channelFor (const juce::String& micWords, int slot)
    {
        const auto m = micWords.toUpperCase();
        if (m.startsWith ("OH") || m.startsWith ("OVERHEAD"))
            return mixer::overheads;
        if (m.startsWith ("RM") || m.startsWith ("ROOM"))
            return mixer::room;
        if (m.startsWith ("AMB"))
            return mixer::ambience;
        const auto kick = slot == BD, snare = slot == SD || slot == SD2 || slot == RIM;
        const auto close = m.isEmpty() || m == "SPOT" || m == "DYN" || m == "DYN1" || m == "TOP" || m == "INSIDE";
        if (slot == T4 && close)
            return -1;   // no TOM 4 channel: its own
        if (kick && (m == "CON" || m == "OUTSIDE"))
            return mixer::kickOut;
        if (snare && (m == "BTM" || m == "BOTTOM"))
            return mixer::snareBottom;
        if (close)
            return homeChannel (slot);
        return -1;
    }

    PackInfo scan (const juce::File& folder)
    {
        PackInfo info;
        info.name = folder.getFileName();
        info.folder = folder;
        // A pack with an INDIVIDUAL MICS folder: that alone (its pre-mixed one-shots would read as other sounds).
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findDirectories))
            if (entry.getFile().getFileName().equalsIgnoreCase ("INDIVIDUAL MICS"))
            {
                info.folder = entry.getFile();
                break;
            }

        std::vector<std::pair<juce::File, juce::StringArray>> named;
        std::vector<std::pair<int, int>> numbers;
        for (const auto& entry : juce::RangedDirectoryIterator (info.folder, true, "*.wav;*.wave;*.aif;*.aiff;*.flac", juce::File::findFiles))
        {
            const auto f = entry.getFile();
            if (f.isHidden() || f.getFileName().startsWithChar ('.'))
                continue;
            juce::StringArray tokens;
            int layer = 0, round = 0;
            if (! parse (f.getFileNameWithoutExtension(), tokens, layer, round))
            {
                info.unused.addIfNotAlreadyThere (f.getFileNameWithoutExtension());
                continue;
            }
            named.push_back ({ f, tokens });
            numbers.push_back ({ layer, round });
        }
        if (named.empty())
            return info;
        // The prefix every name shares (at least one word left in each).
        auto prefix = named.front().second.size();
        for (const auto& [f, t] : named)
        {
            int k = 0;
            while (k < prefix && k < t.size() - 1 && t[k] == named.front().second[k])
                ++k;
            prefix = juce::jmin (prefix, k);
        }

        std::vector<Parsed> parsed;
        std::set<juce::String> extra;
        for (size_t i = 0; i < named.size(); ++i)
        {
            const auto& tokens = named[i].second;
            juce::StringArray sound, mic;
            for (int k = prefix; k < tokens.size(); ++k)
            {
                const auto& t = tokens[k];
                if (isMicWord (t) || (t == "CLOSE" && ! mic.isEmpty() && (mic[mic.size() - 1] == "RM" || mic[mic.size() - 1] == "ROOM")))
                    mic.add (t);
                else if (sound.isEmpty() || sound[sound.size() - 1] != t)
                    sound.addIfNotAlreadyThere (t);   // "KICK_CON_KICK": the sound named twice
            }
            Parsed p;
            p.file = named[i].first;
            p.slot = slotFor (sound);
            p.mic = mic.joinIntoString (" ");
            p.layer = numbers[i].first;
            p.round = numbers[i].second;
            if (p.slot < 0)
            {
                info.unused.addIfNotAlreadyThere (sound.joinIntoString (" "));
                continue;
            }
            if (channelFor (p.mic, p.slot) == -1)
                extra.insert (userName (p.mic, p.slot));
            parsed.push_back (std::move (p));
        }
        // The USER channels: their names in order (so the same pack always gives the same channels), at most 8.
        for (const auto& name : extra)
        {
            if (info.userChannels.size() < mixer::numUserChannels)
                info.userChannels.add (name);
            else
                info.unused.addIfNotAlreadyThere (name + " (no USER channel left)");
        }
        for (auto& p : parsed)
        {
            auto channel = channelFor (p.mic, p.slot);
            if (channel == -1)
            {
                const auto u = info.userChannels.indexOf (userName (p.mic, p.slot));
                if (u < 0)
                    continue;
                channel = mixer::numChannels + u;
            }
            info.slots[p.slot].push_back ({ p.file, channel, p.layer, p.round });
        }
        return info;
    }

    std::shared_ptr<const Instrument> load (const PackInfo& info, int slot, juce::StringArray& problems, double headSeconds)
    {
        const auto found = info.slots.find (slot);
        if (found == info.slots.end() || found->second.empty())
            return {};
        // Its files by layer and round.
        std::map<int, std::map<int, std::vector<File>>> byLayer;
        for (const auto& f : found->second)
            byLayer[f.layer][f.round].push_back (f);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto inst = std::make_shared<Instrument>();
        inst->name = info.name + ": " + juce::String (slotDefaults[(size_t) slot].key);
        inst->kind = Instrument::Kind::samplePack;
        inst->source = info.folder;

        for (const auto& [layerNumber, rounds] : byLayer)
        {
            Instrument::Layer layer;
            for (const auto& [roundNumber, files] : rounds)
            {
                juce::ignoreUnused (roundNumber);
                // Each file's head, and the hit's recipe: a mic (an output) per mixer channel, its files summed.
                auto stream = std::make_shared<StreamSource>();
                std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
                double rate = 0.0;
                juce::int64 longest = 0;
                for (const auto& f : files)
                {
                    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f.file));
                    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
                    {
                        problems.add ("couldn't read " + f.file.getFullPathName());
                        readers.emplace_back();
                        continue;
                    }
                    if (rate == 0.0)
                        rate = reader->sampleRate;
                    else if (std::abs (reader->sampleRate - rate) > 0.5)
                    {
                        problems.add (f.file.getFileName() + " is at another sample rate than its hit's other mics: left out");
                        readers.emplace_back();
                        continue;
                    }
                    longest = std::max<juce::int64> (longest, reader->lengthInSamples);
                    readers.push_back (std::move (reader));
                }
                if (rate <= 0.0 || longest <= 0)
                    continue;
                const auto head = (int) std::min<juce::int64> (longest, (juce::int64) std::ceil (headSeconds * rate));
                const auto streamed = longest > head;
                const auto frames = streamed ? head : (int) longest;
                stream->sampleRate = rate;
                stream->length = longest;
                stream->headFrames = head;

                Instrument::Hit hit;
                std::map<int, int> outputOf;   // mixer channel -> output
                std::vector<bool> stereoOf;
                for (size_t i = 0; i < files.size(); ++i)
                    if (readers[i] != nullptr && outputOf.count (files[i].channel) == 0)
                    {
                        outputOf[files[i].channel] = (int) stereoOf.size();
                        stereoOf.push_back (false);
                    }
                for (size_t i = 0; i < files.size(); ++i)
                    if (readers[i] != nullptr && readers[i]->numChannels > 1)
                        stereoOf[(size_t) outputOf[files[i].channel]] = true;
                if (outputOf.empty() || (int) outputOf.size() > Streamer::maxOutputs)
                {
                    problems.add (slotDefaults[(size_t) slot].key + juce::String (": a hit with more mics than can stream"));
                    continue;
                }
                std::vector<std::shared_ptr<Sample>> samples (outputOf.size());
                for (size_t o = 0; o < samples.size(); ++o)
                {
                    samples[o] = std::make_shared<Sample>();
                    samples[o]->sampleRate = rate;
                    samples[o]->channels.assign (stereoOf[o] ? 2 : 1, std::vector<float> ((size_t) juce::jmax (1, frames), 0.0f));
                }
                juce::AudioBuffer<float> buffer;
                for (size_t i = 0; i < files.size(); ++i)
                {
                    auto& reader = readers[i];
                    if (reader == nullptr)
                        continue;
                    const auto o = outputOf[files[i].channel];
                    const auto fileIndex = (int) stream->files.size();
                    stream->files.push_back (files[i].file);
                    const auto channels = (int) reader->numChannels;
                    buffer.setSize (channels, juce::jmax (1, frames), false, true, true);
                    buffer.clear();
                    reader->read (&buffer, 0, (int) std::min<juce::int64> (frames, reader->lengthInSamples), 0, true, true);
                    const auto stereoOut = stereoOf[(size_t) o];
                    // The recipe for the rest (streamed), and the head summed the same way.
                    const auto addPart = [&] (int fileChannel, int side, float gain)
                    {
                        stream->parts.push_back ({ fileChannel, o, side, gain, fileIndex });
                        const auto* from = buffer.getReadPointer (fileChannel);
                        auto& to = samples[(size_t) o]->channels[(size_t) side];
                        for (int k = 0; k < frames; ++k)
                            to[(size_t) k] += from[k] * gain;
                    };
                    if (! stereoOut)
                    {
                        addPart (0, 0, channels > 1 ? 0.5f : 1.0f);   // (a stereo file in a mono mic: both sides, halved)
                        if (channels > 1)
                            addPart (1, 0, 0.5f);
                    }
                    else if (channels == 1)
                    {
                        addPart (0, 0, 0.7071f);
                        addPart (0, 1, 0.7071f);
                    }
                    else
                    {
                        addPart (0, 0, 1.0f);
                        addPart (1, 1, 1.0f);
                    }
                    hit.files.add (files[i].file.getFullPathName());
                }
                stream->numOutputs = (int) samples.size();
                // The whole kit's mix of the hit (its head): pictures and levels.
                auto mix = std::make_shared<Sample>();
                mix->sampleRate = rate;
                mix->channels.assign (2, std::vector<float> ((size_t) juce::jmax (1, frames), 0.0f));
                for (const auto& [channel, o] : outputOf)
                {
                    auto& sample = *samples[(size_t) o];
                    for (int side = 0; side < 2; ++side)
                    {
                        const auto& from = sample.channels[(size_t) juce::jmin (side, (int) sample.channels.size() - 1)];
                        for (int k = 0; k < frames; ++k)
                            mix->channels[(size_t) side][(size_t) k] += from[(size_t) k] * (sample.channels.size() == 1 ? 0.7071f : 1.0f);
                    }
                    if (streamed)
                    {
                        sample.stream = stream;
                        sample.streamOutput = o;
                    }
                    hit.mics.push_back ({ channel, samples[(size_t) o] });
                }
                // (The outputs in mic order, as `outputOf` numbered them: the streamer's outputs follow the parts.)
                std::sort (hit.mics.begin(), hit.mics.end(), [] (const Instrument::Hit::Mic& a, const Instrument::Hit::Mic& b)
                           { return a.sample->streamOutput < b.sample->streamOutput; });
                hit.sample = std::move (mix);
                layer.hits.push_back (std::move (hit));
            }
            if (layer.hits.empty())
                continue;
            double sum = 0.0;
            for (const auto& h : layer.hits)
                sum += instruments::loudness (*h.sample);
            layer.level = (float) (sum / (double) layer.hits.size());
            inst->layers.push_back (std::move (layer));
            juce::ignoreUnused (layerNumber);
        }
        if (inst->layers.empty())
        {
            problems.add ("no " + juce::String (slotDefaults[(size_t) slot].key) + " sounds could be read in " + info.folder.getFullPathName());
            return {};
        }
        // V01 the softest, as a rule; a pack that counts the other way is turned round (by the layers' loudness).
        if (inst->layers.size() > 1 && inst->layers.front().level > inst->layers.back().level)
            std::reverse (inst->layers.begin(), inst->layers.end());
        // Velocities shared out evenly. Levels as recorded (the pack's own balance between its drums and mics).
        const auto L = (int) inst->layers.size();
        for (int k = 0; k < L; ++k)
        {
            inst->layers[(size_t) k].lo = 1 + k * 127 / L;
            inst->layers[(size_t) k].hi = (k + 1) * 127 / L;
        }
        return inst;
    }
}
