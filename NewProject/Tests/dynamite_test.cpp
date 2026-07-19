/*
  ==============================================================================

    dynamite_test.cpp

    Behavioural bench for DynamiteEngine. It plays steady sine waves across a
    range of levels and checks that the measured output equals what the
    "circuitry" computes, using an INDEPENDENT reference implementation of the
    Valley People Dyna-Mite transfer functions (owner's manual sections 4.1-4.3
    and control-function graphs, Figures 1-5). Agreement between the engine and
    this separately-written reference is the actual proof of correctness.

    It also drives the EXTERNAL side-chain: for EXT modes the gain applied to the
    MAIN signal is determined by the LEVEL of the side-chain signal (ducking when
    LIMIT, keying/envelope-following when EXPAND).

    Calibration: 0 dBFS == +24 dBv (DynamiteEngine::kFullScaleDbv). A sine whose
    PEAK amplitude is a = 10^((D-24)/20) presents a detector level of D dBv.
    PEAK/GATE detection is used throughout so the settled envelope tracks the
    peak and the steady-state output is predictable. (AVG "linear integration"
    intentionally reads below the peak on tonal material, so it is not asserted
    against a peak-domain reference here.)

  ==============================================================================
*/

#include "DynamiteEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    using Source   = DynamiteEngine::Source;
    using Mode     = DynamiteEngine::Mode;
    using Detector = DynamiteEngine::Detector;

    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 256;
    constexpr int    kBlocks     = 400;   // ~2.1 s: ample for attack settling

    int failures = 0;

    struct Cfg
    {
        Source   src;
        Mode     mode;
        Detector det;
        float    thresholdDbv;
        float    rangeDb;
        float    outputDb;
    };

    [[nodiscard]] double dbvToPeak (double dbv)
    {
        return std::pow (10.0, (dbv - DynamiteEngine::kFullScaleDbv) / 20.0);
    }

    [[nodiscard]] bool isCoupled (const Cfg& c)
    {
        return c.mode == Mode::Limit && c.det != Detector::Gate && c.src != Source::External;
    }

    // ---- Independent reference: the transfer function the circuit computes ----
    [[nodiscard]] double refGainReductionDb (const Cfg& c, double levelDbv)
    {
        if (c.mode == Mode::Bypass)
            return 0.0;

        const double slope = (c.det == Detector::Gate) ? 20.0 : 1.0;   // 1:20 gate / 1:-20 duck
        const double gr = (c.mode == Mode::Limit)
                            ? std::max (0.0, levelDbv - (double) c.thresholdDbv) * slope   // above threshold
                            : std::max (0.0, (double) c.thresholdDbv - levelDbv) * slope;  // below threshold

        const double effRange = isCoupled (c) ? 60.0 : (double) c.rangeDb;
        return std::min (std::max (gr, 0.0), effRange);
    }

    [[nodiscard]] double refOutputDbv (const Cfg& c, double mainInDbv, double detectorDbv)
    {
        // Coupled limit: OUTPUT sets the clamped output level (make-up = OUTPUT - THRESHOLD).
        // Otherwise OUTPUT is the nominal VCA gain.
        const double outStage = isCoupled (c) ? ((double) c.outputDb - (double) c.thresholdDbv)
                                              :  (double) c.outputDb;
        return mainInDbv - refGainReductionDb (c, detectorDbv) + outStage;
    }

    void configure (DynamiteEngine& e, const Cfg& c)
    {
        e.setSource (c.src);
        e.setMode (c.mode);
        e.setDetector (c.det);
        e.setThresholdDbv (c.thresholdDbv);
        e.setRangeDb (c.rangeDb);
        e.setOutputDb (c.outputDb);
        e.setReleaseSeconds (0.2f);
        e.setMix (1.0f);
        e.setAutoMakeup (false);
        e.setSidechainListen (false);
        e.setStereoLink (false);
        e.setSafetyCeiling (false);   // measure the true gain, unclamped
    }

    // Drive a steady 1 kHz sine into the main input (and, when useSC, a second
    // 1 kHz sine into the external side-chain), then return the settled MAIN
    // output level in dBv (from the final-block peak).
    [[nodiscard]] double measureOutputDbv (DynamiteEngine& e, const Cfg& c,
                                           double mainInDbv, bool useSC, double scDbv)
    {
        configure (e, c);
        e.reset();

        const double mainPeak = dbvToPeak (mainInDbv);
        const double scPeak   = dbvToPeak (scDbv);
        const double w        = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kSampleRate;

        juce::AudioBuffer<float> main (1, kBlock);
        juce::AudioBuffer<float> sc   (1, kBlock);
        double phase = 0.0, outPeak = 0.0;

        for (int b = 0; b < kBlocks; ++b)
        {
            auto* md = main.getWritePointer (0);
            auto* sd = sc.getWritePointer (0);
            for (int n = 0; n < kBlock; ++n)
            {
                const double s = std::sin (phase);
                md[n] = (float) (mainPeak * s);
                sd[n] = (float) (scPeak   * s);
                phase += w;
                if (phase > 2.0 * juce::MathConstants<double>::pi)
                    phase -= 2.0 * juce::MathConstants<double>::pi;
            }

            e.process (main, useSC ? &sc : nullptr);

            if (b == kBlocks - 1)
                for (int n = 0; n < kBlock; ++n)
                    outPeak = std::max (outPeak, (double) std::abs (md[n]));
        }

        return 20.0 * std::log10 (outPeak + 1.0e-12) + DynamiteEngine::kFullScaleDbv;
    }

    // Sweep a set of levels through one configuration and compare measured vs
    // reference output. For internal modes the swept level IS the input; for
    // side-chain modes the main input is held fixed and the SC level is swept.
    void sweep (const char* title, DynamiteEngine& e, const Cfg& c,
                bool useSC, double fixedMainDbv, const std::vector<double>& levels, double tol)
    {
        std::printf ("\n%s   (tol %.1f dB)\n", title, tol);
        std::printf ("  %-10s %-10s %-12s %-12s %-8s %s\n",
                     useSC ? "SC(dBv)" : "in(dBv)", "main(dBv)", "out(dBv)", "expect(dBv)", "GR(dB)", "");

        for (double lvl : levels)
        {
            const double mainIn = useSC ? fixedMainDbv : lvl;   // detector level == lvl
            const double actual = measureOutputDbv (e, c, mainIn, useSC, useSC ? lvl : 0.0);
            const double expect = refOutputDbv (c, mainIn, lvl);
            const double gr     = refGainReductionDb (c, lvl);
            const bool   ok     = std::abs (actual - expect) <= tol;

            std::printf ("  %+9.1f %+9.1f  %+10.2f  %+10.2f   %6.2f  [%s]\n",
                         lvl, mainIn, actual, expect, gr, ok ? "PASS" : "FAIL");
            if (! ok)
                ++failures;
        }
    }
}

int main()
{
    std::printf ("Dyna-Mite transfer-curve bench  (0 dBFS = +24 dBv)\n");
    std::printf ("Engine output vs independently-computed circuit reference.\n");

    DynamiteEngine engine;
    engine.prepare (kSampleRate, kBlock, 2);

    // GATE modes use a 1:20 / 1:-20 slope which multiplies the peak detector's
    // inherent (<0.1 dB) ripple on a steady tone by 20x, so they get a wider tol.
    constexpr double tol1  = 1.5;   // slope-1 (PEAK/AVG)
    constexpr double tol20 = 3.0;   // slope-20 (GATE)

    std::printf ("\n================= INTERNAL DETECTOR (main drives itself) =================\n");

    // Bypass: unity + OUTPUT.
    sweep ("1) Bypass  (INT, OUT)                       OUTPUT=0",
           engine, { Source::Internal, Mode::Bypass, Detector::Peak, -10.0f, 40.0f, 0.0f },
           false, 0.0, { -40, -20, 0, 6 }, tol1);

    // Coupled inf:1 limiter: output clamps at OUTPUT above threshold, boosted by
    // make-up below it (Fig. 1/2 + manual calibration procedure).
    sweep ("2) Limit inf:1 (INT, LIMIT, PEAK) coupled   T=-10 OUTPUT=0",
           engine, { Source::Internal, Mode::Limit, Detector::Peak, -10.0f, 40.0f, 0.0f },
           false, 0.0, { -30, -20, -10, -5, 0, 6, 12 }, tol1);

    // 1:2 expander: GR = (T - level), capped by RANGE (Fig. 1/3).
    sweep ("3) Expand 1:2 (INT, EXPAND, PEAK)           T=0 RANGE=40 OUTPUT=0",
           engine, { Source::Internal, Mode::Expand, Detector::Peak, 0.0f, 40.0f, 0.0f },
           false, 0.0, { -30, -20, -10, -3, 0, 6 }, tol1);

    // 1:20 gate: GR = 20*(T - level), capped by RANGE.
    sweep ("4) Gate 1:20 (INT, EXPAND, GATE)            T=0 RANGE=40 OUTPUT=0",
           engine, { Source::Internal, Mode::Expand, Detector::Gate, 0.0f, 40.0f, 0.0f },
           false, 0.0, { -6, -3, -1, 0, 3 }, tol20);

    // 1:-20 negative limiting / duck: GR = 20*(level - T), capped by RANGE (Fig. 1).
    sweep ("5) Duck 1:-20 (INT, LIMIT, GATE)            T=0 RANGE=20 OUTPUT=0",
           engine, { Source::Internal, Mode::Limit, Detector::Gate, 0.0f, 20.0f, 0.0f },
           false, 0.0, { -3, 0, 1, 3, 6 }, tol20);

    std::printf ("\n================= EXTERNAL SIDE-CHAIN (SC level drives main gain) ========\n");

    // Soft ducking: main held at -12 dBv; louder SC over threshold ducks it (Fig. 5).
    sweep ("6) Soft duck (EXT, LIMIT, PEAK)             main=-12 T=-10 RANGE=40 OUTPUT=0",
           engine, { Source::External, Mode::Limit, Detector::Peak, -10.0f, 40.0f, 0.0f },
           true, -12.0, { -30, -10, -5, 0, 6 }, tol1);

    // Hard ducking: 1:-20 driven by SC.
    sweep ("7) Hard duck (EXT, LIMIT, GATE)             main=-12 T=-6 RANGE=30 OUTPUT=0",
           engine, { Source::External, Mode::Limit, Detector::Gate, -6.0f, 30.0f, 0.0f },
           true, -12.0, { -20, -6, -5, 0, 6 }, tol20);

    // Soft keying / envelope follow: SC above threshold opens the gain (Fig. 4).
    sweep ("8) Soft key  (EXT, EXPAND, PEAK)            main=-12 T=-6 RANGE=40 OUTPUT=0",
           engine, { Source::External, Mode::Expand, Detector::Peak, -6.0f, 40.0f, 0.0f },
           true, -12.0, { -30, -10, -6, 0, 6 }, tol1);

    // Hard keying: 1:20 driven by SC.
    sweep ("9) Hard key  (EXT, EXPAND, GATE)            main=-12 T=-6 RANGE=40 OUTPUT=0",
           engine, { Source::External, Mode::Expand, Detector::Gate, -6.0f, 40.0f, 0.0f },
           true, -12.0, { -20, -7, -6, 0, 6 }, tol20);

    std::printf ("\n==========================================================================\n");
    std::printf ("%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
