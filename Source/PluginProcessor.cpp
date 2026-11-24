#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

DJToolsAudioProcessor::DJToolsAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DJToolsState", createParameterLayout())
{
    pColorFx    = apvts.getRawParameterValue ("color_fx");
    pColor      = apvts.getRawParameterValue ("color");
    pColorParam = apvts.getRawParameterValue ("color_param");
    pBeatFx     = apvts.getRawParameterValue ("beat_fx");
    pBeat       = apvts.getRawParameterValue ("beat");
    pTime       = apvts.getRawParameterValue ("time");
    pLevel      = apvts.getRawParameterValue ("level");
    pBand[0]    = apvts.getRawParameterValue ("band_low");
    pBand[1]    = apvts.getRawParameterValue ("band_mid");
    pBand[2]    = apvts.getRawParameterValue ("band_high");
    pOn         = apvts.getRawParameterValue ("beat_on");
    pHold       = apvts.getRawParameterValue ("beat_hold");

    setLicenceInfo (licence.check());
}

juce::AudioProcessorValueTreeState::ParameterLayout DJToolsAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    const StringArray colorNames { "Filter", "Sweep", "Dub Echo", "Space", "Noise", "Crush" };
    const StringArray beatNames { "Delay", "Echo", "Ping Pong", "Reverb", "Trans", "Filter", "Flanger", "Phaser",
                                  "Roll", "Slip Roll", "Brake", "Helix", "Spiral", "Pitch" };
    StringArray divNames;
    for (int i = 0; i < dj::numDivisions; ++i) divNames.add (String (dj::divisionName (i)) + (dj::divisionBeats[i] <= 1.0 ? " beat" : " beats"));
    jassert (colorNames.size() == dj::numColorFx && beatNames.size() == dj::numBeatFx);

    l.add (std::make_unique<AudioParameterChoice> (id ("color_fx"), "Color FX", colorNames, dj::cFilter));
    l.add (std::make_unique<AudioParameterFloat> (id ("color"), "Color", NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
               AudioParameterFloatAttributes()
                   .withStringFromValueFunction ([] (float v, int)
                   {
                       if (std::abs (v) < dj::colorDeadZone) return String ("Off");
                       return String (v < 0 ? "Left " : "Right ") + String (juce::roundToInt (std::abs (dj::Engine::colorAmount (v)) * 100.0f)) + " %";
                   })
                   .withValueFromStringFunction ([] (const String& s)
                   {
                       if (s.trim().equalsIgnoreCase ("off")) return 0.0f;
                       const float a = s.retainCharacters ("0123456789.").getFloatValue();
                       const float dial = dj::colorDeadZone + a / 100.0f * (100.0f - dj::colorDeadZone);
                       return s.trim().startsWithIgnoreCase ("l") || s.trim().startsWithChar ('-') ? -dial : dial;
                   })));
    l.add (std::make_unique<AudioParameterFloat> (id ("color_param"), "Color Param", NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
               AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (id ("beat_fx"), "Beat FX", beatNames, dj::bEcho));
    l.add (std::make_unique<AudioParameterChoice> (id ("beat"), "Beat", divNames, dj::defaultDivision));
    l.add (std::make_unique<AudioParameterFloat> (id ("time"), "Time", NormalisableRange<float> (0.0f, 2000.0f, 0.1f, 0.5f), 0.0f,
               AudioParameterFloatAttributes()
                   .withStringFromValueFunction ([] (float v, int) { return v <= 0.5f ? String ("Sync") : String (juce::roundToInt (v)) + " ms"; })
                   .withValueFromStringFunction ([] (const String& s) { return s.trim().equalsIgnoreCase ("sync") ? 0.0f : s.getFloatValue(); })));
    l.add (std::make_unique<AudioParameterFloat> (id ("level"), "Level/Depth", NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
               AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterBool> (id ("band_low"), "FX Freq Low", true));
    l.add (std::make_unique<AudioParameterBool> (id ("band_mid"), "FX Freq Mid", true));
    l.add (std::make_unique<AudioParameterBool> (id ("band_high"), "FX Freq High", true));
    l.add (std::make_unique<AudioParameterBool> (id ("beat_on"), "Beat On", false));
    l.add (std::make_unique<AudioParameterBool> (id ("beat_hold"), "Beat Hold", false));
    return l;
}

dj::Params DJToolsAudioProcessor::currentParams() const
{
    dj::Params p;
    p.colorFx    = juce::jlimit (0, dj::numColorFx - 1, (int) pColorFx->load());
    p.color      = pColor->load();
    p.colorParam = juce::jlimit (0.0f, 1.0f, pColorParam->load() / 100.0f);
    p.beatFx     = juce::jlimit (0, dj::numBeatFx - 1, (int) pBeatFx->load());
    p.division   = juce::jlimit (0, dj::numDivisions - 1, (int) pBeat->load());
    p.timeMs     = pTime->load();
    p.level      = juce::jlimit (0.0f, 1.0f, pLevel->load() / 100.0f);
    for (int b = 0; b < 3; ++b) p.band[b] = pBand[b]->load() > 0.5f;
    p.beatActive = pOn->load() > 0.5f || pHold->load() > 0.5f;
    return p;
}

void DJToolsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    monoScratch.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);
}

bool DJToolsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void DJToolsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);
    if (n <= 0) return;

    // ── Transport: host tempo + grid; free-running clock when there is none ──
    double bpm = standaloneBpm, ppq = -1.0;
    bool playing = false;
    if (wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto q = pos->getPpqPosition()) ppq = *q;
                playing = pos->getIsPlaying();
            }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    if (! playing || ppq < -1.0e8) ppq = freePpq;   // stopped: keep a free-running grid at the host tempo
    freePpq = ppq + n * bpm / (60.0 * sr);
    lastBpm.store (bpm);
    lastPpq.store (ppq);

    // ── The licence gate: no trial / key = audio untouched ──
    if (! dj::gate::audioAllowed (unlocked.load()))
        return;

    const auto params = currentParams();
    const dj::Transport tr { bpm, ppq };
    const int ch = juce::jmin (numIn, numOut);
    if (ch >= 2)
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, params, tr);
    else if (ch == 1)
    {
        // Mono: run the stereo engine on a copy and keep the left side.
        for (int pos = 0; pos < n;)
        {
            const int m = juce::jmin (n - pos, monoScratch.getNumSamples());
            auto* l = monoScratch.getWritePointer (0); auto* r = monoScratch.getWritePointer (1);
            juce::FloatVectorOperations::copy (l, buffer.getReadPointer (0, pos), m);
            juce::FloatVectorOperations::copy (r, buffer.getReadPointer (0, pos), m);
            const double q = ppq + pos * bpm / (60.0 * sr);
            if (engine.process (l, r, m, params, { bpm, q }))
                juce::FloatVectorOperations::copy (buffer.getWritePointer (0, pos), l, m);
            pos += m;
        }
    }
    colorTail.store (engine.colorTailLive());
    bool bt = false;
    for (int e = 0; e < dj::numBeatFx; ++e) if (engine.sendLive (e)) bt = true;
    beatTail.store (bt);
}

void DJToolsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // HOLD is momentary: never save it pressed.
    if (auto xml = state.createXml())
    {
        for (auto* e : xml->getChildIterator())
            if (e->getStringAttribute ("id") == "beat_hold") e->setAttribute ("value", 0.0);
        copyXmlToBinary (*xml, destData);
    }
}

void DJToolsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DJToolsAudioProcessor::createEditor() { return new DJToolsAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DJToolsAudioProcessor(); }
