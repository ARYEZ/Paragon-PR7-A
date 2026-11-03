// The window: its layout, the pages and their tabs, and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;
            // A list's rows are placed by JUCE beyond its visible part, for scrolling; so is a viewport's content.
            if (dynamic_cast<juce::ListBox*> (&parent) != nullptr
                || (parent.getParentComponent() != nullptr && dynamic_cast<juce::Viewport*> (parent.getParentComponent()) != nullptr))
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    float value (ParagonX1AudioProcessor& processor, const juce::String& id)
    {
        return processor.getParameters().getRawParameterValue (id)->load();
    }

    // The control showing `id` in the window, or nullptr.
    ParamControl* controlFor (juce::Component& root, const juce::String& id)
    {
        std::vector<ParamControl*> controls;
        findAll (root, controls);
        for (auto* control : controls)
            if (control->getParameterId() == id)
                return control;
        return nullptr;
    }

    template <typename Type>
    Type* inside (juce::Component& parent)
    {
        std::vector<Type*> found;
        findAll (parent, found);
        return found.empty() ? nullptr : found.front();
    }

    struct Window
    {
        Window()
        {
            editor.reset (processor.createEditor());
            editor->setSize (1600, 900);
            view = dynamic_cast<MainView*> (editor->getChildComponent (0));
            REQUIRE (view != nullptr);
        }
        ParagonX1AudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        MainView* view = nullptr;
    };
}

TEST_CASE ("The window lays out cleanly at every size", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    for (const auto width : { 960, 1280, 1920 })
    {
        DYNAMIC_SECTION (width << " wide")
        {
            editor->setSize (width, width * 9 / 16);
            REQUIRE (editor->getWidth() == width);

            // The view is scaled, not re-laid-out: its transform matches the window.
            auto* view = editor->getChildComponent (0);
            REQUIRE (view != nullptr);
            CHECK_THAT (view->getTransform().mat00, WithinAbs ((float) width / (float) MainView::width, 0.001));

            checkLayout (*view, "view");
        }
    }
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    // 1080p with a 40-pixel taskbar: room left for the DAW's title bar and toolbar.
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (fullHd.y + 80 <= 1040);
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    const auto laptop = Editor::openingSize ({ 1366, 728 });
    CHECK (laptop.x <= 1366 - 40);
    CHECK (laptop.y + 80 <= 728);
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("The window opens on the DRUMS page; GROOVES, FX, PRESETS and SETTINGS cover it; the song strip and OUTPUT stay", "[ui]")
{
    Window w;
    // 0.41.0: no on-screen keyboard anywhere; the SONG track has its strip.
    CHECK (inside<juce::MidiKeyboardComponent> (*w.view) == nullptr);
    auto* keyboard = &w.view->getSongTrack();
    CHECK (keyboard->isVisible());
    CHECK (w.view->getPage() == MainView::Page::drums);
    CHECK (w.view->getDrumPage().isVisible());
    checkLayout (*w.view, "drums");

    // The six tabs (0.20.0: MIXER), side by side in the header, the shown page's lit.
    std::vector<juce::Button*> tabs;
    for (const auto* id : { "drumsTab", "groovesTab", "mixerTab", "fxTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (w.view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        CHECK (tab->getWidth() > 110);   // six share the group (each its icon and name)
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    CHECK (tabs[0]->getToggleState());

    const std::pair<MainView::Page, int> pages[] = { { MainView::Page::grooves, 1 }, { MainView::Page::mixer, 2 }, { MainView::Page::fx, 3 },
                                                     { MainView::Page::presets, 4 }, { MainView::Page::settings, 5 } };
    for (const auto& [page, tab] : pages)
    {
        INFO (tab);
        w.view->showPage (page);
        CHECK_FALSE (w.view->getDrumPage().isVisible());
        CHECK (keyboard->isVisible());
        CHECK (tabs[(size_t) tab]->getToggleState());
        checkLayout (*w.view, "page");
    }
    CHECK (w.view->getFxPage().isVisible() == false);   // SETTINGS shown last
    CHECK (w.view->getGroovesPage().isVisible() == false);
    tabs[0]->onClick();   // as clicking DRUMS does
    CHECK (w.view->getDrumPage().isVisible());
}

TEST_CASE ("Notes play the kit (the processor's keyboard state, as the DAW's notes); others don't", "[ui]")
{
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);   // silence until a key goes down

    w.processor.getKeyboardState().noteOn (1, 38, 0.9f);   // the snare, as clicking its key does
    for (int block = 0; block < 4; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) > 0.05f);
    CHECK (w.processor.getActiveVoices() >= 1);
    w.processor.getKeyboardState().noteOff (1, 38, 0.0f);

    w.processor.getKeyboardState().noteOn (1, 72, 0.9f);   // outside the kit: nothing new
    for (int block = 0; block < 200; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);
    CHECK (w.processor.getActiveVoices() == 0);
    w.processor.getKeyboardState().noteOff (1, 72, 0.0f);

    // (0.41.0: the on-screen keyboard and its tinted keys are gone; the kit still plays from notes, as above.)
}
