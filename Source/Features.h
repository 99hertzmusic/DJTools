#pragma once
// DJ Tools: effect lists, beat divisions and THE licence gate.
// Everything about what is locked lives in this file: the processor (audio), the editor (overlay)
// and the tests all read it.
//
// The order of the lists below is the parameter order saved in sessions and automation.
// Never reorder or insert: only append (and then bump the parameter version).

namespace dj
{
// ── COLOR section: one-knob effects (bipolar COLOR dial, centre = off) ──────────
// Slot 4 was NOISE (removed 24 Nov 2026). The slot stays so saved sessions and automation keep their
// indices (CRUSH is still 5). Anything that lands on slot 4 (an old session, a host setting the raw
// value) plays FILTER: always go through colorSlot().
enum ColorFx { cFilter = 0, cSweep, cDubEcho, cSpace, cRetired4, cCrush, numColorFx };

inline int colorSlot (int i)
{
    if (i < 0 || i >= numColorFx || i == cRetired4) return cFilter;
    return i;
}

// The effects the COLOR keys show, left to right.
inline constexpr int numColorKeys = 5;
inline constexpr int colorKeyFx[numColorKeys] = { cFilter, cSweep, cDubEcho, cSpace, cCrush };

inline const char* colorName (int i)
{
    static const char* n[] = { "FILTER", "SWEEP", "DUB ECHO", "SPACE", "FILTER", "CRUSH" };
    return n[colorSlot (i)];
}
// What turning the COLOR dial left / right does (silk under the big dial).
inline const char* colorLeft (int i)
{
    static const char* n[] = { "LOW PASS", "GATE", "MID ECHO", "LOW ROOM", "LOW PASS", "DRIVE" };
    return n[colorSlot (i)];
}
inline const char* colorRight (int i)
{
    static const char* n[] = { "HIGH PASS", "BAND", "HIGH ECHO", "HIGH ROOM", "HIGH PASS", "CRUSH" };
    return n[colorSlot (i)];
}
inline const char* colorParamName (int i)
{
    static const char* n[] = { "RESONANCE", "DEPTH", "FEEDBACK", "SIZE", "RESONANCE", "INTENSITY" };
    return n[colorSlot (i)];
}

// COLOR dial: -100..+100. Inside +-colorDeadZone it is OFF (bit-exact passthrough), so a hardware
// knob parked at its centre click (MIDI 63 or 64) is always off.
inline constexpr float colorDeadZone = 5.0f;

// ── BEAT section: tempo-synced effects ─────────────────────────────────────────
enum BeatFx { bDelay = 0, bEcho, bPingPong, bReverb, bTrans, bFilter, bFlanger, bPhaser,
              bRoll, bSlipRoll, bBrake, bHelix, bSpiral, bPitch, numBeatFx };

inline const char* beatName (int i)
{
    static const char* n[] = { "DELAY", "ECHO", "PING PONG", "REVERB", "TRANS", "FILTER", "FLANGER", "PHASER",
                               "ROLL", "SLIP ROLL", "BRAKE", "HELIX", "SPIRAL", "PITCH" };
    return n[i >= 0 && i < numBeatFx ? i : 0];
}
inline const char* beatParamName (int i) { return i == bPitch ? "PITCH" : "AMOUNT"; }

// Send effects keep ringing after they are switched off (the DJ feel); insert effects stop.
inline bool beatIsSend (int i)
{
    return i == bDelay || i == bEcho || i == bPingPong || i == bReverb || i == bHelix || i == bSpiral;
}

// BEAT dial steps, in beats.
inline constexpr int numDivisions = 10;
inline constexpr double divisionBeats[numDivisions] = { 0.0625, 0.125, 0.25, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0 };
inline const char* divisionName (int i)
{
    static const char* n[] = { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8", "16" };
    return n[i >= 0 && i < numDivisions ? i : 3];
}
inline constexpr int defaultDivision = 3;   // 1/2 beat

// ── THE LICENCE GATE ───────────────────────────────────────────────────────────
// DJ Tools has no free tier: a valid 7-day trial or a bought key unlocks everything.
// Without one the plugin passes audio through untouched (bit-exact) and shows the licence panel.
namespace gate
{
    inline constexpr bool freeTier = false;
    inline bool audioAllowed (bool unlocked) { return freeTier || unlocked; }
}

inline constexpr const char* licenceProduct = "djtools";
inline constexpr const char* buyUrl = "https://99hertz.com/dj-tools?utm_source=plugin&utm_medium=djtools&utm_campaign=upgrade";
} // namespace dj
