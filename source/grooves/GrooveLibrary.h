// THE GROOVE PLAYER (PR7-D 0.4.0): the player's own groove folders ("linked folders"), every MIDI file in them read
// once in the background (and remembered between sessions), and the browser's queries: a folder as a filter (with
// or without its subfolders), text, favourites, sorting. Kept in the presets folder's "Grooves.xml", shared by every
// PR7-D: the grooves are on the computer, not in a song.
#pragma once

#include "Groove.h"
#include "Rhythm.h"

#include <juce_events/juce_events.h>

#include <map>
#include <mutex>

namespace grooves
{
    // What the browser lists for a groove (its hits are read again when it's played or dragged).
    struct Entry
    {
        juce::File file;
        int folder = 0;              // which linked folder it's in
        juce::String name;
        juce::String family;         // the folder it's in, by name ("Funky 16ths 102 BPM")
        double bpm = 120.0;
        bool bpmKnown = false;
        int numerator = 4, denominator = 4;
        int bars = 1;
        PowerHand hand = PowerHand::none;
        float intensity = 0.0f;
        int numHits = 0;
        Feel feel = Feel::straight;
        Part part = Part::beat;      // from the names (worked out when listed)
        std::shared_ptr<const Rhythm> rhythm;   // TAP TO FIND (0.6.0): its rhythm (shared: entries are copied a lot)
        float match = -1.0f;         // with a tap search: how close, 0 to 1 (else -1)
        // 0.16.0: what a groove database says of it (none for plain MIDI files), each "Group:Value" ("Genre:Pop/Rock",
        // "Play Style:Double Kick", "Type:Half Time", "Power Hand:Ride Crash"), and its place in the pack's own order
        // (lower first; 0 for most).
        juce::StringArray tags;
        int priority = 0;
        // 0.30.0: worked out once when the library lists it (null for an entry made elsewhere: worked out each time),
        // so the page's filters, counts, search and sorting don't redo them for every groove on every click.
        struct Cached
        {
            std::array<juce::StringArray, 7> values;   // valuesOf each Column
            juce::String haystack;                    // what the search looks in, lower case
            int nameRank = 0;                         // its place by family then name (natural order)
        };
        std::shared_ptr<const Cached> cached;

        juce::StringArray tagged (const juce::String& group) const;   // the values of one group of its tags

        juce::String signature() const { return juce::String (numerator) + "/" + juce::String (denominator); }
        double lengthBeats() const noexcept { return bars * numerator * 4.0 / denominator; }
    };

    // THE FILTER COLUMNS (0.5.0; 0.16.0 as groove packs label their grooves): each groove's values in each, as text. A
    // groove can have several in a column (TYPE: "Beat", "Straight", "Half Time", "Soft Hits"...) or none (GENRE and
    // PLAY STYLE come only from groove databases).
    // - GENRE, PLAY STYLE: the database's labels.
    // - TYPE: the database's; a MIDI file's from its notes and names: Straight or Swing (FEEL), and Beat, Fill or Ending.
    // - TIME: the time signature. POWER HAND: the database's, else ours from the notes.
    // - TEMPO: in steps of 20 BPM. SECTION: from the names (Intro, Verse, Chorus...): our own, as before.
    enum class Column { genre, playStyle, type, time, hand, tempo, section };
    inline constexpr int numColumns = 7;
    static_assert (std::tuple_size_v<decltype (Entry::Cached::values)> == numColumns);
    inline const juce::StringArray columnNames { "GENRE", "PLAY STYLE", "TYPE", "TIME", "POWER HAND", "TEMPO", "SECTION" };
    inline constexpr std::array<bool, numColumns> shownByDefault { true, true, true, true, true, false, false };
    juce::StringArray valuesOf (const Entry&, Column);
    // The same, without a copy when the entry has them cached (`scratch` holds them when it hasn't).
    const juce::StringArray& valuesIn (const Entry&, Column, juce::StringArray& scratch);
    // Works out each entry's `cached` (the library does it whenever it lists them).
    void cacheEntries (std::vector<Entry>&);
    juce::String valueOf (const Entry&, Column);   // its values, joined (", ")
    // The order a column lists the values it knows (others follow, alphabetically; TIME, simplest first).
    juce::StringArray orderOf (Column);
    juce::StringArray sortedValues (Column, juce::StringArray values);

    // Values chosen in each column (a groove passes with any of them) and values excluded (it passes with none of them);
    // the columns together (it passes them all).
    struct Filters
    {
        std::array<juce::StringArray, numColumns> include, exclude;
        bool isEmpty() const noexcept
        {
            return std::all_of (include.begin(), include.end(), [] (const auto& a) { return a.isEmpty(); })
                && std::all_of (exclude.begin(), exclude.end(), [] (const auto& a) { return a.isEmpty(); });
        }
        bool passes (const Entry& e) const
        {
            for (int c = 0; c < numColumns; ++c)
            {
                const auto& in = include[(size_t) c];
                const auto& out = exclude[(size_t) c];
                if (in.isEmpty() && out.isEmpty())
                    continue;
                juce::StringArray scratch;
                const auto& values = valuesIn (e, (Column) c, scratch);
                // TYPE's values describe different things at once (how it's felt, what it is, how hard): every one
                // chosen must be there. The other columns: any one.
                if (c == (int) Column::type)
                {
                    if (! std::all_of (in.begin(), in.end(), [&values] (const juce::String& v) { return values.contains (v); }))
                        return false;
                }
                else if (! in.isEmpty() && ! std::any_of (values.begin(), values.end(), [&in] (const juce::String& v) { return in.contains (v); }))
                    return false;
                if (std::any_of (values.begin(), values.end(), [&out] (const juce::String& v) { return out.contains (v); }))
                    return false;
            }
            return true;
        }
    };

    struct LinkedFolder
    {
        juce::File folder;
        // Which drum each note in its files is ("FILES' MAP"): groove packs are written for different layouts.
        synth::drums::notemap::Layout layout = synth::drums::notemap::Layout::grooveLibrary;
        bool user = false;       // 0.16.0: the USER MIDI folder (PR7-D's own, in the presets folder: files dropped in)
        bool excluded = false;   // 0.16.0: left out of searches and filters (listed only when browsing inside it)
    };

    enum class Sort { name, songTempo, songSignature };
    inline const juce::StringArray sortNames { "Name", "Nearest Song Tempo", "Song Time Signature First" };
    // 0.16.0: the sorting ticks, together: the song's time signature first, then nearest the song's tempo, then the
    // packs' own order (simplest first: a database's priority, else the gentler groove), then by folder and name.
    struct SortOrder
    {
        bool priority = false, songTempo = false, songSignature = false;
        SortOrder() = default;
        SortOrder (Sort s) : songTempo (s == Sort::songTempo), songSignature (s == Sort::songSignature) {}
        bool operator== (const SortOrder& o) const noexcept
        {
            return priority == o.priority && songTempo == o.songTempo && songSignature == o.songSignature;
        }
    };

    class Library final : public juce::ChangeBroadcaster
    {
    public:
        // `settings`: where the folders, favourites and the preview's settings are kept; `index`: what was read (listed
        // by loadIndex, not here: every PR7-D in a song makes a Library, most never open GROOVES).
        Library (juce::File settings, juce::File index);
        ~Library() override;

        // THE FOLDERS. Adding or removing one, or changing its map, reads the folders again (in the background).
        const std::vector<LinkedFolder>& getFolders() const noexcept { return folders; }
        bool addFolder (const juce::File&);
        void removeFolder (int);
        void setLayout (int folder, synth::drums::notemap::Layout);
        bool getSubfolders() const noexcept { return subfolders; }
        void setSubfolders (bool);
        // How a folder's files' notes become drums: its map (CUSTOM: the plugin's own, `custom`; ROOT from `root`).
        synth::drums::notemap::Map layoutFor (int folder, const synth::drums::notemap::Map& custom, int root) const;

        // THE USER MIDI FOLDER (0.16.0): PR7-D's own (made if missing), always listed, last; files dropped onto it (or a
        // folder inside it) are copied in and read. Not removed with removeFolder.
        void setUserFolder (const juce::File&);
        int getUserFolder() const noexcept;   // its index (-1: none)
        int addToUserFolder (const juce::StringArray& files, const juce::File& into);   // how many MIDI files copied
        // Left out of searches and filters (0.16.0, any folder): its grooves listed only when browsing inside it.
        void setExcluded (int folder, bool);
        // Whether a folder's MIDI files on disk differ from what was read (a quick look: how many, and none newer than
        // the last check): the USER MIDI folder's, which files are dropped into from outside.
        bool needsSync (int folder) const;
        // The groove packs (the folder inside each groove database) among what's listed.
        std::vector<juce::File> getPacks() const;

        // What was read last time, listed at once (once; a scan brings it up to date). The page calls it on opening.
        void loadIndex();

        // READING. scan() starts reading in the background (listeners hear when it's done, and as it goes): it lists
        // what was read before at once, then checks the folders for new or changed files, reading only those.
        // `onlyIfNeeded` (0.15.2, the page opening): not if this DAW session has checked these folders already, with
        // this index (every PR7-D in a song shares that: only the first checks). scanNow() checks on this thread.
        void scan (bool onlyIfNeeded = false);
        void scanNow();
        bool isScanning() const noexcept { return scanning.load(); }
        int getNumScanned() const noexcept { return scanned.load(); }   // files read anew (not known from before)

        // THE GROOVES (a copy: safe while the scan replaces them).
        std::vector<Entry> getEntries() const;
        int getNumEntries() const;

        // THE BROWSER: the grooves in `within` (nothing: everywhere; a folder: its files, and its subfolders' if
        // SUBFOLDERS is on) whose name or folder has every word of `text`, favourites only if asked, sorted.
        // TAP TO FIND (0.6.0): with `tap`, every groove gets its match to the tapped rhythm on the `grid`, and the list
        // is sorted by it, closest first (instead of `sort`).
        struct TapSearch { Rhythm rhythm; double grid = 0.5; };
        std::vector<Entry> query (const juce::File& within, const juce::String& text, bool favouritesOnly, SortOrder sort,
                                  double songBpm, int songNumerator, int songDenominator, const Filters& filters = {},
                                  const TapSearch* tap = nullptr) const;

        // FAVOURITES.
        bool isFavourite (const juce::File&) const;
        void setFavourite (const juce::File&, bool);

        // THE PREVIEW's settings, kept with the rest.
        struct Preview
        {
            bool songTempo = true;            // the song's tempo (else each groove's own)
            TempoFactor factor = TempoFactor::normal;
            float velocity = 1.0f;            // 0.25 to 1.75: every hit's velocity scaled
            bool forThisKit = true;           // dragged out: on this kit's notes (else as the file has them)
            Sort sort = Sort::name;           // (before 0.16.0: one choice; read into sortOrder)
            SortOrder sortOrder;
            // 0.16.0: which filter columns and result columns show (by name: "GENRE", "NAME"...); empty: the defaults.
            juce::StringArray filterColumns, resultColumns;
        };
        Preview getPreview() const noexcept { return preview; }
        void setPreview (const Preview&);

        static constexpr int maxFiles = 100000;   // read no more than this (a whole drive linked by mistake)

    private:
        void load();
        void save() const;
        void read (const std::atomic<bool>* stop, bool check);
        juce::String foldersChecked() const;
        bool checkedThisSessionAlready() const;
        void stopScan();   // the folders are only changed with no scan running
        class Scanner;

        juce::File settingsFile, indexFile;
        std::vector<LinkedFolder> folders;
        bool subfolders = true;
        juce::StringArray favourites;   // full paths, sorted
        juce::Time lastChecked;         // the last check of the folders finished (needsSync)
        Preview preview;

        std::atomic<bool> indexLoaded { false };   // set by a scan too
        mutable std::mutex lock;        // guards entries
        std::vector<Entry> entries;
        mutable std::vector<juce::File> knownPacks;   // getPacks's, while packsKnown (both under the lock)
        mutable bool packsKnown = false;
        // 0.31.0: each groove's match for the last tap search (filters and folders clicked during one ask again).
        mutable juce::String matchKey;
        mutable std::map<juce::String, float> matchScores;
        std::atomic<bool> scanning { false };
        std::atomic<int> scanned { 0 };
        std::unique_ptr<Scanner> scanner;
    };
}
