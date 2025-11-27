#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DynamiteComponent : public juce::Component,
                          public juce::Timer,
                          public juce::Slider::Listener,
                          private juce::Label::Listener
{
public:
    DynamiteComponent(NewProjectAudioProcessor&);
    ~DynamiteComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Timer callback for meter updates
    void timerCallback() override;

    void sliderValueChanged(juce::Slider* slider) override;
    void labelTextChanged(juce::Label* labelThatHasChanged) override {}

    // Mouse event handlers for tooltip functionality
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    // Reference to processor
    NewProjectAudioProcessor& audioProcessor;
    
    // Knob styling function
    struct KnobStyle : public juce::LookAndFeel_V4
    {
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, 
                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle, 
                              juce::Slider& slider) override;
    };
    
    KnobStyle knobStyle;
    
    // UI components
    juce::Slider thresholdKnob;
    juce::Slider releaseKnob;
    juce::Slider rangeKnob;
    juce::Slider outputKnob;
    juce::Slider mixKnob;
    juce::ComboBox detectionToggle;    // INT/DS/EXT
    juce::ComboBox modeToggle;         // GR/OUT/LIMIT
    juce::ComboBox detectorToggle;     // GATE/PEAK/AVG
    juce::Slider gainReductionSlider;
    
    // Label for mix knob
    std::unique_ptr<juce::Label> mixLabel;
    
    // Tooltips and hover state
    juce::Rectangle<int> hoverArea;
    juce::String hoverText;
    bool isHovering = false;
    juce::Component* hoveredComponent = nullptr;
    
    // Setup methods
    void setupControls();
    void setupMeters();
    
    // Drawing methods
    void drawBackground(juce::Graphics& g);
    void drawKnob(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                 const juce::String& label, float value, float minVal, float maxVal);
    void drawLED(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                bool isOn, const juce::Colour& onColor);
    void drawToggleSwitch(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                         const juce::StringArray& options, int position);
    void drawGainReductionMeter(juce::Graphics& g, const juce::Rectangle<int>& bounds, float gainReduction);
    void drawHoverTooltip(juce::Graphics& g);
    
    // Parameter attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> rangeAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> mixKnobAttachment;
    std::unique_ptr<ComboBoxAttachment> detectionToggleAttachment;
    std::unique_ptr<ComboBoxAttachment> modeToggleAttachment;
    std::unique_ptr<ComboBoxAttachment> detectorToggleAttachment;
    std::unique_ptr<SliderAttachment> gainReductionAttachment;
    
    // Get the display name for a component
    juce::String getComponentName(juce::Component* component);
    
    // Prevent copying
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamiteComponent)
}; 