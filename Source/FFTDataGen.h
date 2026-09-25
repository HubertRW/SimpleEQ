/*
  ==============================================================================

    FFTDataGen.h
    Created: 3 Aug 2026 10:14:54pm
    Author:  huber

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

// Retained for compatibility with the original order names.
enum {
    fftOrder = 10,
    fftSize = 1 << fftOrder,
    scopeSize = 512,
    Order2048 = 11,
    Order4096 = 12,
    Order8192 = 13
};

/** Produces a one-sided, Hann-windowed amplitude spectrum in dBFS.

    Own and use this object on ONE analysis/message thread. Feed it samples
    drained from a separate audio-thread SPSC FIFO, never directly from a
    concurrently running processBlock. This class is not a thread-safe FIFO.
    FFT work belongs outside the audio callback.

    Frames are non-overlapping. Input may arrive in any block size. When several
    frames arrive before the display updates, only the latest spectrum is kept.
    A full-scale, bin-centred sine reads approximately 0 dBFS at its peak bin.
    This is an amplitude spectrum, not a power spectral density measurement.
*/
template <typename TYPE = float>
class FFTDataGen
{
    static_assert(std::is_same<TYPE, float>::value,
                  "JUCE's real FFT uses float samples. Use FFTDataGen<float>.");

public:
    explicit FFTDataGen(int order = fftOrder)
        : transformOrder(validatedOrder(order)),
          transformSize(1 << transformOrder),
          forwardFFT(transformOrder),
          window(static_cast<size_t>(transformSize),
                 juce::dsp::WindowingFunction<float>::hann, false),
          sampleFrame(static_cast<size_t>(transformSize), 0.0f),
          fftData(static_cast<size_t>(transformSize) * 2, 0.0f),
          spectrum(static_cast<size_t>(transformSize / 2 + 1), floorDecibels)
    {
        // Measure the actual window sum rather than assuming a Hann gain of 0.5.
        std::fill(fftData.begin(), fftData.begin() + transformSize, 1.0f);
        window.multiplyWithWindowingTable(fftData.data(),
                                         static_cast<size_t>(transformSize));
        double windowSum = 0.0;
        for (int i = 0; i < transformSize; ++i)
            windowSum += fftData[static_cast<size_t>(i)];
        inverseWindowSum = static_cast<float>(1.0 / windowSum);
        reset();
    }

    int getFFTSize() const noexcept { return transformSize; }
    int getFFTOrder() const noexcept { return transformOrder; }
    int getNumBins() const noexcept { return transformSize / 2 + 1; }
    float getFloorDecibels() const noexcept { return floorDecibels; }

    // Bin 0 is DC, bin N/2 is Nyquist. The caller supplies the current sample rate.
    double getBinFrequency(int bin, double sampleRate) const noexcept
    {
        if (bin < 0 || bin >= getNumBins()
            || !std::isfinite(sampleRate) || sampleRate <= 0.0)
            return 0.0;
        return static_cast<double>(bin) * sampleRate / transformSize;
    }

    // Reset on transport discontinuities, dropped input, or sample-rate changes.
    // Call only on the owning thread, not concurrently with sample ingestion.
    void reset() noexcept
    {
        std::fill(sampleFrame.begin(), sampleFrame.end(), 0.0f);
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::fill(spectrum.begin(), spectrum.end(), floorDecibels);
        writeIndex = 0;
        newSpectrumAvailable = false;
    }

    // Returns true if at least one complete frame was produced by this call.
    bool processSamples(const float* samples, int numSamples)
    {
        if (samples == nullptr || numSamples <= 0)
            return false;

        bool producedFrame = false;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = samples[i];
            sampleFrame[static_cast<size_t>(writeIndex++)] =
                std::isfinite(sample) ? sample : 0.0f;

            if (writeIndex == transformSize)
            {
                generateSpectrum();
                writeIndex = 0;
                producedFrame = true;
            }
        }
        return producedFrame;
    }

    bool processSamples(const juce::AudioBuffer<float>& buffer, int channel)
    {
        if (channel < 0 || channel >= buffer.getNumChannels())
            return false;
        return processSamples(buffer.getReadPointer(channel), buffer.getNumSamples());
    }

    bool hasNewSpectrum() const noexcept { return newSpectrumAvailable; }

    // Valid until destruction, contents change on the next full frame or reset.
    // Consume immediately on the owning thread. Reading acknowledges the frame.
    const std::vector<float>& getSpectrumDecibels() noexcept
    {
        newSpectrumAvailable = false;
        return spectrum;
    }

private:
    static int validatedOrder(int order) noexcept
    {
        // Bound shifts, allocation sizes and analysis cost in release builds too.
        jassert(order >= fftOrder && order <= Order8192);
        return juce::jlimit(static_cast<int>(fftOrder),
                            static_cast<int>(Order8192), order);
    }

    void generateSpectrum()
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::copy(sampleFrame.begin(), sampleFrame.end(), fftData.begin());
        window.multiplyWithWindowingTable(fftData.data(),
                                         static_cast<size_t>(transformSize));
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data(), true);

        for (int bin = 0; bin < getNumBins(); ++bin)
        {
            // Interior bins represent positive and negative frequency pairs.
            // DC and Nyquist occur only once and must not be doubled.
            const float scale = (bin == 0 || bin == transformSize / 2)
                                  ? inverseWindowSum : 2.0f * inverseWindowSum;
            const auto amplitude = fftData[static_cast<size_t>(bin)] * scale;
            spectrum[static_cast<size_t>(bin)] = std::isfinite(amplitude)
                ? juce::Decibels::gainToDecibels(amplitude, floorDecibels)
                : floorDecibels;
        }
        newSpectrumAvailable = true;
    }

    static constexpr float floorDecibels = -100.0f;
    const int transformOrder;
    const int transformSize;
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> sampleFrame;
    std::vector<float> fftData;
    std::vector<float> spectrum;
    float inverseWindowSum = 1.0f;
    int writeIndex = 0;
    bool newSpectrumAvailable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFTDataGen);
};
