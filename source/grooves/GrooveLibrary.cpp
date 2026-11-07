#include "GrooveLibrary.h"
#include "grooves/GrooveDatabase.h"

#include <map>
#include <mutex>
#include <set>

namespace grooves
{
    namespace
    {
        using synth::drums::notemap::Layout;

        // What was read from a file, kept with what it was read as (so a changed file, or folder map, is read again),
        // and the index's format ("2": with FEEL, 0.5.0; "3": with the rhythm, 0.6.0; "4": the rhythm's velocities and
        // the clap as a snare, 0.31.0; anything older is read again, once).
        juce::String stamp (const juce::File& f, Layout layout)
        {
            // (0.58.0: GROOVE LIBRARY's notes became the owner's full map: its folders read again once, ":2".)
            return "4:" + juce::String (f.getSize()) + ":" + juce::String (f.getLastModificationTime().toMilliseconds()) + ":"
                   + juce::String ((int) layout) + (layout == Layout::grooveLibrary ? ":2" : "");
        }

        std::unique_ptr<juce::XmlElement> toXml (const Entry& e, const juce::String& key)
        {
            auto x = std::make_unique<juce::XmlElement> ("G");
            x->setAttribute ("path", e.file.getFullPathName());
            x->setAttribute ("stamp", key);
            x->setAttribute ("bpm", e.bpm);
            x->setAttribute ("bpmKnown", e.bpmKnown ? 1 : 0);
            x->setAttribute ("num", e.numerator);
            x->setAttribute ("den", e.denominator);
            x->setAttribute ("bars", e.bars);
            x->setAttribute ("hand", (int) e.hand);
            x->setAttribute ("intensity", (double) e.intensity);
            x->setAttribute ("hits", e.numHits);
            x->setAttribute ("feel", (int) e.feel);
            if (e.rhythm != nullptr)
                x->setAttribute ("rhythm", e.rhythm->encode());
            if (! e.tags.isEmpty())
                x->setAttribute ("tags", e.tags.joinIntoString ("|"));
            if (e.priority != 0)
                x->setAttribute ("prio", e.priority);
            return x;
        }

        Entry fromXml (const juce::XmlElement& x)
        {
            Entry e;
            e.file = juce::File (x.getStringAttribute ("path"));
            e.bpm = x.getDoubleAttribute ("bpm", 120.0);
            e.bpmKnown = x.getIntAttribute ("bpmKnown") != 0;
            e.numerator = x.getIntAttribute ("num", 4);
            e.denominator = x.getIntAttribute ("den", 4);
            e.bars = x.getIntAttribute ("bars", 1);
            e.hand = (PowerHand) juce::jlimit (0, powerHandNames.size() - 1, x.getIntAttribute ("hand"));
            e.intensity = (float) x.getDoubleAttribute ("intensity");
            e.numHits = x.getIntAttribute ("hits");
            e.feel = (Feel) juce::jlimit (0, feelNames.size() - 1, x.getIntAttribute ("feel"));
            e.rhythm = std::make_shared<const Rhythm> (Rhythm::decode (x.getStringAttribute ("rhythm")));
            e.tags = juce::StringArray::fromTokens (x.getStringAttribute ("tags"), "|", "");
            e.tags.removeEmptyStrings();
            e.priority = x.getIntAttribute ("prio");
            return e;
        }

        Entry entryOf (const Groove& g, int folder, const synth::drums::notemap::Map& layout)
        {
            Entry e;
            e.file = g.file;
            e.folder = folder;
            e.name = g.name;
            e.family = g.file.getParentDirectory().getFileName();
            e.bpm = g.bpm;
            e.bpmKnown = g.bpmKnown;
            e.numerator = g.numerator;
            e.denominator = g.denominator;
            e.bars = g.bars;
            e.hand = g.hand;
            e.intensity = g.intensity;
            e.numHits = (int) g.hits.size();
            e.feel = g.feel;
            e.rhythm = std::make_shared<const Rhythm> (rhythmOf (g, layout));
            return e;
        }
    }

    namespace
    {
        // 0.15.2: what this DAW session has already checked, by index file: the folders and maps checked, and the
        // index as that check left it. Every PR7-D in a song shares it, so only the first checks the folders.
        std::mutex sessionLock;
        std::map<juce::String, juce::String> checkedThisSession;

        juce::String indexStamp (const juce::File& index)
        {
            return juce::String (index.getSize()) + ":" + juce::String (index.getLastModificationTime().toMilliseconds());
        }
    }

    juce::String Library::foldersChecked() const
    {
        juce::String s (subfolders ? "1" : "0");
        for (const auto& f : folders)
            s << "|" << f.folder.getFullPathName() << ":" << (int) f.layout;
        return s;
    }

    class Library::Scanner final : public juce::Thread
    {
    public:
        Scanner (Library& l, bool c) : juce::Thread ("PR7-D grooves"), library (l), check (c) {}
        ~Scanner() override { stopThread (10000); }
        void run() override
        {
            library.read (&stop, check);
            library.scanning = false;
            library.sendChangeMessage();
        }
        void signalStop() { stop = true; signalThreadShouldExit(); }

    private:
        Library& library;
        bool check;
        std::atomic<bool> stop { false };
    };

    Library::Library (juce::File settings, juce::File index) : settingsFile (std::move (settings)), indexFile (std::move (index))
    {
        load();
    }

    void Library::loadIndex()
    {
        if (indexLoaded)
            return;
        indexLoaded = true;
        if (const auto xml = juce::XmlDocument::parse (indexFile))
        {
            std::vector<Entry> known;
            for (const auto* x : xml->getChildWithTagNameIterator ("G"))
            {
                auto e = fromXml (*x);
                for (int f = 0; f < (int) folders.size(); ++f)
                    if (e.file.isAChildOf (folders[(size_t) f].folder))
                    {
                        e.folder = f;
                        e.name = e.file.getFileNameWithoutExtension();
                        e.family = e.file.getParentDirectory().getFileName();
                        e.part = partOf (e.file);
                        known.push_back (e);
                        break;
                    }
            }
            cacheEntries (known);
            std::lock_guard<std::mutex> guard (lock);
            entries = std::move (known);
            packsKnown = false;
            ++generation;
            matchScores.clear();
        }
    }

    Library::~Library()
    {
        stopScan();
    }

    void Library::load()
    {
        const auto xml = juce::XmlDocument::parse (settingsFile);
        if (xml == nullptr)
            return;
        subfolders = xml->getBoolAttribute ("subfolders", true);
        for (const auto* f : xml->getChildWithTagNameIterator ("Folder"))
            folders.push_back ({ juce::File (f->getStringAttribute ("path")),
                                 (Layout) juce::jlimit (0, synth::drums::notemap::layoutNames.size() - 1, f->getIntAttribute ("map", (int) Layout::grooveLibrary)),
                                 f->getBoolAttribute ("user"), f->getBoolAttribute ("excluded") });
        for (const auto* f : xml->getChildWithTagNameIterator ("Favourite"))
            favourites.addIfNotAlreadyThere (f->getStringAttribute ("path"));
        favourites.sort (false);
        if (const auto* p = xml->getChildByName ("Preview"))
        {
            preview.songTempo = p->getBoolAttribute ("songTempo", true);
            preview.factor = (TempoFactor) juce::jlimit (0, tempoFactorNames.size() - 1, p->getIntAttribute ("factor", 1));
            preview.velocity = (float) juce::jlimit (0.25, 1.75, p->getDoubleAttribute ("velocity", 1.0));
            preview.forThisKit = p->getBoolAttribute ("forThisKit", true);
            preview.sort = (Sort) juce::jlimit (0, sortNames.size() - 1, p->getIntAttribute ("sort", 0));
            preview.sortOrder = SortOrder (preview.sort);   // before 0.16.0: the one choice
            if (p->hasAttribute ("sortPriority"))
            {
                preview.sortOrder.priority = p->getBoolAttribute ("sortPriority");
                preview.sortOrder.songTempo = p->getBoolAttribute ("sortTempo");
                preview.sortOrder.songSignature = p->getBoolAttribute ("sortSignature");
            }
            preview.filterColumns = juce::StringArray::fromTokens (p->getStringAttribute ("filterColumns"), ",", "");
            preview.filterColumns.removeEmptyStrings();
            preview.resultColumns = juce::StringArray::fromTokens (p->getStringAttribute ("resultColumns"), ",", "");
            preview.resultColumns.removeEmptyStrings();
        }
    }

    void Library::save() const
    {
        juce::XmlElement xml ("Grooves");
        xml.setAttribute ("subfolders", subfolders);
        for (const auto& f : folders)
        {
            auto* e = xml.createNewChildElement ("Folder");
            e->setAttribute ("path", f.folder.getFullPathName());
            e->setAttribute ("map", (int) f.layout);
            if (f.user)
                e->setAttribute ("user", true);
            if (f.excluded)
                e->setAttribute ("excluded", true);
        }
        for (const auto& path : favourites)
            xml.createNewChildElement ("Favourite")->setAttribute ("path", path);
        auto* p = xml.createNewChildElement ("Preview");
        p->setAttribute ("songTempo", preview.songTempo);
        p->setAttribute ("factor", (int) preview.factor);
        p->setAttribute ("velocity", (double) preview.velocity);
        p->setAttribute ("forThisKit", preview.forThisKit);
        p->setAttribute ("sort", (int) preview.sort);
        p->setAttribute ("sortPriority", preview.sortOrder.priority);
        p->setAttribute ("sortTempo", preview.sortOrder.songTempo);
        p->setAttribute ("sortSignature", preview.sortOrder.songSignature);
        if (! preview.filterColumns.isEmpty())
            p->setAttribute ("filterColumns", preview.filterColumns.joinIntoString (","));
        if (! preview.resultColumns.isEmpty())
            p->setAttribute ("resultColumns", preview.resultColumns.joinIntoString (","));
        settingsFile.getParentDirectory().createDirectory();
        xml.writeTo (settingsFile);
    }

    bool Library::addFolder (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return false;
        for (const auto& f : folders)
            if (f.folder == folder || folder.isAChildOf (f.folder))
                return false;   // already there (inside another, its files are listed already)
        stopScan();   // the folders change under it
        // Before the USER MIDI folder, which stays last.
        const auto at = ! folders.empty() && folders.back().user ? folders.end() - 1 : folders.end();
        folders.insert (at, { folder, Layout::grooveLibrary });
        save();
        scan();
        return true;
    }

    void Library::setUserFolder (const juce::File& folder)
    {
        folder.createDirectory();
        for (auto& f : folders)
            if (f.user)
            {
                if (f.folder == folder)
                    return;
                f.folder = folder;   // (the presets folder moved)
                save();
                return;
            }
        folders.push_back ({ folder, Layout::grooveLibrary, true, false });
        save();
    }

    int Library::getUserFolder() const noexcept
    {
        for (int i = 0; i < (int) folders.size(); ++i)
            if (folders[(size_t) i].user)
                return i;
        return -1;
    }

    int Library::addToUserFolder (const juce::StringArray& files, const juce::File& into)
    {
        const auto user = getUserFolder();
        if (user < 0)
            return 0;
        const auto& root = folders[(size_t) user].folder;
        const auto target = into == root || into.isAChildOf (root) ? into : root;
        target.createDirectory();
        auto copied = 0;
        for (const auto& path : files)
        {
            const juce::File source (path);
            if (source.isDirectory())
            {
                // A folder: its MIDI files, as a folder of the same name.
                const auto inside = target.getChildFile (source.getFileName());
                juce::StringArray found;
                for (const auto& item : juce::RangedDirectoryIterator (source, true, "*.mid;*.midi", juce::File::findFiles))
                    found.add (item.getFile().getFullPathName());
                for (const auto& f : found)
                {
                    const auto copy = inside.getChildFile (juce::File (f).getRelativePathFrom (source));
                    copy.getParentDirectory().createDirectory();
                    if (juce::File (f).copyFileTo (copy.getNonexistentSibling (false)))
                        ++copied;
                }
            }
            else if (source.hasFileExtension ("mid;midi") && source.copyFileTo (target.getChildFile (source.getFileName()).getNonexistentSibling (false)))
                ++copied;
        }
        if (copied > 0)
            scan();
        return copied;
    }

    void Library::setExcluded (int folder, bool on)
    {
        if (folder < 0 || folder >= (int) folders.size() || folders[(size_t) folder].excluded == on)
            return;
        folders[(size_t) folder].excluded = on;
        save();
        sendChangeMessage();
    }

    bool Library::needsSync (int folder) const
    {
        if (folder < 0 || folder >= (int) folders.size() || isScanning())
            return false;
        const auto& root = folders[(size_t) folder].folder;
        auto onDisk = 0;
        for (const auto& item : juce::RangedDirectoryIterator (root, true, "*.mid;*.midi", juce::File::findFiles))
        {
            ++onDisk;
            if (item.getModificationTime() > lastChecked)
                return true;   // added or changed since the last check
        }
        std::lock_guard<std::mutex> guard (lock);
        const auto listed = std::count_if (entries.begin(), entries.end(), [&root] (const Entry& e) { return e.file.isAChildOf (root); });
        return onDisk != (int) listed;
    }

    std::vector<juce::File> Library::getPacks() const
    {
        // A groove's path inside a database is <database file>/<pack>/...: the pack is the folder right inside the
        // database, found by walking up to the first path that exists on disk (the database).
        std::set<juce::String> seen;
        std::vector<juce::File> packs;
        std::lock_guard<std::mutex> guard (lock);
        if (packsKnown)
            return knownPacks;   // 0.30.0: worked out once each time the grooves are listed
        for (const auto& e : entries)
        {
            if (! e.file.getFullPathName().containsIgnoreCase ("mididb"))
                continue;
            auto f = e.file.getParentDirectory();
            while (f.getParentDirectory() != f && ! f.getParentDirectory().getFileName().equalsIgnoreCase ("mididb")
                   && ! f.getParentDirectory().getFileName().endsWithIgnoreCase (".mididb"))
                f = f.getParentDirectory();
            if (f.getParentDirectory() != f && seen.insert (f.getFullPathName()).second)
                packs.push_back (f);
        }
        std::sort (packs.begin(), packs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        knownPacks = packs;
        packsKnown = true;
        return packs;
    }

    void Library::removeFolder (int index)
    {
        if (index < 0 || index >= (int) folders.size() || folders[(size_t) index].user)
            return;
        stopScan();
        folders.erase (folders.begin() + index);
        save();
        scan();
    }

    void Library::setLayout (int folder, Layout layout)
    {
        if (folder < 0 || folder >= (int) folders.size() || folders[(size_t) folder].layout == layout)
            return;
        stopScan();
        folders[(size_t) folder].layout = layout;
        save();
        scan();   // its power hands change
    }

    void Library::setSubfolders (bool on)
    {
        if (on == subfolders)
            return;
        subfolders = on;
        save();
        sendChangeMessage();
    }

    synth::drums::notemap::Map Library::layoutFor (int folder, const synth::drums::notemap::Map& custom, int root) const
    {
        using namespace synth::drums::notemap;
        switch (folder >= 0 && folder < (int) folders.size() ? folders[(size_t) folder].layout : Layout::grooveLibrary)
        {
            case Layout::root: return rootLayout (root);
            case Layout::generalMidi: return generalMidi();
            case Layout::custom: return custom;
            case Layout::grooveLibrary: break;
        }
        return grooveLibrary();
    }

    void Library::stopScan()
    {
        if (scanner != nullptr)
            scanner->signalStop();
        scanner.reset();   // waits for it to stop
        scanning = false;
    }

    bool Library::checkedThisSessionAlready() const
    {
        std::lock_guard<std::mutex> guard (sessionLock);
        const auto known = checkedThisSession.find (indexFile.getFullPathName());
        return known != checkedThisSession.end() && known->second == foldersChecked() + "#" + indexStamp (indexFile);
    }

    void Library::scan (bool onlyIfNeeded)
    {
        stopScan();
        const auto check = ! (onlyIfNeeded && checkedThisSessionAlready());
        if (! check && indexLoaded)
            return;   // listed, and checked this session (by this PR7-D or another)
        scanning = true;
        scanned = 0;
        looked = 0;
        scanner = std::make_unique<Scanner> (*this, check);
        scanner->startThread (juce::Thread::Priority::low);
    }

    void Library::scanNow()
    {
        stopScan();
        scanning = true;
        scanned = 0;
        looked = 0;
        read (nullptr, true);
        scanning = false;
        sendChangeMessage();
    }

    void Library::read (const std::atomic<bool>* stop, bool check)
    {
        // What was read before, by path: a file whose size, date and folder map haven't changed isn't read again.
        std::map<juce::String, std::pair<juce::String, Entry>> before;
        if (const auto xml = juce::XmlDocument::parse (indexFile))
            for (const auto* x : xml->getChildWithTagNameIterator ("G"))
                before[x->getStringAttribute ("path")] = { x->getStringAttribute ("stamp"), fromXml (*x) };

        // 0.15.2: listed at once from what was read before (off the window's thread), then checked.
        if (! indexLoaded)
        {
            std::vector<Entry> known;
            for (const auto& [path, item] : before)
            {
                auto e = item.second;
                for (int f = 0; f < (int) folders.size(); ++f)
                    if (e.file.isAChildOf (folders[(size_t) f].folder))
                    {
                        e.folder = f;
                        e.name = e.file.getFileNameWithoutExtension();
                        e.family = e.file.getParentDirectory().getFileName();
                        e.part = partOf (e.file);
                        known.push_back (std::move (e));
                        break;
                    }
            }
            cacheEntries (known);
            {
                std::lock_guard<std::mutex> guard (lock);
                entries = std::move (known);
                packsKnown = false;
                ++generation;
                matchScores.clear();
            }
            indexLoaded = true;
            sendChangeMessage();
        }
        if (! check || (stop != nullptr && stop->load()))
            return;

        std::vector<Entry> found;
        std::set<juce::String> seen;
        auto changed = false;   // anything read anew (else the index stays as it is)
        // 0.33.1: grooves read anew go into the list as the scan goes (every half second), not only when it ends: a
        // newly linked folder of thousands fills in as it's read instead of staying empty until the last file.
        std::vector<Entry> fresh;
        auto lastShown = juce::Time::getMillisecondCounterHiRes();
        const auto showFresh = [&]
        {
            if (fresh.empty())
                return;
            std::set<juce::String> paths;
            for (const auto& e : fresh)
                paths.insert (e.file.getFullPathName());
            std::vector<Entry> listed;
            {
                std::lock_guard<std::mutex> guard (lock);
                listed.reserve (entries.size() + fresh.size());
                for (const auto& e : entries)
                    if (paths.count (e.file.getFullPathName()) == 0)
                        listed.push_back (e);
            }
            for (auto& e : fresh)
                listed.push_back (std::move (e));
            fresh.clear();
            cacheEntries (listed, true);
            {
                std::lock_guard<std::mutex> guard (lock);
                entries = std::move (listed);
                packsKnown = false;
                ++generation;
                matchScores.clear();
                indexLoaded = true;
            }
            lastShown = juce::Time::getMillisecondCounterHiRes();
            sendChangeMessage();
        };
        juce::XmlElement index ("GrooveIndex");
        // 0.33.2: the folder the page is showing is walked first (then every linked folder, skipping what it read), so
        // the grooves you're looking at come in first however big the rest is.
        const auto walk = [&] (int f, const juce::File& from)
        {
            const auto& linked = folders[(size_t) f];
            // CUSTOM and ROOT files: their power hand from the factory layout (the plugin's own map can change any time).
            const auto map = layoutFor (f, synth::drums::notemap::grooveLibrary(), 36);
            const auto add = [&] (const juce::File& file, const juce::String& key, auto readIt, const db::Contents::Item* item = nullptr)
            {
                if (seen.count (file.getFullPathName()) > 0)
                    return;   // (walked already: the folder shown first)
                Entry e;
                auto isNew = false;
                if (const auto known = before.find (file.getFullPathName()); known != before.end() && known->second.first == key)
                    e = known->second.second;
                else if (const auto g = readIt())
                {
                    e = entryOf (*g, f, map);
                    if (item != nullptr)
                    {
                        e.tags = item->tags;
                        e.priority = item->priority;
                    }
                    changed = true;
                    ++scanned;
                    isNew = true;
                }
                else
                    return;   // not a groove: no notes, or not a MIDI file
                seen.insert (file.getFullPathName());
                e.folder = f;
                e.name = file.getFileNameWithoutExtension();
                e.family = file.getParentDirectory().getFileName();
                e.part = partOf (file);
                index.addChildElement (toXml (e, key).release());
                if (isNew)
                {
                    fresh.push_back (e);
                    if (juce::Time::getMillisecondCounterHiRes() - lastShown >= showFreshEveryMs.load())
                        showFresh();
                }
                found.push_back (std::move (e));
            };
            for (const auto& item : juce::RangedDirectoryIterator (from, true, juce::String ("*.mid;*.midi;") + db::wildcard, juce::File::findFiles))
            {
                if ((stop != nullptr && stop->load()) || (int) found.size() >= maxFiles)
                    break;
                ++looked;
                const auto file = item.getFile();
                if (! file.hasFileExtension ("mid;midi"))
                {
                    // A groove database (0.15.0): each groove in it, as if a file inside it; known ones by the database's
                    // own size and date.
                    if (! db::isDatabase (file))
                        continue;
                    const auto key = "db4:" + stamp (file, linked.layout);   // (4: with the database's labels, 0.16.0)
                    // 0.15.2: unchanged (its size and date as read before): its grooves as listed, without reading it.
                    const auto inside = file.getFullPathName() + juce::File::getSeparatorString();
                    std::vector<juce::File> listed;
                    for (auto it = before.lower_bound (inside); it != before.end() && it->first.startsWith (inside); ++it)
                        if (it->second.first == key)
                            listed.push_back (juce::File (it->first));
                    if (! listed.empty())
                    {
                        for (const auto& groove : listed)
                            add (groove, key, [] { return std::optional<Groove>(); });
                        continue;
                    }
                    const auto contents = db::load (file);
                    for (const auto& groove : contents->items)
                    {
                        if ((stop != nullptr && stop->load()) || (int) found.size() >= maxFiles)
                            break;
                        add (groove.file, key, [&]
                        {
                            juce::MemoryInputStream in (groove.midi, false);
                            return grooves::read (in, groove.file, map);
                        }, &groove);
                    }
                    continue;
                }
                add (file, stamp (file, linked.layout), [&] { return grooves::read (file, map); });
            }
        };
        const auto first = getFocus();
        if (first.isDirectory())
            for (int f = 0; f < (int) folders.size(); ++f)
                if (first == folders[(size_t) f].folder || first.isAChildOf (folders[(size_t) f].folder))
                {
                    walk (f, first);
                    showFresh();
                    break;
                }
        for (int f = 0; f < (int) folders.size(); ++f)
            walk (f, folders[(size_t) f].folder);
        indexFile.getParentDirectory().createDirectory();
        if (stop != nullptr && stop->load())
        {
            // Stopped (another scan, or this PR7-D removed): what it read anew is kept, with the rest as before, so the
            // next one carries on from here.
            showFresh();
            if (changed)
            {
                for (const auto& [path, item] : before)
                    if (seen.count (path) == 0)
                        index.addChildElement (toXml (item.second, item.first).release());
                index.writeTo (indexFile);
            }
            return;
        }
        cacheEntries (found);
        {
            std::lock_guard<std::mutex> guard (lock);
            entries = std::move (found);
            packsKnown = false;
            ++generation;
            matchScores.clear();
            indexLoaded = true;   // what's listed is newer than the index
        }
        // Written only when something changed (a file read anew, or one gone): 10,000 grooves are megabytes.
        if (changed || seen.size() != before.size() || ! indexFile.existsAsFile())
            index.writeTo (indexFile);
        lastChecked = juce::Time::getCurrentTime();
        std::lock_guard<std::mutex> guard (sessionLock);
        checkedThisSession[indexFile.getFullPathName()] = foldersChecked() + "#" + indexStamp (indexFile);
    }

    void Library::setFocus (const juce::File& folder)
    {
        std::lock_guard<std::mutex> guard (focusLock);
        focus = folder;
    }

    juce::File Library::getFocus() const
    {
        std::lock_guard<std::mutex> guard (focusLock);
        return focus;
    }

    bool Library::hasUnlisted (const juce::File& folder, int upTo) const
    {
        // MIDI files in `folder` (and its subfolders, the first `upTo`) the list doesn't have: new since the last check.
        if (! folder.isDirectory())
            return false;
        std::set<juce::String> listed;
        {
            std::lock_guard<std::mutex> guard (lock);
            for (const auto& e : entries)
                if (e.file.isAChildOf (folder))
                    listed.insert (e.file.getFullPathName());
        }
        auto count = 0;
        for (const auto& item : juce::RangedDirectoryIterator (folder, true, "*.mid;*.midi", juce::File::findFiles))
        {
            if (listed.count (item.getFile().getFullPathName()) == 0)
                return true;
            if (++count >= upTo)
                break;
        }
        return false;
    }

    std::vector<Entry> Library::getEntries() const
    {
        std::lock_guard<std::mutex> guard (lock);
        return entries;
    }

    int Library::getNumEntries() const
    {
        std::lock_guard<std::mutex> guard (lock);
        return (int) entries.size();
    }

    juce::StringArray Entry::tagged (const juce::String& group) const
    {
        juce::StringArray values;
        const auto prefix = group + ":";
        for (const auto& t : tags)
            if (t.startsWith (prefix))
                values.add (t.substring (prefix.length()));
        return values;
    }

    juce::StringArray valuesOf (const Entry& e, Column c)
    {
        switch (c)
        {
            case Column::genre: return e.tagged ("Genre");
            case Column::playStyle: return e.tagged ("Play Style");
            case Column::type:
            {
                if (auto labelled = e.tagged ("Type"); ! labelled.isEmpty())
                    return labelled;
                // A MIDI file's, ours: how it's felt, and what it is.
                const auto kind = e.part == Part::fill ? "Fill" : e.part == Part::ending ? "Ending" : "Beat";
                return { e.feel == Feel::swung ? "Swing" : "Straight", kind };
            }
            case Column::time: return { e.signature() };
            case Column::hand:
            {
                if (auto labelled = e.tagged ("Power Hand"); ! labelled.isEmpty())
                    return labelled;
                return e.hand == PowerHand::none ? juce::StringArray() : juce::StringArray { powerHandNames[(int) e.hand] };
            }
            case Column::tempo:
            {
                if (! e.bpmKnown)
                    return { "Unknown" };
                const auto bpm = juce::roundToInt (e.bpm);
                if (bpm < 80) return { "Below 80" };
                if (bpm >= 160) return { "160 and up" };
                const auto low = bpm / 20 * 20;
                return { juce::String (low) + "-" + juce::String (low + 19) };
            }
            case Column::section: return { partNames[(int) e.part] };
        }
        return {};
    }

    const juce::StringArray& valuesIn (const Entry& e, Column c, juce::StringArray& scratch)
    {
        if (e.cached != nullptr)
            return e.cached->values[(size_t) c];
        scratch = valuesOf (e, c);
        return scratch;
    }

    void cacheEntries (std::vector<Entry>& list, bool keepValues)
    {
        // Each groove's column values and search text, and its place by name, worked out once (in the scanner's
        // thread when it's the one listing them).
        std::vector<size_t> order (list.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort (order.begin(), order.end(), [&list] (size_t a, size_t b)
        {
            if (const auto c = list[a].family.compareNatural (list[b].family); c != 0)
                return c < 0;
            return list[a].name.compareNatural (list[b].name) < 0;
        });
        std::vector<int> rank (list.size());
        for (size_t k = 0; k < order.size(); ++k)
            rank[order[k]] = (int) k;
        for (size_t i = 0; i < list.size(); ++i)
        {
            auto& e = list[i];
            e.nameRank = rank[i];
            if (keepValues && e.cached != nullptr)
                continue;   // (0.33.1) worked out before
            e.cached = nullptr;
            auto c = std::make_shared<Entry::Cached>();
            for (int col = 0; col < numColumns; ++col)
                c->values[(size_t) col] = valuesOf (e, (Column) col);
            c->haystack = (e.name + " " + e.family + " " + e.file.getParentDirectory().getParentDirectory().getFileName()).toLowerCase();
            e.cached = std::move (c);
        }
    }

    juce::String valueOf (const Entry& e, Column c)
    {
        if (e.cached != nullptr)
            return e.cached->values[(size_t) c].joinIntoString (", ");
        return valuesOf (e, c).joinIntoString (", ");
    }

    juce::StringArray orderOf (Column c)
    {
        switch (c)
        {
            case Column::type:
                return { "Straight", "Swing", "Beat", "Fill", "Ending", "Normal Time", "Half Time", "Double Time",
                         "Soft Hits", "Medium Hits", "Hard Hits" };
            case Column::playStyle: return { "Standard" };
            case Column::hand: return { "Hi-Hat Closed", "Hi-Hat Open", "Hi-Hat Pedal", "Ride", "Ride Crash", "Crash", "China", "Splash", "Percussion" };
            case Column::tempo: return { "Below 80", "80-99", "100-119", "120-139", "140-159", "160 and up", "Unknown" };
            case Column::section: return partNames;
            case Column::genre:
            case Column::time: break;
        }
        return {};
    }

    juce::StringArray sortedValues (Column c, juce::StringArray values)
    {
        const auto order = orderOf (c);
        std::sort (values.begin(), values.end(), [&] (const juce::String& a, const juce::String& b)
        {
            if (c == Column::time)
            {
                const auto da = a.fromFirstOccurrenceOf ("/", false, false).getIntValue(), db = b.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                return da != db ? da < db : a.getIntValue() < b.getIntValue();
            }
            const auto ia = order.indexOf (a), ib = order.indexOf (b);
            if (ia != ib)
                return ia < 0 ? false : ib < 0 ? true : ia < ib;
            return a.compareNatural (b) < 0;
        });
        return values;
    }

    std::vector<Entry> Library::query (const juce::File& within, const juce::String& text, bool favouritesOnly, SortOrder sort,
                                       double songBpm, int songNumerator, int songDenominator, const Filters& filters,
                                       const TapSearch* tap) const
    {
        juce::StringArray words;
        words.addTokens (text.toLowerCase(), " ", "\"");
        words.removeEmptyStrings();
        std::vector<Entry> result;
        {
            std::lock_guard<std::mutex> guard (lock);
            // Folders left out of searches (0.16.0): only when browsing inside them.
            std::vector<juce::File> leftOut;
            for (const auto& f : folders)
                if (f.excluded && ! (within == f.folder || within.isAChildOf (f.folder)))
                    leftOut.push_back (f.folder);
            if (tap != nullptr)
            {
                const auto key = tap->riff != nullptr ? "riff#" + tap->riff->key
                                                      : tap->rhythm.encode() + "#" + juce::String (tap->rhythm.slackBeats, 6) + "#" + juce::String (tap->grid, 6);
                if (key != matchKey)
                {
                    matchKey = key;
                    matchScores.clear();
                }
            }
            for (const auto& e : entries)
            {
                if (within != juce::File() && ! (subfolders ? e.file.isAChildOf (within) : e.file.getParentDirectory() == within))
                    continue;
                if (favouritesOnly && ! favourites.contains (e.file.getFullPathName()))
                    continue;
                if (std::any_of (leftOut.begin(), leftOut.end(), [&e] (const juce::File& f) { return e.file.isAChildOf (f); }))
                    continue;
                if (! words.isEmpty())
                {
                    const auto haystack = e.cached != nullptr ? e.cached->haystack
                        : (e.name + " " + e.family + " " + e.file.getParentDirectory().getParentDirectory().getFileName()).toLowerCase();
                    if (! std::all_of (words.begin(), words.end(), [&] (const juce::String& w) { return haystack.contains (w); }))
                        continue;
                }
                if (! filters.passes (e))
                    continue;
                result.push_back (e);
                if (tap != nullptr)
                {
                    const auto key = e.file.getFullPathName();
                    if (const auto known = matchScores.find (key); known != matchScores.end())
                        result.back().match = known->second;
                    else
                        result.back().match = matchScores[key] = e.rhythm == nullptr ? 0.0f
                            : tap->riff != nullptr ? (float) riffMatch (*tap->riff, *e.rhythm, e.feel)
                            : (float) match (tap->rhythm, *e.rhythm, tap->grid);
                }
            }
        }
        const auto byName = [] (const Entry& a, const Entry& b)
        {
            if (a.cached != nullptr && b.cached != nullptr)
                return a.nameRank < b.nameRank;
            if (const auto c = a.family.compareNatural (b.family); c != 0)
                return c < 0;
            return a.name.compareNatural (b.name) < 0;
        };
        if (tap != nullptr)
        {
            std::stable_sort (result.begin(), result.end(), [&] (const Entry& a, const Entry& b)
            {
                return a.match != b.match ? a.match > b.match : byName (a, b);
            });
            return result;
        }
        std::sort (result.begin(), result.end(), [&] (const Entry& a, const Entry& b)
        {
            if (sort.songSignature)
            {
                const auto ma = a.numerator == songNumerator && a.denominator == songDenominator;
                const auto mb = b.numerator == songNumerator && b.denominator == songDenominator;
                if (ma != mb)
                    return ma;
            }
            if (sort.songTempo)
            {
                const auto da = std::abs (a.bpm - songBpm), db = std::abs (b.bpm - songBpm);
                if (std::abs (da - db) > 1.0e-6)
                    return da < db;
            }
            if (sort.priority)
            {
                if (a.priority != b.priority)
                    return a.priority < b.priority;
                if (std::abs (a.intensity - b.intensity) > 1.0e-6f)
                    return a.intensity < b.intensity;   // the gentler groove first (our own measure)
            }
            return byName (a, b);
        });
        return result;
    }

    bool Library::isFavourite (const juce::File& f) const
    {
        return favourites.contains (f.getFullPathName());
    }

    void Library::setFavourite (const juce::File& f, bool on)
    {
        if (on)
            favourites.addIfNotAlreadyThere (f.getFullPathName());
        else
            favourites.removeString (f.getFullPathName());
        save();
        sendChangeMessage();
    }

    void Library::setPreview (const Preview& p)
    {
        preview = p;
        save();
    }
}
