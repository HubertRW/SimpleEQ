/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQCustomLook.h"
#include "PathProducer.h"
#include <array>



//==============================================================================
/**
*/

class RotarySlider : public juce::Slider {
private:
    juce::String labelStr;
    juce::Colour accent;

public:
    static constexpr int kLabelH = 16;
    static constexpr float kKnobScale = 0.8f;
    static constexpr float kKnobYOffset = 25.f;

    explicit RotarySlider(juce::String text, juce::Colour accentColour) : juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
    labelStr(text), accent(accentColour)
    {
        getProperties().set("accentColour", accentColour.toString());
    }

    void paint(juce::Graphics& g)  override {
        
        auto bounds = getLocalBounds();
        auto knobBounds = bounds.withTrimmedBottom(kLabelH).toFloat();
        knobBounds = knobBounds.withSizeKeepingCentre(knobBounds.getWidth() * kKnobScale,
                                                      knobBounds.getHeight() * kKnobScale);
        knobBounds.translate(0.f, kKnobYOffset);

        const float position = static_cast<float>((getValue() - getMinimum()) / (getMaximum() - getMinimum()));

        getLookAndFeel().drawRotarySlider(g, static_cast<int>(knobBounds.getX()), static_cast<int>(knobBounds.getY()), 
            static_cast<int>(knobBounds.getWidth()), static_cast<int>(knobBounds.getHeight()), position,
            juce::MathConstants<float>::pi * 1.25f,
            juce::MathConstants<float>::pi * 2.75f, 
            *this);

        
        //making the label as uppercase and spaced

        juce::String spaced;    
        for (int i = 0; i < labelStr.length(); ++i) {
            spaced += juce::String::charToString(labelStr[i]).toUpperCase();

            if (i < labelStr.length() - 1) {
                spaced += " ";
            }
        }

        g.setColour(EQColours::labelText);
        g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::plain)));
        g.drawText(spaced, 0, bounds.getHeight() - kLabelH, bounds.getWidth(), kLabelH, juce::Justification::centred, 1);
    }

    void resized() override {}

};

class ResponseCurveComponent : public juce::Component, public juce::Timer,
    public juce::AudioProcessorParameter::Listener
{
private:
    juce::Atomic<int> parametersChanged = 1;
    SimpleEQAudioProcessor& audioProcessor;
    std::array<PathProducer, 2> spectrumProducers;
    juce::AudioBuffer<float> analyzerScratch { 1, 2048 };
    juce::Path responsePath;
    std::array<juce::uint32, 2> lastSampleTime {};
    std::array<bool, 2> receivedSamples {};
    std::uint64_t displayedGeneration = 0;
    double displaySampleRate = 44100.0;
    bool haveAnalyzerState = false;
    bool responseDirty = true;

    juce::Rectangle<float> getAnalysisBounds() const;
    void rebuildResponsePath();
    bool clearSpectra();
    bool drainChannel(int channel, const SimpleEQAudioProcessor::AnalyzerState& state,
                      juce::uint32 now);

public:
    explicit ResponseCurveComponent( SimpleEQAudioProcessor& p);
    ~ResponseCurveComponent();
    
    void parameterValueChanged(int indexParameter, float newValue) override;
    void parameterGestureChanged(int, bool) override;
    void timerCallback() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Read-only geometry, owned and accessed on the message thread.
    const juce::Path& getSpectrumPath(int channel) const
    {
        jassert(channel == 0 || channel == 1);
        return spectrumProducers[static_cast<size_t>(juce::jlimit(0, 1, channel))].getPath();
    }
};


class SimpleEQAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor&);
    ~SimpleEQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;



private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    SimpleEQAudioProcessor& audioProcessor;
    EQCustomLook eqLookandFeel;
    ResponseCurveComponent ResponseCurve;


    // LCF(low cut frequency), LCS(low cut slope)
    // HCF(high cut frequency), HCS(high cut slope)
    // PF(peak frequency), PG(peak gain), PQ(peak quality)

    RotarySlider LCFSlider{ "Freq", EQColours::accentLowCut };
    RotarySlider LCSSlider{ "Slope", EQColours::accentLowCut };
    RotarySlider HCFSlider{"Freq", EQColours::accentHighCut};
    RotarySlider HCSSlider{ "Slope", EQColours::accentHighCut };
    RotarySlider PGSlider{ "Gain", EQColours::accentPeak }, PQSlider{ "Q", EQColours::accentPeak }, 
        PFSlider{ "Freq", EQColours::accentPeak };


    std::vector<juce::Component*> getComps();

    static void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, juce::Colour accent);
    
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    
    Attachment lowCutFreqAttach, lowCutSlopeAttach,
        highCutFreqAttach, highCutSlopeAttach,
        peakFreqAttach, peakGainAttach, peakQualityAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessorEditor)
};

