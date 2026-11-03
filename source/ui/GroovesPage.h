// THE GROOVES page (PR7-D 0.4.0; rebuilt in 0.16.0 to work the way the owner's reference drum software's groove tab
// does, in our own look and words). From the top:
// - the header: MENU (folders, which filter and result columns show, the sorting ticks, how grooves are dragged out),
//   back / forward through where you've browsed, ORIGINAL TEMPO, TEMPO (1/2x to 3/2x), VELOCITY, STOP;
// - FIND SIMILAR (TAP TO FIND..., and a drop zone for a MIDI file that shows the rhythm searched with) beside the
//   filter columns (GENRE, PLAY STYLE, TYPE, TIME, POWER HAND, and TEMPO and SECTION when shown): every value with how
//   many grooves have it, those none here have greyed; click to choose, right-click to exclude; right-click a column's
//   name to show or hide columns;
// - the active filters: SEARCH (suggesting filters and grooves as you type), CLEAR ALL FILTERS, and a chip for every
//   filter at work (click one to take it away; excluded ones red; RESET when TEMPO or VELOCITY is changed);
// - the browser: folders in columns, left to right (the groove packs, USER MIDI and your linked folders; then each
//   one's folders, two levels to a column), each a filter on the results; a chosen folder's menu (its note map,
//   leaving it out of searches, adding a folder, opening it, syncing);
// - the results: favourite, play (the row under the mouse), MATCH (a rhythm search), and the columns chosen, sortable
//   by clicking a column's name; right-click for COPY, SEARCH WITH TAGS, SEARCH WITH TAP TO FIND, SHOW IN BROWSER,
//   USE WITH SONG CREATOR and ours (ADD TO SONG, TO SEQUENCER);
// - GROOVE PARTS beside them (it folds away), with TO SEQUENCER and LAYER ONTO.
#pragma once

#include "ui/PaintCache.h"

#include "PluginProcessor.h"
#include "ui/TapPanel.h"
#include "ui/RiffPanel.h"

class GroovesPage final : public juce::Component,
                          public juce::DragAndDropContainer,
                          public juce::FileDragAndDropTarget,
                          private juce::TableListBoxModel,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit GroovesPage (ParagonX1AudioProcessor&);
    ~GroovesPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // For tests and snapshots.
    void refresh();                                   // the browser and the list, afresh
    const std::vector<grooves::Entry>& getResults() const noexcept { return results; }
    void chooseFolder (const juce::File&);            // as clicking it in the browser (shown there; nothing: all)
    juce::File getFolder() const noexcept { return within; }
    void setSearch (const juce::String&);
    void playRow (int row, bool loop);
    // THE FILTER COLUMNS: a value chosen (`exclude`: excluded) or let go, as clicking (right-clicking) it does.
    void toggleFilter (grooves::Column, const juce::String& value, bool exclude);
    void clearFilters();                              // CLEAR ALL FILTERS: filters, the folder, the search, a tap
    const grooves::Filters& getFilters() const noexcept { return filters; }
    juce::StringArray getColumnValues (grooves::Column) const;   // as listed, "value (count)" (greyed ones count 0)
    void showFilterColumn (grooves::Column, bool);
    bool isFilterColumnShown (grooves::Column c) const noexcept { return shownFilters[(size_t) c]; }
    // INTO THE SEQUENCER (0.5.0): the chosen groove as pattern A-D, as its button does.
    void toPattern (int pattern);
    // TAP TO FIND (0.6.0): the panel opened; a search with a rhythm (FIND, a MIDI file dropped, a groove's rhythm),
    // and back to none.
    void openTapPanel();
    void findByRhythm (const grooves::Rhythm&, double grid, const juce::String& what);
    void findSimilar (int row);
    bool findLikeFile (const juce::File&);
    void clearTap();
    bool isTapSearching() const noexcept { return tapSearch.has_value(); }
    TapPanel& getTapPanel() noexcept { return *tapPanel; }
    // RIFF MATCH (0.32.0): a riff (audio or MIDI) read (in the background; `wait`: here and now) and set up in its
    // panel, heard with the previews; a search with it; back to none.
    bool loadRiff (const juce::File&, bool wait = false);
    void openRiffPanel();
    void findByRiff (const grooves::Riff&);
    bool isRiffSearching() const noexcept { return tapSearch.has_value() && tapSearch->riff != nullptr; }
    RiffPanel& getRiffPanel() noexcept { return *riffPanel; }
    void chooseRiffFile();
    void putRiffAway();   // the riff gone (not heard, its search cleared)
    // GROOVE PARTS (0.7.0).
    void toggleDrumPart (int part);   // -1: ALL
    unsigned getDrumParts() const noexcept { return chosenParts; }
    std::array<int, grooves::numDrumParts> getPartCounts() const noexcept { return partCounts; }
    void layerOnto (int pattern);
    // THE SONG TRACK (0.8.0) and COPY (0.9.0).
    bool addRowToSong (int row, double beat = -1.0);
    std::function<void()> onAddedToSong;
    bool copyRowForSong (int row);
    // ONTO THE SEQUENCER's GRID (0.13.0).
    bool rowToPattern (int row, int pattern);
    bool partToPattern (int part, int pattern);
    juce::String describeRowDrop (int pattern) const;
    // USE WITH SONG CREATOR (0.10.0).
    std::function<void (const juce::File&)> onUseWithCreator;

    // 0.16.0.
    void searchWithTags (int row);        // its labels (genre, play style, type, time, power hand) as the filters
    void showInBrowser (int row);         // the browser opened to its folder, the groove chosen in the list
    bool goBack();                        // where you browsed before (false: nowhere)
    bool goForward();
    juce::StringArray getChips() const;   // the active filters' chips, as shown ("TYPE: Fill", "NOT TIME: 6/8"...)
    void removeChip (int index);          // as clicking it
    juce::StringArray getSuggestions() const;    // what the search suggests now ("TYPE: Fill", grooves' names)
    void acceptSuggestion (int index);           // as clicking one
    struct BrowserRow { bool heading = false; juce::File folder; juce::String label; };
    std::vector<std::vector<BrowserRow>> getBrowserColumns() const;   // the columns shown, left to right
    void setOriginalTempo (bool);
    void setSortOrder (grooves::SortOrder);

    // A MIDI file dropped on the page: a search with its first two bars.
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    juce::TableListBox& getTable() noexcept { return table; }

    enum Column { favourite = 1, name, family, signature, bars, tempo, hand, intensity, matchScore,
                  play, genre, playStyle, type, section };

private:
    // TableListBoxModel: the grooves.
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int column, bool forwards) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::String getCellTooltip (int row, int column) override;

    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files,
                                               bool& canMoveFiles) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void findPlayingRow();
    void paintPanels (juce::Graphics&);
    PaintCache panelCache;
    void requery();
    void rebuildBrowser();
    void browseTo (const juce::File& folder, bool remember);   // the folder chosen (history kept unless going back)
    void checkShownFolder();   // 0.33.2: new files in it: scanned, it first
    void savePreview();
    void showMenu();
    void showFolderMenu (const juce::File& folder);
    void showFilterColumnMenu();
    void applyResultColumns();
    void sortResults();
    void rebuildChips();
    void updateSuggestions();
    int folderIndexOf (const juce::File&) const;   // which linked folder holds it (-1: none)
    std::optional<grooves::Groove> readRow (int row) const;
    std::vector<BrowserRow> rowsFor (const juce::File& folder) const;   // a browser column's rows for a folder
    std::vector<juce::File> childrenOf (const juce::File&) const;
    // 0.30.0: each folder's subfolders, read from the disk once until the library changes or REFRESH (the browser asks
    // for them many times a click).
    mutable std::map<juce::String, std::vector<juce::File>> childCache;
    mutable std::map<juce::String, std::vector<BrowserRow>> rowCache;

    class BrowserColumn;
    class FilterColumn;
    class PartsList;
    class Chip;
    class DropZone;
    class RiffZone;
    class Suggestions;

    ParagonX1AudioProcessor& processor;
    grooves::Library& library;

    // The header.
    juce::TextButton menuButton { "MENU" }, backButton { "<" }, forwardButton { ">" }, originalTempo { "ORIGINAL TEMPO" },
                     stop { "STOP" }, partsToggle { "GROOVE PARTS" };
    juce::ComboBox factor;
    juce::Label tempoLabel { {}, "TEMPO" }, velocityLabel { {}, "VELOCITY" }, status, playing;
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    // FIND SIMILAR and the filter columns.
    juce::TextButton tapButton { "TAP TO FIND..." };
    std::unique_ptr<DropZone> dropZone;
    juce::TextButton riffButton { "LOAD RIFF..." };
    std::unique_ptr<RiffZone> riffZone;
    std::array<std::unique_ptr<FilterColumn>, grooves::numColumns> filterColumns;
    std::array<bool, grooves::numColumns> shownFilters {};
    grooves::Filters filters;
    std::vector<std::pair<int, juce::String>> filterOrder;   // the filters in the order they were chosen (their chips' order)

    // The active filters.
    juce::TextEditor search;
    juce::TextButton clearAll { "CLEAR ALL FILTERS" };
    std::vector<std::unique_ptr<Chip>> chips;
    juce::StringArray chipTexts;
    std::vector<std::function<void()>> chipActions;   // each chip's taking away (all of them, shown or not)
    juce::Rectangle<int> chipArea;
    std::unique_ptr<Suggestions> suggestions;
    std::vector<std::pair<int, juce::String>> suggested;   // (a filter column, its value), or (-1, a groove's name)
    std::array<juce::StringArray, grooves::numColumns> knownValues;   // every value each column has in the library
    bool favouritesOnly = false;

    // The browser.
    std::array<std::unique_ptr<BrowserColumn>, 3> browserColumns;
    std::vector<juce::File> browsePath;          // what's chosen in each column, left to right (all of them, not just the shown)
    std::vector<juce::File> history, future;     // back / forward
    juce::File within;                           // the folder filtering the results (nothing: all)
    bool userNeedsSync = false;
    int syncTick = 0;

    // The results.
    juce::TableListBox table { "Grooves", this };
    std::vector<grooves::Entry> results;
    int hoveredRow = -1;
    int headerSort = 0;                          // a result column clicked to sort by (0: the sorting ticks)
    bool headerForwards = true;

    // GROOVE PARTS, TO SEQUENCER, LAYER ONTO.
    std::unique_ptr<PartsList> partsList;
    juce::Label toSequencerLabel { {}, "TO SEQUENCER" }, layerLabel { {}, "LAYER ONTO" };
    std::array<juce::TextButton, 4> patternButtons, layerButtons;
    unsigned chosenParts = grooves::allParts;
    std::array<int, grooves::numDrumParts> partCounts {};
    juce::File partsOf;
    bool partsShown = true;

    // TAP TO FIND.
    std::unique_ptr<TapPanel> tapPanel;
    std::optional<grooves::Library::TapSearch> tapSearch;
    juce::String tapWhat;
    // RIFF MATCH.
    std::unique_ptr<RiffPanel> riffPanel;
    void riffChanged (const grooves::Riff&);   // heard as it is now (and searched again when searching with it)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // a background read finishing after the page went

    juce::String message;            // a moment's note in the status line
    double messageUntil = 0.0;
    std::unique_ptr<juce::FileChooser> chooser;
    bool scannedOnce = false;
    juce::File shownPlaying;
    int playingRow = -1;   // its row in `results` (-1: not listed), found once when it or the list changes
    double shownPosition = -2.0;
};
