/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>
#include "MonoChannelSampleFifo.h"

//==============================================================================
SimpleEQAudioProcessor::SimpleEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

SimpleEQAudioProcessor::~SimpleEQAudioProcessor()
{
}

//==============================================================================

const juce::String SimpleEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleEQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleEQAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String SimpleEQAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void SimpleEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void SimpleEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Host lifecycle calls are serialized with processBlock, but not with the GUI.
    // Publish an odd generation while changing format; never reset a live FIFO.
    analyzerGeneration.fetch_add(1);
    captureSampleRate = sampleRate;
    expectedTransportSample.reset();
    filtersInitialized = false;
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    juce::dsp::ProcessSpec spec;

    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = 1;

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    leftChain.reset();
    rightChain.reset();
    leftSampleFifo.markDiscontinuity();
    rightSampleFifo.markDiscontinuity();

    updateFilters();

    analyzerSampleRate.store(sampleRate);
    analyzerChannels.store(getTotalNumOutputChannels());
    captureGeneration = analyzerGeneration.fetch_add(1) + 1;

}


void SimpleEQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    analyzerGeneration.fetch_add(1);
    captureSampleRate = 0.0;
    expectedTransportSample.reset();
    analyzerSampleRate.store(0.0);
    analyzerChannels.store(0);
    captureGeneration = analyzerGeneration.fetch_add(1) + 1;

}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpleEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0
        || captureSampleRate <= 0.0)
        return;

    updateFilters();

    juce::dsp::AudioBlock<float> block(buffer);

    auto leftBlock = block.getSingleChannelBlock(0);
    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    leftChain.process(leftContext);

    if (buffer.getNumChannels() > 1)
    {
        auto rightBlock = block.getSingleChannelBlock(1);
        juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
        rightChain.process(rightContext);
    }

    captureAnalyzerSamples(buffer);
}

void SimpleEQAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0 && captureSampleRate > 0.0)
        captureAnalyzerSamples(buffer);
}

void SimpleEQAudioProcessor::captureAnalyzerSamples(const juce::AudioBuffer<float>& buffer)
{
    // Mark starts/seeks/loops without touching the consumer's indices.
    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto sample = position->getTimeInSamples();
                sample && position->getIsPlaying())
            {
                if (!expectedTransportSample || *sample != *expectedTransportSample)
                {
                    leftSampleFifo.markDiscontinuity();
                    rightSampleFifo.markDiscontinuity();
                }
                expectedTransportSample = *sample + buffer.getNumSamples();
            }
            else
            {
                if (expectedTransportSample)
                {
                    leftSampleFifo.markDiscontinuity();
                    rightSampleFifo.markDiscontinuity();
                }
                expectedTransportSample.reset();
            }
        }
        else
            expectedTransportSample.reset();
    }
    else
        expectedTransportSample.reset();

    // Analyzer-only copies AFTER filtering. Full queues drop visualization data.
    leftSampleFifo.push(buffer, captureSampleRate, captureGeneration);
    rightSampleFifo.push(buffer, captureSampleRate, captureGeneration);
}

SimpleEQAudioProcessor::AnalyzerState SimpleEQAudioProcessor::getAnalyzerState() const noexcept
{
    const auto before = analyzerGeneration.load();
    if ((before & 1u) != 0)
        return {};
    const auto rate = analyzerSampleRate.load();
    const auto channels = analyzerChannels.load();
    const auto after = analyzerGeneration.load();
    return { rate, after, channels, before == after };
}

MonoChannelSampleFifo::ReadResult SimpleEQAudioProcessor::pullAnalyzerSamples(
    int channel, juce::AudioBuffer<float>& destination, int maxSamples)
{
    if (channel == 0)
        return leftSampleFifo.pull(destination, maxSamples);
    if (channel == 1)
        return rightSampleFifo.pull(destination, maxSamples);
    destination.setSize(1, 0);
    return {};
}

int SimpleEQAudioProcessor::getAvailableAnalyzerSamples(int channel) const noexcept
{
    return channel == 0 ? leftSampleFifo.getNumAvailableSamples()
         : channel == 1 ? rightSampleFifo.getNumAvailableSamples() : 0;
}

void SimpleEQAudioProcessor::discardAnalyzerSamples() noexcept
{
    leftSampleFifo.discardPending();
    rightSampleFifo.discardPending();
}

//==============================================================================
bool SimpleEQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleEQAudioProcessor::createEditor()
{
    return new SimpleEQAudioProcessorEditor (*this);

    //return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream MOS(destData, true);
    apvts.state.writeToStream(MOS);
}

void SimpleEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

    auto valueTree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (valueTree.isValid()) {
        apvts.replaceState(valueTree);
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts, double sampleRate)
{
    ChainSettings Settings;

    Settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    Settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    Settings.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    Settings.peakGainInDecibels = apvts.getRawParameterValue("Peak Gain")->load();
    Settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    Settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    Settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());

    if (sampleRate > 0.0)
    {
        const auto maximum = static_cast<float>(sampleRate * 0.499);
        Settings.lowCutFreq = juce::jmin(Settings.lowCutFreq, maximum);
        Settings.highCutFreq = juce::jmin(Settings.highCutFreq, maximum);
        Settings.peakFreq = juce::jmin(Settings.peakFreq, maximum);
    }
    return Settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //low frequency parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut Freq", "LowCut Freq", 
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.4f), 20.f));

    //high frequency parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut Freq", "HighCut Freq",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 1.0f), 20000.0f));

    //peak frequency
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Freq", "Peak Freq", 
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 1.0f), 750.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Gain", "Peak Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.5f, 1.0f), 0.0f));

    //adjustments for the peak: how narrow/wide it is
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Quality", "Peak Quality",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.05f, 1.0f), 1.0f));

    juce::StringArray stringArray;
    for (int i = 0; i < 4; i++) {
        juce::String str;
        str << (12 + i * 12);
        str << " dB/Oct";
        stringArray.add(str);
    }

    layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut Slope", "LowCut Slope", stringArray, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut Slope", "HighCut Slope", stringArray, 0));

    return layout;
}

void SimpleEQAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
    auto peakCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(getSampleRate(), 
        chainSettings.peakFreq, chainSettings.peakQuality, 
        juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));

    *leftChain.get<chainPositions::Peak>().coefficients = *peakCoefficients;
    *rightChain.get<chainPositions::Peak>().coefficients = *peakCoefficients;
}

void SimpleEQAudioProcessor::updateLowCutFilter(const ChainSettings& chainSettings)
{
    auto lowCutCoefficients = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
        chainSettings.lowCutFreq, getSampleRate(),
        2 * (chainSettings.lowCutSlope + 1)
    );

    auto& leftLowCut = leftChain.get<chainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<chainPositions::LowCut>();

    updateCutFilter(leftLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(rightLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
}

void SimpleEQAudioProcessor::updateHighCutFilter(const ChainSettings& chainSettings)
{
    auto highcutCoefficients = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
        chainSettings.highCutFreq, getSampleRate(),
        2 * (chainSettings.highCutSlope + 1)
    );

    auto& leftHighCut = leftChain.get<chainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<chainPositions::HighCut>();

    updateCutFilter(leftHighCut, highcutCoefficients, chainSettings.highCutSlope);
    updateCutFilter(rightHighCut, highcutCoefficients, chainSettings.highCutSlope);
}

void SimpleEQAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts, captureSampleRate);

    // Do not allocate new coefficient designs every unchanged audio block.
    if (filtersInitialized
        && chainSettings.lowCutFreq == previousSettings.lowCutFreq
        && chainSettings.highCutFreq == previousSettings.highCutFreq
        && chainSettings.peakFreq == previousSettings.peakFreq
        && chainSettings.peakGainInDecibels == previousSettings.peakGainInDecibels
        && chainSettings.peakQuality == previousSettings.peakQuality
        && chainSettings.lowCutSlope == previousSettings.lowCutSlope
        && chainSettings.highCutSlope == previousSettings.highCutSlope)
        return;

    updatePeakFilter(chainSettings);
    updateLowCutFilter(chainSettings);
    updateHighCutFilter(chainSettings);
    previousSettings = chainSettings;
    filtersInitialized = true;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleEQAudioProcessor();
}

