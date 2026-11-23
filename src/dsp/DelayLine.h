#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

namespace yrev::dsp
{
    namespace delaydetail
    {
        // Large power-of-two buffers start on page boundaries, so lines written in step (the engine writes dozens per
        // sample) would all write the same cache set and evict one another. Each line therefore starts writing at its
        // own offset, a different number of cache lines in (CPU pass, 2026-11-21). Where a line writes has no effect on
        // what it reads back, so the output is unchanged.
        inline int nextStagger () noexcept
        {
            constexpr int kStaggerLines = 37; // odd, so successive lines fall in different sets
            constexpr unsigned kCycle = 4096; // offsets repeat after this many lines (and never overflow)
            static std::atomic<unsigned> counter{0};
            return (int) ((counter.fetch_add (1u, std::memory_order_relaxed) + 1u) % kCycle) * kStaggerLines;
        }
    } // namespace delaydetail

    // Power-of-two circular delay line. Allocates in prepare() only.
    template <typename T> class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 2)
                size <<= 1;
            buffer.assign ((size_t) size, T (0));
            mask = size - 1;
            constexpr int kCacheLine = 64;
            constexpr int kPerLine = kCacheLine / (int) sizeof (T) > 0 ? kCacheLine / (int) sizeof (T) : 1;
            writePos = (delaydetail::nextStagger () * kPerLine) & mask;
        }
        void reset () { std::fill (buffer.begin (), buffer.end (), T (0)); }
        int capacity () const { return mask - 1; }

        // The sample written `d` writes ago, read before this sample's write (d >= 1).
        T readBeforeWrite (int d) const noexcept { return buffer[(size_t) ((writePos - d) & mask)]; }
        // The sample written `d` writes ago, read after this sample's write (d >= 0; 0 is the newest).
        T tap (int d) const noexcept { return buffer[(size_t) ((writePos - 1 - d) & mask)]; }
        void write (T x) noexcept
        {
            buffer[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
        }

    private:
        std::vector<T> buffer;
        int mask = 0, writePos = 0;
    };

    // Schroeder all-pass, y = -g x + s[n-M], s[n] = x + g y.
    template <typename T> class AllPass
    {
    public:
        void prepare (int maxLength) { line.prepare (maxLength); }
        void reset () { line.reset (); }
        void setLength (int samples) { length = std::clamp (samples, 1, line.capacity ()); }
        int getLength () const { return length; }
        void setGain (T g) { gain = g; }

        T process (T x) noexcept
        {
            const T y = -gain * x + line.readBeforeWrite (length);
            line.write (x + gain * y);
            return y;
        }

    private:
        DelayLine<T> line;
        int length = 1;
        T gain = T (0);
    };
} // namespace yrev::dsp
