// DJ Tools offline test app.
// Usage:
//   DJToolsTest <outdir> [quick] [pro=KEY trial=KEY expired=KEY wrong=KEY all=KEY]   all tests + UI snapshots
//   DJToolsTest demos <outdir> <bpm> a.wav b.wav ...                                   every effect on the mixed stems
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <functional>

using APP = DJToolsAudioProcessor;
using Buf = juce::AudioBuffer<float>;
static int failures = 0, passes = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
    if (ok) ++passes; else ++failures;
}
static const double twoPi = juce::MathConstants<double>::twoPi;

// ── Host stand-in ────────────────────────────────────────────────────────────
struct Head : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

static void setLicence (APP& p, hz::LicenceInfo::Kind kind)
{
    hz::LicenceInfo li; li.kind = kind; li.email = "buyer@example.com";
    li.expires = kind == hz::LicenceInfo::trial ? juce::Time::currentTimeMillis() / 1000 + 5 * 86400 - 60 : 0;
    p.setLicenceInfo (li);
}

struct Rig
{
    std::unique_ptr<APP> p;
    Head head;
    double sr, ppq0;
    int bs;
    juce::int64 done = 0;
    Rig (hz::LicenceInfo::Kind lic = hz::LicenceInfo::pro, double sampleRate = 48000.0, int block = 256, double bpm = 120.0, double startPpq = 0.0)
        : sr (sampleRate), ppq0 (startPpq), bs (block)
    {
        p = std::make_unique<APP>();
        setLicence (*p, lic);
        head.bpm = bpm;
        p->setPlayHead (&head);
        p->setRateAndBufferSizeDetails (sr, bs);
        p->prepareToPlay (sr, bs);
    }
    void set (const char* id, float plain)
    {
        auto* prm = p->apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    }
    float get (const char* id) { return p->apvts.getRawParameterValue (id)->load(); }
    // Process `len` samples of `in` (looped, aligned to the absolute position). `atBlock` runs before each block.
    Buf run (const Buf& in, int len, std::function<void (juce::int64)> atBlock = {})
    {
        Buf out (2, len), blk (2, bs);
        juce::MidiBuffer m;
        for (int pos = 0; pos < len; pos += bs)
        {
            const int n = juce::jmin (bs, len - pos);
            blk.setSize (2, n, false, false, true);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) blk.setSample (c, i, in.getSample (c, (int) ((done + i) % in.getNumSamples())));
            if (atBlock) atBlock (done);
            head.ppq = ppq0 + (double) done * head.bpm / (60.0 * sr);
            p->processBlock (blk, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            done += n;
        }
        return out;
    }
    // The input as the plugin saw it for the last `len` samples.
    Buf lastInput (const Buf& in, int len) const
    {
        Buf b (2, len);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) b.setSample (c, i, in.getSample (c, (int) ((done - len + i) % in.getNumSamples())));
        return b;
    }
    double spb() const { return 60.0 / head.bpm * sr; }
};

// ── Signals ──────────────────────────────────────────────────────────────────
static float kickAt (double t)
{
    if (t < 0 || t > 0.35) return 0.0f;
    const double f0 = 48.0, f1 = 170.0, k = 30.0;
    const double ph = twoPi * (f0 * t + (f1 - f0) * (1 - std::exp (-k * t)) / k);
    return (float) (std::sin (ph) * std::exp (-t / 0.11));
}

// Drums + bass + pad, peaks at -6 dBFS.
static Buf music (double secs, double sr = 48000.0, double bpm = 124.0)
{
    const int n = (int) (secs * sr);
    Buf b (2, n); b.clear();
    juce::Random rnd (42);
    const double step = 60.0 / bpm / 4.0;
    const int steps = (int) (secs / step) + 1;
    float hpPrev = 0;
    for (int s = 0; s < steps; ++s)
    {
        const int pos16 = s % 16;
        const float vel = 0.8f + 0.2f * rnd.nextFloat();
        const int start = (int) (s * step * sr);
        const bool kick = pos16 % 4 == 0, snare = pos16 == 4 || pos16 == 12, hat = pos16 % 2 == 1;
        for (int i = 0; i < (int) (0.4 * sr) && start + i < n; ++i)
        {
            const double t = i / sr;
            float l = 0, r = 0;
            if (kick) { const float k = 0.85f * vel * kickAt (t); l += k; r += k; }
            if (snare && t < 0.25)
            {
                const float nz = (rnd.nextFloat() * 2 - 1) * (float) std::exp (-t / 0.07) * 0.45f;
                const float tone = (float) (std::sin (twoPi * 190.0 * t) * std::exp (-t / 0.035)) * 0.35f;
                l += vel * (nz + tone); r += vel * (nz * 0.9f + tone);
            }
            if (hat && t < 0.08)
            {
                const float w = rnd.nextFloat() * 2 - 1; const float hp = w - hpPrev; hpPrev = w;
                const float h = hp * (float) std::exp (-t / 0.012) * 0.16f * vel;
                l += h * 0.7f; r += h;
            }
            b.addSample (0, start + i, l); b.addSample (1, start + i, r);
        }
    }
    const double bar = 4 * 60.0 / bpm;
    const double notes[] = { 55.0, 55.0, 73.42, 49.0 };
    const double chord[] = { 220.0, 261.63, 329.63, 392.0 };
    double bph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = notes[(int) (t / bar) % 4];
        bph += f / sr;
        double saw = 0; for (int k = 1; k <= 8; ++k) saw += std::sin (twoPi * k * bph) / k;
        const double noteT = std::fmod (t, bar / 2);
        const double env = std::min (1.0, noteT / 0.008) * (noteT > bar / 2 - 0.03 ? (bar / 2 - noteT) / 0.03 : 1.0);
        double pad = 0; for (int k = 0; k < 4; ++k) pad += std::sin (twoPi * chord[k] * t) + std::sin (twoPi * chord[k] * 1.004 * t + k);
        const float bass = (float) (0.25 * saw * env), pd = (float) (0.03 * pad);
        b.addSample (0, i, bass + pd); b.addSample (1, i, bass + pd * 0.9f);
    }
    b.applyGain (0.5f / b.getMagnitude (0, n));
    return b;
}

static Buf sine (double secs, double f = 220.0, float amp = 0.25f, double sr = 48000.0)
{
    const int n = (int) (secs * sr);
    Buf b (2, n);
    for (int i = 0; i < n; ++i) { const float v = amp * (float) std::sin (twoPi * f * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}

static Buf noise (double secs, float amp = 0.3f, double sr = 48000.0, int seed = 7)
{
    const int n = (int) (secs * sr);
    Buf b (2, n);
    juce::Random r (seed);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, amp * (r.nextFloat() * 2 - 1));
    return b;
}

static Buf silence (int n) { Buf b (2, n); b.clear(); return b; }

// ── Measurements ─────────────────────────────────────────────────────────────
static float peak (const Buf& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    float p = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) p = juce::jmax (p, std::abs (b.getSample (c, i)));
    return p;
}
static double rmsDb (const Buf& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    double s = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return 10 * std::log10 (s / juce::jmax (1, 2 * (to - from)) + 1e-30);
}
// Energy of (out - in) relative to the input, dB.
static double diffDb (const Buf& in, const Buf& out, int from = 0, int to = -1)
{
    if (to < 0) to = out.getNumSamples();
    double e = 0, s = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < to; ++i)
        {
            const double x = in.getSample (c, i % in.getNumSamples()), y = out.getSample (c, i);
            e += (y - x) * (y - x); s += x * x;
        }
    return 10 * std::log10 (e / juce::jmax (1e-30, s) + 1e-30);
}
static bool bitExact (const Buf& in, const Buf& out, int len)
{
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < len; ++i)
            if (std::memcmp (in.getReadPointer (c) + (i % in.getNumSamples()), out.getReadPointer (c) + i, sizeof (float)) != 0) return false;
    return true;
}
static bool healthy (const Buf& b)
{
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (c, i);
            if (! std::isfinite (x) || std::fpclassify (x) == FP_SUBNORMAL) return false;
        }
    return true;
}
// Largest second difference: a click shows up as a spike far above the signal's own curvature.
static float maxD2 (const Buf& b, int from, int to)
{
    float m = 0;
    from = juce::jmax (2, from); to = juce::jmin (to, b.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < to; ++i)
            m = juce::jmax (m, std::abs (b.getSample (c, i) - 2 * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
    return m;
}

static const char* colorIds[] = { "FILTER", "SWEEP", "DUB ECHO", "SPACE", "NOISE", "CRUSH" };

// ── 1. Every effect makes sound, stays safe ─────────────────────────────────
static void effectTests()
{
    std::printf ("\n== Every effect: output, level safety, NaN / denormal ==\n");
    const auto mus = music (4.0);
    const int len = (int) (3.0 * 48000);
    for (int fx = 0; fx < dj::numColorFx; ++fx)
        for (float dial : { -70.0f, 70.0f, -100.0f, 100.0f })
        {
            float worst = 0; double minDiff = 1e9; bool ok = true;
            for (float prm : { 0.0f, 50.0f, 100.0f })
            {
                Rig r;
                r.set ("color_fx", (float) fx); r.set ("color", dial); r.set ("color_param", prm);
                auto out = r.run (mus, len);
                auto tail = r.run (silence (48000), 3 * 48000);   // silence after: tails must die cleanly
                worst = juce::jmax (worst, peak (out), peak (tail));
                minDiff = juce::jmin (minDiff, diffDb (mus, out, 4800, len));
                ok = ok && healthy (out) && healthy (tail);
            }
            check (ok && minDiff > -35.0 && worst <= 2.0f,
                   juce::String ("COLOR ") + colorIds[fx] + " dial " + juce::String ((int) dial) + ": changes the sound (min diff "
                       + juce::String (minDiff, 1) + " dB), peak " + juce::String (juce::Decibels::gainToDecibels (worst), 1) + " dBFS, finite, no denormals");
        }
    for (int fx = 0; fx < dj::numBeatFx; ++fx)
    {
        float worst = 0; double minDiff = 1e9; bool ok = true;
        for (float lev : { 40.0f, 100.0f })
            for (int div : { 1, 3, 5, 7 })
            {
                Rig r;
                r.set ("beat_fx", (float) fx); r.set ("beat", (float) div);
                r.set ("level", fx == dj::bPitch ? (lev > 50 ? 90.0f : 15.0f) : lev);
                r.set ("beat_on", 1.0f);
                auto out = r.run (mus, len);
                r.set ("beat_on", 0.0f);
                auto tail = r.run (silence (48000), 3 * 48000);
                worst = juce::jmax (worst, peak (out), peak (tail));
                minDiff = juce::jmin (minDiff, diffDb (mus, out, 24000, len));
                ok = ok && healthy (out) && healthy (tail);
            }
        check (ok && minDiff > -30.0 && worst <= 2.0f,
               juce::String ("BEAT ") + dj::beatName (fx) + ": changes the sound (min diff " + juce::String (minDiff, 1)
                   + " dB), peak " + juce::String (juce::Decibels::gainToDecibels (worst), 1) + " dBFS, finite, no denormals");
    }
    // Hot input + everything at once
    {
        Rig r;
        auto hot = music (4.0); hot.applyGain (2.0f);   // 0 dBFS peaks
        r.set ("color_fx", dj::cFilter); r.set ("color", -60.0f); r.set ("color_param", 100.0f);
        r.set ("beat_fx", dj::bHelix); r.set ("level", 100.0f); r.set ("beat_on", 1.0f);
        auto out = r.run (hot, 8 * 48000);
        check (healthy (out) && peak (out) <= 2.0f, "0 dBFS input, max resonance + HELIX at 100% for 8 s: peak "
               + juce::String (juce::Decibels::gainToDecibels (peak (out)), 1) + " dBFS (limit +6)");
    }
    // Other rates / odd block sizes
    {
        bool ok = true; juce::String bad;
        for (auto cfg : { std::pair<double, int> { 44100.0, 33 }, { 96000.0, 512 }, { 192000.0, 64 } })
            for (int fx = 0; fx < dj::numBeatFx; ++fx)
            {
                Rig r (hz::LicenceInfo::pro, cfg.first, cfg.second, 128.0);
                const auto m = music (1.5, cfg.first);
                r.set ("beat_fx", (float) fx); r.set ("beat_on", 1.0f); r.set ("color", fx % 2 ? 60.0f : -60.0f); r.set ("color_fx", (float) (fx % 6));
                if (fx == dj::bPitch) r.set ("level", 80.0f);
                auto out = r.run (m, (int) (1.5 * cfg.first));
                const bool good = healthy (out) && peak (out) <= 2.0f && diffDb (m, out, (int) (0.5 * cfg.first)) > -40.0;
                if (! good) bad << dj::beatName (fx) << "@" << (int) cfg.first << " ";
                ok = ok && good;
            }
        check (ok, "44.1 kHz / 33-sample blocks, 96 kHz / 512, 192 kHz / 64: every BEAT effect (+ COLOR) works and stays safe" + (bad.isEmpty() ? "" : "  " + bad));
    }
}

// ── 2. Beat-synced timing on the host grid ───────────────────────────────────
static int argMax (const Buf& b, int ch, int from, int to)
{
    int best = from; float bv = -1;
    for (int i = from; i < juce::jmin (to, b.getNumSamples()); ++i) if (std::abs (b.getSample (ch, i)) > bv) { bv = std::abs (b.getSample (ch, i)); best = i; }
    return best;
}
static float lerpAt (const Buf& b, int ch, double pos)
{
    const int i = (int) std::floor (pos);
    const float fr = (float) (pos - i);
    const int n = b.getNumSamples();
    const float a = b.getSample (ch, i % n);
    return fr == 0.0f ? a : a + fr * (b.getSample (ch, (i + 1) % n) - a);
}

static void timingTests()
{
    std::printf ("\n== Beat-synced timing (120 BPM, 48 kHz and 130 BPM, 44.1 kHz) ==\n");
    for (int fx : { dj::bDelay, dj::bEcho, dj::bPingPong, dj::bHelix, dj::bSpiral })
    {
        bool ok = true; juce::String worst; int cases = 0;
        for (int div = 0; div <= 7; ++div)
            for (auto cfg : { std::pair<double, double> { 48000.0, 120.0 }, { 44100.0, 130.0 } })
            {
                Rig r (hz::LicenceInfo::pro, cfg.first, 256, cfg.second, 3.37);
                const double D = dj::divisionBeats[div] * r.spb();
                if (D > 3.9 * cfg.first) continue;
                ++cases;
                const int at = 3000, len = at + (int) (2.2 * D) + 4000;
                Buf in (2, len); in.clear(); in.setSample (0, at, 0.8f); in.setSample (1, at, 0.8f);
                r.set ("beat_fx", (float) fx); r.set ("beat", (float) div); r.set ("level", 50.0f); r.set ("beat_on", 1.0f);
                auto out = r.run (in, len);
                const int pk = argMax (out, 0, at + 8, at + (int) (1.5 * D) + 64);
                const double err = (pk - at) - D;
                if (std::abs (err) > 0.51) { ok = false; worst << dj::divisionName (div) << "@" << (int) cfg.first << " off by " << juce::String (err, 2) << " smp; "; }
                if (fx == dj::bPingPong)
                {
                    const int pr = argMax (out, 1, at + (int) (1.5 * D), at + (int) (2.5 * D) + 64);
                    // 2nd repeat has been through the feedback filters once (under 1 sample of group delay)
                    if (std::abs ((pr - at) - 2 * D) > 1.5) { ok = false; worst << "pingpong R " << juce::String ((pr - at) - 2 * D, 2) << "; "; }
                }
            }
        check (ok && cases >= 14, juce::String (dj::beatName (fx)) + ": first repeat lands one BEAT division later to the nearest sample (1/16 .. 4 beats, "
               + juce::String (cases) + " cases" + (fx == dj::bPingPong ? ", 2nd repeat on the right at 2x" : "") + ")" + (worst.isEmpty() ? "" : "  " + worst));
    }

    // TIME override in ms
    {
        Rig r (hz::LicenceInfo::pro, 48000.0, 256, 120.0);
        const int at = 1000, len = 30000;
        Buf in (2, len); in.clear(); in.setSample (0, at, 0.8f); in.setSample (1, at, 0.8f);
        r.set ("beat_fx", dj::bDelay); r.set ("time", 250.0f); r.set ("beat_on", 1.0f);
        auto out = r.run (in, len);
        const int pk = argMax (out, 0, at + 8, len);
        check (pk - at == 12000, "TIME 250 ms overrides BEAT: repeat at " + juce::String (pk - at) + " samples (want 12000)");
    }

    // TRANS: the cut starts exactly on the grid line (host starts part-way into a beat)
    {
        bool ok = true; juce::String info;
        const double p0 = 0.37013;
        for (int div : { 1, 2, 3, 5 })
        {
            Rig r (hz::LicenceInfo::pro, 48000.0, 128, 120.0, p0);
            Buf dc (2, 48000); for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (dc.getWritePointer (c), 0.5f, 48000);
            r.set ("beat_fx", dj::bTrans); r.set ("beat", (float) div); r.set ("level", 100.0f); r.set ("beat_on", 1.0f);
            auto out = r.run (dc, 96000);
            const double period = dj::divisionBeats[div], spb = r.spb();
            const double startBeat = p0 + 0.2 * 2.0;   // look after 0.2 s
            const double cutBeat = (std::floor ((startBeat - period / 2) / period) + 1) * period + period / 2;
            const double cutSample = (cutBeat - p0) * spb;
            const int ci = (int) std::ceil (cutSample);
            const bool before = std::abs (out.getSample (0, ci - 1) - 0.5f) < 1e-6f;
            const bool after = out.getSample (0, ci) < 0.5f - 1e-4f && out.getSample (0, ci + 60) < 1e-6f;
            if (! (before && after)) { ok = false; info << dj::divisionName (div) << " "; }
        }
        check (ok, "TRANS: gate closes on the exact sample of the host grid line (host started at beat 0.37; 1/8, 1/4, 1/2, 1 beat)" + (info.isEmpty() ? "" : " failed: " + info));
    }

    // ROLL: loop length exact and the loop starts at the grid line before ON
    for (auto cfg : { std::pair<double, double> { 48000.0, 120.0 }, { 44100.0, 130.0 } })
    {
        bool ok = true; juce::String info; double worstErr = 0;
        for (int div : { 0, 2, 3, 4, 5, 6 })
        {
            Rig r (hz::LicenceInfo::pro, cfg.first, 256, cfg.second, 0.0);
            const auto nz = noise (12.0, 0.3f, cfg.first);
            const double spb = r.spb();
            const double L = dj::divisionBeats[div] * spb;
            const juce::int64 onAt = (juce::int64) std::llround (2.0 * spb + 0.37 * L) / 256 * 256;   // ON part-way into a division
            r.set ("beat_fx", dj::bRoll); r.set ("beat", (float) div); r.set ("level", 100.0f);
            const int len = (int) (onAt + 4 * L + 4096);
            auto out = r.run (nz, len, [&] (juce::int64 pos) { if (pos == onAt) r.set ("beat_on", 1.0f); });
            const double onBeat = (double) onAt / spb;
            const double gridBeat = std::floor (onBeat / dj::divisionBeats[div] + 1e-9) * dj::divisionBeats[div];
            const auto gridS = onAt - (juce::int64) std::llround ((onBeat - gridBeat) * spb);
            const int F = juce::jmin ((int) (0.003 * cfg.first), (int) (L / 4));
            double maxErr = 0; int compared = 0;
            for (int k = 1; k <= 2; ++k)
                for (juce::int64 s = gridS + (juce::int64) std::ceil (k * L); s < gridS + (juce::int64) std::floor ((k + 1) * L); ++s)
                {
                    const double rel = (double) (s - gridS) - k * L;
                    if (rel < F + 2 || rel > L - 2) continue;
                    if (s < onAt + (juce::int64) (0.01 * cfg.first)) continue;   // still inside the 8 ms ON fade
                    const float want = lerpAt (nz, 0, (double) gridS + rel);
                    maxErr = juce::jmax (maxErr, (double) std::abs (out.getSample (0, (int) s) - want));
                    ++compared;
                }
            worstErr = juce::jmax (worstErr, maxErr);
            if (maxErr > 1e-5 || compared < 50) { ok = false; info << dj::divisionName (div) << " err " << juce::String (maxErr, 6) << " n=" << compared << "; "; }
        }
        check (ok, "ROLL @" + juce::String ((int) cfg.first) + " Hz / " + juce::String ((int) cfg.second)
               + " BPM: loop = audio from the grid line before ON, repeating every division exactly (1/16 .. 2 beats, max err "
               + juce::String (worstErr, 7) + ")" + (info.isEmpty() ? "" : "  " + info));
    }

    // SLIP ROLL: each beat repeats its own first slice (follows the track)
    {
        Rig r (hz::LicenceInfo::pro, 48000.0, 256, 120.0, 0.0);
        const auto nz = noise (6.0);
        r.set ("beat_fx", dj::bSlipRoll); r.set ("beat", 2.0f); r.set ("level", 100.0f); r.set ("beat_on", 1.0f);
        auto out = r.run (nz, 5 * 24000);
        double maxErr = 0;
        for (int beat = 1; beat <= 3; ++beat)
            for (int rep = 1; rep < 4; ++rep)
                for (int j = 200; j < 5800; ++j)
                {
                    const int pos = beat * 24000 + rep * 6000 + j;
                    maxErr = juce::jmax (maxErr, (double) std::abs (out.getSample (0, pos) - nz.getSample (0, beat * 24000 + j)));
                }
        check (maxErr < 1e-5, "SLIP ROLL 1/4: every beat re-grabs fresh audio and repeats its first quarter (max err " + juce::String (maxErr, 7) + ")");
    }

    // Beat FILTER / FLANGER / PHASER: the sweep is locked to the host grid (not to when ON was pressed)
    for (int fx : { dj::bFilter, dj::bFlanger, dj::bPhaser })
    {
        Rig a (hz::LicenceInfo::pro, 48000.0, 256, 120.0, 0.0), b (hz::LicenceInfo::pro, 48000.0, 256, 120.0, 1.0);
        const auto nz = noise (4.0);
        Buf shifted (2, nz.getNumSamples() - 24000);
        for (int c = 0; c < 2; ++c) shifted.copyFrom (c, 0, nz, c, 24000, shifted.getNumSamples());
        for (auto* r : { &a, &b }) { r->set ("beat_fx", (float) fx); r->set ("beat", 5.0f); r->set ("level", 100.0f); r->set ("beat_on", 1.0f); }
        auto oa = a.run (nz, 3 * 48000), ob = b.run (shifted, 3 * 48000 - 24000);
        double e = 0, s = 0;
        for (int i = 72000; i < 3 * 48000; ++i) { const double d = oa.getSample (0, i) - ob.getSample (0, i - 24000); e += d * d; s += oa.getSample (0, i) * oa.getSample (0, i); }
        const double db = 10 * std::log10 (e / s + 1e-30);
        check (db < -60.0, juce::String (dj::beatName (fx)) + ": sweep position follows the host grid (two hosts, different start: mismatch " + juce::String (db, 1) + " dB)");
    }

    // Standalone / stopped transport: free-running clock keeps going
    {
        Rig r (hz::LicenceInfo::pro, 48000.0, 256, 128.0);
        r.head.playing = false;
        const auto mus = music (2.0);
        r.set ("beat_fx", dj::bTrans); r.set ("beat", 3.0f); r.set ("level", 100.0f); r.set ("beat_on", 1.0f);
        auto out = r.run (mus, 96000);
        check (diffDb (mus, out, 24000) > -12.0 && std::abs (r.p->lastBpm.load() - 128.0) < 1e-9,
               "Host stopped: effects keep running on a free-running grid at the host tempo");
    }
}

// ── 3. Tails ring out ────────────────────────────────────────────────────────
static void tailTests()
{
    std::printf ("\n== Tails ring out after OFF / when the input stops ==\n");
    const auto mus = music (2.0);
    for (int fx : { dj::bDelay, dj::bEcho, dj::bPingPong, dj::bReverb, dj::bHelix, dj::bSpiral })
    {
        Rig r;
        r.set ("beat_fx", (float) fx); r.set ("beat", 3.0f); r.set ("level", 50.0f); r.set ("beat_on", 1.0f);
        r.run (mus, 48000);
        r.set ("beat_on", 0.0f);
        auto out = r.run (silence (48000), 48000);
        const double tail = rmsDb (out, 4800, 48000);
        r.run (mus, 25 * 48000);
        const auto quiet = r.run (mus, 48000);
        check (tail > -50.0 && bitExact (r.lastInput (mus, 48000), quiet, 48000),
               juce::String (dj::beatName (fx)) + ": after OFF (input silent) the tail rings at " + juce::String (tail, 1)
                   + " dB RMS over the next second, then the plugin goes back to bit-exact");
    }
    {
        Rig r;
        r.set ("beat_fx", dj::bEcho); r.set ("beat_on", 1.0f);
        r.run (mus, 48000);
        auto out = r.run (silence (48000), 48000);
        check (rmsDb (out, 4800, 48000) > -45.0, "ECHO: input stops while ON, the repeats carry on (" + juce::String (rmsDb (out, 4800, 48000), 1) + " dB)");
    }
    {
        Rig r;
        r.set ("beat_fx", dj::bRoll); r.set ("beat_on", 1.0f); r.set ("level", 100.0f);
        r.run (mus, 48000);
        r.set ("beat_on", 0.0f);
        auto out = r.run (mus, 48000);
        const auto in = r.lastInput (mus, 48000);
        check (diffDb (in, out, 960, 48000) < -200.0, "ROLL (insert effect): OFF goes straight back to the live track after an 8 ms fade");
    }
    {
        Rig r;
        r.set ("beat_fx", dj::bEcho); r.set ("beat_on", 1.0f); r.set ("level", 50.0f);
        r.run (mus, 48000);
        r.set ("beat_fx", dj::bTrans);
        r.set ("beat_on", 0.0f);
        auto out = r.run (silence (48000), 48000);
        check (rmsDb (out, 4800, 48000) > -45.0, "Switching from ECHO to another effect lets the echo tail ring out");
    }
    for (int fx : { dj::cDubEcho, dj::cSpace })
    {
        Rig r;
        r.set ("color_fx", (float) fx); r.set ("color", -70.0f); r.set ("color_param", 60.0f);
        r.run (mus, 48000);
        r.set ("color", 0.0f);
        auto out = r.run (silence (48000), 48000);
        r.run (mus, 25 * 48000);
        const auto quiet = r.run (mus, 48000);
        check (rmsDb (out, 4800, 48000) > -50.0 && bitExact (r.lastInput (mus, 48000), quiet, 48000),
               juce::String ("COLOR ") + colorIds[fx] + ": dial back to centre, tail rings on (" + juce::String (rmsDb (out, 4800, 48000), 1) + " dB), then bit-exact");
    }
}

// ── 4. COLOR dead zone ───────────────────────────────────────────────────────
static void deadZoneTests()
{
    std::printf ("\n== COLOR centre dead zone = bit-exact passthrough ==\n");
    const auto mus = music (2.0);
    for (int fx = 0; fx < dj::numColorFx; ++fx)
    {
        bool ok = true;
        // MIDI 63 and 64 of 127 land at -0.79 and +0.79 on the -100..100 dial
        for (float dial : { 0.0f, 0.79f, -0.79f, 2.5f, -4.9f, 4.9f })
        {
            Rig r;
            r.set ("color_fx", (float) fx); r.set ("color", dial); r.set ("color_param", 100.0f);
            auto out = r.run (mus, 48000);
            ok = ok && bitExact (mus, out, 48000);
        }
        {
            Rig r;
            r.set ("color_fx", (float) fx); r.set ("color", 80.0f);
            r.run (mus, 24000);
            r.set ("color", 0.79f);
            r.run (mus, 25 * 48000);
            const auto out = r.run (mus, 48000);
            ok = ok && bitExact (r.lastInput (mus, 48000), out, 48000);
        }
        check (ok, juce::String (colorIds[fx]) + ": dial within +-" + juce::String (dj::colorDeadZone, 0) + " of centre (incl. MIDI 63/64) is bit-exact, also after use");
    }
    {
        Rig r;
        r.set ("color", 5.5f);
        auto out = r.run (mus, 48000);
        check (! bitExact (mus, out, 48000), "Just outside the dead zone (5.5) the effect starts");
    }
    {
        Rig r;
        auto out = r.run (mus, 48000);
        check (bitExact (mus, out, 48000), "Default settings (BEAT off, COLOR centre): bit-exact");
    }
}

// ── 5. Clicks ────────────────────────────────────────────────────────────────
// Click score: biggest second difference within 40 ms of the event, against the larger of the steady
// states before and after. A hard switch on this sine scores in the hundreds.
static double clickScore (std::function<void (Rig&)> before, std::function<void (Rig&)> after, const Buf& sig)
{
    auto steady = [&] (bool withAfter)
    {
        Rig r; before (r); if (withAfter) after (r);
        auto o = r.run (sig, 48000 * 2);
        return maxD2 (o, 24000, 96000);
    };
    const float a = steady (false), b = steady (true);
    Rig r; before (r);
    auto o = r.run (sig, 48000, [&] (juce::int64 pos) { if (pos == 24064) after (r); });
    const float ev = maxD2 (o, 24064 - 2000, 24064 + 2000);
    return ev / juce::jmax (1.0e-6f, juce::jmax (a, b));
}

static void clickTests()
{
    std::printf ("\n== No clicks: ON / OFF / HOLD / effect and division changes / COLOR moves ==\n");
    const auto s = sine (3.0, 220.0, 0.25f);
    const double limit = 4.0;   // a hard cut of this sine scores ~46
    juce::String bad; double worst = 0; juce::String worstName;
    auto test = [&] (const juce::String& name, std::function<void (Rig&)> a, std::function<void (Rig&)> b)
    {
        const double sc = clickScore (a, b, s);
        if (sc > worst) { worst = sc; worstName = name; }
        if (sc > limit) bad << name << " (" << juce::String (sc, 2) << ") ";
        if (sc > 2.0) std::printf ("    note: %s scores %.2f\n", name.toRawUTF8(), sc);
    };
    int n = 0;
    for (int fx = 0; fx < dj::numBeatFx; ++fx)
    {
        auto base = [fx] (Rig& r) { r.set ("beat_fx", (float) fx); r.set ("beat", 3.0f); r.set ("level", fx == dj::bPitch ? 80.0f : 70.0f); };
        auto on = [base] (Rig& r) { base (r); r.set ("beat_on", 1.0f); };
        test (juce::String (dj::beatName (fx)) + " ON",   base, [] (Rig& r) { r.set ("beat_on", 1.0f); });
        test (juce::String (dj::beatName (fx)) + " OFF",  on,   [] (Rig& r) { r.set ("beat_on", 0.0f); });
        test (juce::String (dj::beatName (fx)) + " HOLD", base, [] (Rig& r) { r.set ("beat_hold", 1.0f); });
        test (juce::String (dj::beatName (fx)) + " HOLD up", [base] (Rig& r) { base (r); r.set ("beat_hold", 1.0f); }, [] (Rig& r) { r.set ("beat_hold", 0.0f); });
        test (juce::String (dj::beatName (fx)) + " div",  on,   [] (Rig& r) { r.set ("beat", 2.0f); });
        const int nx = (fx + 1) % dj::numBeatFx;
        test (juce::String (dj::beatName (fx)) + "->" + dj::beatName (nx), on, [nx] (Rig& r) { r.set ("beat_fx", (float) nx); });
        n += 6;
    }
    std::printf ("  worst BEAT event: %s %.2f\n", worstName.toRawUTF8(), worst);
    check (bad.isEmpty(), "BEAT: " + juce::String (n) + " events (ON, OFF, HOLD down/up, division and effect change, all 14 effects) stay under "
           + juce::String (limit, 1) + "x the steady-state curvature" + (bad.isEmpty() ? "" : "  FAILED: " + bad));
    bad = {}; worst = 0; n = 0;
    for (int fx = 0; fx < dj::numColorFx; ++fx)
    {
        auto base = [fx] (Rig& r) { r.set ("color_fx", (float) fx); r.set ("color_param", 60.0f); };
        if (fx != dj::cNoise)   // NOISE: the dial sweeps a noise filter, its own movement is the sound (checked for safety elsewhere)
        {
            test (juce::String (colorIds[fx]) + " 0->-80", base, [] (Rig& r) { r.set ("color", -80.0f); });
            test (juce::String (colorIds[fx]) + " 0->+80", base, [] (Rig& r) { r.set ("color", 80.0f); });
            test (juce::String (colorIds[fx]) + " -80->+80", [base] (Rig& r) { base (r); r.set ("color", -80.0f); }, [] (Rig& r) { r.set ("color", 80.0f); });
            n += 3;
        }
        test (juce::String (colorIds[fx]) + " +60->0", [base] (Rig& r) { base (r); r.set ("color", 60.0f); }, [] (Rig& r) { r.set ("color", 0.0f); });
        test (juce::String (colorIds[fx]) + " param", [base] (Rig& r) { base (r); r.set ("color", -60.0f); }, [] (Rig& r) { r.set ("color_param", 0.0f); });
        const int nx = (fx + 1) % dj::numColorFx;
        test (juce::String (colorIds[fx]) + "->" + colorIds[nx], [base] (Rig& r) { base (r); r.set ("color", -60.0f); }, [nx] (Rig& r) { r.set ("color_fx", (float) nx); });
        n += 3;
    }
    std::printf ("  worst COLOR event: %s %.2f\n", worstName.toRawUTF8(), worst);
    check (bad.isEmpty(), "COLOR: " + juce::String (n) + " events (dial jumps both ways, through the centre, back to off, PARAM, effect change) stay under "
           + juce::String (limit, 1) + "x" + (bad.isEmpty() ? "" : "  FAILED: " + bad));
    bad = {};
    for (const char* band : { "band_low", "band_mid", "band_high" })
        test (juce::String ("FX FREQ ") + band, [] (Rig& r) { r.set ("beat_fx", dj::bFilter); r.set ("beat_on", 1.0f); r.set ("level", 80.0f); },
              [band] (Rig& r) { r.set (band, 0.0f); });
    check (bad.isEmpty(), "FX FREQ: switching LOW / MID / HIGH off while running is click-free" + (bad.isEmpty() ? "" : "  FAILED: " + bad));
    {
        Buf hard (2, 1000); hard.clear();
        for (int i = 0; i < 500; ++i) hard.setSample (0, i, 0.25f * (float) std::sin (twoPi * 220.0 * i / 48000.0 + 1.3));
        std::printf ("  (for scale: cutting this sine dead scores %.0fx)\n", maxD2 (hard, 400, 600) / maxD2 (hard, 10, 400));
    }
}

// ── 6. Smoothing ─────────────────────────────────────────────────────────────
static void smoothingTests()
{
    std::printf ("\n== Parameter smoothing ==\n");
    const auto mus = music (3.0);
    {
        Rig a, b;
        for (auto* r : { &a, &b }) { r->set ("color_fx", dj::cFilter); r->set ("color_param", 40.0f); }
        a.set ("color", -20.0f); b.set ("color", -90.0f);
        a.run (mus, 48000); b.run (mus, 48000);
        a.set ("color", -90.0f);
        auto oa = a.run (mus, 48000), ob = b.run (mus, 48000);
        const double early = diffDb (ob, oa, 0, 480), late = diffDb (ob, oa, 24000, 48000);
        check (early > -30.0 && late < -60.0, "FILTER cutoff glides (~40 ms) instead of jumping: first 10 ms differ by " + juce::String (early, 1)
               + " dB from the target, after 0.5 s " + juce::String (late, 1) + " dB");
    }
    {
        const double sc = clickScore ([] (Rig& r) { r.set ("beat_fx", dj::bEcho); r.set ("beat", 3.0f); r.set ("beat_on", 1.0f); },
                                      [] (Rig& r) { r.set ("beat", 1.0f); }, sine (4.0, 330.0, 0.25f));
        check (sc < 4.0, "ECHO: changing BEAT while repeats ring glides the delay time (score " + juce::String (sc, 2) + ")");
    }
    {
        const double sc = clickScore ([] (Rig& r) { r.set ("beat_fx", dj::bDelay); r.set ("beat_on", 1.0f); },
                                      [] (Rig& r) { r.set ("time", 180.0f); }, sine (4.0, 330.0, 0.25f));
        check (sc < 4.0, "DELAY: touching TIME glides from the BEAT time to the ms time (score " + juce::String (sc, 2) + ")");
    }
    {
        const double sc = clickScore ([] (Rig& r) { r.set ("beat_fx", dj::bFlanger); r.set ("level", 0.0f); r.set ("beat_on", 1.0f); },
                                      [] (Rig& r) { r.set ("level", 100.0f); }, sine (3.0));
        check (sc < 4.0, "LEVEL/DEPTH jump 0 -> 100% is smoothed (score " + juce::String (sc, 2) + ")");
    }
    {
        Rig r; r.set ("color_fx", dj::cFilter); r.set ("color_param", 70.0f);
        int k = 0;
        auto o = r.run (sine (3.0), 96000, [&] (juce::int64) { r.set ("color", (k++ % 2) ? -95.0f : 95.0f); });
        check (healthy (o) && peak (o) <= 2.0f, "COLOR dial slammed end to end every block for 2 s: finite, peak " + juce::String (juce::Decibels::gainToDecibels (peak (o)), 1) + " dBFS");
    }
}

// ── 7. State ─────────────────────────────────────────────────────────────────
static void stateTests()
{
    std::printf ("\n== Save / restore ==\n");
    Rig a;
    const std::pair<const char*, float> vals[] = { { "color_fx", 3 }, { "color", -42.5f }, { "color_param", 77 }, { "beat_fx", 9 },
                                                   { "beat", 6 }, { "time", 333 }, { "level", 81 }, { "band_low", 0 }, { "band_mid", 1 },
                                                   { "band_high", 0 }, { "beat_on", 1 }, { "beat_hold", 1 } };
    for (auto& v : vals) a.set (v.first, v.second);
    juce::MemoryBlock mb;
    a.p->getStateInformation (mb);
    Rig b;
    b.p->setStateInformation (mb.getData(), (int) mb.getSize());
    bool ok = true; juce::String bad;
    for (auto& v : vals)
    {
        const float want = juce::String (v.first) == "beat_hold" ? 0.0f : v.second;
        if (std::abs (b.get (v.first) - want) > 0.05f) { ok = false; bad << v.first << "=" << b.get (v.first) << " "; }
    }
    check (ok, "All 12 parameters survive save -> new instance -> restore (HOLD is momentary: restores released)" + (bad.isEmpty() ? "" : "  " + bad));
    const auto mus = music (2.0);
    Rig c, d;
    c.p->setStateInformation (mb.getData(), (int) mb.getSize());
    d.p->setStateInformation (mb.getData(), (int) mb.getSize());
    auto oc = c.run (mus, 48000), od = d.run (mus, 48000);
    check (diffDb (oc, od) < -200.0 && diffDb (mus, oc, 0) > -30.0, "Two restored instances produce identical audio (and it's processed)");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.p->createEditor());
        ed.reset (b.p->createEditor());
        check (ed != nullptr, "Editor opens / closes / reopens");
    }
    {
        // every parameter is automatable and has a readable value
        bool all = true;
        for (auto* prm : a.p->getParameters())
            all = all && prm->isAutomatable() && prm->getCurrentValueAsText().isNotEmpty();
        check (all && a.p->getParameters().size() == 12, "12 parameters, all automatable (MIDI-mappable in the DAW), all show a value");
    }
}

// ── 8. Licence gate ──────────────────────────────────────────────────────────
static void licenceGateTests()
{
    std::printf ("\n== Licence gate: no trial / key = bit-exact passthrough ==\n");
    const auto mus = music (3.0);
    for (auto kind : { hz::LicenceInfo::none, hz::LicenceInfo::invalid, hz::LicenceInfo::trialExpired })
    {
        bool ok = true;
        for (int fx = 0; fx < dj::numBeatFx; ++fx)
        {
            Rig r (kind);
            r.set ("color_fx", (float) (fx % 6)); r.set ("color", -80.0f); r.set ("beat_fx", (float) fx); r.set ("beat_on", 1.0f);
            r.set ("band_low", 0.0f); r.set ("level", 100.0f);
            auto out = r.run (mus, 2 * 48000);
            ok = ok && bitExact (mus, out, 2 * 48000);
        }
        const char* names[] = { "no licence", "trial", "pro", "trial ended", "invalid key" };
        check (ok, juce::String (names[(int) kind]) + ": every effect ON, COLOR at -80, a band off -> output is bit-exact input");
    }
    {
        Rig r (hz::LicenceInfo::trial);
        r.set ("beat_fx", dj::bEcho); r.set ("beat_on", 1.0f);
        auto out = r.run (mus, 48000);
        check (diffDb (mus, out, 4800, 48000) > -30.0, "7-day trial: effects work");
    }
    {
        Rig r (hz::LicenceInfo::pro);
        r.set ("beat_fx", dj::bEcho); r.set ("beat_on", 1.0f);
        r.run (mus, 48000);
        setLicence (*r.p, hz::LicenceInfo::trialExpired);
        auto out = r.run (mus, 48000);
        check (bitExact (r.lastInput (mus, 48000), out, 48000), "Licence lost while playing (trial ran out): untouched from the next block");
    }
}

static void keyTests (int argc, char** argv)
{
    std::printf ("\n== Licence keys ==\n");
    int n = 0;
    for (int a = 2; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (! arg.containsChar ('=')) continue;
        ++n;
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto info = hz::Licence::verify (arg.fromFirstOccurrenceOf ("=", false, false), dj::licenceProduct);
        const char* kinds[] = { "none", "trial", "pro", "trialExpired", "invalid" };
        hz::LicenceInfo::Kind want = hz::LicenceInfo::invalid;
        if (name == "pro" || name == "all") want = hz::LicenceInfo::pro;
        else if (name == "trial") want = hz::LicenceInfo::trial;
        else if (name == "expired") want = hz::LicenceInfo::trialExpired;
        check (info.kind == want && (name != "trial" || info.daysLeft() == 7),
               "key " + name + " -> " + kinds[(int) info.kind] + (name == "trial" ? " (" + juce::String (info.daysLeft()) + " days left)" : juce::String()));
    }
    for (int a = 2; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (! arg.startsWith ("pro=")) continue;
        auto key = arg.fromFirstOccurrenceOf ("=", false, false);
        const int i = 12;
        key = key.substring (0, i) + (key[i] == 'A' ? "B" : "A") + key.substring (i + 1);
        check (hz::Licence::verify (key, dj::licenceProduct).kind == hz::LicenceInfo::invalid, "tampered pro key -> invalid");
    }
    check (hz::Licence::verify ("hello", dj::licenceProduct).kind == hz::LicenceInfo::invalid, "garbage key -> invalid");
    if (n == 0) std::printf ("  (no keys passed)\n");
}

// ── UI snapshots ─────────────────────────────────────────────────────────────
static void snapshots (const juce::File& outDir)
{
    std::printf ("\n== UI snapshots ==\n");
    const auto mus = music (3.0);
    auto snap = [&] (const char* name, hz::LicenceInfo::Kind kind, int panel, bool beatLed, std::function<void (Rig&)> setup)
    {
        Rig r (kind, 48000.0, 256, 128.0, 6.3);
        if (setup) setup (r);
        r.run (mus, 24000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p->createEditor());
        auto* e = dynamic_cast<DJToolsAudioProcessorEditor*> (ed.get());
        auto* t = dynamic_cast<juce::Timer*> (ed.get());
        for (int i = 0; i < 3; ++i) t->timerCallback();
        e->showSection (beatLed);
        t->timerCallback();
        for (auto* c : ed->getChildren())
            if (auto* pp = dynamic_cast<ProPanel*> (c)) { if (panel == 1) pp->show ({}); else if (panel == 0) pp->setVisible (false); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream fo (outDir.getChildFile (name));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("  %s\n", name);
    };
    snap ("ui-01-locked-first-open.png", hz::LicenceInfo::none, -1, false, nullptr);
    snap ("ui-02-locked-overlay.png", hz::LicenceInfo::none, 0, false, nullptr);
    snap ("ui-03-trial-color-filter.png", hz::LicenceInfo::trial, 0, false, [] (Rig& r) { r.set ("color", 62.0f); r.set ("color_param", 65.0f); });
    snap ("ui-04-trial-beat-echo-on.png", hz::LicenceInfo::trial, 0, true, [] (Rig& r) { r.set ("beat_on", 1.0f); r.set ("beat", 4.0f); r.set ("level", 64.0f); });
    snap ("ui-05-pro-both-sections.png", hz::LicenceInfo::pro, 0, true, [] (Rig& r)
          { r.set ("color_fx", dj::cDubEcho); r.set ("color", -55.0f); r.set ("beat_fx", dj::bRoll); r.set ("beat", 2.0f); r.set ("beat_on", 1.0f); r.set ("band_low", 0.0f); r.set ("level", 80.0f); });
    snap ("ui-06-pro-pitch-time.png", hz::LicenceInfo::pro, 0, true, [] (Rig& r)
          { r.set ("color_fx", dj::cCrush); r.set ("color", 100.0f); r.set ("beat_fx", dj::bPitch); r.set ("level", 85.0f); r.set ("time", 180.0f); r.set ("beat_on", 1.0f); });
    snap ("ui-07-pro-color-space-left.png", hz::LicenceInfo::pro, 0, false, [] (Rig& r)
          { r.set ("color_fx", dj::cSpace); r.set ("color", -80.0f); r.set ("beat_fx", dj::bBrake); r.set ("beat", 8.0f); });
    snap ("ui-08-pro-panel.png", hz::LicenceInfo::pro, 1, false, nullptr);
    snap ("ui-09-trial-ended.png", hz::LicenceInfo::trialExpired, 0, false, nullptr);
    snap ("ui-10-trial-ended-panel.png", hz::LicenceInfo::trialExpired, 1, false, nullptr);
    snap ("ui-11-trial-panel.png", hz::LicenceInfo::trial, 1, false, nullptr);
}

// ── Demos: every effect on a real loop ───────────────────────────────────────
static void writeWav (const juce::File& f, const Buf& b, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat w;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release()))
        if (auto wr = std::unique_ptr<juce::AudioFormatWriter> (w.createWriterFor (os.get(), rate, 2, 24, {}, 0)))
        { os.release(); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

static int demos (int argc, char** argv)
{
    const juce::File od { juce::String (argv[2]) }; od.createDirectory();
    const double bpm = juce::String (argv[3]).getDoubleValue();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    Buf mix; double rate = 44100.0;
    for (int a = 4; a < argc; ++a)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::String (argv[a]))));
        if (! r) continue;
        rate = r->sampleRate;
        const int len = (int) r->lengthInSamples;
        Buf x (2, len); r->read (&x, 0, len, 0, true, true);
        if (r->numChannels == 1) x.copyFrom (1, 0, x, 0, 0, len);
        if (mix.getNumSamples() == 0) { mix.setSize (2, len); mix.clear(); }
        for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, x, c, 0, juce::jmin (len, mix.getNumSamples()));
    }
    if (mix.getNumSamples() == 0) { std::printf ("no audio\n"); return 1; }
    mix.applyGain (0.5f / mix.getMagnitude (0, mix.getNumSamples()));   // -6 dBFS peaks
    const double spb = 60.0 / bpm * rate;
    const int len = (int) (8 * 4 * spb);   // 8 bars
    {
        Buf o (2, len);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) o.setSample (c, i, mix.getSample (c, i % mix.getNumSamples()));
        writeWav (od.getChildFile ("00 DRY.wav"), o, rate);
    }
    const double pi = juce::MathConstants<double>::pi;
    Buf dry (2, len);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) dry.setSample (c, i, mix.getSample (c, i % mix.getNumSamples()));
    auto stats = [&] (const Buf& out)
    {
        return juce::String::formatted ("change vs dry: bars 3-4 %6.1f dB, bars 5-6 %6.1f dB, bars 7-8 %6.1f dB",
                                        diffDb (dry, out, (int) (8 * spb), (int) (16 * spb)), diffDb (dry, out, (int) (16 * spb), (int) (24 * spb)),
                                        diffDb (dry, out, (int) (24 * spb), len));
    };

    // COLOR: 2 bars dry, swing left and back over 2 bars, right and back over 2 bars, 2 bars dry
    for (int fx = 0; fx < dj::numColorFx; ++fx)
    {
        Rig r (hz::LicenceInfo::pro, rate, 256, bpm, 0.0);
        r.set ("color_fx", (float) fx);
        r.set ("color_param", fx == dj::cFilter ? 55.0f : (fx == dj::cDubEcho ? 70.0f : 60.0f));
        auto out = r.run (mix, len, [&] (juce::int64 pos)
        {
            const double b = pos / spb;
            float v = 0.0f;
            if (b >= 8 && b < 16)       v = (float) (-100.0 * std::sin ((b - 8) / 8.0 * pi));
            else if (b >= 16 && b < 24) v = (float) (100.0 * std::sin ((b - 16) / 8.0 * pi));
            r.set ("color", v);
        });
        writeWav (od.getChildFile (juce::String::formatted ("1%d COLOR %s.wav", fx + 1, colorIds[fx])), out, rate);
        std::printf ("  COLOR %-9s peak %5.1f dBFS  %s\n", colorIds[fx], juce::Decibels::gainToDecibels (peak (out)), stats (out).toRawUTF8());
    }
    // BEAT: 2 bars dry, effect ON for 2 bars (division change halfway), OFF so tails ring, dry.
    for (int fx = 0; fx < dj::numBeatFx; ++fx)
    {
        Rig r (hz::LicenceInfo::pro, rate, 256, bpm, 0.0);
        r.set ("beat_fx", (float) fx);
        const bool rollish = fx == dj::bRoll || fx == dj::bSlipRoll;
        r.set ("level", fx == dj::bPitch ? 50.0f : (rollish || fx == dj::bTrans || fx == dj::bBrake ? 100.0f : 60.0f));
        auto out = r.run (mix, len, [&] (juce::int64 pos)
        {
            const double b = pos / spb;
            bool on = false; int div = 3;
            if (rollish)
            {
                on = (b >= 8 && b < 9) || (b >= 12 && b < 16) || (b >= 20 && b < 22);
                div = b < 13 ? 5 : (b < 14 ? 3 : (b < 15 ? 2 : 1));
                if (b >= 20) div = fx == dj::bSlipRoll ? 2 : 3;
            }
            else if (fx == dj::bBrake) { on = (b >= 14 && b < 16) || (b >= 22 && b < 24); div = b < 18 ? 6 : 5; }
            else if (fx == dj::bPitch)
            {
                on = b >= 8 && b < 16;
                r.set ("level", (float) (b < 8 ? 50.0 : (b < 12 ? 50.0 + 45.0 * std::sin ((b - 8) / 4.0 * pi / 2) : juce::jmax (5.0, 95.0 - 90.0 * (b - 12) / 4.0))));
            }
            else
            {
                on = b >= 8 && b < 16;
                if (fx == dj::bReverb) div = 5;
                else if (fx == dj::bFilter || fx == dj::bFlanger || fx == dj::bPhaser) div = b < 12 ? 7 : 6;
                else if (fx == dj::bTrans) div = b < 12 ? 2 : 1;
                else if (fx == dj::bHelix) div = 3;
                else div = b < 12 ? 4 : 3;
            }
            r.set ("beat", (float) div);
            r.set ("beat_on", on ? 1.0f : 0.0f);
        });
        writeWav (od.getChildFile (juce::String::formatted ("2%c BEAT %s.wav", 'A' + fx, dj::beatName (fx))), out, rate);
        std::printf ("  BEAT  %-9s peak %5.1f dBFS  %s\n", dj::beatName (fx), juce::Decibels::gainToDecibels (peak (out)), stats (out).toRawUTF8());
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 4 && juce::String (argv[1]) == "demos") return demos (argc, argv);

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const bool quick = argc > 2 && juce::String (argv[2]) == "quick";

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    deadZoneTests();
    licenceGateTests();
    timingTests();
    tailTests();
    smoothingTests();
    stateTests();
    if (! quick) { effectTests(); clickTests(); }
    keyTests (argc, argv);
    snapshots (outDir);

    std::printf ("\n%s: %d passed, %d failed (%.0f s)\n", failures == 0 ? "ALL PASS" : "FAILURES", passes, failures,
                 (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    return failures == 0 ? 0 : 1;
}
