#pragma once

// Test helpers: octave-band reverb time from Schroeder backward integration, correlation and level. The usual
// room-acoustics definitions.

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/Biquad.h"

namespace yrev::test
{
    // Three cascaded band-passes at `centre`: about an octave wide, ~45 dB down two octaves away, so a fast band
    // is not masked by slower neighbours leaking in.
    inline std::vector<float> octaveBand (const std::vector<float>& x, double fs, float centre)
    {
        dsp::BasicBiquad<double> f[3];
        for (auto& s : f)
            s.setBandPass (fs, centre, 1.0f);
        std::vector<float> y (x.size ());
        for (size_t i = 0; i < x.size (); ++i)
            y[i] = (float) f[2].process (f[1].process (f[0].process ((double) x[i])));
        return y;
    }

    // Energy decay curve in dB (0 dB at the start).
    inline std::vector<double> edc (const std::vector<float>& x)
    {
        std::vector<double> e (x.size ());
        double acc = 0.0;
        for (size_t i = x.size (); i-- > 0;)
        {
            acc += (double) x[i] * x[i];
            e[i] = acc;
        }
        const double total = std::max (e.empty () ? 0.0 : e[0], 1.0e-30);
        for (auto& v : e)
            v = 10.0 * std::log10 (std::max (v / total, 1.0e-30));
        return e;
    }

    // RT60 from a least-squares fit of the EDC between `hiDb` and `loDb` (T20 by default). Returns 0 on failure.
    inline double reverbTime (const std::vector<float>& x, double fs, double hiDb = -5.0, double loDb = -25.0)
    {
        const auto e = edc (x);
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < e.size (); ++i)
            if (e[i] <= hiDb && e[i] >= loDb)
            {
                const double t = (double) i / fs;
                n += 1;
                sx += t;
                sy += e[i];
                sxx += t * t;
                sxy += t * e[i];
            }
        if (n < 10)
            return 0.0;
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per second
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    inline double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = from; i < std::min ({to, a.size (), b.size ()}); ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }
        return ab / std::sqrt (std::max (aa * bb, 1.0e-30));
    }

    inline float peak (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = from; i < std::min (to, x.size ()); ++i)
            m = std::max (m, std::abs (x[i]));
        return m;
    }
} // namespace yrev::test
