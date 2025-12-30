/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//states for cut slopes: 12, 24, 36, and 48 DB
enum Slope {
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

class ChainSettings {
public:
    Slope highCutSlope{ Slope::Slope_12 }, lowCutSlope{ Slope::Slope_12 };
    float peakFreq{ 0 }, peakQuality{ 1.0 };
    float peakGainInDecibels{ 0 };
    float highCutFreq{ 0 }, lowCutFreq{ 0 };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);


//==============================================================================
/**
*/
class SimpleEQAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleEQAudioProcessor();
    ~SimpleEQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameter", createParameterLayout()};

private:
    
    using Filter = juce::dsp::IIR::Filter<float>;
    using Gain = juce::dsp::Gain<float>;

    //cutting slopes for 12, 24, 36, and 48 DB
    using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

    using MonoFilter = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

    MonoFilter leftChain;
    MonoFilter rightChain;

    enum chainPositions {
        
        LowCut,
        Peak,
        HighCut
    };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessor)
};
