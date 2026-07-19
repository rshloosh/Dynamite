/*
  ==============================================================================

    DynamiteComponent.cpp

    Vintage Valley People "Dyna-Mite" faceplate rendering + control wiring.

  ==============================================================================
*/

#include "DynamiteComponent.h"

//==============================================================================
// File-local palette + drawing helpers (shared by every class in this TU).
namespace
{
    using juce::Colour;

    const Colour cMetal      { 0xff6a6e73 }; // brushed anodised aluminium
    const Colour cMetalLight { 0xff9aa0a6 };
    const Colour cMetalDark  { 0xff3b3d41 };
    const Colour cSilk       { 0xfff2e8d0 }; // warm off-white silkscreen
    const Colour cSilkDim    { 0xffb7b099 };
    const Colour cCream      { 0xffece0c4 }; // knob caps
    const Colour cCreamDark  { 0xffb6a986 };
    const Colour cBezel      { 0xff232427 };
    const Colour cLedGreen   { 0xff4fe163 };
    const Colour cLedAmber   { 0xffffb023 };
    const Colour cLedRed     { 0xffff453a };
    const Colour cShadow     { 0x70000000 };

    constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kEndAngle   = juce::MathConstants<float>::pi * 2.75f;

    [[nodiscard]] juce::Font makeFont (float height, bool bold = false)
    {
        auto opts = juce::FontOptions().withHeight (height);
        if (bold)
            opts = opts.withStyle ("Bold");
        return juce::Font (opts);
    }

    // Engraved silkscreen text: a dark drop-shadow under the light glyphs so the
    // label reads on any metal tone.
    void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                       juce::Justification just, const juce::Font& font, Colour colour)
    {
        g.setFont (font);
        g.setColour (cShadow);
        g.drawText (text, area.translated (0, 1), just, false);
        g.setColour (colour);
        g.drawText (text, area, just, false);
    }

    // A single LED with socket, glow and specular highlight.
    void drawLed (juce::Graphics& g, juce::Rectangle<float> r, Colour colour, bool on)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (r.expanded (1.6f));

        if (on)
        {
            g.setColour (colour.withAlpha (0.30f));
            g.fillEllipse (r.expanded (3.2f));

            juce::ColourGradient grad (colour.brighter (0.55f), r.getCentreX(), r.getY(),
                                       colour.darker (0.30f),  r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillEllipse (r);

            auto hl = r.reduced (r.getWidth() * 0.30f)
                       .translated (-r.getWidth() * 0.10f, -r.getHeight() * 0.16f);
            g.setColour (juce::Colours::white.withAlpha (0.60f));
            g.fillEllipse (hl);
        }
        else
        {
            g.setColour (colour.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.15f));
            g.fillEllipse (r);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (r, 0.8f);
        }
    }

    void drawScrew (juce::Graphics& g, float cx, float cy, float slotAngle)
    {
        constexpr float r = 7.0f;
        const juce::Rectangle<float> s (cx - r, cy - r, r * 2.0f, r * 2.0f);

        juce::ColourGradient gr (cMetalLight, cx - r * 0.4f, cy - r * 0.4f,
                                 cMetalDark,  cx + r * 0.6f, cy + r * 0.6f, true);
        g.setGradientFill (gr);
        g.fillEllipse (s);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (s, 1.0f);

        juce::Path slot;
        slot.addRoundedRectangle (cx - r * 0.7f, cy - 1.1f, r * 1.4f, 2.2f, 1.0f);
        slot.applyTransform (juce::AffineTransform::rotation (slotAngle, cx, cy));
        g.setColour (cMetalDark.darker (0.5f));
        g.fillPath (slot);
    }

    // Recessed engraved value window.
    void drawReadout (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        const auto rf = r.toFloat();
        g.setColour (cBezel.withAlpha (0.85f));
        g.fillRoundedRectangle (rf, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 3.0f, 0.8f);
        g.setColour (cSilk);
        g.setFont (makeFont (11.0f, true));
        g.drawText (text, r, juce::Justification::centred, false);
    }

    // knobKind: 0 threshold(dBv) 1 release(s) 2 range(dB) 3 output(dB) 4 mix(%)
    [[nodiscard]] juce::String formatValue (int knobKind, double v)
    {
        switch (knobKind)
        {
            case 0: return juce::String (juce::roundToInt (v)) + " dBv";
            case 1: return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms"
                                   : juce::String (v, 2) + " s";
            case 2: return juce::String (juce::roundToInt (v)) + " dB";
            case 3: { auto s = juce::String (v, 1); return (v > 0.0 ? "+" + s : s) + " dB"; }
            case 4: return juce::String (juce::roundToInt (v * 100.0)) + "%";
            default: return {};
        }
    }
}

//==============================================================================
// DynamiteLookAndFeel
//==============================================================================
DynamiteLookAndFeel::DynamiteLookAndFeel() = default;

void DynamiteLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const juce::Point<float> centre (bounds.getCentreX(), bounds.getCentreY());
    const float radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // ---- Outer skirt (dark, with a top-lit gradient) ----
    {
        juce::ColourGradient sg (cMetalDark.brighter (0.22f), centre.x, centre.y - radius,
                                 juce::Colours::black,        centre.x, centre.y + radius, false);
        g.setGradientFill (sg);
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.60f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // ---- Tick ring (lights up to the current value) ----
    constexpr int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float a  = rotaryStartAngle + (float) i / (float) (numTicks - 1) * (rotaryEndAngle - rotaryStartAngle);
        const auto  p1 = centre.getPointOnCircumference (radius * 0.99f, a);
        const auto  p2 = centre.getPointOnCircumference (radius * 0.80f, a);
        const bool  active = a <= toAngle + 0.001f;
        g.setColour (active ? cSilk : cSilkDim.withAlpha (0.45f));
        g.drawLine ({ p1, p2 }, active ? 1.7f : 1.0f);
    }

    // ---- Cream cap ----
    const float capR = radius * 0.66f;
    {
        juce::ColourGradient cg (cCream.brighter (0.12f), centre.x, centre.y - capR,
                                 cCreamDark,               centre.x, centre.y + capR, false);
        g.setGradientFill (cg);
        g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.4f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawEllipse (centre.x - capR + 1.4f, centre.y - capR + 1.4f, capR * 2.0f - 2.8f, capR * 2.0f - 2.8f, 1.0f);

    // ---- Pointer ----
    const auto pEnd   = centre.getPointOnCircumference (capR * 0.92f, toAngle);
    const auto pStart = centre.getPointOnCircumference (capR * 0.24f, toAngle);
    g.setColour (cBezel.darker (0.4f));
    g.drawLine ({ pStart, pEnd }, 3.2f);

    // ---- Centre hub ----
    const float hub = capR * 0.18f;
    juce::ColourGradient hg (cCream.brighter (0.2f), centre.x, centre.y - hub,
                             cCreamDark.darker (0.15f), centre.x, centre.y + hub, false);
    g.setGradientFill (hg);
    g.fillEllipse (centre.x - hub, centre.y - hub, hub * 2.0f, hub * 2.0f);
}

void DynamiteLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto cap = button.getLocalBounds().toFloat().reduced (1.5f);
    const bool on  = button.getToggleState();

    juce::ColourGradient cg (on ? cMetalLight : cMetalLight.darker (0.05f), cap.getX(), cap.getY(),
                             cMetalDark,  cap.getX(), cap.getBottom(), false);
    g.setGradientFill (cg);
    g.fillRoundedRectangle (cap, 4.0f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (cap, 4.0f);
    }
    if (shouldDrawButtonAsDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (cap, 4.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.60f));
    g.drawRoundedRectangle (cap, 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (cap.reduced (1.0f), 3.5f, 1.0f);

    // Status LED on the left.
    const float d = juce::jmin (12.0f, cap.getHeight() * 0.55f);
    const juce::Rectangle<float> led (cap.getX() + 7.0f, cap.getCentreY() - d * 0.5f, d, d);
    drawLed (g, led, cLedAmber, on);

    // Legend to the right of the LED.
    auto textArea = cap.withTrimmedLeft (7.0f + d + 6.0f).toNearestInt();
    drawEngraved (g, button.getButtonText(), textArea, juce::Justification::centredLeft,
                  makeFont (10.0f, true), on ? cSilk : cSilkDim);
}

//==============================================================================
// ThreePositionSwitch
//==============================================================================
ThreePositionSwitch::ThreePositionSwitch (juce::String title, juce::StringArray legends)
    : titleText (std::move (title))
{
    for (int i = 0; i < legends.size(); ++i)
        box.addItem (legends[i], i + 1);

    box.setSelectedItemIndex (0, juce::dontSendNotification);
    addChildComponent (box);                 // invisible carrier for the attachment
    box.onChange = [this] { repaint(); };

    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
}

void ThreePositionSwitch::resized()
{
    box.setBounds (getLocalBounds());
}

void ThreePositionSwitch::step (int direction)
{
    const int n = box.getNumItems();
    if (n <= 0)
        return;

    const int idx = ((box.getSelectedItemIndex() + direction) % n + n) % n;
    box.setSelectedItemIndex (idx, juce::sendNotificationSync);
}

void ThreePositionSwitch::mouseDown (const juce::MouseEvent& e)
{
    step (e.mods.isRightButtonDown() || e.mods.isPopupMenu() ? -1 : +1);
}

void ThreePositionSwitch::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f)
        step (w.deltaY > 0.0f ? +1 : -1);
}

void ThreePositionSwitch::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();

    const int sel = juce::jmax (0, box.getSelectedItemIndex());
    const int n   = juce::jmax (1, box.getNumItems());
    const auto legendColour = [&] (int i) { return (i == sel) ? cSilk : cSilkDim; };

    // --- Banded layout: title row, then legend row, then knob. Keeping each in
    //     its own horizontal band guarantees the title and legends never overlap. ---
    auto titleRow = b.removeFromTop (15);
    drawEngraved (g, titleText, titleRow, juce::Justification::centred,
                  makeFont (11.0f, true).withExtraKerningFactor (0.18f), cSilk);

    // The centre legend keeps its own band directly above the knob's top
    // detent; the flanking legends are placed beside their own detents below.
    auto legendRow = b.removeFromTop (15);

    // --- Knob occupies the remaining band ---
    const juce::Point<float> c ((float) b.getCentreX(), (float) b.getCentreY());
    float r = juce::jmin ((float) b.getWidth() * 0.30f, (float) b.getHeight() * 0.42f);
    r = juce::jmax (14.0f, r);

    const float angles[3] = { -0.72f, 0.0f, 0.72f };

    // Legend placement: the middle value stays centred above the top detent,
    // while the left/right values tuck in next to their own detents — lower and
    // hard against the knob — so each reads like a silkscreened position marker.
    if (n > 1)
        drawEngraved (g, box.getItemText (1), legendRow,
                      juce::Justification::centred, makeFont (10.5f), legendColour (1));

    constexpr int   legendW   = 70;
    constexpr int   legendH   = 14;
    constexpr float legendGap = 5.0f;   // clearance between detent tick and text
    if (n > 0)
    {
        const auto p = c.getPointOnCircumference (r + 6.0f, angles[0]);
        const juce::Rectangle<int> lr ((int) (p.x - legendGap) - legendW,
                                       (int) p.y - legendH / 2, legendW, legendH);
        drawEngraved (g, box.getItemText (0), lr,
                      juce::Justification::centredRight, makeFont (10.5f), legendColour (0));
    }
    if (n > 2)
    {
        const auto p = c.getPointOnCircumference (r + 6.0f, angles[2]);
        const juce::Rectangle<int> rr ((int) (p.x + legendGap),
                                       (int) p.y - legendH / 2, legendW, legendH);
        drawEngraved (g, box.getItemText (2), rr,
                      juce::Justification::centredLeft, makeFont (10.5f), legendColour (2));
    }

    // Detent tick marks just outside the knob body (stay clear of the legend row).
    for (int i = 0; i < n && i < 3; ++i)
    {
        const bool active = (i == sel);
        const auto outer = c.getPointOnCircumference (r + 6.0f, angles[i]);
        const auto inner = c.getPointOnCircumference (r + 2.0f, angles[i]);
        g.setColour (active ? cSilk : cSilkDim.withAlpha (0.6f));
        g.drawLine ({ inner, outer }, active ? 2.0f : 1.2f);
    }

    // Metal knob body.
    {
        juce::ColourGradient bg (cMetalLight, c.x, c.y - r, cMetalDark, c.x, c.y + r, false);
        g.setGradientFill (bg);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.4f);
    g.setColour (cMetalLight.withAlpha (0.5f));
    g.drawEllipse (c.x - r + 1.4f, c.y - r + 1.4f, r * 2.0f - 2.8f, r * 2.0f - 2.8f, 1.0f);

    // Pointer to the selected detent.
    const float pa = angles[juce::jlimit (0, 2, sel)];
    const auto  pe = c.getPointOnCircumference (r * 0.92f, pa);
    const auto  ps = c.getPointOnCircumference (r * 0.20f, pa);
    g.setColour (cCream);
    g.drawLine ({ ps, pe }, 3.2f);

    // Hub.
    const float hub = r * 0.22f;
    juce::ColourGradient hg (cMetalLight.brighter (0.2f), c.x, c.y - hub,
                             cMetalDark, c.x, c.y + hub, false);
    g.setGradientFill (hg);
    g.fillEllipse (c.x - hub, c.y - hub, hub * 2.0f, hub * 2.0f);
}

//==============================================================================
// DynamiteComponent
//==============================================================================
DynamiteComponent::DynamiteComponent (NewProjectAudioProcessor& p)
    : audioProcessor (p)
{
    const auto setupKnob = [this] (juce::Slider& s, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (kStartAngle, kEndAngle, true);
        s.setLookAndFeel (&lnf);
        s.setTooltip (tip);
        s.onValueChange = [this] { repaint(); };   // refresh engraved value readouts
        addAndMakeVisible (s);
    };

    setupKnob (thresholdKnob, "Threshold (dBv)");
    setupKnob (releaseKnob,   "Release time");
    setupKnob (rangeKnob,     "Range - maximum gain reduction (dB)");
    setupKnob (outputKnob,    "Output gain (dB)");
    setupKnob (mixKnob,       "Dry / Wet mix");

    for (auto* sw : { &sourceSwitch, &modeSwitch, &detectorSwitch })
        addAndMakeVisible (*sw);

    const auto setupButton = [this] (juce::ToggleButton& btn, const juce::String& text, const juce::String& tip)
    {
        btn.setButtonText (text);
        btn.setLookAndFeel (&lnf);
        btn.setTooltip (tip);
        addAndMakeVisible (btn);
    };

    setupButton (autoMakeupButton, "AUTO MAKEUP", "Automatic make-up gain");
    setupButton (scListenButton,   "SC LISTEN",   "Monitor the side-chain / detector signal");
    setupButton (stereoLinkButton, "STEREO LINK", "Link gain reduction across channels");
    setupButton (safetyButton,     "SAFETY",      "Output safety ceiling");

    // ---- Bind to APVTS (Choice items were added inside each switch's ctor) ----
    auto& s = audioProcessor.parameters;
    thresholdAtt  = std::make_unique<SA> (s, "threshold", thresholdKnob);
    releaseAtt    = std::make_unique<SA> (s, "release",   releaseKnob);
    rangeAtt      = std::make_unique<SA> (s, "range",     rangeKnob);
    outputAtt     = std::make_unique<SA> (s, "output",    outputKnob);
    mixAtt        = std::make_unique<SA> (s, "mix",       mixKnob);

    sourceAtt     = std::make_unique<CA> (s, "source",   sourceSwitch.getComboBox());
    modeAtt       = std::make_unique<CA> (s, "mode",     modeSwitch.getComboBox());
    detectorAtt   = std::make_unique<CA> (s, "detector", detectorSwitch.getComboBox());

    autoMakeupAtt = std::make_unique<BA> (s, "autoMakeup", autoMakeupButton);
    scListenAtt   = std::make_unique<BA> (s, "scListen",   scListenButton);
    stereoLinkAtt = std::make_unique<BA> (s, "stereoLink", stereoLinkButton);
    safetyAtt     = std::make_unique<BA> (s, "safety",     safetyButton);

    setSize (660, 450);
    startTimerHz (35);
}

DynamiteComponent::~DynamiteComponent()
{
    stopTimer();

    // Attachments off first, then detach the LookAndFeel from every control.
    thresholdAtt.reset(); releaseAtt.reset(); rangeAtt.reset(); outputAtt.reset(); mixAtt.reset();
    sourceAtt.reset(); modeAtt.reset(); detectorAtt.reset();
    autoMakeupAtt.reset(); scListenAtt.reset(); stereoLinkAtt.reset(); safetyAtt.reset();

    for (auto* knob : { &thresholdKnob, &releaseKnob, &rangeKnob, &outputKnob, &mixKnob })
        knob->setLookAndFeel (nullptr);
    for (auto* btn : { &autoMakeupButton, &scListenButton, &stereoLinkButton, &safetyButton })
        btn->setLookAndFeel (nullptr);
}

//==============================================================================
void DynamiteComponent::timerCallback()
{
    const float gr = juce::jmax (0.0f, audioProcessor.getCurrentGainReduction());

    // Fast attack, slow release ballistics.
    grDisplay += (gr - grDisplay) * (gr > grDisplay ? 0.55f : 0.14f);

    // Peak-hold with a slow post-hold decay.
    if (gr >= grPeak)          { grPeak = gr; peakHold = 42; }
    else if (peakHold > 0)     { --peakHold; }
    else                       { grPeak += (gr - grPeak) * 0.10f; }

    if (audioProcessor.getOverload()) { overloadOn = true; overloadHold = 18; }
    else if (overloadHold > 0)        { --overloadHold; }
    else                              { overloadOn = false; }

    repaint (meterArea);
}

//==============================================================================
void DynamiteComponent::resized()
{
    buildPanelImage();

    auto content = getLocalBounds().reduced (20, 16);

    headerArea = content.removeFromTop (74);
    const auto meterRaw = headerArea.removeFromRight (300);
    meterArea    = meterRaw.reduced (2, 6);
    wordmarkArea = headerArea;

    // Four main knobs.
    auto knobRow = content.removeFromTop (152);
    const int colW = knobRow.getWidth() / 4;
    juce::Slider* knobs[4] = { &thresholdKnob, &releaseKnob, &rangeKnob, &outputKnob };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = (i < 3) ? knobRow.removeFromLeft (colW) : knobRow;
        knobCells[(size_t) i] = cell;

        auto kb = cell;
        kb.removeFromTop (16);
        kb.removeFromBottom (18);
        const int d = juce::jmin (kb.getWidth(), kb.getHeight());
        knobs[i]->setBounds (kb.withSizeKeepingCentre (d, d));
    }

    // Three rotary selectors.
    auto selRow = content.removeFromTop (104);
    const int selW = selRow.getWidth() / 3;
    sourceSwitch  .setBounds (selRow.removeFromLeft (selW));
    modeSwitch    .setBounds (selRow.removeFromLeft (selW));
    detectorSwitch.setBounds (selRow);

    // Modern extras strip: MIX knob + four toggles.
    extrasArea = content;
    auto ex = extrasArea.reduced (4, 6);

    mixCell = ex.removeFromLeft (120);
    {
        auto kb = mixCell;
        kb.removeFromTop (14);
        kb.removeFromBottom (15);
        const int d = juce::jmin (kb.getWidth(), kb.getHeight());
        mixKnob.setBounds (kb.withSizeKeepingCentre (d, d));
    }

    ex.removeFromLeft (8);
    juce::ToggleButton* btns[4] = { &autoMakeupButton, &scListenButton, &stereoLinkButton, &safetyButton };
    const int bw = ex.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = (i < 3) ? ex.removeFromLeft (bw) : ex;
        const int h = juce::jmin (30, cell.getHeight() - 12);
        btns[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 10, h));
    }
}

//==============================================================================
void DynamiteComponent::buildPanelImage()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    panelImage = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (panelImage);

    // Base vertical metal gradient.
    juce::ColourGradient grad (cMetal.brighter (0.14f), 0.0f, 0.0f,
                               cMetal.darker (0.12f),   0.0f, (float) h, false);
    grad.addColour (0.5, cMetal);
    g.setGradientFill (grad);
    g.fillAll();

    // Brushed hairlines (deterministic so it never shimmers between repaints).
    juce::Random rng (0x0d17a17eLL);
    for (int x = 0; x < w; ++x)
    {
        const float n = rng.nextFloat() - 0.5f;
        g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 0.07f, 0.028f + n * 0.06f)));
        g.drawVerticalLine (x, 0.0f, (float) h);

        if (rng.nextFloat() < 0.14f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.05f));
            g.drawVerticalLine (x, 0.0f, (float) h);
        }
    }

    // Top sheen + frame bevel.
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), 0.0f, 0.0f,
                                juce::Colours::transparentWhite,        0.0f, (float) h * 0.4f, false);
    g.setGradientFill (sheen);
    g.fillRect (0, 0, w, (int) ((float) h * 0.4f));

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRect (0, 0, w, h, 1);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRect (1, 1, w - 2, h - 2, 1);
}

//==============================================================================
void DynamiteComponent::paint (juce::Graphics& g)
{
    if (panelImage.isValid())
        g.drawImageAt (panelImage, 0, 0);
    else
        g.fillAll (cMetal);

    drawScrews (g);
    drawHeader (g);
    drawMeter (g);
    drawKnobFurniture (g);
    drawExtras (g);
}

void DynamiteComponent::drawScrews (juce::Graphics& g) const
{
    const auto b = getLocalBounds();
    drawScrew (g, (float) b.getX() + 16.0f,      (float) b.getY() + 16.0f,       0.5f);
    drawScrew (g, (float) b.getRight() - 16.0f,  (float) b.getY() + 16.0f,      -0.6f);
    drawScrew (g, (float) b.getX() + 16.0f,      (float) b.getBottom() - 16.0f,  1.1f);
    drawScrew (g, (float) b.getRight() - 16.0f,  (float) b.getBottom() - 16.0f,  0.2f);
}

void DynamiteComponent::drawHeader (juce::Graphics& g) const
{
    auto a = wordmarkArea;

    auto brand = a.removeFromTop (18);
    drawEngraved (g, "VALLEY PEOPLE", brand, juce::Justification::centredLeft,
                  makeFont (12.0f, true).withExtraKerningFactor (0.30f), cSilkDim);

    auto mark = a.removeFromTop (44);
    drawEngraved (g, "dyna-mite", mark, juce::Justification::centredLeft,
                  makeFont (42.0f, true), cSilk);

    drawEngraved (g, "dynamic gate / limiter / de-esser", a.removeFromTop (14),
                  juce::Justification::centredLeft, makeFont (10.5f), cSilkDim);
}

void DynamiteComponent::drawMeter (juce::Graphics& g) const
{
    const auto hf = meterArea.toFloat();

    // Recessed meter window.
    g.setColour (cBezel);
    g.fillRoundedRectangle (hf, 7.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (hf.reduced (0.5f), 7.0f, 1.4f);
    g.setColour (cMetalLight.withAlpha (0.25f));
    g.drawRoundedRectangle (hf.reduced (1.5f), 6.0f, 1.0f);

    auto inner = meterArea.reduced (10, 7);

    auto capRow = inner.removeFromTop (12);
    drawEngraved (g, "GAIN REDUCTION", capRow.removeFromLeft (inner.getWidth() - 34),
                  juce::Justification::centredLeft, makeFont (9.5f, true).withExtraKerningFactor (0.10f), cSilkDim);
    drawEngraved (g, "dB", capRow, juce::Justification::centredRight, makeFont (9.5f, true), cSilkDim);

    auto olBlock = inner.removeFromRight (54);
    inner.removeFromRight (6);

    auto labelRow = inner.removeFromBottom (11);
    auto ledRow   = inner;

    static const float thr[8]  = { 1.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 30.0f, 40.0f };
    static const char* lab[8]  = { "1", "3", "6", "10", "15", "20", "30", "40" };

    int peakIdx = -1;
    for (int i = 0; i < 8; ++i)
        if (grPeak >= thr[i])
            peakIdx = i;

    const float cellW = (float) ledRow.getWidth() / 8.0f;
    const float d     = juce::jmin (cellW * 0.60f, (float) ledRow.getHeight() * 0.62f);

    for (int i = 0; i < 8; ++i)
    {
        const auto cell = ledRow.toFloat().withX ((float) ledRow.getX() + cellW * (float) i).withWidth (cellW);
        const auto led  = juce::Rectangle<float> (0.0f, 0.0f, d, d)
                              .withCentre ({ cell.getCentreX(), cell.getCentreY() });

        const Colour c  = (i < 3) ? cLedGreen : (i < 6 ? cLedAmber : cLedRed);
        const bool  on  = grDisplay >= thr[i];
        drawLed (g, led, c, on || i == peakIdx);

        const juce::Rectangle<int> lc ((int) cell.getX(), labelRow.getY(), (int) cellW, labelRow.getHeight());
        drawEngraved (g, lab[i], lc, juce::Justification::centred, makeFont (8.5f), on ? cSilk : cSilkDim);
    }

    // Overload lamp.
    const float od = juce::jmin (14.0f, (float) ledRow.getHeight() * 0.7f);
    const auto  ol = juce::Rectangle<float> (0.0f, 0.0f, od, od)
                         .withCentre ({ (float) olBlock.getCentreX(), (float) ledRow.getCentreY() });
    drawLed (g, ol, cLedRed, overloadOn);

    const juce::Rectangle<int> olLabel (olBlock.getX() - 6, labelRow.getY(), olBlock.getWidth() + 12, labelRow.getHeight());
    drawEngraved (g, "OVERLOAD", olLabel, juce::Justification::centred,
                  makeFont (7.0f, true), overloadOn ? cLedRed.brighter (0.3f) : cSilkDim);
}

void DynamiteComponent::drawKnobFurniture (juce::Graphics& g) const
{
    struct Item { const juce::Slider* slider; const char* caption; int kind; };
    const Item items[4] =
    {
        { &thresholdKnob, "THRESHOLD", 0 },
        { &releaseKnob,   "RELEASE",   1 },
        { &rangeKnob,     "RANGE",     2 },
        { &outputKnob,    "OUTPUT",    3 },
    };

    for (int i = 0; i < 4; ++i)
    {
        auto cell = knobCells[(size_t) i];

        auto cap = cell.removeFromTop (16);
        drawEngraved (g, items[i].caption, cap, juce::Justification::centred,
                      makeFont (11.5f, true).withExtraKerningFactor (0.08f), cSilk);

        auto vr = cell.removeFromBottom (18);
        const juce::Rectangle<int> readout =
            juce::Rectangle<int> (0, 0, 72, 16).withCentre ({ vr.getCentreX(), vr.getCentreY() });
        drawReadout (g, readout, formatValue (items[i].kind, items[i].slider->getValue()));
    }
}

void DynamiteComponent::drawExtras (juce::Graphics& g) const
{
    const auto ef = extrasArea.toFloat();

    // Recessed darker strip that reads as a modern add-on module.
    g.setColour (cMetalDark.withAlpha (0.55f));
    g.fillRoundedRectangle (ef.reduced (1.0f), 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (ef.reduced (1.0f), 6.0f, 1.0f);
    g.setColour (cMetalLight.withAlpha (0.15f));
    g.drawLine (ef.getX() + 8.0f, ef.getY() + 1.5f, ef.getRight() - 8.0f, ef.getY() + 1.5f, 1.0f);

    // MIX knob furniture.
    auto cell = mixCell;
    auto cap  = cell.removeFromTop (14);
    drawEngraved (g, "MIX", cap, juce::Justification::centred, makeFont (10.5f, true), cSilk);

    auto vr = cell.removeFromBottom (15);
    const juce::Rectangle<int> readout =
        juce::Rectangle<int> (0, 0, 58, 14).withCentre ({ vr.getCentreX(), vr.getCentreY() });
    drawReadout (g, readout, formatValue (4, mixKnob.getValue()));
}
