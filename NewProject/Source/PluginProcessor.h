/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DynamiteProcessor.h"

//==============================================================================
/**
*/
class NewProjectAudioProcessor : public juce::AudioProcessor,
                                 public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockDouble (juce::AudioBuffer<double>&, juce::MidiBuffer&);
    bool supportsDoublePrecisionProcessing();

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Get the current gain reduction value
    float getCurrentGainReduction() const { return compressor.getCurrentGainReduction(); }
    
    // Set the mix level (dry/wet balance)
    void setMixLevel(float newMixValue);

    // Access to parameters - making this public to allow UI access
    juce::AudioProcessorValueTreeState parameters;

    // Callback for parameter changes
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    // Compressor instance
    DynamiteProcessor compressor;
    
    // Double precision buffer
    juce::AudioBuffer<double> doubleBuffer;
    
    // Parameter pointers for real-time access
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* detectionModeParam = nullptr;
    std::atomic<float>* modeModeParam = nullptr;
    std::atomic<float>* detectorTypeParam = nullptr;
    std::atomic<float>* rangeParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* gainReductionParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<bool>* autoMakeupParam = nullptr;
    
    // Flag to prevent concurrent processing that could cause crashes
    std::atomic<bool> isProcessing{false};
    
    // DeEsser state
    struct DeEsserState
    {
        juce::dsp::IIR::Filter<float> filter;
        double lastSampleRate = 0.0;
        std::atomic<bool> needsUpdate{true};
        float currentFreq = 6000.0f;
        float currentQ = 0.707f;
        float currentGain = -12.0f;
    };
    
    DeEsserState deesserState;
    juce::CriticalSection deesserLock;
    
    // Update the DeEsser filter coefficients if needed
    void updateDeEsserIfNeeded();
    
    // Update all compressor settings from parameters
    void updateCompressorSettings();
    
    // Calculate auto makeup gain based on parameters
    float calculateAutoMakeupGain();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};

//==============================================================================
class DynamiteAudioProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    DynamiteAudioProcessor();
    ~DynamiteAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockDouble (juce::AudioBuffer<double>&, juce::MidiBuffer&);

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Access to parameters
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    float getCurrentGainReduction() const { return compressor.getCurrentGainReduction(); }

private:
    // Parameter handling
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateCompressorSettings();
    void updateDeEsserIfNeeded();
    float calculateAutoMakeupGain();

    // Parameter definitions
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* detectionModeParam = nullptr;
    std::atomic<float>* modeModeParam = nullptr;
    std::atomic<float>* sidechainParam = nullptr;
    std::atomic<float>* rangeParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* gainReductionParam = nullptr;
    std::atomic<float>* detectorTypeParam = nullptr;
    std::atomic<float>* limitModeParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<bool>* autoMakeupParam = nullptr;

    // Compressor processing
    DynamiteProcessor compressor;
    juce::AudioBuffer<double> doubleBuffer;

    // Double-precision processing
    bool supportsDoublePrecisionProcessing();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamiteAudioProcessor)
};
