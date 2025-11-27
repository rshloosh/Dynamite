#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>

class DynamiteProcessor : public juce::AudioProcessor
{
public:
    /**
     * Constructor - initializes with outputGain = 1.0 (0dB)
     */
    DynamiteProcessor();
    
    /**
     * Destructor
     */
    ~DynamiteProcessor() override;
    
    /**
     * Initialize processor to default starting state
     */
    void initialize();
    
    /**
     * Prepare for playback with the given specifications
     */
    void prepare(const juce::dsp::ProcessSpec& spec);
    
    /**
     * Reset the processor state
     */
    void reset() override;
    
    /**
     * Process audio buffer - direct pass-through with fixed gain of 1.0 (0dB)
     * This method actively processes the buffer to ensure audio passes through
     */
    void process(juce::AudioBuffer<double>& buffer);
    
    /**
     * Set output gain in dB - always forces gain to 1.0 (0dB)
     */
    void setOutput(double outputDB);
    
    /**
     * Get current gain reduction in dB - always returns 0.0 (no reduction)
     */
    double getCurrentGainReduction() const;
    
    // Compressor parameters
    void setThreshold(float thresholdDB);
    void setRelease(float releaseMs);
    void setDetectionMode(bool external, bool dsMode);
    void setProcessingMode(bool limitMode, bool outputMode);
    void setDetectorType(bool avgDetector, bool gateDetector);
    void setRange(float rangeDB);
    void setGainReduction(int reductionIndex);

    // Gate parameters
    void setGateThreshold(float thresholdDB);
    void setGateRelease(float releaseMs);
    
    // Deesser parameters
    void setDeesserThreshold(float thresholdDB);
    void setDeesserRatio(float ratio);
    void setDeesserRange(float rangeDB);
    
    // Limiter parameters
    void setLimiterThreshold(float thresholdDB);
    void setLimiterRange(float rangeDB);

    // Auto makeup gain
    void setAutoMakeupGain(bool enabled);
    
    // Mix parameter (0.0 = dry, 1.0 = wet)
    void setMix(float mix);

    // Required virtual methods from AudioProcessor
    const juce::String getName() const override { return "Dynamite"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}
    void getStateInformation(juce::MemoryBlock& destData) override {}
    void setStateInformation(const void* data, int sizeInBytes) override {}

private:
    // Compressor state
    float thresholdDB = -20.0f;  // dB
    float releaseMs = 150.0f;    // ms
    float rangeValue = 20.0f;    // dB (ratio is derived from this)
    bool isExternal = false;
    bool isDSMode = false;
    bool isLimitMode = false;
    bool isOutputMode = false;
    bool isAvgDetector = false;
    bool isGateDetector = false;
    
    // Gate state
    float gateThreshold = -40.0f;  // dB
    float gateRelease = 300.0f;    // ms (scaled up from UI value)
    
    // Limiter state
    double limiterEnvelope = 0.0;
    
    // Processing state
    juce::Time lastChangeTime;
    juce::Time lastOutputChangeTime;
    
    double inputGain = 1.0;       // linear gain
    double outputGain = 1.0;      // linear gain
    
    float limiterThreshold = 0.0f;
    float limiterRange = 20.0f;
    
    // Compressor processing variables
    double inputLevelDB = -100.0;  // current input level in dB
    double gainReduction = 0.0;    // in dB, negative value
    int gainReductionIndex = 0;    // 0 = 1:1 (no compression)
    int silenceCounter = 0;        // Counter for silence detection
    double sampleRate = 44100.0;
    
    // Auto makeup gain
    bool autoMakeupGain = false;
    
    // Mix control (0.0 = dry, 1.0 = wet)
    float mixAmount = 1.0f;
    
    // Compressor processing
    double envelope = 0.0;
    double lastGainReduction = 0.0;
    
    // Gate processing
    double gateEnvelope = 0.0;
    double gateGain = 1.0;
    double lastGateGain = 1.0;

    // Deesser state
    double deesserEnvelope = 0.0;
    float deesserThreshold = -20.0f;
    float deesserRatio = 2.0f;
    float deesserRange = 20.0f;
    
    // Look-ahead buffer for transient detection
    static constexpr int lookAheadSamples = 64;
    juce::AudioBuffer<double> lookAheadBuffer;
    int lookAheadWritePos = 0;
    bool lookAheadBufferFilled = false;
    
    // Helper methods
    double dbToLinear(double db) const;
    double linearToDb(double linear) const;
    void updateCompressorEnvelope(const double* input, int numSamples);
    void updateGateEnvelope(const double* input, int numSamples);
    void updateDeesserEnvelope(const double* input, int numSamples);
    void updateLimiterEnvelope(const double* input, int numSamples);
    double calculateCompressorGain() const;
    double calculateGateGain() const;
    double calculateDeesserGain() const;
    double calculateLimiterGain() const;
    void processWithoutCompression(juce::AudioBuffer<double>& buffer);
    void processWithLookAhead(juce::AudioBuffer<double>& buffer);
    
    // Add missing function declarations
    void calculateGainReductions();
    void applyGains(juce::AudioBuffer<double>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamiteProcessor)
};