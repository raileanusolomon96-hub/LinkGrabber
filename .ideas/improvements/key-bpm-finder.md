# LinkGrabber - Key and BPM Finder

**Created:** 2026-11-08
**Type:** Feature
**Aspect:** Features (engine + UI)
**Version Impact:** MINOR (1.0.0 -> 1.1.0)
**Restore point:** git tag `linkgrabber-v1.0.0`, branch `backup/linkgrabber-v1.0.0`, built plugins in `backups/LinkGrabber/`

## Current State

LinkGrabber downloads audio (MP3 320 / WAV 24-bit) and offers it for dragging. It knows nothing about the music in the file.

## Proposed Change

After every download finishes, LinkGrabber analyses the file and finds its **Key** (e.g. "A minor") and **BPM** (e.g. 128).
- Runs automatically; no button.
- Shown on the result tile and in every History row.
- Written into the file name: `Title [id] Am 128bpm.wav`.
- Key written as musical names (A minor, C# major), not Camelot.
- Only LinkGrabber downloads are analysed (no drop zone for other files).

## Rationale

Producers sampling a track need tempo and key to match it to the project. Finding them at download time saves a separate tool.

## Implementation Notes

- Analysis in C++ inside the plugin (no new external tool): decode with juce::AudioFormatManager, mono downmix, downsample.
- BPM: onset-strength envelope + autocorrelation, with octave (half/double) correction toward 70-180.
- Key: chromagram + Krumhansl-Schmuckler profile correlation (24 candidates).
- Runs on the DownloadManager worker thread, new state `analysing` between `converting` and `done`. Cancel must stop it.
- The file rename must happen before `done` so the drag path, history entry and tile all hold the final path. Handle name collisions and illegal characters.
- History entries gain optional `key` and `bpm` fields (old entries stay valid; show nothing, or analyse lazily later).
- UI: Windows 95 style labels on tile and rows; board/smiley show an "analysing" state. Needs a UI mockup revision (v2).
- Analysis failure (silence, too short) must never fail the download: keep the file, show "Key/BPM unknown".
- Research first: accuracy of the approach, MP3 decode via JUCE, runtime for 60 min files (cap the analysed span).

## Backward Compatibility

Non-breaking. Same plugin ID, same parameter (`format`). Old history.json entries load with no key/bpm.

## Testing Criteria

- [ ] Known-tempo/known-key test files give correct BPM (within 1) and key
- [ ] Half/double-tempo errors corrected on typical 4/4 tracks
- [ ] Silence or very short audio: download succeeds, key/bpm shown as unknown
- [ ] File name rename works, drag-out and history use the new path
- [ ] Cancel during analysis stops cleanly; pluginval strictness 10 passes
- [ ] Old history.json still loads

## Success Metrics

A real YouTube download shows a plausible Key and BPM within a few seconds, in the tile, History and file name, and drags into Logic.
