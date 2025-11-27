/*
  ==============================================================================

    DynamiteProcessor.cpp
    Created: 25 Mar 2024
    Author:  Dynamite Processor Implementation

  ==============================================================================
*/

#include "DynamiteProcessor.h"

// All functionality is now implemented directly in the header
// This file only exists for compatibility with existing build system 

// Enum definitions for compression modes - placed at the top before any usage
enum class Mode {
    NORMAL,
    DEESS,
    LIMIT
};

DynamiteProcessor::DynamiteProcessor()
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // Set proper initial values to prevent compression on startup
    thresholdDB = 0.0f;         // Start with no compression (0dB threshold)
    gainReductionIndex = 0;   // Start with 1:1 ratio (no compression)
    rangeValue = 0.0f;             // Start with 0dB range
    envelope = -120.0;        // Start with silence envelope
    gainReduction = 0.0;      // Start with no gain reduction
    autoMakeupGain = false;   // Start with auto makeup gain off
    
    // Log initialization with new default values
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor initialized with outputGain = " + juce::String(outputGain) + 
                          ", threshold = " + juce::String(thresholdDB) + 
                          ", ratio = 1:1, range = " + juce::String(rangeValue) + 
                          ", autoMakeupGain = " + juce::String(autoMakeupGain ? "on" : "off"));
}

DynamiteProcessor::~DynamiteProcessor()
{
    // Destructor
}

void DynamiteProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::prepare called with sample rate " + juce::String(spec.sampleRate) + " Hz");

    // Store sample rate for future use
    sampleRate = spec.sampleRate;
    
    // Initialize look-ahead buffer
    lookAheadBuffer.setSize(spec.numChannels, lookAheadSamples);
    lookAheadBuffer.clear();
    lookAheadWritePos = 0;
    lookAheadBufferFilled = false;
    
    juce::Logger::writeToLog(timestamp + " - Look-ahead buffer initialized with " + 
                           juce::String(lookAheadSamples) + " samples (" + 
                           juce::String(1000.0 * lookAheadSamples / spec.sampleRate) + " ms)");
}

void DynamiteProcessor::reset()
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::reset called");
    
    // Reset compression parameters to default non-compressing state
    if (thresholdDB >= 0.0f || gainReductionIndex == 0 || rangeValue <= 0.0f) {
        // If any of the bypass conditions are true, ensure all processing state is inactive
        envelope = -120.0;
        gainReduction = 0.0;
        lastGainReduction = 0.0;
        gateEnvelope = -120.0;
        gateGain = 1.0;
        lastGateGain = 1.0;
        deesserEnvelope = 0.0;
        limiterEnvelope = 0.0;
        
        juce::Logger::writeToLog(timestamp + " - Reset to inactive state (no compression)");
    } else {
        // Reset processing state while maintaining settings
        envelope = -120.0;
        gainReduction = 0.0;
        lastGainReduction = 0.0;
        gateEnvelope = -120.0;
        gateGain = 1.0;
        lastGateGain = 1.0;
        deesserEnvelope = 0.0;
        limiterEnvelope = 0.0;
        
        juce::Logger::writeToLog(timestamp + " - Reset with active settings: threshold = " + 
                              juce::String(thresholdDB) + " dB, ratio = 1:" + 
                              juce::String(1.0 + (gainReductionIndex * 2.0)) + 
                              ", range = " + juce::String(rangeValue) + " dB");
    }
    
    // Reset look-ahead buffer
    lookAheadBuffer.clear();
    lookAheadWritePos = 0;
    lookAheadBufferFilled = false;
}

void DynamiteProcessor::process(juce::AudioBuffer<double>& buffer)
{
    // Create a copy of the input buffer for the dry signal
    juce::AudioBuffer<double> dryBuffer;
    dryBuffer.makeCopyOf(buffer);
    
    // Determine current mode based on settings
    Mode mode = Mode::NORMAL;
    if (isDSMode) {
        mode = Mode::DEESS;
    } else if (isLimitMode) {
        mode = Mode::LIMIT;
    }
    
    // Flag for limiter functionality
    bool enableLimiter = (isLimitMode || isOutputMode);
    
    if (thresholdDB >= 0.0f)
    {
        // Bypass mode - use processWithoutCompression instead
        processWithoutCompression(buffer);
    }
    else
    {
        // Process with look-ahead
        processWithLookAhead(buffer);
    }
    
    // Apply mix (blend between dry and wet signals)
    if (mixAmount < 0.999) // Only perform mixing if not full wet
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            double* wetData = buffer.getWritePointer(channel);
            const double* dryData = dryBuffer.getReadPointer(channel);
            
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                // Linear interpolation between dry and wet signals
                wetData[sample] = (1.0 - mixAmount) * dryData[sample] + mixAmount * wetData[sample];
            }
        }
    }
}

// Add the new look-ahead processing method
void DynamiteProcessor::processWithLookAhead(juce::AudioBuffer<double>& buffer)
{
    // Create a copy of the input buffer for the dry signal (for mix control)
    juce::AudioBuffer<double> dryBuffer;
    dryBuffer.makeCopyOf(buffer);
    
    // Get buffer dimensions
    int numChannels = buffer.getNumChannels();
    int numSamples = buffer.getNumSamples();
    
    // Ensure look-ahead buffer has correct number of channels
    if (lookAheadBuffer.getNumChannels() != numChannels) {
        lookAheadBuffer.setSize(numChannels, lookAheadSamples, true, true, true);
        lookAheadBufferFilled = false;
    }
    
    // First pass: analyze incoming audio for compression detection
    // This provides look-ahead capability - we analyze first, then apply
    for (int channel = 0; channel < numChannels; ++channel)
    {
        // Get current buffer data for analysis
        const double* inputData = buffer.getReadPointer(channel);
        
        // Update appropriate envelopes based on detector mode
        if (!isGateDetector) {
            updateCompressorEnvelope(inputData, numSamples);
        } else {
            updateGateEnvelope(inputData, numSamples);
        }
        
        if (isDSMode && !isGateDetector) {
            updateDeesserEnvelope(inputData, numSamples);
        }
        
        if ((isLimitMode || isOutputMode) && !isGateDetector) {
            updateLimiterEnvelope(inputData, numSamples);
        }
    }
    
    // Calculate all gain reductions based on the updated envelopes
    calculateGainReductions();
    
    // Process audio with look-ahead - swap delayed audio in and current audio out
    for (int channel = 0; channel < numChannels; ++channel)
    {
        double* channelData = buffer.getWritePointer(channel);
        
        if (!lookAheadBufferFilled) {
            // Still in startup phase - filling the look-ahead buffer
            
            // Store input samples in look-ahead buffer
            for (int sample = 0; sample < numSamples; ++sample) {
                // Save current sample to look-ahead buffer
                lookAheadBuffer.setSample(channel, lookAheadWritePos, channelData[sample]);
                
                // Output silence during initial buffer fill
                channelData[sample] = 0.0;
                
                // Advance write position
                lookAheadWritePos = (lookAheadWritePos + 1) % lookAheadSamples;
                
                // Check if buffer is now full
                if (lookAheadWritePos == 0) {
                    lookAheadBufferFilled = true;
                    break; // Exit the loop early as buffer is now filled
                }
            }
            
            // If we filled the buffer during this processing call, process the remaining samples
            if (lookAheadBufferFilled) {
                // Process remaining samples after buffer filled
                for (int sample = lookAheadWritePos; sample < numSamples; ++sample) {
                    // Get delayed sample from buffer
                    double delayedSample = lookAheadBuffer.getSample(channel, lookAheadWritePos);
                    
                    // Store current sample in look-ahead buffer
                    lookAheadBuffer.setSample(channel, lookAheadWritePos, channelData[sample]);
                    
                    // Apply compression to delayed sample
                    double gainFactor = dbToLinear(-gainReduction);
                    channelData[sample] = delayedSample * gainFactor * outputGain;
                    
                    // Advance write position
                    lookAheadWritePos = (lookAheadWritePos + 1) % lookAheadSamples;
                }
            }
        } else {
            // Normal operation with filled look-ahead buffer
            for (int sample = 0; sample < numSamples; ++sample) {
                // Get delayed sample from buffer
                double delayedSample = lookAheadBuffer.getSample(channel, lookAheadWritePos);
                
                // Store current sample in look-ahead buffer
                lookAheadBuffer.setSample(channel, lookAheadWritePos, channelData[sample]);
                
                // Apply compression to delayed sample
                double gainFactor = dbToLinear(-gainReduction);
                channelData[sample] = delayedSample * gainFactor * outputGain;
                
                // Advance write position
                lookAheadWritePos = (lookAheadWritePos + 1) % lookAheadSamples;
            }
        }
    }
    
    // Apply mix (blend between dry and wet signals)
    if (mixAmount < 0.999) // Only perform mixing if not full wet
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            double* wetData = buffer.getWritePointer(channel);
            const double* dryData = dryBuffer.getReadPointer(channel);
            
            for (int sample = 0; sample < numSamples; ++sample)
            {
                // Linear interpolation between dry and wet signals
                wetData[sample] = (1.0 - mixAmount) * dryData[sample] + mixAmount * wetData[sample];
            }
        }
    }
}

// New method to process audio without compression
void DynamiteProcessor::processWithoutCompression(juce::AudioBuffer<double>& buffer)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // CRITICAL FIX: Zero out ALL processing state completely
    envelope = -120.0;
    gainReduction = 0.0;
    lastGainReduction = 0.0;
    gateEnvelope = -120.0;
    gateGain = 1.0;
    lastGateGain = 1.0;
    deesserEnvelope = 0.0;
    limiterEnvelope = 0.0;
    silenceCounter = 0;
    
    if (thresholdDB >= 0.0f) {
        juce::Logger::writeToLog(timestamp + " - PROCESSWITHOUCOMPRESSION BYPASS: Threshold is " + juce::String(thresholdDB) + 
                            " dB >= 0dB - Applying ONLY output gain, zeroed ALL processors");
    }
    
    // Process each channel with ONLY output gain
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        double* channelData = buffer.getWritePointer(channel);
        if (channelData == nullptr) {
            continue;
        }
        
        // CRITICAL FIX: Apply ONLY output gain, nothing else
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            channelData[sample] *= outputGain;
        }
    }
}

void DynamiteProcessor::setOutput(double outputDB)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // Log current value before change
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::setOutput - Current value: " + juce::String(outputGain) + 
                           " (" + juce::String(20.0 * std::log10(outputGain)) + " dB)");
    
    // Calculate auto makeup gain if enabled
    double makeupDB = 0.0;
    if (autoMakeupGain && thresholdDB < 0.0f && gainReductionIndex > 0) {
        // Simple makeup gain formula based on threshold and ratio
        // The lower the threshold and higher the ratio, the more makeup gain
        double ratio = 1.0 + (gainReductionIndex * 2.0);
        makeupDB = std::abs(thresholdDB) * (1.0 - 1.0/ratio) * 0.5; // 50% compensation
        
        // Cap makeup gain to avoid excessive amplification
        makeupDB = std::min(makeupDB, 12.0);
        
        juce::Logger::writeToLog(timestamp + " - Auto makeup gain: " + juce::String(makeupDB) + " dB");
    }
    
    // Convert dB to linear gain (including makeup gain)
    outputGain = dbToLinear(outputDB + makeupDB);
    
    // Log the actual value after change
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::setOutput - New value: " + juce::String(outputGain) + 
                           " (" + juce::String(outputDB) + " dB, auto makeup: " + juce::String(makeupDB) + " dB)");
                           
    // Debug - Store the time we last changed this value
    lastOutputChangeTime = juce::Time::getCurrentTime();
}

double DynamiteProcessor::getCurrentGainReduction() const
{
    // Fix for gain reduction meter showing full reduction with no audio
    if (thresholdDB >= 0.0f) {
        // When the threshold is at or above 0dB, we're in bypass mode and should show no reduction
        return 0.0;
    }
    
    // When below threshold but with silence, don't show any reduction
    if (envelope < -60.0) {
        return 0.0;
    }
    
    // Return the current gain reduction in dB (negative value)
    return -std::abs(gainReduction); // Ensure the value is negative
}

void DynamiteProcessor::setThreshold(float thresholdValue)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Updating threshold: " + juce::String(thresholdDB) + " → " + juce::String(thresholdValue));
    
    // Store previous state to check if we're crossing the 0dB threshold
    bool wasAboveZero = thresholdDB >= 0.0f;
    bool willBeAboveZero = thresholdValue >= 0.0f;
    
    // THRESHOLD FIX: Force envelope recalculation when threshold changes
    // This ensures that changes to threshold immediately affect compression amount
    double oldThreshold = thresholdDB;
    
    // Update threshold value
    thresholdDB = thresholdValue;
    
    // ABSOLUTE BYPASS: When threshold becomes 0dB or higher, immediately zero out ALL processing state
    if (willBeAboveZero) {
        // Force all processing state to inactive values
        envelope = -120.0;
        gainReduction = 0.0;
        lastGainReduction = 0.0;
        gateEnvelope = -120.0;
        gateGain = 1.0;
        lastGateGain = 1.0;
        deesserEnvelope = 0.0;
        limiterEnvelope = 0.0;
        silenceCounter = 0;
        
        // Log the absolute bypass enforcement
        juce::Logger::writeToLog(timestamp + " - ABSOLUTE BYPASS ACTIVATED: Threshold set to " + 
                             juce::String(thresholdDB) + " dB (≥ 0dB) - ALL compression and limiter effects disabled");
    }
    else {
        // CRITICAL FIX: Threshold is now below 0dB - ensure compression is active
        // When threshold changes, adjust envelope to be relative to the new threshold
        // This allows threshold changes to immediately affect compression
        
        // If we're coming from above zero (bypass state) to below zero
        if (wasAboveZero) {
            // Initialize the envelope at a reasonable level relative to the new threshold
            // This ensures compression activates immediately after adjusting from bypass to active
            envelope = thresholdDB - 5.0; // Start 5dB below threshold
            juce::Logger::writeToLog(timestamp + " - COMPRESSION ENABLED: Threshold moved below 0dB to " + 
                             juce::String(thresholdDB) + " dB - initializing envelope at " + juce::String(envelope) + " dB");
        }
        else {
            // Both old and new threshold are below zero
            
            // If threshold is being lowered (more compression)
            if (thresholdValue < oldThreshold) {
                // For lower thresholds, adjust the envelope to maintain the relative position
                // This makes lowering threshold immediately increase compression
                double distanceFromThreshold = envelope - oldThreshold;
                
                // Only update if we're somewhat close to the threshold
                if (std::abs(distanceFromThreshold) < 30.0 && envelope > -90.0) {
                    // Maintain same relative position to threshold
                    envelope = thresholdValue + distanceFromThreshold;
                    juce::Logger::writeToLog(timestamp + " - THRESHOLD LOWERED: Adjusting envelope from " + 
                                      juce::String(envelope) + " dB to maintain distance of " + 
                                      juce::String(distanceFromThreshold) + " dB from threshold");
                }
            }
            // If threshold is being raised (less compression)
            else if (thresholdValue > oldThreshold) {
                // For higher thresholds, keep the envelope at the same level
                // This preserves the current compression amount until audio changes
                juce::Logger::writeToLog(timestamp + " - THRESHOLD RAISED: Keeping envelope at " + 
                                   juce::String(envelope) + " dB with new threshold at " + 
                                   juce::String(thresholdDB) + " dB");
            }
        }
    }
    
    // Always apply deesser and limiter thresholds proportionally to main threshold
    deesserThreshold = thresholdDB;
    limiterThreshold = thresholdDB;
    
    // Log final state to ensure proper debugging information
    juce::Logger::writeToLog(timestamp + " - Final threshold state: Threshold=" + juce::String(thresholdDB) + 
                           "dB, Envelope=" + juce::String(envelope) + 
                           "dB, Bypass=" + juce::String(willBeAboveZero ? "yes" : "no"));
}

void DynamiteProcessor::setRelease(float releaseMs)
{
    // Quadruple the release time value for a more vintage response (was doubled)
    this->releaseMs = releaseMs * 4.0f;
    
    // Log the applied release value
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Set release: " + juce::String(releaseMs) + 
                           " ms (scaled: " + juce::String(this->releaseMs) + " ms)");
}

void DynamiteProcessor::setDetectionMode(bool external, bool dsMode)
{
    isExternal = external;
    isDSMode = dsMode;
}

void DynamiteProcessor::setProcessingMode(bool limitMode, bool outputMode)
{
    isLimitMode = limitMode;
    isOutputMode = outputMode;
}

void DynamiteProcessor::setDetectorType(bool avgDetector, bool gateDetector)
{
    // Capture previous state for comparison
    bool wasGateDetector = isGateDetector;
    
    // Update detector type
    isAvgDetector = avgDetector;
    isGateDetector = gateDetector;
    
    // CRITICAL FIX: When switching from gate to another detector, reset the processors
    // to ensure we don't stay in a gated (muted) state
    if (wasGateDetector && !isGateDetector) {
        juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
        juce::Logger::writeToLog(timestamp + " - CRITICAL FIX: Switching FROM gate TO compressor - resetting envelope values");
        
        // Reset envelope values to ensure we don't keep gate state
        gateEnvelope = -120.0;
        envelope = -120.0;
        
        // Force gateGain to unity (1.0) to ensure gate is fully opened when switching away from gate mode
        gateGain = 1.0;
        lastGateGain = 1.0;
    }
}

void DynamiteProcessor::setRange(float rangeValue)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Updating range: " + juce::String(this->rangeValue) + " → " + juce::String(rangeValue));
    
    this->rangeValue = rangeValue;
    
    // NOTE: Range setting doesn't affect whether compression happens at threshold 0
    // That is controlled purely by the threshold value
    
    // Ensure gain reduction is reset when range is 0 or negative
    if (rangeValue <= 0.0f) {
        gainReduction = 0.0;
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + " dB (inactive - no compression)");
    } else if (rangeValue < 0.5f) {
        // For extremely small range values, provide detailed scaling info
        double exponent = 2.5; 
        double rangeScale = std::pow(rangeValue / 10.0f, exponent);
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (extremely gentle compression - effective max: " + 
                              juce::String(rangeValue * rangeScale) + " dB)");
    } else if (rangeValue < 1.0f) {
        // For very small range values, provide detailed scaling info
        double exponent = 2.2;
        double rangeScale = std::pow(rangeValue / 10.0f, exponent);
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (very gentle compression - effective max: " + 
                              juce::String(rangeValue * rangeScale) + " dB)");
    } else if (rangeValue < 2.0f) {
        // For small range values, provide detailed scaling info
        double exponent = 2.0;
        double rangeScale = std::pow(rangeValue / 10.0f, exponent);
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (gentle compression - effective max: " + 
                              juce::String(rangeValue * rangeScale) + " dB)");
    } else if (rangeValue < 5.0f) {
        // For moderate range values, provide detailed scaling info
        double exponent = 1.8;
        double rangeScale = std::pow(rangeValue / 10.0f, exponent);
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (moderate compression - effective max: " + 
                              juce::String(rangeValue * rangeScale) + " dB)");
    } else if (rangeValue < 10.0f) {
        // For medium-high range values, provide detailed scaling info
        double exponent = 1.5;
        double rangeScale = std::pow(rangeValue / 10.0f, exponent);
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (medium-high compression - effective max: " + 
                              juce::String(rangeValue * rangeScale) + " dB)");
    } else {
        // For higher range values, standard behavior
        juce::Logger::writeToLog(timestamp + " - Range set to " + juce::String(rangeValue) + 
                              " dB (standard compression - full range applied)");
    }
}

void DynamiteProcessor::setGainReduction(int reductionIndex)
{
    // This sets ratio from 1:1 (0) to 1:15 (8)
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    float oldRatio = 1.0 + (gainReductionIndex * 2.0);
    float newRatio = 1.0 + (reductionIndex * 2.0);
    juce::Logger::writeToLog(timestamp + " - Updating ratio: 1:" + juce::String(oldRatio) + " → 1:" + juce::String(newRatio));
    
    gainReductionIndex = reductionIndex;
    
    // Ensure gain reduction is reset when ratio is 1:1
    if (gainReductionIndex == 0) {
        gainReduction = 0.0;
    }
}

void DynamiteProcessor::setGateThreshold(float thresholdDB)
{
    gateThreshold = thresholdDB;
}

void DynamiteProcessor::setGateRelease(float releaseMs)
{
    // Quadruple the gate release time value (was doubled)
    gateRelease = releaseMs * 4.0f;
    
    // Log the applied gate release value
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Set gate release: " + juce::String(releaseMs) + 
                           " ms (scaled: " + juce::String(gateRelease) + " ms)");
}

// Helper methods implementation
double DynamiteProcessor::dbToLinear(double db) const
{
    return std::pow(10.0, db / 20.0);
}

double DynamiteProcessor::linearToDb(double linear) const
{
    return 20.0 * std::log10(linear);
}

void DynamiteProcessor::updateCompressorEnvelope(const double* input, int numSamples)
{
    // ABSOLUTE BYPASS: If threshold is 0 or above, do not update envelope at all
    if (thresholdDB >= 0.0f) {
        // Keep envelope at silence level
        envelope = -120.0;
        return;
    }
    
    // For logging
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // Track maximum level in this buffer for debugging
    double maxLevel = -120.0;
    
    // FIX: Properly balance attack and release times for better threshold response
    // Use faster attack for better threshold tracking
    double attackTime = 0.05; // 0.05ms attack (was 0.1ms) - faster attack for better compression response
    double attackSamples = attackTime * sampleRate / 1000.0;
    
    // Calculate release time in samples - use the configured release time (scaled by 4x in the setRelease method)
    double releaseSamples = this->releaseMs * sampleRate / 1000.0;
    
    // CRITICAL FIX: For lower threshold values, use a shorter release time to ensure
    // we detect transients even when below the threshold generally
    if (thresholdDB < -20.0) {
        double thresholdScale = 1.0 - ((std::abs(thresholdDB) - 20.0) / 40.0); // Range 1.0 to 0.5 for thresholds -20 to -60
        thresholdScale = std::max(0.5, thresholdScale); // Don't go below 0.5x the release time
        releaseSamples *= thresholdScale;
        
        // Log the adjusted release time occasionally
        if (juce::Time::getCurrentTime().toMilliseconds() % 1000 < 5) {
            juce::Logger::writeToLog(timestamp + " - THRESHOLD SENSITIVE RELEASE: Adjusted release time for low threshold " +
                                  juce::String(thresholdDB) + " dB using scale factor " + juce::String(thresholdScale));
        }
    }
    
    // Process each sample
    for (int i = 0; i < numSamples; ++i)
    {
        // Get input level - ensure we're not processing tiny values
        double inputLevel = std::abs(input[i]);
        if (inputLevel < 1.0e-8) {
            inputLevel = 0.0; // Treat very small values as silence to avoid floating point issues
        }
        
        // Convert to dB with small epsilon to avoid -infinity for silence
        // Only add epsilon for non-zero values to maintain proper silence detection
        double inputDB;
        if (inputLevel > 0.0) {
            inputDB = 20.0 * std::log10(inputLevel);
            maxLevel = std::max(maxLevel, inputDB);
        } else {
            inputDB = -120.0; // True silence floor
        }
        
        // THRESHOLD FIX: Ensure envelope is updated with proper attack/release behavior
        // to maintain responsiveness at various threshold levels
        if (isAvgDetector)
        {
            // Average detection with enhanced response
            if (inputDB > envelope)
            {
                // IMPORTANT FIX: Use a more responsive attack for better threshold tracking
                // Especially important when threshold is set to lower values
                double attackFactor = 0.1;
                if (thresholdDB < -30.0) {
                    // For very low thresholds, use an even faster attack to catch transients
                    attackFactor = 0.05;
                }
                
                envelope = envelope + (inputDB - envelope) / (attackSamples * attackFactor);
            }
            else
            {
                // Standard release behavior
                envelope = envelope + (inputDB - envelope) / releaseSamples;
            }
        }
        else
        {
            // Peak detection with enhanced response
            if (inputDB > envelope)
            {
                // THRESHOLD FIX: For peak detection, still use envelope following rather than
                // direct assignment to avoid stepping artifacts in the compression
                // Especially important when threshold is very low and we need smoother response
                if (inputDB > (envelope + 10.0)) {
                    // Quick response to large jumps (10dB or more)
                    envelope = inputDB;
                } else {
                    // Smoother response to smaller changes
                    envelope = envelope + (inputDB - envelope) * 0.7; // 70% of the way to the new value
                }
            }
            else
            {
                // Standard release behavior, but make it slightly smoother
                envelope = envelope + (inputDB - envelope) / releaseSamples;
            }
        }
    }
    
    // THRESHOLD FIX: Make sure envelope doesn't fall too far below threshold to ensure
    // we stay responsive to signals near the threshold level
    if (envelope < (thresholdDB - 20.0)) {
        // If envelope falls more than 20dB below threshold, keep it closer to respond faster
        envelope = thresholdDB - 20.0;
    }
    
    // Occasional logging of envelope levels - increase frequency for debugging
    if (juce::Time::getCurrentTime().toMilliseconds() % 500 < 10) {
        juce::Logger::writeToLog(timestamp + " - COMPRESSOR ENVELOPE: Max level: " + juce::String(maxLevel) +
                               " dB, threshold: " + juce::String(thresholdDB) + " dB, final envelope: " + 
                               juce::String(envelope) + " dB, over threshold: " + 
                               juce::String(envelope > thresholdDB ? "YES" : "NO"));
    }
}

void DynamiteProcessor::updateGateEnvelope(const double* input, int numSamples)
{
    // CRITICAL FIX: Immediately exit if threshold is 0 or above - no processing at all
    if (thresholdDB >= 0.0f) {
        gateEnvelope = -120.0;
        gateGain = 1.0;
        lastGateGain = 1.0;
        return;
    }
    
    // Calculate release time in samples
    double releaseSamples = gateRelease * sampleRate / 1000.0;
    
    // Process each sample
    for (int i = 0; i < numSamples; ++i)
    {
        // Get input level
        double inputLevel = std::abs(input[i]);
        
        // Convert to dB
        double inputDB = linearToDb(inputLevel);
        
        // Update gate envelope with faster response
        if (inputDB > gateThreshold)
        {
            // Fast attack for gate
            gateEnvelope = inputDB;
        }
        else
        {
            // Smooth release
            gateEnvelope = gateEnvelope + (inputDB - gateEnvelope) / releaseSamples;
        }
        
        // Ensure gate envelope doesn't go below threshold
        gateEnvelope = std::max(gateEnvelope, static_cast<double>(gateThreshold));
    }
}

double DynamiteProcessor::calculateCompressorGain() const
{
    // ABSOLUTE BYPASS: If threshold is 0 or above, ALWAYS return 1.0 (unity gain)
    // No other checks or calculations should ever happen
    if (thresholdDB >= 0.0f) {
        return 1.0; // Hard-coded unity gain with zero compression
    }
    
    // Only continue below with threshold < 0
    // Calculate gain reduction based on envelope and threshold
    double gainReductionDB = 0.0;
    
    // For debug logging
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // No need to check threshold again - we already know it's < 0
    
    if (gainReductionIndex == 0) {
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) { // Reduce logging frequency
            juce::Logger::writeToLog(timestamp + " - No compression: Ratio is 1:1");
        }
        return 1.0; // No reduction with 1:1 ratio
    }
    
    if (rangeValue <= 0.0f) {
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) { // Reduce logging frequency
            juce::Logger::writeToLog(timestamp + " - No compression: Range is " + juce::String(rangeValue) + 
                              " dB (≤ 0)");
        }
        return 1.0; // No reduction with range at or below 0
    }
    
    // NEW FIX FOR ULTRA-LOW RANGE VALUES: Use significantly stronger scaling for ultra-low range values
    // For range values near 0, ensure virtually no compression occurs (0.04 should not cause 17dB reduction)
    if (rangeValue <= 1.0f) {
        // CRITICAL FIX: For ultra-low range values <= 1.0, completely bypass compression
        // Log the action with high visibility
        juce::Logger::writeToLog(timestamp + " - EMERGENCY FIX: Any range " + juce::String(rangeValue) + 
                              " dB below or equal to 1.0 dB is COMPLETELY BYPASSED with no gain reduction");
        
        // Return exact unity gain - no compression whatsoever for any range <= 1.0
        return 1.0; // Absolutely no reduction regardless of input level
    }
    
    // REVISED SCALING: Gradual increase with heavy effect starting at about 50% knob position
    // Assuming knob range is 0-40 dB with midpoint at 20 dB
    
    // FIX: Progressive scaling for range values to provide a more gradual effect
    double scaledRange = rangeValue;
    double maxRangeDB = 40.0; // Assuming this is the max knob value
    double midpointDB = 20.0; // 50% knob position
    
    // Calculate the percentage of the full range (0-100%)
    double rangePercent = rangeValue / maxRangeDB * 100.0;
    
    if (rangeValue < 0.1f) {
        // For very low range values (under 0.1), virtually no compression (1/1000 of input)
        scaledRange = rangeValue * 0.001;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Very low range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to minimal effect: " + 
                              juce::String(scaledRange) + " dB");
        }
    } else if (rangeValue < 1.0f) {
        // Range 0.1-1.0: Ultra light effect (1/100 of input)
        scaledRange = rangeValue * 0.01;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Low range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to very light effect: " + 
                              juce::String(scaledRange) + " dB");
        }
    } else if (rangeValue < 5.0f) {
        // Range 1.0-5.0: Very light effect (1/20 of input)
        scaledRange = rangeValue * 0.05;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Low-medium range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to light effect: " + 
                              juce::String(scaledRange) + " dB");
        }
    } else if (rangeValue < 10.0f) {
        // Range 5.0-10.0: Light effect (1/10 of input)
        scaledRange = rangeValue * 0.1;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Light-medium range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to gentle effect: " + 
                              juce::String(scaledRange) + " dB");
        }
    } else if (rangeValue < midpointDB) {
        // Range 10.0-20.0 (below 50% position): Gentle transition to medium effect
        // Linear scaling from 10% to 40% effect as we approach midpoint
        double factor = 0.1 + 0.3 * ((rangeValue - 10.0) / (midpointDB - 10.0));
        scaledRange = rangeValue * factor;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Medium range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to medium effect: " + 
                              juce::String(scaledRange) + " dB with factor " + juce::String(factor));
        }
    } else if (rangeValue < 30.0f) {
        // Range 20.0-30.0 (50-75% position): Significant effect begins
        // Linear scaling from 40% to 75% effect in this range
        double factor = 0.4 + 0.35 * ((rangeValue - midpointDB) / (30.0 - midpointDB));
        scaledRange = rangeValue * factor;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Medium-high range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to significant effect: " + 
                              juce::String(scaledRange) + " dB with factor " + juce::String(factor));
        }
    } else {
        // Range 30.0-40.0 (75-100% position): Heavy to full effect
        // Linear scaling from 75% to 100% effect
        double factor = 0.75 + 0.25 * ((rangeValue - 30.0) / (maxRangeDB - 30.0));
        scaledRange = rangeValue * factor;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - High range value: " + juce::String(rangeValue) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to heavy effect: " + 
                              juce::String(scaledRange) + " dB with factor " + juce::String(factor));
        }
    }
    
    // Calculate ratio - this gives us ratios from 1:1 to 1:15
    double ratio = 1.0 + (gainReductionIndex * 2.0);
    
    // If ratio is 1:1, no gain reduction
    if (ratio <= 1.01) {
        return 1.0;
    }
    
    // CRITICAL FIX: Modified threshold comparison to actually use the threshold value
    // Check if the envelope exceeds the threshold by at least 0.1dB
    if (envelope > (thresholdDB + 0.1))
    {
        // Log this calculation
        if (juce::Time::getCurrentTime().toMilliseconds() % 500 < 10) { // Increase logging frequency for debugging
            juce::Logger::writeToLog(timestamp + " - THRESHOLD ACTIVE: Envelope: " + juce::String(envelope) + 
                                  " dB, Threshold: " + juce::String(thresholdDB) + 
                                  " dB, Difference: " + juce::String(envelope - thresholdDB) +
                                  ", Ratio: 1:" + juce::String(ratio));
        }
        
        // Calculate gain reduction - only above threshold
        double overThreshold = envelope - thresholdDB;
        
        // Apply no reduction below the threshold
        if (overThreshold <= 0.0) {
            return 1.0; // No gain reduction needed
        }
        
        // THRESHOLD FIX: Ensure that lower threshold values cause more gain reduction
        // for the same signal level by using the full amount over threshold
        gainReductionDB = overThreshold * (1.0 - 1.0/ratio);
        
        // Apply range limit - if range is 0, no gain reduction
        if (rangeValue <= 0.0f) {
            return 1.0;
        }
        
        // LINEAR RANGE IMPLEMENTATION:
        // Use the scaled range value instead of the raw range
        
        // Store original calculated gain reduction for reference
        double originalGainReductionDB = gainReductionDB;
        
        // Apply linear scaling with the scaled range value
        gainReductionDB = std::min(originalGainReductionDB, static_cast<double>(scaledRange));
        
        // Log the linear scaling for debug purposes
        if (juce::Time::getCurrentTime().toMilliseconds() % 500 < 10) { // Increase logging frequency
            juce::Logger::writeToLog(timestamp + " - THRESHOLD EFFECT: Original GR " + 
                                  juce::String(originalGainReductionDB) + " dB, Threshold: " + 
                                  juce::String(thresholdDB) + " dB, Range: " + 
                                  juce::String(rangeValue) + " dB, Effective Range: " + 
                                  juce::String(scaledRange) + " dB, Final GR: " + 
                                  juce::String(gainReductionDB) + " dB");
        }
        
        // In limit mode, ensure we don't exceed the threshold
        if (isLimitMode)
        {
            gainReductionDB = std::max(gainReductionDB, overThreshold);
        }
        
        // In output mode, apply less aggressive reduction
        if (isOutputMode)
        {
            gainReductionDB *= 0.5; // Less aggressive reduction
        }
        
        // Add knee for smoother response when close to threshold
        double kneeWidth = 6.0; // dB
        if (overThreshold < kneeWidth)
        {
            double kneeRatio = (overThreshold / kneeWidth);
            gainReductionDB *= kneeRatio; // Smooth linear knee
        }
        
        // Adjust gain reduction based on threshold position
        if (thresholdDB < -20.0)
        {
            // Use a gentler scaling approach for lower thresholds to prevent distortion
            double thresholdDiff = std::abs(thresholdDB + 20.0);
            double scaleFactor = 1.0 - (thresholdDiff / 200.0); // Much more gentle scaling
            scaleFactor = std::max(0.7, scaleFactor); // Keep at least 70% of the gain reduction
            gainReductionDB *= scaleFactor;
        }
        
        // Ensure reduction doesn't cause distortion by being too extreme
        gainReductionDB = std::min(gainReductionDB, 40.0); // Cap maximum reduction at 40dB
    }
    else {
        // Below threshold - no gain reduction
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) { // Reduce logging frequency
            juce::Logger::writeToLog(timestamp + " - Below threshold: Envelope " + juce::String(envelope) + 
                                  " dB is below threshold " + juce::String(thresholdDB) + " dB + margin (0.1 dB)");
        }
        return 1.0;
    }
    
    // Convert to linear gain (reduction is negative dB)
    return dbToLinear(-gainReductionDB);
}

double DynamiteProcessor::calculateGateGain() const
{
    // CRITICAL FIX: Immediately return unity gain if threshold is 0 or above
    if (thresholdDB >= 0.0f) {
        return 1.0; // No gating at all with threshold at 0 or higher
    }
    
    // Calculate gate gain based on envelope and threshold
    if (gateEnvelope > gateThreshold)
    {
        return 1.0; // Above threshold - full gain
    }
    else
    {
        // Below threshold - completely cut audio for true gate effect
        // Add a very small transition region to avoid clicks
        double gateRangeDB = 40.0; // Maximum gate range
        
        // Calculate gate amount based on how far below threshold we are
        double gateAmount = (gateThreshold - gateEnvelope) / 3.0; // Use a narrower 3dB transition window
        gateAmount = std::min(gateAmount, 1.0); // Limit to full gate
        
        // Apply sharp curve for fast gate effect with minimal transition
        gateAmount = std::pow(gateAmount, 3.0); // More aggressive curve for faster gating
        
        // Complete cut-off when fully gated (use -120dB which is effectively silence)
        double gateGainDB = -120.0 * gateAmount;
        
        // Log gate effect when significant
        static double lastLoggedGainDB = 0.0;
        if (std::abs(gateGainDB - lastLoggedGainDB) > 10.0 && 
            juce::Time::getCurrentTime().toMilliseconds() % 1000 < 10) {
            juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
            juce::Logger::writeToLog(timestamp + " - GATE ACTIVE: Threshold=" + juce::String(gateThreshold) + 
                          " dB, Envelope=" + juce::String(gateEnvelope) + 
                          " dB, Reduction=" + juce::String(gateGainDB) + " dB");
            lastLoggedGainDB = gateGainDB;
        }
        
        return dbToLinear(gateGainDB);
    }
}

void DynamiteProcessor::updateDeesserEnvelope(const double* input, int numSamples)
{
    // CRITICAL FIX: Immediately exit if threshold is 0 or above - no processing at all
    if (thresholdDB >= 0.0f) {
        deesserEnvelope = 0.0;
        return;
    }
    
    // Deesser envelope follows the input signal with fast attack and release
    const double attackTime = 0.001; // 1ms attack
    const double releaseTime = 0.05; // 50ms release
    
    for (int i = 0; i < numSamples; ++i)
    {
        double inputLevel = std::abs(input[i]);
        
        if (inputLevel > deesserEnvelope)
        {
            // Fast attack
            deesserEnvelope = deesserEnvelope + (inputLevel - deesserEnvelope) * (1.0 - std::exp(-1.0 / (sampleRate * attackTime)));
        }
        else
        {
            // Release
            deesserEnvelope = deesserEnvelope + (inputLevel - deesserEnvelope) * (1.0 - std::exp(-1.0 / (sampleRate * releaseTime)));
        }
    }
}

double DynamiteProcessor::calculateDeesserGain() const
{
    // CRITICAL FIX: Immediately return unity gain if threshold is 0 or above
    if (thresholdDB >= 0.0f) {
        return 1.0; // No de-essing at all with threshold at 0 or higher
    }
    
    double thresholdLinear = dbToLinear(deesserThreshold);
    double gainReduction = 1.0;
    
    if (deesserEnvelope > thresholdLinear)
    {
        double overThreshold = deesserEnvelope / thresholdLinear;
        gainReduction = 1.0 / (1.0 + (overThreshold - 1.0) * deesserRatio);
        gainReduction = std::max(gainReduction, dbToLinear(-deesserRange));
    }
    
    return gainReduction;
}

void DynamiteProcessor::updateLimiterEnvelope(const double* input, int numSamples)
{
    // CRITICAL FIX: Immediately exit if threshold is 0 or above - no processing at all
    if (thresholdDB >= 0.0f) {
        limiterEnvelope = 0.0;
        return;
    }
    
    // Limiter envelope follows the input signal with very fast attack and release
    const double attackTime = 0.0001; // 0.1ms attack
    const double releaseTime = 0.01; // 10ms release
    
    for (int i = 0; i < numSamples; ++i)
    {
        double inputLevel = std::abs(input[i]);
        
        if (inputLevel > limiterEnvelope)
        {
            // Very fast attack
            limiterEnvelope = limiterEnvelope + (inputLevel - limiterEnvelope) * (1.0 - std::exp(-1.0 / (sampleRate * attackTime)));
        }
        else
        {
            // Release
            limiterEnvelope = limiterEnvelope + (inputLevel - limiterEnvelope) * (1.0 - std::exp(-1.0 / (sampleRate * releaseTime)));
        }
    }
}

double DynamiteProcessor::calculateLimiterGain() const
{
    // ABSOLUTE BYPASS: If threshold is 0 or above, we MUST return 1.0 (unity gain)
    // This check must happen before anything else
    if (thresholdDB >= 0.0f || limiterThreshold >= 0.0f) {
        return 1.0; // Hard-coded unity gain with zero limiting
    }
    
    // Only continue below if thresholds are < 0
    double thresholdLinear = dbToLinear(limiterThreshold);
    double gainReduction = 1.0;
    
    // For debug logging
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // NEW FIX: For ultra-low range values (≤ 0.05), completely disable limiting
    if (limiterRange <= 0.05f) {
        return 1.0; // No reduction with very low range - ensures clean bypass behavior
    }
    
    // REVISED SCALING: Gradual increase with heavy effect starting at about 50% knob position
    // Assuming knob range is 0-40 dB with midpoint at 20 dB
    double maxRangeDB = 40.0; // Assuming this is the max knob value
    double midpointDB = 20.0; // 50% knob position
    
    // Calculate the percentage of the full range (0-100%)
    double rangePercent = limiterRange / maxRangeDB * 100.0;
    
    // FIX: Handle limiter range values with the same progressive scaling as the compressor
    double effectiveRange = limiterRange;
    
    if (limiterRange < 0.1f && limiterRange > 0.0f) {
        // For very low range values (under 0.1), virtually no limiting
        effectiveRange = limiterRange * 0.001;
    } else if (limiterRange < 1.0f) {
        // Range 0.1-1.0: Ultra light effect
        effectiveRange = limiterRange * 0.01;
    } else if (limiterRange < 5.0f) {
        // Range 1.0-5.0: Very light effect
        effectiveRange = limiterRange * 0.05;
    } else if (limiterRange < 10.0f) {
        // Range 5.0-10.0: Light effect
        effectiveRange = limiterRange * 0.1;
    } else if (limiterRange < midpointDB) {
        // Range 10.0-20.0 (below 50% position): Gentle transition to medium effect
        // Linear scaling from 10% to 40% effect as we approach midpoint
        double factor = 0.1 + 0.3 * ((limiterRange - 10.0) / (midpointDB - 10.0));
        effectiveRange = limiterRange * factor;
        
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            juce::Logger::writeToLog(timestamp + " - Limiter: Medium range value: " + juce::String(limiterRange) + 
                              " dB (" + juce::String(rangePercent) + "%) scaled to medium effect: " + 
                              juce::String(effectiveRange) + " dB");
        }
    } else if (limiterRange < 30.0f) {
        // Range 20.0-30.0 (50-75% position): Significant effect begins
        // Linear scaling from 40% to 75% effect in this range
        double factor = 0.4 + 0.35 * ((limiterRange - midpointDB) / (30.0 - midpointDB));
        effectiveRange = limiterRange * factor;
    } else {
        // Range 30.0-40.0 (75-100% position): Heavy to full effect
        // Linear scaling from 75% to 100% effect
        double factor = 0.75 + 0.25 * ((limiterRange - 30.0) / (maxRangeDB - 30.0));
        effectiveRange = limiterRange * factor;
    }
    
    if (limiterEnvelope > thresholdLinear)
    {
        // Calculate the gain reduction
        double rawReduction = thresholdLinear / limiterEnvelope;
        
        // Calculate the minimum gain (maximum reduction) in linear terms
        double minGain = dbToLinear(-effectiveRange);
        
        // Apply scaled limiting based on the effective range
        gainReduction = std::max(rawReduction, minGain);
        
        // Log limiter action occasionally
        if (juce::Time::getCurrentTime().toMilliseconds() % 2000 < 10) {
            double gainReductionDB = 20.0 * std::log10(gainReduction);
            juce::Logger::writeToLog(timestamp + " - Limiter active: " + juce::String(gainReductionDB) + 
                                  " dB reduction with range: " + juce::String(limiterRange) + 
                                  " dB scaled to effective range: " + juce::String(effectiveRange) + " dB");
        }
    }
    
    return gainReduction;
}

void DynamiteProcessor::setDeesserThreshold(float thresholdDB)
{
    deesserThreshold = thresholdDB;
}

void DynamiteProcessor::setDeesserRatio(float ratio)
{
    deesserRatio = ratio;
}

void DynamiteProcessor::setDeesserRange(float rangeDB)
{
    deesserRange = rangeDB;
}

void DynamiteProcessor::setLimiterThreshold(float thresholdDB)
{
    limiterThreshold = thresholdDB;
}

void DynamiteProcessor::setLimiterRange(float rangeDB)
{
    limiterRange = rangeDB;
}

void DynamiteProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::prepareToPlay called with sample rate " + 
                           juce::String(sampleRate) + " Hz, block size " + juce::String(samplesPerBlock));

    // Store sample rate for future use
    this->sampleRate = sampleRate;
    
    // Initialize the look-ahead buffer
    lookAheadBuffer.setSize(getNumInputChannels(), lookAheadSamples);
    lookAheadBuffer.clear();
    lookAheadWritePos = 0;
    lookAheadBufferFilled = false;
    
    // Report latency to the host for proper plugin delay compensation
    setLatencySamples(lookAheadSamples);
    
    juce::Logger::writeToLog(timestamp + " - Look-ahead buffer initialized with " + 
                           juce::String(lookAheadSamples) + " samples (" + 
                           juce::String(1000.0 * lookAheadSamples / sampleRate) + " ms)");
    juce::Logger::writeToLog(timestamp + " - Plugin latency set to " + juce::String(lookAheadSamples) + " samples");
    
    prepare(juce::dsp::ProcessSpec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 
                                 static_cast<juce::uint32>(getNumInputChannels())});
}

void DynamiteProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - ProcessBlock called with " + 
                          juce::String(buffer.getNumChannels()) + " channels, " + 
                          juce::String(buffer.getNumSamples()) + " samples");
    
    // Create a temporary double precision buffer
    juce::AudioBuffer<double> doubleBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    
    // Copy input from float to double
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* floatData = buffer.getWritePointer(channel);
        double* doubleData = doubleBuffer.getWritePointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            doubleData[sample] = static_cast<double>(floatData[sample]);
        }
    }
    
    // Process the double buffer using our main processing method
    process(doubleBuffer);
    
    // Copy the processed double data back to the float buffer
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* floatData = buffer.getWritePointer(channel);
        const double* doubleData = doubleBuffer.getReadPointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            floatData[sample] = static_cast<float>(doubleData[sample]);
        }
    }
}

// Add new method to enable/disable auto makeup gain
void DynamiteProcessor::setAutoMakeupGain(bool enabled)
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Auto makeup gain: " + juce::String(enabled ? "enabled" : "disabled"));
    
    autoMakeupGain = enabled;
    
    // Update the output gain to apply/remove makeup if needed
    // We can reuse the current output gain value (in dB) to recalculate with/without makeup
    double currentOutputDB = 20.0 * std::log10(outputGain);
    setOutput(currentOutputDB);
}

void DynamiteProcessor::initialize()
{
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    
    // Initialize with default values
    gainReductionIndex = 0;      // No compression
    thresholdDB = 0.0f;         // Start with no compression (0dB threshold)
    float rangeValue = 0.0f;             // Start with 0dB range
    
    // Log initialization
    juce::Logger::writeToLog(timestamp + " - DynamiteProcessor::initialize - " + 
                           "gainReductionIndex = " + juce::String(gainReductionIndex) + 
                           ", thresholdDB = " + juce::String(thresholdDB) + 
                           ", ratio = 1:1, range = " + juce::String(rangeValue) + 
                           ", output = " + juce::String(20.0 * std::log10(outputGain)) + " dB");
}

void DynamiteProcessor::setMix(float newMix)
{
    // Make sure the mix value is constrained between 0 and 1
    mixAmount = juce::jlimit(0.0f, 1.0f, newMix);
    juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::Logger::writeToLog(timestamp + " - Set mix: " + juce::String(mixAmount) + " (0.0=dry, 1.0=wet)");
}

// Add missing implementations for the methods called in process
void DynamiteProcessor::calculateGainReductions()
{
    // Calculate compressor gain reduction
    double compGain = 0.0;
    if (!isGateDetector) {
        compGain = calculateCompressorGain();
    }
    
    // Calculate gate gain
    double gateG = 1.0;
    if (isGateDetector) {
        gateG = calculateGateGain();
    }
    
    // Calculate deesser gain
    double deesserGain = 1.0;
    if (isDSMode && !isGateDetector) {
        deesserGain = calculateDeesserGain();
    }
    
    // Calculate limiter gain
    double limiterGain = 1.0;
    if ((isLimitMode || isOutputMode) && !isGateDetector) {
        limiterGain = calculateLimiterGain();
    }
    
    // Combine all gains
    double totalGain = compGain * gateG * deesserGain * limiterGain;
    
    // Apply smoothing
    double alpha = 0.9; // Smoothing factor
    gainReduction = alpha * lastGainReduction + (1.0 - alpha) * (-20.0 * std::log10(totalGain));
    lastGainReduction = gainReduction;
}

void DynamiteProcessor::applyGains(juce::AudioBuffer<double>& buffer)
{
    // Calculate linear gain from dB gain reduction
    double linearGain = std::pow(10.0, -gainReduction / 20.0);
    
    // Apply gain to all channels
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        double* channelData = buffer.getWritePointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            // Apply gain reduction and output gain
            channelData[sample] = channelData[sample] * linearGain * outputGain;
        }
    }
} 