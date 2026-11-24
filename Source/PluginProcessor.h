#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Features.h"
#include "Engine.h"
#include "Licence/Licence.h"

// DJ Tools: a club mixer's effects section in the DAW.
//  COLOR: one-knob effects (FILTER, SWEEP, DUB ECHO, SPACE, CRUSH) on a bipolar dial, centre = off.
//  BEAT:  tempo-synced effects (delays, reverb, trans, filter, flanger, phaser, rolls, brake, pitch)
//         with BEAT division, TIME, AMOUNT, BAND keys, FX ON and momentary HOLD.
// Every control is a plain automatable parameter, so a MIDI controller can be mapped in the DAW.
// No free tier: without a trial or a key the audio passes through untouched (Features.h, dj::gate).
class DJToolsAudioProcessor : public juce::AudioProcessor
{
public:
    DJToolsAudioProcessor();
    ~DJToolsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DJ Tools"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Licence ─────────────────────────────────────────────────────────────
    hz::Licence licence { dj::licenceProduct };
    hz::LicenceInfo licenceInfo;                 // message thread
    std::atomic<bool> unlocked { false };        // audio thread reads this
    std::atomic<int> licenceVersion { 0 };       // bumps on every change, so open editors can follow
    void setLicenceInfo (const hz::LicenceInfo& i) { licenceInfo = i; unlocked.store (i.unlocked()); ++licenceVersion; }
    std::unique_ptr<hz::LicenceWatcher> licenceWatcher;   // picks up keys saved by other instances / trial expiry

    // ── What the editor shows ───────────────────────────────────────────────
    dj::Params currentParams() const;
    std::atomic<double> lastBpm { 128.0 }, lastPpq { 0.0 };
    std::atomic<bool> colorTail { false }, beatTail { false };
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    // Standalone (no DAW): free-running clock.
    static constexpr double standaloneBpm = 128.0;

    dj::Engine& getEngine() { return engine; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* pColorFx = nullptr; std::atomic<float>* pColor = nullptr; std::atomic<float>* pColorParam = nullptr;
    std::atomic<float>* pBeatFx = nullptr;  std::atomic<float>* pBeat = nullptr;  std::atomic<float>* pTime = nullptr;
    std::atomic<float>* pLevel = nullptr;   std::atomic<float>* pBand[3] {};
    std::atomic<float>* pOn = nullptr;      std::atomic<float>* pHold = nullptr;

    dj::Engine engine;
    juce::AudioBuffer<float> monoScratch;
    double sr = 48000.0;
    double freePpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DJToolsAudioProcessor)
};
