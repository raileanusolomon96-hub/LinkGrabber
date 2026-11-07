#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

// Native "Windows 95 Minesweeper" widgets for LinkGrabber (mockup v1-ui.yaml). Header-only,
// pure juce::Graphics, no images. All coordinates are in logical pixels.
namespace lgui
{
    inline const juce::Colour kFace      { 0xffc0c0c0 };
    inline const juce::Colour kLight     { 0xffffffff };
    inline const juce::Colour kDark      { 0xff808080 };
    inline const juce::Colour kDarkest   { 0xff404040 };
    inline const juce::Colour kBlack     { 0xff000000 };
    inline const juce::Colour kLedOn     { 0xffff0000 };
    inline const juce::Colour kLedOff    { 0xff2a0000 };
    inline const juce::Colour kNavy      { 0xff000080 };
    inline const juce::Colour kErrorText { 0xff800000 };

    // Closest system sans to "MS Sans Serif, Tahoma, Verdana".
    inline juce::Font makeFont(float size, bool bold = false)
    {
        static const juce::String family = []
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (auto* n : { "Tahoma", "Verdana" })
                if (names.contains(n))
                    return juce::String(n);
            return juce::String();
        }();
        juce::FontOptions o(size);
        if (family.isNotEmpty())
            o = o.withName(family);
        return juce::Font(o.withStyle(bold ? "Bold" : "Regular"));
    }

    //==========================================================================
    // Bevels. `t` = thickness. Raised: light top/left, dark bottom/right. Sunken: inverse.
    inline void bevelRect(juce::Graphics& g, juce::Rectangle<int> r, int t,
                          juce::Colour tl, juce::Colour br)
    {
        const int x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        g.setColour(tl);
        g.fillRect(x, y, w, t);
        g.fillRect(x, y, t, h);
        g.setColour(br);
        g.fillRect(x, y + h - t, w, t);
        g.fillRect(x + w - t, y, t, h);
        for (int i = 0; i < t; ++i)  // mitre the two off-corners
        {
            g.fillRect(x + w - t + i, y + i, 1, 1);
            g.fillRect(x + i, y + h - t + i, 1, 1);
        }
    }

    inline void drawSunken(juce::Graphics& g, juce::Rectangle<int> r, int t, juce::Colour fill)
    {
        g.setColour(fill);
        g.fillRect(r);
        bevelRect(g, r, t, kDark, kLight);
    }

    // Button / smiley bevel: 1px black outline + two-line inner bevel (raised) or flat dark (pressed).
    inline void drawRaisedButton(juce::Graphics& g, juce::Rectangle<int> r, bool pressed)
    {
        g.setColour(kFace);
        g.fillRect(r);
        g.setColour(kBlack);
        g.drawRect(r, 1);
        auto in = r.reduced(1);
        if (pressed)
        {
            g.setColour(kDark);
            g.drawRect(in, 1);
        }
        else
        {
            bevelRect(g, in, 1, kLight, kDark);
            auto in2 = in.reduced(1);
            bevelRect(g, in2, 1, juce::Colour(0xffdfdfdf), kDark);
        }
    }

    //==========================================================================
    class LedCounter : public juce::Component
    {
    public:
        void setValue(int v)
        {
            v = juce::jlimit(0, 999, v);
            if (v != value) { value = v; repaint(); }
        }

        void paint(juce::Graphics& g) override
        {
            // 1px sunken edge, black background
            g.setColour(kBlack);
            g.fillRect(0, 0, 53, 34);
            g.setColour(kDark);
            g.fillRect(0, 0, 53, 1);
            g.fillRect(0, 0, 1, 34);
            g.setColour(kLight);
            g.fillRect(0, 33, 53, 1);
            g.fillRect(52, 0, 1, 34);

            static const int seg[7][4] = { { 2, 0, 9, 3 }, { 10, 2, 3, 9 }, { 10, 12, 3, 10 }, { 2, 21, 9, 3 },
                                           { 0, 12, 3, 10 }, { 0, 2, 3, 9 }, { 2, 10, 9, 3 } };  // a b c d e f g
            static const char* digs[10] = { "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg" };

            const int d[3] = { value / 100, (value / 10) % 10, value % 10 };
            for (int i = 0; i < 3; ++i)
            {
                const int ox = 4 + i * 16, oy = 5;
                const juce::String on(digs[d[i]]);
                for (int k = 0; k < 7; ++k)
                {
                    g.setColour(on.containsChar((juce::juce_wchar) ('a' + k)) ? kLedOn : kLedOff);
                    g.fillRect(ox + seg[k][0], oy + seg[k][1], seg[k][2], seg[k][3]);
                }
            }
        }

    private:
        int value = 0;
    };

    //==========================================================================
    class SmileyButton : public juce::Button
    {
    public:
        enum class Face { smile, oh, cool, dead };

        SmileyButton() : juce::Button("smiley") { setTooltip("Reset"); }

        void setFace(Face f)
        {
            if (f != face) { face = f; repaint(); }
        }

        void paintButton(juce::Graphics& g, bool, bool down) override
        {
            drawRaisedButton(g, getLocalBounds(), down);

            const float o = down ? 1.0f : 0.0f;
            const float cx = 17.0f + o, cy = 17.0f + o;

            g.setColour(kBlack);
            g.fillEllipse(cx - 12.0f, cy - 12.0f, 24.0f, 24.0f);
            g.setColour(juce::Colour(0xffffff00));
            g.fillEllipse(cx - 11.0f, cy - 11.0f, 22.0f, 22.0f);
            g.setColour(kBlack);

            auto eyes = [&] { g.fillRect(cx - 5.0f, cy - 5.0f, 2.0f, 3.0f); g.fillRect(cx + 3.0f, cy - 5.0f, 2.0f, 3.0f); };
            auto arc = [&](float acx, float acy, float rad, float a0, float a1, float thick)
            {
                juce::Path p;
                p.addCentredArc(acx, acy, rad, rad, 0.0f, a0, a1, true);
                g.strokePath(p, juce::PathStrokeType(thick));
            };
            // juce arcs: 0 = 12 o'clock, clockwise. Bottom smile: from 0.15pi.. 0.85pi in canvas terms
            // (canvas angle 0 = 3 o'clock, clockwise) -> JUCE angle = canvas + pi/2.
            const float pi = juce::MathConstants<float>::pi;
            auto smile = [&] { arc(cx, cy + 1.0f, 6.5f, 0.15f * pi + pi / 2, 0.85f * pi + pi / 2, 2.0f); };

            switch (face)
            {
                case Face::smile: eyes(); smile(); break;
                case Face::oh:
                    eyes();
                    g.fillEllipse(cx - 2.5f, cy + 5.0f - 3.2f, 5.0f, 6.4f);
                    break;
                case Face::cool:
                    g.fillRect(cx - 9.0f, cy - 6.0f, 18.0f, 3.0f);
                    g.fillRect(cx - 8.0f, cy - 4.0f, 7.0f, 5.0f);
                    g.fillRect(cx + 1.0f, cy - 4.0f, 7.0f, 5.0f);
                    smile();
                    break;
                case Face::dead:
                    for (float ex : { cx - 4.0f, cx + 4.0f })
                    {
                        g.drawLine(ex - 2.5f, cy - 7.0f, ex + 2.5f, cy - 2.0f, 1.6f);
                        g.drawLine(ex + 2.5f, cy - 7.0f, ex - 2.5f, cy - 2.0f, 1.6f);
                    }
                    arc(cx, cy + 9.0f, 5.5f, 1.15f * pi + pi / 2, 1.85f * pi + pi / 2, 2.0f);
                    break;
            }
        }

    private:
        Face face = Face::smile;
    };

    //==========================================================================
    class BevelButton : public juce::Button
    {
    public:
        explicit BevelButton(const juce::String& text, bool boldText = false)
            : juce::Button(text), bold(boldText) { setButtonText(text); }

        void paintButton(juce::Graphics& g, bool, bool down) override
        {
            drawRaisedButton(g, getLocalBounds(), down);
            auto r = getLocalBounds().reduced(2);
            if (down) r.translate(1, 1);
            g.setFont(makeFont(12.0f, bold));
            if (! isEnabled())
            {
                g.setColour(kLight);
                g.drawText(getButtonText(), r.translated(1, 1), juce::Justification::centred, false);
                g.setColour(kDark);
            }
            else
                g.setColour(kBlack);
            g.drawText(getButtonText(), r, juce::Justification::centred, false);
        }

    private:
        bool bold;
    };

    //==========================================================================
    // 16 x 6 tile board; draws tiles only (the sunken frame is painted by the editor).
    class TileBoard : public juce::Component
    {
    public:
        enum class Mode { hidden, downloading, converting, done };
        static constexpr int cols = 16, rows = 6, count = 96, ts = 27;

        TileBoard()
        {
            setInterceptsMouseClicks(false, false);
            // Same seeded shuffle + number table as the approved mockup (mulberry32, seed 1337).
            uint32_t a = 1337u;
            auto rnd = [&a]() -> double
            {
                a += 0x6D2B79F5u;
                uint32_t t = (a ^ (a >> 15)) * (1u | a);
                t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
                return double((t ^ (t >> 14))) / 4294967296.0;
            };
            for (int i = 0; i < count; ++i) order[(size_t) i] = i;
            for (int i = count - 1; i > 0; --i)
            {
                const int j = (int) std::floor(rnd() * (i + 1));
                std::swap(order[(size_t) i], order[(size_t) j]);
            }
            for (int r = 0; r < count; ++r) rank[(size_t) order[(size_t) r]] = r;
            for (int i = 0; i < count; ++i)
            {
                const double r = rnd();
                numbers[(size_t) i] = r < 0.12 ? 1 : r < 0.18 ? 2 : r < 0.21 ? 3 : 0;
            }
        }

        static int revealedCount(int percent) { return (int) std::floor(percent / 100.0 * count * 0.75); }
        int mineTileFor(int percent) const { return order[(size_t) juce::jmin(count - 1, revealedCount(percent))]; }

        // mineTile < 0: none. waveCol < 0: none.
        void setView(Mode m, int percent, int waveCol, int mine)
        {
            if (m != mode || percent != pct || waveCol != wave || mine != mineTile)
            {
                mode = m; pct = percent; wave = waveCol; mineTile = mine;
                repaint();
            }
        }

        void paint(juce::Graphics& g) override
        {
            const int revealedN = revealedCount(pct);
            static const juce::Colour numCol[4] = { juce::Colour(0xff0000ff), juce::Colour(0xff008000),
                                                    juce::Colour(0xffff0000), juce::Colour(0xff000080) };
            for (int t = 0; t < count; ++t)
            {
                const int c = t % cols, r = t / cols, x = c * ts, y = r * ts;
                bool revealed = false, isWave = false;
                switch (mode)
                {
                    case Mode::done:        revealed = true; break;
                    case Mode::converting:  revealed = rank[(size_t) t] < revealedN || c < wave; isWave = (c == wave); break;
                    case Mode::downloading: revealed = rank[(size_t) t] < revealedN; break;
                    case Mode::hidden:      break;
                }

                if (t == mineTile)
                {
                    g.setColour(kDark);   g.fillRect(x, y, ts, ts);
                    g.setColour(kLedOn);  g.fillRect(x + 1, y + 1, ts - 1, ts - 1);
                    drawMine(g, (float) (x + 13), (float) (y + 13));
                    continue;
                }
                if (revealed)
                {
                    g.setColour(kDark);  g.fillRect(x, y, ts, ts);
                    g.setColour(kFace);  g.fillRect(x + 1, y + 1, ts - 1, ts - 1);
                    const int n = numbers[(size_t) t];
                    if (mode != Mode::done && n > 0)
                    {
                        g.setColour(numCol[n - 1]);
                        g.setFont(makeFont(16.0f, true));
                        g.drawText(juce::String(n), x, y, ts, ts, juce::Justification::centred, false);
                    }
                }
                else if (isWave)
                {
                    g.setColour(juce::Colour(0xffe6e6e6)); g.fillRect(x, y, ts, ts);
                    bevelRect(g, { x, y, ts, ts }, 1, kLight, kDark);
                }
                else
                {
                    g.setColour(kFace); g.fillRect(x, y, ts, ts);
                    bevelRect(g, { x, y, ts, ts }, 2, kLight, kDark);
                }
            }
        }

    private:
        static void drawMine(juce::Graphics& g, float cx, float cy)
        {
            g.setColour(kBlack);
            g.fillEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.fillRect(cx - 8.0f, cy, 16.0f, 1.5f);
            g.fillRect(cx, cy - 8.0f, 1.5f, 16.0f);
            for (auto d : { std::pair<float, float> { -6, -6 }, { 5, -6 }, { -6, 5 }, { 5, 5 } })
                g.fillRect(cx + d.first, cy + d.second, 2.0f, 2.0f);
            g.setColour(kLight);
            g.fillRect(cx - 2.0f, cy - 2.0f, 2.0f, 2.0f);
        }

        std::array<int, count> order {}, rank {}, numbers {};
        Mode mode = Mode::hidden;
        int pct = 0, wave = -1, mineTile = -1;
    };

    //==========================================================================
    // Shared external-drag logic (verified pattern from the Phase 2.0 spike, Logic Pro AU):
    // performExternalDragDropOfFiles is called SYNCHRONOUSLY from the live mouseDrag event after
    // >= 4 px movement; the guard flag is set BEFORE the call and cleared in the callback and in
    // mouseUp; canMoveFiles = false; the source component is the draggable component itself.
    // Never defer this call (callAsync / timers): macOS needs the window's current event.
    class FileDragSource
    {
    public:
        void mouseDown() { started = false; }
        void mouseUp()   { started = false; }

        // Returns true if a drag was started by this call.
        bool mouseDrag(juce::Component& source, const juce::MouseEvent& e, const juce::File& file)
        {
            if (started || e.getDistanceFromDragStart() < 4)
                return false;
            if (! file.existsAsFile())
                return false;

            started = true;  // before the call so nothing can start a second drag
            juce::Component::SafePointer<juce::Component> safe(&source);
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                juce::StringArray { file.getFullPathName() }, false, &source,
                [safe, this] { if (safe != nullptr) started = false; });
            return true;
        }

    private:
        bool started = false;
    };

    //==========================================================================
    // Result tile (mockup `result_tile`), local origin = (30,119), size 420x150. Whole tile drags.
    class ResultTile : public juce::Component
    {
    public:
        ResultTile() { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }

        void setInfo(const juce::String& newTitle, const juce::String& newMeta, const juce::String& newPath)
        {
            if (newTitle != title || newMeta != meta || newPath != path)
            {
                title = newTitle; meta = newMeta; path = newPath;
                repaint();
            }
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(kFace);
            bevelRect(g, getLocalBounds(), 3, kLight, kDark);

            // Grip icon at (14,14,28,56): 3 cols x 6 rows of 4px dots, 8px pitch
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 6; ++r)
                {
                    const int dx = 14 + 4 + c * 8, dy = 14 + 6 + r * 8;
                    g.setColour(kLight); g.fillRect(dx + 1, dy + 1, 4, 4);
                    g.setColour(kDark);  g.fillRect(dx, dy, 4, 4);
                }

            g.setColour(kBlack);
            g.setFont(makeFont(14.0f, true));
            g.drawText(title, 58, 14, 348, 20, juce::Justification::centredLeft, true);
            g.setFont(makeFont(12.0f));
            g.drawText(meta, 58, 39, 348, 16, juce::Justification::centredLeft, true);

            // Drag strip (14,94,392,40)
            const juce::Rectangle<float> strip(14.0f, 94.0f, 392.0f, 40.0f);
            g.setColour(juce::Colour(0xffd8d8f0));
            g.fillRect(strip);
            juce::Path outline, dashed;
            outline.addRectangle(strip.reduced(1.0f));
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(kNavy);
            g.fillPath(dashed);
            g.setFont(makeFont(13.0f, true));
            g.drawText("Drag me to your DAW  >>", strip.toNearestInt(), juce::Justification::centred, false);
        }

        void mouseDown(const juce::MouseEvent&) override { drag.mouseDown(); }
        void mouseUp(const juce::MouseEvent&) override   { drag.mouseUp(); }
        void mouseDrag(const juce::MouseEvent& e) override { drag.mouseDrag(*this, e, juce::File(path)); }

    private:
        juce::String title, meta, path;
        FileDragSource drag;
    };

    //==========================================================================
    // History header bar (mockup `history_header`): raised, pressed = sunken, arrow right/down.
    class ExpanderBar : public juce::Button
    {
    public:
        ExpanderBar() : juce::Button("history") {}

        void setExpanded(bool e) { if (e != expanded) { expanded = e; repaint(); } }
        bool isExpanded() const { return expanded; }

        void paintButton(juce::Graphics& g, bool, bool down) override
        {
            drawRaisedButton(g, getLocalBounds(), down);
            const float o = down ? 1.0f : 0.0f;
            juce::Path arrow;
            if (expanded)
                arrow.addTriangle(8.0f + o, 8.0f + o, 18.0f + o, 8.0f + o, 13.0f + o, 14.0f + o);   // down
            else
                arrow.addTriangle(9.0f + o, 6.0f + o, 9.0f + o, 16.0f + o, 15.0f + o, 11.0f + o);   // right
            g.setColour(kBlack);
            g.fillPath(arrow);
            g.setFont(makeFont(12.0f));
            g.drawText(getButtonText(), 24 + (int) o, 0 + (int) o, getWidth() - 28, getHeight(),
                       juce::Justification::centredLeft, false);
        }

    private:
        bool expanded = false;
    };

    //==========================================================================
    // History list (mockup `history_list`), hand-painted. Rows are supplied by the editor.
    class HistoryList : public juce::Component
    {
    public:
        struct Row { juce::String id, title, path, format; bool missing = false; };
        static constexpr int rowH = 22;

        std::function<void(const juce::String& id)> onRemove;

        HistoryList() { setWantsKeyboardFocus(true); }

        void setRows(std::vector<Row> r)
        {
            rows = std::move(r);
            if (findRow(selectedId) < 0)
                selectedId.clear();
            clampScroll();
            repaint();
        }

        void setSelectedId(const juce::String& id) { selectedId = id; repaint(); }
        juce::String getSelectedId() const { return selectedId; }
        juce::String getSelectedPath() const
        {
            const int i = findRow(selectedId);
            return i >= 0 ? rows[(size_t) i].path : juce::String();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(kLight);
            const int w = getWidth();
            {
                juce::Graphics::ScopedSaveState ss(g);
                g.reduceClipRegion(2, 2, w - 4, getHeight() - 4);
                if (rows.empty())
                {
                    g.setColour(kDark);
                    g.setFont(makeFont(12.0f));
                    g.drawText("No downloads yet.", 8, 2, 300, rowH, juce::Justification::centredLeft, false);
                }
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const int y = 2 + (int) i * rowH - scrollY;
                    if (y + rowH < 2 || y > getHeight()) continue;
                    const auto& r = rows[i];
                    const bool sel = (r.id == selectedId);
                    if (sel) { g.setColour(kNavy); g.fillRect(2, y, w - 4, rowH); }
                    g.setColour(sel ? kLight : (r.missing ? kDark : kBlack));
                    g.setFont(makeFont(12.0f));
                    g.drawText(r.title, 8, y, 262, rowH, juce::Justification::centredLeft, true);
                    g.drawText(r.format.equalsIgnoreCase("wav") ? "WAV 24-bit" : "MP3 320",
                               276, y, 80, rowH, juce::Justification::centredRight, false);
                    g.drawText(r.missing ? "Missing" : "OK", 366, y, 60, rowH, juce::Justification::centredRight, false);
                }
            }
            // Minimal scroll thumb when the content overflows
            const int content = (int) rows.size() * rowH, view = getHeight() - 4;
            if (content > view)
            {
                const int th = juce::jmax(16, view * view / content);
                const int ty = 2 + (view - th) * scrollY / juce::jmax(1, content - view);
                g.setColour(kDark);
                g.fillRect(w - 2 - 8, ty, 8, th);
            }
            bevelRect(g, getLocalBounds(), 2, kDark, kLight);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            drag.mouseDown();
            const int i = rowAt(e.y);
            dragPath.clear();
            if (i >= 0)
            {
                selectedId = rows[(size_t) i].id;
                if (! rows[(size_t) i].missing)
                    dragPath = rows[(size_t) i].path;
            }
            else
                selectedId.clear();
            repaint();

            if (i >= 0 && e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem(1, "Remove from history");
                m.showMenuAsync(juce::PopupMenu::Options(),
                                [safe = juce::Component::SafePointer<HistoryList>(this), id = selectedId](int result)
                                { if (result == 1 && safe != nullptr && safe->onRemove) safe->onRemove(id); });
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (dragPath.isNotEmpty() && ! e.mods.isPopupMenu())
                drag.mouseDrag(*this, e, juce::File(dragPath));
        }

        void mouseUp(const juce::MouseEvent&) override { drag.mouseUp(); }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            scrollY -= (int) std::round(w.deltaY * 3.0f * rowH * (w.isReversed ? -1.0f : 1.0f));
            clampScroll();
            repaint();
        }

        bool keyPressed(const juce::KeyPress& k) override
        {
            if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selectedId.isNotEmpty())
            {
                if (onRemove) onRemove(selectedId);
                return true;
            }
            if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
            {
                if (rows.empty()) return true;
                int i = findRow(selectedId);
                i = i < 0 ? 0 : juce::jlimit(0, (int) rows.size() - 1, i + (k == juce::KeyPress::downKey ? 1 : -1));
                selectedId = rows[(size_t) i].id;
                const int top = i * rowH, view = getHeight() - 4;
                if (top < scrollY) scrollY = top;
                else if (top + rowH > scrollY + view) scrollY = top + rowH - view;
                clampScroll();
                repaint();
                return true;
            }
            return false;
        }

    private:
        int findRow(const juce::String& id) const
        {
            if (id.isEmpty()) return -1;
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].id == id) return (int) i;
            return -1;
        }
        int rowAt(int y) const
        {
            const int i = (y - 2 + scrollY) / rowH;
            return (y >= 2 && i >= 0 && i < (int) rows.size()) ? i : -1;
        }
        void clampScroll()
        {
            scrollY = juce::jlimit(0, juce::jmax(0, (int) rows.size() * rowH - (getHeight() - 4)), scrollY);
        }

        std::vector<Row> rows;
        juce::String selectedId, dragPath;
        int scrollY = 0;
        FileDragSource drag;
    };

    //==========================================================================
    // First-run rights notice: an in-editor overlay (never a modal OS window). Covers the whole
    // editor so it swallows all mouse input; the editor disables the controls underneath.
    class NoticePanel : public juce::Component
    {
    public:
        std::function<void()> onAccept;

        NoticePanel() : okButton("I understand", true)
        {
            okButton.onClick = [this] { if (onAccept) onAccept(); };
            addAndMakeVisible(okButton);
            setWantsKeyboardFocus(true);
        }

        static juce::Rectangle<int> panelBounds(juce::Rectangle<int> whole)
        {
            return juce::Rectangle<int>(0, 0, 380, 216).withCentre(whole.getCentre());
        }

        void resized() override
        {
            const auto p = panelBounds(getLocalBounds());
            okButton.setBounds(juce::Rectangle<int>(110, 28).withCentre({ p.getCentreX(), p.getBottom() - 30 }));
        }

        void paint(juce::Graphics& g) override
        {
            const auto p = panelBounds(getLocalBounds());
            g.setColour(kBlack);                    // hard shadow
            g.fillRect(p.translated(3, 3));
            drawRaisedButton(g, p, false);

            const juce::Rectangle<int> title(p.getX() + 4, p.getY() + 4, p.getWidth() - 8, 20);
            g.setColour(kNavy);
            g.fillRect(title);
            g.setColour(kLight);
            g.setFont(makeFont(12.0f, true));
            g.drawText("LinkGrabber - Before you start", title.reduced(6, 0), juce::Justification::centredLeft, false);

            g.setColour(kBlack);
            g.setFont(makeFont(12.0f));
            g.drawFittedText(
                "LinkGrabber downloads audio from YouTube links you give it. Downloading may violate "
                "YouTube's Terms of Service and copyright law. Only use it for content you own, that is "
                "licensed for this use, or that is in the public domain. You are responsible for what you "
                "download. This tool is intended for personal use.",
                { p.getX() + 16, title.getBottom() + 12, p.getWidth() - 32, 104 },
                juce::Justification::topLeft, 8, 1.0f);
        }

        bool keyPressed(const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
            {
                if (onAccept) onAccept();
            }
            return true;  // modal for keyboard too
        }

    private:
        BevelButton okButton;
    };

    //==========================================================================
    class LinkGrabberLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LinkGrabberLookAndFeel()
        {
            setColour(juce::ComboBox::textColourId, kBlack);
            setColour(juce::TextEditor::backgroundColourId, kLight);
            setColour(juce::TextEditor::textColourId, kBlack);
            setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            setColour(juce::TextEditor::highlightColourId, kNavy);
            setColour(juce::TextEditor::highlightedTextColourId, kLight);
            setColour(juce::CaretComponent::caretColourId, kBlack);
            setColour(juce::PopupMenu::backgroundColourId, kLight);
        }

        juce::Font getComboBoxFont(juce::ComboBox&) override { return makeFont(12.0f); }
        juce::Font getPopupMenuFont() override { return makeFont(12.0f); }

        void drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override
        {
            drawRaisedButton(g, { 0, 0, w, h }, down);
            // down arrow
            const float ax = (float) w - 14.0f + (down ? 1.0f : 0.0f), ay = (float) h * 0.5f - 1.0f + (down ? 1.0f : 0.0f);
            juce::Path p;
            p.addTriangle(ax, ay, ax + 7.0f, ay, ax + 3.5f, ay + 4.0f);
            g.setColour(kBlack);
            g.fillPath(p);
        }

        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds(8, 2, box.getWidth() - 26, box.getHeight() - 4);
            label.setFont(getComboBoxFont(box));
        }

        void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
        {
            g.fillAll(kLight);
            g.setColour(kBlack);
            g.drawRect(0, 0, w, h, 1);
        }

        void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                               bool isHighlighted, bool, bool, const juce::String& text, const juce::String&,
                               const juce::Drawable*, const juce::Colour*) override
        {
            if (isSeparator) return;
            if (isHighlighted && isActive)
            {
                g.setColour(kNavy);
                g.fillRect(area);
                g.setColour(kLight);
            }
            else
                g.setColour(isActive ? kBlack : kDark);
            g.setFont(makeFont(12.0f));
            g.drawText(text, area.reduced(8, 0), juce::Justification::centredLeft, true);
        }
    };
} // namespace lgui
