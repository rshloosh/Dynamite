/*
  ==============================================================================

    PluginProcessor.h

    JUCE AudioProcessor wrapper around the real-time-safe DynamiteEngine.
    Owns the APVTS parameter tree and delegates all DSP to the engine.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DynamiteEngine.h"

//==============================================================================
class NewProjectAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

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

    //==============================================================================
    // Metering accessors for the editor (GUI thread reads engine atomics).
    [[nodiscard]] float getCurrentGainReduction() const { return engine.getGainReductionDb(); }
    [[nodiscard]] bool  getOverload()             const { return engine.getOverload(); }

    // Public so DynamiteComponent can attach Slider/ComboBox/Button controls to
    // the parameter IDs declared in createParameterLayout().
    juce::AudioProcessorValueTreeState parameters;

private:
    //==============================================================================
    [[nodiscard]] static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reads the cached APVTS atomics and pushes scalars to the engine. RT-safe:
    // no allocation, no locking, no logging — just atomic loads and scalar stores.
    void pushParametersToEngine() noexcept;

    template <typename FloatT>
    void processImpl (juce::AudioBuffer<FloatT>& buffer) noexcept;

    //==============================================================================
    DynamiteEngine engine;

    // Cached raw parameter pointers for lock-free audio-thread access.
    std::atomic<float>* pThreshold  = nullptr;
    std::atomic<float>* pRelease    = nullptr;
    std::atomic<float>* pRange      = nullptr;
    std::atomic<float>* pOutput     = nullptr;
    std::atomic<float>* pMix        = nullptr;
    std::atomic<float>* pSource     = nullptr;
    std::atomic<float>* pMode       = nullptr;
    std::atomic<float>* pDetector   = nullptr;
    std::atomic<float>* pAutoMakeup = nullptr;
    std::atomic<float>* pScListen   = nullptr;
    std::atomic<float>* pStereoLink = nullptr;
    std::atomic<float>* pSafety     = nullptr;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
