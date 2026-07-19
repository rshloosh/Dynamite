/*
  ==============================================================================

    DynamiteComponent.h

    Faithful vintage faceplate for the Valley People "Dyna-Mite" dynamics
    processor. Brushed-metal panel, cream/black skirted knobs, an 8-LED gain
    reduction ladder with a separate OVERLOAD lamp, three 3-position rotary
    selectors, and a tastefully secondary "modern extras" row.

    All controls bind to the plugin's APVTS via Slider/ComboBox/Button
    attachments. The gain-reduction meter is driven by a ~35 Hz Timer that
    reads audioProcessor.getCurrentGainReduction() and getOverload().

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

//==============================================================================
/** Custom LookAndFeel: vintage cream/black skirted rotary knobs and small
    illuminated modern toggle buttons. */
class DynamiteLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DynamiteLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamiteLookAndFeel)
};

//==============================================================================
/** A vintage 3-position rotary selector. It draws a metal knob whose pointer
    indexes one of three silkscreened legends. A hidden juce::ComboBox carries
    the actual selection so a ComboBoxAttachment can bind it to a Choice
    parameter; clicking (or the mouse wheel) steps the selection. */
class ThreePositionSwitch : public juce::Component
{
public:
    ThreePositionSwitch (juce::String title, juce::StringArray legends);

    [[nodiscard]] juce::ComboBox& getComboBox() noexcept { return box; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void step (int direction);

    juce::String  titleText;
    juce::ComboBox box;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreePositionSwitch)
};

//==============================================================================
class DynamiteComponent : public juce::Component,
                          private juce::Timer
{
public:
    explicit DynamiteComponent (NewProjectAudioProcessor&);
    ~DynamiteComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void buildPanelImage();
    void drawScrews        (juce::Graphics&) const;
    void drawHeader        (juce::Graphics&) const;
    void drawMeter         (juce::Graphics&) const;
    void drawKnobFurniture (juce::Graphics&) const;
    void drawExtras        (juce::Graphics&) const;

    NewProjectAudioProcessor& audioProcessor;

    // LookAndFeel first -> destroyed last (after the controls that reference it).
    DynamiteLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 550 };

    // ---- Controls ----
    juce::Slider thresholdKnob, releaseKnob, rangeKnob, outputKnob, mixKnob;

    ThreePositionSwitch sourceSwitch   { "SOURCE",   { "INT", "DS-FM", "EXT" } };
    ThreePositionSwitch modeSwitch     { "MODE",     { "LIMIT", "EXPAND", "OUT" } };
    ThreePositionSwitch detectorSwitch { "DETECTOR", { "AVG", "PEAK", "GATE" } };

    juce::ToggleButton autoMakeupButton, scListenButton, stereoLinkButton, safetyButton;

    // ---- Attachments (declared last -> destroyed first) ----
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SA> thresholdAtt, releaseAtt, rangeAtt, outputAtt, mixAtt;
    std::unique_ptr<CA> sourceAtt, modeAtt, detectorAtt;
    std::unique_ptr<BA> autoMakeupAtt, scListenAtt, stereoLinkAtt, safetyAtt;

    // ---- Layout ----
    juce::Rectangle<int> headerArea, wordmarkArea, meterArea, extrasArea, mixCell;
    std::array<juce::Rectangle<int>, 4> knobCells;

    // Cached brushed-metal background (rebuilt on resize; editor is fixed size).
    juce::Image panelImage;

    // ---- Meter ballistics (GUI thread only) ----
    float grDisplay   = 0.0f;   // smoothed gain reduction (dB, >= 0)
    float grPeak      = 0.0f;   // peak-hold value (dB, >= 0)
    int   peakHold    = 0;      // remaining hold ticks
    bool  overloadOn  = false;
    int   overloadHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamiteComponent)
};
