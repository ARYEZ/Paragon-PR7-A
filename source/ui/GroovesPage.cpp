#include "GroovesPage.h"
#include "grooves/GrooveDatabase.h"

#include <map>
#include <set>

#include "ui/DockPanels.h"
#include "ui/Icons.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int rowHeight = 26;
    constexpr int browserRowHeight = 22;
    const juce::String dragPrefix = "groove:";

    juce::String tempoText (const grooves::Entry& e)
    {
        // Whole numbers as such: a file keeps its tempo in microseconds a beat, so 104 reads back as 104.0001.
        const auto whole = std::abs (e.bpm - std::round (e.bpm)) < 0.05;
        return e.bpmKnown ? (whole ? juce::String (juce::roundToInt (e.bpm)) : juce::String (e.bpm, 1)) : juce::String ("-");
    }

    // The results' columns: their names, widths, and whether they show at first.
    struct ResultColumn { int id; const char* title; int width; bool shown; bool sortable; };
    const ResultColumn resultColumns[] {
        { GroovesPage::favourite, "", 28, true, true },
        { GroovesPage::play, "", 28, true, false },
        { GroovesPage::matchScore, "MATCH", 70, true, true },
        { GroovesPage::name, "NAME", 190, true, true },
        { GroovesPage::family, "FAMILY", 190, true, true },
        { GroovesPage::intensity, "INTENSITY", 110, true, true },
        { GroovesPage::hand, "POWER HAND", 110, true, true },
        { GroovesPage::signature, "SIGN.", 54, true, true },
        { GroovesPage::bars, "BARS", 50, true, true },
        { GroovesPage::tempo, "TEMPO", 62, true, true },
        { GroovesPage::genre, "GENRE", 120, false, true },
        { GroovesPage::playStyle, "PLAY STYLE", 110, false, true },
        { GroovesPage::type, "TYPE", 160, false, true },
        { GroovesPage::section, "SECTION", 90, false, true },
    };

    bool optionalColumn (int id) { return id != GroovesPage::favourite && id != GroovesPage::play && id != GroovesPage::matchScore && id != GroovesPage::name; }

    // A column of the results' value as text, for sorting and the optional columns.
    juce::String cellText (const grooves::Entry& e, int column)
    {
        switch (column)
        {
            case GroovesPage::name: return e.name;
            case GroovesPage::family: return e.family;
            case GroovesPage::hand: return grooves::valueOf (e, grooves::Column::hand);
            case GroovesPage::genre: return grooves::valueOf (e, grooves::Column::genre);
            case GroovesPage::playStyle: return grooves::valueOf (e, grooves::Column::playStyle);
            case GroovesPage::type: return grooves::valueOf (e, grooves::Column::type);
            case GroovesPage::section: return grooves::valueOf (e, grooves::Column::section);
            default: return {};
        }
    }

    void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (text, area, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// A column of the folder browser: headings (a folder whose folders are listed under it, or a group's name) and items.
class GroovesPage::BrowserColumn final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit BrowserColumn (GroovesPage& p) : page (p)
    {
        list.setModel (this);
        list.setRowHeight (browserRowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }

    void setRows (std::vector<BrowserRow> r, const juce::File& chosenOne)
    {
        rows = std::move (r);
        chosen = chosenOne;
        list.updateContent();
        list.repaint();
    }
    const std::vector<BrowserRow>& getRows() const noexcept { return rows; }
    int rowAt (juce::Point<int> inPage) const
    {
        const auto at = getLocalPoint (&page, inPage);
        return getLocalBounds().contains (at) ? list.getRowContainingPosition (at.x, at.y) : -1;
    }
    void resized() override { list.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& r = rows[(size_t) row];
        const auto isChosen = r.folder != juce::File() && r.folder == chosen;
        if (isChosen)
        {
            g.setColour (ParagonColours::accentDim.withAlpha (0.7f));
            g.fillRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, 3.0f);
            // The folder's menu.
            const auto box = juce::Rectangle<float> ((float) width - 22.0f, 3.0f, 18.0f, (float) height - 6.0f);
            g.setColour (ParagonColours::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (box, 2.0f);
            juce::Path arrow;
            arrow.addTriangle (box.getCentreX() - 4.0f, box.getCentreY() - 2.0f, box.getCentreX() + 4.0f, box.getCentreY() - 2.0f,
                               box.getCentreX(), box.getCentreY() + 3.0f);
            g.setColour (ParagonColours::text);
            g.fillPath (arrow);
        }
        auto x = 6.0f;
        if (r.heading)
        {
            g.setColour (isChosen ? ParagonColours::accent : r.folder == juce::File() ? ParagonColours::textDim : ParagonColours::text);
            g.setFont (juce::FontOptions (r.folder == juce::File() ? 10.5f : 12.5f, juce::Font::bold));
        }
        else
        {
            // A small folder.
            const auto icon = juce::Rectangle<float> (x + 2.0f, (float) height * 0.5f - 5.0f, 13.0f, 10.0f);
            g.setColour (isChosen ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.8f));
            g.fillRoundedRectangle (icon.withWidth (6.0f).translated (0.0f, -2.0f).withHeight (4.0f), 1.0f);
            g.fillRoundedRectangle (icon, 1.5f);
            x += 22.0f;
            g.setColour (isChosen ? ParagonColours::accent : ParagonColours::text.withAlpha (0.9f));
            g.setFont (juce::FontOptions (12.5f));
        }
        g.drawText (r.label, (int) x, 0, width - (int) x - 26, height, juce::Justification::centredLeft, true);
        // The USER MIDI folder out of step with its files on disk: an orange dot (SYNC FILE CHANGES in its menu).
        if (page.userNeedsSync && ! r.heading && page.library.getUserFolder() >= 0
            && r.folder == page.library.getFolders()[(size_t) page.library.getUserFolder()].folder)
        {
            g.setColour (ParagonColours::accent);
            g.fillEllipse ((float) width - (isChosen ? 34.0f : 14.0f), (float) height * 0.5f - 4.0f, 8.0f, 8.0f);
        }
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].folder == juce::File())
            return;   // a group's name
        const auto folder = rows[(size_t) row].folder;
        if (e.mods.isPopupMenu() || (folder == chosen && e.x > list.getWidth() - 26))
        {
            if (folder != chosen)
                page.browseTo (folder, true);
            page.showFolderMenu (folder);
            return;
        }
        page.browseTo (folder, true);
    }

    juce::String getTooltipForRow (int row) override
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].folder == juce::File())
            return {};
        return "Click: only the grooves in here (and its folders, with SHOW MIDI FROM SUBFOLDERS in MENU). The chosen "
               "folder's arrow (or right-click): its menu.";
    }

    GroovesPage& page;
    juce::ListBox list;
    std::vector<BrowserRow> rows;
    juce::File chosen;
};

//==============================================================================
// A filter column: its name with how many of its values are chosen and how many the grooves here have; every value
// the library has, those none here have greyed; click to choose, right-click to exclude.
class GroovesPage::FilterColumn final : public juce::Component, private juce::ListBoxModel
{
public:
    FilterColumn (GroovesPage& p, grooves::Column c) : page (p), column (c)
    {
        list.setModel (this);
        list.setRowHeight (19);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }

    void setValues (juce::StringArray v, juce::Array<int> c)
    {
        values = std::move (v);
        counts = std::move (c);
        list.updateContent();
        list.repaint();
        repaint();
    }
    juce::StringArray describe() const
    {
        juce::StringArray result;
        for (int i = 0; i < values.size(); ++i)
            result.add (values[i] + " (" + juce::String (counts[i]) + ")");
        return result;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& f = page.filters;
        const auto chosenHere = f.include[(size_t) column].size() + f.exclude[(size_t) column].size();
        const auto available = std::count_if (counts.begin(), counts.end(), [] (int n) { return n > 0; });
        g.setColour (chosenHere > 0 ? ParagonColours::accent : ParagonColours::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (grooves::columnNames[(int) column], 4, 0, getWidth() - 50, 16, juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String (chosenHere) + "/" + juce::String ((int) available), getWidth() - 48, 0, 44, 16,
                    juce::Justification::centredRight, false);
        g.setColour (ParagonColours::panelEdge);
        g.drawVerticalLine (getWidth() - 1, 2.0f, (float) getHeight() - 2.0f);
    }
    void resized() override { list.setBounds (getLocalBounds().withTrimmedTop (18).withTrimmedRight (3)); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 18)
            page.showFilterColumnMenu();
    }

private:
    int getNumRows() override { return values.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || row >= values.size())
            return;
        const auto& f = page.filters;
        const auto included = f.include[(size_t) column].contains (values[row]);
        const auto excluded = f.exclude[(size_t) column].contains (values[row]);
        const auto none = counts[row] == 0;
        if (included || excluded)
        {
            g.setColour ((excluded ? ParagonColours::meterRed : ParagonColours::accentDim).withAlpha (excluded ? 0.25f : 0.7f));
            g.fillRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, 3.0f);
        }
        const auto colour = excluded ? ParagonColours::meterRed : included ? ParagonColours::accent
                          : none ? ParagonColours::textDim.withAlpha (0.45f) : ParagonColours::text;
        g.setColour (colour);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (values[row], 6, 0, width - 40, height, juce::Justification::centredLeft, true);
        if (excluded)
            g.drawHorizontalLine (height / 2, 6.0f, 6.0f + (float) juce::jmin (width - 46, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f), values[row])));
        if (! none)
        {
            g.setColour (colour.withAlpha (0.55f));
            g.drawText (juce::String (counts[row]), width - 36, 0, 32, height, juce::Justification::centredRight, false);
        }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= values.size())
            return;
        const auto& f = page.filters;
        const auto chosenAlready = f.include[(size_t) column].contains (values[row]) || f.exclude[(size_t) column].contains (values[row]);
        if (counts[row] == 0 && ! chosenAlready)
            return;   // greyed: none of the grooves here has it
        page.toggleFilter (column, values[row], e.mods.isPopupMenu());
    }
    juce::String getTooltipForRow (int) override
    {
        return "Click: only grooves with this (in TYPE, with every one chosen; elsewhere, any). Right-click: none with it. "
               "Greyed: none of the grooves here has it. Right-click the column's name to show or hide columns.";
    }

    GroovesPage& page;
    grooves::Column column;
    juce::ListBox list;
    juce::StringArray values;
    juce::Array<int> counts;
};

//==============================================================================
// GROOVE PARTS: ALL, then each part with how many hits the chosen groove has in it; click ticks it, a drag takes it out.
class GroovesPage::PartsList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PartsList (GroovesPage& p) : page (p)
    {
        list.setModel (this);
        list.setRowHeight (23);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTooltip ("The chosen groove's parts. Ticked: heard in the preview, dragged out with it, laid into a pattern with "
                         "LAYER ONTO. Drag a part to take just it out, or onto a block in the song.");
        addAndMakeVisible (list);
    }
    void refresh() { list.updateContent(); list.repaint(); }
    void resized() override { list.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return 1 + grooves::numDrumParts; }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto part = row - 1;
        const auto ticked = part < 0 ? page.chosenParts == grooves::allParts : (page.chosenParts & (1u << part)) != 0;
        const auto count = part < 0 ? 0 : page.partCounts[(size_t) part];
        const auto used = part < 0 || count > 0;
        const auto box = juce::Rectangle<float> (6.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f);
        g.setColour (ticked ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.6f));
        if (ticked)
            g.fillRoundedRectangle (box, 2.0f);
        else
            g.drawRoundedRectangle (box, 2.0f, 1.0f);
        g.setColour (! used ? ParagonColours::textDim.withAlpha (0.5f) : ticked ? ParagonColours::text : ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.5f, part < 0 ? juce::Font::bold : juce::Font::plain));
        g.drawText (part < 0 ? juce::String ("ALL") : grooves::drumPartNames[part], 26, 0, width - 64, height, juce::Justification::centredLeft, true);
        if (part >= 0 && count > 0)
            g.drawText (juce::String (count), width - 40, 0, 34, height, juce::Justification::centredRight, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.toggleDrumPart (row - 1); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        return rows.isEmpty() || rows[0] < 1 ? juce::var() : juce::var ("part:" + juce::String (rows[0] - 1));
    }

    GroovesPage& page;
    juce::ListBox list;
};

//==============================================================================
// An active filter: its text and a cross; a click takes it away (excluded ones red, RESET in the accent).
class GroovesPage::Chip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Chip (juce::String t, juce::Colour c, std::function<void()> click) : text (std::move (t)), colour (c), onClick (std::move (click))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click to take this filter away.");
    }
    int idealWidth() const { return juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.5f, juce::Font::bold), text) + 34; }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f, 3.0f);
        g.setColour (colour.withAlpha (isMouseOver() ? 0.35f : 0.2f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (text, r.toNearestInt().withTrimmedLeft (8).withTrimmedRight (20), juce::Justification::centredLeft, true);
        const auto x = r.getRight() - 13.0f, y = r.getCentreY();
        g.drawLine (x - 3.5f, y - 3.5f, x + 3.5f, y + 3.5f, 1.4f);
        g.drawLine (x - 3.5f, y + 3.5f, x + 3.5f, y - 3.5f, 1.4f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
            onClick();
    }
    const juce::String& getText() const noexcept { return text; }

private:
    juce::String text;
    juce::Colour colour;
    std::function<void()> onClick;
};

//==============================================================================
// FIND SIMILAR's drop zone: drop a MIDI file to find grooves like it; after a rhythm search, the rhythm searched with
// (each drum group's hits across its bars) and a cross to clear it.
class GroovesPage::DropZone final : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit DropZone (GroovesPage& p) : page (p)
    {
        setTooltip ("Drop a MIDI file here to list the grooves whose rhythm is closest to it. After TAP TO FIND or a search by "
                    "rhythm, the rhythm searched with; the cross clears it.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::Path outline;
        outline.addRoundedRectangle (r, 4.0f);
        juce::Path dashed;
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (hovering ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.6f));
        g.fillPath (dashed);
        if (! page.tapSearch.has_value())
        {
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("MIDI DROP ZONE", r, juce::Justification::centred, false);
            return;
        }
        // The rhythm: a row of dots per drum group.
        const auto& rhythm = page.tapSearch->rhythm;
        const auto area = r.reduced (8.0f, 6.0f).withTrimmedRight (14.0f);
        const auto rowH = area.getHeight() / (float) grooves::numLanes;
        for (int lane = 0; lane < grooves::numLanes; ++lane)
        {
            const auto y = area.getY() + rowH * ((float) lane + 0.5f);
            g.setColour (ParagonColours::panelEdge);
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            g.setColour (ParagonColours::accent);
            for (const auto beat : rhythm.beats[(size_t) lane])
            {
                const auto x = area.getX() + area.getWidth() * (float) (beat / juce::jmax (1.0, rhythm.lengthBeats));
                g.fillEllipse (x - 1.8f, y - 1.8f, 3.6f, 3.6f);
            }
        }
        // The cross.
        const auto cx = r.getRight() - 9.0f, cy = r.getY() + 9.0f;
        g.setColour (ParagonColours::text);
        g.drawLine (cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, 1.4f);
        g.drawLine (cx - 3.5f, cy + 3.5f, cx + 3.5f, cy - 3.5f, 1.4f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (page.tapSearch.has_value() && e.x > getWidth() - 20 && e.y < 20)
            page.clearTap();
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && juce::File (files[0]).hasFileExtension ("mid;midi");
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { hovering = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hovering = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        hovering = false;
        if (! files.isEmpty())
            page.findLikeFile (juce::File (files[0]));
        repaint();
    }

private:
    GroovesPage& page;
    bool hovering = false;
};

//==============================================================================
// What SEARCH suggests as you type: filter values first (click: that filter), then grooves' names.
class GroovesPage::Suggestions final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Suggestions (GroovesPage& p) : page (p)
    {
        list.setModel (this);
        list.setRowHeight (22);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }
    void setItems (juce::StringArray items)
    {
        texts = std::move (items);
        list.updateContent();
        list.repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff141015));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (ParagonColours::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
    void resized() override { list.setBounds (getLocalBounds().reduced (3)); }

private:
    int getNumRows() override { return texts.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= texts.size())
            return;
        const auto isFilter = page.suggested[(size_t) row].first >= 0;
        if (selected)
        {
            g.setColour (ParagonColours::accentDim.withAlpha (0.6f));
            g.fillRect (0, 0, width, height);
        }
        g.setColour (isFilter ? ParagonColours::accent : ParagonColours::text);
        g.setFont (juce::FontOptions (12.0f, isFilter ? juce::Font::bold : juce::Font::plain));
        g.drawText (texts[row], 8, 0, width - 12, height, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.acceptSuggestion (row); }

    GroovesPage& page;
    juce::ListBox list;
    juce::StringArray texts;
};

//==============================================================================
GroovesPage::GroovesPage (ParagonX1AudioProcessor& p) : processor (p), library (p.getGrooveLibrary())
{
    const auto p0 = library.getPreview();

    // THE HEADER.
    menuButton.setTooltip ("Folders (link one, show MIDI from subfolders), which filter and result columns show, the sorting, and "
                           "how grooves are dragged out.");
    menuButton.onClick = [this] { showMenu(); };
    backButton.setTooltip ("Back to the folder you browsed before.");
    backButton.onClick = [this] { goBack(); };
    forwardButton.setTooltip ("Forward again.");
    forwardButton.onClick = [this] { goForward(); };
    originalTempo.setTooltip ("On: grooves play at the tempo they were played at. Off: at your song's.");
    originalTempo.setClickingTogglesState (true);
    originalTempo.setToggleState (! p0.songTempo, juce::dontSendNotification);
    originalTempo.onClick = [this] { savePreview(); };
    factor.addItemList (grooves::tempoFactorNames, 1);
    factor.setTooltip ("Half or double time, or 2/3 (a 4/4 groove felt in 6/8) and 3/2 (6/8 to 4/4): only how fast it plays; a "
                       "groove dragged out or into the song gets it too.");
    factor.setSelectedItemIndex ((int) p0.factor, juce::dontSendNotification);
    factor.onChange = [this] { savePreview(); };
    velocity.setRange (25.0, 175.0, 1.0);
    velocity.setTextValueSuffix ("%");
    velocity.setTooltip ("Every hit's velocity scaled together, so the playing keeps its accents: 100% as played.");
    velocity.setValue (p0.velocity * 100.0, juce::dontSendNotification);
    velocity.onDragEnd = [this] { savePreview(); };
    velocity.onValueChange = [this] { if (! velocity.isMouseButtonDown()) savePreview(); };
    stop.setTooltip ("Stop the groove playing.");
    stop.onClick = [this] { processor.stopGroove(); };
    for (auto* l : { &tempoLabel, &velocityLabel })
    {
        l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setJustificationType (juce::Justification::centredRight);
    playing.setColour (juce::Label::textColourId, ParagonColours::accent);
    for (juce::Component* c : { (juce::Component*) &menuButton, (juce::Component*) &backButton, (juce::Component*) &forwardButton,
                                (juce::Component*) &originalTempo, (juce::Component*) &tempoLabel, (juce::Component*) &factor,
                                (juce::Component*) &velocityLabel, (juce::Component*) &velocity, (juce::Component*) &stop,
                                (juce::Component*) &playing, (juce::Component*) &status })
        addAndMakeVisible (c);

    // FIND SIMILAR and the filter columns.
    tapButton.setTooltip ("Tap a rhythm (on the pads, your controller or the keyboard) over a two-bar loop, and list the grooves "
                          "closest to it first.");
    tapButton.onClick = [this] { openTapPanel(); };
    addAndMakeVisible (tapButton);
    dropZone = std::make_unique<DropZone> (*this);
    addAndMakeVisible (*dropZone);
    for (int c = 0; c < grooves::numColumns; ++c)
    {
        filterColumns[(size_t) c] = std::make_unique<FilterColumn> (*this, (grooves::Column) c);
        shownFilters[(size_t) c] = p0.filterColumns.isEmpty() ? grooves::shownByDefault[(size_t) c]
                                                              : p0.filterColumns.contains (grooves::columnNames[c]);
        addChildComponent (*filterColumns[(size_t) c]);
        filterColumns[(size_t) c]->setVisible (shownFilters[(size_t) c]);
    }

    // THE ACTIVE FILTERS.
    search.setTextToShowWhenEmpty ("SEARCH", ParagonColours::textDim);
    search.setTooltip ("Words in a groove's name, its folders or its labels; as you type, matching filters and grooves are "
                       "suggested below (Enter takes a filter that matches exactly).");
    search.onTextChange = [this] { requery(); updateSuggestions(); };
    search.onReturnKey = [this]
    {
        if (! suggested.empty() && suggested.front().first >= 0 && suggested.front().second.equalsIgnoreCase (search.getText().trim()))
            acceptSuggestion (0);
        suggestions->setVisible (false);
    };
    search.onEscapeKey = [this] { suggestions->setVisible (false); };
    clearAll.setTooltip ("Every filter away: the columns, the folder, the search, a rhythm search, favourites.");
    clearAll.onClick = [this] { clearFilters(); };
    partsToggle.setTooltip ("Show or fold away GROOVE PARTS (and TO SEQUENCER, LAYER ONTO).");
    partsToggle.setClickingTogglesState (true);
    partsToggle.setToggleState (true, juce::dontSendNotification);
    partsToggle.onClick = [this] { partsShown = partsToggle.getToggleState(); resized(); };
    for (juce::Component* c : { (juce::Component*) &search, (juce::Component*) &clearAll, (juce::Component*) &partsToggle })
        addAndMakeVisible (c);

    // THE BROWSER.
    for (auto& column : browserColumns)
    {
        column = std::make_unique<BrowserColumn> (*this);
        addAndMakeVisible (*column);
    }

    // THE RESULTS.
    table.setRowHeight (rowHeight);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, ParagonColours::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, ParagonColours::textDim);
    header.setColour (juce::TableHeaderComponent::outlineColourId, ParagonColours::panelEdge);
    for (const auto& c : resultColumns)
    {
        auto columnFlags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
        if (c.sortable)
            columnFlags |= juce::TableHeaderComponent::sortable;
        if (c.id == favourite || c.id == play)
            columnFlags = juce::TableHeaderComponent::visible | (c.sortable ? juce::TableHeaderComponent::sortable : 0);
        header.addColumn (c.title, c.id, c.width, c.id == favourite || c.id == play ? c.width : 40, 600, columnFlags);
    }
    header.setStretchToFitActive (true);
    table.setMultipleSelectionEnabled (false);
    table.addMouseListener (this, true);
    addAndMakeVisible (table);
    applyResultColumns();

    // GROOVE PARTS, TO SEQUENCER, LAYER ONTO.
    partsList = std::make_unique<PartsList> (*this);
    addAndMakeVisible (*partsList);
    for (auto* l : { &toSequencerLabel, &layerLabel })
    {
        l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addAndMakeVisible (*l);
    }
    for (int i = 0; i < 4; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setButtonText (synth::drumseq::patternNames[i]);
        b.setTooltip ("The chosen groove as the drum sequencer's pattern " + synth::drumseq::patternNames[i] + " (replacing it): "
                      "each hit a note on its drum, LENGTH the groove's (16 beats at most), the sequencer playing it. Undo takes "
                      "it back.");
        b.onClick = [this, i] { toPattern (i); };
        addAndMakeVisible (b);
        auto& l = layerButtons[(size_t) i];
        l.setButtonText (synth::drumseq::patternNames[i]);
        l.setTooltip ("Lay the ticked parts of the chosen groove into pattern " + synth::drumseq::patternNames[i] + ": its notes on "
                      "those drums replaced by the groove's, its other drums kept. Undo takes it back.");
        l.onClick = [this, i] { layerOnto (i); };
        addAndMakeVisible (l);
    }

    // TAP TO FIND's panel, and the search's suggestions, over the rest.
    tapPanel = std::make_unique<TapPanel> (processor);
    tapPanel->onFind = [this] (const grooves::Rhythm& r, double grid)
    {
        tapPanel->close();
        findByRhythm (r, grid, "your taps");
    };
    addChildComponent (*tapPanel);
    suggestions = std::make_unique<Suggestions> (*this);
    addChildComponent (*suggestions);

    library.addChangeListener (this);
    refresh();
    startTimerHz (15);
}

GroovesPage::~GroovesPage()
{
    // The window closing: the preview stops (nothing would be left to stop it with).
    processor.stopGroove();
    library.removeChangeListener (this);
    table.removeMouseListener (this);
}

void GroovesPage::visibilityChanged()
{
    // The first time the page is shown: what was read before is listed (in the background), and the folders are
    // checked for changes once a DAW session (0.15.2).
    if (isShowing() && ! scannedOnce)
    {
        scannedOnce = true;
        refresh();
        if (! library.getFolders().empty())
            library.scan (true);
    }
}

void GroovesPage::refresh()
{
    // The folders on disk may have changed: read again when asked.
    childCache.clear();
    rowCache.clear();
    // Every value each filter column has anywhere in the library (those none of the grooves here have show greyed).
    std::array<std::set<juce::String>, grooves::numColumns> seen;
    juce::StringArray scratch;
    for (const auto& e : library.getEntries())
        for (int c = 0; c < grooves::numColumns; ++c)
            for (const auto& v : grooves::valuesIn (e, (grooves::Column) c, scratch))
                seen[(size_t) c].insert (v);
    for (int c = 0; c < grooves::numColumns; ++c)
    {
        juce::StringArray values;
        for (const auto& v : seen[(size_t) c])
            values.add (v);
        knownValues[(size_t) c] = grooves::sortedValues ((grooves::Column) c, values);
    }
    // The folder chosen still there? (A linked folder removed, a pack gone.)
    if (within != juce::File() && folderIndexOf (within) < 0)
        within = juce::File();
    rebuildBrowser();
    requery();
}

//==============================================================================
// THE BROWSER
std::vector<juce::File> GroovesPage::childrenOf (const juce::File& folder) const
{
    const auto key = folder.getFullPathName();
    if (const auto known = childCache.find (key); known != childCache.end())
        return known->second;
    std::vector<juce::File> kids;
    if (folder.isDirectory())
    {
        auto real = folder.findChildFiles (juce::File::findDirectories, false);
        real.sort();
        for (const auto& f : real)
            if (! f.isHidden())
                kids.push_back (f);
    }
    for (const auto& f : grooves::db::foldersIn (folder))   // a groove database's folders
        kids.push_back (f);
    std::stable_sort (kids.begin(), kids.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    childCache[key] = kids;
    return kids;
}

std::vector<GroovesPage::BrowserRow> GroovesPage::rowsFor (const juce::File& folder) const
{
    // Two levels to a column: a folder with folders inside is a heading with them under it; the others are listed
    // first, under the folder's own name.
    const auto key = folder.getFullPathName();
    if (const auto known = rowCache.find (key); known != rowCache.end())
        return known->second;
    std::vector<BrowserRow> leaves, groups;
    for (const auto& kid : childrenOf (folder))
    {
        const auto grand = childrenOf (kid);
        if (grand.empty())
        {
            leaves.push_back ({ false, kid, kid.getFileName() });
            continue;
        }
        groups.push_back ({ true, kid, kid.getFileName() });
        for (const auto& g : grand)
            groups.push_back ({ false, g, g.getFileName() });
    }
    std::vector<BrowserRow> rows;
    if (! leaves.empty())
    {
        rows.push_back ({ true, folder, folder.getFileName() });
        rows.insert (rows.end(), leaves.begin(), leaves.end());
    }
    rows.insert (rows.end(), groups.begin(), groups.end());
    rowCache[key] = rows;
    return rows;
}

std::vector<std::vector<GroovesPage::BrowserRow>> GroovesPage::getBrowserColumns() const
{
    std::vector<std::vector<BrowserRow>> all;
    // The first: the groove packs, USER MIDI, the linked folders.
    std::vector<BrowserRow> root;
    if (const auto packs = library.getPacks(); ! packs.empty())
    {
        root.push_back ({ true, {}, "GROOVE PACKS" });
        for (const auto& p : packs)
            root.push_back ({ false, p, p.getFileName() });
    }
    const auto& folders = library.getFolders();
    if (const auto user = library.getUserFolder(); user >= 0)
    {
        root.push_back ({ true, {}, "USER LIBRARIES" });
        root.push_back ({ false, folders[(size_t) user].folder, "User MIDI" });
    }
    std::vector<BrowserRow> linked;
    for (const auto& f : folders)
        if (! f.user)
            linked.push_back ({ false, f.folder, f.folder.getFileName() + (f.excluded ? "  (left out)" : "") });
    if (! linked.empty())
    {
        root.push_back ({ true, {}, "LINKED FOLDERS" });
        root.insert (root.end(), linked.begin(), linked.end());
    }
    all.push_back (root);
    for (const auto& chosen : browsePath)
    {
        auto rows = rowsFor (chosen);
        if (rows.empty())
            break;
        all.push_back (std::move (rows));
    }
    // The last three shown (the deepest).
    if (all.size() > browserColumns.size())
        all.erase (all.begin(), all.end() - (std::ptrdiff_t) browserColumns.size());
    return all;
}

void GroovesPage::rebuildBrowser()
{
    // The path to the folder chosen: from the first column's entry holding it (a pack before the folder it's in), down
    // through the columns: a heading's folder when it's the folder chosen, else the item under it.
    browsePath.clear();
    if (within != juce::File())
    {
        juce::File root;
        for (const auto& p : library.getPacks())
            if (within == p || within.isAChildOf (p))
                root = p;
        if (root == juce::File())
            for (const auto& f : library.getFolders())
                if ((within == f.folder || within.isAChildOf (f.folder)) && (root == juce::File() || f.folder.isAChildOf (root)))
                    root = f.folder;
        if (root != juce::File())
        {
            std::vector<juce::File> chain;   // root .. within
            for (auto f = within; f != root && f.getFullPathName().length() > root.getFullPathName().length(); f = f.getParentDirectory())
                chain.insert (chain.begin(), f);
            chain.insert (chain.begin(), root);
            browsePath.push_back (root);
            for (size_t i = 1; i < chain.size();)
            {
                // chain[i] is a heading in the column for chain[i - 1] when it has folders inside; the item to choose
                // then is the next one down (if the folder chosen is deeper).
                if (i + 1 < chain.size() && ! childrenOf (chain[i]).empty())
                {
                    browsePath.push_back (chain[i + 1]);
                    i += 2;
                }
                else
                {
                    browsePath.push_back (chain[i]);
                    ++i;
                }
            }
        }
    }
    const auto columns = getBrowserColumns();
    // Which of the shown columns' choices: the path's, lined up with the deepest columns.
    std::vector<juce::File> chosen (browsePath);
    std::vector<juce::File> choices;
    {
        size_t total = 1;   // how many columns there are in all
        for (const auto& f : browsePath)
        {
            if (rowsFor (f).empty())
                break;
            ++total;
        }
        const auto first = total > browserColumns.size() ? total - browserColumns.size() : 0;
        for (size_t k = first; k < total; ++k)
            choices.push_back (k < chosen.size() ? chosen[k] : juce::File());
    }
    for (size_t i = 0; i < browserColumns.size(); ++i)
    {
        browserColumns[i]->setVisible (i < columns.size());
        if (i < columns.size())
            browserColumns[i]->setRows (columns[i], i < choices.size() ? choices[i] : juce::File());
    }
    backButton.setEnabled (! history.empty());
    forwardButton.setEnabled (! future.empty());
}

void GroovesPage::browseTo (const juce::File& folder, bool remember)
{
    if (remember && folder != within)
    {
        history.push_back (within);
        future.clear();
    }
    within = folder;
    suggestions->setVisible (false);
    rebuildBrowser();
    requery();
}

bool GroovesPage::goBack()
{
    if (history.empty())
        return false;
    future.push_back (within);
    const auto to = history.back();
    history.pop_back();
    browseTo (to, false);
    return true;
}

bool GroovesPage::goForward()
{
    if (future.empty())
        return false;
    history.push_back (within);
    const auto to = future.back();
    future.pop_back();
    browseTo (to, false);
    return true;
}

void GroovesPage::chooseFolder (const juce::File& folder)
{
    browseTo (folder, true);
}

int GroovesPage::folderIndexOf (const juce::File& f) const
{
    const auto& folders = library.getFolders();
    for (int i = 0; i < (int) folders.size(); ++i)
        if (f == folders[(size_t) i].folder || f.isAChildOf (folders[(size_t) i].folder))
            return i;
    return -1;
}

void GroovesPage::showFolderMenu (const juce::File& folder)
{
    const auto f = folderIndexOf (folder);
    const auto& folders = library.getFolders();
    const auto user = library.getUserFolder();
    const auto inUser = user >= 0 && (folder == folders[(size_t) user].folder || folder.isAChildOf (folders[(size_t) user].folder));
    const auto isRoot = f >= 0 && folder == folders[(size_t) f].folder;
    juce::PopupMenu menu;
    if (f >= 0)
    {
        juce::PopupMenu maps;
        for (int l = 0; l < synth::drums::notemap::layoutNames.size(); ++l)
            maps.addItem (synth::drums::notemap::layoutNames[l], true, (int) folders[(size_t) f].layout == l, [this, f, l]
            {
                library.setLayout (f, (synth::drums::notemap::Layout) l);
            });
        menu.addSubMenu ("NOTE MAP (which drum each note is)", maps);
    }
    if (inUser && folder.isDirectory())
        menu.addItem ("ADD FOLDER...", [this, folder]
        {
            auto* w = new juce::AlertWindow ("Add a folder", "A folder inside " + folder.getFileName() + " for your MIDI files:",
                                             juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "New Folder");
            w->addButton ("ADD", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, folder] (int result)
            {
                const auto folderName = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
                if (result == 1 && folderName.isNotEmpty() && folder.getChildFile (folderName).createDirectory())
                    rebuildBrowser();
            }), true);
        });
    if (isRoot)
        menu.addItem ("LEAVE OUT OF SEARCHES", true, folders[(size_t) f].excluded, [this, f]
        {
            library.setExcluded (f, ! library.getFolders()[(size_t) f].excluded);
            rebuildBrowser();
            requery();
        });
    {
        // Opened in the computer's file browser (a folder inside a groove database: the database's folder).
        auto onDisk = folder;
        while (onDisk.getFullPathName().isNotEmpty() && ! onDisk.exists() && onDisk != onDisk.getParentDirectory())
            onDisk = onDisk.getParentDirectory();
#if JUCE_MAC
        const juce::String openText ("OPEN IN FINDER");
#else
        const juce::String openText ("OPEN IN EXPLORER");
#endif
        menu.addItem (openText, onDisk.exists(), false, [onDisk] { onDisk.revealToUser(); });
    }
    if (f >= 0)
        menu.addItem (inUser && userNeedsSync ? juce::String ("SYNC FILE CHANGES  (changed on disk)") : juce::String ("SYNC FILE CHANGES"),
                      [this] { library.scan(); });
    if (isRoot && ! folders[(size_t) f].user)
    {
        menu.addSeparator();
        menu.addItem ("REMOVE LINKED FOLDER (its files stay)", [this, f]
        {
            library.removeFolder (f);
            history.clear();
            future.clear();
            browseTo ({}, false);
            refresh();
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options());
}

//==============================================================================
// THE MENU
void GroovesPage::showMenu()
{
    juce::PopupMenu menu;
    {
        juce::PopupMenu folders;
        folders.addItem ("SHOW MIDI FROM SUBFOLDERS", true, library.getSubfolders(), [this]
        {
            library.setSubfolders (! library.getSubfolders());
            requery();
        });
        folders.addItem ("ADD LINKED FOLDER...", [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("A folder of MIDI grooves", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c)
            {
                if (c.getResult().isDirectory() && library.addFolder (c.getResult()))
                    refresh();
            });
        });
        folders.addItem ("CHECK ALL FOLDERS FOR CHANGES", [this] { library.scan(); });
        menu.addSubMenu ("FOLDERS", folders);
    }
    {
        juce::PopupMenu columns;
        for (int c = 0; c < grooves::numColumns; ++c)
            columns.addItem (grooves::columnNames[c], true, shownFilters[(size_t) c], [this, c] { showFilterColumn ((grooves::Column) c, ! shownFilters[(size_t) c]); });
        columns.addSeparator();
        columns.addItem ("BACK TO THE USUAL", [this]
        {
            for (int c = 0; c < grooves::numColumns; ++c)
                showFilterColumn ((grooves::Column) c, grooves::shownByDefault[(size_t) c]);
        });
        menu.addSubMenu ("VISIBLE FILTER COLUMNS", columns);
    }
    {
        juce::PopupMenu columns;
        auto& header = table.getHeader();
        for (const auto& c : resultColumns)
            if (optionalColumn (c.id))
                columns.addItem (c.title, true, header.isColumnVisible (c.id), [this, id = c.id]
                {
                    auto& h = table.getHeader();
                    h.setColumnVisible (id, ! h.isColumnVisible (id));
                    savePreview();
                });
        menu.addSubMenu ("VISIBLE RESULT COLUMNS", columns);
    }
    {
        const auto order = library.getPreview().sortOrder;
        juce::PopupMenu sorting;
        sorting.addItem ("THE PACKS' OWN ORDER (SIMPLEST FIRST)", true, order.priority, [this, order]
        {
            auto o = order;
            o.priority = ! o.priority;
            setSortOrder (o);
        });
        sorting.addItem ("NEAREST YOUR SONG'S TEMPO", true, order.songTempo, [this, order]
        {
            auto o = order;
            o.songTempo = ! o.songTempo;
            setSortOrder (o);
        });
        sorting.addItem ("YOUR SONG'S TIME SIGNATURE FIRST", true, order.songSignature, [this, order]
        {
            auto o = order;
            o.songSignature = ! o.songSignature;
            setSortOrder (o);
        });
        menu.addSubMenu ("SORTING", sorting);
    }
    {
        const auto forThisKit = library.getPreview().forThisKit;
        juce::PopupMenu drag;
        drag.addItem ("FOR THIS KIT (its notes moved to this kit's map)", true, forThisKit, [this]
        {
            auto pr = library.getPreview();
            pr.forThisKit = true;
            library.setPreview (pr);
        });
        drag.addItem ("AS WRITTEN (the file's own notes)", true, ! forThisKit, [this]
        {
            auto pr = library.getPreview();
            pr.forThisKit = false;
            library.setPreview (pr);
        });
        menu.addSubMenu ("GROOVES DRAGGED OUT", drag);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void GroovesPage::showFilterColumnMenu()
{
    juce::PopupMenu columns;
    for (int c = 0; c < grooves::numColumns; ++c)
        columns.addItem (grooves::columnNames[c], true, shownFilters[(size_t) c], [this, c] { showFilterColumn ((grooves::Column) c, ! shownFilters[(size_t) c]); });
    columns.addSeparator();
    columns.addItem ("BACK TO THE USUAL", [this]
    {
        for (int c = 0; c < grooves::numColumns; ++c)
            showFilterColumn ((grooves::Column) c, grooves::shownByDefault[(size_t) c]);
    });
    columns.showMenuAsync (juce::PopupMenu::Options());
}

void GroovesPage::showFilterColumn (grooves::Column c, bool on)
{
    // At least one shows.
    if (! on && std::count (shownFilters.begin(), shownFilters.end(), true) <= 1 && shownFilters[(size_t) c])
        return;
    shownFilters[(size_t) c] = on;
    filterColumns[(size_t) c]->setVisible (on);
    savePreview();
    resized();
}

void GroovesPage::applyResultColumns()
{
    const auto shown = library.getPreview().resultColumns;
    auto& header = table.getHeader();
    for (const auto& c : resultColumns)
        if (optionalColumn (c.id))
            header.setColumnVisible (c.id, shown.isEmpty() ? c.shown : shown.contains (c.title));
}

void GroovesPage::setOriginalTempo (bool on)
{
    originalTempo.setToggleState (on, juce::dontSendNotification);
    savePreview();
}

void GroovesPage::setSortOrder (grooves::SortOrder order)
{
    auto pr = library.getPreview();
    pr.sortOrder = order;
    library.setPreview (pr);
    headerSort = 0;   // the ticks again, not a column clicked
    table.getHeader().setSortColumnId (0, true);
    requery();
}

void GroovesPage::savePreview()
{
    auto p = library.getPreview();
    p.songTempo = ! originalTempo.getToggleState();
    p.factor = (grooves::TempoFactor) juce::jmax (0, factor.getSelectedItemIndex());
    p.velocity = (float) velocity.getValue() / 100.0f;
    p.filterColumns.clear();
    for (int c = 0; c < grooves::numColumns; ++c)
        if (shownFilters[(size_t) c])
            p.filterColumns.add (grooves::columnNames[c]);
    p.resultColumns.clear();
    for (const auto& c : resultColumns)
        if (optionalColumn (c.id) && table.getHeader().isColumnVisible (c.id))
            p.resultColumns.add (c.title);
    library.setPreview (p);
    processor.applyGroovePreview();
    rebuildChips();
}

//==============================================================================
// THE LIST
void GroovesPage::requery()
{
    // The grooves the folder, words, favourites (and a rhythm) leave are the scope: each filter column counts its
    // values in them; the list shows those that pass the filters.
    const auto p = library.getPreview();
    const auto scope = library.query (within, search.getText(), favouritesOnly, p.sortOrder, processor.getHostBpm(),
                                      processor.getHostNumerator(), processor.getHostDenominator(), {},
                                      tapSearch.has_value() ? &*tapSearch : nullptr);
    results.clear();
    for (const auto& e : scope)
        if (filters.passes (e))
            results.push_back (e);
    for (int c = 0; c < grooves::numColumns; ++c)
    {
        const auto column = (grooves::Column) c;
        std::map<juce::String, int> counts;
        juce::StringArray scratch;
        for (const auto& e : scope)
            for (const auto& v : grooves::valuesIn (e, column, scratch))
                ++counts[v];
        auto values = knownValues[(size_t) c];
        for (const auto* chosenOnes : { &filters.include[(size_t) c], &filters.exclude[(size_t) c] })
            for (const auto& v : *chosenOnes)
                values.addIfNotAlreadyThere (v);
        for (const auto& [v, n] : counts)
            values.addIfNotAlreadyThere (v);
        values = grooves::sortedValues (column, values);
        juce::Array<int> numbers;
        for (const auto& v : values)
        {
            const auto found = counts.find (v);
            numbers.add (found != counts.end() ? found->second : 0);
        }
        filterColumns[(size_t) c]->setValues (values, numbers);
    }
    sortResults();
    findPlayingRow();
    table.getHeader().setColumnVisible (matchScore, tapSearch.has_value());
    rebuildChips();
    table.updateContent();
    table.repaint();
    dropZone->repaint();
    timerCallback();
}

void GroovesPage::sortResults()
{
    if (headerSort == 0)
        return;
    const auto column = headerSort;
    const auto forwards = headerForwards;
    std::stable_sort (results.begin(), results.end(), [column, forwards] (const grooves::Entry& a, const grooves::Entry& b)
    {
        int order = 0;
        switch (column)
        {
            case signature: order = a.denominator != b.denominator ? (a.denominator < b.denominator ? -1 : 1)
                                  : a.numerator != b.numerator ? (a.numerator < b.numerator ? -1 : 1) : 0; break;
            case bars: order = a.bars < b.bars ? -1 : a.bars > b.bars ? 1 : 0; break;
            case tempo: order = a.bpm < b.bpm ? -1 : a.bpm > b.bpm ? 1 : 0; break;
            case intensity: order = a.intensity < b.intensity ? -1 : a.intensity > b.intensity ? 1 : 0; break;
            case matchScore: order = a.match > b.match ? -1 : a.match < b.match ? 1 : 0; break;
            default: order = cellText (a, column).compareNatural (cellText (b, column)); break;
        }
        return forwards ? order < 0 : order > 0;
    });
}

void GroovesPage::sortOrderChanged (int column, bool forwards)
{
    if (column == favourite)
    {
        // The star's column: favourites only, or all again.
        favouritesOnly = ! favouritesOnly;
        table.getHeader().setSortColumnId (headerSort, headerForwards);
        requery();
        return;
    }
    headerSort = column;
    headerForwards = forwards;
    sortResults();
    findPlayingRow();
    table.updateContent();
    table.repaint();
}

void GroovesPage::toggleFilter (grooves::Column column, const juce::String& value, bool exclude)
{
    auto& include = filters.include[(size_t) column];
    auto& excluded = filters.exclude[(size_t) column];
    auto& into = exclude ? excluded : include;
    auto& other = exclude ? include : excluded;
    other.removeString (value);
    if (into.contains (value))
        into.removeString (value);
    else
        into.add (value);
    // Its chip's place: where it was first chosen; gone once it's let go.
    const std::pair<int, juce::String> key { (int) column, value };
    const auto active = include.contains (value) || excluded.contains (value);
    const auto at = std::find (filterOrder.begin(), filterOrder.end(), key);
    if (active && at == filterOrder.end())
        filterOrder.push_back (key);
    else if (! active && at != filterOrder.end())
        filterOrder.erase (at);
    requery();
}

void GroovesPage::clearFilters()
{
    filters = {};
    filterOrder.clear();
    favouritesOnly = false;
    tapSearch.reset();
    search.setText ({}, juce::dontSendNotification);
    suggestions->setVisible (false);
    browseTo ({}, true);
}

juce::StringArray GroovesPage::getColumnValues (grooves::Column c) const
{
    return filterColumns[(size_t) c]->describe();
}

//==============================================================================
// THE ACTIVE FILTERS
void GroovesPage::rebuildChips()
{
    struct Spec { juce::String text; juce::Colour colour; std::function<void()> remove; };
    std::vector<Spec> specs;
    if (within != juce::File())
        specs.push_back ({ "FOLDER: " + within.getFileName(), ParagonColours::accent, [this] { browseTo ({}, true); } });
    if (tapSearch.has_value())
        specs.push_back ({ "SIMILAR TO: " + tapWhat, ParagonColours::accent, [this] { clearTap(); } });
    if (search.getText().trim().isNotEmpty())
        specs.push_back ({ "SEARCH: " + search.getText().trim(), ParagonColours::accent, [this] { setSearch ({}); } });
    if (favouritesOnly)
        specs.push_back ({ "FAVOURITES", ParagonColours::accent, [this] { favouritesOnly = false; requery(); } });
    // The filters, in the order they were chosen.
    for (const auto& [c, v] : filterOrder)
    {
        if (filters.include[(size_t) c].contains (v))
            specs.push_back ({ grooves::columnNames[c] + ": " + v, ParagonColours::accent, [this, col = c, val = v] { toggleFilter ((grooves::Column) col, val, false); } });
        else if (filters.exclude[(size_t) c].contains (v))
            specs.push_back ({ "NOT " + grooves::columnNames[c] + ": " + v, ParagonColours::meterRed, [this, col = c, val = v] { toggleFilter ((grooves::Column) col, val, true); } });
    }
    // The preview changed from as played: a chip to reset it.
    const auto p = library.getPreview();
    if (p.factor != grooves::TempoFactor::normal || std::abs (p.velocity - 1.0f) > 0.001f)
    {
        juce::StringArray what;
        if (p.factor != grooves::TempoFactor::normal)
            what.add ("TEMPO " + grooves::tempoFactorNames[(int) p.factor].upToFirstOccurrenceOf (" ", false, false));
        if (std::abs (p.velocity - 1.0f) > 0.001f)
            what.add ("VELOCITY " + juce::String (juce::roundToInt (p.velocity * 100.0f)) + "%");
        specs.push_back ({ what.joinIntoString (", ") + ": RESET", ParagonColours::glow, [this]
        {
            factor.setSelectedItemIndex ((int) grooves::TempoFactor::normal, juce::dontSendNotification);
            velocity.setValue (100.0, juce::dontSendNotification);
            savePreview();
        } });
    }

    chipTexts.clear();
    chipActions.clear();
    for (const auto& s : specs)
    {
        chipTexts.add (s.text);
        chipActions.push_back (s.remove);
    }
    for (auto& c : chips)
        removeChildComponent (c.get());
    chips.clear();
    auto area = chipArea;
    for (const auto& s : specs)
    {
        auto chip = std::make_unique<Chip> (s.text, s.colour, s.remove);
        const auto w = juce::jmin (chip->idealWidth(), 260);
        if (w > area.getWidth())
            break;   // no room left: the rest aren't shown (CLEAR ALL FILTERS still takes them)
        chip->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (6);
        addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    }
    clearAll.setEnabled (! specs.empty());
}

juce::StringArray GroovesPage::getChips() const
{
    return chipTexts;
}

void GroovesPage::removeChip (int index)
{
    if (index >= 0 && index < (int) chipActions.size())
    {
        const auto action = chipActions[(size_t) index];   // (it rebuilds the chips)
        action();
    }
}

void GroovesPage::setSearch (const juce::String& text)
{
    // As typed (the suggestions show only while the search has the keyboard).
    search.setText (text, juce::dontSendNotification);
    requery();
    updateSuggestions();
}

void GroovesPage::updateSuggestions()
{
    suggested.clear();
    const auto text = search.getText().trim();
    if (text.length() >= 2)
    {
        for (int c = 0; c < grooves::numColumns && suggested.size() < 6; ++c)
            for (const auto& v : knownValues[(size_t) c])
                if (v.containsIgnoreCase (text) && suggested.size() < 6)
                    suggested.push_back ({ c, v });
        std::set<juce::String> names;
        for (const auto& e : library.getEntries())
        {
            if (names.size() >= 6)
                break;
            const auto label = e.name + "  (" + e.family + ")";
            if (e.name.containsIgnoreCase (text) && names.insert (label).second)
                suggested.push_back ({ -1, e.name });
        }
    }
    juce::StringArray texts;
    for (const auto& [c, v] : suggested)
        texts.add (c >= 0 ? grooves::columnNames[c] + ": " + v : v);
    suggestions->setItems (texts);
    const auto show = ! texts.isEmpty() && search.hasKeyboardFocus (true);
    suggestions->setBounds (search.getX(), search.getBottom() + 2, juce::jmax (search.getWidth(), 320), juce::jmin (texts.size(), 12) * 22 + 6);
    suggestions->setVisible (show);
    if (show)
        suggestions->toFront (false);
}

juce::StringArray GroovesPage::getSuggestions() const
{
    juce::StringArray texts;
    for (const auto& [c, v] : suggested)
        texts.add (c >= 0 ? grooves::columnNames[c] + ": " + v : v);
    return texts;
}

void GroovesPage::acceptSuggestion (int index)
{
    if (index < 0 || index >= (int) suggested.size())
        return;
    const auto [c, value] = suggested[(size_t) index];
    suggestions->setVisible (false);
    if (c >= 0)
    {
        // A filter: chosen, the words taken away.
        search.setText ({}, juce::dontSendNotification);
        if (! filters.include[(size_t) c].contains (value))
            toggleFilter ((grooves::Column) c, value, false);
        else
            requery();
    }
    else
        setSearch (value);
}

//==============================================================================
void GroovesPage::toPattern (int pattern)
{
    const auto row = table.getSelectedRow();
    const auto g = readRow (row);
    if (! g.has_value())
        message = "Choose a groove first (click it in the list)";
    else
    {
        const auto r = processor.grooveToPattern (*g, results[(size_t) row].folder, pattern);
        message = g->name + " is pattern " + synth::drumseq::patternNames[pattern] + " now (" + juce::String (r.lengthBeats) + " beats)";
        if (r.pastEnd > 0)
            message << ", its first 16 beats";
        if (r.noDrum > 0)
            message << "; " << r.noDrum << (r.noDrum == 1 ? " note" : " notes") << " no drum plays";
    }
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    timerCallback();
}

void GroovesPage::openTapPanel()
{
    processor.stopGroove();   // the loop and its metronome alone
    const auto perBar = processor.getHostNumerator() * 4.0 / juce::jmax (1, processor.getHostDenominator());
    tapPanel->open (processor.getHostBpm(), perBar);
    tapPanel->toFront (false);
}

void GroovesPage::findByRhythm (const grooves::Rhythm& rhythm, double grid, const juce::String& what)
{
    if (rhythm.isEmpty())
        return;
    tapSearch = grooves::Library::TapSearch { rhythm, grid };
    tapWhat = what;
    headerSort = 0;   // closest first
    table.getHeader().setSortColumnId (0, true);
    requery();
    table.scrollToEnsureRowIsOnscreen (0);
}

void GroovesPage::findSimilar (int row)
{
    if (row < 0 || row >= (int) results.size() || results[(size_t) row].rhythm == nullptr)
        return;
    const auto& e = results[(size_t) row];
    const auto perBar = e.numerator * 4.0 / juce::jmax (1, e.denominator);
    findByRhythm (grooves::firstBars (*e.rhythm, 2.0 * perBar), tapPanel->getGrid(), e.name);
}

bool GroovesPage::findLikeFile (const juce::File& file)
{
    const auto layout = processor.getMapLayout() == synth::drums::notemap::Layout::root ? synth::drums::notemap::grooveLibrary()
                                                                                          : processor.getNoteMap();
    const auto g = grooves::read (file, layout);
    if (! g.has_value())
        return false;
    findByRhythm (grooves::firstBars (grooves::rhythmOf (*g, layout), 2.0 * g->beatsPerBar()), tapPanel->getGrid(),
                  file.getFileNameWithoutExtension());
    return true;
}

void GroovesPage::clearTap()
{
    tapSearch.reset();
    requery();
}

void GroovesPage::searchWithTags (int row)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const auto e = results[(size_t) row];
    filters = {};
    filterOrder.clear();
    for (const auto c : { grooves::Column::genre, grooves::Column::playStyle, grooves::Column::type, grooves::Column::time, grooves::Column::hand })
    {
        filters.include[(size_t) c] = grooves::valuesOf (e, c);
        for (const auto& v : filters.include[(size_t) c])
            filterOrder.push_back ({ (int) c, v });
    }
    tapSearch.reset();
    favouritesOnly = false;
    search.setText ({}, juce::dontSendNotification);
    browseTo ({}, true);
}

void GroovesPage::showInBrowser (int row)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const auto file = results[(size_t) row].file;
    browseTo (file.getParentDirectory(), true);
    for (int r = 0; r < (int) results.size(); ++r)
        if (results[(size_t) r].file == file)
        {
            table.selectRow (r);
            table.scrollToEnsureRowIsOnscreen (r);
            break;
        }
}

bool GroovesPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    // MIDI files (or folders of them): onto USER MIDI (or a folder in it) to add them; anywhere else, one file to
    // search with.
    return std::any_of (files.begin(), files.end(), [] (const juce::String& f)
    {
        return juce::File (f).hasFileExtension ("mid;midi") || juce::File (f).isDirectory();
    });
}

void GroovesPage::filesDropped (const juce::StringArray& files, int x, int y)
{
    // Onto a folder of USER MIDI in the browser: copied in.
    const auto user = library.getUserFolder();
    if (user >= 0)
        for (auto& column : browserColumns)
            if (column->isVisible())
                if (const auto row = column->rowAt ({ x, y }); row >= 0 && row < (int) column->getRows().size())
                {
                    const auto& target = column->getRows()[(size_t) row].folder;
                    const auto& root = library.getFolders()[(size_t) user].folder;
                    if (target != juce::File() && (target == root || target.isAChildOf (root)) && target.isDirectory())
                    {
                        const auto copied = library.addToUserFolder (files, target);
                        message = juce::String (copied) + (copied == 1 ? " MIDI file" : " MIDI files") + " added to " + target.getFileName();
                        messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
                        return;
                    }
                }
    if (! files.isEmpty() && juce::File (files[0]).hasFileExtension ("mid;midi") && ! findLikeFile (juce::File (files[0])))
    {
        message = "That file has no notes to search with";
        messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    }
}

void GroovesPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Read further (or folders changed): the values, the browser and the list afresh.
    refresh();
}

void GroovesPage::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() < messageUntil)
        status.setText (message, juce::dontSendNotification);
    else if (library.isScanning() && results.empty() && library.getNumEntries() == 0)
        status.setText ("Reading your grooves... " + juce::String (library.getNumScanned()) + " files", juce::dontSendNotification);
    else if (library.isScanning())
        status.setText (juce::String (results.size()) + (results.size() == 1 ? " groove" : " grooves") + " (checking for new ones"
                            + (library.getNumScanned() > 0 ? ": " + juce::String (library.getNumScanned()) + " read" : juce::String()) + ")",
                        juce::dontSendNotification);
    else if (library.getNumEntries() == 0)
        status.setText ("No grooves yet: MENU > FOLDERS > ADD LINKED FOLDER..., or drop MIDI files on User MIDI", juce::dontSendNotification);
    else
        status.setText (juce::String (results.size()) + (results.size() == 1 ? " groove" : " grooves"), juce::dontSendNotification);

    // USER MIDI out of step with its files (files dropped into it from outside): checked every few seconds.
    if (isShowing() && ++syncTick % 60 == 0)
    {
        const auto needs = library.needsSync (library.getUserFolder());
        if (needs != userNeedsSync)
        {
            userNeedsSync = needs;
            browserColumns[0]->repaint();
        }
    }

    const auto file = processor.getGrooveFile();
    const auto position = processor.getGroovePosition();
    playing.setText (position >= 0.0 && file != juce::File() ? "PLAYING: " + file.getFileNameWithoutExtension() : juce::String(),
                     juce::dontSendNotification);
    const auto nowPlaying = position >= 0.0 ? file : juce::File();
    if (nowPlaying != shownPlaying)
    {
        // Only the rows it leaves and goes to (the whole list was repainted before 0.30.0).
        const auto before = playingRow;
        shownPlaying = nowPlaying;
        shownPosition = position;
        findPlayingRow();
        if (before >= 0)
            table.repaintRow (before);
        if (playingRow >= 0)
            table.repaintRow (playingRow);
    }
    else if (playingRow >= 0 && std::abs (position - shownPosition) > 1.0e-3)
    {
        shownPosition = position;
        table.repaintRow (playingRow);
    }
}

void GroovesPage::findPlayingRow()
{
    playingRow = -1;
    if (shownPlaying == juce::File())
        return;
    for (int row = 0; row < (int) results.size(); ++row)
        if (results[(size_t) row].file == shownPlaying)
        {
            playingRow = row;
            return;
        }
}

std::optional<grooves::Groove> GroovesPage::readRow (int row) const
{
    if (row < 0 || row >= (int) results.size())
        return std::nullopt;
    const auto& e = results[(size_t) row];
    return grooves::read (e.file, processor.grooveLayout (e.folder));
}

bool GroovesPage::addRowToSong (int row, double beat)
{
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    processor.addToSong (*g, results[(size_t) row].folder, beat);
    message = g->name + " is in the song now";
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    if (onAddedToSong != nullptr)
        onAddedToSong();
    return true;
}

bool GroovesPage::rowToPattern (int row, int pattern)
{
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    const auto folder = results[(size_t) row].folder;
    const auto r = chosenParts == grooves::allParts ? processor.grooveToPattern (*g, folder, pattern)
                                                    : processor.layerParts (*g, folder, chosenParts, pattern);
    message = g->name + " is in pattern " + synth::drumseq::patternNames[pattern] + " now";
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
    return r.placed > 0;
}

bool GroovesPage::partToPattern (int part, int pattern)
{
    const auto row = table.getSelectedRow();
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    return processor.layerParts (*g, results[(size_t) row].folder, 1u << juce::jlimit (0, grooves::numDrumParts - 1, part), pattern).placed > 0;
}

juce::String GroovesPage::describeRowDrop (int pattern) const
{
    const auto letter = synth::drumseq::patternNames[pattern];
    if (chosenParts == grooves::allParts)
        return "DROP: THE GROOVE REPLACES PATTERN " + letter;
    juce::StringArray names;
    for (int k = 0; k < grooves::numDrumParts; ++k)
        if ((chosenParts & (1u << k)) != 0)
            names.add (grooves::drumPartNames[k]);
    return "DROP: ITS " + names.joinIntoString (", ") + " LAID ONTO PATTERN " + letter;
}

bool GroovesPage::copyRowForSong (int row)
{
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    processor.copyGrooveForSong (*g, results[(size_t) row].folder, chosenParts);
    message = g->name + " copied: right-click a block in the song, PASTE DRUMS ONTO IT";
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
    return true;
}

void GroovesPage::playRow (int row, bool loop)
{
    if (const auto g = readRow (row))
        processor.playGroove (*g, results[(size_t) row].folder, loop, chosenParts);
}

void GroovesPage::selectedRowsChanged (int row)
{
    partCounts = {};
    partsOf = juce::File();
    if (const auto g = readRow (row))
    {
        partsOf = g->file;
        const auto map = processor.grooveLayout (results[(size_t) row].folder);
        for (const auto& h : g->hits)
            if (const auto part = grooves::drumPartOf (synth::drums::notemap::slotOf (map, h.note)); part >= 0)
                ++partCounts[(size_t) part];
    }
    partsList->refresh();
}

void GroovesPage::toggleDrumPart (int part)
{
    if (part < 0)
        chosenParts = grooves::allParts;
    else if (chosenParts == grooves::allParts)
        chosenParts = 1u << part;   // from ALL, a click picks just that part
    else
    {
        chosenParts ^= 1u << part;
        if (chosenParts == 0)
            chosenParts = grooves::allParts;
    }
    partsList->refresh();
    if (const auto row = table.getSelectedRow(); row >= 0 && row < (int) results.size()
        && processor.getGroovePosition() >= 0.0 && processor.getGrooveFile() == results[(size_t) row].file)
        playRow (row, true);
}

void GroovesPage::layerOnto (int pattern)
{
    const auto row = table.getSelectedRow();
    const auto g = readRow (row);
    if (! g.has_value())
        message = "Choose a groove first (click it in the list)";
    else
    {
        processor.layerParts (*g, results[(size_t) row].folder, chosenParts, pattern);
        juce::StringArray names;
        for (int k = 0; k < grooves::numDrumParts; ++k)
            if ((chosenParts & (1u << k)) != 0)
                names.add (grooves::drumPartNames[k]);
        message = (chosenParts == grooves::allParts ? juce::String ("All of ") : names.joinIntoString (", ") + " of ") + g->name
                  + " laid into pattern " + synth::drumseq::patternNames[pattern];
    }
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    timerCallback();
}

//==============================================================================
int GroovesPage::getNumRows()
{
    return (int) results.size();
}

void GroovesPage::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    const auto isPlaying = row < (int) results.size() && results[(size_t) row].file == shownPlaying;
    g.setColour (selected || isPlaying ? ParagonColours::accentDim.withAlpha (0.45f)
                 : row == hoveredRow ? juce::Colour (0x14ffffff)
                 : (row % 2 == 0 ? juce::Colour (0x00000000) : juce::Colour (0x0affffff)));
    g.fillRect (0, 0, width, height);
    g.setColour (ParagonColours::panelEdge.withAlpha (0.5f));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void GroovesPage::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const auto& e = results[(size_t) row];
    const auto isPlaying = e.file == shownPlaying;
    g.setColour (isPlaying ? ParagonColours::accent : ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f));
    const auto text = [&] (const juce::String& s, juce::Justification j = juce::Justification::centredLeft)
    {
        g.drawText (s, 6, 0, width - 10, height, j, true);
    };
    switch (column)
    {
        case favourite:
        {
            auto star = Icons::get (Icons::Type::star);
            const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).withSizeKeepingCentre (14.0f, 14.0f);
            star.applyTransform (star.getTransformToScaleToFit (box, true));
            if (library.isFavourite (e.file))
            {
                g.setColour (ParagonColours::accent);
                g.fillPath (star);
            }
            else
            {
                g.setColour (ParagonColours::textDim.withAlpha (0.6f));
                g.strokePath (star, juce::PathStrokeType (1.0f));
            }
            break;
        }
        case play:
        {
            // The row under the mouse (or the playing one): play, or stop.
            if (row != hoveredRow && ! isPlaying)
                break;
            const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).withSizeKeepingCentre (12.0f, 12.0f);
            g.setColour (isPlaying ? ParagonColours::accent : ParagonColours::text);
            if (isPlaying)
                g.fillRect (box.reduced (1.5f));
            else
            {
                juce::Path triangle;
                triangle.addTriangle (box.getX() + 1.0f, box.getY(), box.getX() + 1.0f, box.getBottom(), box.getRight(), box.getCentreY());
                g.fillPath (triangle);
            }
            break;
        }
        case name:
        {
            text (e.name);
            if (isPlaying && shownPosition >= 0.0)
            {
                const auto length = e.bars * e.numerator * 4.0 / e.denominator;
                const auto share = juce::jlimit (0.0, 1.0, std::fmod (shownPosition, length) / length);
                g.setColour (ParagonColours::accent);
                g.fillRect (6.0f, (float) height - 4.0f, (float) (width - 12) * (float) share, 2.0f);
            }
            break;
        }
        case family: g.setColour (ParagonColours::textDim); text (e.family); break;
        case signature: text (e.signature(), juce::Justification::centred); break;
        case bars: text (juce::String (e.bars), juce::Justification::centred); break;
        case tempo: text (tempoText (e), juce::Justification::centred); break;
        case hand:
        case genre:
        case playStyle:
        case type:
        case section:
            if (column != hand)
                g.setColour (ParagonColours::textDim);
            text (cellText (e, column));
            break;
        case matchScore:
            text (e.match >= 0.0f ? juce::String (juce::roundToInt (e.match * 100.0f)) + "%" : juce::String ("-"), juce::Justification::centred);
            break;
        case intensity:
        {
            const auto bar = juce::Rectangle<float> (8.0f, (float) height * 0.5f - 3.0f, (float) width - 16.0f, 6.0f);
            g.setColour (ParagonColours::panelEdge);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (isPlaying ? ParagonColours::accent : ParagonColours::text.withAlpha (0.75f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, e.intensity)), 3.0f);
            break;
        }
        default: break;
    }
}

juce::String GroovesPage::getCellTooltip (int row, int column)
{
    if (row < 0 || row >= (int) results.size())
        return {};
    const auto& e = results[(size_t) row];
    switch (column)
    {
        case favourite: return "Click: a favourite. Click the column's star: favourites only.";
        case play: return "Click: play it, round and round (again: stop). Double-click a groove: once.";
        case tempo: return e.bpmKnown ? "The tempo it was played at." : "The file doesn't say its tempo.";
        case hand: return "What the riding hand plays most (the pack's label, or worked out from its notes).";
        case intensity: return "How hard and how busy it's played (our own measure, from its notes).";
        case matchScore: return "How close its rhythm is to the one searched with: the drums tapped, on QUANTIZE's grid.";
        default: return "Click: choose it (its GROOVE PARTS). Double-click: play it once. Drag it onto a track in your DAW, into "
                        "the song or the sequencer, or to a folder. Right-click: more.\n" + e.file.getFullPathName();
    }
}

void GroovesPage::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const auto file = results[(size_t) row].file;
    if (e.mods.isPopupMenu())
    {
        table.selectRow (row);
        juce::PopupMenu menu;
        menu.addItem (chosenParts == grooves::allParts ? "COPY" : "COPY (THE TICKED PARTS)", [this, row] { copyRowForSong (row); });
        menu.addItem ("SEARCH WITH TAGS (its labels as the filters)", [this, row] { searchWithTags (row); });
        menu.addItem ("SEARCH WITH TAP TO FIND (its rhythm)", [this, row] { findSimilar (row); });
        menu.addItem ("SHOW IN BROWSER", [this, row] { showInBrowser (row); });
        menu.addItem ("USE WITH SONG CREATOR", onUseWithCreator != nullptr, false, [this, file]
        {
            if (onUseWithCreator != nullptr)
                onUseWithCreator (file);
        });
        menu.addSeparator();
        menu.addItem ("ADD TO SONG", [this, row] { addRowToSong (row); });
        juce::PopupMenu patterns;
        for (int i = 0; i < 4; ++i)
            patterns.addItem ("PATTERN " + synth::drumseq::patternNames[i], [this, row, i]
            {
                table.selectRow (row);
                toPattern (i);
            });
        menu.addSubMenu ("TO SEQUENCER", patterns);
        menu.addItem (library.isFavourite (file) ? "NOT A FAVOURITE" : "FAVOURITE", [this, file]
        {
            library.setFavourite (file, ! library.isFavourite (file));
            requery();
        });
        menu.showMenuAsync (juce::PopupMenu::Options());
        return;
    }
    if (column == favourite)
    {
        library.setFavourite (file, ! library.isFavourite (file));
        if (favouritesOnly)
            requery();
        table.repaintRow (row);
        return;
    }
    if (column == play)
    {
        if (file == shownPlaying)
            processor.stopGroove();
        else
            playRow (row, true);
        timerCallback();
    }
}

void GroovesPage::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column != favourite && column != play)
        playRow (row, false);
}

void GroovesPage::mouseMove (const juce::MouseEvent& e)
{
    // The row under the mouse shows its play button.
    if (e.eventComponent == nullptr || ! (e.eventComponent == &table || table.isParentOf (e.eventComponent)))
        return;
    const auto at = e.getEventRelativeTo (&table).getPosition();
    const auto row = table.getRowContainingPosition (at.x, at.y);
    if (row != hoveredRow)
    {
        const auto before = hoveredRow;
        hoveredRow = row;
        table.repaintRow (before);
        table.repaintRow (row);
    }
}

void GroovesPage::mouseExit (const juce::MouseEvent& e)
{
    if (e.eventComponent == &table || (e.eventComponent != nullptr && table.isParentOf (e.eventComponent)))
        if (! table.getScreenBounds().contains (e.getScreenPosition()) && hoveredRow >= 0)
        {
            table.repaintRow (hoveredRow);
            hoveredRow = -1;
        }
}

void GroovesPage::mouseDown (const juce::MouseEvent& e)
{
    // A click away from the search: its suggestions close.
    if (e.eventComponent != &search && suggestions->isVisible() && ! suggestions->isParentOf (e.eventComponent))
        suggestions->setVisible (false);
}

juce::var GroovesPage::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var (dragPrefix + juce::String (rows[0]));
}

bool GroovesPage::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details, juce::StringArray& files,
                                                        bool& canMoveFiles)
{
    const auto description = details.description.toString();
    auto row = -1;
    auto parts = chosenParts;
    if (description.startsWith (dragPrefix))
        row = description.fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
    else if (description.startsWith ("part:"))
    {
        row = table.getSelectedRow();
        parts = 1u << juce::jlimit (0, grooves::numDrumParts - 1, description.fromFirstOccurrenceOf ("part:", false, false).getIntValue());
    }
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    const auto file = processor.grooveDragFile (*g, results[(size_t) row].folder, parts);
    if (! file.existsAsFile())
        return false;
    files.add (file.getFullPathName());
    canMoveFiles = false;
    return true;
}

//==============================================================================
void GroovesPage::paint (juce::Graphics& g)
{
    // 0.30.0: drawn once and copied (it's under the list, whose rows repaint as a groove plays).
    panelCache.draw (g, getLocalBounds(), [this] (juce::Graphics& pg) { paintPanels (pg); });
}

void GroovesPage::paintPanels (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    const auto panel = [&g] (juce::Rectangle<int> r)
    {
        if (r.isEmpty())
            return;
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (r.toFloat().expanded (4.0f), 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (r.toFloat().expanded (4.0f), 5.0f, 1.0f);
    };
    // FIND SIMILAR, the filter columns, the browser, the results, the parts.
    juce::Rectangle<int> filtersArea;
    for (const auto& c : filterColumns)
        if (c->isVisible())
            filtersArea = filtersArea.isEmpty() ? c->getBounds() : filtersArea.getUnion (c->getBounds());
    panel (tapButton.getBounds().getUnion (dropZone->getBounds()).withTrimmedTop (-18));
    panel (filtersArea);
    panel (browserColumns.front()->getBounds().getUnion (browserColumns.back()->getBounds()));   // the whole browser, its empty columns too
    panel (table.getBounds());
    if (partsShown)
        panel (partsList->getBounds().getUnion (layerButtons[0].getBounds()));
    drawLabel (g, "FIND SIMILAR", tapButton.getBounds().withY (tapButton.getY() - 20).withHeight (16));
    // Lines between the browser's columns.
    g.setColour (ParagonColours::panelEdge);
    for (size_t i = 1; i < browserColumns.size(); ++i)
        g.drawVerticalLine (browserColumns[i]->getX() - 3, (float) browserColumns[i]->getY(), (float) browserColumns[i]->getBottom());
}

void GroovesPage::resized()
{
    panelCache.invalidate();
    auto area = getLocalBounds().reduced (14, 10);
    const auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width, int gap = 8)
    {
        c.setBounds (row.removeFromLeft (width).withSizeKeepingCentre (width, 30));
        row.removeFromLeft (gap);
    };

    // THE HEADER.
    auto top = area.removeFromTop (34);
    place (top, menuButton, 76, 6);
    place (top, backButton, 30, 2);
    place (top, forwardButton, 30, 14);
    place (top, originalTempo, 140, 14);
    place (top, tempoLabel, 48, 6);
    place (top, factor, 150, 14);
    place (top, velocityLabel, 64, 6);
    place (top, velocity, 190, 12);
    place (top, stop, 64, 14);
    status.setBounds (top.removeFromRight (360));
    playing.setBounds (top);
    area.removeFromTop (12);

    // FIND SIMILAR and the filter columns.
    auto strip = area.removeFromTop (150).withTrimmedTop (20);
    {
        auto find = strip.removeFromLeft (170).reduced (4, 0);
        tapButton.setBounds (find.removeFromTop (34));
        find.removeFromTop (6);
        dropZone->setBounds (find.reduced (0, 2));
        strip.removeFromLeft (14);
    }
    strip.setY (strip.getY() - 20);
    strip.setHeight (strip.getHeight() + 20);
    const auto shownCount = (int) std::count (shownFilters.begin(), shownFilters.end(), true);
    const auto columnWidth = shownCount > 0 ? strip.getWidth() / shownCount : 0;
    for (int c = 0; c < grooves::numColumns; ++c)
        if (shownFilters[(size_t) c])
            filterColumns[(size_t) c]->setBounds (strip.removeFromLeft (columnWidth).reduced (3, 0));
    area.removeFromTop (12);

    // THE ACTIVE FILTERS (GROOVE PARTS' switch at their end).
    auto bar = area.removeFromTop (30);
    search.setBounds (bar.removeFromLeft (200).reduced (0, 2));
    bar.removeFromLeft (8);
    clearAll.setBounds (bar.removeFromLeft (140).reduced (0, 1));
    bar.removeFromLeft (10);
    partsToggle.setBounds (bar.removeFromRight (130).reduced (0, 1));
    bar.removeFromRight (10);
    chipArea = bar;
    area.removeFromTop (12);

    // THE BROWSER, THE RESULTS, GROOVE PARTS.
    auto browser = area.removeFromLeft (540).reduced (4);
    const auto w = browser.getWidth() / (int) browserColumns.size();
    for (auto& c : browserColumns)
        c->setBounds (browser.removeFromLeft (w).reduced (3, 0));
    area.removeFromLeft (12);
    for (auto* c : std::initializer_list<juce::Component*> { partsList.get(), &toSequencerLabel, &layerLabel })
        c->setVisible (partsShown);
    for (int i = 0; i < 4; ++i)
    {
        patternButtons[(size_t) i].setVisible (partsShown);
        layerButtons[(size_t) i].setVisible (partsShown);
    }
    if (partsShown)
    {
        auto parts = area.removeFromRight (190).reduced (4);
        auto layer = parts.removeFromBottom (28);
        layerLabel.setBounds (parts.removeFromBottom (20));
        parts.removeFromBottom (6);
        auto toSequencer = parts.removeFromBottom (28);
        toSequencerLabel.setBounds (parts.removeFromBottom (20));
        parts.removeFromBottom (6);
        partsList->setBounds (parts);
        const auto bw = layer.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            patternButtons[(size_t) i].setBounds (toSequencer.removeFromLeft (bw).reduced (2, 0));
            layerButtons[(size_t) i].setBounds (layer.removeFromLeft (bw).reduced (2, 0));
        }
        area.removeFromRight (12);
    }
    table.setBounds (area.reduced (4));

    // TAP TO FIND's panel over the filters and the list while open.
    tapPanel->setBounds (table.getBounds().getUnion (tapButton.getBounds()).expanded (4));
    rebuildChips();
}
