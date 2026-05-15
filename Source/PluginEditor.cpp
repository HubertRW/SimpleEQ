/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    for (auto* p : getComps()) {
        addAndMakeVisible(p);
    }

    setSize (600, 400);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::orange);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.330);
    
    // Divide the remaining strip into three equal sections
    const int thirdWidth = bounds.getWidth() / 3;

    auto lowCutArea = bounds.removeFromLeft(thirdWidth);
    auto highCutArea = bounds.removeFromRight(thirdWidth);
    auto peakArea = bounds; // centre remainder

    const int halfH = lowCutArea.getHeight() / 2;

    LCFSlider.setBounds(lowCutArea.removeFromTop(halfH));
    LCSSlider.setBounds(lowCutArea);

    HCFSlider.setBounds(highCutArea.removeFromTop(halfH));
    HCSSlider.setBounds(highCutArea);

    const int thirdH = peakArea.getHeight() / 3;
    PFSlider.setBounds(peakArea.removeFromTop(thirdH));
    PGSlider.setBounds(peakArea.removeFromTop(thirdH));
    PQSlider.setBounds(peakArea);
    

}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
    return {
        &LCFSlider,
        &LCSSlider,
        &HCFSlider,
        &HCSSlider,
        &PFSlider,
        &PGSlider,
        &PQSlider
    };

}
