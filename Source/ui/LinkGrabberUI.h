#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>

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
