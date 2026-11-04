#include "dsp/synth/Instrument.h"

#include <set>

namespace synth::drums
{
    int Instrument::numHits() const noexcept
    {
        int n = 0;
        for (const auto& l : layers)
            n += (int) l.hits.size();
        return n;
    }

    bool Instrument::streamed() const noexcept
    {
        for (const auto& l : layers)
            for (const auto& h : l.hits)
                for (const auto& m : h.mics)
                    if (m.sample != nullptr && m.sample->stream != nullptr)
                        return true;
        return false;
    }

    double Instrument::longestSeconds() const noexcept
    {
        double longest = 0.0;
        for (const auto& l : layers)
            for (const auto& h : l.hits)
            {
                if (h.sample != nullptr)
                    longest = juce::jmax (longest, (double) h.sample->frames() / h.sample->sampleRate);
                for (const auto& m : h.mics)
                    if (m.sample != nullptr)
                        longest = juce::jmax (longest, (double) m.sample->frames() / m.sample->sampleRate);
            }
        return longest;
    }

    bool Instrument::hasMics() const noexcept
    {
        for (const auto& l : layers)
            for (const auto& h : l.hits)
                if (! h.mics.empty())
                    return true;
        return false;
    }

    juce::Array<int> Instrument::micChannels() const
    {
        juce::Array<int> channels;
        for (const auto& l : layers)
            for (const auto& h : l.hits)
                for (const auto& m : h.mics)
                    channels.addIfNotAlreadyThere (m.channel);
        channels.sort();
        return channels;
    }

    int Instrument::layerFor (int velocity) const noexcept
    {
        if (layers.empty())
            return -1;
        velocity = juce::jlimit (1, 127, velocity);
        // The layer whose range holds it; else the nearest (a gap between ranges, or below / above them all).
        auto best = 0, bestDistance = 1000;
        for (int i = 0; i < (int) layers.size(); ++i)
        {
            const auto& l = layers[(size_t) i];
            if (velocity >= l.lo && velocity <= l.hi && ! l.hits.empty())
                return i;
            const auto d = l.hits.empty() ? 1000 : juce::jmin (std::abs (velocity - l.lo), std::abs (velocity - l.hi));
            if (d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }
        return best;
    }

    Instrument::Choice Instrument::choose (int velocity, bool randomize, bool adjacent, int layerLimit, HitMemory& state) const noexcept
    {
        Choice c;
        auto layer = layerFor (velocity);
        if (layer < 0 || layers[(size_t) layer].hits.empty())
            return c;
        auto nextRandom = [&state]
        {
            state.random = state.random * 1664525u + 1013904223u;
            return state.random >> 8;
        };
        const auto home = layer;
        // ADJACENT LAYERS: in the top (or bottom) third of its range, a third of the time, the layer that way.
        if (adjacent && layers.size() > 1)
        {
            const auto& l = layers[(size_t) layer];
            const auto span = juce::jmax (1, l.hi - l.lo);
            const auto where = (float) (velocity - l.lo) / (float) span;
            const auto towards = where > 0.67f ? layer + 1 : where < 0.33f ? layer - 1 : layer;
            if (towards != layer && towards >= 0 && towards < (int) layers.size() && ! layers[(size_t) towards].hits.empty()
                && nextRandom() % 3 == 0)
                layer = towards;
        }
        const auto& pool = layers[(size_t) layer].hits;
        const auto count = layerLimit > 0 ? juce::jmin (layerLimit, (int) pool.size()) : (int) pool.size();
        auto& last = state.last[(size_t) juce::jlimit (0, (int) state.last.size() - 1, layer)];
        int index;
        if (count <= 1)
            index = 0;
        else if (randomize)
        {
            // Never the one just played (last holds it + 1; 0: none yet).
            index = (int) (nextRandom() % (std::uint32_t) (last > 0 ? count - 1 : count));
            if (last > 0 && index >= last - 1)
                ++index;
        }
        else
            index = last % count;   // in turn: last holds the one played + 1, so this is the next
        last = (std::int16_t) (index + 1);
        c.layer = layer;
        c.hit = index;
        c.sample = pool[(size_t) index].sample.get();
        c.mics = &pool[(size_t) index].mics;
        // A neighbour's hit matched to this layer's loudness.
        if (layer != home && layers[(size_t) layer].level > 0.0f)
            c.gain = layers[(size_t) home].level / layers[(size_t) layer].level;
        return c;
    }

    namespace instruments
    {
        float loudness (const Sample& s)
        {
            const auto n = juce::jmin (s.length(), (int) (0.1 * s.sampleRate));
            if (n <= 0)
                return 0.0f;
            double sum = 0.0;
            for (const auto& c : s.channels)
                for (int i = 0; i < n; ++i)
                    sum += (double) c[(size_t) i] * c[(size_t) i];
            return (float) std::sqrt (sum / ((double) n * (double) juce::jmax<size_t> (1, s.channels.size())));
        }

        namespace
        {
            // `from` added into `into` (made stereo, as long as needed), at `gain`, panned as a balance, from `offset`.
            bool addInto (Sample& into, const Sample& from, float gain, float pan, int offset)
            {
                if (from.length() < 2)
                    return false;
                if (into.channels.empty())
                {
                    into.sampleRate = from.sampleRate;
                    into.channels.assign (2, {});
                }
                if (std::abs (from.sampleRate - into.sampleRate) > 0.5)
                    return false;
                offset = juce::jlimit (0, from.length() - 1, offset);
                const auto length = from.length() - offset;
                for (auto& c : into.channels)
                    if ((int) c.size() < length)
                        c.resize ((size_t) length, 0.0f);
                const float sideGain[2] = { gain * (pan > 0.0f ? 1.0f - pan : 1.0f), gain * (pan < 0.0f ? 1.0f + pan : 1.0f) };
                for (int c = 0; c < 2; ++c)
                {
                    const auto& src = from.channels[(size_t) juce::jmin (c, (int) from.channels.size() - 1)];
                    auto& dst = into.channels[(size_t) c];
                    for (int i = 0; i < length; ++i)
                        dst[(size_t) i] += src[(size_t) (i + offset)] * sideGain[c];
                }
                return true;
            }

            void finish (Instrument& inst)
            {
                std::sort (inst.layers.begin(), inst.layers.end(), [] (const auto& a, const auto& b) { return a.lo < b.lo; });
                for (auto& l : inst.layers)
                {
                    double sum = 0.0;
                    for (const auto& h : l.hits)
                        sum += loudness (*h.sample);
                    l.level = l.hits.empty() ? 0.0f : (float) (sum / (double) l.hits.size());
                }
            }

            // A number after one of the words in a file's name ("v3", "vel_3", "rr2"), or -1.
            int numberAfter (const juce::String& name, const juce::StringArray& words)
            {
                const auto lower = name.toLowerCase();
                for (const auto& w : words)
                    for (int at = lower.indexOf (w); at >= 0; at = lower.indexOf (at + 1, w))
                    {
                        // The word must start the name or follow something that isn't a letter.
                        if (at > 0 && juce::CharacterFunctions::isLetter (lower[at - 1]))
                            continue;
                        auto i = at + w.length();
                        while (i < lower.length() && (lower[i] == '_' || lower[i] == '-' || lower[i] == ' '))
                            ++i;
                        if (i < lower.length() && juce::CharacterFunctions::isDigit (lower[i]))
                            return lower.substring (i).getIntValue();
                    }
                return -1;
            }
        }

        std::shared_ptr<const Instrument> fromFile (const juce::File& file)
        {
            auto sample = sampleFiles::load (file);
            if (sample == nullptr || sample->length() < 2)
                return {};
            auto inst = std::make_shared<Instrument>();
            inst->name = file.getFileNameWithoutExtension();
            inst->kind = Instrument::Kind::folder;
            inst->source = file;
            Instrument::Layer layer;
            Instrument::Hit hit;
            hit.sample = std::move (sample);
            hit.files.add (file.getFullPathName());
            layer.hits.push_back (std::move (hit));
            inst->layers.push_back (std::move (layer));
            finish (*inst);
            return inst;
        }

        bool hasMicFolders (const juce::File& folder)
        {
            if (! folder.isDirectory() || ! folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg").isEmpty())
                return false;
            auto mics = 0;
            for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, false))
                if (mixer::channelForName (sub.getFileName()) >= 0)
                    ++mics;
            return mics >= 2;
        }

        std::shared_ptr<const Instrument> fromFolder (const juce::File& folder)
        {
            const juce::String audio ("*.wav;*.aif;*.aiff;*.flac;*.ogg");
            auto files = folder.findChildFiles (juce::File::findFiles, false, audio);
            files.sort();
            struct Loaded { std::shared_ptr<const Sample> sample; juce::String path; float level; int layer, rr; std::vector<Instrument::Hit::Mic> mics; };
            std::vector<Loaded> hits;
            const auto add = [&hits] (std::shared_ptr<const Sample> s, const juce::File& f, std::vector<Instrument::Hit::Mic> mics)
            {
                const auto name = f.getFileNameWithoutExtension();
                hits.push_back ({ s, f.getFullPathName(), loudness (*s), numberAfter (name, { "vel", "v", "layer", "dyn" }),
                                  numberAfter (name, { "rr", "round", "take" }), std::move (mics) });
            };
            for (const auto& f : files)
                if (auto s = sampleFiles::load (f))
                    add (s, f, {});
            // MIC FOLDERS (0.20.0): no hits of its own, a subfolder per mic: each hit by its name in every mic's folder.
            if (hits.empty())
            {
                std::vector<std::pair<int, juce::File>> micFolders;
                for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, false))
                    if (const auto channel = mixer::channelForName (sub.getFileName()); channel >= 0)
                        micFolders.emplace_back (channel, sub);
                if (! micFolders.empty())
                {
                    std::sort (micFolders.begin(), micFolders.end(), [] (const auto& a, const auto& b) { return a.second.getFileName() < b.second.getFileName(); });
                    auto names = micFolders.front().second.findChildFiles (juce::File::findFiles, false, audio);
                    names.sort();
                    for (const auto& first : names)
                    {
                        auto sum = std::make_shared<Sample>();
                        sum->name = first.getFileNameWithoutExtension();
                        std::vector<Instrument::Hit::Mic> mics;
                        for (const auto& [channel, sub] : micFolders)
                            if (const auto f = sub.getChildFile (first.getFileName()); f.existsAsFile())
                                if (auto s = sampleFiles::load (f); s != nullptr && addInto (*sum, *s, 1.0f, 0.0f, 0))
                                    mics.push_back ({ channel, s });
                        if (! sum->channels.empty())
                            add (sum, first, micFolders.size() > 1 ? std::move (mics) : std::vector<Instrument::Hit::Mic> {});
                    }
                }
            }
            if (hits.empty())
                return nullptr;
            auto inst = std::make_shared<Instrument>();
            inst->name = folder.getFileName();
            inst->kind = Instrument::Kind::folder;
            inst->source = folder;

            // Named layers: every hit says its layer.
            const auto named = std::all_of (hits.begin(), hits.end(), [] (const auto& h) { return h.layer >= 0; });
            std::vector<std::vector<Loaded>> groups;
            if (named)
            {
                std::map<int, std::vector<Loaded>> byLayer;
                for (const auto& h : hits)
                    byLayer[h.layer].push_back (h);
                for (auto& [n, g] : byLayer)
                {
                    std::sort (g.begin(), g.end(), [] (const auto& a, const auto& b) { return a.rr < b.rr; });
                    groups.push_back (g);
                }
                // Layer numbers rising: but are they soft to loud? Ordered by their loudness to be sure.
                std::sort (groups.begin(), groups.end(), [] (const auto& a, const auto& b)
                {
                    const auto mean = [] (const std::vector<Loaded>& g)
                    {
                        double s = 0.0;
                        for (const auto& h : g)
                            s += h.level;
                        return s / (double) g.size();
                    };
                    return mean (a) < mean (b);
                });
            }
            else
            {
                std::sort (hits.begin(), hits.end(), [] (const auto& a, const auto& b) { return a.level < b.level; });
                const auto layerCount = juce::jlimit (1, juce::jmin (16, (int) hits.size()), juce::roundToInt (std::sqrt ((double) hits.size())));
                groups.resize ((size_t) layerCount);
                for (size_t i = 0; i < hits.size(); ++i)
                    groups[i * (size_t) layerCount / hits.size()].push_back (hits[i]);
            }
            // Velocities shared out evenly, quietest layer lowest.
            const auto n = (int) groups.size();
            for (int i = 0; i < n; ++i)
            {
                Instrument::Layer l;
                l.lo = 1 + 127 * i / n;
                l.hi = 127 * (i + 1) / n;
                for (const auto& h : groups[(size_t) i])
                    l.hits.push_back ({ h.sample, juce::StringArray (h.path), h.mics });
                inst->layers.push_back (std::move (l));
            }
            finish (*inst);
            return inst;
        }

        //==============================================================================
        // SFZ.
        namespace
        {
            using Opcodes = std::map<juce::String, juce::String>;

            int noteNumber (const juce::String& text)
            {
                const auto t = text.trim().toLowerCase();
                if (t.isEmpty())
                    return -1;
                if (juce::CharacterFunctions::isDigit (t[0]) || t[0] == '-')
                    return t.getIntValue();
                // A name: c4 = 60 (SFZ's middle C), with # or b.
                const juce::String letters ("c d ef g a b");
                const auto pos = letters.indexOfChar (t[0]);
                if (pos < 0 || t[0] == ' ')
                    return -1;
                auto i = 1, semis = pos;
                if (i < t.length() && t[i] == '#') { ++semis; ++i; }
                else if (i < t.length() && t[i] == 'b') { --semis; ++i; }
                const auto octave = t.substring (i).getIntValue();
                return juce::jlimit (0, 127, (octave + 1) * 12 + semis);
            }

            // The file's text, comments out, #include read in (up to 8 deep), #define put in.
            juce::String preprocess (const juce::File& file, std::map<juce::String, juce::String>& defines, int depth, juce::StringArray& problems)
            {
                if (depth > 8 || ! file.existsAsFile())
                {
                    problems.add ("missing: " + file.getFullPathName());
                    return {};
                }
                auto text = file.loadFileAsString();
                // Comments: /* ... */ and // to the end of the line.
                juce::String out;
                for (int i = 0; i < text.length();)
                {
                    if (text[i] == '/' && i + 1 < text.length() && text[i + 1] == '*')
                    {
                        const auto end = text.indexOf (i + 2, "*/");
                        i = end < 0 ? text.length() : end + 2;
                    }
                    else if (text[i] == '/' && i + 1 < text.length() && text[i + 1] == '/')
                    {
                        while (i < text.length() && text[i] != '\n')
                            ++i;
                    }
                    else
                        out << text[i++];
                }
                juce::String result;
                for (auto line : juce::StringArray::fromLines (out))
                {
                    const auto trimmed = line.trim();
                    if (trimmed.startsWith ("#define"))
                    {
                        const auto rest = trimmed.substring (7).trim();
                        defines[rest.upToFirstOccurrenceOf (" ", false, false).trim()] = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                        continue;
                    }
                    if (trimmed.startsWith ("#include"))
                    {
                        const auto name = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                        result << preprocess (file.getParentDirectory().getChildFile (name), defines, depth + 1, problems) << "\n";
                        continue;
                    }
                    // Longest names first, so $KIT2 isn't read as $KIT then 2.
                    std::vector<std::pair<juce::String, juce::String>> sorted (defines.begin(), defines.end());
                    std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
                    for (const auto& [k, v] : sorted)
                        line = line.replace (k, v);
                    result << line << "\n";
                }
                return result;
            }

            struct Region { Opcodes op; };

            // The headers and their opcodes, each region with everything it inherits.
            std::vector<Region> parse (const juce::String& text, juce::String& defaultPath)
            {
                std::vector<Region> regions;
                Opcodes control, global, master, group, current;
                juce::String header;
                const auto flush = [&]
                {
                    if (header == "region")
                    {
                        Region r;
                        for (const auto* level : { &global, &master, &group, &current })
                            for (const auto& [k, v] : *level)
                                r.op[k] = v;
                        regions.push_back (std::move (r));
                    }
                    else if (header == "control")
                        control = current;
                    else if (header == "global")
                        global = current;
                    else if (header == "master")
                        master = current;
                    else if (header == "group")
                        group = current;
                    current.clear();
                };
                for (const auto& line : juce::StringArray::fromLines (text))
                {
                    int i = 0;
                    const auto n = line.length();
                    while (i < n)
                    {
                        while (i < n && juce::CharacterFunctions::isWhitespace (line[i]))
                            ++i;
                        if (i >= n)
                            break;
                        if (line[i] == '<')
                        {
                            const auto end = line.indexOfChar (i, '>');
                            if (end < 0)
                                break;
                            flush();
                            header = line.substring (i + 1, end).trim().toLowerCase();
                            if (header == "master")
                                group.clear();
                            if (header == "global")
                                master.clear(), group.clear();
                            i = end + 1;
                            continue;
                        }
                        const auto eq = line.indexOfChar (i, '=');
                        if (eq < 0)
                            break;
                        const auto key = line.substring (i, eq).trim().toLowerCase();
                        // The value: to the next "name=" or '<' on the line (a sample's path may have spaces).
                        auto j = eq + 1;
                        auto valueEnd = n;
                        for (auto k = j; k < n; ++k)
                        {
                            if (line[k] == '<')
                            {
                                valueEnd = k;
                                break;
                            }
                            if (line[k] == '=' )
                            {
                                auto back = k - 1;
                                while (back > j && ! juce::CharacterFunctions::isWhitespace (line[back]))
                                    --back;
                                valueEnd = back;
                                break;
                            }
                        }
                        current[key] = line.substring (j, valueEnd).trim();
                        i = valueEnd;
                    }
                }
                flush();
                defaultPath = control.count ("default_path") ? control["default_path"] : juce::String();
                return regions;
            }

            juce::String get (const Opcodes& op, const char* key, const juce::String& otherwise = {})
            {
                const auto it = op.find (key);
                return it != op.end() ? it->second : otherwise;
            }

            struct Range { int lo, hi; };
            Range keysOf (const Opcodes& op)
            {
                if (const auto key = noteNumber (get (op, "key")); key >= 0)
                    return { key, key };
                const auto lo = noteNumber (get (op, "lokey", "0"));
                const auto hi = noteNumber (get (op, "hikey", "127"));
                return { juce::jmax (0, lo), juce::jmin (127, hi) };
            }
        }

        std::map<int, int> sfzNotes (const juce::File& file)
        {
            std::map<juce::String, juce::String> defines;
            juce::StringArray problems;
            juce::String defaultPath;
            std::map<int, int> notes;
            for (const auto& r : parse (preprocess (file, defines, 0, problems), defaultPath))
            {
                if (get (r.op, "sample").isEmpty() || get (r.op, "trigger", "attack").toLowerCase() == "release")
                    continue;
                const auto keys = keysOf (r.op);
                if (keys.hi - keys.lo > 24)
                    continue;   // a whole keyboard of one sound: not a drum
                for (int k = keys.lo; k <= keys.hi; ++k)
                    ++notes[k];
            }
            return notes;
        }

        Sfz fromSfz (const juce::File& file, int onlyNote)
        {
            Sfz result;
            std::map<juce::String, juce::String> defines;
            juce::String defaultPath;
            const auto regions = parse (preprocess (file, defines, 0, result.problems), defaultPath);
            // The hits: note -> (lovel, hivel) -> (round-robin) -> the regions summed.
            struct HitKey
            {
                int lo, hi, rr;
                bool operator< (const HitKey& o) const { return std::tie (lo, hi, rr) < std::tie (o.lo, o.hi, o.rr); }
            };
            std::map<int, std::map<HitKey, std::vector<const Region*>>> byNote;
            for (const auto& r : regions)
            {
                if (get (r.op, "sample").isEmpty() || get (r.op, "trigger", "attack").toLowerCase() == "release")
                    continue;
                auto keys = keysOf (r.op);
                if (onlyNote == wholeFile)
                    keys = { 0, 0 };
                else if (keys.hi - keys.lo > 24)
                    continue;
                const HitKey key { juce::jlimit (0, 127, get (r.op, "lovel", "1").getIntValue()),
                                   juce::jlimit (0, 127, get (r.op, "hivel", "127").getIntValue()),
                                   get (r.op, "seq_position", "1").getIntValue() * 1000
                                       + juce::roundToInt (get (r.op, "lorand", "0").getFloatValue() * 999.0f) };
                for (int k = keys.lo; k <= keys.hi; ++k)
                    if (onlyNote < 0 || k == onlyNote)
                        byNote[k][key].push_back (&r);
            }

            // Each file once (a kit's regions share files across notes and layers).
            std::map<juce::String, std::shared_ptr<const Sample>> cache;
            // A region's mic: its label (group, region, master or global), else its file's folder and name.
            const auto micOf = [] (const Region& r)
            {
                for (const auto* key : { "region_label", "group_label", "master_label", "global_label" })
                    if (const auto label = get (r.op, key); label.isNotEmpty())
                        if (const auto channel = mixer::channelForName (label); channel >= 0)
                            return channel;
                const juce::File f ("/" + get (r.op, "sample").replaceCharacter ('\\', '/'));
                return mixer::channelForName (f.getParentDirectory().getFileName() + " " + f.getFileNameWithoutExtension());
            };
            const auto loadFile = [&] (const juce::String& relative) -> std::shared_ptr<const Sample>
            {
                const auto path = (defaultPath + relative).replaceCharacter ('\\', '/');
                const auto f = juce::File::isAbsolutePath (path) ? juce::File (path) : file.getParentDirectory().getChildFile (path);
                const auto id = f.getFullPathName();
                if (const auto it = cache.find (id); it != cache.end())
                    return it->second;
                auto s = sampleFiles::load (f);
                if (s == nullptr)
                    result.problems.addIfNotAlreadyThere ("couldn't load: " + id);
                cache[id] = s;
                return s;
            };

            for (const auto& [note, hits] : byNote)
            {
                auto inst = std::make_shared<Instrument>();
                inst->name = file.getFileNameWithoutExtension() + " " + juce::String (note);
                inst->kind = Instrument::Kind::sfz;
                inst->source = file;
                inst->note = onlyNote == wholeFile ? -1 : note;
                std::map<std::pair<int, int>, Instrument::Layer> layers;
                // Multi-mic: the note's regions in two or more mic channels.
                juce::Array<int> noteMics;
                for (const auto& [key, parts] : hits)
                    for (const auto* r : parts)
                        if (const auto m = micOf (*r); m >= 0)
                            noteMics.addIfNotAlreadyThere (m);
                const auto multiMic = noteMics.size() >= 2;
                for (const auto& [key, parts] : hits)
                {
                    std::map<int, std::shared_ptr<Sample>> byMic;
                    // The hit: its regions' files summed, each at its volume / amplitude and pan, from its offset.
                    auto sum = std::make_shared<Sample>();
                    juce::StringArray files;
                    for (const auto* r : parts)
                    {
                        const auto s = loadFile (get (r->op, "sample"));
                        if (s == nullptr || s->length() < 2)
                            continue;
                        files.add (get (r->op, "sample"));
                        if (multiMic)
                        {
                            auto& part = byMic[micOf (*r)];
                            if (part == nullptr)
                                part = std::make_shared<Sample>();
                            addInto (*part, *s, juce::Decibels::decibelsToGain (get (r->op, "volume", "0").getFloatValue())
                                                    * get (r->op, "amplitude", "100").getFloatValue() / 100.0f,
                                     juce::jlimit (-1.0f, 1.0f, get (r->op, "pan", "0").getFloatValue() / 100.0f), get (r->op, "offset", "0").getIntValue());
                        }
                        const auto gain = juce::Decibels::decibelsToGain (get (r->op, "volume", "0").getFloatValue())
                                        * get (r->op, "amplitude", "100").getFloatValue() / 100.0f;
                        const auto pan = juce::jlimit (-1.0f, 1.0f, get (r->op, "pan", "0").getFloatValue() / 100.0f);
                        const auto offset = juce::jlimit (0, s->length() - 1, get (r->op, "offset", "0").getIntValue());
                        if (sum->channels.empty())
                        {
                            sum->sampleRate = s->sampleRate;
                            sum->name = juce::File (get (r->op, "sample").replaceCharacter ('\\', '/')).getFileNameWithoutExtension();
                            sum->channels.assign (2, {});
                        }
                        if (std::abs (s->sampleRate - sum->sampleRate) > 0.5)
                        {
                            result.problems.addIfNotAlreadyThere ("a different sample rate, left out: " + get (r->op, "sample"));
                            continue;
                        }
                        const auto length = s->length() - offset;
                        for (auto& c : sum->channels)
                            if ((int) c.size() < length)
                                c.resize ((size_t) length, 0.0f);
                        // Pan as a balance (centre: both sides as they are).
                        const float sideGain[2] = { gain * (pan > 0.0f ? 1.0f - pan : 1.0f), gain * (pan < 0.0f ? 1.0f + pan : 1.0f) };
                        for (int c = 0; c < 2; ++c)
                        {
                            const auto& from = s->channels[(size_t) juce::jmin (c, (int) s->channels.size() - 1)];
                            auto& to = sum->channels[(size_t) c];
                            for (int i = 0; i < length; ++i)
                                to[(size_t) i] += from[(size_t) (i + offset)] * sideGain[c];
                        }
                    }
                    if (sum->channels.empty())
                        continue;
                    auto& layer = layers[{ key.lo, key.hi }];
                    layer.lo = juce::jmax (1, key.lo);
                    layer.hi = key.hi;
                    std::vector<Instrument::Hit::Mic> mics;
                    for (const auto& [channel, part] : byMic)
                        if (! part->channels.empty())
                            mics.push_back ({ channel, part });
                    layer.hits.push_back ({ sum, files, mics });
                }
                for (auto& [range, l] : layers)
                    inst->layers.push_back (std::move (l));
                if (inst->numHits() == 0)
                    continue;
                finish (*inst);
                result.notes[note] = inst;
            }
            return result;
        }
    }
}
