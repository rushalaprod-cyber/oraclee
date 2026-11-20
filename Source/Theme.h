#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#ifndef ORACLEE_VERSION
 #define ORACLEE_VERSION "dev"
#endif

// oraclee visual identity - same family as ravenn: navy steel, brushed metal,
// electric-blue accents, letter-spaced capitals, bracket-cornered panels.
namespace theme
{
const juce::Colour bg { 0xff060a11 };
const juce::Colour bg2 { 0xff080d16 };
const juce::Colour panel { 0xff0b111c };
const juce::Colour panel2 { 0xff101826 };
const juce::Colour panel3 { 0xff172234 };
const juce::Colour line { 0xff1c2a3e };
const juce::Colour frame { 0xff2b3d57 };
const juce::Colour text { 0xffdbe6f5 };
const juce::Colour dim { 0xff7d8ea8 };
const juce::Colour faint { 0xff3a4b64 };
const juce::Colour accent { 0xff3f8cff };  // electric blue
const juce::Colour accent2 { 0xff7cc4ff }; // ice blue

inline juce::Colour layer (int t)
{
    static const juce::Colour c[] = { juce::Colour (0xff62c6ff), juce::Colour (0xff9a8cff),
                                      juce::Colour (0xffffb35c), juce::Colour (0xffff5f8f) };
    return t >= 0 && t < 4 ? c[t] : text;
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

// toUpperCase that also handles French accents (é -> É ...)
inline juce::String upper (const juce::String& s)
{
    auto r = s.toUpperCase();
    const char* pairs[][2] = { { "é", "É" }, { "è", "È" }, { "ê", "Ê" }, { "à", "À" }, { "â", "Â" }, { "ô", "Ô" },
                               { "û", "Û" }, { "ù", "Ù" }, { "ç", "Ç" }, { "ë", "Ë" }, { "ï", "Ï" }, { "î", "Î" } };
    for (auto& p : pairs)
        r = r.replace (juce::String::fromUTF8 (p[0]), juce::String::fromUTF8 (p[1]));
    return r;
}

// letter-spaced capitals, like the ravenn headings
inline juce::Font spaced (float h, float kerning = 0.22f, bool bold = true)
{
    auto f = font (h, bold);
    f.setExtraKerningFactor (kerning);
    return f;
}

// "◆ TITLE ———" section header + bracket corners
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (panel.withAlpha (0.78f));
    g.fillRect (r);
    g.setColour (line.withAlpha (0.9f));
    g.drawRect (r, 1.0f);
    const float c = 12.0f;
    g.setColour (frame.brighter (0.25f));
    auto corner = [&] (float x, float y, float dx, float dy)
    {
        g.drawLine (x, y, x + dx * c, y, 1.6f);
        g.drawLine (x, y, x, y + dy * c, 1.6f);
    };
    corner (r.getX(), r.getY(), 1, 1);
    corner (r.getRight(), r.getY(), -1, 1);
    corner (r.getX(), r.getBottom(), 1, -1);
    corner (r.getRight(), r.getBottom(), -1, -1);
    if (title.isNotEmpty())
    {
        const float ty = r.getY() + 22.0f;
        juce::Path d;
        d.addQuadrilateral (r.getX() + 18.0f, ty - 4.0f, r.getX() + 22.0f, ty, r.getX() + 18.0f, ty + 4.0f, r.getX() + 14.0f, ty);
        g.setColour (accent);
        g.fillPath (d);
        g.setColour (text);
        const auto f = spaced (12.5f, 0.32f);
        g.setFont (f);
        g.drawText (title, (int) r.getX() + 30, (int) ty - 9, 400, 18, juce::Justification::centredLeft);
        const float tw = juce::GlyphArrangement::getStringWidth (f, title) + 44.0f;
        g.setColour (line.brighter (0.15f));
        g.drawHorizontalLine ((int) ty, r.getX() + tw, r.getRight() - 16.0f);
    }
}
} // namespace theme
