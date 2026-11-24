#pragma once
// DJ Tools audio engine: COLOR section (one-knob effects) into BEAT section (tempo-synced effects).
// No JUCE in here so the tests can drive it directly. Allocation-free after prepare().
#include "DSP.h"
#include "Features.h"
#include <cstdint>
#include <vector>

namespace dj
{
struct Params
{
    int   colorFx = cFilter;
    float color = 0.0f;          // -100..100, +-colorDeadZone = off
    float colorParam = 0.5f;     // 0..1
    int   beatFx = bEcho;
    int   division = defaultDivision;
    float timeMs = 0.0f;         // 0 = follow BEAT, otherwise the period in ms
    float level = 0.5f;          // 0..1 (PITCH: 0.5 = no shift)
    bool  band[3] = { true, true, true };
    bool  beatActive = false;    // ON or HOLD
};

struct Transport
{
    double bpm = 128.0;
    double ppq = 0.0;            // musical position (quarter notes) of the block's first sample
};

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();

    // In place. Returns false when the block was left completely untouched (bit-exact passthrough).
    bool process (float* L, float* R, int n, const Params& p, const Transport& t);

    static double periodBeats (const Params& p, double bpm);
    static float  colorAmount (float dial);              // -1..1 after the dead zone
    static double colorCutoffHz (float v, float param);  // FILTER cutoff for the display
    static double pitchSemitones (float level);

    bool sendLive (int beatFx) const;
    bool colorTailLive() const { return dubLive || spaceLive; }
    float beatAmount (int fx) const { return beatA[fx].v; }
    double sampleRate() const { return sr; }

    static constexpr double maxEchoSeconds = 4.0;
    static constexpr double maxLoopSeconds = 8.0;

private:
    struct Echo
    {
        dsp::DelayLine d[2];
        // Delay time: a BEAT / TIME change crossfades between the old and the new tap (40 ms), so the
        // repeats never jump or warble.
        double curD = 1.0, nextD = 1.0, targetD = 1.0;
        int    xfPos = 0, xfLen = 1;
        bool   xfading = false;
        dsp::Glide fb;
        dsp::OnePole lp[2], hp[2];
        dsp::Allpass ap[2][3];
        int quiet = 0;
        bool live = false;
    };
    struct Roll
    {
        bool   on = false;
        int64_t start = 0;
        double rp = 0.0, L = 1.0, oldAbs = 0.0;
        int    fade = 0, fadeLen = 1;
        int64_t restartIdx = 0;
    };

    void  startRoll (Roll& r, bool useLoop, double ppq, double period, double spb);
    float histRead (int ch, double pos) const;
    float loopRead (int ch, double pos) const;
    void  rollStep (Roll& r, bool useLoop, float& outL, float& outR, double periodSamples, double ppq, double spb, double restartBeats);
    void  processEcho (int fx, Echo& e, float a, float inL, float inR, float& wl, float& wr);
    void  clearEcho (Echo& e);

    double sr = 48000.0;
    int64_t absNow = 0;

    // ── COLOR ──
    dsp::Glide colorS, paramS;
    dsp::Ramp  colorW[numColorFx];
    dsp::Svf   fFilter[2];
    float      swEnv = 0.0f, swSlow = 0.0f, swGain = 1.0f;
    dsp::Svf   swHp[2], swBp[2];
    dsp::DelayLine dubLine[2];
    dsp::Glide dubTime;
    dsp::Svf   dubSendA[2], dubSendB[2];
    dsp::OnePole dubLp[2], dubHp[2];
    int  dubQuiet = 0; bool dubLive = false;
    dsp::Fdn   space;
    dsp::Svf   spSendA[2], spSendB[2];
    int  spaceQuiet = 0; bool spaceLive = false;
    dsp::Noise noise[2];
    dsp::Svf   noiseF[2];
    float      crHold[2] {}, crPhase = 0.0f;
    dsp::Svf   crHp[2];
    int        colorCtl = 0;

    // ── BEAT ──
    dsp::Ramp  beatA[numBeatFx];
    dsp::Glide levelS;
    dsp::Ramp  bandG[3];
    dsp::Biquad lowA[2], lowB[2], highA[2], highB[2];
    Echo echoes[numBeatFx];          // used by DELAY, ECHO, PING PONG, HELIX, SPIRAL
    dsp::Fdn   reverb;
    dsp::OnePole revHp[2];
    int  revQuiet = 0; bool revLive = false;
    dsp::Ramp  transG;
    dsp::Svf   bfSvf[2];
    dsp::DelayLine flLine[2];
    float      flFb[2] {};
    float      phZ[2][6] {}, phFb[2] {};
    std::vector<float> hist[2];
    int64_t    histMask = 0, histValidFrom = 0;
    Roll       roll, slip;
    std::vector<float> loopBuf[2];   // ROLL keeps its own copy, so it can be held for as long as you like
    int        loopFill = 0;
    double     brakePos = 0.0, brakeElapsed = 0.0;
    dsp::PitchShift pitch;
    double     revRt = -1.0;
    bool       fresh = true;
};
} // namespace dj
