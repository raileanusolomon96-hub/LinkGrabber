# LinkGrabber Notes

## Status
- **Current Status:** 🚧 Stage 2 (Phase 2.0 drag-out spike code complete; host tests pending)
- **Version:** N/A
- **Type:** Utility (YouTube Audio Downloader)

## Lifecycle Timeline

- **2026-11-07:** Creative brief created
- **2026-11-07 (Stage 0):** Research & Planning complete - Architecture and plan documented (Complexity 5.0, phased)
- **2026-11-07 (Stage 0):** UI mockup v1 created (Minesweeper, 480x360)
- **2026-11-07 (Stage 0):** UI mockup v1 finalized; scaffolding generated (parameter-spec.md, v1-ui.html reference, native JUCE integration checklist). WebView PluginEditor/CMake templates intentionally skipped (D1).
- **2026-11-07 (Stage 1):** Foundation complete - CMake, passthrough processor, APVTS `format` (non-automatable), extras tree, placeholder editor

- **2026-11-07 (Stage 2.0):** Drag-out spike code written (temporary `FileHandle` in the placeholder editor, dummy WAV/MP3 generation, Show in Finder). Host tests pending.

## Known Issues

None

## Additional Notes

Successor concept to the removed LinkGrab plugin. See `.ideas/creative-brief.md`.

## Stage 0 Decisions

- Native JUCE UI (no WebView) so file drag-out comes from a real Component.
- `format` is an APVTS AudioParameterChoice, non-automatable. Link/output folder are extra state.
- History is a global file `~/Library/Application Support/LinkGrabber/history.json`.
- Tools: Homebrew discovery first; managed tool folder in Application Support later (phase 2.4). Not embedded in the signed bundle.
- Drag-out is unverified in every DAW: Phase 2.0 spike comes first. Per-host results table goes here.

## Phase 2.0 Drag-out Test Results

Spike: drag the "Drag test file" box in the editor (WAV/MP3 toggle selects the file; "Show in Finder" is the fallback). Files are generated in the output folder (default `~/Music/LinkGrabber/LinkGrabber drag test.wav|.mp3`). Test with a Debug build (to surface JUCE assertions) installed via `scripts/build-and-install.sh` (pattern #10), caches cleared (pattern #18). Drop target: an audio track / arrangement.

Dummy file notes: the WAV is real audio (1 s, 440 Hz, 24-bit stereo 44.1 kHz). JUCE has no MP3 encoder, so the "MP3" is ~1 s of SILENT, hand-assembled MPEG-1 Layer III frames (128 kbps, 44.1 kHz, 417-byte frames, header `FF FB 90 00` + zero payload). Whether each host decodes it is itself UNTESTED. If a host rejects it, replace the file by hand with a real MP3 (for example, in Terminal: `ffmpeg -f lavfi -i sine=frequency=440:duration=1 -b:a 320k "LinkGrabber drag test.mp3"`) in the output folder; the plugin only regenerates the file when it is missing (or tiny).

| Host / format | Drag result (WAV) | Drag result (MP3) | Notes | Date |
|---------------|-------------------|-------------------|-------|------|
| Standalone -> Finder | NOT YET TESTED | NOT YET TESTED | | |
| Logic Pro, AU | WORKS (user report) | WORKS (user report) | User confirmed dragging from the plugin into Logic works; WAV vs MP3 not separately reported. Release build, not Debug | 2026-11-07 |
| Ableton Live, VST3 | NOT YET TESTED | NOT YET TESTED | | |
| Ableton Live, AU | NOT YET TESTED | NOT YET TESTED | | |
| Reaper, VST3 | NOT YET TESTED | NOT YET TESTED | | |
| Reaper, AU | NOT YET TESTED | NOT YET TESTED | | |

Also record per host: JUCE assertion in Debug (Y/N), any second drag from one gesture (Y/N), whether "Show in Finder" fallback was needed.

- **2026-11-07 (Stage 2, Phase 2.0):** Drag-out confirmed working in Logic Pro (AU) by user. Other hosts untested; risk R1 retired for the primary host.
