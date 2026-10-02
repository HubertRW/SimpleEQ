/*
  ==============================================================================

    PathProducer.h
    Created: 7 Aug 2026 9:33:36pm
    Author:  huber

  ==============================================================================
*/

#pragma once
#include "FFTDataGen.h"

/** Converts analysis-thread samples into a cached spectrum outline.

    Use on the message thread only, with samples drained from an audio-thread
    FIFO. Do NOT pass a live processBlock buffer across threads or call this
    from the audio callback. Path construction can allocate.

    Typical editor workflow:
      timer: setView(plotBounds, sampleRate), then InitPath(drainedBuffer, 0).
      paint: g.strokePath(getPath(), juce::PathStrokeType(1.0f)).

    Bounds must use the same coordinate system as the Graphics used to paint.
    The x axis stays at 20 Hz to 20 kHz to match the existing EQ grid; bins above
    Nyquist are omitted. The y axis is -100 to 0 dBFS, NOT the EQ's gain axis.
*/
class PathProducer
{
public:
    explicit PathProducer(int order = Order4096) : dataGen(order) {}

    // Call before feeding samples and whenever bounds or sample rate change.
    // Returns true when the editor should repaint (including clearing a trace).
    bool setView(juce::Rectangle<float> bounds, double sampleRate)
    {
        const auto validRate = std::isfinite(sampleRate) && sampleRate > 40.0
                                 ? sampleRate : 0.0;
        if (bounds == plotBounds && validRate == currentSampleRate)
            return false;

        if (validRate != currentSampleRate)
            reset(); // Do not combine samples captured at different rates.

        plotBounds = bounds;
        currentSampleRate = validRate;
        rebuildPath(); // A resize must also work without another audio frame.
        return true;
    }

    // channel is a zero-based index: 0 = left/mono, 1 = right.
    // Returns true when a new frame or a channel switch requires repainting.
    // false is normal: fewer than N samples may have accumulated so far.
    // Invalid channels are ignored without disturbing the previous spectrum.
    bool InitPath(const juce::AudioBuffer<float>& buffer, int channel = 0)
    {
        if (currentSampleRate == 0.0 || channel < 0
            || channel >= buffer.getNumChannels())
            return false;

        bool clearedPath = false;
        if (channel != currentChannel)
        {
            clearedPath = !spectrumPath.isEmpty();
            reset();
            currentChannel = channel;
        }

        if (!dataGen.processSamples(buffer, channel))
            return clearedPath;

        // The generator owns these bins. They remain valid until the next frame.
        dataGen.getSpectrumDecibels(); // Acknowledge the new frame.
        hasSpectrum = true;
        rebuildPath();
        return true;
    }

    const juce::Path& getPath() const noexcept { return spectrumPath; }

    // Call after a FIFO discontinuity, or to clear a stopped transport's trace.
    // Repaint after reset. All methods, including painting, use the same thread.
    void reset() noexcept
    {
        dataGen.reset();
        spectrumPath.clear();
        hasSpectrum = false;
        currentChannel = -1;
    }

private:
    void rebuildPath()
    {
        spectrumPath.clear();
        if (!hasSpectrum || currentSampleRate == 0.0 || plotBounds.isEmpty()
            || !std::isfinite(plotBounds.getX())
            || !std::isfinite(plotBounds.getY())
            || !std::isfinite(plotBounds.getRight())
            || !std::isfinite(plotBounds.getBottom()))
            return;

        const auto& bins = dataGen.getSpectrumDecibels();
        const auto maxFrequency = juce::jmin(20000.0, currentSampleRate * 0.5);
        const auto logRange = std::log(20000.0 / 20.0);
        const auto floorDb = dataGen.getFloorDecibels();

        // Merge bins landing in the same pixel column, retaining the peak.
        // No fixed 512-point scope array is required and narrow peaks survive.
        double previousColumn = -1.0;
        float peakDb = floorDb;
        float peakX = 0.0f;
        bool started = false;
        auto appendPoint = [&]
        {
            const auto y = juce::jmap(juce::jlimit(floorDb, 0.0f, peakDb),
                                     floorDb, 0.0f,
                                     plotBounds.getBottom(), plotBounds.getY());
            if (!started)
            {
                spectrumPath.startNewSubPath(peakX, y);
                started = true;
            }
            else
                spectrumPath.lineTo(peakX, y);
        };

        // Skip DC: log(0) is undefined and DC has no position on this axis.
        for (int bin = 1; bin < dataGen.getNumBins(); ++bin)
        {
            const auto frequency = dataGen.getBinFrequency(bin, currentSampleRate);
            if (frequency < 20.0)
                continue;
            if (frequency > maxFrequency)
                break;

            const auto relativeX = std::log(frequency / 20.0) / logRange
                                   * plotBounds.getWidth();
            const auto column = std::floor(relativeX);
            const auto x = plotBounds.getX() + static_cast<float>(relativeX);
            const auto db = bins[static_cast<size_t>(bin)];

            if (column != previousColumn)
            {
                if (previousColumn >= 0.0)
                    appendPoint();
                previousColumn = column;
                peakDb = db;
                peakX = x;
            }
            else if (db > peakDb)
            {
                peakDb = db;
                peakX = x;
            }
        }

        if (previousColumn >= 0.0)
        {
            appendPoint();
            // A single visible column otherwise contains only a move command.
            if (spectrumPath.getBounds().getWidth() == 0.0f)
                spectrumPath.lineTo(juce::jmin(plotBounds.getRight(), peakX + 1.0f),
                                   spectrumPath.getCurrentPosition().y);
        }
    }

    FFTDataGen<float> dataGen;
    juce::Path spectrumPath;
    juce::Rectangle<float> plotBounds;
    double currentSampleRate = 0.0;
    int currentChannel = -1;
    bool hasSpectrum = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PathProducer);
};
