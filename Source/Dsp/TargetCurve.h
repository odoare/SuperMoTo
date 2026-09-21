/*
  ------------------------------------------------------------------------------
    TargetCurve.h

    The monitor target curve ("house curve"): the gentle downward tilt a
    corrected system is aimed at, rather than the flat in-room response a
    measurement asks for on its own.

    A loudspeaker whose direct sound is flat does not measure flat in a room.
    Its directivity index rises with frequency (ITU-R BS.1116-3 requires a
    monitor's to, 6-12 dB over 500 Hz - 10 kHz), so the reverberant share of a
    steady-state, spatially averaged measurement falls as frequency rises, and
    absorption takes more off the top. The ear follows the direct sound; the
    microphone average follows the steady state. Flatten the steady state and
    the direct sound has been lifted in the treble by the difference, which is
    what "harsh" sounds like. EBU Tech 3276's tolerance mask lets the room
    response fall at 1 dB/octave above 2 kHz, BS.1116-3's widens at
    1.5 dB/octave, and the Harman in-room target falls about 1 dB/octave across
    the band with a low shelf under ~105 Hz.

    The curve is one object seen twice. Its definition is a cascade of
    first-order pole/zero sections, and its realisation is those same sections
    bilinear-transformed into biquads, so what the GUI plots and what the
    outputs apply cannot drift apart:

      - Six sections carry the tilt, their poles geometric over the band
        widened two octaves at each end and each losing an equal share of the
        drop between its pole and its zero. That is what makes the result a
        straight line in dB against log f: measured against a least-squares
        line it is straight to 0.011 dB at -3 dB of tilt, 0.022 dB at -6 and
        0.036 dB at -10, and it delivers what it is asked for to 0.05 dB (a
        turnover's knee is rounded rather than square, so the fall measured
        from the turnover itself is 0.11 dB short of the figure asked for).
        Shelves were the obvious alternative and are not good enough: one RBJ
        shelf is 1.2 dB off a straight line at only 3 dB of tilt, two are
        0.26 dB off.
      - A seventh section, when asked for, is the low shelf.
      - Two sections pack into one biquad, so a full curve costs four.

    The bilinear transform is the only place the two views differ, and only at
    the top, where the widened pole spread runs past Nyquist and is clamped:
    0.00 dB to 1 kHz, 0.04 at 5 kHz and 0.18 at 20 kHz for a -6 dB tilt at
    44.1 kHz. A turnover, whose slope is steeper, reaches 0.37 dB around
    14 kHz. Well under audibility for a taste curve, and nowhere near the
    several dB the curve exists to correct.

    The realised filter carries one more constant: TargetCurveShape::offsetDb,
    which brings the curve's highest point to 0 dB so the layer only ever
    attenuates and can never clip an output. The master level makes the
    loudness back up.

    Applied identically to every output, which is the point: an identical
    minimum-phase filter on both sides of a crossover cancels out of the
    main-to-subwoofer phase difference, where baking the tilt into the mains'
    correction FIRs alone would not. EBU Tech 3276 asks for the same thing in
    words: "All channels should be adjusted in the same way."

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>     // fxme::Biquad comes via the FxmeTools module umbrella
#include <cmath>
#include <complex>

namespace smt
{

/** The band the curve is defined over, and the band it is normalised in. */
inline constexpr double targetLowHz     = 20.0;
inline constexpr double targetHighHz    = 20000.0;
inline constexpr double targetRefLowHz  = 200.0;
inline constexpr double targetRefHighHz = 2000.0;

inline constexpr int targetTiltSections = 6;                        // first-order
inline constexpr int maxTargetSections  = targetTiltSections + 1;   // + the shelf
inline constexpr int maxTargetBiquads   = (maxTargetSections + 1) / 2;

/** A target curve, as the model stores it. Plain values: it is copied to the
    audio thread with the rest of the engine's settings. */
struct TargetCurve
{
    /** Total drop from 20 Hz to 20 kHz, in dB. Negative falls, which is the
        useful direction; 0 is a flat target. */
    float tiltDb = -4.0f;

    /** Flat below this, with the whole drop spread over [turnoverHz, 20 kHz].
        0 keeps the straight line across the band. Allen's account of the
        X-curve is the argument for moving this, rather than the slope, when a
        room is small or the listener sits close. */
    float turnoverHz = 0.0f;

    /** A first-order low shelf under the tilt: bassDb of gain below bassHz,
        unity above. The Harman target's bass shelf sits near 105 Hz. */
    float bassDb = 0.0f;
    float bassHz = 105.0f;

    // juce::exactlyEqual rather than ==: these are stored values compared for
    // "has the user moved this", not measurements, and the plain operator
    // trips -Wfloat-equal.
    bool operator== (const TargetCurve& o) const noexcept
    {
        return juce::exactlyEqual (tiltDb, o.tiltDb)
            && juce::exactlyEqual (turnoverHz, o.turnoverHz)
            && juce::exactlyEqual (bassDb, o.bassDb)
            && juce::exactlyEqual (bassHz, o.bassHz);
    }
    bool operator!= (const TargetCurve& o) const noexcept   { return ! (*this == o); }

    /** Nothing to apply: the curve is unity everywhere. */
    bool isFlat() const noexcept
    {
        return juce::exactlyEqual (tiltDb, 0.0f) && juce::exactlyEqual (bassDb, 0.0f);
    }
};

//==============================================================================
namespace targetcurve
{

/** One first-order section, as a pole and a zero in Hz. A section is
    (1 + s/wz)/(1 + s/wp): unity at DC and p/z above both corners, so a zero
    above its pole falls and a zero below it rises. */
struct PoleZero
{
    double poleHz = 1.0, zeroHz = 1.0;
};

/** Where the tilt starts falling: the turnover, or the bottom of the band. */
inline double tiltLowHz (const TargetCurve& c) noexcept
{
    return c.turnoverHz > 0.0f
               ? juce::jlimit (targetLowHz, targetHighHz * 0.5, (double) c.turnoverHz)
               : targetLowHz;
}

inline double shelfHz (const TargetCurve& c) noexcept
{
    return juce::jlimit (10.0, 1000.0, (double) c.bassHz);
}

/** The sections a curve is made of, written into `out` (maxTargetSections
    long); returns how many. This is the curve's definition: everything else
    here evaluates or discretises these. */
inline int sectionsOf (const TargetCurve& c, PoleZero* out) noexcept
{
    int n = 0;

    // The tilt. Poles geometric over the band widened by two octaves at each
    // end, so the line stays straight to 20 Hz and 20 kHz instead of bending
    // at the ends, with the drop scaled to the widened span to keep the slope.
    if (! juce::exactlyEqual (c.tiltDb, 0.0f))
    {
        const double lo  = tiltLowHz (c);
        const double flo = lo * 0.25, fhi = targetHighHz * 4.0;
        const double total = -(double) c.tiltDb * std::log2 (fhi / flo)
                                                / std::log2 (targetHighHz / lo);
        const double r = std::pow (10.0, total / (20.0 * targetTiltSections));

        for (int k = 0; k < targetTiltSections; ++k)
        {
            const double p = flo * std::pow (fhi / flo,
                                             ((double) k + 0.5) / targetTiltSections)
                                 / std::sqrt (r);
            out[n++] = { p, p * r };
        }
    }

    // The shelf: bassDb below bassHz, unity above. A pole at bassHz with its
    // zero a factor G above gives 1/G below and 1 above; the missing G is a
    // constant, and constants disappear into the normalisation.
    if (! juce::exactlyEqual (c.bassDb, 0.0f))
    {
        const double f0 = shelfHz (c);
        out[n++] = { f0, f0 * std::pow (10.0, (double) c.bassDb / 20.0) };
    }

    return n;
}

/** |H| of the analog cascade at f, in dB, before normalisation. */
inline double analogDb (const PoleZero* s, int n, double f) noexcept
{
    f = juce::jlimit (0.01, 1.0e6, f);
    double db = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double xz = f / s[i].zeroHz, xp = f / s[i].poleHz;
        db += 10.0 * std::log10 ((1.0 + xz * xz) / (1.0 + xp * xp));
    }
    return db;
}

/** A section as a digital first-order filter: bilinear transform with the
    pre-warp matched at the geometric centre of the pair, {b0, b1} over
    {1, a1}. Corners are clamped below Nyquist, which is what the top-octave
    error in the header comment is. */
struct Digital
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
};

inline Digital digitise (const PoleZero& s, double sr) noexcept
{
    const double limit = 0.47 * sr;
    const double p  = juce::jlimit (0.1, limit, s.poleHz);
    const double z  = juce::jlimit (0.1, limit, s.zeroHz);
    const double fc = juce::jmin (std::sqrt (p * z), limit);

    const double c  = 2.0 * juce::MathConstants<double>::pi * fc
                          / std::tan (juce::MathConstants<double>::pi * fc / sr);
    const double wp = 2.0 * juce::MathConstants<double>::pi * p;
    const double wz = 2.0 * juce::MathConstants<double>::pi * z;

    const double a0 = 1.0 + c / wp;
    return { (1.0 + c / wz) / a0, (1.0 - c / wz) / a0, (1.0 - c / wp) / a0 };
}

/** |H| of a digitised cascade at f, in dB. */
inline double digitalDb (const Digital* s, int n, double f, double sr) noexcept
{
    const std::complex<double> z
        = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / sr);
    std::complex<double> h (1.0, 0.0);
    for (int i = 0; i < n; ++i)
        h *= (s[i].b0 + s[i].b1 * z) / (1.0 + s[i].a1 * z);
    return 20.0 * std::log10 (juce::jmax (1.0e-12, std::abs (h)));
}

/** The mean linear gain of a cascade over the reference band, in dB. The same
    grid for the analog and the digital form, so their normalisations agree. */
template <typename EvalDb>
inline double referenceDb (EvalDb&& db) noexcept
{
    constexpr int n = 33;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f = targetRefLowHz * std::pow (targetRefHighHz / targetRefLowHz,
                                                    (double) i / (double) (n - 1));
        sum += std::pow (10.0, db (f) / 20.0);
    }
    return 20.0 * std::log10 (juce::jmax (1.0e-9, sum / (double) n));
}

} // namespace targetcurve

//==============================================================================
/** A curve with its normalisation worked out: what the GUI plots, and what
    buildTargetCascade() aims the biquads at. Cheap to construct. */
class TargetCurveShape
{
public:
    explicit TargetCurveShape (const TargetCurve& c) : curve (c)
    {
        numSections = targetcurve::sectionsOf (c, sections);

        // Normalised on the mean linear gain over 200 Hz - 2 kHz, the band
        // AnalysisEngine::recomputeCorrection() normalises the response it
        // inverts in, so a target and a measurement are drawn against the same
        // 0 dB.
        refDb = targetcurve::referenceDb ([this] (double f)
                    { return targetcurve::analogDb (sections, numSections, f); });

        // The highest point of the normalised curve: the realised filter is
        // pulled down by it, so it only ever attenuates.
        double peak = 0.0;
        constexpr int n = 97;
        for (int i = 0; i < n; ++i)
        {
            const double f = targetLowHz * std::pow (targetHighHz / targetLowHz,
                                                     (double) i / (double) (n - 1));
            peak = juce::jmax (peak, gainDb (f));
        }
        offset = -peak;
    }

    /** The target's gain at f, in dB: 0 dB on average over 200 Hz - 2 kHz. */
    double gainDb (double f) const noexcept
    {
        return targetcurve::analogDb (sections, numSections, f) - refDb;
    }

    /** The constant attenuation the realised filter carries on top of the
        curve, so that it has gain nowhere in the band. Never positive. */
    double offsetDb() const noexcept            { return offset; }

    const TargetCurve& get() const noexcept     { return curve; }

private:
    TargetCurve curve;
    targetcurve::PoleZero sections[maxTargetSections];
    int numSections = 0;
    double refDb = 0.0;
    double offset = 0.0;
};

//==============================================================================
/** Builds the biquads that realise `c` at `sr`, its normalisation and its
    attenuation folded into the first one, and returns how many were written
    (0 for a flat curve). The caller's array must hold maxTargetBiquads. No
    allocation, and no locks: this runs on the audio thread, once per change
    rather than once per block. */
inline int buildTargetCascade (const TargetCurve& c, double sr,
                               fxme::BiquadCoeffs* out, int maxBiquads) noexcept
{
    if (out == nullptr || maxBiquads <= 0 || sr <= 0.0 || c.isFlat())
        return 0;

    targetcurve::PoleZero pz[maxTargetSections];
    const int numSections = targetcurve::sectionsOf (c, pz);

    targetcurve::Digital d[maxTargetSections];
    for (int i = 0; i < numSections; ++i)
        d[i] = targetcurve::digitise (pz[i], sr);

    // Normalised on the built sections rather than on the definition, so that
    // whatever the bilinear transform did to them cannot move the level: the
    // filter's mean over the reference band is 0 dB, as the curve's is, and
    // the attenuation goes on top of that.
    const TargetCurveShape shape (c);
    const double meanDb = targetcurve::referenceDb ([&d, numSections, sr] (double f)
                              { return targetcurve::digitalDb (d, numSections, f, sr); });
    const double gain = std::pow (10.0, (shape.offsetDb() - meanDb) / 20.0);

    // Two first-order sections per biquad.
    int written = 0;
    for (int i = 0; i < numSections && written < maxBiquads; i += 2)
    {
        fxme::BiquadCoeffs bq;

        if (i + 1 < numSections)
        {
            bq.b0 = (float) (d[i].b0 * d[i + 1].b0);
            bq.b1 = (float) (d[i].b0 * d[i + 1].b1 + d[i].b1 * d[i + 1].b0);
            bq.b2 = (float) (d[i].b1 * d[i + 1].b1);
            bq.a1 = (float) (d[i].a1 + d[i + 1].a1);
            bq.a2 = (float) (d[i].a1 * d[i + 1].a1);
        }
        else
        {
            bq.b0 = (float) d[i].b0;
            bq.b1 = (float) d[i].b1;
            bq.a1 = (float) d[i].a1;
        }

        out[written++] = bq;
    }

    if (written > 0)
    {
        out[0].b0 = (float) ((double) out[0].b0 * gain);
        out[0].b1 = (float) ((double) out[0].b1 * gain);
        out[0].b2 = (float) ((double) out[0].b2 * gain);
    }

    return written;
}

} // namespace smt
