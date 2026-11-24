#include "Engine.h"
#include <cmath>
#include <cstring>

namespace dj
{
using namespace dsp;

static constexpr float quietLevel = 1.0e-6f;   // -120 dBFS: a tail below this for long enough is finished
static inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
static inline double frac (double x) { return x - std::floor (x); }

// ── Static helpers (also used by the editor) ─────────────────────────────────
double Engine::periodBeats (const Params& p, double bpm)
{
    if (p.timeMs > 0.5f) return std::max (0.001, (double) p.timeMs * bpm / 60000.0);
    return divisionBeats[clampT (p.division, 0, numDivisions - 1)];
}

float Engine::colorAmount (float dial)
{
    const float a = std::abs (dial);
    if (a < colorDeadZone) return 0.0f;
    const float v = std::min (1.0f, (a - colorDeadZone) / (100.0f - colorDeadZone));
    return dial < 0 ? -v : v;
}

double Engine::colorCutoffHz (float v, float)
{
    const double a = std::abs (v);
    return v < 0 ? 20000.0 * std::pow (70.0 / 20000.0, a) : 20.0 * std::pow (8000.0 / 20.0, a);
}

double Engine::pitchSemitones (float level)
{
    const double d = level - 0.5, dz = 0.03;
    if (std::abs (d) <= dz) return 0.0;
    return (d < 0 ? -1.0 : 1.0) * (std::abs (d) - dz) / (0.5 - dz) * 12.0;
}

bool Engine::sendLive (int fx) const
{
    if (fx == bReverb) return revLive;
    return beatIsSend (fx) && echoes[fx].live;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────
void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    colorS.setTime (sr, 40.0);  colorS.eps = 1.0e-4;
    paramS.setTime (sr, 40.0);  paramS.eps = 1.0e-6;
    for (auto& w : colorW) w.setTime (sr, 0.03);
    for (auto& d : dubLine) d.prepare ((int) (2.5 * sr));
    dubTime.setTime (sr, 120.0); dubTime.eps = 1.0e-3;
    for (int c = 0; c < 2; ++c) { dubLp[c].setLP (3200.0, sr); dubHp[c].setLP (250.0, sr); }
    space.prepare (sr);

    for (auto& a : beatA) a.setTime (sr, 0.012);
    levelS.setTime (sr, 30.0); levelS.eps = 1.0e-6;
    for (auto& b : bandG) b.setTime (sr, 0.02);
    for (int c = 0; c < 2; ++c)
    {
        lowA[c].lowPass (300.0, sr);   lowB[c].lowPass (300.0, sr);
        highA[c].highPass (3000.0, sr); highB[c].highPass (3000.0, sr);
    }

    struct Cfg { int fx; double lp, hp; };
    for (auto cfg : { Cfg { bDelay, 12000, 20 }, Cfg { bEcho, 5000, 150 }, Cfg { bPingPong, 7000, 100 },
                      Cfg { bHelix, 8000, 60 }, Cfg { bSpiral, 4500, 200 } })
    {
        auto& e = echoes[cfg.fx];
        for (int c = 0; c < 2; ++c)
        {
            e.d[c].prepare ((int) (maxEchoSeconds * sr) + 8);
            e.lp[c].setLP (cfg.lp, sr);
            e.hp[c].setLP (cfg.hp, sr);
            static const double apMs[3] = { 5.3, 7.9, 11.3 };
            for (int k = 0; k < 3; ++k)
                e.ap[c][k].prepare ((int) std::lround (apMs[k] * (c == 0 ? 1.0 : 1.07) * 0.001 * sr), 0.55f);
        }
        e.xfLen = std::max (1, (int) (0.04 * sr));
        e.fb.setTime (sr, 150.0);  e.fb.eps = 1.0e-6;
    }
    reverb.prepare (sr);
    for (auto& h : revHp) h.setLP (150.0, sr);
    transG.setTime (sr, 0.001);
    for (auto& f : flLine) f.prepare ((int) (0.02 * sr));

    int64_t size = 1;
    while (size < (int64_t) ((maxLoopSeconds * 1.3 + 1.0) * sr)) size <<= 1;
    for (auto& h : hist) h.assign ((size_t) size, 0.0f);
    histMask = size - 1;
    const size_t loopCap = (size_t) (maxLoopSeconds * sr) + (size_t) (0.01 * sr) + 16;
    for (auto& l : loopBuf) l.assign (loopCap, 0.0f);
    pitch.prepare (sr);
    reset();
}

void Engine::clearEcho (Echo& e)
{
    for (int c = 0; c < 2; ++c)
    {
        e.d[c].clear(); e.lp[c].reset(); e.hp[c].reset();
        for (auto& a : e.ap[c]) a.clear();
    }
    e.quiet = 0;
}

void Engine::reset()
{
    for (auto& f : fFilter) f.reset();
    swEnv = swSlow = 0.0f; swGain = 1.0f;
    for (int c = 0; c < 2; ++c)
    {
        swHp[c].reset(); swBp[c].reset(); dubSendA[c].reset(); dubSendB[c].reset(); dubLp[c].reset(); dubHp[c].reset();
        spSendA[c].reset(); spSendB[c].reset(); noiseF[c].reset(); crHp[c].reset(); crHold[c] = 0.0f;
        lowA[c].reset(); lowB[c].reset(); highA[c].reset(); highB[c].reset();
        bfSvf[c].reset(); revHp[c].reset(); flFb[c] = 0.0f; phFb[c] = 0.0f;
        for (auto& z : phZ[c]) z = 0.0f;
        flLine[c].clear();
    }
    for (auto& d : dubLine) d.clear();
    dubLive = false; dubQuiet = 0;
    space.clear(); spaceLive = false; spaceQuiet = 0;
    for (auto& e : echoes) { if (! e.d[0].buf.empty()) clearEcho (e); e.live = false; }
    reverb.clear(); revLive = false; revQuiet = 0;
    for (auto& a : beatA) a.reset (0.0f);
    roll = {}; slip = {};
    histValidFrom = absNow;
    pitch.reset();
    crPhase = 0.0f;
    fresh = true;
}

// ── Roll helpers ─────────────────────────────────────────────────────────────
float Engine::histRead (int ch, double pos) const
{
    const auto i = (int64_t) std::floor (pos);
    const float fr = (float) (pos - (double) i);
    const auto& h = hist[ch];
    const float a = h[(size_t) (i & histMask)];
    if (fr == 0.0f) return a;
    return a + fr * (h[(size_t) ((i + 1) & histMask)] - a);
}

float Engine::loopRead (int ch, double pos) const
{
    const int last = std::max (0, loopFill - 1);
    const int i = clampT ((int) std::floor (pos), 0, last);
    const float fr = (float) (pos - std::floor (pos));
    const float a = loopBuf[ch][(size_t) i];
    if (fr == 0.0f || i >= last) return a;
    return a + fr * (loopBuf[ch][(size_t) i + 1] - a);
}

// Start a roll at the most recent grid line of `period` (so repeats land on the host grid).
void Engine::startRoll (Roll& r, bool useLoop, double ppq, double period, double spb)
{
    const double grid = std::floor (ppq / period + 1.0e-9) * period;
    const double behind = std::max (0.0, (ppq - grid) * spb);
    int64_t start = absNow - (int64_t) std::llround (behind);
    const int64_t oldest = std::max (histValidFrom, absNow - (int64_t) (maxLoopSeconds * sr));
    start = std::max (start, oldest);
    r.on = true;
    r.start = start;
    r.rp = (double) (absNow - start);
    r.fade = 0;
    r.L = std::max (64.0, std::min (period * spb, maxLoopSeconds * sr));
    if (useLoop)
    {
        // Copy what's already been heard since the grid line into the loop buffer.
        loopFill = 0;
        for (int64_t a = start; a < absNow; ++a, ++loopFill)
            for (int c = 0; c < 2; ++c) loopBuf[c][(size_t) loopFill] = hist[c][(size_t) (a & histMask)];
    }
}

// One sample of ROLL (loop buffer) or SLIP ROLL (history, re-grabs every `restartBeats`).
void Engine::rollStep (Roll& r, bool useLoop, float& outL, float& outR, double periodSamples,
                       double ppq, double spb, double restartBeats)
{
    const double newL = std::max (64.0, std::min (periodSamples, maxLoopSeconds * sr));
    auto beginFade = [&] (double fromPos)
    {
        r.oldAbs = fromPos;
        r.fadeLen = std::max (1, std::min ((int) (0.003 * sr), (int) (newL / 4)));
        r.fade = r.fadeLen;
    };
    if (newL != r.L)
    {
        if (r.rp >= newL) { beginFade (r.rp); r.rp = std::fmod (r.rp, newL); }
        r.L = newL;
    }
    if (restartBeats > 0.0)
    {
        const auto idx = (int64_t) std::floor (ppq / restartBeats + 1.0e-9);
        if (idx != r.restartIdx)
        {
            // Re-grab: carry on from the newest audio (relative positions are measured from the new start).
            r.restartIdx = idx;
            const double behind = std::max (0.0, (ppq - (double) idx * restartBeats) * spb);
            const int64_t ns = absNow - (int64_t) std::llround (behind);
            const double oldAbsPos = (double) r.start + r.rp;
            r.start = ns;
            r.rp = (double) (absNow - ns);
            beginFade (oldAbsPos - (double) ns);
        }
    }
    auto rd = [&] (int ch, double rel) { return useLoop ? loopRead (ch, rel) : histRead (ch, (double) r.start + rel); };
    float l = rd (0, r.rp), rr = rd (1, r.rp);
    if (r.fade > 0)
    {
        const float t = 1.0f - (float) r.fade / (float) r.fadeLen;
        l  = t * l  + (1.0f - t) * rd (0, r.oldAbs);
        rr = t * rr + (1.0f - t) * rd (1, r.oldAbs);
        r.oldAbs += 1.0;
        --r.fade;
    }
    outL = l; outR = rr;
    r.rp += 1.0;
    if (r.rp >= r.L) { beginFade (r.rp); r.rp -= r.L; }
}

// One sample of a delay-type BEAT effect. Returns the wet signal.
void Engine::processEcho (int fx, Echo& e, float a, float inL, float inR, float& wl, float& wr)
{
    if (! e.xfading && e.targetD != e.curD) { e.nextD = e.targetD; e.xfPos = 0; e.xfading = true; }
    const double D = e.curD;
    const float fb = (float) e.fb.next();
    wl = e.d[0].read (D);
    wr = e.d[1].read (D);
    if (e.xfading)
    {
        const float t = (float) e.xfPos / (float) e.xfLen;
        wl += t * (e.d[0].read (e.nextD) - wl);
        wr += t * (e.d[1].read (e.nextD) - wr);
        if (++e.xfPos >= e.xfLen) { e.curD = e.nextD; e.xfading = false; }
    }
    switch (fx)
    {
        case bDelay:
            e.d[0].write (inL + fb * e.lp[0].lp (wl));
            e.d[1].write (inR + fb * e.lp[1].lp (wr));
            break;
        case bEcho:
            e.d[0].write (inL + fb * e.hp[0].hp (e.lp[0].lp (wl)));
            e.d[1].write (inR + fb * e.hp[1].hp (e.lp[1].lp (wr)));
            break;
        case bPingPong:
        {
            const float m = 0.5f * (inL + inR);
            e.d[0].write (m + fb * e.hp[0].hp (e.lp[0].lp (wr)));
            e.d[1].write (fb * e.hp[1].hp (e.lp[1].lp (wl)));
            break;
        }
        case bHelix:
            e.d[0].write (0.7f * inL + fb * std::tanh (e.hp[0].hp (e.lp[0].lp (wl))));
            e.d[1].write (0.7f * inR + fb * std::tanh (e.hp[1].hp (e.lp[1].lp (wr))));
            break;
        case bSpiral:
        default:
        {
            float sl = wl, sr2 = wr;
            for (int k = 0; k < 3; ++k) { sl = e.ap[0][k].process (sl); sr2 = e.ap[1][k].process (sr2); }
            e.d[0].write (inL + fb * e.hp[0].hp (e.lp[0].lp (sl)));
            e.d[1].write (inR + fb * e.hp[1].hp (e.lp[1].lp (sr2)));
            break;
        }
    }
    if (a == 0.0f && beatA[fx].target == 0.0f)
    {
        if (std::abs (wl) < quietLevel && std::abs (wr) < quietLevel) ++e.quiet; else e.quiet = 0;
        if (e.quiet > (int) (std::max (e.curD, e.nextD) + 0.3 * sr)) { e.live = false; clearEcho (e); }
    }
    else e.quiet = 0;
}

// ── Process ──────────────────────────────────────────────────────────────────
bool Engine::process (float* L, float* R, int n, const Params& p, const Transport& t)
{
    const double bpm = clampT (t.bpm, 20.0, 400.0);
    const double spb = 60.0 / bpm * sr;          // samples per beat
    const double bps = 1.0 / spb;                // beats per sample
    const double period = periodBeats (p, bpm);
    const double periodSamples = period * spb;
    const int sel  = clampT (p.beatFx, 0, numBeatFx - 1);
    const int csel = clampT (p.colorFx, 0, numColorFx - 1);

    if (fresh)
    {
        colorS.reset (p.color); paramS.reset (p.colorParam); levelS.reset (p.level);
        for (int e = 0; e < numColorFx; ++e) colorW[e].reset (e == csel ? 1.0f : 0.0f);
        for (int b = 0; b < 3; ++b) bandG[b].reset (p.band[b] ? 1.0f : 0.0f);
        fresh = false;
    }
    colorS.t = p.color; paramS.t = p.colorParam; levelS.t = p.level;
    for (int e = 0; e < numColorFx; ++e) colorW[e].target = e == csel ? 1.0f : 0.0f;
    for (int b = 0; b < 3; ++b) bandG[b].target = p.band[b] ? 1.0f : 0.0f;

    // ── What needs to run this block ──
    const bool colorNeeded = std::abs (p.color) >= colorDeadZone || std::abs ((float) colorS.v) >= colorDeadZone
                             || dubLive || spaceLive;
    if (! colorNeeded)
    {
        colorS.reset (p.color);
        paramS.reset (p.colorParam);
        for (auto& w : colorW) w.reset (w.target);
    }

    // Longer than the delay line: halve until it fits, so the repeats stay on the grid.
    auto fitDelay = [] (double d, const Echo& ec)
    {
        const double mx = (double) ec.d[0].maxDelay() - 2.0;
        while (d > mx) d *= 0.5;
        return std::max (1.0, d);
    };
    // Delay-type settings for this block.
    for (int e : { bDelay, bEcho, bPingPong, bHelix, bSpiral })
    {
        auto& ec = echoes[e];
        ec.targetD = fitDelay (periodSamples, ec);
        switch (e)
        {
            case bDelay:    ec.fb.t = 0.25; break;
            case bEcho:     ec.fb.t = 0.62; break;
            case bPingPong: ec.fb.t = 0.6;  break;
            case bHelix:    ec.fb.t = (sel == bHelix && p.beatActive) ? 0.97 : 0.5; break;
            default:        ec.fb.t = 0.78; break;
        }
    }
    for (int e = 0; e < numBeatFx; ++e)
    {
        const float tgt = (e == sel && p.beatActive) ? 1.0f : 0.0f;
        if (tgt > 0.0f && beatA[e].v == 0.0f)
        {
            // Fresh start of an effect.
            switch (e)
            {
                case bTrans:    transG.reset (frac (t.ppq / period) < 0.5 ? 1.0f : 0.0f); break;
                case bFilter:
                case bPhaser:
                case bFlanger:
                {
                    // Warm up on the last ~40 ms the plugin heard, so the effect starts mid-flow (no zero-state thump).
                    if (e == bFilter) for (auto& f : bfSvf) f.reset();
                    for (int c = 0; c < 2; ++c)
                    {
                        if (e == bPhaser) { phFb[c] = 0.0f; for (auto& z : phZ[c]) z = 0.0f; }
                        if (e == bFlanger) flLine[c].clear();
                    }
                    const int warm = (int) (0.04 * sr);
                    for (int k = warm; k > 0; --k)
                    {
                        const auto pos = absNow - k;
                        const float hl = hist[0][(size_t) (pos & histMask)], hr = hist[1][(size_t) (pos & histMask)];
                        const double lfo = 0.5 - 0.5 * std::cos (2.0 * kPi * frac ((t.ppq - k * bps) / period));
                        if (e == bFilter)
                        {
                            const float g = svfG (250.0 * std::pow (40.0, lfo), sr);
                            bfSvf[0].process (hl, g, 0.4f); bfSvf[1].process (hr, g, 0.4f);
                        }
                        else if (e == bFlanger)
                        {
                            const double D = (0.0006 + 0.0055 * lfo) * sr;
                            const float yl = flLine[0].read (D), yr = flLine[1].read (D);
                            flLine[0].write (hl + 0.55f * yl); flLine[1].write (hr + 0.55f * yr);
                        }
                        else
                        {
                            const double tt = std::tan (kPi * 200.0 * std::pow (20.0, lfo) / sr);
                            const float ac = (float) ((tt - 1.0) / (tt + 1.0));
                            const float in2[2] = { hl, hr };
                            for (int c = 0; c < 2; ++c)
                            {
                                float x = in2[c] + 0.45f * phFb[c];
                                for (int q = 0; q < 6; ++q) { const float y = ac * x + phZ[c][q]; phZ[c][q] = x - ac * y; x = y; }
                                phFb[c] = x;
                            }
                        }
                    }
                    break;
                }
                case bRoll:     startRoll (roll, true, t.ppq, period, spb); break;
                case bSlipRoll: startRoll (slip, false, t.ppq, period, spb); slip.restartIdx = INT64_MIN; break;
                case bBrake:    brakePos = (double) absNow; brakeElapsed = 0.0; break;
                case bPitch:
                    pitch.reset();
                    for (int k = pitch.primeLength(); k > 0; --k)
                        pitch.prime (hist[0][(size_t) ((absNow - k) & histMask)], hist[1][(size_t) ((absNow - k) & histMask)]);
                    break;
                default: break;
            }
        }
        beatA[e].target = tgt;
        if (beatIsSend (e) && tgt > 0.0f)
        {
            const bool wasLive = e == bReverb ? revLive : echoes[e].live;
            if (e == bReverb) revLive = true;
            else
            {
                auto& ec = echoes[e];
                ec.live = true;
                if (! wasLive)
                {
                    ec.curD = ec.nextD = ec.targetD = fitDelay (periodSamples, ec);
                    ec.xfading = false;
                    ec.fb.reset (ec.fb.t);
                }
            }
        }
    }
    {
        const double rt = clampT (0.6 + 1.6 * period * 60.0 / bpm, 0.5, 12.0);   // longer BEAT = bigger room
        if (rt != revRt) { reverb.setDecay (rt, 6500.0); revRt = rt; }
    }
    const double slipRestart = [&]
    {
        if (period >= 4.0) return 0.0;               // long rolls: no re-grab (same as ROLL)
        double r = period < 1.0 ? 1.0 : 4.0;
        while (r * spb > maxLoopSeconds * sr && r > 1.0) r /= 2.0;
        return r;
    }();

    bool beatNeeded = p.beatActive || revLive;
    for (int e = 0; e < numBeatFx; ++e) if (beatA[e].v > 0.0f || (beatIsSend (e) && e != bReverb && echoes[e].live)) beatNeeded = true;
    for (auto& b : bandG) if (b.v != 1.0f || b.target != 1.0f) beatNeeded = true;

    if (! colorNeeded && ! beatNeeded)
    {
        // Nothing to do: keep the history (for ROLL / SLIP ROLL / BRAKE) and leave the audio alone.
        for (int i = 0; i < n; ++i, ++absNow)
        {
            hist[0][(size_t) (absNow & histMask)] = L[i];
            hist[1][(size_t) (absNow & histMask)] = R[i];
        }
        levelS.reset (p.level);
        for (auto& f : fFilter) f.reset();
        return false;
    }

    const float dz = colorDeadZone;
    const float atkC = std::exp (-1.0f / (float) (0.0002 * sr)), relC = std::exp (-1.0f / (float) (0.04 * sr));
    const float slowC = 1.0f - std::exp (-1.0f / (float) (0.3 * sr));
    const float gateAtk = 1.0f - std::exp (-1.0f / (float) (0.0005 * sr));
    const double dubTarget = clampT (0.75 * spb, 1.0, (double) dubLine[0].maxDelay() - 2.0);
    dubTime.t = dubTarget;

    for (int i = 0; i < n; ++i, ++absNow)
    {
        const float xL = L[i], xR = R[i];
        float yL = xL, yR = xR;

        // ════ COLOR ════
        if (colorNeeded)
        {
            // Glide, plus a speed limit (centre to end in no less than 60 ms) so a jump sweeps like a fast hand.
            {
                const double maxStep = 95.0 / (0.06 * sr);
                const double before = colorS.v;
                colorS.next();
                colorS.v = clampT (colorS.v, before - maxStep, before + maxStep);
            }
            const float cv = (float) colorS.v;
            const float cp = (float) paramS.next();
            const float av0 = std::abs (cv);
            const float v = av0 < dz ? 0.0f : (cv < 0 ? -1.0f : 1.0f) * std::min (1.0f, (av0 - dz) / (100.0f - dz));
            const float av = std::abs (v);
            const float entry = std::min (1.0f, av / 0.15f);   // fade the effect in over the first 15% past the centre
            float w;

            // FILTER: left low-pass, right high-pass, PARAM = resonance
            w = colorW[cFilter].next();
            if (w > 0.0f && av > 0.0f)
            {
                const double fc = colorCutoffHz (v, cp);
                const float g = svfG (fc, sr);
                const float q = 0.7f + cp * cp * 2.8f, k = 1.0f / q;
                const float comp = 1.0f / (1.0f + 0.25f * (q - 0.7f));
                fFilter[0].process (xL, g, k); fFilter[1].process (xR, g, k);
                const float fl = (v < 0 ? fFilter[0].lp : fFilter[0].hp) * comp;
                const float fr = (v < 0 ? fFilter[1].lp : fFilter[1].hp) * comp;
                const float m = w * entry;
                yL += m * (fl - xL); yR += m * (fr - xR);
            }
            else { fFilter[0].reset(); fFilter[1].reset(); }

            // SWEEP: left = gate that tightens + thins, right = band-pass that narrows (PARAM = depth / centre)
            w = colorW[cSweep].next();
            if (w > 0.0f && av > 0.0f)
            {
                float pl, pr;
                if (v < 0)
                {
                    const float lvl = std::max (std::abs (xL), std::abs (xR));
                    swEnv = lvl > swEnv ? lvl + (swEnv - lvl) * atkC : lvl + (swEnv - lvl) * relC;
                    swSlow += (swEnv - swSlow) * slowC;
                    const float thr = swSlow * dbToGain (-30.0f + 27.0f * av);
                    const float floorG = dbToGain (-(10.0f + 50.0f * cp));
                    const float tgt = swEnv >= thr ? 1.0f : floorG;
                    const float rel = 1.0f / (float) ((0.005 + 0.07 * (1.0 - av)) * sr);
                    swGain += (tgt - swGain) * (tgt > swGain ? gateAtk : rel);
                    const float g = svfG (20.0 * std::pow (20.0, (double) av), sr);
                    swHp[0].process (xL * swGain, g, 1.4f); swHp[1].process (xR * swGain, g, 1.4f);
                    pl = swHp[0].hp; pr = swHp[1].hp;
                }
                else
                {
                    const float g = svfG (200.0 * std::pow (25.0, (double) cp), sr);
                    const float q = 0.7f + 8.0f * std::pow (av, 1.5f), k = 1.0f / q;
                    swBp[0].process (xL, g, k); swBp[1].process (xR, g, k);
                    const float mk = k * (1.0f + 0.22f * (q - 0.7f));
                    pl = swBp[0].bp * mk; pr = swBp[1].bp * mk;
                }
                const float m = w * entry;
                yL += m * (pl - xL); yR += m * (pr - xR);
            }
            else { swEnv = swSlow = 0.0f; swGain = 1.0f; for (int c = 0; c < 2; ++c) { swHp[c].reset(); swBp[c].reset(); } }

            // NOISE: filtered white noise, left = low-pass sweeping down, right = high-pass sweeping up
            w = colorW[cNoise].next();
            if (w > 0.0f && av > 0.0f)
            {
                const double fc = v < 0 ? 16000.0 * std::pow (250.0 / 16000.0, (double) av) : 150.0 * std::pow (60.0, (double) av);
                const float g = svfG (fc, sr);
                noiseF[0].process (noise[0].next(), g, 0.8f); noiseF[1].process (noise[1].next(), g, 0.8f);
                const float lvl = dbToGain (-28.0f + 22.0f * cp) * w * entry;
                yL += lvl * (v < 0 ? noiseF[0].lp : noiseF[0].hp);
                yR += lvl * (v < 0 ? noiseF[1].lp : noiseF[1].hp);
            }
            else { noiseF[0].reset(); noiseF[1].reset(); }

            // CRUSH: left = drive, right = bit crush + sample-rate reduction + high-pass (PARAM = intensity)
            w = colorW[cCrush].next();
            if (w > 0.0f && av > 0.0f)
            {
                const float inten = av * (0.35f + 0.65f * cp);
                float pl, pr;
                if (v < 0)
                {
                    const float gd = 1.0f + 40.0f * inten;
                    const float comp = 1.0f / (1.0f + 0.45f * std::log2 (gd));
                    pl = std::tanh (gd * xL) * comp; pr = std::tanh (gd * xR) * comp;
                }
                else
                {
                    const float q = std::pow (2.0f, 15.0f - 12.0f * inten);
                    const float hold = 1.0f + 15.0f * inten;
                    crPhase += 1.0f;
                    if (crPhase >= hold)
                    {
                        crPhase -= hold;
                        crHold[0] = std::round (xL * q) / q;
                        crHold[1] = std::round (xR * q) / q;
                    }
                    const float g = svfG (20.0 * std::pow (60.0, (double) av), sr);
                    crHp[0].process (crHold[0], g, 1.2f); crHp[1].process (crHold[1], g, 1.2f);
                    pl = crHp[0].hp; pr = crHp[1].hp;
                }
                const float m = w * entry;
                yL += m * (pl - xL); yR += m * (pr - xR);
            }
            else { crHp[0].reset(); crHp[1].reset(); crPhase = 0.0f; crHold[0] = crHold[1] = 0.0f; }

            // DUB ECHO (send): left = mid echo, right = high echo, PARAM = feedback. Tails ring on.
            w = colorW[cDubEcho].next();
            {
                const float send = w * av;
                if (send > 0.0f) dubLive = true;
                if (dubLive)
                {
                    const double D = dubTime.next();
                    float sl = 0.0f, sr2 = 0.0f;
                    if (send > 0.0f)
                    {
                        if (v < 0)
                        {
                            const float gh = svfG (400.0, sr), gl = svfG (2500.0, sr);
                            dubSendA[0].process (xL, gh, 1.2f); dubSendA[1].process (xR, gh, 1.2f);
                            dubSendB[0].process (dubSendA[0].hp, gl, 1.2f); dubSendB[1].process (dubSendA[1].hp, gl, 1.2f);
                            sl = dubSendB[0].lp; sr2 = dubSendB[1].lp;
                        }
                        else
                        {
                            const float gh = svfG (2500.0, sr);
                            dubSendA[0].process (xL, gh, 1.0f); dubSendA[1].process (xR, gh, 1.0f);
                            sl = dubSendA[0].hp * 1.4f; sr2 = dubSendA[1].hp * 1.4f;
                        }
                    }
                    const float fb = 0.3f + 0.65f * cp;
                    const float wl = dubLine[0].read (D), wr = dubLine[1].read (D);
                    dubLine[0].write (send * sl + fb * std::tanh (dubHp[0].hp (dubLp[0].lp (wl))));
                    dubLine[1].write (send * sr2 + fb * std::tanh (dubHp[1].hp (dubLp[1].lp (wr))));
                    yL += wl; yR += wr;
                    if (send == 0.0f)
                    {
                        if (std::abs (wl) < quietLevel && std::abs (wr) < quietLevel) ++dubQuiet; else dubQuiet = 0;
                        if (dubQuiet > (int) (D + 0.3 * sr))
                        {
                            dubLive = false; dubQuiet = 0;
                            for (int c = 0; c < 2; ++c) { dubLine[c].clear(); dubLp[c].reset(); dubHp[c].reset(); dubSendA[c].reset(); dubSendB[c].reset(); }
                        }
                    }
                    else dubQuiet = 0;
                }
                else dubTime.reset (dubTarget);
            }

            // SPACE (send): left = reverb on mids/lows, right = mids/highs, PARAM = size / feedback. Tails ring on.
            w = colorW[cSpace].next();
            {
                const float send = w * av;
                if (send > 0.0f) spaceLive = true;
                if (spaceLive)
                {
                    if ((colorCtl++ & 63) == 0)
                        space.setDecay (0.5 + 7.5 * std::pow ((double) cp, 1.5), v < 0 ? 3500.0 : 9000.0);
                    float sl = 0.0f, sr2 = 0.0f;
                    if (send > 0.0f)
                    {
                        if (v < 0)
                        {
                            const float gh = svfG (120.0, sr), gl = svfG (1800.0, sr);
                            spSendA[0].process (xL, gh, 1.0f); spSendA[1].process (xR, gh, 1.0f);
                            spSendB[0].process (spSendA[0].hp, gl, 1.0f); spSendB[1].process (spSendA[1].hp, gl, 1.0f);
                            sl = spSendB[0].lp; sr2 = spSendB[1].lp;
                        }
                        else
                        {
                            const float gh = svfG (800.0, sr);
                            spSendA[0].process (xL, gh, 1.0f); spSendA[1].process (xR, gh, 1.0f);
                            sl = spSendA[0].hp; sr2 = spSendA[1].hp;
                        }
                    }
                    float wl, wr;
                    space.process (send * sl, send * sr2, wl, wr);
                    yL += wl; yR += wr;
                    if (send == 0.0f)
                    {
                        if (std::abs (wl) < quietLevel && std::abs (wr) < quietLevel) ++spaceQuiet; else spaceQuiet = 0;
                        if (spaceQuiet > (int) (0.3 * sr))
                        {
                            spaceLive = false; spaceQuiet = 0; space.clear();
                            for (int c = 0; c < 2; ++c) { spSendA[c].reset(); spSendB[c].reset(); }
                        }
                    }
                    else spaceQuiet = 0;
                }
            }
        }

        // ════ BEAT ════
        float fL = yL, fR = yR;
        if (beatNeeded)
        {
            const float g0 = bandG[0].next(), g1 = bandG[1].next(), g2 = bandG[2].next();
            const float loL = lowB[0].process (lowA[0].process (yL)), loR = lowB[1].process (lowA[1].process (yR));
            const float hiL = highB[0].process (highA[0].process (yL)), hiR = highB[1].process (highA[1].process (yR));
            const float miL = yL - loL - hiL, miR = yR - loR - hiR;
            fL = yL - (1.0f - g0) * loL - (1.0f - g1) * miL - (1.0f - g2) * hiL;
            fR = yR - (1.0f - g0) * loR - (1.0f - g1) * miR - (1.0f - g2) * hiR;
        }
        hist[0][(size_t) (absNow & histMask)] = fL;
        hist[1][(size_t) (absNow & histMask)] = fR;
        if (loopFill < (int) loopBuf[0].size() && roll.on)
        {
            loopBuf[0][(size_t) loopFill] = fL;
            loopBuf[1][(size_t) loopFill] = fR;
            ++loopFill;
        }

        if (beatNeeded)
        {
            const float lev = (float) levelS.next();
            const double ppq = t.ppq + i * bps;
            const double phase = frac (ppq / period);
            float oL = yL, oR = yR, dryCut = 0.0f;
            const float wetGain = std::min (1.0f, 2.0f * lev);
            const float dryRed = std::max (0.0f, 2.0f * lev - 1.0f);

            for (int e = 0; e < numBeatFx; ++e)
            {
                const float a = beatA[e].next();
                if (beatIsSend (e))
                {
                    float wl = 0.0f, wr = 0.0f;
                    if (e == bReverb)
                    {
                        if (! revLive) continue;
                        reverb.process (revHp[0].hp (a * fL), revHp[1].hp (a * fR), wl, wr);
                        wl *= 1.6f; wr *= 1.6f;
                        if (a == 0.0f && beatA[e].target == 0.0f)
                        {
                            if (std::abs (wl) < quietLevel && std::abs (wr) < quietLevel) ++revQuiet; else revQuiet = 0;
                            if (revQuiet > (int) (0.3 * sr)) { revLive = false; revQuiet = 0; reverb.clear(); revHp[0].reset(); revHp[1].reset(); }
                        }
                        else revQuiet = 0;
                    }
                    else
                    {
                        if (! echoes[e].live) continue;
                        processEcho (e, echoes[e], a, a * fL, a * fR, wl, wr);
                    }
                    oL += wetGain * wl; oR += wetGain * wr;
                    dryCut = std::max (dryCut, a * dryRed);
                    continue;
                }
                if (a <= 0.0f)
                {
                    if (e == bRoll && roll.on) roll.on = false;
                    if (e == bSlipRoll) slip.on = false;
                    continue;
                }

                float pl = fL, pr = fR, amt = lev;
                switch (e)
                {
                    case bTrans:
                    {
                        transG.target = phase < 0.5 ? 1.0f : 0.0f;
                        const float g = transG.next();
                        pl = fL * g; pr = fR * g;
                        break;
                    }
                    case bFilter:
                    {
                        const double lfo = 0.5 - 0.5 * std::cos (2.0 * kPi * phase);
                        const float g = svfG (250.0 * std::pow (40.0, lfo), sr);
                        bfSvf[0].process (fL, g, 0.4f); bfSvf[1].process (fR, g, 0.4f);
                        pl = bfSvf[0].lp * 0.85f; pr = bfSvf[1].lp * 0.85f;
                        break;
                    }
                    case bFlanger:
                    {
                        const double lfo = 0.5 - 0.5 * std::cos (2.0 * kPi * phase);
                        const double D = (0.0006 + 0.0055 * lfo) * sr;
                        const float yl = flLine[0].read (D), yr = flLine[1].read (D);
                        flLine[0].write (fL + 0.55f * yl); flLine[1].write (fR + 0.55f * yr);
                        pl = 0.55f * (fL + yl); pr = 0.55f * (fR + yr);
                        break;
                    }
                    case bPhaser:
                    {
                        const double lfo = 0.5 - 0.5 * std::cos (2.0 * kPi * phase);
                        const double tt = std::tan (kPi * 200.0 * std::pow (20.0, lfo) / sr);
                        const float ac = (float) ((tt - 1.0) / (tt + 1.0));
                        float io[2] = { fL + 0.45f * phFb[0], fR + 0.45f * phFb[1] };
                        for (int c = 0; c < 2; ++c)
                        {
                            float x = io[c];
                            for (int k = 0; k < 6; ++k) { const float y = ac * x + phZ[c][k]; phZ[c][k] = x - ac * y; x = y; }
                            phFb[c] = x;
                            io[c] = x;
                        }
                        pl = 0.5f * (fL + io[0]); pr = 0.5f * (fR + io[1]);
                        break;
                    }
                    case bRoll:
                        if (! roll.on) startRoll (roll, true, ppq, period, spb);
                        rollStep (roll, true, pl, pr, periodSamples, ppq, spb, 0.0);
                        break;
                    case bSlipRoll:
                        if (! slip.on) { startRoll (slip, false, ppq, period, spb); slip.restartIdx = INT64_MIN; }
                        if (slip.restartIdx == INT64_MIN && slipRestart > 0.0)
                            slip.restartIdx = (int64_t) std::floor (ppq / slipRestart + 1.0e-9);
                        rollStep (slip, false, pl, pr, periodSamples, ppq, spb, slipRestart);
                        break;
                    case bBrake:
                    {
                        const double len = std::max (0.05 * sr, std::min (periodSamples, maxLoopSeconds * sr));
                        const double speed = std::max (0.0, 1.0 - brakeElapsed / len);
                        const float g = (float) std::min (1.0, speed * 4.0);
                        pl = g > 0.0f ? histRead (0, brakePos) * g : 0.0f;
                        pr = g > 0.0f ? histRead (1, brakePos) * g : 0.0f;
                        brakePos += speed;
                        brakeElapsed += 1.0;
                        break;
                    }
                    case bPitch:
                    {
                        const double st = pitchSemitones (lev);
                        amt = std::min (1.0f, std::max (0.0f, (std::abs (lev - 0.5f) - 0.03f) / 0.04f));
                        pitch.process (pl, pr, std::pow (2.0, st / 12.0));
                        break;
                    }
                    default: break;
                }
                oL += a * amt * (pl - fL);
                oR += a * amt * (pr - fR);
            }
            oL -= dryCut * fL;
            oR -= dryCut * fR;
            yL = softClip (oL);
            yR = softClip (oR);
        }
        else if (colorNeeded)
        {
            yL = softClip (yL);
            yR = softClip (yR);
        }
        L[i] = yL;
        R[i] = yR;
    }

    if (! roll.on) loopFill = 0;
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i]))
        {
            // Should never happen: drop all state rather than ever hand a host NaN / inf.
            const auto keep = absNow;
            reset();
            absNow = keep;
            std::memset (L, 0, sizeof (float) * (size_t) n);
            std::memset (R, 0, sizeof (float) * (size_t) n);
            break;
        }
    return true;
}
} // namespace dj
