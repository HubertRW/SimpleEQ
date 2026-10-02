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

    timerCallback(); // Discard audio from before this editor was opened.
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    stopTimer();
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

juce::Rectangle<float> ResponseCurveComponent::getAnalysisBounds() const
{
    return { 42.0f, 24.0f, juce::jmax(0.0f, getWidth() - 86.0f),
             juce::jmax(0.0f, getHeight() - 46.0f) };
}

bool ResponseCurveComponent::clearSpectra()
{
    bool changed = false;
    for (size_t channel = 0; channel < spectrumProducers.size(); ++channel)
    {
        changed = changed || !spectrumProducers[channel].getPath().isEmpty();
        spectrumProducers[channel].reset();
        receivedSamples[channel] = false;
    }
    return changed;
}

bool ResponseCurveComponent::drainChannel(
    int channel, const SimpleEQAudioProcessor::AnalyzerState& state, juce::uint32 now)
{
    const auto index = static_cast<size_t>(channel);
    auto& producer = spectrumProducers[index];
    bool changed = producer.setView(getAnalysisBounds(), state.sampleRate);
    // A fixed sample budget prevents offline rendering from monopolising the GUI.
    int remaining = juce::jmin(audioProcessor.getAvailableAnalyzerSamples(channel), 8192);
    while (remaining > 0)
    {
        const auto read = audioProcessor.pullAnalyzerSamples(
            channel, analyzerScratch, juce::jmin(remaining, 2048));
        if (read.numSamples == 0)
            break;
        remaining -= read.numSamples;
        if (read.generation != state.generation || read.sampleRate != state.sampleRate
            || channel >= state.channels)
            continue; // Stale audio is consumed but NEVER mapped at the new rate.
        if (read.discontinuity)
        {
            producer.reset();
            changed = true;
        }
        lastSampleTime[index] = now;
        receivedSamples[index] = true;
        if (producer.InitPath(analyzerScratch, 0))
            changed = true;
    }
    // Clear frozen traces when the host stops calling processBlock.
    if (receivedSamples[index] && now - lastSampleTime[index] > 500u)
    {
        producer.reset();
        receivedSamples[index] = false;
        changed = true;
    }
    return changed;
}

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool(0, 1))
        responseDirty = true;

    const auto state = audioProcessor.getAnalyzerState();
    if (!state.stable)
    {
        if (clearSpectra())
            repaint();
        return; // Preparation in progress. Never wait for the audio/lifecycle thread.
    }

    bool changed = false;
    if (!haveAnalyzerState || displayedGeneration != state.generation)
    {
        clearSpectra();
        // Consumer-side discard is safe while the producer continues writing.
        audioProcessor.discardAnalyzerSamples();
        displayedGeneration = state.generation;
        haveAnalyzerState = true;
        if (state.sampleRate > 0.0)
            displaySampleRate = state.sampleRate;
        responseDirty = true;
        changed = true;
    }

    if (state.sampleRate > 0.0)
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (int channel = 0; channel < 2; ++channel)
            if (drainChannel(channel, state, now))
                changed = true;
    }
    else
    {
        if (clearSpectra())
            changed = true;
        audioProcessor.discardAnalyzerSamples();
    }

    // A prepare/release could have happened during the bounded drain.
    const auto after = audioProcessor.getAnalyzerState();
    if (!after.stable || after.generation != state.generation)
    {
        clearSpectra();
        haveAnalyzerState = false;
        changed = true;
    }

    if (responseDirty)
    {
        rebuildResponsePath();
        changed = true;
    }
    if (changed)
        repaint();
}

void ResponseCurveComponent::resized()
{
    for (auto& producer : spectrumProducers)
        producer.setView(getAnalysisBounds(), displaySampleRate);
    responseDirty = true;
    rebuildResponsePath();
    repaint();
}

void ResponseCurveComponent::rebuildResponsePath()
{
    responsePath.clear();
    responseDirty = false;
    const auto bounds = getAnalysisBounds();
    if (bounds.isEmpty() || displaySampleRate <= 40.0)
        return;

    const auto settings = getChainSettings(audioProcessor.apvts, displaySampleRate);
    const auto peak = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        displaySampleRate, settings.peakFreq, settings.peakQuality,
        juce::Decibels::decibelsToGain(settings.peakGainInDecibels));
    const auto low = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
        settings.lowCutFreq, displaySampleRate, 2 * (settings.lowCutSlope + 1));
    const auto high = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
        settings.highCutFreq, displaySampleRate, 2 * (settings.highCutSlope + 1));

    const int width = juce::jmax(1, static_cast<int>(std::ceil(bounds.getWidth())));
    for (int pixel = 0; pixel <= width; ++pixel)
    {
        const float proportion = static_cast<float>(pixel) / width;
        const double frequency = 20.0 * std::pow(1000.0, proportion);
        if (frequency >= displaySampleRate * 0.5)
            break;
        double magnitude = peak->getMagnitudeForFrequency(frequency, displaySampleRate);
        for (const auto& coefficients : low)
            magnitude *= coefficients->getMagnitudeForFrequency(frequency, displaySampleRate);
        for (const auto& coefficients : high)
            magnitude *= coefficients->getMagnitudeForFrequency(frequency, displaySampleRate);
        const auto db = juce::jlimit(-30.0f, 30.0f,
            static_cast<float>(juce::Decibels::gainToDecibels(magnitude, -100.0)));
        const float x = bounds.getX() + proportion * bounds.getWidth();
        const float y = juce::jmap(db, -30.0f, 30.0f, bounds.getBottom(), bounds.getY());
        if (pixel == 0)
            responsePath.startNewSubPath(x, y);
        else
            responsePath.lineTo(x, y);
    }
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    const auto panelBounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(EQColours::panel);
    g.fillRoundedRectangle(panelBounds, 4.0f);
    const auto bounds = getAnalysisBounds();
    if (bounds.isEmpty())
        return;

    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.setColour(EQColours::labelText);
    g.drawText("POST-EQ  dBFS", 5, 3, 100, 16, juce::Justification::left);
    g.setColour(EQColours::spectrumLeft);
    g.drawText("L / MONO", 110, 3, 58, 16, juce::Justification::left);
    g.setColour(EQColours::spectrumRight);
    g.drawText("R", 177, 3, 18, 16, juce::Justification::left);
    g.setColour(EQColours::responseLine);
    g.drawText("EQ gain dB", getWidth() - 82, 3, 77, 16, juce::Justification::right);

    const float frequencies[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const juce::String labels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const auto x = bounds.getX() + std::log10(frequencies[i] / 20.0f) / 3.0f * bounds.getWidth();
        g.setColour(EQColours::responseGrid);
        g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
        g.setColour(EQColours::labelText.withAlpha(0.7f));
        g.drawText(labels[i], static_cast<int>(x) - 14, static_cast<int>(bounds.getBottom()) + 3,
                   28, 14, juce::Justification::centred);
    }
    for (int db = -100; db <= 0; db += 25)
    {
        const auto y = juce::jmap(static_cast<float>(db), -100.0f, 0.0f,
                                 bounds.getBottom(), bounds.getY());
        g.setColour(EQColours::responseGrid);
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
        g.setColour(EQColours::labelText.withAlpha(0.7f));
        g.drawText(juce::String(db), 2, static_cast<int>(y) - 6, 34, 12,
                   juce::Justification::right);
    }
    for (int db = -24; db <= 24; db += 12)
    {
        const auto y = juce::jmap(static_cast<float>(db), -30.0f, 30.0f,
                                 bounds.getBottom(), bounds.getY());
        g.setColour(EQColours::responseLine);
        const auto label = (db > 0 ? "+" : "") + juce::String(db);
        g.drawText(label, static_cast<int>(bounds.getRight()) + 4,
                   static_cast<int>(y) - 6, 34, 12, juce::Justification::left);
    }

    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());
        const juce::Colour colours[] = { EQColours::spectrumLeft, EQColours::spectrumRight };
        for (size_t channel = 0; channel < spectrumProducers.size(); ++channel)
        {
            const auto& path = spectrumProducers[channel].getPath();
            if (path.isEmpty())
                continue;
            auto fill = path;
            const auto end = path.getCurrentPosition();
            fill.lineTo(end.x, bounds.getBottom());
            fill.lineTo(path.getBounds().getX(), bounds.getBottom());
            fill.closeSubPath();
            g.setColour(colours[channel].withAlpha(0.08f));
            g.fillPath(fill);
            g.setColour(colours[channel].withAlpha(0.85f));
            g.strokePath(path, juce::PathStrokeType(1.2f));
        }
        // Cached EQ response sits above the measured spectrum, on its own dB axis.
        g.setColour(EQColours::responseLine.withAlpha(0.25f));
        g.strokePath(responsePath, juce::PathStrokeType(4.0f));
        g.setColour(EQColours::responseLine);
        g.strokePath(responsePath, juce::PathStrokeType(1.5f));
    }
    g.setColour(EQColours::panelBorder);
    g.drawRoundedRectangle(panelBounds, 4.0f, 1.0f);
}
