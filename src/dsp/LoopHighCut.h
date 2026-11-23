#pragma once

// HIGH (x0.1..1.0) in a feedback loop as a one-pole low-pass: y += a (x - y), with `a` chosen so the loop's gain at
// kLoopHighRefHz equals HIGH. Used by the FX slot's delay and echo and by the REV5 ER feedback. The REV7 owner's
// manual calls the echo's damping "a low-pass filter in the feedback loop" (p. 14); the law (one pole, HIGH = the
// gain at about 5 kHz) is measured on a third-party SPX90 model (2026-11-22: 500 Hz corner at
// x0.1, 3.3 kHz at x0.55, flat at x1.0). Replaces a 3 kHz high shelf
// (inference).

#include <algorithm>
#include <cmath>

namespace yrev::dsp
{
    inline constexpr double kLoopHighRefHz = 5000.0;
    inline constexpr double kLoopHighThru = 0.999; // HIGH at or above this is a plain loop (a = 1)

    // Coefficient `a` for a one-pole low-pass whose magnitude at kLoopHighRefHz is `high`, at sample rate `fs`.
    inline double loopHighCoef (double high, double fs)
    {
        const double h = std::clamp (high, 0.01, 1.0);
        if (h >= kLoopHighThru || fs <= 0.0)
            return 1.0;
        const double w = std::min (2.0 * 3.14159265358979323846 * kLoopHighRefHz / fs, 3.14159265358979323846 * 0.95);
        // |a / (1 - b e^-jw)|^2 = h^2 with a = 1 - b: (1 - h^2) b^2 - 2 (1 - h^2 cos w) b + (1 - h^2) = 0, smaller root
        const double h2 = h * h, k = 1.0 - h2, m = 1.0 - h2 * std::cos (w);
        const double b = (m - std::sqrt (std::max (0.0, m * m - k * k))) / k;
        return std::clamp (1.0 - b, 1.0e-6, 1.0);
    }
} // namespace yrev::dsp
