#include "DynamiteComponent.h"

// Implementation of the KnobStyle look and feel
void DynamiteComponent::KnobStyle::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, 
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle, 
                          juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto centerX = bounds.getCentreX();
    auto centerY = bounds.getCentreY();
    
    // Draw 3D knob body with gradient
    juce::ColourGradient gradient(
        juce::Colour(90, 90, 90), centerX, centerY - radius * 0.2f,
        juce::Colour(30, 30, 30), centerX, centerY + radius * 1.2f,
        true);
    
    g.setGradientFill(gradient);
    g.fillEllipse(centerX - radius, centerY - radius, radius * 2.0f, radius * 2.0f);
    
    // Add metallic ring around the knob
    g.setColour(juce::Colour(120, 120, 120));
    g.drawEllipse(centerX - radius, centerY - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    
    // Draw knob indicator - make it more visible
    g.setColour(juce::Colours::white);
    
    juce::Path p;
    auto pointerLength = radius * 0.8f;
    auto pointerThickness = 2.5f;
    
    // Add a more visible pointer
    p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
    p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centerX, centerY));
    
    g.fillPath(p);
    
    // Draw knob center dot with gradient
    juce::ColourGradient centerGradient(
        juce::Colours::white.withAlpha(0.8f), centerX, centerY - radius * 0.15f,
        juce::Colours::silver.withAlpha(0.9f), centerX, centerY + radius * 0.15f,
        true);
    
    g.setGradientFill(centerGradient);
    g.fillEllipse(centerX - radius * 0.2f, centerY - radius * 0.2f, radius * 0.4f, radius * 0.4f);
    
    // Add a highlight to give 3D effect - using proper path creation
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    juce::Path highlightPath;
    highlightPath.addArc(centerX - radius * 0.8f, centerY - radius * 0.8f, 
                        radius * 1.6f, radius * 0.5f, 0.f, juce::MathConstants<float>::pi, true);
    g.fillPath(highlightPath);
}

DynamiteComponent::DynamiteComponent(NewProjectAudioProcessor& p)
    : audioProcessor(p)
{
    // Set up UI components
    setupControls();
    setupMeters();
    
    // Create parameter attachments
    // Explicitly re-create the threshold attachment to ensure it's properly connected
    thresholdAttachment = nullptr; // Clear any existing attachment first
    thresholdAttachment.reset(new SliderAttachment(audioProcessor.parameters, "threshold", thresholdKnob));
    
    // Set up other attachments
    releaseAttachment.reset(new SliderAttachment(audioProcessor.parameters, "release", releaseKnob));
    rangeAttachment.reset(new SliderAttachment(audioProcessor.parameters, "range", rangeKnob));
    outputAttachment.reset(new SliderAttachment(audioProcessor.parameters, "output", outputKnob));
    
    // Set up 3-position toggle attachments
    detectionToggleAttachment.reset(new ComboBoxAttachment(audioProcessor.parameters, "detection_mode", detectionToggle));
    modeToggleAttachment.reset(new ComboBoxAttachment(audioProcessor.parameters, "mode_mode", modeToggle));
    detectorToggleAttachment.reset(new ComboBoxAttachment(audioProcessor.parameters, "detector_type", detectorToggle));
    
    // Gain reduction slider
    gainReductionAttachment.reset(new SliderAttachment(audioProcessor.parameters, "gain_reduction", gainReductionSlider));
    
    // Force a refresh of the threshold parameter after attachment
    juce::Logger::writeToLog("Initializing threshold knob attachment - current value: " + 
                            juce::String(thresholdKnob.getValue()));
    
    // Add the Mix knob - positioned near the bottom with corrected settings
    mixKnob.setLookAndFeel(&knobStyle);
    mixKnob.setRange(0.0, 1.0, 0.01);
    mixKnob.setValue(1.0, juce::dontSendNotification); // Initialize to 100% wet
    mixKnob.setTextValueSuffix(" %");
    mixKnob.setDoubleClickReturnValue(true, 1.0); // Reset to 100% wet
    mixKnob.setTooltip("Dry/Wet mix - 0% = fully dry, 100% = fully wet");
    mixKnob.addListener(this);
    addAndMakeVisible(mixKnob);
    mixKnobAttachment.reset(new SliderAttachment(audioProcessor.parameters, "mix", mixKnob));
    
    // Add the label for Mix knob
    mixLabel.reset(new juce::Label("Mix Label", "MIX"));
    addAndMakeVisible(mixLabel.get());
    mixLabel->setFont(juce::Font(9.0f, juce::Font::bold));
    mixLabel->setJustificationType(juce::Justification::centred);
    mixLabel->setEditable(false, false, false);
    mixLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    
    // Decrease height while keeping width the same
    setSize(900, 250);
    
    // Enable mouse events for tooltips
    setInterceptsMouseClicks(false, true);
    
    // Set all components to report mouse events to this component
    for (auto* slider : { &thresholdKnob, &releaseKnob, &rangeKnob, &outputKnob, &mixKnob, &gainReductionSlider }) {
        slider->setInterceptsMouseClicks(true, true);
    }
    
    for (auto* comboBox : { &detectionToggle, &modeToggle, &detectorToggle }) {
        comboBox->setInterceptsMouseClicks(true, true);
    }
}

DynamiteComponent::~DynamiteComponent()
{
    // Stop timer first to prevent any callbacks during destruction
    stopTimer();
    
    // Clear all attachments before destroying the controls
    // Important: null these in the correct order - first attachments, then components
    mixKnobAttachment = nullptr;
    thresholdAttachment = nullptr;
    releaseAttachment = nullptr;
    rangeAttachment = nullptr;
    outputAttachment = nullptr;
    detectionToggleAttachment = nullptr;
    modeToggleAttachment = nullptr;
    detectorToggleAttachment = nullptr;
    gainReductionAttachment = nullptr;
    
    // Now destroy the UI components after their attachments
    mixLabel = nullptr;
    
    // Remove lookAndFeel
    thresholdKnob.setLookAndFeel(nullptr);
    releaseKnob.setLookAndFeel(nullptr);
    rangeKnob.setLookAndFeel(nullptr);
    outputKnob.setLookAndFeel(nullptr);
    mixKnob.setLookAndFeel(nullptr);
}

void DynamiteComponent::paint(juce::Graphics& g)
{
    // Draw main background
    drawBackground(g);
    
    // Draw plugin title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 18.0f, juce::Font::bold)); // Smaller title
    g.drawText("dyna-mite", getLocalBounds().removeFromTop(25), juce::Justification::centred); // Reduced top area
    
    // Draw knob labels
    auto knobArea = getLocalBounds().reduced(20).withTrimmedTop(25).withTrimmedBottom(60); // Reduced top margin
    float knobWidth = knobArea.getWidth() / 5.0f;
    
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 9.0f, juce::Font::bold)); // Smaller font
    
    // Draw each knob label
    g.drawText("THRESHOLD", knobArea.removeFromLeft(knobWidth).removeFromTop(14), juce::Justification::centred); // Less height
    g.drawText("RELEASE", knobArea.removeFromLeft(knobWidth).removeFromTop(14), juce::Justification::centred);
    g.drawText("RANGE", knobArea.removeFromLeft(knobWidth).removeFromTop(14), juce::Justification::centred);
    g.drawText("GAIN REDUCTION", knobArea.removeFromLeft(knobWidth).removeFromTop(14), juce::Justification::centred);
    g.drawText("OUTPUT", knobArea.removeFromLeft(knobWidth).removeFromTop(14), juce::Justification::centred);
    
    // Reset knobArea for the gain reduction display
    knobArea = getLocalBounds().reduced(20).withTrimmedTop(25); // Reduced top margin
    knobWidth = knobArea.getWidth() / 5.0f;
    
    // Skip the threshold, release, and range knobs
    knobArea.removeFromLeft(knobWidth * 3);
    
    // Draw gain reduction display
    auto grArea = knobArea.removeFromLeft(knobWidth).reduced(6);
    
    // Adjust top margin to account for the label we already drew
    grArea.removeFromTop(14); // Less height
    
    // Draw gain reduction meter
    drawGainReductionMeter(g, grArea, audioProcessor.getCurrentGainReduction());
    
    // Draw toggle labels above the toggle switches
    auto switchArea = getLocalBounds().removeFromBottom(60);
    float switchWidth = switchArea.getWidth() / 3.0f;
    
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 9.0f, juce::Font::plain)); // Smaller font
    
    // Draw detection and mode toggle labels
    g.drawText("DETECTION", switchArea.removeFromLeft(switchWidth).removeFromTop(16), juce::Justification::centred);
    g.drawText("MODE", switchArea.removeFromLeft(switchWidth).removeFromTop(16), juce::Justification::centred);
    
    // Draw detector toggle label - moved 70 pixels to the right (changed from 20)
    auto detectorLabelArea = switchArea.removeFromLeft(switchWidth).removeFromTop(16);
    g.drawText("DETECTOR", detectorLabelArea.translated(70, 0), juce::Justification::centred);
    
    // Draw hover tooltip if hovering
    if (isHovering && !hoverText.isEmpty()) {
        drawHoverTooltip(g);
    }
}

void DynamiteComponent::resized()
{
    // Layout constants
    const int toggleAreaHeight = 60;
    const int topMargin = 25;
    const int mainHeight = getHeight() - topMargin - toggleAreaHeight;
    const int knobRowHeight = mainHeight / 2; // Split main area into two rows
    
    // Position knobs - smaller area to reduce vertical space
    auto mainArea = getLocalBounds().reduced(20);
    mainArea.removeFromTop(topMargin); // Remove title area
    
    auto toggleArea = mainArea.removeFromBottom(toggleAreaHeight);
    
    // Calculate knob areas
    float knobWidth = mainArea.getWidth() / 5.0f;
    
    // Create two rows for knobs
    auto topRow = mainArea.removeFromTop(knobRowHeight);
    auto bottomRow = mainArea;
    
    // Calculate knob margin - 50% larger knobs means less margin
    int knobMargin = static_cast<int>(knobWidth * 0.075f);
    
    // Position knobs in the top row with larger size
    thresholdKnob.setBounds(topRow.removeFromLeft(knobWidth).reduced(knobMargin));
    releaseKnob.setBounds(topRow.removeFromLeft(knobWidth).reduced(knobMargin));
    rangeKnob.setBounds(topRow.removeFromLeft(knobWidth).reduced(knobMargin));
    
    // Position gain reduction meter
    gainReductionSlider.setBounds(topRow.removeFromLeft(knobWidth).reduced(knobMargin));
    
    // Position the outputKnob in the top row at the same height as other knobs
    outputKnob.setBounds(topRow.removeFromLeft(knobWidth).reduced(knobMargin));
    
    // Position Mix knob in the bottom row - make it 60% larger than standard knobs (increased from 30%)
    float mixKnobWidth = knobWidth * 1.6f;
    int mixKnobMargin = static_cast<int>(mixKnobWidth * 0.03f); // Reduced margin to maximize grab area
    
    // Center the larger mix knob in its area and allow more space around it
    auto mixArea = bottomRow.removeFromLeft(knobWidth * 1.5f); // Increased area width by 50%
    mixKnob.setBounds(mixArea.withSizeKeepingCentre(mixKnobWidth, mixArea.getHeight()).reduced(mixKnobMargin));
    
    // Position mix label 
    mixLabel->setBounds(mixKnob.getBounds().removeFromTop(14));
    
    // Position toggle switches at bottom
    float switchWidth = toggleArea.getWidth() / 3.0f;
    auto detectionArea = toggleArea.removeFromLeft(switchWidth);
    auto modeArea = toggleArea.removeFromLeft(switchWidth);
    auto detectorArea = toggleArea;
    
    // Calculate toggle size - decrease width and height by 50%
    auto toggleWidth = (switchWidth - 16) * 0.5f; // 50% reduction
    auto toggleHeight = 35 * 0.5f; // 50% reduction to height
    
    // Set the bounds of the detection and mode combo boxes - moved down by 20 pixels
    detectionToggle.setBounds(detectionArea.getCentreX() - toggleWidth/2, 
                             detectionArea.getCentreY() - toggleHeight/2 + 20, 
                             toggleWidth, toggleHeight);
    
    modeToggle.setBounds(modeArea.getCentreX() - toggleWidth/2, 
                        modeArea.getCentreY() - toggleHeight/2 + 20, 
                        toggleWidth, toggleHeight);
    
    // Move the detectorToggle 70 pixels to the right
    detectorToggle.setBounds(detectorArea.getCentreX() - toggleWidth/2 + 70, 
                            detectorArea.getCentreY() - toggleHeight/2 + 20, 
                            toggleWidth, toggleHeight);
}

void DynamiteComponent::setupControls()
{
    // Set up knobs
    for (auto* knob : { &thresholdKnob, &releaseKnob, &rangeKnob, &outputKnob, &mixKnob })
    {
        knob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 70, 0); // Smaller text box
        knob->setPopupDisplayEnabled(true, true, this);
        knob->setLookAndFeel(&knobStyle);
        addAndMakeVisible(knob);
    }
    
    // Configure knob ranges and labels
    thresholdKnob.setRange(-40.0, 0.0, 0.1);
    thresholdKnob.setTextValueSuffix(" dB");
    thresholdKnob.setName("THRESHOLD");
    thresholdKnob.setDoubleClickReturnValue(true, -20.0);
    thresholdKnob.setMouseDragSensitivity(100);
    
    releaseKnob.setRange(0.05, 5.0, 0.01);
    releaseKnob.setTextValueSuffix(" s/20dB");
    releaseKnob.setName("RELEASE");
    
    rangeKnob.setRange(0.0, 60.0, 0.1);
    rangeKnob.setTextValueSuffix(" dB");
    rangeKnob.setName("RANGE");
    
    outputKnob.setRange(-15.0, 40.0, 0.1);
    outputKnob.setTextValueSuffix(" dB");
    outputKnob.setName("OUTPUT");
    
    // Set up 3-position toggle switches with smaller size
    detectionToggle.addItem("INT", 1);
    detectionToggle.addItem("DS", 2);
    detectionToggle.addItem("EXT", 3);
    detectionToggle.setSelectedId(1);
    detectionToggle.setTooltip("Detection Mode: INT (Internal), DS (Direct Signal), EXT (External)");
    detectionToggle.setColour(juce::ComboBox::backgroundColourId, juce::Colour(40, 40, 40));
    detectionToggle.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    detectionToggle.setColour(juce::ComboBox::outlineColourId, juce::Colours::grey);
    detectionToggle.setJustificationType(juce::Justification::centred);
    detectionToggle.setTextWhenNothingSelected("DET.");
    detectionToggle.setTextWhenNoChoicesAvailable("DET.");
    addAndMakeVisible(detectionToggle);
    
    // Mode toggle with smaller size
    modeToggle.addItem("GR", 1);
    modeToggle.addItem("OUT", 2);
    modeToggle.addItem("LIMIT", 3);
    modeToggle.setSelectedId(1);
    modeToggle.setTooltip("Mode: GR (Gain Reduction), OUT (Output Only), LIMIT (Limiter)");
    modeToggle.setColour(juce::ComboBox::backgroundColourId, juce::Colour(40, 40, 40));
    modeToggle.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    modeToggle.setColour(juce::ComboBox::outlineColourId, juce::Colours::grey);
    modeToggle.setJustificationType(juce::Justification::centred);
    modeToggle.setTextWhenNothingSelected("MODE");
    modeToggle.setTextWhenNoChoicesAvailable("MODE");
    addAndMakeVisible(modeToggle);
    
    // Detector toggle with smaller size
    detectorToggle.addItem("GATE", 1);
    detectorToggle.addItem("PEAK", 2);
    detectorToggle.addItem("AVG", 3);
    detectorToggle.setSelectedId(2);
    detectorToggle.setTooltip("Detector Type: GATE, PEAK (Peak Detection), AVG (Average Detection)");
    detectorToggle.setColour(juce::ComboBox::backgroundColourId, juce::Colour(40, 40, 40));
    detectorToggle.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    detectorToggle.setColour(juce::ComboBox::outlineColourId, juce::Colours::grey);
    detectorToggle.setJustificationType(juce::Justification::centred);
    detectorToggle.setTextWhenNothingSelected("DET.");
    detectorToggle.setTextWhenNoChoicesAvailable("DET.");
    addAndMakeVisible(detectorToggle);
    
    // Configure gain reduction slider
    gainReductionSlider.setRange(-40.0, 0.0, 0.1);
    gainReductionSlider.setValue(0.0);
    gainReductionSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainReductionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 70, 0); // Smaller text box
    gainReductionSlider.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    gainReductionSlider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    gainReductionSlider.setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(gainReductionSlider);
}

void DynamiteComponent::setupMeters()
{
    // Use higher refresh rate for responsive metering
    startTimerHz(40); // 40 fps is sufficient for smooth VU meter animation
}

void DynamiteComponent::timerCallback()
{
    // Get current gain reduction from processor
    float currentReduction = audioProcessor.getCurrentGainReduction();
    
    // Force a minimum detectable level of gain reduction for testing
    // if (currentReduction > -0.1f && audioProcessor.parameters.getRawParameterValue("threshold") != nullptr)
    // {
    //     float threshold = *audioProcessor.parameters.getRawParameterValue("threshold");
    //     if (threshold < -1.0f) {
    //         // Simulate gain reduction for testing
    //         currentReduction = threshold / 2.0f; // Create some reduction based on threshold
    //     }
    // }
    
    // Debug logging (less frequent to avoid log spam)
    static int logCounter = 0;
    if (logCounter++ % 100 == 0) {
        juce::Logger::writeToLog("Gain Reduction: " + juce::String(currentReduction) + " dB");
    }
    
    // Force repaint of gain reduction display to ensure it updates
    repaint();
    
    // Check if threshold parameter should be updated (poll connection occasionally)
    static int thresholdCheckCounter = 0;
    if (thresholdCheckCounter++ % 50 == 0) { // Check less frequently than repaints
        // Get the actual parameter value from the processor
        auto* thresholdParameter = audioProcessor.parameters.getRawParameterValue("threshold");
        
        if (thresholdParameter != nullptr) {
            float processorThreshold = *thresholdParameter;
            
            // If there's a significant mismatch, force a parameter refresh
            if (std::abs(processorThreshold - thresholdKnob.getValue()) > 0.1f) {
                juce::Logger::writeToLog("Detected threshold mismatch - Processor: " + 
                                        juce::String(processorThreshold) + " dB, UI: " + 
                                        juce::String(thresholdKnob.getValue()) + " dB - forcing refresh");
                
                // Temporarily detach and reattach to force a refresh
                thresholdAttachment = nullptr;
                thresholdAttachment.reset(new SliderAttachment(audioProcessor.parameters, "threshold", thresholdKnob));
            }
        }
    }
    
    // Trigger repaint only if reduction has changed significantly
    static float lastReduction = 0.0f;
    if (std::abs(currentReduction - lastReduction) > 0.05f) {
        lastReduction = currentReduction;
        repaint();
    }
}

void DynamiteComponent::drawBackground(juce::Graphics& g)
{
    // Draw the dark background
    g.fillAll(juce::Colour(40, 40, 40));
    
    // Draw wooden frame
    g.setColour(juce::Colour(120, 80, 40));
    g.drawRect(getLocalBounds(), 5);
    
    // Draw inner panel
    auto innerBounds = getLocalBounds().reduced(10);
    g.setColour(juce::Colour(20, 20, 20));
    g.fillRect(innerBounds);
}

void DynamiteComponent::drawKnob(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                             const juce::String& label, float value, float minVal, float maxVal)
{
    // Draw label
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 15.0f, juce::Font::plain));
    g.drawText(label, bounds.withHeight(20.0f), juce::Justification::centred);
    
    // Calculate knob position and size
    auto knobBounds = bounds.withTrimmedTop(25.0f);
    
    // Draw knob background
    g.setColour(juce::Colour(60, 60, 60));
    g.fillEllipse(knobBounds.reduced(10.0f));
    
    // Draw knob indicator
    g.setColour(juce::Colours::lightgrey);
    
    float normalizedValue = (value - minVal) / (maxVal - minVal);
    float angle = juce::MathConstants<float>::pi * 1.5f - normalizedValue * juce::MathConstants<float>::pi * 1.8f;
    
    juce::Path p;
    float radius = knobBounds.getWidth() / 2.0f - 15.0f;
    juce::Point<float> center(knobBounds.getCentreX(), knobBounds.getCentreY());
    p.addLineSegment(juce::Line<float>(center, center.getPointOnCircumference(radius, angle)), 2.0f);
    
    g.setColour(juce::Colours::white);
    g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    
    // Draw knob cap
    g.setColour(juce::Colours::silver);
    g.fillEllipse(knobBounds.reduced(20.0f));
    
    // Draw value
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 12.0f, juce::Font::plain));
    
    juce::String valueText;
    if (label == "THRESHOLD" || label == "RANGE" || label == "OUTPUT")
        valueText = juce::String(value, 1) + " dB";
    else if (label == "RELEASE")
        valueText = juce::String(value, 2) + " s";
    
    g.drawText(valueText, knobBounds.reduced(5.0f), juce::Justification::centred);
}

void DynamiteComponent::drawLED(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                            bool isOn, const juce::Colour& onColor)
{
    // Draw LED background (perfectly circular)
    g.setColour(juce::Colour(30, 30, 30));
    g.fillEllipse(bounds);
    
    // Draw LED light (perfectly circular)
    if (isOn)
    {
        // Draw glow effect
        g.setColour(onColor.withAlpha(0.3f));
        g.fillEllipse(bounds.expanded(2.0f));
        
        // Draw main LED color
        g.setColour(onColor);
        g.fillEllipse(bounds.reduced(1.0f));
        
        // Draw LED highlight (slight shine effect on top)
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        auto highlightBounds = bounds.reduced(bounds.getWidth() * 0.3f)
                                    .withTrimmedBottom(bounds.getHeight() * 0.7f);
        g.fillEllipse(highlightBounds);
    }
    else
    {
        // Draw dimmed LED when off
        g.setColour(onColor.withAlpha(0.1f));
        g.fillEllipse(bounds.reduced(1.0f));
    }
}

void DynamiteComponent::drawToggleSwitch(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                                     const juce::StringArray& options, int position)
{
    // Draw switch background
    g.setColour(juce::Colour(60, 60, 60));
    g.fillRoundedRectangle(bounds, 4.0f);
    
    // Draw border
    g.setColour(juce::Colours::darkgrey);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    
    // Calculate switch positions
    float segmentHeight = bounds.getHeight() / options.size();
    
    // Draw switch segments
    for (int i = 0; i < options.size(); i++)
    {
        bool isSelected = (i == position);
        
        auto segmentBounds = bounds.withHeight(segmentHeight)
                                   .withY(bounds.getY() + i * segmentHeight);
        
        g.setColour(isSelected ? juce::Colours::orange : juce::Colours::darkgrey);
        g.fillRoundedRectangle(segmentBounds.reduced(2.0f), 3.0f);
        
        // Draw label
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 12.0f * 0.65f, juce::Font::plain)); // Reduce font size by 35%
        g.drawText(options[i], segmentBounds.reduced(4.0f), juce::Justification::centred);
    }
}

void DynamiteComponent::drawGainReductionMeter(juce::Graphics& g, const juce::Rectangle<int>& bounds, float gainReduction)
{
    // Create a mutable copy of bounds to work with
    auto meterBounds = bounds;
    
    // Ensure gain reduction is negative (or zero) and within display range
    gainReduction = juce::jlimit(-40.0f, 0.0f, gainReduction);
    
    // Create meter with 10 LEDs
    const int numLeds = 10;
    float ledHeight = meterBounds.getHeight() / static_cast<float>(numLeds);
    
    // Define the dB thresholds for each LED, from least to most reduction (top to bottom)
    const float reductionThresholds[] = {
        -1.0f, -3.0f, -6.0f, -10.0f, -15.0f,
        -20.0f, -25.0f, -30.0f, -35.0f, -40.0f
    };
    
    // Draw each LED
    for (int i = 0; i < numLeds; ++i)
    {
        auto ledBounds = meterBounds.removeFromTop(ledHeight).reduced(2);
        
        // Calculate if LED should be on (gainReduction should be at or more than threshold)
        bool isOn = (gainReduction <= reductionThresholds[i]);
        
        // Choose color based on amount of reduction
        juce::Colour ledColor;
        if (i < 3) {
            // Green for light reduction (top LEDs)
            ledColor = isOn ? juce::Colour(20, 230, 20) : juce::Colour(20, 70, 20);
        } else if (i < 6) {
            // Yellow/orange for medium reduction (middle LEDs)
            ledColor = isOn ? juce::Colour(230, 180, 0) : juce::Colour(70, 60, 0);
        } else {
            // Red for heavy reduction (bottom LEDs)
            ledColor = isOn ? juce::Colour(230, 20, 20) : juce::Colour(70, 20, 20);
        }
        
        // Draw LED label - reduce font size by 50%
        auto textArea = ledBounds.removeFromRight(20);
        g.setColour(isOn ? juce::Colours::white : juce::Colours::darkgrey);
        g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 5.0f, juce::Font::plain)); // Reduced from 10.0f to 5.0f
        g.drawText(juce::String(reductionThresholds[i]), textArea, juce::Justification::centredRight);
        
        // Draw LED
        drawLED(g, ledBounds.toFloat().withSizeKeepingCentre(ledHeight * 0.7f, ledHeight * 0.7f), 
               isOn, ledColor);
    }
    
    // Draw the numerical value of the gain reduction below the meter - reduce font size by 50%
    auto valueArea = meterBounds.removeFromTop(20);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultSansSerifFontName(), 7.0f, juce::Font::bold)); // Reduced from 14.0f to 7.0f
    
    // Only display non-zero gain reduction
    if (gainReduction < -0.1f) {
        g.drawText(juce::String(gainReduction, 1) + " dB", 
                  valueArea, juce::Justification::centred);
    } else {
        g.drawText("0 dB", valueArea, juce::Justification::centred);
    }
}

// Mouse event handlers for tooltips
void DynamiteComponent::mouseMove(const juce::MouseEvent& event)
{
    // Find component under mouse
    auto* component = getComponentAt(event.getPosition());
    
    if (component != nullptr && component != this) {
        hoveredComponent = component;
        hoverText = getComponentName(component);
        isHovering = !hoverText.isEmpty();
        hoverArea = component->getBounds();
        repaint();
    } else {
        if (isHovering) {
            isHovering = false;
            hoveredComponent = nullptr;
            hoverText = "";
            repaint();
        }
    }
}

void DynamiteComponent::mouseExit(const juce::MouseEvent& event)
{
    isHovering = false;
    hoveredComponent = nullptr;
    hoverText = "";
    repaint();
}

juce::String DynamiteComponent::getComponentName(juce::Component* component)
{
    juce::String name;
    
    if (component == &thresholdKnob) 
        name = "Threshold";
    else if (component == &releaseKnob) 
        name = "Release";
    else if (component == &rangeKnob) 
        name = "Range";
    else if (component == &outputKnob) 
        name = "Output";
    else if (component == &mixKnob) 
        name = "Mix";
    else if (component == &gainReductionSlider) 
        name = "Gain Reduction";
    else if (component == &detectionToggle) 
        name = "Detection Mode";
    else if (component == &modeToggle) 
        name = "Mode";
    else if (component == &detectorToggle) 
        name = "Detector Type";
    
    return name;
}

void DynamiteComponent::drawHoverTooltip(juce::Graphics& g)
{
    if (hoveredComponent == nullptr || hoverText.isEmpty())
        return;
    
    // Create tooltip area above the component
    auto tooltipBounds = hoverArea.translated(0, -30);
    tooltipBounds.setHeight(25);
    
    // Background for tooltip
    g.setColour(juce::Colour(20, 20, 20).withAlpha(0.9f));
    g.fillRoundedRectangle(tooltipBounds.toFloat(), 4.0f);
    g.setColour(juce::Colours::grey);
    g.drawRoundedRectangle(tooltipBounds.toFloat(), 4.0f, 1.0f);
    
    // Text in red
    g.setColour(juce::Colours::red);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(hoverText, tooltipBounds, juce::Justification::centred, false);
    
    // Add parameter value if it's a slider
    juce::String valueText;
    
    if (auto* slider = dynamic_cast<juce::Slider*>(hoveredComponent)) {
        valueText = juce::String(slider->getValue(), 1);
        
        if (slider == &thresholdKnob || slider == &rangeKnob || slider == &outputKnob) {
            valueText += " dB";
        } else if (slider == &releaseKnob) {
            valueText += " s";
        } else if (slider == &mixKnob) {
            valueText = juce::String(slider->getValue() * 100.0, 0) + "%";
        }
        
        if (!valueText.isEmpty()) {
            // Create a separate area for the value text below the name
            auto valueBounds = tooltipBounds.translated(0, tooltipBounds.getHeight());
            
            // Background for value
            g.setColour(juce::Colour(20, 20, 20).withAlpha(0.9f));
            g.fillRoundedRectangle(valueBounds.toFloat(), 4.0f);
            g.setColour(juce::Colours::grey);
            g.drawRoundedRectangle(valueBounds.toFloat(), 4.0f, 1.0f);
            
            // Draw value text in white
            g.setColour(juce::Colours::white);
            g.drawText(valueText, valueBounds, juce::Justification::centred, false);
        }
    }
}

void DynamiteComponent::sliderValueChanged(juce::Slider* slider)
{
    // Handle mix knob changes explicitly
    if (slider == &mixKnob) {
        // Ensure the processor is updated with the current mix value
        audioProcessor.setMixLevel(static_cast<float>(mixKnob.getValue()));
    }
} 