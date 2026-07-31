/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), ResponseCurve(p),
    lowCutFreqAttach(p.apvts, "LowCut Freq", LCFSlider), lowCutSlopeAttach(p.apvts, "LowCut Slope", LCSSlider),
    highCutFreqAttach(p.apvts, "HighCut Freq", HCFSlider), highCutSlopeAttach(p.apvts, "HighCut Slope", HCSSlider),
    peakFreqAttach(p.apvts, "Peak Freq", PFSlider), peakGainAttach(p.apvts, "Peak Gain", PGSlider),
    peakQualityAttach(p.apvts, "Peak Quality", PQSlider)

{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    for (auto* comp : getComps()) {
        comp->setLookAndFeel(&eqLookandFeel);
        addAndMakeVisible(comp);
    }
    addAndMakeVisible(ResponseCurve);

    setResizable(true, true);
    setResizeLimits(100, 100, 1500, 1500);
    setSize (1000, 600);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
    for (auto comp : getComps()) {
        comp->setLookAndFeel(nullptr);
    }
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Main background
    g.fillAll(EQColours::background);

    // Panel backgrounds use the same split as resized(), so the painted panels
    // stay aligned with the controls after resizing.
    auto bounds = getLocalBounds();
    bounds.removeFromTop(static_cast<int>(static_cast<float>(bounds.getHeight()) * 0.330f));

    const int panelPad = 4;
    const int thirdW = bounds.getWidth() / 3;

    auto lowCutPanel = bounds.removeFromLeft(thirdW).reduced(panelPad);
    auto highCutPanel = bounds.removeFromRight(thirdW).reduced(panelPad);
    auto peakPanel = bounds.reduced(panelPad);

    drawPanel(g, lowCutPanel, "Low Cut", EQColours::accentLowCut);
    drawPanel(g, highCutPanel, "High Cut", EQColours::accentHighCut);
    drawPanel(g, peakPanel, "Peak", EQColours::accentPeak);
}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(static_cast<int>(static_cast<float>(bounds.getHeight()) * 0.330f));
    ResponseCurve.setBounds(responseArea);
    
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

void SimpleEQAudioProcessorEditor::drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, juce::Colour accent)
{
    // Background
    g.setColour(EQColours::panel);
    g.fillRoundedRectangle(bounds.toFloat(), 6.f);

    // Border
    g.setColour(EQColours::panelBorder);
    g.drawRoundedRectangle(bounds.toFloat(), 6.f, 1.f);

    // Top accent line
    auto accentBar = bounds.removeFromTop(2).toFloat();
    juce::ColourGradient bar(accent.withAlpha(0.f), accentBar.getX(), accentBar.getY(),
        accent, accentBar.getCentreX(), accentBar.getY(),
        false);
    bar.addColour(1.0, accent.withAlpha(0.f));
    g.setGradientFill(bar);
    g.fillRoundedRectangle(accentBar, 1.f);

    // Title label — spaced uppercase
    juce::String spaced;
    for (int i = 0; i < title.length(); ++i)
    {
        spaced += juce::String::charToString(title[i]).toUpperCase();
        if (i < title.length() - 1) spaced += " ";
    }
    g.setColour(accent.withAlpha(0.75f));
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.f, juce::Font::plain)));
    g.drawText(spaced, bounds.getX(), bounds.getY() + 6, bounds.getWidth(), 14,
        juce::Justification::centred);
}

ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : audioProcessor(p) //DC
{
    for (auto* param : audioProcessor.getParameters()) {
        param->addListener(this);
    }

    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    for (auto* param : audioProcessor.getParameters()) {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int indexParameter, float newValue)
{
    juce::ignoreUnused(indexParameter, newValue);
    parametersChanged.set(1);
}

void ResponseCurveComponent::parameterGestureChanged(int, bool)
{
}

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool(0, 1))
    {
        repaint();
    }
}


void ResponseCurveComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.f);
    const int   w = static_cast<int>(bounds.getWidth());
    const float sr = audioProcessor.getSampleRate() > 0.0
        ? static_cast<float>(audioProcessor.getSampleRate())
        : 44100.f;

    // Panel background
    g.setColour(EQColours::panel);
    g.fillRoundedRectangle(bounds, 4.f);

    // Grid — vertical frequency lines
    g.setColour(EQColours::responseGrid);
    const float freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    const juce::String labels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
    for (int i = 0; i < 8; ++i)
    {
        const float nx = std::log10(freqs[i] / 20.f) / std::log10(20000.f / 20.f);
        const float px = bounds.getX() + nx * bounds.getWidth();
        g.drawVerticalLine(static_cast<int>(px), bounds.getY(), bounds.getBottom());
    }

    // Grid — horizontal dB lines
    const float dbs[] = { -24.f, -12.f, 0.f, 12.f, 24.f };
    for (auto db : dbs)
    {
        const float py = juce::jmap(db, -30.f, 30.f, bounds.getBottom(), bounds.getY());
        g.drawHorizontalLine(static_cast<int>(py), bounds.getX(), bounds.getRight());
    }

    // Frequency axis labels
    g.setColour(EQColours::labelText.withAlpha(0.55f));
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.f, juce::Font::plain)));
    for (int i = 0; i < 8; ++i)
    {
        const float nx = std::log10(freqs[i] / 20.f) / std::log10(20000.f / 20.f);
        const float px = bounds.getX() + nx * bounds.getWidth();
        g.drawText(labels[i],
            static_cast<int>(px) - 12,
            static_cast<int>(bounds.getBottom()) - 14,
            24, 12, juce::Justification::centred);
    }

    // dB axis labels (right edge)
    const juce::String dbLabels[] = { "-24", "-12", "0", "+12", "+24" };
    for (int i = 0; i < 5; ++i)
    {
        const float py = juce::jmap(dbs[i], -30.f, 30.f,
            bounds.getBottom(), bounds.getY());
        g.drawText(dbLabels[i],
            static_cast<int>(bounds.getRight()) - 26,
            static_cast<int>(py) - 6,
            24, 12, juce::Justification::right);
    }

    // --- Magnitude response path ---
    auto settings = getChainSettings(audioProcessor.apvts);

    auto peakCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sr, settings.peakFreq, settings.peakQuality,
        juce::Decibels::decibelsToGain(settings.peakGainInDecibels));

    auto lowCutCoeffsArray =
        juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
            settings.lowCutFreq, sr, 2 * (settings.lowCutSlope + 1));

    auto highCutCoeffsArray =
        juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
            settings.highCutFreq, sr, 2 * (settings.highCutSlope + 1));

    juce::Path curve;

    for (int px = 0; px < w; ++px)
    {
        const float normX = static_cast<float>(px) / static_cast<float>(w);
        const float freq = 20.f * std::pow(1000.f, normX);   // 20 Hz – 20 kHz log scale

        double mag = 1.0;
        mag *= peakCoeffs->getMagnitudeForFrequency(freq, sr);

        for (int s = 0; s <= settings.lowCutSlope &&
            s < static_cast<int>(lowCutCoeffsArray.size()); ++s)
            mag *= lowCutCoeffsArray[s]->getMagnitudeForFrequency(freq, sr);

        for (int s = 0; s <= settings.highCutSlope &&
            s < static_cast<int>(highCutCoeffsArray.size()); ++s)
            mag *= highCutCoeffsArray[s]->getMagnitudeForFrequency(freq, sr);

        const float db = static_cast<float>(juce::Decibels::gainToDecibels(mag));
        const float py = juce::jmap(db, -30.f, 30.f, bounds.getBottom(), bounds.getY());

        if (px == 0) curve.startNewSubPath(bounds.getX(), py);
        else         curve.lineTo(bounds.getX() + static_cast<float>(px), py);
    }

    // Filled area under curve
    juce::Path fill = curve;
    fill.lineTo(bounds.getRight(), bounds.getBottom());
    fill.lineTo(bounds.getX(), bounds.getBottom());
    fill.closeSubPath();
    g.setColour(EQColours::responseFill);
    g.fillPath(fill);

    // Curve line with glow
    g.setColour(EQColours::responseLine.withAlpha(0.3f));
    g.strokePath(curve, juce::PathStrokeType(4.f, juce::PathStrokeType::curved));
    g.setColour(EQColours::responseLine);
    g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));

    // Border
    g.setColour(EQColours::panelBorder);
    g.drawRoundedRectangle(bounds, 4.f, 1.f);
}
