/*
  ==============================================================================

    DynamiteEngine.cpp

    Real-time-safe DSP engine modelling the Valley People "Dyna-Mite" dynamics
    processor. Implements the pinned interface declared in DynamiteEngine.h.

    Signal flow (feed-forward VCA, per the owner's manual):

        main in --> [look-ahead delay N samples] ----------------.
                |                                                 |
                '--> detector key (SOURCE select, DS-FM shelf) --> level (dBv)
                                                                   |  fast-up
                                                                   |  slow-down
                                                              gain computer
                                                                   |
                     wet = delayed * grGain * outStageGain <-------'
                     out = mix*wet + (1-mix)*delayedDry
                     --> safety ceiling --> overload detect --> out

    REAL-TIME SAFETY: nothing on the audio path allocates, locks, logs, builds
    a juce::String, calls juce::Time, throws, or makeCopyOf's. Everything is
    pre-allocated in prepare(); parameter setters only store scalars. Detector
    maths are kept in double regardless of the sample type being processed.

  ==============================================================================
*/

#include "DynamiteEngine.h"

#include <cmath>

//==============================================================================
DynamiteEngine::DynamiteEngine() = default;

//==============================================================================
void DynamiteEngine::prepare (double newSampleRate, int maximumBlockSize, int numChannelsToUse)
{
    juce::ignoreUnused (maximumBlockSize);

    sampleRate  = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    numChannels = juce::jlimit (1, 2, numChannelsToUse);

    // Look-ahead ~1 ms, clamped to a sane 16..128 sample window.
    const int oneMs = (int) std::lround (sampleRate * 0.001);
    lookaheadSamples = juce::jlimit (16, 128, oneMs);

    // Pre-allocate the (double) look-ahead delay ring. Sized to exactly the
    // delay length: a read-before-write on the same slot yields an N-sample
    // pure delay, independent of the host block size.
    lookaheadBuffer.setSize (numChannels, lookaheadSamples, false, true, true);

    // Detector-only 75 us pre-emphasis high shelf (DS-FM). fc ~= 2.1 kHz,
    // +15 dB plateau, gentle Q. Never touches the audio path.
    for (auto& f : dsFmFilter)
        setHighShelf (f, 2100.0, 15.0, 0.5);

    reset();
}

void DynamiteEngine::reset() noexcept
{
    levelDbv.fill (-120.0);

    for (auto& f : dsFmFilter)
        f.reset();

    lookaheadBuffer.clear();
    lookaheadWritePos = 0;

    meterGrDb.store (0.0f, std::memory_order_relaxed);
    meterOverload.store (false, std::memory_order_relaxed);
}

//==============================================================================
// RBJ cookbook high shelf, normalised by a0 (a0 folded to 1 for the TDF-II
// Biquad in the header). A = sqrt(linear gain); note the +/- 2*sqrt(A)*alpha
// term that distinguishes the shelf from a peaking filter.
void DynamiteEngine::setHighShelf (Biquad& bq, double fc, double gainDb, double q) const noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * juce::MathConstants<double>::pi * fc / sampleRate;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double alpha = sinw0 / (2.0 * q);
    const double sqrtA = std::sqrt (A);
    const double beta  = 2.0 * sqrtA * alpha;

    const double b0 =        A * ((A + 1.0) + (A - 1.0) * cosw0 + beta);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    const double b2 =        A * ((A + 1.0) + (A - 1.0) * cosw0 - beta);
    const double a0 =             (A + 1.0) - (A - 1.0) * cosw0 + beta;
    const double a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * cosw0);
    const double a2 =             (A + 1.0) - (A - 1.0) * cosw0 - beta;

    const double inv = 1.0 / a0;
    bq.b0 = b0 * inv;
    bq.b1 = b1 * inv;
    bq.b2 = b2 * inv;
    bq.a1 = a1 * inv;
    bq.a2 = a2 * inv;
}

//==============================================================================
// Single collapsed static gain computer. Returns gain reduction in dB (>= 0)
// for a detector level already expressed in dBv.
double DynamiteEngine::computeGainReductionDb (double levelDbv_) const noexcept
{
    if (mode == Mode::Bypass)
        return 0.0;

    const double T     = (double) thresholdDbv;
    const double slope = (detector == Detector::Gate) ? 20.0 : 1.0;

    double gr;
    if (mode == Mode::Limit)
    {
        const double over = levelDbv_ - T;          // above threshold
        gr = (over > 0.0) ? slope * over : 0.0;
    }
    else // Mode::Expand
    {
        const double under = T - levelDbv_;          // below threshold
        gr = (under > 0.0) ? slope * under : 0.0;
    }

    const double effRange = isCoupled() ? 60.0 : (double) rangeDb;
    return juce::jlimit (0.0, effRange, gr);
}

//==============================================================================
void DynamiteEngine::process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain)
{
    processInternal<float> (main, sidechain);
}

void DynamiteEngine::process (juce::AudioBuffer<double>& main, const juce::AudioBuffer<double>* sidechain)
{
    processInternal<double> (main, sidechain);
}

//==============================================================================
template <typename FloatT>
void DynamiteEngine::processInternal (juce::AudioBuffer<FloatT>& main, const juce::AudioBuffer<FloatT>* sidechain)
{
    const int numSamples = main.getNumSamples();
    const int nCh        = juce::jmin (main.getNumChannels(), lookaheadBuffer.getNumChannels(), 2);

    if (numSamples <= 0 || nCh <= 0)
        return;

    // ---- Per-block constants (parameters are constant across the block) ----
    const double sr = sampleRate;

    // Fast up / slow down. AVG uses a ~5 ms integration; PEAK & GATE ~50 us.
    const double attackTime  = (detector == Detector::Avg) ? 0.005 : 0.00005;
    const double aAtt        = std::exp (-1.0 / (attackTime            * sr));
    const double aRel        = std::exp (-1.0 / ((double) releaseSeconds * sr));
    const double oneMinusAtt = 1.0 - aAtt;
    const double oneMinusRel = 1.0 - aRel;

    // Threshold/output coupling and make-up.
    const bool   coupled = isCoupled();
    const double T       = (double) thresholdDbv;
    const double outStageDb = coupled
        ? ((double) outputDb - T)
        : ((double) outputDb + ((autoMakeup && mode == Mode::Limit) ? juce::jmax (0.0, -T) : 0.0));
    const double outStageGain = std::pow (10.0, outStageDb / 20.0);

    const double mixWet = (double) mix;
    const double mixDry = 1.0 - mixWet;

    const bool listen = sidechainListen;
    const bool safety = safetyCeiling;
    const bool linked = stereoLink && (nCh > 1);

    const double ceilAbs     = std::pow (10.0, -0.1 / 20.0);   // ~ -0.1 dBFS safety ceiling
    const double overloadAbs = std::pow (10.0, -0.3 / 20.0);   // ~ -0.3 dBFS overload trip

    const int ring = lookaheadSamples;

    // ---- Channel pointers (no allocation) ----
    FloatT*  mainData[2] = { nullptr, nullptr };
    double*  laData[2]   = { nullptr, nullptr };
    for (int c = 0; c < nCh; ++c)
    {
        mainData[c] = main.getWritePointer (c);
        laData[c]   = lookaheadBuffer.getWritePointer (c);
    }

    // External sidechain, with graceful fall-back to the main input if absent
    // or too short. Only fetched when SOURCE == External.
    const FloatT* scData[2] = { nullptr, nullptr };
    const bool haveSc = (source == Source::External
                         && sidechain != nullptr
                         && sidechain->getNumChannels() > 0
                         && sidechain->getNumSamples() >= numSamples);
    if (haveSc)
        for (int c = 0; c < nCh; ++c)
            scData[c] = sidechain->getReadPointer (juce::jmin (c, sidechain->getNumChannels() - 1));

    double blockMaxGr   = 0.0;
    bool   blockOverload = false;

    int writePos = lookaheadWritePos;

    for (int n = 0; n < numSamples; ++n)
    {
        double mainIn[2] = { 0.0, 0.0 };
        double key[2]    = { 0.0, 0.0 };   // source-selected, DS-FM-filtered, undelayed detector signal

        for (int c = 0; c < nCh; ++c)
        {
            const double x = (double) mainData[c][n];
            mainIn[c] = x;

            switch (source)
            {
                case Source::Internal: key[c] = x;                                          break;
                case Source::External: key[c] = haveSc ? (double) scData[c][n] : x;         break;
                case Source::DsFm:     key[c] = dsFmFilter[(size_t) c].processSample (x);    break;
            }
        }

        // ---- Detector level (dBv) -> gain reduction -> linear gain ----
        double g[2] = { 1.0, 1.0 };

        if (linked)
        {
            // Hardware "couple": follow the higher gain change -> the louder key.
            const double km       = juce::jmax (std::abs (key[0]), std::abs (key[1]));
            const double levelNow = 20.0 * std::log10 (km + 1.0e-12) + kFullScaleDbv;

            double env = levelDbv[0];
            env = (levelNow > env) ? (aAtt * env + oneMinusAtt * levelNow)
                                   : (aRel * env + oneMinusRel * levelNow);
            levelDbv[0] = env;
            levelDbv[1] = env;   // keep both coherent for click-free link toggling

            const double gr = computeGainReductionDb (env);
            const double gg = std::pow (10.0, -gr / 20.0);
            g[0] = gg;
            g[1] = gg;

            if (gr > blockMaxGr) blockMaxGr = gr;
        }
        else
        {
            for (int c = 0; c < nCh; ++c)
            {
                const double levelNow = 20.0 * std::log10 (std::abs (key[c]) + 1.0e-12) + kFullScaleDbv;

                double env = levelDbv[(size_t) c];
                env = (levelNow > env) ? (aAtt * env + oneMinusAtt * levelNow)
                                       : (aRel * env + oneMinusRel * levelNow);
                levelDbv[(size_t) c] = env;

                const double gr = computeGainReductionDb (env);
                g[c] = std::pow (10.0, -gr / 20.0);

                if (gr > blockMaxGr) blockMaxGr = gr;
            }
        }

        // ---- Look-ahead delay + VCA + time-aligned dry/wet + output ----
        for (int c = 0; c < nCh; ++c)
        {
            const double delayed = laData[c][writePos];   // input from `ring` samples ago (dry, time-aligned)
            laData[c][writePos]  = mainIn[c];              // push current input

            double outv;
            if (listen)
            {
                // Monitor exactly what drives detection (undelayed, source-selected, DS-FM-filtered).
                outv = key[c];
            }
            else
            {
                // Apply the CURRENT gain to the DELAYED sample so reduction slightly precedes transients.
                const double wet = delayed * g[c] * outStageGain;
                outv = mixWet * wet + mixDry * delayed;
            }

            if (safety)
                outv = juce::jlimit (-ceilAbs, ceilAbs, outv);

            if (std::abs (outv) >= overloadAbs)
                blockOverload = true;

            mainData[c][n] = (FloatT) outv;
        }

        if (++writePos >= ring)
            writePos = 0;
    }

    lookaheadWritePos = writePos;

    // ---- Metering: block max GR + block overload (atomics; GUI reads these) ----
    meterGrDb.store ((float) blockMaxGr, std::memory_order_relaxed);
    meterOverload.store (blockOverload, std::memory_order_relaxed);
}

// Explicit instantiations are unnecessary: both process() overloads above are
// defined in this translation unit and force instantiation of each specialisation.
