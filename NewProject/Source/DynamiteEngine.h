/*
  ==============================================================================

    DynamiteEngine.h

    Real-time-safe DSP engine modelling the Valley People "Dyna-Mite" dynamics
    processor, per the Dyna-Mite Owner's Manual (Valley People, Inc.).

    The hardware is a feed-forward VCA processor whose behaviour is set by three
    3-position switches and four continuously-variable controls:

      S1  SOURCE   : INTERNAL | DS-FM | EXTERNAL   (what feeds the detector)
      S2  MODE     : LIMIT    | EXPAND | OUT        (above / below / bypass)
      S3  DETECTOR : AVG      | PEAK   | GATE       (integration + ratio)

      THRESHOLD  -40 .. +20 dBv
      RELEASE     50 ms .. 5 s  (per 20 dB)
      RANGE        0 .. 60 dB   (max gain reduction; inactive in coupled limit)
      OUTPUT     -15 .. +15 dB  (VCA gain, or coupled output level in limit)

    The 19 functional modes of the manual collapse into a single gain computer:
      * LIMIT  -> gain reduction grows as the detector rises ABOVE threshold.
      * EXPAND -> gain reduction grows as the detector falls BELOW threshold.
      * AVG/PEAK -> slope 1  (inf:1 limiting / 1:2 expansion).
      * GATE     -> slope 20 (1:-20 negative-limit/duck / 1:20 gate/key).
    Attack/Release smooth the detector LEVEL (fast up, slow down) so every mode
    behaves as the manual describes.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>

class DynamiteEngine
{
public:
    // S1 - detector source
    enum class Source { Internal = 0, DsFm = 1, External = 2 };
    // S2 - fundamental mode
    enum class Mode { Limit = 0, Expand = 1, Bypass = 2 };
    // S3 - detector characteristic
    enum class Detector { Avg = 0, Peak = 1, Gate = 2 };

    DynamiteEngine();

    // Reference calibration: digital full scale (0 dBFS) == +24 dBv, matching the
    // hardware's +24 dBv maximum input. Threshold/level maths work in dBv.
    static constexpr double kFullScaleDbv = 24.0;

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    // ---- Parameter setters (call from the audio thread; cheap, no allocation) ----
    void setThresholdDbv (float dbv) noexcept          { thresholdDbv = dbv; }
    void setReleaseSeconds (float seconds) noexcept     { releaseSeconds = juce::jmax (0.02f, seconds); }
    void setRangeDb (float db) noexcept                 { rangeDb = juce::jlimit (0.0f, 60.0f, db); }
    void setOutputDb (float db) noexcept                { outputDb = db; }
    void setMix (float mix01) noexcept                  { mix = juce::jlimit (0.0f, 1.0f, mix01); }
    void setSource (Source s) noexcept                  { source = s; }
    void setMode (Mode m) noexcept                      { mode = m; }
    void setDetector (Detector d) noexcept              { detector = d; }
    void setAutoMakeup (bool on) noexcept               { autoMakeup = on; }
    void setSidechainListen (bool on) noexcept          { sidechainListen = on; }
    void setStereoLink (bool on) noexcept               { stereoLink = on; }
    void setSafetyCeiling (bool on) noexcept            { safetyCeiling = on; }

    [[nodiscard]] int getLatencySamples() const noexcept { return lookaheadSamples; }

    // ---- Processing ----
    void process (juce::AudioBuffer<float>&  main, const juce::AudioBuffer<float>*  sidechain);
    void process (juce::AudioBuffer<double>& main, const juce::AudioBuffer<double>* sidechain);

    // ---- Metering (audio thread writes, GUI reads) ----
    [[nodiscard]] float getGainReductionDb() const noexcept { return meterGrDb.load (std::memory_order_relaxed); }
    [[nodiscard]] bool  getOverload()        const noexcept { return meterOverload.load (std::memory_order_relaxed); }

private:
    template <typename FloatT>
    void processInternal (juce::AudioBuffer<FloatT>& main, const juce::AudioBuffer<FloatT>* sidechain);

    // Static gain computer: returns gain reduction in dB (>= 0) for a detector
    // level already expressed in dBv.
    [[nodiscard]] double computeGainReductionDb (double levelDbv) const noexcept;

    // True when the THRESHOLD/OUTPUT gain-coupling is engaged (manual modes 2,3,5,6).
    [[nodiscard]] bool isCoupled() const noexcept
    {
        return mode == Mode::Limit && detector != Detector::Gate && source != Source::External;
    }

    // First-order-ish high shelf used for the DS-FM detector pre-emphasis (75 us).
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        void reset() noexcept { z1 = z2 = 0.0; }
        inline double processSample (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void setHighShelf (Biquad& bq, double fc, double gainDb, double q) const noexcept;

    // ---- Parameters (audio-thread owned scalars) ----
    float thresholdDbv   = -10.0f;
    float releaseSeconds =  0.25f;
    float rangeDb        =  40.0f;
    float outputDb       =   0.0f;
    float mix            =   1.0f;
    Source   source   = Source::Internal;
    Mode     mode     = Mode::Limit;
    Detector detector = Detector::Peak;
    bool autoMakeup      = false;
    bool sidechainListen = false;
    bool stereoLink      = true;
    bool safetyCeiling   = true;

    // ---- Prepared state ----
    double sampleRate = 44100.0;
    int    numChannels = 2;
    int    lookaheadSamples = 32;

    // Per-channel detector level envelope (in dBv) and DS-FM pre-emphasis filters.
    std::array<double, 2> levelDbv { { -120.0, -120.0 } };
    std::array<Biquad, 2> dsFmFilter;

    // Look-ahead delay line for the main signal (pre-allocated ring buffer).
    juce::AudioBuffer<double> lookaheadBuffer;
    int lookaheadWritePos = 0;

    // ---- Metering ----
    std::atomic<float> meterGrDb   { 0.0f };
    std::atomic<bool>  meterOverload { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamiteEngine)
};
