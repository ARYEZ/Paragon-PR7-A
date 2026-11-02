#include "MultiPointEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr float pointRadius = 5.0f;
}

MultiPointEditor::MultiPointEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Drag a point to move it, a line up or down to curve it. Double-click to add a point (or, on a point, to delete it). "
                "Right-click empty space for ready-made shapes.");
    shown = getShape();
    startTimerHz (15);
}

void MultiPointEditor::setTarget (Kind newKind, int newIndex)
{
    kind = newKind;
    index = juce::jlimit (0, ModMatrix::numDrumSlots - 1, newIndex);
    shown = getShape();
    repaint();
}

ModShapeData MultiPointEditor::getShape() const
{
    return processor.getModMatrix().drumEnvelopes[(size_t) juce::jlimit (0, ModMatrix::numDrumSlots - 1, index)];
}

void MultiPointEditor::setShape (const ModShapeData& shape, bool finished)
{
    auto matrix = processor.getModMatrix();
    matrix.drumEnvelopes[(size_t) index] = shape;
    processor.setModMatrix (matrix);
    shown = shape;
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

void MultiPointEditor::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    // Undo or a preset changed it.
    if (dragging < 0 && curving < 0 && ! (getShape() == shown))
    {
        shown = getShape();
        repaint();
    }
}

juce::Rectangle<float> MultiPointEditor::getPlotArea() const { return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (12.0f); }

float MultiPointEditor::pixelX (float x) const { const auto a = getPlotArea(); return a.getX() + a.getWidth() * x; }
float MultiPointEditor::pixelY (float y) const { const auto a = getPlotArea(); return a.getBottom() - a.getHeight() * y; }
float MultiPointEditor::xAt (float px) const { const auto a = getPlotArea(); return juce::jlimit (0.0f, 1.0f, (px - a.getX()) / a.getWidth()); }
float MultiPointEditor::yAt (float py) const { const auto a = getPlotArea(); return juce::jlimit (0.0f, 1.0f, (a.getBottom() - py) / a.getHeight()); }

juce::Point<float> MultiPointEditor::pointPosition (int point) const
{
    const auto& p = shown.points[(size_t) point];
    return { pixelX (p.x), pixelY (p.y) };
}

int MultiPointEditor::pointNear (juce::Point<float> at) const
{
    for (int i = (int) shown.points.size() - 1; i >= 0; --i)
        if (pointPosition (i).getDistanceFrom (at) <= pointRadius + 4.0f)
            return i;
    return -1;
}

void MultiPointEditor::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto area = getPlotArea();

    // Grid: quarters of the hit.
    g.setFont (juce::FontOptions (9.5f));
    for (float x = 0.25f; x < 1.0f; x += 0.25f)
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (pixelX (x)), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (x * 100.0f)) + "%", juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ pixelX (x), area.getBottom() + 8.0f }),
                    juce::Justification::centred, false);
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (pixelY (0.0f)), area.getX(), area.getRight());

    if (caption.isNotEmpty())
    {
        // Below the top line (where a flat envelope runs), on a dark backing so the curve can pass behind it.
        const juce::Font font (juce::FontOptions (10.0f, juce::Font::bold));
        const auto box = juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 12.0f,
                                                 juce::GlyphArrangement::getStringWidth (font, caption) + 10.0f, 14.0f);
        g.setColour (juce::Colour (0xff060407).withAlpha (0.8f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (ParagonColours::textDim);
        g.setFont (font);
        g.drawText (caption, box, juce::Justification::centred, false);
    }
    if (shown.points.empty())
        return;

    // The curve, through every point.
    const auto shape = shown.toShape();
    juce::Path path;
    const auto count = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < count; ++i)
    {
        const auto x = (float) i / (float) (count - 1);
        const juce::Point<float> p (area.getX() + (float) i, pixelY (shape.valueAt (x)));
        if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
    }
    DisplayStyle::drawCurve (g, path, area, pixelY (0.0f), true);

    for (int i = 0; i < (int) shown.points.size(); ++i)
    {
        const auto at = pointPosition (i);
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillEllipse (juce::Rectangle<float> (pointRadius * 2.0f + 2.0f, pointRadius * 2.0f + 2.0f).withCentre (at));
        g.setColour (i == dragging ? ParagonColours::text : ParagonColours::accent);
        g.drawEllipse (juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (at), 2.0f);
    }
}

void MultiPointEditor::mouseDown (const juce::MouseEvent& e)
{
    shown = getShape();
    const auto near = pointNear (e.position);
    if (e.mods.isPopupMenu())
    {
        if (near < 0)
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Flat (no change)");
            menu.addItem (2, "Back to front");
            menu.addItem (3, "Upside down");
            menu.addSeparator();
            menu.addItem (4, "Fade in");
            menu.addItem (5, "Fade out");
            menu.addItem (6, "Gate (cut halfway)");
            menu.addItem (7, "Swell");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                                [safe = juce::Component::SafePointer<MultiPointEditor> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0)
                                        safe->applyDrumShape (result);
                                });
            return;
        }
        juce::PopupMenu menu;
        const auto canDelete = (int) shown.points.size() > 2 && near != 0 && near != (int) shown.points.size() - 1;
        menu.addItem (1, "Delete this point", canDelete);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe = juce::Component::SafePointer<MultiPointEditor> (this), near] (int result)
                            {
                                if (safe == nullptr || result != 1)
                                    return;
                                auto shape = safe->getShape();
                                shape.points.erase (shape.points.begin() + near);
                                safe->setShape (shape, true);
                            });
        return;
    }
    if (near >= 0)
    {
        dragging = near;
        return;
    }
    // On a line: curve it.
    const auto x = xAt (e.position.x);
    for (int i = 0; i + 1 < (int) shown.points.size(); ++i)
        if (x >= shown.points[(size_t) i].x && x <= shown.points[(size_t) i + 1].x)
        {
            curving = i;
            dragStartCurve = shown.points[(size_t) i].curve;
            dragStartY = e.position.y;
            break;
        }
}

void MultiPointEditor::applyDrumShape (int choice)
{
    const auto current = getShape();
    ModShapeData shape;
    switch (choice)
    {
        case 1: shape = ModShapeData::flatDrumEnvelope(); break;
        case 2: shape = current.mirrored(); break;
        case 3: shape = current.flipped(); break;
        case 4: shape = { { { 0.0f, 0.0f, 0.4f }, { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 }; break;
        case 5: shape = { { { 0.0f, 1.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        case 6: shape = { { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.52f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        case 7: shape = { { { 0.0f, 0.15f, 0.6f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        default: return;
    }
    setShape (shape, true);
}

void MultiPointEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        auto shape = shown;
        auto& p = shape.points[(size_t) dragging];
        const auto last = (int) shape.points.size() - 1;
        const auto before = dragging > 0 ? shape.points[(size_t) dragging - 1].x : 0.0f;
        const auto after = dragging < last ? shape.points[(size_t) dragging + 1].x : 1.0f;
        p.x = dragging == 0 ? 0.0f : dragging == last ? 1.0f : juce::jlimit (before, after, xAt (e.position.x));   // the ends stay at the ends
        p.y = yAt (e.position.y);
        setShape (shape, false);
    }
    else if (curving >= 0)
    {
        auto shape = shown;
        // Up bends towards the later point's level early (a curve below 0); the drag's direction follows the line's.
        const auto& a = shape.points[(size_t) curving];
        const auto& b = shape.points[(size_t) curving + 1];
        const auto rising = b.y >= a.y ? 1.0f : -1.0f;
        shape.points[(size_t) curving].curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve + rising * (e.position.y - dragStartY) / 120.0f);
        setShape (shape, false);
    }
}

void MultiPointEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 || curving >= 0)
        processor.modMatrixEditFinished();
    dragging = curving = -1;
    repaint();
}

void MultiPointEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto shape = getShape();
    shown = shape;
    const auto near = pointNear (e.position);
    if (near >= 0)
    {
        // Delete it (keeping at least two, and the ends).
        if ((int) shape.points.size() <= 2 || near == 0 || near == (int) shape.points.size() - 1)
            return;
        shape.points.erase (shape.points.begin() + near);
        setShape (shape, true);
        return;
    }
    if ((int) shape.points.size() >= synth::mod::maxShapePoints)
        return;
    // Add one where clicked, between the ends.
    const auto x = xAt (e.position.x);
    auto at = (int) shape.points.size();
    for (int i = 0; i < (int) shape.points.size(); ++i)
        if (shape.points[(size_t) i].x > x)
        {
            at = i;
            break;
        }
    if (at == 0 || at == (int) shape.points.size())
        return;
    shape.points.insert (shape.points.begin() + at, { x, yAt (e.position.y), 0.0f });
    setShape (shape, true);
}
