# Analyzer integration tests (Windows)

From the repository root run:

```cmd
Tests\RunAnalyzerTests.cmd
```

Requirements: Visual Studio C++ tools, the generated VisualStudio2022 solution, and JUCE modules at `%USERPROFILE%\JUCE\modules` (override with `JUCE_MODULES`). The generated solution also needs its normal JUCE paths configured.

The runner builds the actual Debug VST3 and standalone targets, then links an integration executable against the project's Shared Code library. It does not copy or stub the processor, editor, FIFO, FFT or path code. The test acts as a small host: it calls the real processor lifecycle and audio callbacks, creates the real editor, pumps Windows messages so JUCE's real timer runs, and renders the real UI. It follows JUCE's `editorBeingDeleted()` ownership protocol before closing an editor.

Artifacts are written to `%TEMP%\SimpleEQ-AnalyzerTests`, including `analyzer.png`. The runner does not install the VST3 into a system plugin directory or play audio through speakers.

## Checks

- Stereo post-EQ FIFO samples exactly equal the processed output.
- Bypass leaves audio untouched but still captures analyzer data.
- Zero-length callbacks are harmless.
- Unchanged audio callbacks allocate no CRT heap memory, including queue overflow.
- Queues remain bounded while the editor is closed.
- Real GUI timer displays distinct 1500 Hz left and 3000 Hz right peaks without parameter changes.
- Quarter-scale signal maps to the expected dBFS height.
- Editor rendering, resizing, minimum-size bounds and cached-path restoration.
- EQ automation changes the measured output spectrum.
- Silence reaches the floor and missing callbacks clear stale traces after 500 ms.
- Queued old-session audio is rejected after sample-rate changes.
- Correct frequency mapping and Nyquist clipping at 32 kHz.
- Editor close/reopen resumes only with fresh audio.
- Mono/stereo transitions and 96 kHz processing.
- Transport continuity, seeks, and input monitoring with stopped transport.
- Audio-thread reprepare while the actual GUI timer runs.
- Release clears the active display.

## Analyzer behavior

The processor permanently owns the left/right sample queues. Each queued sample has a rate and session generation tag. Preparation/release publish a consistent atomic state without resetting or resizing a live queue. The editor drains bounded amounts on its 60 Hz timer and rejects stale generations. FFTs use 4096 samples, non-overlapping frames. The plot uses a fixed 20 Hz to 20 kHz axis, with spectrum dBFS on the left and EQ gain dB on the right. Mono uses the left trace.

This validates the real processor/editor workflow but is not a third-party DAW compatibility test. Parameter changes still use JUCE's existing allocating filter-design helpers on the audio thread; unchanged blocks skip those designs. Analyzer capture itself does not allocate. Spectrum smoothing and overlapping FFT frames are not implemented.
