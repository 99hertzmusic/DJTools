#pragma once
// DJ Tools: small DSP building blocks (no JUCE). Everything here is allocation-free after prepare().
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dj::dsp
{
constexpr double kPi = 3.14159265358979323846;

template <typename T> inline T clampT (T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Linear ramp that lands exactly on its target (gains, crossfades).
struct Ramp
{
    float v = 0.0f, target = 0.0f, step = 0.001f;
    void setTime (double sr, double seconds) { step = (float) (1.0 / std::max (1.0, seconds * sr)); }
    void reset (float x) { v = target = x; }
    float next()
    {
        if (v < target) v = std::min (target, v + step);
        else if (v > target) v = std::max (target, v - step);
        return v;
    }
    bool settled() const { return v == target; }
};

// One-pole glide with snap (dial values, delay times, cutoffs).
struct Glide
{
    double v = 0.0, t = 0.0, k = 0.01, eps = 1.0e-5;
    void setTime (double sr, double ms) { k = 1.0 - std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }
    void reset (double x) { v = t = x; }
    double next()
    {
        v += k * (t - v);
        if (std::abs (t - v) < eps) v = t;
        return v;
    }
    bool settled() const { return v == t; }
};

// Ring buffer delay. read (d) before write() returns the sample written d samples ago (d >= 1).
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxDelay)
    {
        int n = 1;
        while (n < maxDelay + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    int maxDelay() const { return (int) buf.size() - 3; }
    float read (double d) const
    {
        const int i = (int) d;
        const float fr = (float) (d - i);
        const float a = buf[(size_t) ((w - i) & mask)], b = buf[(size_t) ((w - i - 1) & mask)];
        return a + fr * (b - a);
    }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
};

// Topology-preserving state variable filter (stable under fast cutoff modulation).
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    // g = tan (pi fc / sr), k = 1 / Q
    void process (float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};
inline float svfG (double fc, double sr) { return (float) std::tan (kPi * clampT (fc, 10.0, 0.47 * sr) / sr); }

struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void setLP (double fc, double sr) { a = (float) std::exp (-2.0 * kPi * fc / sr); }
    void reset() { z = 0.0f; }
    float lp (float x) { z = x + a * (z - x); return z; }
    float hp (float x) { return x - lp (x); }
};

// Biquad (transposed direct form II), RBJ cookbook.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    void reset() { s1 = s2 = 0.0f; }
    float process (float x)
    {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void lowPass (double fc, double sr, double q = 0.70710678)
    {
        const double w = 2 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double fc, double sr, double q = 0.70710678)
    {
        const double w = 2 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// Linkwitz-Riley 4th order low-pass (two Butterworth stages).
struct LR4
{
    Biquad a, b;
    void lowPass (double fc, double sr) { a.lowPass (fc, sr); b.lowPass (fc, sr); }
    void reset() { a.reset(); b.reset(); }
    float process (float x) { return b.process (a.process (x)); }
};

// Schroeder all-pass diffuser.
struct Allpass
{
    DelayLine d; int len = 1; float g = 0.5f;
    void prepare (int samples, float gain) { len = std::max (1, samples); d.prepare (len + 2); g = gain; }
    void clear() { d.clear(); }
    float process (float x)
    {
        const float z = d.readInt (len);
        const float v = x + g * z;
        d.write (v);
        return z - g * v;
    }
};

struct Noise
{
    uint32_t s = 0x9E3779B9u;
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) ((double) s / 2147483648.0 - 1.0);
    }
};

inline float softClip (float x)
{
    // Untouched up to 1.5 (+3.5 dBFS), then bends smoothly towards 2.0 (+6 dBFS).
    const float a = std::abs (x);
    if (a <= 1.5f) return x;
    const float y = 1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f);
    return x < 0 ? -y : y;
}

// 8-line feedback delay network reverb with input diffusion. Stereo in / out.
class Fdn
{
public:
    static constexpr int N = 8;
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static const double ms[N] = { 31.7, 37.1, 41.9, 44.3, 49.1, 53.9, 60.7, 67.3 };
        for (int i = 0; i < N; ++i)
        {
            len[i] = std::max (8, (int) std::lround (ms[i] * 0.001 * sr));
            line[i].prepare (len[i] + 2);
            damp[i].reset();
        }
        static const double dms[4] = { 4.7, 3.6, 12.7, 9.3 };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                diff[c][k].prepare ((int) std::lround (dms[k] * (c == 0 ? 1.0 : 1.13) * 0.001 * sr), 0.62f);
        setDecay (2.0, 6000.0, true);
    }
    void clear()
    {
        for (auto& l : line) l.clear();
        for (auto& d : damp) d.reset();
        for (auto& c : diff) for (auto& a : c) a.clear();
    }
    // The decay glides (~40 ms) so changing the room size never steps the tail's level.
    void setDecay (double rt60, double dampHz, bool snap = false)
    {
        rt60 = clampT (rt60, 0.1, 30.0);
        for (int i = 0; i < N; ++i)
        {
            gT[i] = (float) std::pow (10.0, -3.0 * len[i] / (rt60 * sr));
            if (snap) g[i] = gT[i];
            damp[i].setLP (dampHz, sr);
        }
        gk = (float) (1.0 - std::exp (-1.0 / (0.04 * sr)));
    }
    void process (float inL, float inR, float& outL, float& outR)
    {
        for (int k = 0; k < 4; ++k) { inL = diff[0][k].process (inL); inR = diff[1][k].process (inR); }
        float s[N], sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            g[i] += (gT[i] - g[i]) * gk;
            s[i] = damp[i].lp (line[i].readInt (len[i])) * g[i];
            sum += s[i];
        }
        sum *= 2.0f / N;
        for (int i = 0; i < N; ++i) line[i].write (s[i] - sum + ((i & 1) ? inR : inL) * 0.5f);
        outL = (s[0] - s[2] + s[4] - s[6]) * 0.55f;
        outR = (s[1] - s[3] + s[5] - s[7]) * 0.55f;
    }
private:
    double sr = 48000.0;
    DelayLine line[N];
    int len[N] {};
    float g[N] {}, gT[N] {}, gk = 0.001f;
    OnePole damp[N];
    Allpass diff[2][4];
};

// Two-grain delay-line pitch shifter (window ~50 ms).
class PitchShift
{
public:
    void prepare (double sr) { W = 0.05 * sr; for (auto& d : line) d.prepare ((int) W + 8); reset(); }
    void reset() { for (auto& d : line) d.clear(); phase = 0.0; }
    int primeLength() const { return (int) W + 4; }
    void prime (float l, float r) { line[0].write (l); line[1].write (r); }
    void process (float& l, float& r, double ratio)
    {
        line[0].write (l); line[1].write (r);
        phase += (1.0 - ratio) / W;
        phase -= std::floor (phase);
        const double p1 = phase, p2 = std::fmod (phase + 0.5, 1.0);
        const float w1 = (float) std::pow (std::sin (kPi * p1), 2.0), w2 = 1.0f - w1;
        const double d1 = 2.0 + p1 * W, d2 = 2.0 + p2 * W;
        l = w1 * line[0].read (d1) + w2 * line[0].read (d2);
        r = w1 * line[1].read (d1) + w2 * line[1].read (d2);
    }
private:
    DelayLine line[2];
    double W = 2400.0, phase = 0.0;
};
} // namespace dj::dsp
