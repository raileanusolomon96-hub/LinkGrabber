# LinkGrabber Notes

## Status
- **Current Status:** 🚧 Stage 0 (Research & Planning complete)
- **Version:** N/A
- **Type:** Utility (YouTube Audio Downloader)

## Lifecycle Timeline

- **2026-11-07:** Creative brief created
- **2026-11-07 (Stage 0):** Research & Planning complete - Architecture and plan documented (Complexity 5.0, phased)
- **2026-11-07 (Stage 0):** UI mockup v1 created (Minesweeper, 480x360)

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
