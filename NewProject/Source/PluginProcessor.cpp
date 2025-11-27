/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// Parameter IDs
const juce::String thresholdID = "threshold";
const juce::String releaseID = "release";
const juce::String detectionModeID = "detection_mode";
const juce::String modeModeID = "mode_mode";
const juce::String detectorTypeID = "detector_type";
const juce::String rangeID = "range";
const juce::String outputID = "output";
const juce::String gainReductionID = "gain_reduction";

// Parameter defaults from the hardware
const float defaultThreshold = -10.0f;    // dBV
const float defaultRelease = 0.15f;       // seconds (150ms - typical 1176 "3" position)
const int defaultGainReduction = 3;       // Index (6dB)
const float defaultRange = 20.0f;         // dB
const float defaultOutput = 0.0f;         // dB
const int defaultDetectionMode = 0;       // INT (Internal)
const int defaultModeMode = 0;            // GR (Gain Reduction - normal compressor operation)
const int defaultDetectorType = 1;        // PEAK (peak detection)

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       parameters(*this, nullptr, juce::Identifier("DynamiteCompressor"), {
           std::make_unique<juce::AudioParameterFloat>(thresholdID, "Threshold", -60.0f, 0.0f, -20.0f),
           std::make_unique<juce::AudioParameterFloat>("ratio", "Ratio", 1.0f, 20.0f, 4.0f),
           std::make_unique<juce::AudioParameterFloat>("attack", "Attack", 0.1f, 50.0f, 1.0f),
           std::make_unique<juce::AudioParameterFloat>(releaseID, "Release", 20.0f, 2000.0f, 150.0f),
           std::make_unique<juce::AudioParameterFloat>("makeup", "Make-up", 0.0f, 40.0f, 0.0f),
           std::make_unique<juce::AudioParameterFloat>("gateThreshold", "Gate", -60.0f, 0.0f, -40.0f),
           std::make_unique<juce::AudioParameterFloat>("gateRelease", "Gate Release", 1.0f, 100.0f, 10.0f),
           std::make_unique<juce::AudioParameterFloat>("deesserFreq", "DeEsser Freq", 2000.0f, 8000.0f, 6000.0f),
           std::make_unique<juce::AudioParameterFloat>("deesserThreshold", "DeEsser Thresh", -60.0f, 0.0f, -20.0f),
           std::make_unique<juce::AudioParameterFloat>("deesserRatio", "DeEsser Ratio", 1.0f, 20.0f, 4.0f),
           std::make_unique<juce::AudioParameterBool>("externalSidechain", "Ext", false),
           std::make_unique<juce::AudioParameterChoice>(detectionModeID, "Detection Mode", juce::StringArray{"INT", "DS", "EXT"}, defaultDetectionMode),
           std::make_unique<juce::AudioParameterChoice>(modeModeID, "Mode", juce::StringArray{"GR", "OUT", "LIMIT"}, defaultModeMode),
           std::make_unique<juce::AudioParameterChoice>(detectorTypeID, "Detector Type", juce::StringArray{"GATE", "PEAK", "AVG"}, defaultDetectorType),
           std::make_unique<juce::AudioParameterFloat>(rangeID, "Range", 0.0f, 60.0f, defaultRange),
           std::make_unique<juce::AudioParameterFloat>(outputID, "Output", -15.0f, 40.0f, defaultOutput),
           std::make_unique<juce::AudioParameterInt>(gainReductionID, "Gain Reduction", 1, 8, defaultGainReduction),
           std::make_unique<juce::AudioParameterBool>("autoMakeup", "Auto Makeup", false),
           std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f)
       })
#endif
{
    // Get parameter pointers for real-time access with error checking
    thresholdParam = parameters.getRawParameterValue(thresholdID);
    releaseParam = parameters.getRawParameterValue(releaseID);
    detectionModeParam = parameters.getRawParameterValue(detectionModeID);
    modeModeParam = parameters.getRawParameterValue(modeModeID);
    detectorTypeParam = parameters.getRawParameterValue(detectorTypeID);
    rangeParam = parameters.getRawParameterValue(rangeID);
    outputParam = parameters.getRawParameterValue(outputID);
    gainReductionParam = parameters.getRawParameterValue(gainReductionID);
    ratioParam = parameters.getRawParameterValue("ratio");
    auto* rawAutoMakeup = parameters.getRawParameterValue("autoMakeup");
    autoMakeupParam = reinterpret_cast<std::atomic<bool>*>(rawAutoMakeup);

    // Verify all parameters were found
    if (!thresholdParam || !releaseParam || !detectionModeParam || !modeModeParam || 
        !detectorTypeParam || !rangeParam || !outputParam || !gainReductionParam || !ratioParam || !autoMakeupParam)
    {
        juce::Logger::writeToLog("Error: Failed to initialize parameters");
        return;
    }

    // Register for parameter changes
    parameters.addParameterListener(thresholdID, this);
    parameters.addParameterListener(releaseID, this);
    parameters.addParameterListener(detectionModeID, this);
    parameters.addParameterListener(modeModeID, this);
    parameters.addParameterListener(detectorTypeID, this);
    parameters.addParameterListener(rangeID, this);
    parameters.addParameterListener(outputID, this);
    parameters.addParameterListener(gainReductionID, this);

    // Set initial compressor state
    updateCompressorSettings();

    // Initialize DeEsser filter with default sample rate
    juce::dsp::ProcessSpec spec{44100.0f, static_cast<juce::uint32>(44100.0f), 1};
    deesserState.filter.prepare(spec);
    auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(44100.0f, 6000.0f, 0.707f, 10.0f);
    if (coefficients != nullptr)
    {
        deesserState.filter.coefficients = coefficients;
    }
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
    // Unregister parameter listeners
    parameters.removeParameterListener(thresholdID, this);
    parameters.removeParameterListener(releaseID, this);
    parameters.removeParameterListener(detectionModeID, this);
    parameters.removeParameterListener(modeModeID, this);
    parameters.removeParameterListener(detectorTypeID, this);
    parameters.removeParameterListener(rangeID, this);
    parameters.removeParameterListener(outputID, this);
    parameters.removeParameterListener(gainReductionID, this);
}

//==============================================================================
const juce::String NewProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NewProjectAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NewProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NewProjectAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NewProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NewProjectAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NewProjectAudioProcessor::getProgramName (int index)
{
    return {};
}

void NewProjectAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Configure DSP processing
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumInputChannels());
    
    // Prepare the compressor
    compressor.prepare(spec);
    
    // Initialize double buffer for double precision processing
    doubleBuffer.setSize(getTotalNumInputChannels(), samplesPerBlock);
    
    // Prepare DeEsser filter
    const juce::ScopedLock sl(deesserLock);
    deesserState.filter.prepare(spec);
    deesserState.lastSampleRate = sampleRate;
    deesserState.needsUpdate = true;
    updateDeEsserIfNeeded();
}

void NewProjectAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    compressor.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Add timestamp to log for tracking
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // Log the start of processing with buffer details
    juce::Logger::writeToLog(timestamp + " - ProcessBlock called - isProcessing: " + 
                            juce::String(isProcessing.load() ? "true" : "false") + 
                            ", Buffer: " + juce::String(buffer.getNumChannels()) + " channels, " + 
                            juce::String(buffer.getNumSamples()) + " samples");
    
    // CRITICAL: Check buffer validity before attempting to process
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) {
        juce::Logger::writeToLog(timestamp + " - ERROR: Empty buffer received");
        isProcessing = false;
        return;
    }
    
    // Set processing flag with more detailed logging
    bool wasAlreadyProcessing = isProcessing.exchange(true);
    if (wasAlreadyProcessing)
    {
        // Another thread is already processing, skip this buffer
        juce::Logger::writeToLog(timestamp + " - CRITICAL: Concurrent processing detected, skipping this buffer");
        isProcessing = false; // Reset flag and return without clearing buffer
        return;
    }
    
    // Calculate input level before any processing
    float inputLevel = buffer.getMagnitude(0, 0, buffer.getNumSamples());
    juce::Logger::writeToLog(timestamp + " - Input level: " + juce::String(inputLevel) + 
                           " (" + juce::String(20.0f * std::log10(inputLevel + 1.0e-6f)) + " dB)");
    
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();
    
    // CRITICAL: Ensure we have at least one input and output channel
    if (totalNumInputChannels == 0 || totalNumOutputChannels == 0) {
        juce::Logger::writeToLog(timestamp + " - ERROR: No input or output channels available");
        isProcessing = false;
        return;
    }
    
    // Clear any unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);
    
    try {
        // Log compressor settings update
        juce::Logger::writeToLog(timestamp + " - Updating compressor settings");
        
        // Update settings in case any parameters have changed
        updateCompressorSettings();
        
        // Convert float buffer to double for processing
        if (doubleBuffer.getNumChannels() != totalNumInputChannels || doubleBuffer.getNumSamples() != numSamples)
        {
            juce::Logger::writeToLog(timestamp + " - Resizing double buffer to " + 
                                    juce::String(totalNumInputChannels) + " channels, " + 
                                    juce::String(numSamples) + " samples");
                                    
            doubleBuffer.setSize(totalNumInputChannels, numSamples, false, false, true);
            if (doubleBuffer.getNumChannels() == 0 || doubleBuffer.getNumSamples() == 0)
            {
                juce::Logger::writeToLog(timestamp + " - CRITICAL: Failed to allocate double buffer");
                isProcessing = false;
                return;
            }
        }
        
        // Copy input to double buffer
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* floatData = buffer.getReadPointer(channel);
            auto* doubleData = doubleBuffer.getWritePointer(channel);
            
            if (floatData == nullptr || doubleData == nullptr)
            {
                juce::Logger::writeToLog(timestamp + " - CRITICAL: Null buffer pointers");
                isProcessing = false;
                return;
            }
            
            for (int sample = 0; sample < numSamples; ++sample)
                doubleData[sample] = static_cast<double>(floatData[sample]);
        }
        
        // Process through compressor
        juce::Logger::writeToLog(timestamp + " - Processing through compressor");
        compressor.process(doubleBuffer);
        
        // Copy processed double buffer back to float buffer
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* floatData = buffer.getWritePointer(channel);
            auto* doubleData = doubleBuffer.getReadPointer(channel);
            
            if (floatData == nullptr || doubleData == nullptr)
            {
                juce::Logger::writeToLog(timestamp + " - CRITICAL: Null buffer pointers");
                isProcessing = false;
                return;
            }
            
            // Copy data back with explicit conversion
            for (int sample = 0; sample < numSamples; ++sample) {
                floatData[sample] = static_cast<float>(doubleData[sample]);
            }
        }
        
        // Calculate output level after processing
        float outputLevel = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        juce::Logger::writeToLog(timestamp + " - Output level: " + juce::String(outputLevel) + 
                               " (" + juce::String(20.0f * std::log10(outputLevel + 1.0e-6f)) + " dB)");
        
        // EMERGENCY SANITY CHECK: If output is completely silent but input wasn't, just pass through the original audio
        if (outputLevel < 1.0e-10f && inputLevel > 1.0e-10f) {
            juce::Logger::writeToLog(timestamp + " - CRITICAL: Output is silent but input wasn't! Forcing audio through");
            // Copy original audio back
            for (int channel = 0; channel < totalNumInputChannels; ++channel) {
                auto* floatData = buffer.getWritePointer(channel);
                auto* originalData = buffer.getReadPointer(channel);
                if (floatData != nullptr && originalData != nullptr) {
                    for (int sample = 0; sample < numSamples; ++sample) {
                        floatData[sample] = originalData[sample];
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog(timestamp + " - CRITICAL: Exception in processBlock: " + juce::String(e.what()));
        // Don't clear buffer on exception - keep original audio
    }
    
    // Reset processing flag - CRITICAL!
    bool stillProcessing = isProcessing.exchange(false);
    if (!stillProcessing) {
        juce::Logger::writeToLog(timestamp + " - WARNING: isProcessing was already false before reset!");
    }
    
    juce::Logger::writeToLog(timestamp + " - ProcessBlock complete - isProcessing reset to false");
}

void NewProjectAudioProcessor::updateDeEsserIfNeeded()
{
    if (deesserState.needsUpdate.exchange(false))
    {
        const juce::ScopedLock sl(deesserLock);
        
        try {
            if (deesserState.lastSampleRate > 0.0)
            {
                // Use narrower Q for more focused frequency targeting
                deesserState.currentQ = 2.5f;  // Narrower Q for more precise frequency targeting
                
                // More aggressive gain reduction for the filter
                deesserState.currentGain = -24.0f;  // Deeper cut
                
                auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    static_cast<float>(deesserState.lastSampleRate),
                    deesserState.currentFreq,
                    deesserState.currentQ,
                    deesserState.currentGain);
                    
                if (coefficients != nullptr)
                {
                    deesserState.filter.coefficients = coefficients;
                }
                else
                {
                    juce::Logger::writeToLog("Error: Failed to create DeEsser filter coefficients");
                }
            }
            else
            {
                juce::Logger::writeToLog("Error: Invalid sample rate");
            }
        }
        catch (const std::exception& e) {
            juce::Logger::writeToLog("Error updating DeEsser: " + juce::String(e.what()));
        }
    }
}

void NewProjectAudioProcessor::processBlockDouble(juce::AudioBuffer<double>& buffer, 
                                                 juce::MidiBuffer& midiMessages)
{
    // Direct processing in double precision
    compressor.process(buffer);
}

bool NewProjectAudioProcessor::supportsDoublePrecisionProcessing()
{
    return isUsingDoublePrecision() && 
           !isNonRealtime() && 
           getTotalNumInputChannels() == getTotalNumOutputChannels();
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Save all parameters to XML
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameters from XML
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
void NewProjectAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == thresholdID || parameterID == releaseID || 
        parameterID == detectionModeID || parameterID == modeModeID ||
        parameterID == detectorTypeID || parameterID == rangeID || 
        parameterID == outputID || parameterID == gainReductionID)
    {
        updateCompressorSettings();
    }
    else if (parameterID == "attack")
    {
        // Just update settings, there's no direct setAttack method
        updateCompressorSettings();
    }
    else if (parameterID == "makeup")
    {
        // Convert to dB since setOutput takes dB value
        compressor.setOutput(newValue);
    }
    else if (parameterID == "ratio")
    {
        // Just update settings, there's no direct setRatio method
        updateCompressorSettings();
    }
    else if (parameterID == "gateThreshold")
    {
        compressor.setGateThreshold(newValue);
    }
    else if (parameterID == "gateRelease")
    {
        compressor.setGateRelease(newValue);
    }
    else if (parameterID == "deesserFreq")
    {
        // No direct access to deesser frequency setter
        updateCompressorSettings();
    }
    else if (parameterID == "deesserThreshold")
    {
        compressor.setDeesserThreshold(newValue);
    }
    else if (parameterID == "deesserRatio")
    {
        compressor.setDeesserRatio(newValue);
    }
    else if (parameterID == "mix")
    {
        compressor.setMix(newValue);
    }
}

void NewProjectAudioProcessor::updateCompressorSettings()
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Updating compressor settings");
    
    // Apply all parameter values to the compressor
    float thresholdValue = thresholdParam->load();
    compressor.setThreshold(thresholdValue);
    juce::Logger::writeToLog(timestamp + " - Set threshold: " + juce::String(thresholdValue) + " dB");
    
    // Apply non-linear scaling to release time for better control distribution
    float releaseMs = releaseParam->load();
    float scaledRelease = std::pow(releaseMs / 20.0f, 1.5f) * 20.0f;
    compressor.setRelease(scaledRelease);
    juce::Logger::writeToLog(timestamp + " - Set release: " + juce::String(releaseMs) + " ms (scaled: " + juce::String(scaledRelease) + " ms)");
    
    // Calculate and apply auto makeup gain
    float makeupGain = calculateAutoMakeupGain();
    float outputValue = outputParam->load();
    float finalGain = outputValue + (autoMakeupParam->load() ? makeupGain : 0.0f);
    compressor.setOutput(finalGain);
    juce::Logger::writeToLog(timestamp + " - Set output gain: " + juce::String(finalGain) + " dB (base: " + 
                            juce::String(outputValue) + " dB, auto makeup: " + 
                            juce::String(autoMakeupParam->load() ? makeupGain : 0.0f) + " dB)");
    
    // Handle detection mode (INT/DS/EXT) - 3-way toggle
    int detectionMode = static_cast<int>(detectionModeParam->load());
    bool isExternal = (detectionMode == 2); // EXT position
    bool isDSMode = (detectionMode == 1);   // DS position
    compressor.setDetectionMode(isExternal, isDSMode);
    
    // Handle mode (GR/OUT/LIMIT) - 3-way toggle
    int modeMode = static_cast<int>(modeModeParam->load());
    bool isLimitMode = (modeMode == 2);     // LIMIT position
    bool isOutputMode = (modeMode == 1);    // OUT position
    compressor.setProcessingMode(isLimitMode, isOutputMode);
    
    // Handle detector type (GATE/PEAK/AVG) - 3-way toggle
    int detectorType = static_cast<int>(detectorTypeParam->load());
    bool isAvgDetector = (detectorType == 2);   // AVG position
    bool isGateDetector = (detectorType == 0);  // GATE position
    compressor.setDetectorType(isAvgDetector, isGateDetector);
    
    compressor.setRange(rangeParam->load());
    compressor.setGainReduction(static_cast<int>(gainReductionParam->load()));
}

float NewProjectAudioProcessor::calculateAutoMakeupGain()
{
    if (!thresholdParam || !ratioParam || !autoMakeupParam)
        return 0.0f;

    if (!autoMakeupParam->load())
        return 0.0f;

    float threshold = thresholdParam->load();
    float ratio = ratioParam->load();
    
    // Calculate makeup gain to compensate for the reduction
    // Use a more aggressive compensation curve
    float makeupGain = -threshold * (1.0f - 1.0f/ratio) * 1.2f;
    
    // Add additional compensation for higher ratios
    if (ratio > 4.0f) {
        makeupGain *= 1.0f + (ratio - 4.0f) / 8.0f;
    }
    
    // Ensure the gain stays within reasonable limits
    return std::min(std::max(makeupGain, 0.0f), 40.0f);
}

//==============================================================================
// Add a method to update the mix level
void NewProjectAudioProcessor::setMixLevel(float newMixValue)
{
    // Directly update the compressor's mix parameter
    compressor.setMix(newMixValue);
    
    // Also update the parameter in the APVTS if it exists
    if (auto* mixParam = parameters.getParameter("mix"))
    {
        float normValue = mixParam->convertTo0to1(newMixValue);
        mixParam->setValueNotifyingHost(normValue);
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
