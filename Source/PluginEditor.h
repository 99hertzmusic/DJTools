#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class HardwareLookAndFeel;

// Grid of hardware lamp keys bound to a choice parameter (effect selectors).
class KeyGrid : public juce::Component
{
public:
    KeyGrid (juce::RangedAudioParameter& param, juce::StringArray labels, int columns);
    void resized() override;
    void showValue (int value);
    int gapX = 8, gapY = 6;
private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
    int cols = 1, shown = -1;
};

// Lamp key bound to a bool parameter. Momentary keys (HOLD) are on only while pressed.
class ParamKey : public juce::TextButton
{
public:
    ParamKey (juce::RangedAudioParameter& param, const juce::String& text, bool momentary);
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void clicked() override {}
private:
    juce::RangedAudioParameter& prm;
    juce::ParameterAttachment attachment;
    const bool momentary;
};

// Rotary dial. Bipolar dials (COLOR) stick to the centre so OFF is easy to find with a mouse.
class Dial : public juce::Slider
{
public:
    float centreSnap = 0.0f;   // > 0: values within +-centreSnap snap to 0 while dragging
    double snapValue (double v, DragMode) override { return (centreSnap > 0.0f && std::abs (v) < centreSnap) ? 0.0 : v; }
};

// Licence panel: start the 7-day trial, paste a key, or buy.
class ProPanel : public juce::Component
{
public:
    explicit ProPanel (DJToolsAudioProcessor& p);
    void show (const juce::String& reason);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // click outside the card closes it
    std::function<void()> onLicenceChanged;
private:
    void refresh();
    juce::Rectangle<int> card() const;
    DJToolsAudioProcessor& proc;
    juce::String reason, message;
    bool messageIsError = false, busy = false;
    juce::TextEditor emailBox, keyBox;
    juce::ToggleButton subscribe { "Send me tips and offers" };
    juce::TextButton trialButton { "START 7-DAY FREE TRIAL" }, unlockButton { "UNLOCK" },
                     buyButton { "BUY DJ TOOLS" }, closeButton { "CLOSE" };
};

// Covers the controls while there's no trial or key.
class LockOverlay : public juce::Component
{
public:
    explicit LockOverlay (DJToolsAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onOpen;
private:
    DJToolsAudioProcessor& proc;
    juce::TextButton openButton { "START FREE TRIAL / ENTER KEY" };
};

// Small LED screen: the beat grid with the division lit and the play position.
class BeatGrid : public juce::Component
{
public:
    explicit BeatGrid (DJToolsAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    DJToolsAudioProcessor& proc;
};

class DJToolsAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit DJToolsAudioProcessorEditor (DJToolsAudioProcessor&);
    ~DJToolsAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void openPanel (const juce::String& reason) { proPanel.show (reason); }
    void showSection (bool beat) { ledBeat = beat; ledTouch = 0; }   // tests

private:
    struct Knob
    {
        Dial slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label, paramId, value;
        juce::StringArray stepLabels;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, juce::StringArray steps = {});
    Knob* knob (const juce::String& id);
    void renderBackground();
    void updateLicence();
    void updateLed();

    DJToolsAudioProcessor& proc;
    std::unique_ptr<HardwareLookAndFeel> laf;

    KeyGrid colorKeys, beatKeys;
    ParamKey bandLow, bandMid, bandHigh, holdKey, onKey;
    BeatGrid gridView;
    juce::TextButton licenceButton;
    juce::OwnedArray<Knob> knobs;
    LockOverlay lockOverlay;
    ProPanel proPanel;

    juce::Image background;
    juce::Rectangle<int> ledArea, secColor, secBeat;
    juce::String ledLabel, ledText, ledRightText;
    bool ledBeat = false;
    int ledTouch = 0;                       // frames left showing a just-touched secondary value
    juce::String touchedLabel, touchedText;
    std::map<juce::String, float> lastValues;
    bool wasUnlocked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DJToolsAudioProcessorEditor)
};
