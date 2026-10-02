// Integration tests for the actual processor, editor, timer and renderer.
// No copied implementations: link the solution's Debug Shared Code library.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PluginEditor.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <crtdbg.h>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
    std::cout << "PASS " << message << std::endl;
}

void pump(int milliseconds = 40)
{
    const auto start = juce::Time::getMillisecondCounter();
    do
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        juce::Thread::sleep(1);
    }
    while (juce::Time::getMillisecondCounter() - start < static_cast<juce::uint32>(milliseconds));
}

struct EditorDeleter
{
    void operator()(juce::AudioProcessorEditor* editor) const
    {
        if (editor == nullptr)
            return;
        editor->processor.editorBeingDeleted(editor);
        delete editor;
    }
};
using EditorOwner = std::unique_ptr<juce::AudioProcessorEditor, EditorDeleter>;

ResponseCurveComponent& display(juce::AudioProcessorEditor& editor)
{
    for (auto* child : editor.getChildren())
        if (auto* response = dynamic_cast<ResponseCurveComponent*>(child))
            return *response;
    throw std::runtime_error("Real editor has no response component");
}

void prepare(SimpleEQAudioProcessor& processor, double rate, int channels = 2)
{
    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference(0) = channels == 1
        ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference(0) = layout.inputBuses[0];
    if (!processor.setBusesLayout(layout))
        throw std::runtime_error("Requested bus layout was rejected");
    processor.setRateAndBufferSizeDetails(rate, 2048);
    processor.prepareToPlay(rate, 2048);
}

void feed(SimpleEQAudioProcessor& processor, double leftFrequency = 1500.0,
          double rightFrequency = 3000.0, bool dispatch = true, int blocks = 30,
          bool silent = false)
{
    const int sizes[] = { 64, 511, 1024, 37, 2048 };
    juce::MidiBuffer midi;
    juce::int64 sequence = 0;
    const auto rate = processor.getSampleRate();
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), sizes[block % 5]);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, silent ? 0.0f : static_cast<float>(0.25 * std::sin(
                    2.0 * juce::MathConstants<double>::pi * (ch == 0 ? leftFrequency : rightFrequency)
                    * static_cast<double>(sequence + i) / rate)));
        processor.processBlock(buffer, midi);
        sequence += buffer.getNumSamples();
        if (dispatch)
            pump(3);
    }
    if (dispatch)
        pump(60);
}

juce::Point<float> peak(const juce::Path& path)
{
    juce::Point<float> result(0.0f, 1.0e10f);
    juce::Path::Iterator iterator(path);
    while (iterator.next())
    {
        if (!std::isfinite(iterator.x1) || !std::isfinite(iterator.y1))
            throw std::runtime_error("Nonfinite rendered geometry");
        if (iterator.y1 < result.y)
            result = { iterator.x1, iterator.y1 };
    }
    return result;
}

bool peakAt(ResponseCurveComponent& component, int channel, double frequency)
{
    const auto point = peak(component.getSpectrumPath(channel));
    const double expected = 42.0 + (component.getWidth() - 86.0)
        * std::log(frequency / 20.0) / std::log(1000.0);
    return !component.getSpectrumPath(channel).isEmpty() && std::abs(point.x - expected) < 3.0;
}

thread_local bool trackAllocations = false;
thread_local int allocationCount = 0;
int allocationHook(int kind, void*, size_t, int, long, const unsigned char*, int)
{
    if (trackAllocations && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC))
        ++allocationCount;
    return 1;
}

struct TestPlayHead : juce::AudioPlayHead
{
    juce::int64 sample = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setTimeInSamples(sample);
        result.setIsPlaying(playing);
        return result;
    }
};
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        SimpleEQAudioProcessor processor;
        prepare(processor, 48000.0);
        auto state = processor.getAnalyzerState();
        require(state.stable && state.sampleRate == 48000.0 && state.channels == 2
                && state.generation % 2 == 0, "prepare publishes a stable stereo analyzer session");

        // Directly check that the tap contains output audio, not the input signal.
        auto* gain = processor.apvts.getParameter("Peak Gain");
        gain->setValueNotifyingHost(gain->convertTo0to1(12.0f));
        juce::AudioBuffer<float> output(2, 512), captured;
        output.clear(); output.setSample(0, 0, 0.5f); output.setSample(1, 0, -0.25f);
        juce::MidiBuffer midi;
        processor.processBlock(output, midi);
        bool exact = true;
        for (int channel = 0; channel < 2; ++channel)
        {
            auto read = processor.pullAnalyzerSamples(channel, captured, 512);
            exact = exact && read.numSamples == 512 && read.sampleRate == 48000.0
                    && read.generation == state.generation;
            for (int i = 0; i < read.numSamples; ++i)
                exact = exact && captured.getSample(0, i) == output.getSample(channel, i);
        }
        require(exact, "post-EQ FIFO samples exactly match both processed output channels");
        processor.discardAnalyzerSamples();
        output.clear();
        output.setSample(0, 0, 0.5f);
        processor.processBlockBypassed(output, midi);
        const auto bypassed = processor.pullAnalyzerSamples(0, captured, 512);
        require(bypassed.numSamples == 512 && output.getSample(0, 0) == 0.5f
                && output.getSample(0, 1) == 0.0f && captured.getSample(0, 0) == 0.5f,
                "bypassed processing leaves audio untouched and still supplies the analyzer");
        processor.discardAnalyzerSamples();
        juce::AudioBuffer<float> emptyBlock(2, 0);
        processor.processBlock(emptyBlock, midi);
        require(processor.getAvailableAnalyzerSamples(0) == 0,
                "zero-length host callbacks do not add analyzer samples");
        gain->setValueNotifyingHost(gain->convertTo0to1(0.0f));
        processor.processBlock(output, midi); // Settle parameter-related allocations first.
        processor.discardAnalyzerSamples();

        auto oldHook = _CrtSetAllocHook(allocationHook);
        trackAllocations = true;
        for (int block = 0; block < 40; ++block)
            processor.processBlock(output, midi);
        trackAllocations = false;
        _CrtSetAllocHook(oldHook);
        require(allocationCount == 0, "unchanged processBlock plus analyzer capture makes no CRT allocations, including overflow");
        require(processor.getAvailableAnalyzerSamples(0) == 16384,
                "closed editor leaves bounded queues rather than blocking audio");

        EditorOwner editor(processor.createEditorIfNeeded());
        require(editor != nullptr, "actual processor creates its real editor");
        auto* response = &display(*editor);
        pump();
        require(response->getSpectrumPath(0).isEmpty() && processor.getAvailableAnalyzerSamples(0) == 0,
                "editor opening discards stale pre-open samples");
        feed(processor);
        require(peakAt(*response, 0, 1500.0) && peakAt(*response, 1, 3000.0),
                "real message-thread timer renders distinct left/right frequency peaks without parameter changes");
        const float expectedY = 24.0f + (response->getHeight() - 46.0f) * (12.0412f / 100.0f);
        require(std::abs(peak(response->getSpectrumPath(0)).y - expectedY) < 2.0f,
                "display amplitude matches quarter-scale audio on its dBFS axis");

        if (argc > 1)
        {
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            juce::File destination(juce::String::fromUTF8(argv[1]));
            destination.deleteFile();
            juce::FileOutputStream stream(destination);
            require(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream),
                    "real editor paints a complete PNG snapshot");
        }
        editor->setSize(720, 450);
        require(peakAt(*response, 0, 1500.0), "resize remaps cached spectrum without new audio");
        editor->setSize(100, 100);
        pump();
        editor->createComponentSnapshot(editor->getLocalBounds());
        editor->setSize(1000, 600);
        require(peakAt(*response, 0, 1500.0), "minimum-size empty plot can restore its cached spectrum");

        // A real parameter update must affect the measured post-EQ trace.
        auto* frequency = processor.apvts.getParameter("Peak Freq");
        frequency->setValueNotifyingHost(frequency->convertTo0to1(1500.0f));
        gain->setValueNotifyingHost(gain->convertTo0to1(12.0f));
        feed(processor);
        require(peak(response->getSpectrumPath(0)).y < expectedY - 10.0f,
                "EQ automation changes the measured post-EQ spectrum");
        gain->setValueNotifyingHost(gain->convertTo0to1(0.0f));
        feed(processor, 1500, 3000, true, 40, true);
        require(peak(response->getSpectrumPath(0)).y > response->getHeight() - 30.0f,
                "silence falls to analyzer floor after filter state settles");
        feed(processor);
        pump(650);
        require(response->getSpectrumPath(0).isEmpty() && response->getSpectrumPath(1).isEmpty(),
                "trace clears after the host stops delivering callbacks");

        // Leave old-rate samples queued and reprepare while the editor still exists.
        feed(processor, 1500, 3000, false, 10);
        processor.releaseResources();
        prepare(processor, 32000.0);
        pump();
        require(response->getSpectrumPath(0).isEmpty(), "rate transition rejects queued old-session audio");
        feed(processor, 1000, 2000);
        require(peakAt(*response, 0, 1000.0) && peakAt(*response, 1, 2000.0),
                "live editor uses 32kHz metadata without out-of-Nyquist filter assertions");
        const double nyquistX = 42.0 + (response->getWidth() - 86.0) * std::log(16000.0 / 20.0) / std::log(1000.0);
        require(response->getSpectrumPath(0).getBounds().getRight() <= nyquistX + 0.01,
                "low-rate spectrum stops at Nyquist on the fixed frequency axis");

        editor.reset();
        feed(processor, 1000, 2000, false, 40);
        editor.reset(processor.createEditorIfNeeded()); response = &display(*editor);
        pump();
        require(response->getSpectrumPath(0).isEmpty(), "editor reopen does not show stale closed-editor audio");
        feed(processor, 1000, 2000);
        require(peakAt(*response, 0, 1000.0), "fresh audio resumes after editor reopen");

        processor.releaseResources();
        prepare(processor, 48000.0, 1);
        pump(); feed(processor);
        require(peakAt(*response, 0, 1500.0) && response->getSpectrumPath(1).isEmpty(),
                "mono layout displays one trace and never reads missing channel one");
        processor.releaseResources(); prepare(processor, 96000.0, 2);
        pump(); feed(processor, 3000, 6000);
        require(peakAt(*response, 0, 3000.0) && peakAt(*response, 1, 6000.0),
                "96kHz stereo resumes correctly after mono layout");

        // Test transport metadata using the processor's real playhead API.
        editor.reset(); processor.discardAnalyzerSamples();
        TestPlayHead playhead;
        processor.setPlayHead(&playhead);
        processor.processBlock(output, midi);
        processor.pullAnalyzerSamples(0, captured, 512);
        playhead.sample = 512;
        processor.processBlock(output, midi);
        require(!processor.pullAnalyzerSamples(0, captured, 512).discontinuity,
                "contiguous playing transport does not create artificial gaps");
        playhead.sample = 90000;
        processor.processBlock(output, midi);
        require(processor.pullAnalyzerSamples(0, captured, 512).discontinuity,
                "transport seek marks the next FFT segment discontinuous");
        playhead.playing = false;
        processor.processBlock(output, midi);
        processor.pullAnalyzerSamples(0, captured, 512);
        processor.processBlock(output, midi);
        require(!processor.pullAnalyzerSamples(0, captured, 512).discontinuity,
                "stopped transport still supports continuous live-input monitoring");
        processor.setPlayHead(nullptr);

        editor.reset(processor.createEditorIfNeeded()); response = &display(*editor);
        // Producer lifecycle changes race against the actual GUI timer, not a fake consumer.
        std::atomic<bool> done { false };
        std::thread audio([&]
        {
            for (int pass = 0; pass < 12; ++pass)
            {
                processor.releaseResources();
                const double rate = pass % 2 == 0 ? 48000.0 : 96000.0;
                processor.setRateAndBufferSizeDetails(rate, 2048);
                processor.prepareToPlay(rate, 2048);
                feed(processor, rate / 32.0, rate / 16.0, false, 30);
                juce::Thread::sleep(2);
            }
            done.store(true);
        });
        while (!done.load()) pump(10);
        audio.join();
        pump();
        feed(processor, 3000, 6000);
        require(peakAt(*response, 0, 3000.0), "audio-thread reprepare stress recovers with the correct final-session geometry");
        processor.releaseResources(); pump();
        require(response->getSpectrumPath(0).isEmpty() && response->getSpectrumPath(1).isEmpty(),
                "releaseResources clears the active editor safely");
        editor.reset();
        std::cout << "ALL PROCESSOR/EDITOR INTEGRATION CHECKS PASSED" << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        trackAllocations = false;
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
