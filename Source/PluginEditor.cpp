// DJ Tools – UI (99Hertz hardware look: brushed panel, knurled dials, lit keys, LED screens)
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using APP = DJToolsAudioProcessor;

namespace Pal
{
    static const juce::Colour panelTop  { 0xff202124 };
    static const juce::Colour panelBot  { 0xff141517 };
    static const juce::Colour screen    { 0xff060707 };
    static const juce::Colour silk      { 0xffe4e5e7 };
    static const juce::Colour silkMid   { 0xffb4b7bc };
    static const juce::Colour silkLo    { 0xff7d8086 };
    static const juce::Colour lampOff   { 0xff3a3b3f };
    static const juce::Colour accent    { 0xffff9a2e };   // DJ Tools orange LED
    static const juce::Colour warn      { 0xffff4d3d };
    static const juce::Colour level     { 0xfff4f1e6 };
}

// ── Fonts (embedded, OFL: Barlow Condensed + Share Tech Mono) ────────────────
namespace Fonts
{
    static juce::Typeface::Ptr load (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
    static juce::Typeface::Ptr bold()  { static auto t = load (BinaryData::BarlowCondensedBold_ttf,     BinaryData::BarlowCondensedBold_ttfSize);     return t; }
    static juce::Typeface::Ptr semi()  { static auto t = load (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize); return t; }
    static juce::Typeface::Ptr med()   { static auto t = load (BinaryData::BarlowCondensedMedium_ttf,   BinaryData::BarlowCondensedMedium_ttfSize);   return t; }
    static juce::Typeface::Ptr mono()  { static auto t = load (BinaryData::ShareTechMonoRegular_ttf,    BinaryData::ShareTechMonoRegular_ttfSize);    return t; }
    static juce::Font make (juce::Typeface::Ptr t, float size, float spacing = 0.0f)
    {
        return juce::Font (juce::FontOptions (t).withPointHeight (size).withKerningFactor (spacing));
    }
    static juce::Font silkBold (float s, float sp = 0.2f)  { return make (bold(), s, sp); }
    static juce::Font silkSemi (float s, float sp = 0.16f) { return make (semi(), s, sp); }
    static juce::Font silkMed  (float s, float sp = 0.14f) { return make (med(), s, sp); }
    static juce::Font led      (float s, float sp = 0.06f) { return make (mono(), s, sp); }
}

static float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

static void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float rad = 5.0f)
{
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRoundedRectangle (r.translated (0, 1.0f), rad);
    g.setColour (Pal::screen);
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 10.0f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, rad, 1.0f);
}

static void drawScrew (juce::Graphics& g, juce::Point<float> c, float angle)
{
    const float R = 7.0f;
    juce::ColourGradient gr (juce::Colour (0xff7a7c81), c.x - 1.5f, c.y - 2.5f, juce::Colour (0xff1c1d1f), c.x + R, c.y + R, true);
    gr.addColour (0.6, juce::Colour (0xff3b3c40));
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - R, c.y - R + 1.0f, 2 * R, 2 * R);
    g.setGradientFill (gr);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    juce::Path slot;
    slot.addRoundedRectangle (-5.0f, -1.0f, 10.0f, 2.0f, 1.0f);
    g.setColour (juce::Colour (0xff101012));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

static void padlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto body = r.removeFromBottom (r.getHeight() * 0.58f);
    juce::Path shackle;
    shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.3f, r.getHeight() * 0.85f, 0,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (1.4f));
    g.fillRoundedRectangle (body, 1.5f);
}

static constexpr float dialStart = juce::MathConstants<float>::pi * 1.25f, dialEnd = juce::MathConstants<float>::pi * 2.75f;

// ── LookAndFeel: knurled dials + hardware keys ───────────────────────────────
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel() { setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme()); }

    const juce::Image& knobBody (int D)
    {
        auto& img = bodies[D];
        if (img.isValid()) return img;
        const float sc = 2.0f;
        img = juce::Image (juce::Image::ARGB, (int) (D * sc), (int) (D * sc) + (int) (D * 0.2f * sc), true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (sc));
        const float R = D * 0.5f;
        const juce::Point<float> c (R, R);
        const float rs = R * 0.705f, rc = R * 0.564f;
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            const float e = rs + i * 1.2f;
            g.fillEllipse (c.x - e, c.y - e + R * 0.14f, 2 * e, 2 * e);
        }
        juce::Path skirt; skirt.addEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs);
        g.setColour (juce::Colour (0xff0d0d0f)); g.fillPath (skirt);
        g.setColour (juce::Colour (0xff303135));
        for (int k = 0; k < 60; ++k)
        {
            juce::Path w;
            const float a0 = k * juce::MathConstants<float>::twoPi / 60.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 120.0f;
            w.addPieSegment (c.x - rs, c.y - rs, 2 * rs, 2 * rs, a0, a1, 0.0f);
            g.fillPath (w);
        }
        g.setColour (juce::Colour (0xff050506)); g.drawEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs, 1.0f);
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.16f), 0, c.y - rs, juce::Colours::black.withAlpha (0.42f), 0, c.y + rs, false);
        sh.addColour (0.38, juce::Colours::transparentWhite);
        g.setGradientFill (sh); g.fillPath (skirt);
        juce::ColourGradient cg (juce::Colour (0xff404145), c.x, c.y - rc * 0.48f, juce::Colour (0xff17181a), c.x, c.y + rc * 1.1f, true);
        cg.addColour (0.52, juce::Colour (0xff26272a));
        g.setGradientFill (cg);
        g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawEllipse (c.x - rc + 0.5f, c.y - rc + 0.5f, 2 * rc - 1.0f, 2 * rc - 1.0f, 0.6f);
        g.setColour (juce::Colour (0xff08080a));
        g.drawEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc, 1.0f);
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const int D = juce::jmin (w, h);
        const float R = D * 0.5f;
        const juce::Point<float> c ((float) x + w * 0.5f, (float) y + R);
        const auto& props = s.getProperties();
        if (props.contains ("displayPos")) pos = (float) props["displayPos"];
        const int steps = (int) props.getWithDefault ("steps", 0);
        const bool locked = (bool) props.getWithDefault ("locked", false);
        const float angle = startAngle + juce::jlimit (0.0f, 1.0f, pos) * (endAngle - startAngle);
        if (locked) g.beginTransparencyLayer (0.55f);

        const int ticks = steps > 1 ? steps - 1 : 10;
        for (int k = 0; k <= ticks; ++k)
        {
            const float a = startAngle + k * (endAngle - startAngle) / ticks;
            const float sx = std::sin (a), sy = -std::cos (a);
            const bool strong = steps > 1 || k == 0 || k == ticks || ((bool) props.getWithDefault ("bipolar", false) && k * 2 == ticks);
            g.setColour (juce::Colour (0xffd4d6da).withAlpha (strong ? 0.9f : 0.55f));
            g.drawLine (c.x + sx * R * 0.86f, c.y + sy * R * 0.86f, c.x + sx * R * 0.99f, c.y + sy * R * 0.99f, steps > 1 ? 1.6f : 1.3f);
        }
        const bool bipolar = (bool) props.getWithDefault ("bipolar", false);
        if (bipolar)
        {
            // centre detent mark (12 o'clock), like the hardware click
            g.setColour (juce::Colour (0xfff2f2ef));
            g.fillRoundedRectangle (c.x - 2.0f, c.y - R - 1.0f, 4.0f, R * 0.17f, 1.0f);
        }
        const float arcFrom = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - arcFrom) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 0.79f, R * 0.79f, 0, juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Pal::accent);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto& body = knobBody (D);
        g.drawImage (body, juce::Rectangle<float> (c.x - R, c.y - R, (float) D, (float) body.getHeight() / 2.0f));

        const float rc = R * 0.564f;
        const float sx = std::sin (angle), sy = -std::cos (angle);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f + 1.0f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f + 1.0f, D * 0.045f);
        g.setColour (juce::Colour (0xfff4f4f1));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f, D * 0.045f);
        if (locked)
        {
            g.endTransparencyLayer();
            // padlock badge on the cap
            auto badge = juce::Rectangle<float> (c.x - 9.0f, c.y - 10.0f, 18.0f, 20.0f);
            g.setColour (juce::Colour (0xff17181a).withAlpha (0.85f));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            padlock (g, badge.reduced (3.5f, 3.0f), Pal::silk);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool warnCol = (bool) b.getProperties()["warn"];
            const auto c = warnCol ? Pal::warn : Pal::accent;
            g.setColour (c.withAlpha (over ? 0.22f : 0.12f));
            g.fillRoundedRectangle (r, r.getHeight() / 2);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
            return;
        }
        const bool big = (bool) b.getProperties().getWithDefault ("big", false);
        const bool locked = (bool) b.getProperties().getWithDefault ("locked", false);
        auto r = b.getLocalBounds().toFloat().reduced (big ? 6.0f : 1.0f).withTrimmedBottom (3.0f);
        const bool on = b.getToggleState() || down;
        const bool lit = big && b.getToggleState() && ! locked;   // FX ON: the whole key glows while it's on
        if (on) r.translate (0.0f, 2.0f);
        const float rad = big ? 8.0f : 6.0f;
        if (lit)
        {
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (Pal::accent.withAlpha (0.07f * (float) (4 - i)));
                g.fillRoundedRectangle (r.expanded (1.5f * (float) i), rad + 1.5f * (float) i);
            }
        }
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r.translated (0.0f, on ? 1.0f : 3.0f), rad);
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff34353a : 0xff2e2f33), 0, r.getY(),
                                                 juce::Colour (0xff1c1d20), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (Pal::accent.withAlpha (0.20f), 0, r.getY(), Pal::accent.withAlpha (0.06f), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
        }
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        g.setColour (lit ? Pal::accent.withAlpha (0.75f) : juce::Colour (0xff050506));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            const bool warnCol = (bool) b.getProperties()["warn"];
            g.setFont (Fonts::led (11.0f, 0.06f));
            g.setColour (warnCol ? Pal::warn : Pal::accent);
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
            return;
        }
        const bool locked = (bool) b.getProperties().getWithDefault ("locked", false);
        const bool big = (bool) b.getProperties().getWithDefault ("big", false);
        const bool on  = b.getToggleState() && ! locked;
        const bool blink = (bool) b.getProperties().getWithDefault ("blink", false) && ! locked;
        const bool lit = on || blink || ((bool) b.getProperties().getWithDefault ("lit", false) && ! locked);
        auto r = b.getLocalBounds().toFloat().reduced (big ? 6.0f : 1.0f).withTrimmedBottom (3.0f);
        if (on || down) r.translate (0.0f, 2.0f);
        const bool small = r.getHeight() < 34.0f;

        const float lw = big ? r.getWidth() * 0.46f : (small ? 20.0f : 30.0f), lh = big ? 6.0f : (small ? 3.0f : 4.0f);
        auto lamp = juce::Rectangle<float> (r.getCentreX() - lw / 2, r.getY() + (big ? 16.0f : (small ? 6.0f : 9.0f)), lw, lh);
        if (lit)
        {
            const float a = (blink && ! on) ? 0.7f : 1.0f;
            g.setColour (Pal::accent.withAlpha (0.25f * a));
            g.fillRoundedRectangle (lamp.expanded (big ? 5.0f : 3.0f, big ? 4.0f : 2.5f), big ? 5.0f : 3.0f);
            g.setColour (Pal::accent.withAlpha (a));
        }
        else g.setColour (Pal::lampOff);
        g.fillRoundedRectangle (lamp, lh / 2);

        if (locked)
            padlock (g, { r.getRight() - 17.0f, r.getY() + 6.0f, 9.0f, 11.0f }, Pal::silkLo);
        if (big)
        {
            auto tr = r.withTrimmedTop (lamp.getBottom() - r.getY());
            g.setFont (Fonts::silkBold (24.0f, 0.16f));
            g.setColour (on ? juce::Colour (0xfff2f2ef) : Pal::silkMid);
            g.drawText (b.getButtonText(), tr.withTrimmedBottom (tr.getHeight() * 0.34f), juce::Justification::centredBottom);
            g.setFont (Fonts::silkMed (11.5f, 0.2f));
            g.setColour (on ? Pal::accent : Pal::silkLo);
            g.drawText (on ? "CLICK TO STOP" : "CLICK TO START", tr.withTrimmedTop (tr.getHeight() * 0.70f).withTrimmedBottom (4.0f), juce::Justification::centredTop);
            return;
        }
        g.setFont (Fonts::silkSemi (small ? 12.0f : 14.5f, 0.14f));
        g.setColour (on ? juce::Colour (0xfff2f2ef) : Pal::silkMid.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawText (b.getButtonText(), r.withTrimmedTop (lamp.getBottom() - r.getY()), juce::Justification::centred);
    }

private:
    std::map<int, juce::Image> bodies;
};


// ── KeyGrid ───────────────────────────────────────────────────────────────────
KeyGrid::KeyGrid (juce::RangedAudioParameter& param, juce::StringArray labels, juce::Array<int> values, int columns,
                  std::function<int (int)> normalise)
    : vals (values), norm (std::move (normalise)),
      attachment (param, [this] (float v) { showValue ((int) std::round (v)); }), cols (juce::jmax (1, columns))
{
    jassert (labels.size() == vals.size());
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->onClick = [this, i]
        {
            attachment.setValueAsCompleteGesture ((float) vals[i]);
            if (onPick) onPick (vals[i]);
        };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void KeyGrid::showValue (int v)
{
    if (norm) v = norm (v);
    shown = v;
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i]->getToggleState() != (vals[i] == v)) buttons[i]->setToggleState (vals[i] == v, juce::dontSendNotification);
}

void KeyGrid::setTooltips (const juce::StringArray& tips)
{
    for (int i = 0; i < buttons.size() && i < tips.size(); ++i) buttons[i]->setTooltip (tips[i]);
}

void KeyGrid::resized()
{
    const int rows = (buttons.size() + cols - 1) / cols;
    const int w = (getWidth() - gapX * (cols - 1)) / cols;
    const int h = (getHeight() - gapY * (rows - 1)) / juce::jmax (1, rows);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds ((i % cols) * (w + gapX), (i / cols) * (h + gapY), w, h);
}

// ── ParamKey ──────────────────────────────────────────────────────────────────
ParamKey::ParamKey (juce::RangedAudioParameter& param, const juce::String& text, bool isMomentary)
    : juce::TextButton (text), prm (param),
      attachment (param, [this] (float v) { setToggleState (v > 0.5f, juce::dontSendNotification); }),
      momentary (isMomentary)
{
    attachment.sendInitialUpdate();
}

void ParamKey::mouseDown (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown (e);
    if (momentary) { attachment.beginGesture(); attachment.setValueAsPartOfGesture (1.0f); }
    else attachment.setValueAsCompleteGesture (prm.getValue() > 0.5f ? 0.0f : 1.0f);
}

void ParamKey::mouseUp (const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp (e);
    if (momentary) { attachment.setValueAsPartOfGesture (0.0f); attachment.endGesture(); }
}

// ── BeatGrid ──────────────────────────────────────────────────────────────────
void BeatGrid::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawScreen (g, area);
    const auto p = proc.currentParams();
    const double bpm = proc.lastBpm.load(), ppq = proc.lastPpq.load();
    const double period = dj::Engine::periodBeats (p, bpm);
    const bool on = p.beatActive && proc.unlocked.load();
    auto in = area.reduced (14.0f, 9.0f);
    auto top = in.removeFromTop (14.0f);
    g.setFont (Fonts::led (12.0f, 0.14f));
    const juce::String beatTxt = p.timeMs > 0.5f ? juce::String (juce::roundToInt (p.timeMs)) + " MS"
                                                  : juce::String (dj::divisionName (p.division)) + (period <= 1.0 ? " BEAT" : " BEATS");
    glowText (g, beatTxt, top, juce::Justification::centredLeft, Pal::accent.withAlpha (0.75f));
    glowText (g, juce::String (dj::beatName (p.beatFx)) + (on ? "" : "  OFF"), top, juce::Justification::centredRight,
              Pal::accent.withAlpha (on ? 0.95f : 0.45f));

    // One bar (16 sixteenths), or the whole period when it is longer than a bar.
    const double window = juce::jmax (4.0, period);
    const int cells = 16;
    const double cellBeats = window / cells;
    auto row = in.withTrimmedTop (8.0f);
    const float gap = 3.0f, cw = (row.getWidth() - gap * (cells - 1)) / cells;
    const int now = (int) std::floor ((ppq / window - std::floor (ppq / window)) * cells);
    for (int i = 0; i < cells; ++i)
    {
        const double b = i * cellBeats;
        const double m = std::fmod (b + 1.0e-9, period);
        const bool hit = m < cellBeats - 1.0e-9 || period < cellBeats;
        auto r = juce::Rectangle<float> (row.getX() + i * (cw + gap), row.getY(), cw, row.getHeight());
        const bool beatLine = std::fmod (b + 1.0e-9, 1.0) < cellBeats - 1.0e-9;
        float alpha = hit ? (on ? 0.55f : 0.28f) : (beatLine ? 0.12f : 0.06f);
        if (i == now) alpha = juce::jmax (alpha, on ? 1.0f : 0.5f);
        if (alpha > 0.5f) { g.setColour (Pal::accent.withAlpha (0.22f)); g.fillRect (r.expanded (1.5f)); }
        g.setColour (Pal::accent.withAlpha (alpha));
        g.fillRect (r);
    }
}

// ── ProPanel ──────────────────────────────────────────────────────────────────
static void styleBox (juce::TextEditor& t, const juce::String& placeholder)
{
    t.setColour (juce::TextEditor::backgroundColourId, Pal::screen);
    t.setColour (juce::TextEditor::textColourId, Pal::accent);
    t.setColour (juce::TextEditor::outlineColourId, Pal::lampOff);
    t.setColour (juce::TextEditor::focusedOutlineColourId, Pal::accent);
    t.setColour (juce::CaretComponent::caretColourId, Pal::accent);
    t.setFont (Fonts::led (15.0f, 0.02f));
    t.setTextToShowWhenEmpty (placeholder, Pal::silkLo);
    t.setIndents (10, 10);
    t.setSelectAllWhenFocused (true);
}

ProPanel::ProPanel (DJToolsAudioProcessor& p) : proc (p)
{
    styleBox (emailBox, "you@email.com");
    styleBox (keyBox, "99HZ-...");
    subscribe.setColour (juce::ToggleButton::textColourId, Pal::silkMid);
    subscribe.setColour (juce::ToggleButton::tickColourId, Pal::accent);
    subscribe.setColour (juce::ToggleButton::tickDisabledColourId, Pal::silkLo);
    for (auto* c : std::initializer_list<juce::Component*> { &emailBox, &keyBox, &subscribe, &trialButton, &unlockButton, &buyButton, &closeButton })
        addAndMakeVisible (c);
    buyButton.getProperties().set ("lit", true);
    trialButton.getProperties().set ("lit", true);

    closeButton.onClick = [this] { setVisible (false); };
    buyButton.onClick = [] { juce::URL (dj::buyUrl).launchInDefaultBrowser(); };
    unlockButton.onClick = [this]
    {
        const auto pasted = hz::Licence::verify (keyBox.getText(), proc.licence.product);
        const auto info = proc.licence.apply (keyBox.getText());
        if (info.kind == hz::LicenceInfo::saveFailed)
        {
            message = "COULDN'T SAVE YOUR KEY. EMAIL INFO@99HERTZ.COM";
            messageIsError = true;
        }
        else if (pasted.kind == hz::LicenceInfo::trialExpired)
        {
            message = "THAT TRIAL KEY HAS ENDED";
            messageIsError = true;
        }
        else if (info.unlocked())
        {
            proc.setLicenceInfo (info);
            message = info.kind == hz::LicenceInfo::pro ? "UNLOCKED. THANK YOU!" : "TRIAL UNLOCKED";
            messageIsError = false;
            keyBox.clear();
            if (onLicenceChanged) onLicenceChanged();
        }
        else
        {
            message = "THAT KEY ISN'T VALID FOR DJ TOOLS";
            messageIsError = true;
        }
        refresh();
    };
    trialButton.onClick = [this]
    {
        const auto email = emailBox.getText().trim();
        if (! email.containsChar ('@') || ! email.fromFirstOccurrenceOf ("@", false, false).containsChar ('.'))
        {
            message = "ENTER YOUR EMAIL TO START THE TRIAL"; messageIsError = true; refresh(); return;
        }
        busy = true; message = "STARTING YOUR TRIAL..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.requestTrial (email, subscribe.getToggleState(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (info.unlocked())
            {
                safe->proc.setLicenceInfo (info);
                safe->message = "UNLOCKED FOR 7 DAYS. YOUR KEY IS ON ITS WAY BY EMAIL.";
                safe->messageIsError = false;
                if (safe->onLicenceChanged) safe->onLicenceChanged();
            }
            else { safe->message = error.toUpperCase(); safe->messageIsError = true; }
            safe->refresh();
        });
    };
}

void ProPanel::licenceChanged()
{
    if (isVisible()) refresh();
}

void ProPanel::show (const juce::String& why)
{
    reason = why; message = {}; messageIsError = false;
    refresh();
    setVisible (true);
    toFront (true);
}

juce::Rectangle<int> ProPanel::card() const { return getLocalBounds().withSizeKeepingCentre (600, 456); }

void ProPanel::refresh()
{
    const auto k = proc.licenceInfo.kind;
    const bool canTrial = k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid;
    const bool isPro = k == hz::LicenceInfo::pro;
    emailBox.setVisible (canTrial); subscribe.setVisible (canTrial); trialButton.setVisible (canTrial);
    keyBox.setVisible (! isPro); unlockButton.setVisible (! isPro); buyButton.setVisible (! isPro);
    trialButton.setEnabled (! busy);
    trialButton.setButtonText (busy ? "STARTING..." : "START 7-DAY FREE TRIAL");
    resized();
    repaint();
}

void ProPanel::resized()
{
    const auto c = card();
    const int x = c.getX() + 36, w = c.getWidth() - 72;
    emailBox.setBounds (x, c.getY() + 222, w - 232, 38);
    trialButton.setBounds (x + w - 220, c.getY() + 218, 220, 46);
    subscribe.setBounds (x - 4, c.getY() + 264, 260, 24);
    keyBox.setBounds (x, c.getY() + 320, w - 232, 38);
    unlockButton.setBounds (x + w - 220, c.getY() + 316, 220, 46);
    buyButton.setBounds (x, c.getBottom() - 62, 220, 46);
    closeButton.setBounds (x + w - 120, c.getBottom() - 62, 120, 46);
}

void ProPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) setVisible (false);
}

void ProPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
    const auto c = card().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (c.translated (0, 8.0f).expanded (4.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (Pal::panelTop.brighter (0.04f), 0, c.getY(), Pal::panelBot, 0, c.getBottom(), false));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);

    const float x = c.getX() + 36.0f, w = c.getWidth() - 72.0f;
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", juce::Rectangle<float> (x, c.getY() + 24.0f, w, 14.0f), juce::Justification::centredLeft);
    g.setFont (Fonts::silkBold (28.0f, 0.06f));
    g.setColour (Pal::silk);
    g.drawText ("DJ TOOLS", juce::Rectangle<float> (x, c.getY() + 38.0f, w, 34.0f), juce::Justification::centredLeft);

    const auto k = proc.licenceInfo.kind;
    juce::String sub = reason;
    if (k == hz::LicenceInfo::trial)             sub = "FREE TRIAL: " + proc.licenceInfo.daysLeftText();
    else if (k == hz::LicenceInfo::trialExpired) sub = "YOUR FREE TRIAL HAS ENDED";
    else if (k == hz::LicenceInfo::pro)          sub = "LICENSED TO " + proc.licenceInfo.email.toUpperCase();
    else if (sub.isEmpty())                      sub = "TRY EVERY EFFECT FREE FOR 7 DAYS";
    g.setFont (Fonts::led (14.0f, 0.08f));
    glowText (g, sub, { x, c.getY() + 78.0f, w, 18.0f }, juce::Justification::centredLeft, k == hz::LicenceInfo::trialExpired ? Pal::warn : Pal::accent);

    const char* feats[] = { "COLOR: filter, sweep, dub echo, space and crush on one knob",
                            "BEAT: 14 tempo-synced effects, from echo and reverb to roll and brake",
                            "Every dial and key can be automated and MIDI-mapped in your DAW",
                            "Echo and reverb tails ring out when you switch them off" };
    g.setFont (Fonts::silkMed (16.0f, 0.03f));
    for (int i = 0; i < 4; ++i)
    {
        const float y = c.getY() + 106.0f + i * 22.0f;
        g.setColour (Pal::accent);
        g.fillEllipse (x + 1.0f, y + 7.0f, 6.0f, 6.0f);
        g.setColour (Pal::silk);
        g.drawText (feats[i], juce::Rectangle<float> (x + 16.0f, y, w - 16.0f, 20.0f), juce::Justification::centredLeft);
    }

    g.setFont (Fonts::silkSemi (12.5f, 0.16f));
    g.setColour (Pal::silkMid);
    if (k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid)
        g.drawText ("NO CARD NEEDED. YOUR TRIAL KEY ALSO COMES BY EMAIL.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    else if (k == hz::LicenceInfo::trialExpired)
        g.drawText ("THANKS FOR TRYING DJ TOOLS. BUY IT TO KEEP USING IT.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (k != hz::LicenceInfo::pro)
        g.drawText ("ALREADY BOUGHT IT? PASTE YOUR KEY", juce::Rectangle<float> (x, c.getY() + 302.0f, w, 14.0f), juce::Justification::centredLeft);

    if (message.isNotEmpty())
    {
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (messageIsError ? Pal::warn : Pal::accent);
        g.drawFittedText (message, juce::Rectangle<int> ((int) x, (int) c.getBottom() - 86, (int) w, 18), juce::Justification::centredLeft, 1);
    }
}

// ── LockOverlay ───────────────────────────────────────────────────────────────
LockOverlay::LockOverlay (DJToolsAudioProcessor& p) : proc (p)
{
    openButton.getProperties().set ("lit", true);
    openButton.onClick = [this] { if (onOpen) onOpen(); };
    addAndMakeVisible (openButton);
}

void LockOverlay::resized() { openButton.setBounds (getLocalBounds().withSizeKeepingCentre (320, 50).translated (0, 52)); }

void LockOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0b0c).withAlpha (0.78f));
    const bool ended = proc.licenceInfo.kind == hz::LicenceInfo::trialExpired;
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    padlock (g, { c.x - 11.0f, c.y - 96.0f, 22.0f, 28.0f }, Pal::accent);
    g.setFont (Fonts::silkBold (24.0f, 0.18f));
    g.setColour (Pal::silk);
    g.drawText (ended ? "YOUR FREE TRIAL HAS ENDED" : "DJ TOOLS IS LOCKED", juce::Rectangle<float> (0, c.y - 60.0f, r.getWidth(), 30.0f), juce::Justification::centred);
    g.setFont (Fonts::silkMed (15.0f, 0.04f));
    g.setColour (Pal::silkMid);
    g.drawText (ended ? "Buy DJ Tools or paste your key. Until then your audio passes through untouched."
                      : "Start a free 7-day trial or paste your key. Until then your audio passes through untouched.",
                juce::Rectangle<float> (0, c.y - 26.0f, r.getWidth(), 20.0f), juce::Justification::centred);
}

// ── Formatting ────────────────────────────────────────────────────────────────
static juce::String fmtHz (double hz)
{
    if (hz >= 1000.0) return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " KHZ";
    return juce::String (juce::roundToInt (hz)) + " HZ";
}
static juce::String pct (float v01) { return juce::String (juce::roundToInt (v01 * 100.0f)) + "%"; }

// ── Editor ────────────────────────────────────────────────────────────────────
static juce::RangedAudioParameter& P (DJToolsAudioProcessor& p, const char* id) { return *p.apvts.getParameter (id); }

static constexpr int W = 1100, H = 570, padX = 44;
static constexpr int beatDialsY = 380;   // centre line of the BEATS / TIME / AMOUNT dials

static juce::StringArray colorKeyLabels()
{
    juce::StringArray s;
    for (int k = 0; k < dj::numColorKeys; ++k) s.add (dj::colorName (dj::colorKeyFx[k]));
    return s;
}
static juce::Array<int> colorKeyValues()
{
    juce::Array<int> v;
    for (int k = 0; k < dj::numColorKeys; ++k) v.add (dj::colorKeyFx[k]);
    return v;
}
static juce::StringArray beatKeyLabels()
{
    juce::StringArray s;
    for (int i = 0; i < dj::numBeatFx; ++i) s.add (dj::beatName (i));
    return s;
}
static juce::Array<int> beatKeyValues()
{
    juce::Array<int> v;
    for (int i = 0; i < dj::numBeatFx; ++i) v.add (i);
    return v;
}

// Short hover help, in COLOR key order (dj::colorKeyFx) and BEAT key order (dj::BeatFx).
static const char* colorTips[dj::numColorKeys] = {
    "FILTER: left = low-pass, right = high-pass",
    "SWEEP: left = gate, right = band-pass sweep",
    "DUB ECHO: echo on the mids (left) or highs (right). Rings on after you stop",
    "SPACE: reverb on the lows (left) or highs (right). Rings on after you stop",
    "CRUSH: left = drive, right = bit crush" };
static const char* beatTips[dj::numBeatFx] = {
    "DELAY: one repeat on the beat",
    "ECHO: repeats that fade out. Rings on after FX OFF",
    "PING PONG: echoes bounce left and right",
    "REVERB: reverb sized to the beat. Rings on after FX OFF",
    "TRANS: chops the sound on and off in time",
    "FILTER: filter that sweeps in time",
    "FLANGER: jet sweep in time",
    "PHASER: swirling sweep in time",
    "ROLL: loops the beat while it's on",
    "SLIP ROLL: loops the beat, then drops back in on time",
    "BRAKE: slows the track to a stop, like a turntable",
    "HELIX: grabs a slice and keeps repeating it",
    "SPIRAL: echo that builds and spirals",
    "PITCH: shifts the pitch up or down (use AMOUNT)" };

DJToolsAudioProcessorEditor::DJToolsAudioProcessorEditor (DJToolsAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      colorKeys (P (p, "color_fx"), colorKeyLabels(), colorKeyValues(), dj::numColorKeys, [] (int v) { return dj::colorSlot (v); }),
      beatKeys (P (p, "beat_fx"), beatKeyLabels(), beatKeyValues(), 5),
      bandLow (P (p, "band_low"), "LOW", false), bandMid (P (p, "band_mid"), "MID", false), bandHigh (P (p, "band_high"), "HIGH", false),
      holdKey (P (p, "beat_hold"), "HOLD", true), onKey (P (p, "beat_on"), "FX ON", false),
      gridView (p), lockOverlay (p), proPanel (p)
{
    laf.reset (new HardwareLookAndFeel());
    setLookAndFeel (laf.get());

   #if JucePlugin_Build_Standalone
    // It's an effect: in the standalone app, pass the input through (JUCE mutes it by default).
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    colorKeys.gapX = 10;
    for (auto* c : std::initializer_list<juce::Component*> { &colorKeys, &beatKeys, &bandLow, &bandMid, &bandHigh, &holdKey, &onKey, &gridView })
        addAndMakeVisible (c);
    onKey.getProperties().set ("big", true);
    onKey.setTooltip ("FX ON: switch the beat effect on and off");
    holdKey.setTooltip ("HOLD: the beat effect plays only while you hold this down");
    bandLow.setTooltip ("BAND: effect on the lows. Switch off to keep the lows dry");
    bandMid.setTooltip ("BAND: effect on the mids. Switch off to keep the mids dry");
    bandHigh.setTooltip ("BAND: effect on the highs. Switch off to keep the highs dry");
    {
        juce::StringArray ct, bt;
        for (auto* t : colorTips) ct.add (t);
        for (auto* t : beatTips) bt.add (t);
        colorKeys.setTooltips (ct);
        beatKeys.setTooltips (bt);
    }
    licenceButton.setTooltip ("Licence: start the free trial or enter your key");

    licenceButton.getProperties().set ("pill", true);
    licenceButton.onClick = [this] { openPanel ({}); };
    addAndMakeVisible (licenceButton);

    auto& color = addKnob ("color", "COLOR");
    color.slider.getProperties().set ("bipolar", true);
    color.slider.centreSnap = dj::colorDeadZone;
    color.slider.setMouseDragSensitivity (360);
    color.slider.setTooltip ("COLOR: turn left or right to add the effect. Centre = off. Double-click = off");
    addKnob ("color_param", "PARAM").slider.setTooltip ("Shapes the COLOR effect (resonance, feedback, size...)");
    juce::StringArray divs;
    for (int i = 0; i < dj::numDivisions; ++i) divs.add (dj::divisionName (i));
    addKnob ("beat", "BEATS", divs).slider.setTooltip ("BEATS: how long the effect's step is, locked to your tempo");
    addKnob ("time", "TIME").slider.setTooltip ("TIME: set the length in ms instead. Fully left = SYNC (follow BEATS)");
    addKnob ("level", "AMOUNT").slider.setTooltip ("AMOUNT: how much of the beat effect you hear (PITCH: down / up)");

    addChildComponent (lockOverlay);
    lockOverlay.onOpen = [this] { openPanel ({}); };
    addChildComponent (proPanel);   // last, so it sits on top
    proPanel.onLicenceChanged = [this] { updateLicence(); repaint(); };
    // Picking a BEAT effect while it's off: blink FX ON and say so on the LED.
    beatKeys.onPick = [this] (int) { if (! proc.currentParams().beatActive) hintFrames = hintLength; };

    for (auto id : { "color_fx", "color", "color_param", "beat_fx", "beat", "time", "level", "beat_on", "beat_hold" })
        lastValues[id] = proc.apvts.getRawParameterValue (id)->load();
    ledBeat = std::abs (proc.apvts.getRawParameterValue ("color")->load()) < dj::colorDeadZone;

    setSize (W, H);
    wasUnlocked = proc.unlocked.load();
    updateLicence();
    if (! proc.unlocked.load()) proPanel.show ({});
    timerCallback();
    startTimerHz (30);
}

DJToolsAudioProcessorEditor::~DJToolsAudioProcessorEditor() { setLookAndFeel (nullptr); }

DJToolsAudioProcessorEditor::Knob& DJToolsAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& label, juce::StringArray steps)
{
    auto* k = knobs.add (new Knob());
    k->label = label; k->paramId = id; k->stepLabels = steps;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (dialStart, dialEnd, true);
    auto* prm = proc.apvts.getParameter (id);
    s.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (steps.size() > 1)
    {
        s.getProperties().set ("steps", steps.size());
        s.setMouseDragSensitivity (24 * steps.size());
    }
    addAndMakeVisible (s);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    return *k;
}

DJToolsAudioProcessorEditor::Knob* DJToolsAudioProcessorEditor::knob (const juce::String& id)
{
    for (auto* k : knobs) if (k->paramId == id) return k;
    return nullptr;
}

void DJToolsAudioProcessorEditor::resized()
{
    ledArea = { 290, 30, 440, 66 };
    gridView.setBounds (750, 30, W - padX - 750, 66);

    secColor = { padX, 122, 480, 430 };
    secBeat  = { 560, 122, W - padX - 560, 430 };

    // COLOR: 1. pick one of 5 effects (one row) -> 2. turn COLOR (big dial) -> PARAM shapes it
    colorKeys.setBounds (secColor.getX(), 162, secColor.getWidth(), 54);
    knob ("color")->slider.setBounds (secColor.getX() + 170 - 84, 270, 168, 168);
    knob ("color_param")->slider.setBounds (secColor.getX() + 400 - 42, 312, 84, 84);

    // BEAT: 1. pick an effect (5 x 3 keys) -> 2. BEATS / TIME / AMOUNT -> 3. the big FX ON key (HOLD under it)
    beatKeys.setBounds (secBeat.getX(), 162, secBeat.getWidth(), 132);
    auto place = [&] (const char* id, int cx, int D) { knob (id)->slider.setBounds (cx - D / 2, beatDialsY - D / 2, D, D); };
    place ("beat",  secBeat.getX() + 78,  88);
    place ("time",  secBeat.getX() + 192, 56);
    place ("level", secBeat.getX() + 284, 88);
    const int keyW = 152, keyX = secBeat.getRight() - keyW;
    onKey.setBounds   (keyX, 318, keyW, 128);
    holdKey.setBounds (keyX + 6, 480, keyW - 12, 46);
    bandLow.setBounds  (secBeat.getX(),       480, 74, 46);
    bandMid.setBounds  (secBeat.getX() + 80,  480, 74, 46);
    bandHigh.setBounds (secBeat.getX() + 160, 480, 74, 46);

    lockOverlay.setBounds (0, 113, W, H - 113);
    proPanel.setBounds (getLocalBounds());
    renderBackground();
}

void DJToolsAudioProcessorEditor::renderBackground()
{
    const float sc = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * sc), (int) (H * sc), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (sc));

    auto panel = juce::Rectangle<float> (0, 0, (float) W, (float) H);
    g.setColour (juce::Colour (0xff0a0a0b)); g.fillAll();
    g.setColour (juce::Colour (0xff18191b)); g.fillRoundedRectangle (panel, 10.0f);
    g.setGradientFill (juce::ColourGradient (Pal::panelTop, 0, 0, Pal::panelBot, 0, (float) H, false));
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (int x = 0; x < W; x += 3) g.fillRect ((float) x, 0.0f, 1.0f, (float) H);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (10.0f, 0.5f, W - 10.0f, 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (10.0f, H - 1.0f, W - 10.0f, H - 1.0f, 2.0f);

    drawScrew (g, { 19.0f, 19.0f }, 0.49f);
    drawScrew (g, { W - 19.0f, 19.0f }, -0.91f);
    drawScrew (g, { 19.0f, H - 19.0f }, 1.29f);
    drawScrew (g, { W - 19.0f, H - 19.0f }, -0.21f);

    // Logo
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", padX, 34, 240, 16, juce::Justification::centredLeft);
    g.setFont (Fonts::silkBold (31.0f, 0.08f));
    g.setColour (Pal::silk);
    g.drawText ("DJ TOOLS", padX, 50, 240, 36, juce::Justification::centredLeft);

    // Dividers
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect ((float) padX, 112.0f, (float) (W - 2 * padX), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (542.0f, 126.0f, 1.0f, (float) H - 126.0f - 30.0f);

    // Step legends around the BEAT dial
    for (auto* k : knobs)
    {
        if (k->stepLabels.size() < 2) continue;
        const auto b = k->slider.getBounds().toFloat();
        const auto c = b.getCentre();
        const int n = k->stepLabels.size();
        g.setFont (Fonts::led (10.5f, 0.0f));
        g.setColour (Pal::silkMid);
        for (int s = 0; s < n; ++s)
        {
            const float a = dialStart + s * (dialEnd - dialStart) / (n - 1);
            const float rr = b.getWidth() * 0.5f + 14.0f;
            const juce::Point<float> pt (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr);
            g.drawText (k->stepLabels[s], juce::Rectangle<float> (pt.x - 16.0f, pt.y - 6.0f, 32.0f, 12.0f), juce::Justification::centred);
        }
    }

    // BAND label under the band keys
    g.setFont (Fonts::silkMed (12.0f, 0.2f));
    g.setColour (Pal::silkLo);
    g.drawText ("BAND", bandLow.getX(), bandLow.getBottom() + 6, bandHigh.getRight() - bandLow.getX(), 14, juce::Justification::centred);

    // Section titles + one-line guides (silk-screen)
    auto guide = [&] (juce::Rectangle<int> sec, const juce::String& t)
    {
        g.setFont (Fonts::silkMed (12.0f, 0.14f));
        g.setColour (Pal::silkMid.withAlpha (0.85f));
        g.drawText (t, sec.withHeight (28), juce::Justification::centredRight);
    };
    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    guide (secColor, "TURN TO ADD COLOR" + dot + "CENTRE = OFF");
    guide (secBeat, "PICK AN EFFECT" + dot + "SET BEATS" + dot + "PRESS FX ON");
}

void DJToolsAudioProcessorEditor::updateLicence()
{
    const auto& li = proc.licenceInfo;
    const auto t = li.buttonText();
    const bool warnCol = ! li.unlocked();   // no free tier: locked (no key / trial ended) shows in warning colour
    if (licenceButton.getButtonText() != t || (bool) licenceButton.getProperties()["warn"] != warnCol || licenceButton.getWidth() == 0)
    {
        licenceButton.setButtonText (t);
        licenceButton.getProperties().set ("warn", warnCol);
        const int w = (int) std::ceil (textWidth (Fonts::led (11.0f, 0.06f), t)) + 24;
        licenceButton.setBounds (padX, 88, w, 20);
        licenceButton.repaint();
    }
    const bool locked = ! proc.unlocked.load();
    if (lockOverlay.isVisible() != locked) lockOverlay.setVisible (locked);
    lockOverlay.repaint();
}

static juce::String pitchText (float level)
{
    const double st = dj::Engine::pitchSemitones (level);
    return std::abs (st) < 0.05 ? juce::String ("0 ST") : (st > 0 ? "+" : "-") + juce::String (std::abs (st), 1) + " ST";
}

void DJToolsAudioProcessorEditor::updateLed()
{
    const auto p = proc.currentParams();
    const double bpm = proc.lastBpm.load();
    const bool unlocked = proc.unlocked.load();
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    // Which section did the user touch last?
    bool beatSetupTouched = false;
    for (auto& kv : lastValues)
    {
        const float now = proc.apvts.getRawParameterValue (kv.first)->load();
        if (now == kv.second) continue;
        kv.second = now;
        idleFrames = 0;
        const bool beat = ! kv.first.startsWith ("color");
        ledBeat = beat;
        ledTouch = 0;
        if (beat && kv.first != "beat_on" && kv.first != "beat_hold") beatSetupTouched = true;
        if (kv.first == "color_param")
        {
            touchedLabel = dj::colorParamName (p.colorFx);
            touchedText = pct (p.colorParam);
            ledTouch = 45;
        }
        else if (kv.first == "level")
        {
            touchedLabel = dj::beatParamName (p.beatFx);
            touchedText = p.beatFx == dj::bPitch ? pitchText (p.level) : pct (p.level);
            ledTouch = 45;
        }
        else if (kv.first == "time")
        {
            touchedLabel = "TIME";
            touchedText = p.timeMs > 0.5f ? juce::String (juce::roundToInt (p.timeMs)) + " MS" : juce::String ("SYNC TO BEATS");
            ledTouch = 45;
        }
    }
    // Set up a BEAT effect while it's off -> "PRESS FX ON" (blinks the key too). FX ON clears it.
    if (p.beatActive) hintFrames = 0;
    else if (beatSetupTouched && unlocked) hintFrames = hintLength;
    if (ledTouch > 0) --ledTouch;
    if (hintFrames > 0) --hintFrames;
    if (idleFrames < idleAfter) ++idleFrames;

    ledRightText = juce::String (bpm, 1);
    const float cv = dj::Engine::colorAmount (p.color);
    const bool colorOn = cv != 0.0f, beatOn = p.beatActive;
    const juce::String colorHead = juce::String ("COLOR") + dot + dj::colorName (p.colorFx);
    const double period = dj::Engine::periodBeats (p, bpm);
    juce::String beatLen;
    if (p.beatFx == dj::bPitch)  beatLen = pitchText (p.level);
    else if (p.timeMs > 0.5f)    beatLen = juce::String (juce::roundToInt (p.timeMs)) + " MS";
    else                         beatLen = juce::String (dj::divisionName (p.division)) + (period <= 1.0 ? " BEAT" : " BEATS");

    if (! unlocked)
    {
        ledLabel = proc.licenceInfo.kind == hz::LicenceInfo::trialExpired ? "TRIAL ENDED" : "LOCKED";
        ledText = "AUDIO PASSES THROUGH";
        return;
    }
    if (ledTouch > 0)
    {
        ledLabel = ledBeat ? juce::String ("BEAT FX") + dot + dj::beatName (p.beatFx) : colorHead;
        ledText = touchedLabel + " " + touchedText;
        return;
    }
    if (hintFrames > 0 && ! beatOn)
    {
        // alternate the effect and the instruction, ~3 times a second
        const bool showHint = ((hintLength - hintFrames) / 10) % 2 == 0;
        ledLabel = juce::String ("BEAT FX") + dot + "OFF";
        ledText = showHint ? juce::String ("PRESS FX ON") : juce::String (dj::beatName (p.beatFx)) + " " + beatLen;
        return;
    }
    if (! colorOn && ! beatOn && idleFrames >= idleAfter)
    {
        ledLabel = "READY" + dot + "NOTHING ON";
        ledText = "TURN COLOR OR PRESS FX ON";
        return;
    }
    if (! ledBeat)
    {
        ledLabel = colorHead + (colorOn ? "" : dot + "TURN THE COLOR KNOB");
        if (! colorOn) ledText = juce::String (dj::colorName (p.colorFx)) + dot + "OFF";
        else
        {
            const juce::String side = cv < 0 ? dj::colorLeft (p.colorFx) : dj::colorRight (p.colorFx);
            if (p.colorFx == dj::cFilter) ledText = juce::String (cv < 0 ? "LOW PASS " : "HIGH PASS ") + fmtHz (dj::Engine::colorCutoffHz (cv, p.colorParam));
            else ledText = side + " " + pct (std::abs (cv));
        }
    }
    else
    {
        const double ms = period * 60000.0 / bpm;
        const bool held = proc.apvts.getRawParameterValue ("beat_hold")->load() > 0.5f;
        ledLabel = juce::String ("BEAT FX");
        if (! beatOn) ledLabel += dot + "PRESS FX ON TO HEAR IT";
        else if (p.beatFx != dj::bPitch && p.timeMs <= 0.5f) ledLabel += dot + juce::String (juce::roundToInt (ms)) + " MS";
        ledText = juce::String (dj::beatName (p.beatFx)) + " " + beatLen + dot + (beatOn ? (held ? "HOLD" : "ON") : "OFF");
    }
}

void DJToolsAudioProcessorEditor::timerCallback()
{
    if (proc.unlocked.load() != wasUnlocked || licenceButton.getWidth() == 0
        || proc.licenceVersion.load() != seenLicenceVersion)
    {
        // another instance unlocked, a trial ended, or the panel saved a key
        wasUnlocked = proc.unlocked.load();
        seenLicenceVersion = proc.licenceVersion.load();
        updateLicence();
        proPanel.licenceChanged();
        repaint();
    }
    updateLed();
    const auto p = proc.currentParams();

    knob ("color")->value = {};
    knob ("color_param")->label = dj::colorParamName (p.colorFx);
    knob ("color_param")->value = pct (p.colorParam);
    knob ("beat")->value = p.timeMs > 0.5f ? juce::String ("USING TIME") : juce::String (dj::divisionName (p.division)) + (dj::divisionBeats[p.division] <= 1.0 ? " BEAT" : " BEATS");
    knob ("time")->value = p.timeMs > 0.5f ? juce::String (juce::roundToInt (p.timeMs)) + " MS" : juce::String ("SYNC");
    knob ("level")->label = dj::beatParamName (p.beatFx);
    knob ("level")->value = p.beatFx == dj::bPitch ? pitchText (p.level) : pct (p.level);

    // FX ON blinks while the "PRESS FX ON" hint is up
    const bool blink = hintFrames > 0 && ! p.beatActive && ((hintLength - hintFrames) / 10) % 2 == 0;
    if ((bool) onKey.getProperties().getWithDefault ("blink", false) != blink)
    {
        onKey.getProperties().set ("blink", blink);
        onKey.repaint();
    }

    gridView.repaint();
    repaint (ledArea.expanded (12));
    repaint (secColor);
    repaint (secBeat.withHeight (40));
    repaint (secBeat.withTrimmedTop (176));
}

void DJToolsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    const auto p = proc.currentParams();
    const bool unlocked = proc.unlocked.load();

    // Licensed: the licence email in small print beside the LICENSED pill.
    if (proc.licenceInfo.kind == hz::LicenceInfo::pro && proc.licenceInfo.email.isNotEmpty())
    {
        const auto lb = licenceButton.getBounds();
        g.setFont (Fonts::silkMed (12.0f, 0.04f));
        g.setColour (Pal::silkMid);
        g.drawFittedText (proc.licenceInfo.email, { lb.getRight() + 8, lb.getY(), ledArea.getX() - lb.getRight() - 16, lb.getHeight() },
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    // ── LED display ──
    {
        auto r = ledArea.toFloat();
        drawScreen (g, r);
        auto in = r.reduced (20.0f, 11.0f);
        auto right = in.removeFromRight (82.0f);
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, ledLabel, in.removeFromTop (16.0f), juce::Justification::centredLeft, Pal::accent.withAlpha (0.75f));
        float fs = 22.0f;
        while (fs > 14.0f && textWidth (Fonts::led (fs, 0.06f), ledText) > in.getWidth() - 24.0f) fs -= 1.0f;
        g.setFont (Fonts::led (fs, 0.06f));
        glowText (g, ledText, in, juce::Justification::centredLeft, Pal::accent);
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, "TEMPO", right.removeFromTop (16.0f), juce::Justification::centredRight, Pal::accent.withAlpha (0.75f));
        g.setFont (Fonts::led (22.0f, 0.06f));
        glowText (g, ledRightText, right, juce::Justification::centredRight, Pal::accent);
    }

    // ── Section titles (lamp lit while the section is doing something) ──
    auto title = [&] (juce::Rectangle<int> s, const juce::String& t, bool on)
    {
        auto r = s.toFloat().withHeight (20.0f).translated (0, 4.0f);
        if (on) { g.setColour (Pal::accent.withAlpha (0.3f)); g.fillEllipse (r.getX() - 2.5f, r.getCentreY() - 6.0f, 12.0f, 12.0f); }
        g.setColour (on ? Pal::accent : Pal::lampOff);
        g.fillEllipse (r.getX() + 1.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setFont (Fonts::silkBold (17.0f, 0.22f));
        g.setColour (Pal::silk);
        g.drawText (t, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft);
    };
    const bool colorOn = unlocked && (dj::Engine::colorAmount (p.color) != 0.0f || proc.colorTail.load());
    const bool beatOn  = unlocked && (p.beatActive || proc.beatTail.load());
    title (secColor, "COLOR", colorOn);
    title (secBeat, "BEAT FX", beatOn);

    // ── COLOR dial: left / right words (they change with the effect) ──
    {
        const auto b = knob ("color")->slider.getBounds();
        auto row = juce::Rectangle<int> (b.getCentreX() - 110, b.getBottom() + 8, 220, 16);
        const float v = dj::Engine::colorAmount (p.color);
        g.setFont (Fonts::silkMed (13.0f, 0.16f));
        g.setColour (v < 0 ? Pal::silk : Pal::silkMid);
        g.drawText (dj::colorLeft (p.colorFx), row, juce::Justification::centredLeft);
        g.setColour (v > 0 ? Pal::silk : Pal::silkMid);
        g.drawText (dj::colorRight (p.colorFx), row, juce::Justification::centredRight);
        g.setFont (Fonts::silkSemi (13.0f, 0.16f));
        g.setColour (Pal::silk);
        g.drawText ("COLOR", row.translated (0, 22), juce::Justification::centred);
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (Pal::accent);
        g.drawText (v == 0.0f ? juce::String ("OFF") : pct (std::abs (v)), row.translated (0, 39), juce::Justification::centred);
    }

    // ── Dial labels + values ──
    for (auto* k : knobs)
    {
        if (k->paramId == "color") continue;
        const auto sb = k->slider.getBounds();
        const bool inBeat = sb.getX() >= secBeat.getX();   // BEAT dials share one label line
        auto cell = juce::Rectangle<float> ((float) sb.getCentreX() - 70.0f, inBeat ? (float) beatDialsY + 56.0f : (float) sb.getBottom() + 12.0f, 140.0f, 16.0f);
        g.setFont (Fonts::silkSemi (13.0f, 0.16f));
        g.setColour (Pal::silk);
        g.drawText (k->label, cell, juce::Justification::centred);
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (Pal::accent);
        g.drawText (k->value, cell.translated (0, 17.0f), juce::Justification::centred);
    }
}
