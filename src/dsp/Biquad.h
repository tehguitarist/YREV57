#pragma once

#include <cmath>

namespace yrev::dsp
{
    // Small allocation-free biquad (RBJ cookbook, transposed direct form II). Float for the wet-path EQs; the
    // reverb loops use the double version so tiny shelf gains (hundredths of a dB per pass) stay exact.
    template <typename T> class BasicBiquad
    {
    public:
        void reset () { z1 = z2 = T (0); }

        void setPeak (double sampleRate, float freq, float q, float gainDb)
        {
            design (Kind::peak, sampleRate, freq, q, gainDb);
        }
        void setLowShelf (double sampleRate, float freq, float q, float gainDb)
        {
            design (Kind::lowShelf, sampleRate, freq, q, gainDb);
        }
        void setHighShelf (double sampleRate, float freq, float q, float gainDb)
        {
            design (Kind::highShelf, sampleRate, freq, q, gainDb);
        }
        void setLowPass (double sampleRate, float freq, float q) { design (Kind::lowPass, sampleRate, freq, q, 0.0f); }
        void setBandPass (double sampleRate, float freq, float q)
        {
            design (Kind::bandPass, sampleRate, freq, q, 0.0f);
        }
        // Raw coefficients (already normalised by a0).
        void setCoefficients (double nb0, double nb1, double nb2, double na1, double na2)
        {
            b0 = (T) nb0;
            b1 = (T) nb1;
            b2 = (T) nb2;
            a1 = (T) na1;
            a2 = (T) na2;
        }
        void setBypass ()
        {
            b0 = T (1);
            b1 = b2 = a1 = a2 = T (0);
        }

        // Magnitude of the current response at `hz` (not for the audio thread's hot path).
        double magnitude (double sampleRate, double hz) const
        {
            const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
            const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            const double nr = (double) b0 + (double) b1 * c1 + (double) b2 * c2,
                         ni = -(double) b1 * s1 - (double) b2 * s2;
            const double dr = 1.0 + (double) a1 * c1 + (double) a2 * c2, di = -(double) a1 * s1 - (double) a2 * s2;
            return std::sqrt ((nr * nr + ni * ni) / std::fmax (dr * dr + di * di, 1.0e-300));
        }

        T process (T x) noexcept
        {
            const T y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        enum class Kind
        {
            peak,
            lowShelf,
            highShelf,
            lowPass,
            bandPass
        };

        void design (Kind kind, double sampleRate, float freq, float q, float gainDb)
        {
            const double f = std::fmin (std::fmax ((double) freq, 10.0), sampleRate * 0.45);
            const double A = std::pow (10.0, (double) gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * f / sampleRate;
            const double cs = std::cos (w0), sn = std::sin (w0);
            const double alpha = sn / (2.0 * std::fmax ((double) q, 0.05));
            double nb0, nb1, nb2, na0, na1, na2;
            if (kind == Kind::lowPass || kind == Kind::bandPass)
            {
                nb0 = kind == Kind::lowPass ? (1.0 - cs) * 0.5 : alpha; // band-pass: 0 dB peak gain
                nb1 = kind == Kind::lowPass ? 1.0 - cs : 0.0;
                nb2 = kind == Kind::lowPass ? (1.0 - cs) * 0.5 : -alpha;
                na0 = 1.0 + alpha;
                na1 = -2.0 * cs;
                na2 = 1.0 - alpha;
            }
            else if (kind == Kind::peak)
            {
                nb0 = 1.0 + alpha * A;
                nb1 = -2.0 * cs;
                nb2 = 1.0 - alpha * A;
                na0 = 1.0 + alpha / A;
                na1 = -2.0 * cs;
                na2 = 1.0 - alpha / A;
            }
            else
            {
                const double beta = 2.0 * std::sqrt (A) * alpha;
                if (kind == Kind::lowShelf)
                {
                    nb0 = A * ((A + 1.0) - (A - 1.0) * cs + beta);
                    nb1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
                    nb2 = A * ((A + 1.0) - (A - 1.0) * cs - beta);
                    na0 = (A + 1.0) + (A - 1.0) * cs + beta;
                    na1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
                    na2 = (A + 1.0) + (A - 1.0) * cs - beta;
                }
                else
                {
                    nb0 = A * ((A + 1.0) + (A - 1.0) * cs + beta);
                    nb1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
                    nb2 = A * ((A + 1.0) + (A - 1.0) * cs - beta);
                    na0 = (A + 1.0) - (A - 1.0) * cs + beta;
                    na1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
                    na2 = (A + 1.0) - (A - 1.0) * cs - beta;
                }
            }
            b0 = (T) (nb0 / na0);
            b1 = (T) (nb1 / na0);
            b2 = (T) (nb2 / na0);
            a1 = (T) (na1 / na0);
            a2 = (T) (na2 / na0);
        }

        T b0 = T (1), b1 = T (0), b2 = T (0), a1 = T (0), a2 = T (0);
        T z1 = T (0), z2 = T (0);
    };

    using Biquad = BasicBiquad<float>;
} // namespace yrev::dsp
