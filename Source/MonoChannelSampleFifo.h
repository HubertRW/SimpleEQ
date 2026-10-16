/*
  ==============================================================================

    MonoChannelSampleFifo.h
    Created: 7 Aug 2026 9:45:33pm
    Author:  huber

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <vector>

/** A bounded single-producer/single-consumer queue of ONE channel's samples.

    Own this in the processor, not the editor. Construct before audio starts.
    Audio thread: push() and markDiscontinuity().
    Message thread: pull() and discardPending().
    Never use two producers or two consumers with the same instance.

    push() copies samples without allocation, locks, waiting, FFT or drawing.
    On overflow it keeps the prefix that fits and drops the remaining input.
    Gap markers travel WITH the samples, so pull() never joins samples across
    a gap. Reset PathProducer before feeding a result with discontinuity=true.

    The ring capacity and input channel stay fixed for the object's lifetime.
    reset() and destruction require BOTH threads to have stopped accessing it.
    Sample-rate/session metadata travels with each sample so the consumer can
    reject stale data without resetting or resizing the live ring.
*/

class MonoChannelSampleFifo
{
public:
    struct ReadResult
    {
        int numSamples = 0;
        bool discontinuity = false; // Gap BEFORE the first returned sample.
        double sampleRate = 0.0;
        std::uint64_t generation = 0;
    };

    // capacitySamples is usable sample capacity, NOT an FFT order or block size.
    // Channel indices are zero-based: 0 = left/mono, 1 = right.
    explicit MonoChannelSampleFifo(int channel = 0, int capacitySamples = 16384)
        : sourceChannel(juce::jmax(0, channel)),
          capacity(checkedCapacity(capacitySamples)),
          fifo(capacity + 1), // AbstractFifo reserves one slot to distinguish full/empty.
          samples(static_cast<size_t>(capacity + 1), 0.0f),
          gapBefore(static_cast<size_t>(capacity + 1), 0),
          formats(static_cast<size_t>(capacity + 1))
    {
        jassert(channel >= 0);
    }

    int getChannel() const noexcept { return sourceChannel; }
    int getCapacity() const noexcept { return capacity; }
    int getNumAvailableSamples() const noexcept { return fifo.getNumReady(); }

    // PRODUCER ONLY. Returns the number accepted. The rest was dropped.
    // Input is never modified. Empty buffers are harmless. A missing selected
    // channel counts as a gap, rather than silently switching to another channel.
    int push(const juce::AudioBuffer<float>& input, double sampleRate = 0.0,
             std::uint64_t generation = 0) noexcept
    {
        const int count = input.getNumSamples();
        if (count <= 0)
            return 0;
        if (sourceChannel >= input.getNumChannels())
        {
            producerGapPending = true;
            return 0;
        }

        int start1, size1, start2, size2;
        fifo.prepareToWrite(count, start1, size1, start2, size2);
        const auto* source = input.getReadPointer(sourceChannel);
        int offset = 0;
        auto copyRange = [&](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto index = static_cast<size_t>(start + i);
                samples[index] = source[offset++];
                formats[index] = { sampleRate, generation };
                gapBefore[index] = producerGapPending ? 1 : 0;
                producerGapPending = false;
            }
        };
        copyRange(start1, size1);
        copyRange(start2, size2);
        const int accepted = size1 + size2;

        // Publish only AFTER both sample data and gap flags have been copied.
        fifo.finishedWrite(accepted);
        if (accepted < count)
            producerGapPending = true;
        return accepted;
    }

    // PRODUCER ONLY. Marks the next accepted sample, without resetting shared
    // indices. Useful for transport jumps. Already queued samples stay intact.
    void markDiscontinuity() noexcept { producerGapPending = true; }

    // CONSUMER ONLY. May allocate when resizing destination, never call on the
    // audio thread. Returns at most maxSamples, stopping BEFORE an interior gap.
    // Destination becomes exactly one channel and numSamples long, so it can be
    // passed directly to PathProducer::InitPath(destination, 0) without padding.
    ReadResult pull(juce::AudioBuffer<float>& destination, int maxSamples = 2048)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(juce::jlimit(0, capacity, maxSamples),
                           start1, size1, start2, size2);
        int count = size1 + size2;
        if (count == 0)
        {
            destination.setSize(1, 0, false, false, true);
            return {};
        }

        auto indexAt = [&](int offset)
        {
            return static_cast<size_t>(offset < size1
                ? start1 + offset : start2 + offset - size1);
        };
        const auto format = formats[indexAt(0)];
        const bool discontinuity = consumerGapPending || gapBefore[indexAt(0)] != 0
            || format.sampleRate != lastReadFormat.sampleRate
            || format.generation != lastReadFormat.generation;
        for (int i = 1; i < count; ++i)
        {
            const auto nextFormat = formats[indexAt(i)];
            if (gapBefore[indexAt(i)] != 0
                || nextFormat.sampleRate != format.sampleRate
                || nextFormat.generation != format.generation)
            {
                count = i;
                break;
            }
        }

        destination.setSize(1, count, false, false, true);
        auto* output = destination.getWritePointer(0);
        for (int i = 0; i < count; ++i)
            output[i] = samples[indexAt(i)];
        // Release slots only AFTER the consumer's private copy is complete.
        fifo.finishedRead(count);
        consumerGapPending = false;
        lastReadFormat = format;
        return { count, discontinuity, format.sampleRate, format.generation };
    }

    // CONSUMER ONLY. Discard currently queued stale samples on editor reopening.
    // The producer can keep running. Samples published after this snapshot remain.
    // The next nonempty pull is marked discontinuous even when nothing was queued.
    int discardPending() noexcept
    {
        const int count = fifo.getNumReady();
        fifo.finishedRead(count);
        consumerGapPending = true;
        return count;
    }

    // BOTH ENDPOINTS MUST BE QUIESCENT. Not a concurrent clear operation.
    // Do not call from prepareToPlay while a live editor is draining the FIFO.
    void reset() noexcept
    {
        fifo.reset();
        producerGapPending = false;
        consumerGapPending = true;
    }

private:
    static int checkedCapacity(int requested) noexcept
    {
        // Keep capacity+1 and allocation sizes bounded even in release builds.
        constexpr int maximumCapacity = 1 << 20;
        jassert(requested >= 1 && requested <= maximumCapacity);
        return juce::jlimit(1, maximumCapacity, requested);
    }

    struct StreamFormat
    {
        double sampleRate = 0.0;
        std::uint64_t generation = 0;
    };

    const int sourceChannel;
    const int capacity;
    juce::AbstractFifo fifo;
    std::vector<float> samples;
    std::vector<std::uint8_t> gapBefore;
    std::vector<StreamFormat> formats;
    StreamFormat lastReadFormat; // Consumer-owned.
    bool producerGapPending = false; // Accessed only by the producer.
    bool consumerGapPending = true; // Accessed only by the consumer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonoChannelSampleFifo);
};


using MCSF = MonoChannelSampleFifo;
